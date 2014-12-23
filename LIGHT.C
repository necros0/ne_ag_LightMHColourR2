// lighting.c

#include <windows.h>
#include "light.h"

#pragma comment (lib, "Winmm.lib")

// threads compatibility
#define LOCK WaitForSingleObject (hThreadMutex, INFINITE)
#define UNLOCK ReleaseMutex (hThreadMutex)

HANDLE hThreadMutex;
volatile int finished;
int numthreads = 4;

typedef struct threadinfo_s
{
	int firstface;
	int lastface;
	int threadnum;
} threadinfo_t;

threadinfo_t *threadinfos;

void RunThreadsOn (LPTHREAD_START_ROUTINE proc)
{
	int i;
	int start = timeGetTime ();
	int end;

	finished = 0;

	if (numthreads < 1) numthreads = 1;
	if (numfaces < numthreads) numthreads = 1;

	InitPercents (numthreads);
	threadinfos = (threadinfo_t *) malloc (numthreads * sizeof (threadinfo_t));

	for (i = 0; i < numthreads; i++)
	{
		threadinfos[i].threadnum = i + 1;
		threadinfos[i].firstface = (numfaces / numthreads) * i;
	}

	for (i = 1; i < numthreads; i++)
		threadinfos[i - 1].lastface = threadinfos[i].firstface;

	threadinfos[numthreads - 1].lastface = numfaces;

	for (i = 0; i < numthreads; i++)
		CreateThread (NULL, 0, proc, &threadinfos[i], 0, NULL);

	while (1)
	{
		if (finished >= numthreads) break;
		Sleep (1);
	}

	if (!SimpPercent)
		printf ("\n");

	printf ("\n");

	end = timeGetTime ();

#ifdef _DEBUG
	printf ("done in %f seconds\npress any key... ", (float) (end - start) / 1000.0f);

	while (1)
	{
		if (_kbhit ()) break;
		Sleep (1);
	}
#endif
}


void InitThreads (void)
{
	hThreadMutex = CreateMutex (NULL, FALSE, NULL);
}


#define LOGFILENAME "LIGHT.LOG"

/*

NOTES
-----

*/

float		scaledist = -1;	      // "-dist" value
float		scalecos = 0.5;	      // Default angle sensitivity
float		ShadowSense = -1;     // Angle (shadow) sensitivity for 2nd sunlight
float		rangescale = -1;      // "-range" value
float		GateVal = 0;	      // Fade Gate value (limit attenuated lights)
int	        worldminlight = -1;
int	        worldmaxlight = -1;

byte		*filebase1, *file_p1, *file_end1;
byte		*filebase3, *file_p3, *file_end3;

dmodel_t	*bspmodel;
int		bspfileface;	      // next surface to dispatch

vec3_t		bsp_origin;
vec3_t		*faceoffset;	      // Rotating entities related

int		OverSample = 1;	      // 1 = No oversampling
int		SoftLight = -1;	      // -1 = Disabled
int		SoftDist = SOFTDIST;  // Distance tolerance for lights behind surface
int		SingleDist;	      // Distance tolerance for lights behind surface in SingleLightFace
int		SkyDist;	      // Distance tolerance for lights behind surface in SkyLightFace
int		LightCap = 0;	      // 0 = Disabled
int		NumSurfPts = 0;	      // Total # surface points
unsigned int	FastLight = 0;	      // 0 = No fast lighting
qboolean	NoLight = false;      // Disables all light entities except global
qboolean	SrcLight = false;     // Disables all unsourced light entities
qboolean	NoWarnings = false;   // Disable repetitive warnings
qboolean	DisableScreen = false;
qboolean	OldLight = false;     // Disables wait/delay/mangle
qboolean	TyrLiteMode = false;  // Enables TyrLite mode
qboolean	TyrLite95Mode = false;// Enables TyrLite95 mode
qboolean	ArghLiteMode = false; // Enables ArghLite mode
qboolean	AddMinLight = false;  // Enables additive minlight
qboolean	UnsupDetails = false; // Prints out unsupported details
qboolean	DetectKeys = false;   // Prints out detected keys summary
qboolean	NoSkillChk = false;   // Disable fuzzy skill target check
qboolean	NoFlash = false;      // Disables flashing lights
qboolean	GlobRange = false;    // Enables global light range
qboolean	IKLiteMode = false;   // Enables IKLite mode
qboolean	IKAngle = false;      // Enables IKLite angle sensitivity
qboolean	LightDLXMode = false; // Enables LightDLX mode
qboolean	OnlyEnts = false;     // Only process switchable lights
qboolean	NoWrite = false;      // Disable bsp write
qboolean	NoReverse = false;    // Prevents reverse entity key order
qboolean	GenCompatible = false;// Enables closer general compatibility
qboolean	ATCompatible = false; // Enables closer Argh/TyrLite compatibility
qboolean	TyrCompatible = false;// Enables closer TyrLite compatibility
qboolean	EnhancedTP = false;   // Enables QuArK/Hammer Enhanced Texture Positioning support
qboolean	AntiLights = false;   // True if negative lights used
qboolean	NoAnti = false;	      // True if negative lights disabled
qboolean	NoAntiOption = false; // True if negative lights disabled via "-noanti" option
qboolean	KinnDelay = false;    // Translates delay 2 lights into delay 5
qboolean	SolidSky = false;     // True if also solid sky brushes will cast sunlight
qboolean	PreScan;	      // True when only calculating # total surfpts, avoid ray tracing

//new settings
qboolean	FakeGISunlight2 = false;//makes sunlight2 cast additive light instead of minlight.
qboolean	FakeGIMode = false;		//makes fake GI with additive light from an array of lights that emit from the void

float	SunLight[2] = {-1, -1};
int		SunMangleVal[3] = {-1, -1, -1};
float	SunLightColor[2][3] = {{255, 255, 255}, {255, 255, 255}};
int		NoOfSuns, NoOfHSuns, NoOfVSuns;
vec3_t		SunMangle[NOOFSUNS];
/*
	If you are not using fake GI modes, the SunMangle array is actually overlarge.  I don't want to mess with malloc
	so I took the lazy route.  Big deal, it's a few extra byes. :S
*/

vec_t		DistFactor1;	      // IKLite related values
vec_t		DistFactor2;

double		start = 0, end;
FILE		*logfile;

void logvprintf (char *fmt, va_list argptr)
{
	if (PreScan)
		return; // No printouts during prescan

	if (!NoWarnings || !DisableScreen)
	{
		// ShowPercent (0, NULL, 0, -1);

		vprintf (fmt, argptr);
		fflush (stdout);
	}

	vfprintf (logfile, fmt, argptr);
	fflush (logfile);
}

void logprintf (char *fmt, ...)
{
	va_list argptr;

	va_start (argptr, fmt);
	logvprintf (fmt, argptr);
	va_end (argptr);
}

void logwprintf (char *fmt, ...)
{
	va_list argptr;

	DisableScreen = true;
	va_start (argptr, fmt);
	logvprintf (fmt, argptr);
	va_end (argptr);
	DisableScreen = false;
}

void PrintFinish (void)
{
	char Str[20];

	if (start != 0)
	{
		end = I_FloatTime ();

		SecToStr ((end - start + 0.5), Str, true);

		logprintf ("\nElapsed time : %s\n", Str);
	}

	fclose (logfile);
}

void ErrorExit (void)
{
	PrintFinish ();
	exit (1);
}

/*
==================
ToRad
Converts from degrees to radians
==================
*/
vec_t ToRad (vec_t Degree)
{
	return Degree * Q_PI / 180;
}

/*
==================
ToDegree
Converts from radians to degrees
==================
*/
vec_t ToDegree (vec_t Rad)
{
	return Rad * 180 / Q_PI;
}

byte *GetFileSpace (int size)
{
	byte *buf;

	LOCK;
	buf = file_p1;

	file_p1 = (byte *) (((long) file_p1 + 3) & ~3);

	if (file_p1 - buf > 0)
		memset (buf, 0, file_p1 - buf); // Clear pad bytes

	buf = file_p1;
	file_p1 += size;
	UNLOCK;

	if (file_p1 > file_end1)
		Error ("Light data size exceeded, max = %s", PrtSize (MAX_MAP_LIGHTING));

	return buf;
}


DWORD WINAPI LightThread (LPVOID *data)
{
	int i;

	// only print on the first thread
	threadinfo_t *threadinfo = (threadinfo_t *) data;

	if (NewLine)
		fprintf (logfile, "\n");
	else logprintf ("\n");

	if (SimpPercent)
		printf ("\n");

	ShowPercent (threadinfo->threadnum, "Light", 0, 0);

	// reduce thread locking overhead by partitioning the BSP faces
	for (i = threadinfo->firstface; i < threadinfo->lastface; i++)
	{
		ShowPercent (threadinfo->threadnum, NULL, (i - threadinfo->firstface), (threadinfo->lastface - threadinfo->firstface));
		LightFace (i, faceoffset[i]);
	}

	ShowPercent (threadinfo->threadnum, NULL, 100, 100);

	LOCK;
	finished++;
	UNLOCK;

	return 0;
}

void FindFaceOffsets (void)
{
	int	 i, j;
	entity_t *ent;
	char	 name[20];
	vec3_t	 org;

	faceoffset = malloc (numfaces * sizeof (vec3_t));
	memset (faceoffset, 0, numfaces * sizeof (vec3_t));

	for (i = 1; i < nummodels; i++)
	{
		sprintf (name, "*%d", i);
		ent = FindEntityWithKeyPair ("model", name);

		if (!ent)
			logprintf ("WARNING: FindFaceOffsets: Couldn't find entity for model %s\n", name);
		else if (!strncmp (ValueForKey (ent, "classname"), "rotate_", 7))
		{
			int	start;
			int	end;

			GetVectorForKey (ent, "origin", org);

			start = dmodels[ i ].firstface;
			end = start + dmodels[ i ].numfaces;

			if (start >= numfaces || end > numfaces)
				Error ("FindFaceOffsets: numfaces (%d) exceeded, start=%d, end=%d", numfaces, start, end);

			for (j = start; j < end; j++)
			{
				faceoffset[ j ][ 0 ] = org[ 0 ];
				faceoffset[ j ][ 1 ] = org[ 1 ];
				faceoffset[ j ][ 2 ] = org[ 2 ];
			}
		}
	}
}

/*
=============
LightWorld
=============
*/
void LightWorld (void)
{
	// Allocate max lightdata
	if (dlightdata1 != NULL)
		free (dlightdata1);

	if (dlightdata3 != NULL)
		free (dlightdata3);

	dlightdata1 = malloc (MAX_MAP_LIGHTING);
	dlightdata3 = malloc (MAX_MAP_LIGHTING * 3);

	lightdatasize1 = MAX_MAP_LIGHTING;
	lightdatasize3 = MAX_MAP_LIGHTING * 3;

	filebase1 = file_p1 = dlightdata1;
	file_end1 = filebase1 + MAX_MAP_LIGHTING;

	filebase3 = file_p3 = dlightdata3;
	file_end3 = filebase3 + MAX_MAP_LIGHTING * 3;

	RunThreadsOn (LightThread);

	lightdatasize1 = file_p1 - filebase1;
	lightdatasize3 = file_p3 - filebase3;

	logprintf ("lightdatasize: %s\n", PrtSize (lightdatasize1));
}

/*
==============
PrintOptions
==============
*/
void PrintOptions (void)
{
	logprintf ("Light performs light processing of Quake .BSP files\n\n");
	logprintf ("light [options] bspfile\n\n");
	logprintf ("Options:\n");
	logprintf ("   -threads [n]     Enable multithreaded processing (faster, default 4)\n");
	logprintf ("   -fast [n]        Enable fast lighting (lower quality, default 2)\n");
	logprintf ("   -soft [n]        Enable soft lighting (reduce jagged shadows)\n");
	logprintf ("   -softdist [n]    Distance tolerance for lights behind surface (default 3)\n");
	logprintf ("   -extra           Enable extra 2x2 sampling for higher quality\n");
	logprintf ("   -extra4          Enable extra 4x4 sampling for even higher quality\n");
	logprintf ("   -dist [n]        Set fade distance, higher is darker (default 1.0)\n");
	logprintf ("   -range [n]       Set brightness range, higher is brighter (default 0.5)\n");
	logprintf ("   -globrange       Enable global range (range affects global light)\n");
	logprintf ("   -noglobrange     Disable global range (use with arghlite/tyrlite)\n");
	logprintf ("   -light [n]       Set minimum light level (default 0)\n");
	logprintf ("   -maxlight [n]    Set maximum light level (default disabled)\n");
	logprintf ("   -nolight         Disable light entities, only global light remains\n");
	logprintf ("   -srclight        Disable all unsourced light entities\n");
	logprintf ("   -sunlight [n]    Set sunlight level (default 0)\n");
	logprintf ("   -sunlight2 [n]   Set 2nd sunlight (outdoor minlight) level (default 0)\n");
	logprintf ("   -sunlight3 [n]   Same as sunlight2 + set Shadow sensitivity to 0.4\n");
	logprintf ("   -sunmangle [y,p] Set sun direction, y=yaw (0 to 360),\n");
	logprintf ("                    p=pitch (90 to -90) (default (0,-90)\n");
	logprintf ("   -nowarnings      Disable repetitive warnings\n");
	logprintf ("   -rate [s,p,l,t]  Control extended progress update rate and format,\n");
	logprintf ("                    s=seconds, p=percent, l=line, t=total (default 10,1.0,1,1)\n");
	logprintf ("   -barpercent      Simplified and weighted bargraph progress information\n");
	logprintf ("   -numpercent      Simplified and weighted numerical progress information\n");
	logprintf ("   -oldlight        Disable entity wait/delay/mangle/antilight features\n");
	logprintf ("   -tyrlite         Enable TyrLite mode\n");
	logprintf ("   -tyrlite95       Enable TyrLite95 mode\n");
	logprintf ("   -arghlite        Enable ArghLite mode\n");
	logprintf ("   -addmin          Enable additive minlight\n");
	logprintf ("   -iklite          Enable IKLite mode\n");
	logprintf ("   -ikangle         Enable IKLite angle sensitivity\n");
	logprintf ("   -anglesense [n]  Set angle sensitivity (default 0.5)\n");
	logprintf ("   -shadowsense [n] Set angle sensitivity for 2nd sunlight (default 0.0)\n");
	logprintf ("   -gate [n]        Set Fade Gate (limit attenuated lights) (default 0.0)\n");
	logprintf ("   -dlx             Enable limited LightDLX mode\n");
	logprintf ("   -kinn            Translate delay 2 lights into delay 5\n");
	logprintf ("   -solidsky        Enable solid sky brushes\n");
	logprintf ("   -unsup           Print details about unsupported keys\n");
	logprintf ("   -detect          Print detected keys summary\n");
	logprintf ("   -noskill         Disable fuzzy skill target check\n");
	logprintf ("   -noflash         Disable flashing lights\n");
	logprintf ("   -noanti          Disable antilights\n");
	logprintf ("   -lightcap [n]    Set maximum light entity intensity\n");
	logprintf ("   -onlyents        Only process switchable light entities\n");
	logprintf ("   -nowrite         Disable bsp write\n");
	logprintf ("   -norev           Prevent reverse entity key order\n");
	logprintf ("   -etp             Enable Enhanced Texture Positioning support\n");
	logprintf ("   -priority [n]    Set thread priority 0-2 (below/normal/above, default 1)\n");
	logprintf ("   -oldhformat      Enable hour format HH:MM:SS instead of HHh MMm\n");
	logprintf ("   -fakeGISun2      Causes sunlight2 to cast additive light.\n");
	logprintf ("   -fakeGIMode      Casts additive light in an array around the world origin from the void.\n");
	logprintf ("   bspfile          .BSP file to process\n");

	fclose (logfile);

	exit (1);
}

/*
========
main

light modelfile
========
*/
int main (int argc, char **argv)
{
	int	 i, ModeCnt = 0, Val;
	char	 source[1024], *Option, *NextOption;
	float	 FVal;
	qboolean NoGlobRange = false;

	logfile = fopen (LOGFILENAME, "w");
	logprintf ("----- Light 1.43 ---- Modified by Bengt Jardrup\n");
	logprintf ("----- Release 2  ---- Coloured light and LIT support by MH\n\n");

	for (i = 1; i < argc; i++)
	{
		Option = argv[i];
		NextOption = i + 1 < argc ? argv[i + 1] : NULL;

		if (Option[0] != '-')
			break;

		++Option;

		if (!stricmp (Option, "fast"))
		{
			FastLight = 2;

			if (NextOption != NULL && isdigit (NextOption[0]) && i + 2 < argc)
			{
				Val = atoi (NextOption);
				i++;

				if (Val > 2)
					FastLight = Val;
			}

			logprintf ("Fast light %d enabled\n", FastLight);
		}
		else if (!stricmp (Option, "soft"))
		{
			SoftLight = 0;

			if (NextOption != NULL && isdigit (NextOption[0]) && i + 2 < argc)
			{
				SoftLight = atoi (NextOption);
				i++;
			}
		}
		else if (!stricmp (Option, "softdist"))
		{
			SoftDist = GetArgument (Option, NextOption);
			i++;
		}
		else if (!stricmp (Option, "extra") || !stricmp (Option, "extra4"))
		{
			OverSample = Option[5] == '4' ? 4 : 2;
			logprintf ("Extra %dx%d sampling enabled\n", OverSample, OverSample);
		}
		else if (!stricmp (Option, "threads"))
		{
			numthreads = GetFloatArgument (Option, NextOption);
			i++;
		}
		else if (!stricmp (Option, "dist"))
		{
			scaledist = GetFloatArgument (Option, NextOption);
			i++;
		}
		else if (!stricmp (Option, "range"))
		{
			rangescale = GetFloatArgument (Option, NextOption);
			i++;
		}
		else if (!stricmp (Option, "globrange"))
		{
			GlobRange = true;
			logprintf ("Global range enabled\n");
		}
		else if (!stricmp (Option, "noglobrange"))
			NoGlobRange = true;
		else if (!stricmp (Option, "light"))
		{
			worldminlight = GetArgument (Option, NextOption);
			i++;
		}
		else if (!stricmp (Option, "maxlight"))
		{
			worldmaxlight = GetArgument (Option, NextOption);
			i++;
		}
		else if (!stricmp (Option, "nolight"))
		{
			NoLight = true;
			logprintf ("Light entities disabled\n");
		}
		else if (!stricmp (Option, "srclight"))
		{
			SrcLight = true;
			logprintf ("Unsourced light entities disabled\n");
		}
		else if (!stricmp (Option, "sunlight"))
		{
			SunLight[0] = GetArgument (Option, NextOption);
			i++;
		}
		else if (!stricmp (Option, "sunlight2"))
		{
			SunLight[1] = GetArgument (Option, NextOption);
			i++;
		}
		else if (!stricmp (Option, "sunlight3"))
		{
			SunLight[1] = GetArgument (Option, NextOption);

			if (ShadowSense == -1)
				ShadowSense = SHADOWSENSE;

			i++;
		}
		else if (!stricmp (Option, "sunmangle"))
		{
			ChkArgument (Option, NextOption);

			if (sscanf (NextOption, "%d,%d", &SunMangleVal[0], &SunMangleVal[1]) != 2)
				Error ("Missing arguments for '%s'", Option);

			SunMangleVal[2] = 0;
			i++;
		}
		else if (!stricmp (Option, "nowarnings"))
			NoWarnings = true;
		else if (!stricmp (Option, "rate"))
		{
			ChkArgument (Option, NextOption);

			if (sscanf (NextOption, "%d,%f,%d,%d", &SecRate, &FVal, &NewLine, &TotTime) > 1)
				PercRate = (FVal + 0.05) * 10; // Fix roundoff

			AutoRate = false;
			i++;
		}
		else if (!stricmp (Option, "barpercent"))
		{
			SimpPercent = true;
			NumPercent = false;
		}
		else if (!stricmp (Option, "numpercent"))
		{
			SimpPercent = true;
			NumPercent = true;
		}
		else if (!stricmp (Option, "oldlight"))
		{
			OldLight = NoAnti = true;
			logprintf ("Oldlight mode enabled\n");
			++ModeCnt;
		}
		else if (!stricmp (Option, "tyrlite"))
		{
			TyrLiteMode = GlobRange = true;
			logprintf ("TyrLite mode enabled\n");
			++ModeCnt;
		}
		else if (!stricmp (Option, "tyrlite95"))
		{
			TyrLite95Mode = TyrLiteMode = GlobRange = true;
			logprintf ("TyrLite95 mode enabled\n");
			++ModeCnt;
		}
		else if (!stricmp (Option, "arghlite"))
		{
			ArghLiteMode = GlobRange = AddMinLight = true;
			logprintf ("ArghLite mode enabled\n");
			++ModeCnt;
		}
		else if (!stricmp (Option, "addmin"))
		{
			AddMinLight = true;
			logprintf ("Additive minlight enabled\n");
		}
		else if (!stricmp (Option, "iklite"))
		{
			IKLiteMode = IKAngle = OldLight = NoAnti = true;
			logprintf ("IKLite mode enabled\n");
			++ModeCnt;
		}
		else if (!stricmp (Option, "ikangle"))
		{
			IKAngle = true;
			logprintf ("IKLite angle sensitivity enabled\n");
		}
		else if (!stricmp (Option, "anglesense"))
		{
			FVal = GetFloatArgument (Option, NextOption);
			i++;

			if (FVal >= 0 && FVal <= 1 && FVal != scalecos)
				logprintf ("Angle sensitivity %g set\n", scalecos = FVal);
		}
		else if (!stricmp (Option, "shadowsense"))
		{
			FVal = GetFloatArgument (Option, NextOption);
			i++;

			if (FVal >= 0 && FVal <= 1)
				ShadowSense = FVal;
		}
		else if (!stricmp (Option, "gate"))
		{
			FVal = GetFloatArgument (Option, NextOption);
			i++;

			if (FVal >= 0 && FVal != GateVal)
				logprintf ("Fade Gate %g set\n", GateVal = FVal);
		}
		else if (!stricmp (Option, "dlx"))
		{
			LightDLXMode = OldLight = true;
			logprintf ("LightDLX mode enabled\n");
			++ModeCnt;
		}
		else if (!stricmp (Option, "kinn"))
		{
			KinnDelay = true;
			logprintf ("Kinn translation enabled\n");
		}
		else if (!stricmp (Option, "solidsky"))
		{
			SolidSky = true;
			logprintf ("Solid sky brushes enabled\n");
		}
		else if (!stricmp (Option, "unsup"))
			UnsupDetails = DetectKeys = true;
		else if (!stricmp (Option, "detect"))
			DetectKeys = true;
		else if (!stricmp (Option, "noskill"))
			NoSkillChk = true;
		else if (!stricmp (Option, "noflash"))
			NoFlash = true;
		else if (!stricmp (Option, "noanti"))
			NoAnti = NoAntiOption = true;
		else if (!stricmp (Option, "lightcap"))
		{
			Val = GetArgument (Option, NextOption);
			i++;

			if (Val >= 0 && Val != LightCap)
				logprintf ("LightCap %d enabled\n", LightCap = Val);
		}
		else if (!stricmp (Option, "onlyents"))
			OnlyEnts = true;
		else if (!stricmp (Option, "nowrite"))
			NoWrite = true;
		else if (!stricmp (Option, "norev"))
			NoReverse = true;
		else if (!stricmp (Option, "etp"))
		{
			EnhancedTP = true;
			logprintf ("Enhanced Texture Positioning enabled\n");
		}
		else if (!stricmp (Option, "priority"))
		{
			SetQPriority (GetArgument (Option, NextOption));
			i++;
		}
		else if (!stricmp (Option, "oldhformat"))
			OldHFormat = true;
		else if (!stricmp (Option, "?") || !stricmp (Option, "help"))
			PrintOptions ();
		else if (!stricmp (Option, "fakeGISun2"))
		{
			FakeGISunlight2 = true;
			logprintf ("Additive sunlight2 enabled\n");
		}
		else if (!stricmp (Option, "fakeGIMode"))
		{
			FakeGIMode = true;
			logprintf ("FakeGI mode enabled\n");
		}
		else
			Error ("Unknown option '%s'", Option);
	}

	if (i != argc - 1)
		PrintOptions ();

	if (GlobRange && NoGlobRange)
	{
		GlobRange = false;
		logprintf ("Global range disabled\n");
	}

	if (ModeCnt > 1)
		Error ("Only one emulation mode allowed");

	if (SoftLight >= 0)
	{
		if (SoftLight == 0)
			SoftLight = OverSample / 2 + 1; // Auto mode

		logprintf ("Soft light %d enabled\n", SoftLight);

		if (SoftDist != SOFTDIST)
			logprintf ("Soft distance %d set\n", SoftDist);
	}

	if (SoftLight > 0)
	{
		// Soft option is in charge
		SingleDist = -SoftDist;
		SkyDist = -SOFTDIST;
	}
	else
	{
		if (TyrLite95Mode)
		{
			SingleDist = 0;
			SkyDist = -ANGLE_EPSILON;
		}
		else
			SingleDist = SkyDist = 1; // Classic (no) soft distance
	}

	if (OverSample <= 2)
	{
		// Set compatibility modes
		GenCompatible = OldLight || TyrLiteMode || ArghLiteMode || IKLiteMode || LightDLXMode;
		ATCompatible = TyrLiteMode || ArghLiteMode;
		TyrCompatible = TyrLiteMode;
	}

	InitThreads ();

	if (!OnlyEnts && !NoWrite)
		start = I_FloatTime ();

	strcpy (source, argv[i]);
	StripExtension (source);
	DefaultExtension (source, ".bsp");

	if (!NoWrite)
		WriteChk (source);

	logprintf ("File: %s\n", source);

	LoadBSPFile (source);

	LoadEntities ();

	if (OnlyEnts)
		logprintf ("Updating entities lump...\n");
	else if (!NoWrite)
	{
		MakeTnodes (&dmodels[0]);

		FindFaceOffsets ();
		LightWorld ();
	}

	if (!NoWrite)
	{
		WriteEntitiesToString ();
		WriteBSPFile (source);
	}

	PrintFinish ();

	return 0;
}

