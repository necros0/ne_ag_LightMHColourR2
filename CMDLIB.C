// cmdlib.c

#include "cmdlib.h"
#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/stat.h>
#include <math.h>

#ifdef WIN32
#include <direct.h>
#include "windows.h"
#endif

#ifdef NeXT
#include <libc.h>
#endif

#define PATHSEPERATOR   '/'

// set these before calling CheckParm
int myargc;
char **myargv;

char		com_token[1024];
qboolean	com_eof;

qboolean		archive;
char			archivedir[1024];


#define	 DYNSAMPLES 3	       // # Dynamic samples

typedef struct timeinfo_s
{
	double   PrevTime, SampTime[DYNSAMPLES], STime, DTime;
	double   SampAPercent[DYNSAMPLES], DAPercent, PrevAPercent;
	int      DSamples;
	char     TextStr[80 + 1];
	qboolean PrintActive, Progressive;
	int    BarPrev;
	double BarPrevTime;
	double FARSample[DYNSAMPLES];
	int    FARDSamples, FARHiResCnt;
} timeinfo_t;

timeinfo_t *timeinfo;

void InitPercents (int numthreads)
{
	int i;
	int j;

	timeinfo = (timeinfo_t *) malloc (numthreads * sizeof (timeinfo_t));

	for (i = 0; i < numthreads; i++)
	{
		timeinfo[i].BarPrev = -1;
		timeinfo[i].BarPrevTime = 0;
		timeinfo[i].DAPercent = 0;
		timeinfo[i].DSamples = 0;
		timeinfo[i].DTime = 0;
		timeinfo[i].PrevAPercent = 0;
		timeinfo[i].PrevTime = 0;
		timeinfo[i].PrintActive = false;
		timeinfo[i].Progressive = false;

		for (j = 0; j < DYNSAMPLES; j++)
		{
			timeinfo[i].SampAPercent[j] = 0;
			timeinfo[i].SampTime[j] = 0;
			timeinfo[i].FARSample[j] = 0;
		}

		timeinfo[i].FARDSamples = 0;
		timeinfo[i].FARHiResCnt = 0;
		timeinfo[i].STime = 0;
		timeinfo[i].TextStr[0] = 0;
	}
}


/*
=================
Error

For abnormal program terminations
=================
*/
void Error (char *error, ...)
{
	va_list argptr;

	logprintf ("************ ERROR ************\n");

	va_start (argptr, error);
	logvprintf (error, argptr);
	va_end (argptr);
	logprintf ("\n");
	ErrorExit ();
}


/*

qdir will hold the path up to the quake directory, including the slash

  f:\quake\
  /raid/quake/

gamedir will hold qdir + the game directory (id1, id2, etc)

  */

char		qdir[1024];
char		gamedir[1024];

void SetQdirFromPath (char *path)
{
	char	temp[1024];
	char	*c;

	if (! (path[0] == '/' || path[0] == '\\' || path[1] == ':'))
	{
		// path is partial
		Q_getwd (temp);
		strcat (temp, path);
		path = temp;
	}

	// search for "quake" in path

	for (c = path; *c; c++)
		if (!Q_strncasecmp (c, "quake", 5))
		{
			strncpy (qdir, path, c + 6 - path);
			printf ("qdir: %s\n", qdir);
			c += 6;

			while (*c)
			{
				if (*c == '/' || *c == '\\')
				{
					strncpy (gamedir, path, c + 1 - path);
					printf ("gamedir: %s\n", gamedir);
					return;
				}

				c++;
			}

			Error ("No gamedir in %s", path);
			return;
		}

	Error ("SeetQdirFromPath: no 'quake' in %s", path);
}

char *ExpandPath (char *path)
{
	static char full[1024];

	if (!qdir)
		Error ("ExpandPath called without qdir set");

	if (path[0] == '/' || path[0] == '\\' || path[1] == ':')
		return path;

	sprintf (full, "%s%s", qdir, path);
	return full;
}

char *ExpandPathAndArchive (char *path)
{
	char	*expanded;
	char	archivename[1024];

	expanded = ExpandPath (path);

	if (archive)
	{
		sprintf (archivename, "%s/%s", archivedir, path);
		QCopyFile (expanded, archivename);
	}

	return expanded;
}


char *copystring (char *s)
{
	char	*b;
	b = malloc (strlen (s) + 1);
	strcpy (b, s);
	return b;
}



/*
================
I_FloatTime
================
*/
double I_FloatTime (void)
{
	struct _timeb timebuffer;

	_ftime (&timebuffer);

	return (double) timebuffer.time + (timebuffer.millitm / 1000.0);
#if 0
	// more precise, less portable
	struct timeval tp;
	struct timezone tzp;
	static int		secbase;

	gettimeofday (&tp, &tzp);

	if (!secbase)
	{
		secbase = tp.tv_sec;
		return tp.tv_usec / 1000000.0;
	}

	return (tp.tv_sec - secbase) + tp.tv_usec / 1000000.0;
#endif
}

void Q_getwd (char *out)
{
#ifdef WIN32
	_getcwd (out, 256);
	strcat (out, "\\");
#else
	getwd (out);
#endif
}


void Q_mkdir (char *path)
{
#ifdef WIN32

	if (_mkdir (path) != -1)
		return;

#else

	if (mkdir (path, 0777) != -1)
		return;

#endif

	if (errno != EEXIST)
		Error ("mkdir %s: %s", path, strerror (errno));
}

/*
============
FileTime

returns -1 if not present
============
*/
int	FileTime (char *path)
{
	struct	stat	buf;

	if (stat (path, &buf) == -1)
		return -1;

	return buf.st_mtime;
}



/*
==============
COM_Parse

Parse a token out of a string
==============
*/
char *COM_Parse (char *data)
{
	int		c;
	int		len;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

	// skip whitespace
skipwhite:

	while ((c = *data) <= ' ')
	{
		if (c == 0)
		{
			com_eof = true;
			return NULL;			// end of file;
		}

		data++;
	}

	// skip // comments
	if (c == '/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;

		goto skipwhite;
	}


	// handle quoted strings specially
	if (c == '\"')
	{
		data++;

		do
		{
			c = *data++;

			if (c == '\"')
			{
				com_token[len] = 0;
				return data;
			}

			com_token[len] = c;
			len++;
		}
		while (1);
	}

	// parse single characters
	if (c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':')
	{
		com_token[len] = c;
		len++;
		com_token[len] = 0;
		return data + 1;
	}

	// parse a regular word
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;

		if (c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':')
			break;
	}
	while (c > 32);

	com_token[len] = 0;
	return data;
}


#ifdef not
int Q_strncasecmp (char *s1, char *s2, int n)
{
	int		c1, c2;

	while (1)
	{
		c1 = *s1++;
		c2 = *s2++;

		if (!n--)
			return 0;		// strings are equal until end point

		if (c1 != c2)
		{
			if (c1 >= 'a' && c1 <= 'z')
				c1 -= ('a' - 'A');

			if (c2 >= 'a' && c2 <= 'z')
				c2 -= ('a' - 'A');

			if (c1 != c2)
				return -1;		// strings not equal
		}

		if (!c1)
			return 0;		// strings are equal
	}

	return -1;
}

int Q_strcasecmp (char *s1, char *s2)
{
	return Q_strncasecmp (s1, s2, 99999);
}
#endif


char *strupr (char *start)
{
	char	*in;
	in = start;

	while (*in)
	{
		*in = toupper (*in);
		in++;
	}

	return start;
}

char *strlower (char *start)
{
	char	*in;
	in = start;

	while (*in)
	{
		*in = tolower (*in);
		in++;
	}

	return start;
}


/*
=============================================================================

						MISC FUNCTIONS

=============================================================================
*/


/*
=================
CheckParm

Checks for the given parameter in the program's command line arguments
Returns the argument number (1 to argc-1) or 0 if not present
=================
*/
int CheckParm (char *check)
{
	int             i;

	for (i = 1; i < myargc; i++)
	{
		if (!Q_strcasecmp (check, myargv[i]))
			return i;
	}

	return 0;
}



/*
================
filelength
================
*/
int filelength (FILE *f)
{
	int		pos;
	int		end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

void GetFTime (char *filename, long *ftime)
{
	struct _stat fstat;

	if (_stat (filename, &fstat) != 0)
		Error ("Can't access file %s", filename);

	*ftime = fstat.st_mtime;
}

void WriteChk (char *filename)
{
	/* Check writability */
	if (AccessFile (filename, 2) != 0)
		Error ("Error opening %s: %s", filename, strerror (errno));
}

int AccessFile (char *filename, int AccessMode)
{
	/* Check writability */
	return _access (filename, AccessMode);
}

FILE *SafeOpenWrite (char *filename)
{
	FILE	*f;

	f = fopen (filename, "wb");

	if (!f)
		Error ("Error opening %s: %s", filename, strerror (errno));

	return f;
}

FILE *SafeOpenRead (char *filename)
{
	FILE	*f;

	f = fopen (filename, "rb");

	if (!f)
		Error ("Error opening %s: %s", filename, strerror (errno));

	return f;
}


void SafeRead (FILE *f, void *buffer, int count)
{
	if (fread (buffer, 1, count, f) != (size_t) count)
		Error ("File read failure");
}


void SafeWrite (FILE *f, void *buffer, int count)
{
	if (fwrite (buffer, 1, count, f) != (size_t) count)
		Error ("File write failure");
}



/*
==============
LoadFile
==============
*/
int    LoadFile (char *filename, void **bufferptr)
{
	FILE	*f;
	int    length;
	void    *buffer;

	f = SafeOpenRead (filename);
	length = filelength (f);
	buffer = malloc (length + 1);
	((char *) buffer) [length] = 0;
	SafeRead (f, buffer, length);
	fclose (f);

	*bufferptr = buffer;
	return length;
}


/*
==============
SaveFile
==============
*/
void    SaveFile (char *filename, void *buffer, int count)
{
	FILE	*f;

	f = SafeOpenWrite (filename);
	SafeWrite (f, buffer, count);
	fclose (f);
}



void DefaultExtension (char *path, char *extension)
{
	char    *src;
	//
	// if path doesn't have a .EXT, append extension
	// (extension should include the .)
	//
	src = path + strlen (path) - 1;

	while (*src != PATHSEPERATOR && src != path)
	{
		if (*src == '.')
			return;                 // it has an extension

		src--;
	}

	strcat (path, extension);
}


void DefaultPath (char *path, char *basepath)
{
	char    temp[128];

	if (path[0] == PATHSEPERATOR)
		return;                   // absolute path location

	strcpy (temp, path);
	strcpy (path, basepath);
	strcat (path, temp);
}


void    StripFilename (char *path)
{
	int             length;

	length = strlen (path) - 1;

	while (length > 0 && path[length] != PATHSEPERATOR)
		length--;

	path[length] = 0;
}

void    StripExtension (char *path)
{
	int             length;

	length = strlen (path) - 1;

	while (length > 0 && path[length] != '.')
	{
		length--;

		if (path[length] == '/')
			return;		// no extension
	}

	if (length)
		path[length] = 0;
}


/*
====================
Extract file parts
====================
*/
void ExtractFilePath (char *path, char *dest)
{
	char    *src;

	src = path + strlen (path) - 1;

	//
	// back up until a \ or the start
	//
	while (src != path && * (src - 1) != PATHSEPERATOR)
		src--;

	memcpy (dest, path, src - path);
	dest[src-path] = 0;
}

void ExtractFileBase (char *path, char *dest)
{
	char    *src;

	src = path + strlen (path) - 1;

	//
	// back up until a \ or the start
	//
	while (src != path && * (src - 1) != PATHSEPERATOR)
		src--;

	while (*src && *src != '.')
	{
		*dest++ = *src++;
	}

	*dest = 0;
}

void ExtractFileExtension (char *path, char *dest)
{
	char    *src;

	src = path + strlen (path) - 1;

	//
	// back up until a . or the start
	//
	while (src != path && * (src - 1) != '.')
		src--;

	if (src == path)
	{
		*dest = 0;	// no extension
		return;
	}

	strcpy (dest, src);
}


/*
==============
ParseNum / ParseHex
==============
*/
int ParseHex (char *hex)
{
	char    *str;
	int    num;

	num = 0;
	str = hex;

	while (*str)
	{
		num <<= 4;

		if (*str >= '0' && *str <= '9')
			num += *str - '0';
		else if (*str >= 'a' && *str <= 'f')
			num += 10 + *str - 'a';
		else if (*str >= 'A' && *str <= 'F')
			num += 10 + *str - 'A';
		else
			Error ("Bad hex number: %s", hex);

		str++;
	}

	return num;
}


int ParseNum (char *str)
{
	if (str[0] == '$')
		return ParseHex (str + 1);

	if (str[0] == '0' && str[1] == 'x')
		return ParseHex (str + 2);

	return atol (str);
}



/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

#ifdef _SGI_SOURCE
#define	__BIG_ENDIAN__
#endif

#ifdef __BIG_ENDIAN__

short   LittleShort (short l)
{
	byte    b1, b2;

	b1 = l & 255;
	b2 = (l >> 8) & 255;

	return (b1 << 8) + b2;
}

short   BigShort (short l)
{
	return l;
}


int    LittleLong (int l)
{
	byte    b1, b2, b3, b4;

	b1 = l & 255;
	b2 = (l >> 8) & 255;
	b3 = (l >> 16) & 255;
	b4 = (l >> 24) & 255;

	return ((int) b1 << 24) + ((int) b2 << 16) + ((int) b3 << 8) + b4;
}

int    BigLong (int l)
{
	return l;
}


float	LittleFloat (float l)
{
	union {byte b[4]; float f;} in, out;

	in.f = l;
	out.b[0] = in.b[3];
	out.b[1] = in.b[2];
	out.b[2] = in.b[1];
	out.b[3] = in.b[0];

	return out.f;
}

float	BigFloat (float l)
{
	return l;
}


#else


short   BigShort (short l)
{
	byte    b1, b2;

	b1 = l & 255;
	b2 = (l >> 8) & 255;

	return (b1 << 8) + b2;
}

#ifdef not
short   LittleShort (short l)
{
	return l;
}
#endif


int    BigLong (int l)
{
	byte    b1, b2, b3, b4;

	b1 = l & 255;
	b2 = (l >> 8) & 255;
	b3 = (l >> 16) & 255;
	b4 = (l >> 24) & 255;

	return ((int) b1 << 24) + ((int) b2 << 16) + ((int) b3 << 8) + b4;
}

#ifdef not
int    LittleLong (int l)
{
	return l;
}
#endif

float	BigFloat (float l)
{
	union {byte b[4]; float f;} in, out;

	in.f = l;
	out.b[0] = in.b[3];
	out.b[1] = in.b[2];
	out.b[2] = in.b[1];
	out.b[3] = in.b[0];

	return out.f;
}

#ifdef not
float	LittleFloat (float l)
{
	return l;
}
#endif


#endif

/*
============
CreatePath
============
*/
void	CreatePath (char *path)
{
	char	*ofs, c;

	for (ofs = path + 1; *ofs; ofs++)
	{
		c = *ofs;

		if (c == '/' || c == '\\')
		{
			// create the directory
			*ofs = 0;
			Q_mkdir (path);
			*ofs = c;
		}
	}
}


/*
============
QCopyFile

  Used to archive source files
============
*/
void QCopyFile (char *from, char *to)
{
	void	*buffer;
	int		length;

	length = LoadFile (from, &buffer);
	CreatePath (to);
	SaveFile (to, buffer, length);
	free (buffer);
}

void SetQPriority (int Priority)
{
#ifdef WIN32

	if (Priority == 0)
		Priority = THREAD_PRIORITY_BELOW_NORMAL;
	else if (Priority == 1)
		Priority = THREAD_PRIORITY_NORMAL;
	else if (Priority == 2)
		Priority = THREAD_PRIORITY_ABOVE_NORMAL;
	else
		Error ("Priority must be 0-2");

	SetThreadPriority (GetCurrentThread(), Priority);
#endif
}

qboolean NoPercent = false;
qboolean NumPercent = false;

/*
============
ShowBar
============
*/
void ShowBar (int threadnum, int Current, int Total)
{
	double	      Time;
	int	      New;

	if (Total == 0)
	{
		if (!NoPercent)
			timeinfo[threadnum].BarPrev = 0; // Enable

		return;
	}

	if (timeinfo[threadnum].BarPrev == -1)
		return; // Disabled

	if (Total == -1)
	{
		if (Current == 0)
		{
			// Disrupted
			if (timeinfo[threadnum].BarPrev > 0)
			{
				printf ("\\\n");

				// Make sure bar will be repainted next time
				timeinfo[threadnum].BarPrev = 0;
				timeinfo[threadnum].BarPrevTime = -1;
			}

			return;
		}

		// Finish and disable

		if (NumPercent)
			printf ("\r%3d%%\n", 100);
		else
		{
			// Finish bar
			while (timeinfo[threadnum].BarPrev < 9)
				printf ("%c", ++timeinfo[threadnum].BarPrev == 5 ? '+' : '-');

			printf ("+\n");
		}

		fflush (stdout);

		timeinfo[threadnum].BarPrev = -1;
		timeinfo[threadnum].BarPrevTime = 0;

		return;
	}

	if (NumPercent)
	{
		// Update every 20th percent or once each second
		New = Current * 100 / Total;

		if (New <= timeinfo[threadnum].BarPrev || New > 99)
			return;

		Time = I_FloatTime ();

		if (New / 20 == timeinfo[threadnum].BarPrev / 20 && Time < timeinfo[threadnum].BarPrevTime + 1)
			return;

		timeinfo[threadnum].BarPrev = New;
		timeinfo[threadnum].BarPrevTime = Time;
		printf ("\r%3d%%", New);
	}
	else
	{
		// Update every 10th percent
		New = Current * 10 / Total;

		if (New <= timeinfo[threadnum].BarPrev || New > 9)
			return;

		// Make sure bar reaches current value
		do
			printf ("%c", ++timeinfo[threadnum].BarPrev == 5 ? '+' : '-');

		while (timeinfo[threadnum].BarPrev < New);
	}

	fflush (stdout);
}

void FixAutoRate (int threadnum, qboolean Reset, double APercent);
int  DivideInt (int Val1, int Val2, int Multiplier);

double	 TimeOffset = 0;       // Offset for elapsed/total time
int 	 SecRate = 10;	       // Slowest of the rate factors will determine update speed
int 	 PercRate = 10;	       // 0.1 percent units
int 	 NewLine = 1;	       // New line for each printout
int	 TotTime = 1;	       // Show total time (elapsed+left)
qboolean SimpPercent = false;  // Enable non-estimated, weighted progress
qboolean AutoRate = true;      // Automatically adjusts update rate
qboolean OldHFormat = false;   // Old hour format
qboolean HiResPercent = false; // Percent in 12.34% format instead of 12.3%


/*
==============
ShowPercent
==============
*/
void ShowPercent (int threadnum, char Text[], int Value, int Total)
{
	double          Time, ETime, ETime2, LTime, TTime, APercent;
	int	        Percent, HunPercent, I, TimePercent, TTimeI, MinTPerc;
	char	        ETimeStr[20], LTimeStr[20], PercStr[20];
	qboolean	NewSample;

	// because threadnums in the threads struct are 1-based
	threadnum--;

	if (SimpPercent)
	{
		if (Text != NULL)
		{
			printf ("%s\n", Text);
			timeinfo[threadnum].Progressive = !strcmp (Text, "Full");
		}

		if (Total > 0 && Value == Total)
		{
			Value = Total = -1;
			timeinfo[threadnum].Progressive = false;
		}
		else if (Total > 0)
		{
			if (timeinfo[threadnum].Progressive)
			{
				// Try to compensate for fullvis non-linearity
				Value = ((double) Value * Value * Value) * 100 / ((double) Total * Total * Total);
				Total = 100;
			}
		}

		ShowBar (threadnum, Value, Total);

		fflush (stdout);

		return;
	}

	if (Text != NULL)
	{
		// Init
		timeinfo[threadnum].SampAPercent[0] = timeinfo[threadnum].SampTime[0] = 0;

		timeinfo[threadnum].PrevTime = timeinfo[threadnum].PrevAPercent = -9999; // Force first printout
		timeinfo[threadnum].DAPercent = timeinfo[threadnum].DSamples = 0;

		timeinfo[threadnum].STime = I_FloatTime ();

		strncpy (timeinfo[threadnum].TextStr, Text, sizeof (timeinfo[threadnum].TextStr) - 1);
		timeinfo[threadnum].TextStr[sizeof (timeinfo[threadnum].TextStr) - 1] = '\0';

		FixAutoRate (threadnum, true, 0);

		return;
	}
	else if (Value == 0 && Total == -1)
	{
		// Disrupted
		if (timeinfo[threadnum].PrintActive)
		{
			printf ("\n");
			timeinfo[threadnum].PrintActive = false;
		}

		return;
	}

	Percent = DivideInt (Value, Total, 100);
	HunPercent = DivideInt (Value, Total, 10000) % 100;
	APercent = (double) Value * 100 / Total;

	Time = I_FloatTime ();

	if (Percent != 100)
	{
		if (Time < timeinfo[threadnum].PrevTime + SecRate || floor (APercent * 10) < floor (timeinfo[threadnum].PrevAPercent * 10 + PercRate))
			return; // Speed & don't generate too many total printouts
	}

	timeinfo[threadnum].PrevTime = Time;
	timeinfo[threadnum].PrevAPercent = APercent;

	NewSample = false;

	// Require at least 1 second interval
	if (Time >= timeinfo[threadnum].SampTime[0] + 1)
	{
		// Baseline: Require at least 1 percent interval
		MinTPerc = 1;

		// Exception 1: Faster samples in the beginning and end
		if (timeinfo[threadnum].DSamples < DYNSAMPLES || APercent >= 99)
			MinTPerc = 10; // Require only 1/10 percent interval

		// Exception 2: At the very end; very fast samples
		if (APercent >= 99.9)
			MinTPerc = 100; // Require only 1/100 percent interval

		if (floor (APercent * MinTPerc) > floor (timeinfo[threadnum].SampAPercent[0] * MinTPerc))
			NewSample = true;
	}

	if (NewSample)
	{
		// We have a new unique two-coordinate sample

		if (timeinfo[threadnum].DSamples > 0)
		{
			// Find oldest value in buffers, but do not allow percent
			// difference to be greater than 10 (if possible)
			for (I = timeinfo[threadnum].DSamples; I > 0; --I)
			{
				// Calculate difference between current and oldest values
				timeinfo[threadnum].DAPercent = APercent - timeinfo[threadnum].SampAPercent[I - 1];
				timeinfo[threadnum].DTime = Time - timeinfo[threadnum].SampTime[I - 1];

				if (timeinfo[threadnum].DAPercent <= 10)
					break;
			}

			// Shift back sample values in buffers
			for (I = DYNSAMPLES - 1; I > 0; --I)
			{
				timeinfo[threadnum].SampAPercent[I] = timeinfo[threadnum].SampAPercent[I - 1];
				timeinfo[threadnum].SampTime[I] = timeinfo[threadnum].SampTime[I - 1];
			}
		}

		timeinfo[threadnum].SampAPercent[0] = APercent;
		timeinfo[threadnum].SampTime[0] = Time;

		if (timeinfo[threadnum].DSamples < DYNSAMPLES)
			++timeinfo[threadnum].DSamples;
	}

	// Elapsed time
	ETime = Time - timeinfo[threadnum].STime;
	ETime2 = ETime + TimeOffset;

	SecToStr (ETime2, ETimeStr, false);

	if (HiResPercent)
		sprintf (PercStr, "%3d.%02d%%", Percent, HunPercent);
	else
		sprintf (PercStr, "%3d.%d%%", Percent, HunPercent / 10);

	printf ("%c%s%7s, Thread %i, Elapsed%s", NewLine ? '\n' : '\r', timeinfo[threadnum].TextStr, PercStr, threadnum, ETimeStr);

	// Estimation possible ?
	if (timeinfo[threadnum].DSamples >= 2)
	{
		if (timeinfo[threadnum].DAPercent == 0)
			LTime = (100 - APercent) * ETime / APercent; // Static linear estimate
		else LTime = (100 - APercent) * timeinfo[threadnum].DTime / timeinfo[threadnum].DAPercent; // Dynamic linear estimate

		SecToStr (LTime, LTimeStr, false);

		printf (", Left%s", LTimeStr);

		if (TotTime)
		{
			TTime = ETime2 + LTime;
			TTimeI = (int) ETime2 + (int) LTime; // Make sure values add up nicely
			SecToStr (TTimeI, LTimeStr, false);
			TimePercent = TTime == 0 ? 0 : ETime2 * 100 / TTime;

			// Sanity check
			if (Percent != 100 && TimePercent == 100)
				TimePercent = 0;
			else if (Percent == 100)
				TimePercent = 100;

			printf (", Total%s %3d%%", LTimeStr, TimePercent);
		}
	}

	if (!NewLine)
		printf ("  ");

	fflush (stdout);

	FixAutoRate (threadnum, false, APercent);

	timeinfo[threadnum].PrintActive = Value < Total;
}

/*
==============
SecToStr
==============
*/
void SecToStr (double sec, char Str[], char Pack)
{
	int Sec;
	int Hour;

	Sec = (int) sec;
	Hour = Sec / 3600;

	if (Hour == 0)
		sprintf (Str, "%s%2d:%02d", Pack ? "" : "   ", (Sec / 60) % 60, Sec % 60);
	else if (OldHFormat)
		sprintf (Str, "%*d:%02d:%02d", Pack ? 1 : 2, Hour, (Sec / 60) % 60, Sec % 60);
	else sprintf (Str, "%*dh %2dm", Pack ? 1 : 3, Hour, (Sec / 60) % 60);
}

/*
==============
DivideInt
==============
*/
int DivideInt (int Val1, int Val2, int Multiplier)
{
	// Handle overflow
	while (Val2 > 2000000000 / Multiplier)
	{
		Multiplier /= 10;
		Val2 *= 10;
	}

	return Val1 * Multiplier / Val2;
}

/*
==============
FixAutoRate
==============
*/
void FixAutoRate (int threadnum, qboolean Reset, double APercent)
{
	double        Time, DTime;
	int	      I, DPerc;
	qboolean      AdjustRate;

	if (Reset)
	{
		timeinfo[threadnum].FARDSamples = timeinfo[threadnum].FARHiResCnt = 0;
		return;
	}

	if (!AutoRate || PercRate == 0 && HiResPercent)
		return; // No AutoRate or all adjustments done

	AdjustRate = PercRate > 0;

	if (AdjustRate)
		Time = I_FloatTime ();

	if (timeinfo[threadnum].FARDSamples > 0)
	{
		if (AdjustRate)
			DTime = Time - timeinfo[threadnum].FARSample[timeinfo[threadnum].FARDSamples - 1]; // Calculate difference between current and oldest timer value
		else
			DPerc = APercent * 10 - timeinfo[threadnum].FARSample[timeinfo[threadnum].FARDSamples - 1] * 10; // Zero indicates non-changing printout

		// Shift back sample values in buffers
		for (I = DYNSAMPLES - 1; I > 0; --I)
			timeinfo[threadnum].FARSample[I] = timeinfo[threadnum].FARSample[I - 1];
	}

	timeinfo[threadnum].FARSample[0] = AdjustRate ? Time : APercent;

	if (timeinfo[threadnum].FARDSamples < DYNSAMPLES)
		++timeinfo[threadnum].FARDSamples;

	if (AdjustRate)
	{
		// Update rate percentage limited ?
		if (timeinfo[threadnum].FARDSamples > 1 && DTime / timeinfo[threadnum].FARDSamples > SecRate * 2)
		{
			PercRate /= 2;
			timeinfo[threadnum].FARDSamples = 0; // Let new setting take effect
		}
	}
	else
	{
		if (timeinfo[threadnum].FARDSamples == DYNSAMPLES && DPerc == 0)
		{
			++timeinfo[threadnum].FARHiResCnt;
			timeinfo[threadnum].FARDSamples = 0; // Start a new trig cycle

			// Change to hires percent printout ?
			if (timeinfo[threadnum].FARHiResCnt == 3)
				HiResPercent = true;
		}
	}
}

/*
==============
ChkArgument
==============
*/
void ChkArgument (char Arg[], char *NextOption)
{
	if (NextOption == NULL)
		Error ("Missing '%s' argument", Arg);
}

/*
==============
GetArgument
==============
*/
int GetArgument (char Arg[], char *NextOption)
{
	ChkArgument (Arg, NextOption);
	return atoi (NextOption);
}

/*
==============
GetFloatArgument
==============
*/
float GetFloatArgument (char Arg[], char *NextOption)
{
	ChkArgument (Arg, NextOption);
	return atof (NextOption);
}

/*
==============
PrtSize
==============
*/
char *PrtSize (int Size)
{
	char *Str;

	Str = malloc (15); // This probably leaks

	if (Size > 99999)
		sprintf (Str, "%i kb", Size / 1024);
	else
		sprintf (Str, "%i", Size);

	return Str;
}

