
#include "cmdlib.h"
#include "mathlib.h"
#include "bspfile.h"
#include "entities.h"

#define	ON_EPSILON	0.1
#define ANGLE_EPSILON 0.001

#define FM_LINEAR	0
#define FM_INVERSE	1
#define FM_INVERSE2	2
#define FM_INFINITE	3
#define FM_LOCMIN	4
#define FM_INVERSE3	5

#define	MAXLIGHTS	1024
#define	NOOFHSUNS	32
//#define	NOOFSUNS	(1 + NOOFHSUNS * NOOFVSUNS)
#define	NOOFSUNS		(1 + NOOFHSUNS_GI * NOOFVSUNS_GI)
/*
	Above is evil and hacky.  Since NOOFSUNS is no longer known (fakeGI mode has more than normal mode) we just make
	the array overly large to make sure we don't go over it.  This is sad but I suck.
*/
#define	NOOFVSUNS	4
#define SHADOWSENSE	0.4
#define SOFTDIST	3

//FakeGI Sunlight settings
#define	NOOFHSUNS_GI	16
//#define	NOOFSUNS_GI		(1 + NOOFHSUNS_GI * NOOFVSUNS_GI)
#define	NOOFVSUNS_GI	16

//extra settings for speed
#define	NOOFHSUNS_OVERSAMPLE1	6
#define	NOOFHSUNS_OVERSAMPLE2	16


void LoadNodes (char *file);

qboolean TestLine (vec3_t start, vec3_t stop);
qboolean TestSky (vec3_t start, vec3_t dirn);

void LightFace (int surfnum, vec3_t faceoffset);
char *GetTexName (int texindex);

void MakeTnodes (dmodel_t *bm);

extern	float		scaledist;
extern	float		scalecos;
extern	float		ShadowSense;
extern	float		rangescale;
extern	float		GateVal;
extern  int	        worldminlight;
extern  int	        worldmaxlight;

extern	float		SunLight[2];
extern	int		SunMangleVal[3];
extern	float		SunLightColor[2][3];
extern	int		NoOfSuns, NoOfHSuns, NoOfVSuns; //added NoOfVSuns external variable due to normal and fakeGI modes having differing sun counts.
extern	vec3_t		SunMangle[];

vec_t ToRad (vec_t Degree);
vec_t ToDegree (vec_t Rad);

byte  *GetFileSpace (int size);

extern	byte	*filebase1;
extern	byte	*filebase3;

extern	vec3_t	bsp_origin;
extern	vec3_t	bsp_xvector;
extern	vec3_t	bsp_yvector;

void TransformSample (vec3_t in, vec3_t out);
void RotateSample (vec3_t in, vec3_t out);

extern	vec_t		DistFactor1;
extern	vec_t		DistFactor2;
extern	int		OverSample;
extern	int		SoftLight;
extern	int		SoftDist;
extern	int		SingleDist;
extern	int		SkyDist;
extern	int		LightCap;
extern	int		NumSurfPts;
extern	unsigned int	FastLight;
extern	qboolean	NoLight;
extern	qboolean	SrcLight;
extern	qboolean	OldLight;
extern	qboolean	TyrLiteMode;
extern	qboolean	TyrLite95Mode;
extern	qboolean	ArghLiteMode;
extern	qboolean	AddMinLight;
extern	qboolean	UnsupDetails;
extern	qboolean	DetectKeys;
extern	qboolean	NoSkillChk;
extern	qboolean	NoFlash;
extern	qboolean	GlobRange;
extern	qboolean	IKLiteMode;
extern	qboolean	IKAngle;
extern	qboolean	LightDLXMode;
extern	qboolean	OnlyEnts;
extern	qboolean	NoWrite;
extern	qboolean	NoReverse;
extern	qboolean	GenCompatible;
extern	qboolean	ATCompatible;
extern	qboolean	TyrCompatible;
extern	qboolean	EnhancedTP;
extern	qboolean	AntiLights;
extern	qboolean	NoAnti;
extern	qboolean	NoAntiOption;
extern	qboolean	KinnDelay;
extern	qboolean	SolidSky;
extern	qboolean	PreScan;

extern	qboolean	FakeGISunlight2;
extern	qboolean	FakeGIMode;