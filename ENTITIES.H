
#define DEFAULTLIGHTLEVEL	300
#define MAXMULTITARGETS		16

typedef struct epair_s
{
	struct epair_s	*next;
	char		*key;
	char		*value;
} epair_t;

typedef struct entity_s
{
	char		*classname;
	vec3_t		origin;
	qboolean	origin_set;
	vec_t		angle;
	vec_t		softangle;
	vec_t		anglesense;
	float		light;
	int			addmax;
	int			style;
	char		**target;
	char		**killtarget;
	char		**targetname;
	struct epair_s	*epairs;
	struct entity_s	*targetent;
	int     lightcolour[3];
	int		formula;
	vec_t		dist;
	qboolean	use_mangle;
	vec3_t		mangle;
	qboolean	islight;
} entity_t;

/* Explanation of values added to struct entity_s
*
* softangle:
*    Specifies the angle of the inner cone in a spotlight with soft transition
*
* anglesense:
*    Takes a float that controls angle sensitivity, 0-1 (default 0.5)
*
* addmax:
*    Takes a value that will limit the amount of light that will be added to each face
*
* formula:
*    Takes a value 0-3 (default 0)
*    0 - Standard lighting formula like original light
*    1 - Light fades as 1/x
*    2 - Light fades as 1/(x*x)
*    3 - Light stays same brightness regardless of distance
*    4 - Local minlight (same as 3 but non-additive)
*    5 - Similar to 2 but with more attenuation and never brighter than "light" value
*
* dist:
*    Takes a float as a value (default 1.0).
*    This reflects how fast a light fades with distance.
*    For example a value of 2 will fade twice as fast, and a value of 0.5
*    will fade half as fast. (Just like arghlite)
*
* mangle:
*    If the entity is a light, then point the spotlight in this direction.
*
* islight:
*    True if entity is light, i.e. classname begins with "light"
*/

extern	entity_t	entities[MAX_MAP_ENTITIES];
extern	int		num_entities;

entity_t *FindEntityWithKeyPair (char *key, char *value);

char 	*ValueForKey (entity_t *ent, char *key);
void 	SetKeyValue (entity_t *ent, char *key, char *value);
void 	GetVectorForKey (entity_t *ent, char *key, vec3_t vec);

void LoadEntities (void);
void WriteEntitiesToString (void);
