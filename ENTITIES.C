// entities.c

#include "light.h"

// Properties of this entity
typedef struct t_ent_s
{
	float	 Dist;
	float	 Range;
	int		Formula;
	int		MaxLight;
	int		StrangeKeys;
	char	 MangleStr[MAX_KEY];
	qboolean Angle;
	qboolean Anti;
	qboolean Compressed;
	qboolean Delay;
	qboolean DLX;
	qboolean Mangle;
	qboolean MangleErr;
	qboolean Multi;
	qboolean OriginErr;
	qboolean Sourced;
	qboolean Sun;
	qboolean Wait;
} t_ent_t;

// Properties of all entities
typedef struct a_ent_s
{
	int	 CappedLights;
	int	 CappedSum;
	int	 DLXEnts;
	int	 IKLiteEnts;
	int	 LightSum;
	int	 NoFlash;
	int	 NumAngle;
	int	 NumAnti;
	int	 NumCompressed;
	int	 NumDelay;
	int	 NumDLX;
	int	 NumLights;
	int	 NumMangle;
	int	 NumWait;
	int	 TyrLiteEnts;
	int	 TyrLiteKeys;
	int	 WorldSpawns;
	qboolean MinLight;
	qboolean SunLight;
	qboolean SunLight2;
} a_ent_t;

entity_t	entities[MAX_MAP_ENTITIES];
int		num_entities;

/*
==============================================================================

ENTITY FILE PARSING

If a light has a targetname, generate a unique style in the 32-63 range
==============================================================================
*/

#define MAX_LIGHT_TARGETS		32
#define OFFSET_LIGHT_TARGETS		32
#define	SPAWNFLAG_NOT_EASY		256
#define	SPAWNFLAG_NOT_MEDIUM		512
#define	SPAWNFLAG_NOT_HARD		1024
#define	SPAWNFLAG_NOT_DEATHMATCH	2048

int		numlighttargets;
char	lighttargets[MAX_LIGHT_TARGETS][MAX_VALUE];

int LightStyleForTargetname (char *targetname, qboolean alloc)
{
	int i;

	for (i = 0; i < numlighttargets; i++)
		if (!strcmp (lighttargets[i], targetname))
			return OFFSET_LIGHT_TARGETS + i;

	if (!alloc)
		return -1;

	if (i == MAX_LIGHT_TARGETS)
		Error ("LightStyleForTargetname: Too many unique light targetnames, max = %i", MAX_LIGHT_TARGETS);

	strcpy (lighttargets[i], targetname);
	numlighttargets++;
	return numlighttargets - 1 + OFFSET_LIGHT_TARGETS;
}

/*
==================
IsMultiTarget
==================
*/
qboolean IsMultiTarget (char *Key, char *Target, int Lth)
{
	if (!strncmp (Key, Target, Lth) && (!Key[Lth] || atoi (&Key[Lth])))
		return true; // Key matches e.g. "target", "target1", "target10" etc

	return false;
}

/*
==================
AddMultiTarget
==================
*/
void AddMultiTarget (char ***Base, char *NewTarget)
{
	int i;

	if (!NewTarget || *NewTarget == 0)
		return;

	if (*Base == NULL)
	{
		*Base = malloc (MAXMULTITARGETS * sizeof (char *));
		memset (*Base, 0, MAXMULTITARGETS * sizeof (char *));
	}

	// Advance to next free slot
	for (i = 0; i < MAXMULTITARGETS && (*Base) [i]; ++i)
		;

	if (i == MAXMULTITARGETS)
		Error ("AddMultiTarget: Too many multi targets for '%s' (max = %d)", NewTarget, MAXMULTITARGETS);

	(*Base) [i] = copystring (NewTarget);
}

/*
==================
FindMultiTarget
==================
*/
qboolean FindMultiTarget (char **Base, char *Target)
{
	int i;

	if (!Base || !Target || *Target == 0)
		return false;

	for (i = 0; i < MAXMULTITARGETS && Base[i]; ++i)
	{
		if (!strcmp (Base[i], Target))
			return true; // Found
	}

	return false;
}

/*
==================
MatchMultiTarget
==================
*/
qboolean MatchMultiTarget (char **Base1, char **Base2)
{
	int i;

	if (!Base1 || !Base2)
		return false;

	for (i = 0; i < MAXMULTITARGETS && Base1[i]; ++i)
	{
		if (FindMultiTarget (Base2, Base1[i]))
			return true; // At least one match between the two sets
	}

	return false;
}

/*
==================
EntName
==================
*/
char *EntName (entity_t *e)
{
	char	    TargetName[2 * MAX_VALUE];
	static char Str[2 * MAX_VALUE];

	TargetName[0] = '\0';

	if (e->targetname != NULL)
		sprintf (TargetName, ", targetname '%s'", e->targetname[0]); // Only print first targetname here

	if (e->origin_set)
		sprintf (Str, "at (%.0f %.0f %.0f) (%s%s)", e->origin[0], e->origin[1], e->origin[2], e->classname, TargetName);
	else
		sprintf (Str, "%s%s", e->classname, TargetName);

	return Str;
}

/*
==================
MatchTargets
==================
*/
void MatchTargets (void)
{
	entity_t *e;
	int	 i, j, k;
	qboolean Found;

	for (i = 0; i < num_entities; i++)
	{
		e = &entities[i];

		if (e->target == NULL)
			continue;

		// Look for targetnames, lights are preferred
		for (k = 0; k < MAXMULTITARGETS && e->target[k]; ++k)
		{
			Found = false;

			for (j = 0; j < num_entities; j++)
			{
				if (FindMultiTarget (entities[j].targetname, e->target[k]))
				{
					Found = true;

					if (!e->targetent || !e->targetent->islight)
						e->targetent = &entities[j]; // Remember first light targetname
				}
			}

			// No targetname for this multi target item found ?
			if (!Found)
				logprintf ("WARNING: Entity %s has unmatched target '%s'\n", EntName (e), e->target[k]);
		}

		// No targetname at all found ?
		if (!e->targetent)
			continue;

		// set the style on the source ent for switchable lights
		if (e->targetent->style >= OFFSET_LIGHT_TARGETS && e->targetent->islight)
		{
			char s[16];

			e->style = e->targetent->style;
			sprintf (s, "%i", e->style);
			SetKeyValue (e, "style", s);
		}
	}
}

/*
==================
Skill
==================
*/
char *Skill (int SFlags)
{
	static char Str[100];

	Str[0] = '\0';

	if (SFlags & SPAWNFLAG_NOT_EASY)
		strcat (Str, "Easy ");

	if (SFlags & SPAWNFLAG_NOT_MEDIUM)
		strcat (Str, "Medium ");

	if (SFlags & SPAWNFLAG_NOT_HARD)
		strcat (Str, "Hard ");

	return Str;
}

/*
==================
SetFlags
==================
*/
int SetFlags (int SFlags, int Mask, int Count, int TrigCount)
{
	if ((SFlags & Mask) == 0 && Count < TrigCount)
		return SFlags | Mask; // Set mask to indicate error

	return SFlags & ~Mask; // Clear mask to indicate success
}

/*
==================
MatchSkillTargets

Fuzzy check for potentially missing triggers in certain skills
==================
*/
void MatchSkillTargets (void)
{
	entity_t *e, *et;
	int	 i, j, SFlags, SFlags2, Easy, Medium, Hard, TrigCount;
	qboolean NoTrigger;

	for (i = 0; i < num_entities; i++)
	{
		e = &entities[i];

		SFlags = atoi (ValueForKey (e, "spawnflags")) & (SPAWNFLAG_NOT_EASY | SPAWNFLAG_NOT_MEDIUM | SPAWNFLAG_NOT_HARD);

		if (SFlags == (SPAWNFLAG_NOT_EASY | SPAWNFLAG_NOT_MEDIUM | SPAWNFLAG_NOT_HARD))
			continue; // Not included in any SP skills

		if (e->targetname == NULL)
		{
			// No targetname for func_spawn* might cause invalid monster counts
			if (!strncmp (e->classname, "func_spawn", 10))
				logprintf ("WARNING: Entity %s has no targetname\n", EntName (e));

			continue;
		}

		if (strncmp (e->classname, "func_", 5) && strncmp (e->classname, "trigger_", 8))
			continue;

		// Target entity is func_* or trigger_*

		if (strcmp (e->classname, "trigger_counter"))
			TrigCount = 1;
		else
		{
			TrigCount = atoi (ValueForKey (e, "count"));

			if (TrigCount == 0)
				TrigCount = 2;
		}

		Easy = Medium = Hard = 0;

		// Look for triggers
		for (j = 0; j < num_entities; j++)
		{
			et = &entities[j];

			// Skip missing/mismatching target/killtarget
			if (!MatchMultiTarget (e->targetname, et->target) &&
					!MatchMultiTarget (e->targetname, et->killtarget))
				continue;

			SFlags2 = atoi (ValueForKey (et, "spawnflags"));

			if ((SFlags2 & SPAWNFLAG_NOT_EASY) == 0)
				++Easy;

			if ((SFlags2 & SPAWNFLAG_NOT_MEDIUM) == 0)
				++Medium;

			if ((SFlags2 & SPAWNFLAG_NOT_HARD) == 0)
				++Hard;
		}

		SFlags = SetFlags (SFlags, SPAWNFLAG_NOT_EASY, Easy, TrigCount);
		SFlags = SetFlags (SFlags, SPAWNFLAG_NOT_MEDIUM, Medium, TrigCount);
		SFlags = SetFlags (SFlags, SPAWNFLAG_NOT_HARD, Hard, TrigCount);

		// No trigger at all ?
		NoTrigger = SFlags == (SPAWNFLAG_NOT_EASY | SPAWNFLAG_NOT_MEDIUM | SPAWNFLAG_NOT_HARD);

		// No trigger for func_spawn* or trigger_teleport might cause invalid monster counts
		if (SFlags == 0 || NoTrigger && strncmp (e->classname, "func_spawn", 10) &&
				strcmp (e->classname, "trigger_teleport"))
			continue; // Trigger included in all or no skills (the latter should be hard to miss ...)

		logprintf ("WARNING: Entity %s has %s", EntName (e), TrigCount == 1 ? "no trigger" : "too few triggers");

		if (!NoTrigger)
			logprintf (" in %sskill", Skill (SFlags));

		logprintf ("\n");
	}
}

/*
==================
FixMinMax
==================
*/
void FixMinMax (int EntLight, qboolean Min)
{
	int	 *PWLight, MinValue;
	qboolean CmdLine;

	PWLight = Min ? &worldminlight : &worldmaxlight;
	MinValue = Min ? 1 : 0;

	CmdLine = *PWLight >= 0;

	if (!CmdLine && EntLight >= MinValue)
		*PWLight = EntLight;

	if (*PWLight >= 0)
		logprintf ("Using %slight value %i from %s\n", Min ? "min" : "max", *PWLight, CmdLine ? "command line" : "worldspawn");
}

#define SNAP_EPSILON 0.000001

/*
==================
SnapVal2
==================
*/
vec_t SnapVal2 (vec_t Val, vec_t Snap)
{
	if (fabs (Val - Snap) < SNAP_EPSILON)
		Val = Snap;

	return Val;
}

/*
==================
SnapVal
==================
*/
void SnapVal (vec3_t SunMangle)
{
	int i;

	// Snap SunMangle array to -1/0/1 if value is close enough
	for (i = 0; i < 3; ++i)
	{
		SunMangle[i] = SnapVal2 (SunMangle[i], floor (SunMangle[i]));
		SunMangle[i] = SnapVal2 (SunMangle[i], ceil (SunMangle[i]));
	}
}

/*
==================
FixSunMangle
==================
*/
void FixSunMangle (int Val[3], vec3_t SunMangle)
{
	if (Val[2] == -1)
		return; // Val not set

	// Precalculate sun vector and
	// make it too large to fit into the map
	SunMangle[0] = cos (ToRad (Val[0])) * cos (ToRad (Val[1]));
	SunMangle[1] = sin (ToRad (Val[0])) * cos (ToRad (Val[1]));
	SunMangle[2] = sin (ToRad (Val[1]));

	// NOTE: Due to numerical inaccuracies above in trig functions, explicitly setting
	// sun mangle to 0 -90 will not render same result as default (also 0 -90). This is
	// because the early gate in SkyLightFace will block also extremely small negative dist
	// values if not -soft is enabled. For TyrLite compatibility, this bug is maintained

	if (!TyrCompatible)
		SnapVal (SunMangle);

	VectorNormalize (SunMangle);
	VectorScale (SunMangle, -16384, SunMangle);
}

/*
==================
FixSunGroup
==================
*/
void FixSunGroup (int Pitch, int *PSun)
{
	int Val[3], i;

	Val[0] = 0;
	Val[1] = Pitch;
	Val[2] = 0; // We already know 2nd sunlight is enabled here

	for (i = 0; i < NoOfHSuns; ++i)
	{
		FixSunMangle (Val, SunMangle[(*PSun) ++]);
		Val[0] = (Val[0] + 360 / NoOfHSuns) % 360;
	}
}

/*
==================
KeysDetected
==================
*/
void KeysDetected (a_ent_t *AllEnts)
{
	qboolean ArghTyr, ArghLite, TyrLite, IKLite, LightDLX;

	// Display detected keys
	ArghTyr = AllEnts->NumAnti > 0 || AllEnts->NumMangle > 0 || AllEnts->NumWait > 0 || AllEnts->MinLight;

	ArghLite = ArghTyr && AllEnts->NumDelay == 0 && !AllEnts->SunLight;
	TyrLite = (ArghTyr || AllEnts->NumDelay > 0 || AllEnts->SunLight) && !AllEnts->SunLight2;
	IKLite = AllEnts->NumAngle > 0;
	LightDLX = AllEnts->NumDLX > 0;

	if (ArghLite)
		logprintf ("ArghLite ");

	if (TyrLite)
		logprintf ("TyrLite ");

	if (IKLite)
		logprintf ("IKLite ");

	if (LightDLX)
		logprintf ("LightDLX ");

	if (ArghLite || TyrLite || IKLite || LightDLX)
		logprintf ("keys detected\n");

	if (AllEnts->NumLights != 0 && AllEnts->NumCompressed == AllEnts->NumLights)
		logprintf ("Entity data possibly compressed\n");
}

/*
==================
Recommend
==================
*/
void Recommend (a_ent_t	*AllEnts)
{
	int	 UnsupportedKeys, UnsupportedEnts;
	char	 *Option;
	qboolean IKLiteBest, DLXBest, AntiDisabled;

	// Figure out proper tool recommendation
	DLXBest = AllEnts->DLXEnts > 2 * AllEnts->IKLiteEnts && AllEnts->DLXEnts > 2 * AllEnts->TyrLiteEnts;
	IKLiteBest = !DLXBest && AllEnts->IKLiteEnts > 2 * AllEnts->TyrLiteEnts;

	if (DLXBest)
		UnsupportedKeys = UnsupportedEnts = AllEnts->DLXEnts;
	else if (IKLiteBest)
		UnsupportedKeys = UnsupportedEnts = AllEnts->IKLiteEnts;
	else
	{
		UnsupportedKeys = AllEnts->TyrLiteKeys;
		UnsupportedEnts = AllEnts->TyrLiteEnts;
	}

	// Antilights specifically disabled and effective ?
	AntiDisabled = NoAntiOption && AllEnts->NumAnti > 0;

	logprintf ("WARNING: %d unsupported light keys found in %d entities, ", UnsupportedKeys, UnsupportedEnts);

	if (DLXBest || IKLiteBest)
		logprintf ("enable %s option\n", DLXBest ? "dlx" : "iklite");
	else if (OldLight || ArghLiteMode || AntiDisabled)
	{
		// Suggest disabling options
		if (AntiDisabled)
			Option = "noanti";
		else if (LightDLXMode)
			Option = "dlx";
		else if (IKLiteMode)
			Option = "iklite";
		else if (OldLight)
			Option = "oldlight";
		else
			Option = "arghlite";

		logprintf ("disable %s option\n", Option);
	}
	else
		logprintf ("TyrLite recommended\n");
}

/*
==================
CalcCasts
==================
*/
char *CalcCasts (void)
{
	vec3_t	    fofs;
	float	    NumCasts;
	int	    i, NumLights;
	char	    CastChar;
	static char Str[100];

	// Calculate # total surfpts
	fofs[0] = fofs[1] = fofs[2] = 0;
	PreScan = true; // Prevent ray tracing

	for (i = 0; i < numfaces; ++i)
		LightFace (i, fofs);

	PreScan = false;

	// Calculate # actual light emitting entities
	for (i = NumLights = 0; i < num_entities; ++i)
	{
		if (entities[i].light != 0)
			++NumLights;
	}

	// Calculate # total casts
	NumCasts = (NumLights + NoOfSuns) * (float) NumSurfPts / (1024 * 1024);

	CastChar = 'M';

	if (NumCasts >= 1000)
	{
		// Whole lotta casts ...
		NumCasts /= 1024;
		CastChar = 'G';
	}

	if (NumCasts >= 100)
		sprintf (Str, "%.0f%c casts", NumCasts, CastChar);
	else
		sprintf (Str, "%.1f%c casts", NumCasts, CastChar);

	return Str;
}

/*
==================
IsValidFormula
==================
*/
qboolean IsValidFormula (int Formula)
{
	switch (Formula)
	{
	case FM_LINEAR   :
	case FM_INVERSE  :
	case FM_INVERSE2 :
	case FM_INFINITE :
	case FM_LOCMIN   :
	case FM_INVERSE3 : return true;
	}

	return false;
}


void CalibrateSunColor (float *slc)
{
	if (slc[0] > 4080 || slc[1] > 4080 || slc[2] > 4080)
	{
		slc[0] /= 255.0;
		slc[1] /= 255.0;
		slc[2] /= 255.0;
	}
}


/*
==================
LoadEntities
==================
*/
void LoadEntities (void)
{
	char 	 *data;
	t_ent_t	 ThisEnt;
	a_ent_t	 AllEnts;
	entity_t *entity;
	char	 key[MAX_KEY], Str[100 + MAX_KEY];
	epair_t	 *epair, *prev;
	double	 vec[4];
	int	 i, /*Val,*/ NoOfVals, Sun;
	float Val;
	qboolean ForceMin, OldFormat;
	float ScaleCalibration = -666;

	data = dentdata;
	//
	// start parsing
	//
	num_entities = 0;
	memset (&AllEnts, 0, sizeof (AllEnts));

	// go through all the entities
	while (1)
	{
		// parse the opening brace
		data = COM_Parse (data);

		if (!data)
			break;

		if (com_token[0] != '{')
			Error ("LoadEntities: found %s when expecting { in entity %d", com_token, num_entities);

		if (num_entities == MAX_MAP_ENTITIES)
			Error ("LoadEntities: MAX_MAP_ENTITIES (%d) exceeded", MAX_MAP_ENTITIES);

		entity = &entities[num_entities];
		entity->anglesense = -1;
		num_entities++;

		memset (&ThisEnt, 0, sizeof (ThisEnt));
		ThisEnt.Compressed = true;
		ThisEnt.Dist = ThisEnt.Range = ThisEnt.Formula = ThisEnt.MaxLight = -1;

		// go through all the keys in this entity

		// Delay warning/error messages until all keys parsed and classname is known
		while (1)
		{
			int c;

			// parse key
			data = COM_Parse (data);

			if (!data)
				Error ("LoadEntities: EOF without closing brace in entity %d", num_entities);

			if (!strcmp (com_token, "}"))
				break;

			if (strlen (com_token) >= MAX_KEY)
				Error ("LoadEntities: Key length > %i in entity %d", MAX_KEY - 1, num_entities);

			strcpy (key, com_token);

			// parse value
			data = COM_Parse (data);

			if (!data)
				Error ("LoadEntities: EOF without closing brace in entity %d", num_entities);

			c = com_token[0];

			if (c == '}')
				Error ("LoadEntities: closing brace without data in entity %d", num_entities);

			if (strlen (com_token) >= MAX_VALUE)
				Error ("LoadEntities: Value length > %i in entity %d", MAX_VALUE - 1, num_entities);

			epair = malloc (sizeof (epair_t));
			memset (epair, 0, sizeof (epair));
			epair->key = copystring (key);
			epair->value = copystring (com_token);

			if (NoReverse)
			{
				// Prevent reverse entity key order
				if (entity->epairs == NULL)
					entity->epairs = epair;
				else
					prev->next = epair;

				prev = epair;
			}
			else
			{
				// Old style
				epair->next = entity->epairs;
				entity->epairs = epair;
			}

			if (!strcmp (key, "classname") && c != 0)
				entity->classname = copystring (com_token);
			else if (IsMultiTarget (key, "target", 6))
			{
				ThisEnt.Compressed = false;
				AddMultiTarget (&entity->target, com_token);
			}
			else if (IsMultiTarget (key, "killtarget", 10))
				AddMultiTarget (&entity->killtarget, com_token);
			else if (IsMultiTarget (key, "targetname", 10))
				AddMultiTarget (&entity->targetname, com_token);
			else if (!strcmp (key, "origin"))
			{
				// scan into doubles, then assign
				// which makes it vec_t size independent
				if (sscanf (com_token, "%lf %lf %lf", &vec[0], &vec[1], &vec[2]) != 3)
					ThisEnt.OriginErr = true;
				else
				{
					for (i = 0; i < 3; i++)
						entity->origin[i] = vec[i];

					entity->origin_set = true;
				}
			}
			else if (!strncmp (key, "light", 5) || !strcmp (key, "_light"))
			{
				ThisEnt.Compressed = false;
				Val = atof (com_token);

				// Check for multiple light values
				if (entity->light != 0 && entity->light != Val)
				{
					ThisEnt.Multi = true;

					if (key[0] == '_')
						Val = entity->light; // Prioritize "light" key
				}

				entity->light = Val;

				ThisEnt.Anti = entity->light < 0; // Antilight

				if (ThisEnt.Anti && NoAnti)
					++ThisEnt.StrangeKeys;
			}
			else if (!strcmp (key, "message"))
			{
				// Limited support for max only
				NoOfVals = sscanf (com_token, "max %d", &Val);

				ThisEnt.DLX = NoOfVals == 1 && Val > 0;

				if (ThisEnt.DLX)
					entity->addmax = Val;
			}
			else if (!strcmp (key, "maxlight") || !strcmp (key, "_maxlight"))
				ThisEnt.MaxLight = atoi (com_token);
			else if (!strcmp (key, "style"))
			{
				ThisEnt.Compressed = false;
				entity->style = atoi (com_token);
			}
			else if (!strcmp (key, "angle"))
			{
				ThisEnt.Compressed = false;
				entity->angle = atof (com_token);
				ThisEnt.Angle = entity->angle >= 1 && entity->angle <= 3;
			}
			else if (!strcmp (key, "_softangle"))
				entity->softangle = atof (com_token);
			else if (!strcmp (key, "_anglesense"))
				entity->anglesense = atof (com_token);
			else if (!strcmp (key, "wait"))
			{
				ThisEnt.Compressed = false;
				entity->dist = atof (com_token);

				// Only count keys that have non-default values
				ThisEnt.Wait = entity->dist > 0 && entity->dist != 1;

				if (ThisEnt.Wait && OldLight)
					++ThisEnt.StrangeKeys;
			}
			else if (!strcmp (key, "delay"))
			{
				ThisEnt.Compressed = false;
				entity->formula = atoi (com_token);

				// Only count keys that have non-default values
				ThisEnt.Delay = IsValidFormula (entity->formula) && entity->formula != FM_LINEAR;

				if (ThisEnt.Delay && (OldLight || ArghLiteMode))
					++ThisEnt.StrangeKeys;
			}
			else if (!strcmp (key, "mangle"))
			{
				ThisEnt.Compressed = false;
				strcpy (ThisEnt.MangleStr, com_token);
				ThisEnt.Mangle = true;

				if (OldLight)
					++ThisEnt.StrangeKeys;

				// Make sure mangle warnings are always displayed,
				// regardless of classname or oldlight option
				NoOfVals = sscanf (ThisEnt.MangleStr, "%lf %lf %lf", &vec[0], &vec[1], &vec[2]);

				if (NoOfVals < 2 || NoOfVals > 3)
					ThisEnt.MangleErr = true;
				else if (!OldLight)
				{
					// Precalculate the direction vector
					entity->use_mangle = true;
					entity->mangle[0] = cos (ToRad (vec[0])) * cos (ToRad (vec[1]));
					entity->mangle[1] = sin (ToRad (vec[0])) * cos (ToRad (vec[1]));
					entity->mangle[2] = sin (ToRad (vec[1]));
					VectorNormalize (entity->mangle);
				}
			}
			// CSL - epca@powerup.com.au
			else if (!strcmp (key, "_color") || !strcmp (key, "color"))
			{
				// ensure that anything not read has a value
				vec[0] = vec[1] = vec[2] = vec[3] = 0;

				// scan into doubles, then assign
				// which makes it vec_t size independent
				// don't error-out if < 3 because Quoth 2 (and possibly others) allows < 3 components in color keys for some entity types... grrr...
				sscanf (com_token, "%lf %lf %lf", &vec[0], &vec[1], &vec[2]);

				for (i = 0; i < 3; i++)
					entity->lightcolour[i] = (vec[i] * 255);
			}
			// CSL
			else if (!strcmp (key, "_sunlight") && SunLight[0] == -1)
			{
				SunLight[0] = atof (com_token);
				AllEnts.SunLight = ThisEnt.Sun = SunLight[0] > 0;
			}
			else if (!strcmp (key, "_sunlight2") && SunLight[1] == -1)
			{
				SunLight[1] = atof (com_token);
				AllEnts.SunLight2 = SunLight[1] > 0;
			}
			else if (!strcmp (key, "_sunlight3") && SunLight[1] == -1)
			{
				SunLight[1] = atof (com_token);
				AllEnts.SunLight2 = SunLight[1] > 0;

				if (ShadowSense == -1)
					ShadowSense = SHADOWSENSE;
			}
			else if (!strcmp (key, "_sunlight_color") || !strcmp (key, "_sunlight_color"))
			{
				// ensure that anything not read has a value
				vec[0] = vec[1] = vec[2] = vec[3] = 0;

				// scan into doubles, then assign
				// which makes it vec_t size independent
				// don't error-out if < 3 because Quoth 2 (and possibly others) allows < 3 components in color keys for some entity types... grrr...
				sscanf (com_token, "%lf %lf %lf", &vec[0], &vec[1], &vec[2]);

				// this entity is going to be worldspawn
				for (i = 0; i < 3; i++)
					SunLightColor[0][i] = (vec[i] * 255);
			}
			else if (!strcmp (key, "_sunlight_color2") || !strcmp (key, "_sunlight_color2"))
			{
				// ensure that anything not read has a value
				vec[0] = vec[1] = vec[2] = vec[3] = 0;

				// scan into doubles, then assign
				// which makes it vec_t size independent
				// don't error-out if < 3 because Quoth 2 (and possibly others) allows < 3 components in color keys for some entity types... grrr...
				sscanf (com_token, "%lf %lf %lf", &vec[0], &vec[1], &vec[2]);

				// this entity is going to be worldspawn
				for (i = 0; i < 3; i++)
					SunLightColor[1][i] = (vec[i] * 255);
			}
			else if (!strcmp (key, "_sunlight_color3") || !strcmp (key, "_sunlight_color3"))
			{
				// scan into doubles, then assign
				// which makes it vec_t size independent
				if (sscanf (com_token, "%lf %lf %lf", &vec[0], &vec[1], &vec[2]) != 3)
					Error ("LoadEntities: not 3 values for colour");

				// this entity is going to be worldspawn
				for (i = 0; i < 3; i++)
					SunLightColor[1][i] = (vec[i] * 255);
			}
			else if (!strcmp (key, "_sun_mangle") && SunMangleVal[2] == -1)
			{
				NoOfVals = sscanf (com_token, "%lf %lf %lf", &vec[0], &vec[1], &vec[2]);

				if (NoOfVals < 2 || NoOfVals > 3)
					Error ("LoadEntities: not 2/3 values for _sun_mangle");

				if (NoOfVals == 2)
					AllEnts.SunLight2 = true;

				OldFormat = false;

				for (i = 0; i < NoOfVals; ++i)
				{
					if (fabs (vec[i]) > 10)
						break;
				}

				if (i == 3 && vec[2] < 0)
				{
					// Probably old TyrLite format (x y z)
					OldFormat = true;
					vec[3] = ToDegree (atan2 (vec[1], vec[0]));
					vec[1] = ToDegree (atan2 (vec[2], sqrt (vec[0] * vec[0] + vec[1] * vec[1])));
					vec[0] = vec[3];
				}

				vec[2] = 0;

				for (i = 0; i < 3; ++i)
					SunMangleVal[i] = Q_rint (vec[i]);

				if (OldFormat)
					logprintf ("Converting old _sun_mangle format into \"%d %d\"\n", SunMangleVal[0], SunMangleVal[1]);
			}
			else if (!strcmp (key, "_dist") && scaledist == -1)
				ThisEnt.Dist = atof (com_token);
			else if (!strcmp (key, "_range") && rangescale == -1)
				ThisEnt.Range = atof (com_token);
		}

		// all fields have been parsed
		if (entity->classname == NULL)
			Error ("LoadEntities: entity %d has no classname", num_entities);

		sprintf (Str, " in entity %d, %s", num_entities, entity->classname);

		if (!strncmp (entity->classname, "light", 5))
		{
			entity->islight = true;

			if (entity->lightcolour[0] > ScaleCalibration) ScaleCalibration = entity->lightcolour[0];
			if (entity->lightcolour[1] > ScaleCalibration) ScaleCalibration = entity->lightcolour[1];
			if (entity->lightcolour[2] > ScaleCalibration) ScaleCalibration = entity->lightcolour[2];

			if (strcmp (entity->classname, "light"))
				ThisEnt.Sourced = true;

			if (!entity->light)
				entity->light = DEFAULTLIGHTLEVEL;

			if (ThisEnt.Anti)
				++AllEnts.NumAnti;

			if (ThisEnt.DLX)
			{
				++AllEnts.NumDLX;

				if (!LightDLXMode)
					++AllEnts.DLXEnts;
			}

			if (ThisEnt.Angle)
				++AllEnts.NumAngle;

			if (ThisEnt.Wait)
				++AllEnts.NumWait;

			if (ThisEnt.Delay)
				++AllEnts.NumDelay;

			if (ThisEnt.Mangle)
				++AllEnts.NumMangle;

			if (ThisEnt.StrangeKeys != 0)
			{
				AllEnts.TyrLiteKeys += ThisEnt.StrangeKeys;
				++AllEnts.TyrLiteEnts;
			}

			if (UnsupDetails)
			{
				if (ThisEnt.DLX && !LightDLXMode)
					logprintf ("DLX value %d%s\n", entity->addmax, Str);

				if (ThisEnt.Wait && OldLight)
					logprintf ("wait key %g%s\n", entity->dist, Str);

				if (ThisEnt.Delay && (OldLight || ArghLiteMode))
					logprintf ("delay key %d%s\n", entity->formula, Str);

				if (ThisEnt.Mangle && OldLight)
					logprintf ("mangle key \'%s\'%s\n", ThisEnt.MangleStr, Str);

				if (ThisEnt.Anti && NoAnti)
					logprintf ("Antilight %d%s\n", entity->light, Str);
			}

			if (IKLiteMode)
			{
				// Translate to native formula
				switch ((int) Q_rint (entity->angle))
				{
				case 1 : ThisEnt.Formula = FM_INVERSE2;
					break;
				case 2 : ThisEnt.Formula = FM_INFINITE;
					break;
				case 3 : ThisEnt.Formula = FM_INVERSE;
					break;
				default: ThisEnt.Formula = FM_LINEAR;
					break;
				}

				entity->angle = 0; // IKLite and spotlights don't mix well
			}
			else if (AllEnts.IKLiteEnts != -1)
			{
				if (entity->angle > 3)
					AllEnts.IKLiteEnts = -1; // Probably not IKLite
				else if (ThisEnt.Angle)
				{
					++AllEnts.IKLiteEnts;

					if (UnsupDetails)
						logprintf ("Possible IKLite angle %g%s\n", entity->angle, Str);
				}
			}

			AllEnts.NumLights++;

			if (ThisEnt.Compressed)
				++AllEnts.NumCompressed;
		}

		// Following tests must be done regardless of classname

		if (ThisEnt.OriginErr)
			Error ("LoadEntities: not 3 values for origin%s", Str);

		if (ThisEnt.Multi)
			logprintf ("WARNING: LoadEntities: Multiple lights%s\n", Str);

		if (NoAnti && entity->light < 0)
			entity->light = 0; // No antilights

		AllEnts.LightSum += entity->light;

		if (LightCap > 0 && entity->light > LightCap)
		{
			// Enforce high limit for each entity
			AllEnts.CappedSum += entity->light - LightCap;
			entity->light = LightCap;
			++AllEnts.CappedLights;
		}

		if (LightDLXMode)
		{
			if (entity->addmax == 0)
				entity->addmax = 255;
		}
		else if (IKLiteMode)
			entity->addmax = 2 * entity->light; // IKLite seems to use this too
		else
			entity->addmax = 0;

		if ((unsigned) entity->style > 254)
			Error ("Bad light style %i (must be 0-254)%s", entity->style, Str);

		if (entity->softangle < 0 || entity->angle - entity->softangle < 0)
			entity->softangle = 0; // Default set spotlight soft angle

		if (entity->anglesense < 0 || entity->anglesense > 1)
			entity->anglesense = scalecos; // Default set angle sensitivity

		if (!TyrLiteMode || entity->islight)
		{
			// TyrLite disables lights indirectly in some non-light entities
			// by not default setting these members. However, if dist or formula
			// is specified, light will still work and this creates a conflict
			// with the soft option. See below for compensation
			if (OldLight || entity->dist <= 0)
				entity->dist = 1.0; // Same default as for dist option

			if (KinnDelay && entity->formula == FM_INVERSE2)
				entity->formula = FM_INVERSE3; // Translate delay 2 into delay 5

			if (OldLight || ArghLiteMode || !IsValidFormula (entity->formula))
				entity->formula = FM_LINEAR; // Default original (linear) falloff
		}

		if (IKLiteMode && ThisEnt.Formula != -1)
			entity->formula = ThisEnt.Formula;

		if (!entity->islight && entity->formula == FM_LOCMIN)
			entity->formula = FM_LINEAR; // No local minlights in non-light entities

		if (IKAngle && entity->anglesense == scalecos)
		{
			// Override angle sensitivity
			switch (entity->formula)
			{

			case FM_INVERSE2 : if (!IKLiteMode)
					entity->anglesense = 0; // No angle sensitivity

				break;
			case FM_INFINITE : entity->anglesense = 1; // More angle sensitivity
				break;
			}
		}

		if (ThisEnt.MangleErr)
			logprintf ("WARNING: LoadEntities: not 2/3 values for mangle%s\n", Str);

		if (entity->islight && entity->targetname != NULL)
		{
			if (!ThisEnt.Sourced)
			{
				// Make sure switchable lights are always recalculated
				if (entity->style == 0 || entity->style >= OFFSET_LIGHT_TARGETS)
				{
					char s[16];

					entity->style = LightStyleForTargetname (entity->targetname[0], true); // No multi targets for lights
					sprintf (s, "%i", entity->style);
					SetKeyValue (entity, "style", s);
				}
				else
					logprintf ("WARNING: LoadEntities: Targeted light '%s' with style %d set%s\n",
							   entity->targetname[0], entity->style, Str); // No multi targets for lights
			}
			else
				logprintf ("WARNING: LoadEntities: Targeted sourced light '%s'%s\n",
						   entity->targetname[0], Str); // No multi targets for lights
		}

		// Must be done after switchable lights check
		if (NoFlash)
		{
			// Disable flashing lights
			switch (entity->style)
			{
			case  3 :
			case  4 :
			case  6 :
			case  7 :
			case  8 :
			case  9 :
			case 10 : entity->style = 0;
				++AllEnts.NoFlash;
				break;
			}
		}

		if (!strcmp (entity->classname, "worldspawn"))
		{
			if (++AllEnts.WorldSpawns > 1)
				logprintf ("WARNING: Multiple worldspawn entities\n");

			AllEnts.MinLight = entity->light > 0;

			FixMinMax (entity->light, true);
			FixMinMax (ThisEnt.MaxLight, false);

			if (!GenCompatible || TyrLiteMode && SoftLight > 0)
				entity->light = 0; // Might otherwise cast strange light as a light entity

			ForceMin = false;

			if (worldminlight > worldmaxlight && worldmaxlight >= 0)
			{
				worldminlight = worldmaxlight; // Maxlight overrides minlight
				ForceMin = true;
			}

			if (NoLight && worldminlight <= 0)
			{
				worldminlight = 1; // Otherwise map will be fullbright
				ForceMin = true;
			}

			if (ForceMin)
				logprintf ("Forcing minlight value %d\n", worldminlight);

			if (ThisEnt.Dist != -1)
				scaledist = ThisEnt.Dist;

			if (ThisEnt.Range != -1)
				rangescale = ThisEnt.Range;

			if (ThisEnt.StrangeKeys != 0)
			{
				AllEnts.TyrLiteKeys += ThisEnt.StrangeKeys;
				++AllEnts.TyrLiteEnts;
			}
		}
		else
		{
			if (ThisEnt.Sun)
				logprintf ("WARNING: Sunlight misplaced%s\n", Str);

			if (TyrLiteMode && !entity->islight && SoftLight > 0)
				entity->light = 0; // TyrLite disables lights in some non-light entities

			if ((NoLight || SrcLight && !ThisEnt.Sourced) && entity->formula != FM_LOCMIN)
				entity->light = 0; // Disable all (or all unsourced) non-ambient light entities
		}
	}

	for (i = 0, entity = entities; i < num_entities; i++, entity++)
	{
		if (!entity->lightcolour[0] && !entity->lightcolour[1] && !entity->lightcolour[2])
		{
			entity->lightcolour[0] = 255;
			entity->lightcolour[1] = 255;
			entity->lightcolour[2] = 255;
		}
		else
		{
			// assume that this is a reasonable cutoff point for switching scale
			if (ScaleCalibration > 4080)
			{
				entity->lightcolour[0] /= 255;
				entity->lightcolour[1] /= 255;
				entity->lightcolour[2] /= 255;
			}
		}
	}

	if (SunLight[0] > 0)
	{
		if (SunMangleVal[2] != -1)
		{
			if (SunMangleVal[1] >= 0 || SunMangleVal[1] < -90)
				logprintf ("WARNING: Possibly bad sun mangle pitch %d\n", SunMangleVal[1]);
		}

		if (FakeGISunlight2)
		{
			NoOfHSuns = NOOFHSUNS_GI; //no speed settings.
			NoOfVSuns = NOOFVSUNS_GI;
		}
		else
		{
			NoOfHSuns = NOOFHSUNS;
			// Speed
			if (OverSample == 1)
				NoOfHSuns = NOOFHSUNS_OVERSAMPLE1;
			else if (OverSample == 2)
				NoOfHSuns = NOOFHSUNS_OVERSAMPLE2;
			
			NoOfVSuns = NOOFVSUNS;
		}

		NoOfSuns = (NoOfHSuns * NoOfVSuns) + 1; //add one extra for the primary sun

		for (i = 0; i < NoOfSuns; ++i)
		{
			// Defaults to straight down
			SunMangle[i][0] = SunMangle[i][1] = 0;
			SunMangle[i][2] = 16384;
		}

		Sun = 0;

		FixSunMangle (SunMangleVal, SunMangle[Sun++]);

		if (SunLight[1] <= 0)
			NoOfSuns = 1; // No 2nd sunlight
		else
		{
			// Try to eliminate bad shadows by reaching every outdoor corner

			
			if (FakeGIMode || FakeGISunlight2) //fake GI modes place the suns in a full sphere
			{
				for (i = 0; i < NoOfVSuns; ++i)
					FixSunGroup (-(90 - 1 - (i * (178) / NoOfVSuns)), &Sun);
			}
			else // Arrange the rest of the suns at even intervals around the upper half-sphere
			{
				for (i = 0; i < NoOfVSuns; ++i)
					FixSunGroup (-(90 - 1 - (i * (90 - 30) / NoOfVSuns)), &Sun);
			}
		}
	}

	CalibrateSunColor (SunLightColor[0]);
	CalibrateSunColor (SunLightColor[1]);

	// Default set dist/range if not set
	if (scaledist == -1)
		scaledist = 1.0;

	if (rangescale == -1)
		rangescale = IKLiteMode ? 1 : 0.5; // Default range in IKLite is doubled

	if (!IKLiteMode)
	{
		DistFactor1 = 128;
		DistFactor2 = 16384;
	}
	else
	{
		// IKLite values empirically determined
		DistFactor1 = 80;
		DistFactor2 = 8192;
	}

	if (DetectKeys)
		KeysDetected (&AllEnts);

	if (AllEnts.TyrLiteEnts > 0 || AllEnts.IKLiteEnts > 0 || AllEnts.DLXEnts > 0)
		Recommend (&AllEnts);

	if (LightCap > 0)
	{
		logprintf ("%d lights capped", AllEnts.CappedLights);

		if (AllEnts.CappedLights > 0)
			logprintf (", total %d (%g%%)", AllEnts.CappedSum, Q_rint ((vec_t) AllEnts.CappedSum * 100 / AllEnts.LightSum));

		logprintf ("\n");
	}

	if (NoFlash)
		logprintf ("%d flashing lights disabled\n", AllEnts.NoFlash);

	if (!NoAnti && AllEnts.NumAnti > 0)
		AntiLights = true;

	if (NoOfSuns <= 1 || ShadowSense == -1)
		ShadowSense = 0; // Default value
	else
		logprintf ("Shadow sensitivity %g set\n", ShadowSense);

	logprintf ("%d entities read, %d are lights, %d faces, %s\n", num_entities, AllEnts.NumLights, numfaces, CalcCasts());
	MatchTargets ();

	if (!NoSkillChk)
		MatchSkillTargets ();

	if (numlighttargets > 0)
		logprintf ("%i switchable light styles\n", numlighttargets);
}

char 	*ValueForKey (entity_t *ent, char *key)
{
	epair_t	*ep;

	for (ep = ent->epairs; ep; ep = ep->next)
		if (!strcmp (ep->key, key))
			return ep->value;

	return "";
}

void 	SetKeyValue (entity_t *ent, char *key, char *value)
{
	epair_t	*ep;

	for (ep = ent->epairs; ep; ep = ep->next)
		if (!strcmp (ep->key, key))
		{
			free (ep->value);
			ep->value = copystring (value);
			return;
		}

	ep = malloc (sizeof (*ep));
	ep->next = ent->epairs;
	ent->epairs = ep;
	ep->key = copystring (key);
	ep->value = copystring (value);
}

entity_t *FindEntityWithKeyPair (char *key, char *value)
{
	entity_t	*ent;
	epair_t		*ep;
	int		i;

	for (i = 0; i < num_entities; i++)
	{
		ent = &entities[ i ];

		for (ep = ent->epairs; ep; ep = ep->next)
		{
			if (!strcmp (ep->key, key))
			{
				if (!strcmp (ep->value, value))
				{
					return ent;
				}

				break;
			}
		}
	}

	return NULL;
}

void 	GetVectorForKey (entity_t *ent, char *key, vec3_t vec)
{
	char	*k;
	double	v1, v2, v3;

	k = ValueForKey (ent, key);
	v1 = v2 = v3 = 0;
	// scanf into doubles, then assign, so it is vec_t size independent
	sscanf (k, "%lf %lf %lf", &v1, &v2, &v3);
	vec[0] = v1;
	vec[1] = v2;
	vec[2] = v3;
}



/*
================
WriteEntitiesToString
================
*/
void WriteEntitiesToString (void)
{
	char	*buf, *end;
	epair_t	*ep;
	char	line[MAX_KEY + MAX_VALUE + 10];
	int	i, len;

	// Alloc temporary max entity lump size
	buf = malloc (MAX_MAP_ENTSTRING);

	end = buf;
	*end = 0;

	for (i = 0; i < num_entities; i++)
	{
		ep = entities[i].epairs;

		if (!ep)
			continue;	// ent got removed

		strcat (end, "{\n");
		end += 2;

		for (ep = entities[i].epairs; ep; ep = ep->next)
		{
			len = sprintf (line, "\"%s\" \"%s\"\n", ep->key, ep->value);

			if (len > 128)
			{
				strcpy (&line[128 - 2], "\"\n"); // Cut off string at 128
				len = 128;
			}

			strcat (end, line);
			end += len;
		}

		strcat (end, "}\n");
		end += 2;

		if (end > buf + MAX_MAP_ENTSTRING)
			Error ("Entity text too long");
	}

	entdatasize = end - buf + 1;

	// Realloc entity lump with new size
	if (dentdata != NULL)
		free (dentdata);

	dentdata = malloc (entdatasize);
	memcpy (dentdata, buf, entdatasize);
	free (buf);
}

