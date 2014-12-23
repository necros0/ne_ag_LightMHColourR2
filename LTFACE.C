
#include "light.h"

/* ======================================================================== */

// Solve three simultaneous equations
// mtx is modified by the function...
#define ZERO_EPSILON (0.001)

static qboolean LU_Decompose (vec3_t mtx[3], int r[3], int c[2])
{
	int   i, j, k;
	vec_t max;
	int   max_r, max_c;

	// Do gauss elimination
	for (i = 0; i < 3; ++i)
	{
		max = 0;
		max_r = max_c = i;

		for (j = i; j < 3; ++j)
		{
			for (k = i; k < 3; ++k)
			{
				if (fabs (mtx[j][k]) > max)
				{
					max = fabs (mtx[j][k]);
					max_r = j;
					max_c = k;
				}
			}
		}

		// Check for parallel planes
		if (max < ZERO_EPSILON)
			return false;

		// Swap rows/columns if necessary
		if (max_r != i)
		{
			for (j = 0; j < 3; ++j)
			{
				max = mtx[i][j];
				mtx[i][j] = mtx[max_r][j];
				mtx[max_r][j] = max;
			}

			k = r[i];
			r[i] = r[max_r];
			r[max_r] = k;
		}

		if (max_c != i)
		{
			for (j = 0; j < 3; ++j)
			{
				max = mtx[j][i];
				mtx[j][i] = mtx[j][max_c];
				mtx[j][max_c] = max;
			}

			k = c[i];
			c[i] = c[max_c];
			c[max_c] = k;
		}

		// Do pivot
		for (j = i + 1; j < 3; ++j)
		{
			mtx[j][i] /= mtx[i][i];

			for (k = i + 1; k < 3; ++k)
				mtx[j][k] -= mtx[j][i] * mtx[i][k];
		}
	}

	return true;
}

static void solve3 (vec3_t mtx[3], int r[3], int c[3], vec3_t rhs, vec3_t soln)
{
	vec3_t y;

	// forward-substitution
	y[0] = rhs[r[0]];
	y[1] = rhs[r[1]] - mtx[1][0] * y[0];
	y[2] = rhs[r[2]] - mtx[2][0] * y[0] - mtx[2][1] * y[1];

	// back-substitution
	soln[c[2]] = y[2] / mtx[2][2];
	soln[c[1]] = (y[1] - mtx[1][2] * soln[c[2]]) / mtx[1][1];
	soln[c[0]] = (y[0] - mtx[0][1] * soln[c[1]] - mtx[0][2] * soln[c[2]]) / mtx[0][0];
}

/* ======================================================================== */
/*
============
CalcDist

Returns the distance between the points
=============
*/
_inline vec_t CalcDist (vec3_t p1, vec3_t p2)
{
	int   i;
	vec_t t;

	for (i = t = 0; i < 3; ++i)
		t += (p2[i] - p1[i]) * (p2[i] - p1[i]);

	if (t == 0)
		t = 1;		// don't blow up...

	return sqrt (t);
}

/*
============
CastRay

Returns the distance between the points, or -1 if blocked
=============
*/
vec_t CastRay (vec3_t p1, vec3_t p2)
{
	qboolean trace;

	trace = TestLine (p1, p2);

	if (!trace)
		return -1;		// ray was blocked

	return CalcDist (p1, p2);
}

/*
===============================================================================

SAMPLE POINT DETERMINATION

void SetupBlock (dface_t *f) Returns with surfpt[] set

This is a little tricky because the lightmap covers more area than the face.
If done in the straightforward fashion, some of the
sample points will be inside walls or on the other side of walls, causing
false shadows and light bleeds.

To solve this, I only consider a sample point valid if a line can be drawn
between it and the exact midpoint of the face.  If invalid, it is adjusted
towards the center until it is valid.

(this doesn't completely work)

===============================================================================
*/

#define	SINGLEMAP (18*18*4*4) // Covers 4x4 oversampling

// Note: This structure isn't cleared via memset for speed reasons
typedef struct
{
	vec_t	 lightmaps[MAXLIGHTMAPS][SINGLEMAP];
	int	 numlightstyles;
	vec_t	 facedist;
	vec3_t	 facenormal;

	int	 numsurfpt;
	vec3_t	 surfpt[SINGLEMAP];
	qboolean locmin[SINGLEMAP]; // True if local minlight hit surfpoint in any style

	vec3_t	 texorg;
	vec3_t	 worldtotex[2];	// s = (world - texorg) . worldtotex[0]
	vec3_t	 textoworld[2];	// world = texorg + s * textoworld[0]

	// ETP begin, kludge
	vec_t    worldtotexETP[2][4]; // Copy of face->texinfo->vecs
	vec3_t   LU[3];
	int      row_p[3];
	int      col_p[3];
	// ETP end

	vec_t	exactmins[2], exactmaxs[2];

	int	texmins[2], texsize[2], size, width;
	int	lightstyles[MAXLIGHTMAPS];
	vec3_t  lightmapcolours[MAXLIGHTMAPS][SINGLEMAP];
	int	surfnum;
	dface_t	*face;
} lightinfo_t;

/*
=================
GetVertex

Returns pointer to vertex for firstedge
=================
*/
static dvertex_t *GetVertex (int firstedge)
{
	int e = dsurfedges[firstedge];

	return dvertexes + dedges[abs (e) ].v[e >= 0 ? 0 : 1];
}

/*
================
CalcFaceVectors

Fills in texorg, worldtotex. and textoworld
================
*/
void CalcFaceVectorsETP (lightinfo_t *l)
{
	texinfo_t *tex;
	int       i, j;
	dvertex_t *v;

	tex = &texinfo[l->face->texinfo];

	// convert from float to vec_t
	for (i = 0; i < 2; i++)
		for (j = 0; j < 4; j++)
			l->worldtotexETP[i][j] = tex->vecs[i][j];

	// Prepare LU and row, column permutations
	for (i = 0; i < 3; ++i)
		l->row_p[i] = l->col_p[i] = i;

	VectorCopy (l->worldtotexETP[0], l->LU[0]);
	VectorCopy (l->worldtotexETP[1], l->LU[1]);
	VectorCopy (l->facenormal, l->LU[2]);

	// Decompose the matrix. If we can't, texture axes are invalid
	if (!LU_Decompose (l->LU, l->row_p, l->col_p))
	{
		v = GetVertex (l->face->firstedge);
		logprintf ("WARNING: Bad texture axes on face near (%.0f %.0f %.0f), %s\n", v->point[0], v->point[1], v->point[2], GetTexName (l->face->texinfo));
		// Recovery possible ?
	}
}

static void tex_to_world (vec_t s, vec_t t, lightinfo_t *l, vec3_t world)
{
	int    i;
	vec3_t rhs;

	if (EnhancedTP)
	{
		rhs[0] = s - l->worldtotexETP[0][3];
		rhs[1] = t - l->worldtotexETP[1][3];
		rhs[2] = l->facedist + 1;         // one "unit" in front of surface

		solve3 (l->LU, l->row_p, l->col_p, rhs, world);
	}
	else
	{
		for (i = 0; i < 3; ++i)
			world[i] = l->texorg[i] + l->textoworld[0][i] * s + l->textoworld[1][i] * t;
	}
}

void CalcFaceVectors (lightinfo_t *l)
{
	texinfo_t *tex;
	int	  i, j;
	vec3_t	  texnormal;
	float	  distscale;
	vec_t	  dist, len;
	dvertex_t *v;

	if (EnhancedTP)
	{
		// Kludge
		CalcFaceVectorsETP (l);
		return;
	}

	tex = &texinfo[l->face->texinfo];

	// convert from float to vec_t
	for (i = 0; i < 2; i++)
		for (j = 0; j < 3; j++)
			l->worldtotex[i][j] = tex->vecs[i][j];

	// calculate a normal to the texture axis.  points can be moved along this
	// without changing their S/T
	texnormal[0] = tex->vecs[1][1] * tex->vecs[0][2]
				   - tex->vecs[1][2] * tex->vecs[0][1];
	texnormal[1] = tex->vecs[1][2] * tex->vecs[0][0]
				   - tex->vecs[1][0] * tex->vecs[0][2];
	texnormal[2] = tex->vecs[1][0] * tex->vecs[0][1]
				   - tex->vecs[1][1] * tex->vecs[0][0];
	VectorNormalize (texnormal);

	// flip it towards plane normal
	distscale = DotProduct (texnormal, l->facenormal);

	if (!distscale)
	{
		v = GetVertex (l->face->firstedge);
		logprintf ("WARNING: Texture axis perpendicular to face near (%.0f %.0f %.0f), %s\n", v->point[0], v->point[1], v->point[2], GetTexName (l->face->texinfo));
		distscale = 1; // Brutal recovery
	}

	if (distscale < 0)
	{
		distscale = -distscale;
		VectorInverse (texnormal);
	}

	// distscale is the ratio of the distance along the texture normal to
	// the distance along the plane normal
	distscale = 1 / distscale;

	for (i = 0; i < 2; i++)
	{
		len = VectorLength (l->worldtotex[i]);
		dist = DotProduct (l->worldtotex[i], l->facenormal);
		dist *= distscale;
		VectorMA (l->worldtotex[i], -dist, texnormal, l->textoworld[i]);
		VectorScale (l->textoworld[i], (1 / len) * (1 / len), l->textoworld[i]);
	}


	// calculate texorg on the texture plane
	for (i = 0; i < 3; i++)
		l->texorg[i] = -tex->vecs[0][3] * l->textoworld[0][i] - tex->vecs[1][3] * l->textoworld[1][i];

	// project back to the face plane
	dist = DotProduct (l->texorg, l->facenormal) - l->facedist - 1;
	dist *= distscale;
	VectorMA (l->texorg, -dist, texnormal, l->texorg);
}

/*
=================
GetTexName

Returns texture name for texindex
=================
*/
char *GetTexName (int texindex)
{
	dmiptexlump_t *mtl;
	miptex_t      *mt;
	int	      miptex;

	if (texdatasize != 0)
	{
		mtl = (dmiptexlump_t *) dtexdata;
		miptex = texinfo[texindex].miptex;

		if (mtl->dataofs[miptex] != -1)
		{
			mt = (miptex_t *) (dtexdata + mtl->dataofs[miptex]);
			return mt->name;
		}
	}

	return "notex";
}

/*
================
CalcFaceExtents

Fills in s->texmins[] and s->texsize[]
also sets exactmins[] and exactmaxs[]
================
*/
qboolean CalcFaceExtents (lightinfo_t *l, vec3_t faceoffset)
{
	dface_t	    *s;
	vec_t	    mins[2], maxs[2], val;
	int	    i, j;
	dvertex_t   *v;
	texinfo_t   *tex;

	s = l->face;

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = &texinfo[s->texinfo];

	for (i = 0; i < s->numedges; i++)
	{
		v = GetVertex (s->firstedge + i);

		for (j = 0; j < 2; j++)
		{
			val = (v->point[0] + faceoffset[0]) * tex->vecs[j][0] +
				  (v->point[1] + faceoffset[1]) * tex->vecs[j][1] +
				  (v->point[2] + faceoffset[2]) * tex->vecs[j][2] +
				  tex->vecs[j][3];

			if (val < mins[j])
				mins[j] = val;

			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i = 0; i < 2; i++)
	{
		l->exactmins[i] = mins[i];
		l->exactmaxs[i] = maxs[i];

		mins[i] = floor (mins[i] / 16);
		maxs[i] = ceil (maxs[i] / 16);

		l->texmins[i] = mins[i];
		l->texsize[i] = maxs[i] - mins[i];

		if (l->texsize[i] > 17)
		{
			if (!PreScan)
			{
				v = GetVertex (l->face->firstedge);
				logprintf ("Bad surface extents (%d, max = %d) on face near (%.0f %.0f %.0f), %s\n", l->texsize[i], 17, v->point[0], v->point[1], v->point[2], GetTexName (l->face->texinfo));
			}

			return false;
		}
	}

	return true;
}

/*
=================
CalcPoints

For each texture aligned grid point, back project onto the plane
to get the world xyz value of the sample point
=================
*/
void CalcPoints (lightinfo_t *l)
{
	int    i;
	int    s, t;
	int    w, h, step;
	vec_t  starts, startt, us, ut;
	vec_t  *surf;
	vec_t  mids, midt;
	vec3_t facemid, move;

	//
	// fill in surforg
	// the points are biased towards the center of the surface
	// to help avoid edge cases just inside walls
	//
	surf = l->surfpt[0];
	mids = (l->exactmaxs[0] + l->exactmins[0]) / 2;
	midt = (l->exactmaxs[1] + l->exactmins[1]) / 2;

	tex_to_world (mids, midt, l, facemid);

	// OverSamp > 1 => extra filtering
	h = (l->texsize[1] + 1) * OverSample;
	w = (l->texsize[0] + 1) * OverSample;
	starts = l->texmins[0] * 16;
	startt = l->texmins[1] * 16;

	if (OverSample > 1)
	{
		starts -= 16 / OverSample;
		startt -= 16 / OverSample;
	}

	step = 16 / OverSample;

	l->numsurfpt = w * h;

	if (PreScan)
		return; // Only surfpts are calculated, prevent ray tracing

	for (t = 0; t < h; t++)
	{
		for (s = 0; s < w; s++, surf += 3)
		{
			us = starts + s * step;
			ut = startt + t * step;

			// if a line can be traced from surf to facemid, the point is good
			for (i = 0; i < 6; i++)
			{
				// calculate texture point
				tex_to_world (us, ut, l, surf);

				if (CastRay (facemid, surf) != -1)
					break;	// got it

				if (i & 1)
				{
					if (us > mids)
					{
						us -= 8;

						if (us < mids)
							us = mids;
					}
					else
					{
						us += 8;

						if (us > mids)
							us = mids;
					}
				}
				else
				{
					if (ut > midt)
					{
						ut -= 8;

						if (ut < midt)
							ut = midt;
					}
					else
					{
						ut += 8;

						if (ut > midt)
							ut = midt;
					}
				}

				// move surf 8 pixels towards the center
				VectorSubtract (facemid, surf, move);
				VectorNormalize (move);
				VectorMA (surf, 8, move, surf);
			}
		}
	}

	l->width = w; // Save for later
}


/*
===============================================================================

FACE LIGHTING

===============================================================================
*/

/*
* ====================================
* Attenuation formulae setup functions
* ====================================
*/

/*
================
AdjustRange
================
*/
vec_t AdjustRange (vec_t Val) // Don't lose precision
{
	if (GlobRange)
		return Val;

	if (rangescale == 0)
		return 0;

	return (Val / 2) / rangescale;
}

/*
================
AdjustGlobal
================
*/
vec_t AdjustGlobal (vec_t Val, vec_t Angle) // Don't lose precision
{
	if (AddMinLight && Angle > 0)
	{
		Val -= worldminlight / Angle;

		if (Val < 0)
			Val = 0;
	}

	return AdjustRange (Val);
}

/*
================
scaledDistance
================
*/
static vec_t scaledDistance (vec_t distance, entity_t *light)
{
	switch (light->formula)
	{
	case FM_LINEAR : return scaledist * light->dist * distance;
	default	       : // Return a small distance to prevent culling these lights,
		// since we know these formulae won't fade to nothing
		return distance <= 0 ? distance - 0.01 : 0.25;
	}
}

/*
================
scaledLight
================
*/
static vec_t scaledLight (vec_t distance, entity_t *light)
{
	vec_t add;
	vec_t dist = scaledist * light->dist * distance;

	switch (light->formula)
	{

	case FM_LINEAR  : if (light->light > 0)
		{
			add = light->light - dist;
			return add > 0 ? add : 0;
		}
		else
		{
			add = light->light + dist;
			return add < 0 ? add : 0;
		}

	case FM_INVERSE : return light->light / (dist / DistFactor1);
	case FM_INVERSE3: dist += sqrt (DistFactor2); // Limit return value to < light->light
		// Fall through
	case FM_INVERSE2: return light->light / (dist * dist / DistFactor2);
	case FM_LOCMIN  : return AdjustRange (light->light);
	default	        : return light->light;
	}
}

/*
================
ModDiv
================
*/
_inline unsigned int ModDiv (unsigned int Val1, unsigned int Val2)
{
	// Speed hack
	if (Val2 == 2)
		return Val1 % 2;
	else if (Val2 == 4)
		return Val1 % 4;
	else if (Val2 == 8)
		return Val1 % 8;
	else if (Val2 == 16)
		return Val1 % 16;

	return Val1 % Val2;
}

/*
================
SkipPt
================
*/
_inline qboolean SkipPt (int SurfPt, int Width)
{
	// When fast light enabled, only check every FastLight surface points
	// Make sure we're handling the points row by row to avoid wraparound effects
	return FastLight && ModDiv (ModDiv (SurfPt, Width), FastLight) != 0;
}

/*
================
WarnStyle
================
*/
void WarnStyle (lightinfo_t *l)
{
	logwprintf ("WARNING: Too many light styles on a face\n");
	logwprintf ("   lightmap point near (%.0f %.0f %.0f), %s\n",
				l->surfpt[0][0], l->surfpt[0][1], l->surfpt[0][2], GetTexName (l->face->texinfo));
}


/*
================
SingleLightFace
================
*/
void SingleLightFace (entity_t *light, lightinfo_t *l)
{
	vec_t	 dist;
	vec3_t	 incoming;
	vec_t	 angle;
	vec_t	 add;
	vec_t	 *surf;
	qboolean hit;
	int	 mapnum;
	int	 size;
	int	 c, i;
	vec3_t	 rel;
	vec3_t	 spotvec;
	vec_t	 falloff, softfalloff, dotp, softscale;
	vec_t	 *lightsamp;
    vec3_t  *lightcoloursamp;
	vec_t	 samp1;
	float	 *samp3;
	vec_t	 Dummy1[SINGLEMAP];
	vec3_t	 Dummy3[SINGLEMAP];
	qboolean FadeGate = false;

	VectorSubtract (light->origin, bsp_origin, rel);
	dist = scaledDistance ((DotProduct (rel, l->facenormal) - l->facedist), light);

	// don't bother with lights behind the surface
	// Local minlight is never blocked here
	if (dist <= 0 && light->formula != FM_LOCMIN)
	{
		// Possibly allow lights to be slightly behind the surface
		if (dist < SingleDist)
			return;
	}

	// don't bother with light too far away
	if (dist > abs (light->light))
		return;

	falloff = 0;

	if (light->targetent || light->use_mangle)
	{
		// targetent overrides use_mangle
		if (light->targetent)
		{
			VectorSubtract (light->targetent->origin, light->origin, spotvec);
			VectorNormalize (spotvec);
		}
		else
			VectorCopy (light->mangle, spotvec);

		angle = 40; // Default 40 degrees spotlight cone

		if (light->angle)
			angle = light->angle;

		softfalloff = falloff = -cos (ToRad (angle / 2)); // Default no soft spotlight

		if (light->softangle)
			softfalloff = -cos (ToRad (light->softangle / 2)); // Inner cone of a soft spotlight
	}

	mapnum = 0;

	for (mapnum = 0; mapnum < l->numlightstyles; mapnum++)
		if (l->lightstyles[mapnum] == light->style)
			break;

	lightsamp = l->lightmaps[mapnum];
    lightcoloursamp = l->lightmapcolours[mapnum];

	if (mapnum == l->numlightstyles)
	{
		// init a new light map
		if (mapnum == MAXLIGHTMAPS)
		{
			// We might be exceeding the limit but hold off warning
			// until we see that the light actually hits this face
			lightsamp = Dummy1; // Make sure we don't trash l->lightmaps array
			lightcoloursamp = Dummy3;
		}

		// Clear lightmap (all surface points) for this face and style
		// This is done repeatedly to eliminate previous tiny light
		// additions (< 1.0) for this style
		size = l->numsurfpt;

		if (GenCompatible)
			size /= OverSample * OverSample; // Old bug

		for (i = 0; i < size; ++i)
		{
            lightcoloursamp[i][0] = lightcoloursamp[i][1] = lightcoloursamp[i][2] = 0;
			lightsamp[i] = 0;
		}
	}

	//
	// check it for real
	//
	if (GateVal > 0)
	{
		// Enable Fade Gate (limit attenuated lights)
		switch (light->formula)
		{
		case FM_LINEAR  :
		case FM_INVERSE :
		case FM_INVERSE3:
		case FM_INVERSE2: FadeGate = true;
			break;
		}
	}

	hit = false;
	surf = l->surfpt[0];

	for (c = 0; c < l->numsurfpt; c++, surf += 3)
	{
		// Skip this point ?
		if (SkipPt (c, l->width))
		{
			l->locmin[c] = l->locmin[c - 1]; // Copy local minlight setting from previous point
			continue;
		}

		if (FadeGate)
		{
			// Quick dist check to eliminate raytracing for far away attenuated lights
			if (fabs (scaledLight (CalcDist (light->origin, surf), light)) < GateVal)
				continue;
		}

		// Check spotlight cone before ray tracing
		VectorSubtract (light->origin, surf, incoming);
		VectorNormalize (incoming);
		angle = DotProduct (incoming, l->facenormal);

		softscale = 1;

		if (light->targetent || light->use_mangle)
		{
			// spotlight cutoff
			dotp = DotProduct (spotvec, incoming);

			if (dotp > falloff)
				continue; // Completely outside spot cone

			if (dotp > softfalloff)
				softscale = 1 - (dotp - softfalloff) / (falloff - softfalloff); // Attenuate in the soft spotlight zone
		}

		// Do the slow ray tracing
		dist = CastRay (light->origin, surf);

		if (scaledDistance (dist, light) < 0)
			continue;	// light doesn't reach

		add = scaledLight (dist, light);

		if (light->formula != FM_LOCMIN)
		{
			angle = (1.0 - light->anglesense) + light->anglesense * angle;
			add *= angle * softscale;

			if (NoAnti && add < 0)
				continue;

			if (light->addmax != 0 && add > light->addmax)
				add = light->addmax;

			lightsamp[c] += add;

            lightcoloursamp[c][0] += (add * light->lightcolour[0]) /255;
            lightcoloursamp[c][1] += (add * light->lightcolour[1]) /255;
            lightcoloursamp[c][2] += (add * light->lightcolour[2]) /255;
		}
		else
		{
			// Local minlight hit this surfpoint
			l->locmin[c] = true;

			// Angle sensitivity isn't used (= 0) for local minlights

			// For local minspotlights, adjust to global minlevel
			add += ((worldminlight < 0 ? 0 : AdjustRange (worldminlight)) - AdjustRange (light->light)) * (1 - softscale);

			// Negative local minlight ?
			if (light->light < 0)
			{
				lightsamp[c] = 2; // Just set a really low level

				lightcoloursamp[c][0] = (2 * light->lightcolour[0]) / 255;
				lightcoloursamp[c][1] = (2 * light->lightcolour[1]) / 255;
				lightcoloursamp[c][2] = (2 * light->lightcolour[2]) / 255;
			}
			else if (lightsamp[c] < add)
			{
				lightsamp[c] = add;

				lightcoloursamp[c][0] = (add * light->lightcolour[0]) / 255;
				lightcoloursamp[c][1] = (add * light->lightcolour[1]) / 255;
				lightcoloursamp[c][2] = (add * light->lightcolour[2]) / 255;
			}
		}

		samp1 = lightsamp[c];
		samp3 = lightcoloursamp[c];

		if (TyrCompatible)
		{
			samp1 = abs (samp1); // TyrLite bug
			samp3[0] = abs (samp3[0]);
			samp3[1] = abs (samp3[1]);
			samp3[2] = abs (samp3[2]);
		}

		if (samp1 > 1)		// ignore real tiny lights
			hit = true;
	}

	if (mapnum == l->numlightstyles && hit)
	{
		if (mapnum == MAXLIGHTMAPS)
		{
			// Now we know that the limit actually was exceeded
			WarnStyle (l);
			logwprintf ("   light->origin (%.0f %.0f %.0f)\n",
						light->origin[0], light->origin[1], light->origin[2]);

			if (light->style == 0 && SoftLight > 0)
			{
				// Replace last style with style 0 (most likely dominant)
				--mapnum;
				memcpy (l->lightmaps[mapnum], lightsamp, l->numsurfpt * sizeof (vec_t));
				memcpy (l->lightmapcolours[mapnum], lightcoloursamp, l->numsurfpt * sizeof (vec3_t));
				l->lightstyles[mapnum] = 0;
			}

			return;
		}

		l->lightstyles[mapnum] = light->style;
		l->numlightstyles++;	// the style has some real data now
	}
}

/*
=============
SkyLightFace
=============
*/
void SkyLightFace (entity_t *light, lightinfo_t *l, float SunLight, float *sunLightColor, vec3_t SunMangle, qboolean SkyMinLight)
{
	int    i, j;
	vec_t  *surf;
	vec3_t incoming;
	vec_t  angle, dist, sunlight, anglesense;

	if (SunLight <= 0)
		return;

	dist = DotProduct (SunMangle, l->facenormal);

	// Don't bother if surface facing away from sun
	if (dist <= 0)
	{
		// Possibly allow main sunlight to be slightly behind the surface
		if (dist < SkyDist || SkyMinLight)
			return;
	}

	// if sunlight is set, use a style 0 light map
	for (i = 0; i < l->numlightstyles; i++)
	{
		if (l->lightstyles[i] == 0)
			break;
	}

	if (i == l->numlightstyles)
	{
		if (l->numlightstyles == MAXLIGHTMAPS)
			return; // oh well, too many lightmaps...

		if (!GenCompatible)
		{
			// Clear lightmap (all surface points) for this face and style
			for (j = 0; j < l->numsurfpt; ++j)
			{
				l->lightmaps[i][j] = 0;
				l->lightmapcolours[i][j][0] = 0;
				l->lightmapcolours[i][j][1] = 0;
				l->lightmapcolours[i][j][2] = 0;
			}
		}

		l->lightstyles[i] = 0;
		l->numlightstyles++;
	}

	// Check each point...

	VectorCopy (SunMangle, incoming);
	VectorNormalize (incoming);
	angle = DotProduct (incoming, l->facenormal);
	anglesense = SkyMinLight ? ShadowSense : light->anglesense;

	angle = (1.0 - anglesense) + anglesense * angle;

	// Compensate for global settings and add angle effect
	sunlight = AdjustGlobal (SunLight, angle) * angle;

	surf = l->surfpt[0];

	for (j = 0; j < l->numsurfpt; j++, surf += 3)
	{
		if (SkyMinLight && l->lightmaps[i][j] >= sunlight)
			continue; // Already bright enough

		// Skip this point ?
		if (SkipPt (j, l->width))
			continue;

		if (TestSky (surf, SunMangle))
		{
			if (!SkyMinLight)
			{
				l->lightmaps[i][j] += sunlight;
				l->lightmapcolours[i][j][0] += sunlight * sunLightColor[0] / 255;
				l->lightmapcolours[i][j][1] += sunlight * sunLightColor[1] / 255;
				l->lightmapcolours[i][j][2] += sunlight * sunLightColor[2] / 255;
			}
			else
			{
				if (l->lightmaps[i][j] < sunlight)
				{
					l->lightmaps[i][j] = sunlight;
					l->lightmapcolours[i][j][0] = sunlight * sunLightColor[0] / 255;
					l->lightmapcolours[i][j][1] = sunlight * sunLightColor[1] / 255;
					l->lightmapcolours[i][j][2] = sunlight * sunLightColor[2] / 255;
				}
			}
		}
	}
}

/*
============
FixMinlight
============
*/
void FixMinlight (lightinfo_t *l)
{
	int   i, j;
	vec_t minlight;

	if (worldminlight <= 0)
		return;

	// Normally default range for min/maxlight
	minlight = AdjustRange (worldminlight);

	// if minlight is set, there must be a style 0 light map
	for (i = 0; i < l->numlightstyles; i++)
	{
		if (l->lightstyles[i] == 0)
			break;
	}

	if (i == l->numlightstyles)
	{
		if (l->numlightstyles == MAXLIGHTMAPS)
		{
			WarnStyle (l);

			if (SoftLight == 0)
				return; // oh well, too many lightmaps...

			--i; // Replace last style with minlight
		}
		else
			l->numlightstyles++;

		for (j = 0; j < l->numsurfpt; j++)
		{
			l->lightmaps[i][j] = minlight;
			l->lightmapcolours[i][j][0] = minlight;
			l->lightmapcolours[i][j][1] = minlight;
			l->lightmapcolours[i][j][2] = minlight;
		}

		l->lightstyles[i] = 0;
	}
	else
	{
		for (j = 0; j < l->numsurfpt; j++)
		{
			if (l->locmin[j])
				continue; // Local minlight already hit this surfpoint

			if (AddMinLight)
			{
				l->lightmaps[i][j] += minlight; // Additive minlight
				l->lightmapcolours[i][j][0] += minlight;
				l->lightmapcolours[i][j][1] += minlight;
				l->lightmapcolours[i][j][2] += minlight;
			}
			else
			{
				if (l->lightmaps[i][j] < minlight)
				{
					l->lightmaps[i][j] = minlight;
					l->lightmapcolours[i][j][0] = minlight;
					l->lightmapcolours[i][j][1] = minlight;
					l->lightmapcolours[i][j][2] = minlight;
				}
			}
		}
	}
}

/*
============
FixFast
============
*/
void FixFast (vec_t *LightMap, vec3_t *LightMap3, int NumSurfPt, int Width)
{
	vec_t Incr1;
	vec3_t Incr3;
	int   i, CCol;

	for (i = 0; i < NumSurfPt; ++i)
	{
		CCol = ModDiv (i, Width);

		if (ModDiv (CCol, FastLight) == 0)
		{
			// Every FastLight point on each row is real and OK

			// Is there a next real point on the same row ?
			if (CCol + FastLight < Width)
			{
				Incr1 = (LightMap[i + FastLight] - LightMap[i]) / FastLight;
				Incr3[0] = (LightMap3[i + FastLight][0] - LightMap3[i][0]) / FastLight;
				Incr3[1] = (LightMap3[i + FastLight][1] - LightMap3[i][1]) / FastLight;
				Incr3[2] = (LightMap3[i + FastLight][2] - LightMap3[i][2]) / FastLight;
			}
			else
			{
				Incr1 = 0; // No interpolation possible
				Incr3[0] = Incr3[1] = Incr3[2] = 0;
			}

			continue;
		}

		// Interpolate incrementally between previous and next real points
		LightMap[i] = LightMap[i - 1] + Incr1;
		LightMap3[i][0] = LightMap3[i - 1][0] + Incr3[0];
		LightMap3[i][1] = LightMap3[i - 1][1] + Incr3[1];
		LightMap3[i][2] = LightMap3[i - 1][2] + Incr3[2];
	}
}

/*
============
Soften
============
*/
void Soften (vec_t *LightMap, vec3_t *LightMap3, int NumSurfPt, int Width)
{
	vec_t TmpMap1[SINGLEMAP], Add1;
	vec3_t TmpMap3[SINGLEMAP], Add3;
	int   i, AddNo, CRow, CCol, Row, Col, FullGrid, Missing, Rows, SRow, ERow, SCol, ECol;

	// Soften light by averaging adjacent points in a grid
	FullGrid = 2 * SoftLight + 1;
	FullGrid *= FullGrid;

	Rows = NumSurfPt / Width;

	for (i = 0; i < NumSurfPt; ++i)
	{
		CRow = i / Width;
		CCol = ModDiv (i, Width);

		SRow = CRow - SoftLight;

		if (SRow < 0)
			SRow = 0;

		ERow = CRow + SoftLight;

		if (ERow >= Rows)
			ERow = Rows - 1;

		SCol = CCol - SoftLight;

		if (SCol < 0)
			SCol = 0;

		ECol = CCol + SoftLight;

		if (ECol >= Width)
			ECol = Width - 1;

		Add1 = AddNo = Add3[0] = Add3[1] = Add3[2] = 0;

		for (Row = SRow; Row <= ERow; ++Row)
		{
			for (Col = SCol; Col <= ECol; ++Col)
			{
				Add1 += LightMap[Row * Width + Col];
				Add3[0] += LightMap3[Row * Width + Col][0];
				Add3[1] += LightMap3[Row * Width + Col][1];
				Add3[2] += LightMap3[Row * Width + Col][2];
				++AddNo;
			}
		}

		Missing = FullGrid - AddNo;

		if (Missing > 0)
		{
			// Not full grid; compensate by multiple weighted center values
			Add1 += LightMap[i] * Missing * 2;
			Add3[0] += LightMap3[i][0] * Missing * 2;
			Add3[1] += LightMap3[i][1] * Missing * 2;
			Add3[2] += LightMap3[i][2] * Missing * 2;
			AddNo += Missing * 2;
		}

		TmpMap1[i] = Add1 / AddNo;
		TmpMap3[i][0] = Add3[0] / AddNo;
		TmpMap3[i][1] = Add3[1] / AddNo;
		TmpMap3[i][2] = Add3[2] / AddNo;
	}

	memcpy (LightMap, TmpMap1, sizeof (vec_t) * NumSurfPt);
	memcpy (LightMap3, TmpMap3, sizeof (vec3_t) * NumSurfPt);
}

/*
============
LightFace
============
*/
void LightFace (int surfnum, vec3_t faceoffset)
{
	dface_t     *f;
	lightinfo_t l;
	int	    s, t;
	int	    i, j, k, c;
	vec_t	    total1;
	vec3_t	    total3;
	int	    lightmapwidth, lightmapsize;
	byte	    *out1;
	byte		*out3;
	vec_t	    *light1;
	vec3_t	    *light3;
	vec_t		MaxLight;
	int	    w;
	vec3_t	    point;

	f = dfaces + surfnum;

	// ensure this
	l.numlightstyles = 0;

	// Don't alter any bsp data in prescan
	if (!PreScan)
	{
		f->lightofs = -1;

		for (j = 0; j < MAXLIGHTMAPS; j++)
			f->styles[j] = 255;
	}

	// some surfaces don't need lightmaps
	if (texinfo[f->texinfo].flags & TEX_SPECIAL)
	{
		// non-lit texture
		return;
	}

	l.surfnum = surfnum;
	l.face = f;

	// rotate plane
	VectorCopy (dplanes[f->planenum].normal, l.facenormal);
	l.facedist = dplanes[f->planenum].dist;

	if (ATCompatible || faceoffset[0] != 0 || faceoffset[1] != 0 || faceoffset[2] != 0)
	{
		VectorScale (l.facenormal, l.facedist, point);
		VectorAdd (point, faceoffset, point);
		l.facedist = DotProduct (point, l.facenormal);
	}

	if (f->side)
	{
		VectorInverse (l.facenormal);
		l.facedist = -l.facedist;
	}

	CalcFaceVectors (&l);

	if (!CalcFaceExtents (&l, faceoffset))
		return;

	CalcPoints (&l);

	if (PreScan)
	{
		NumSurfPts += l.numsurfpt;
		return; // Only surfpts are calculated, prevent ray tracing
	}

	if (GenCompatible)
	{
		// Clear lightmap (all surface points) for this face
		for (i = 0; i < MAXLIGHTMAPS; ++i)
		{
			for (j = 0; j < l.numsurfpt; ++j)
			{
				l.lightmaps[i][j] = 0;
				l.lightmapcolours[i][j][0] = 0;
				l.lightmapcolours[i][j][1] = 0;
				l.lightmapcolours[i][j][2] = 0;
			}
		}
	}

	// Clear local minlight logic for this face
	for (j = 0; j < l.numsurfpt; ++j)
		l.locmin[j] = false;

	lightmapwidth = l.texsize[0] + 1;

	l.size = lightmapwidth * (l.texsize[1] + 1);

	if (l.size > SINGLEMAP)
		Error ("Bad lightmap size %d", l.size);

	for (i = 0; i < MAXLIGHTMAPS; i++)
		l.lightstyles[i] = 255;

	// cast all positive lights except local minlights
	l.numlightstyles = 0;

	for (i = 0; i < num_entities; i++)
	{
		if (entities[i].light > 0 && entities[i].formula != FM_LOCMIN)
			SingleLightFace (&entities[i], &l);
	}

	// cast sky light
	if (SunLight[0] > 0)
	{
		if (FakeGISunlight2) //using additive sunlight2
		{
			for (i = 0; i < NoOfSuns; ++i)
				SkyLightFace (&entities[0], &l, SunLight[i != 0], SunLightColor[i != 0], SunMangle[i], false); //ALL suns emit additive light
		}
		else
		{
			for (i = 0; i < NoOfSuns; ++i)
				SkyLightFace (&entities[0], &l, SunLight[i != 0], SunLightColor[i != 0], SunMangle[i], i > 0); //only first sun emits additive light
		}
	}

	// cast local minlights
	for (i = 0; i < num_entities; i++)
	{
		if (entities[i].formula == FM_LOCMIN)
			SingleLightFace (&entities[i], &l);
	}

	if (FastLight && !AntiLights)
	{
		// Extrapolate skipped surface points
		for (i = 0; i < l.numlightstyles; ++i)
			FixFast (l.lightmaps[i], l.lightmapcolours[i], l.numsurfpt, l.width);
	}

	FixMinlight (&l);

	if (AntiLights)
	{
		// Cast all negative lights
		for (i = 0; i < num_entities; ++i)
		{
			if (entities[i].light < 0)
				SingleLightFace (&entities[i], &l);
		}

		if (FastLight)
		{
			// Extrapolate skipped surface points
			for (i = 0; i < l.numlightstyles; ++i)
				FixFast (l.lightmaps[i], l.lightmapcolours[i], l.numsurfpt, l.width);
		}

		if (TyrCompatible)
		{
			// Fix any negative values
			for (i = 0; i < l.numlightstyles; ++i)
			{
				for (j = 0; j < l.numsurfpt; ++j)
				{
					if (l.lightmaps[i][j] < 0) l.lightmaps[i][j] = 0;
					if (l.lightmapcolours[i][j][0] < 0) l.lightmapcolours[i][j][0] = 0;
					if (l.lightmapcolours[i][j][1] < 0) l.lightmapcolours[i][j][1] = 0;
					if (l.lightmapcolours[i][j][2] < 0) l.lightmapcolours[i][j][2] = 0;
				}
			}
		}
	}

	if (!l.numlightstyles)
		return; // no light hitting it

	if (SoftLight > 0)
	{
		for (i = 0; i < l.numlightstyles; ++i)
			Soften (l.lightmaps[i], l.lightmapcolours[i], l.numsurfpt, l.width);
	}

	// save out the values
	for (i = 0; i < MAXLIGHTMAPS; i++)
		f->styles[i] = l.lightstyles[i];

	lightmapsize = l.size * l.numlightstyles;

	out1 = GetFileSpace (lightmapsize);

	// don't need a lightofs3 as the offsets are the same and the engine will multiply them by 3
	f->lightofs = out1 - filebase1;

	// now we get the pointer for 3 component at the correct offset
	// this one doesn't need to lock the thread as it's going to be different all the time
	out3 = &dlightdata3[f->lightofs * 3];

	// extra filtering
	w = l.width;

	for (i = 0; i < l.numlightstyles; i++)
	{
		if (l.lightstyles[i] == 255)
			Error ("Wrote empty lightmap");

		light1 = l.lightmaps[i];
		light3 = l.lightmapcolours[i];
		c = 0;

		for (t = 0; t <= l.texsize[1]; t++)
		{
			for (s = 0; s <= l.texsize[0]; s++, c++)
			{
				if (OverSample > 1)
				{
					// filtered sample
					for (j = total1 = total3[0] = total3[1] = total3[2] = 0; j < OverSample; ++j)
					{
						for (k = 0; k < OverSample; ++k)
						{
							total1 += light1[(t * OverSample + j) * w + s * OverSample + k];
							total3[0] += light3[(t * OverSample + j) * w + s * OverSample + k][0];
							total3[1] += light3[(t * OverSample + j) * w + s * OverSample + k][1];
							total3[2] += light3[(t * OverSample + j) * w + s * OverSample + k][2];
						}
					}

					total1 /= OverSample * OverSample;
					total3[0] /= OverSample * OverSample;
					total3[1] /= OverSample * OverSample;
					total3[2] /= OverSample * OverSample;
				}
				else
				{
					total1 = light1[c];
					total3[0] = light3[c][0];
					total3[1] = light3[c][1];
					total3[2] = light3[c][2];
				}

				total1 *= rangescale;	// scale before clamping
				total3[0] *= rangescale;	// scale before clamping
				total3[1] *= rangescale;	// scale before clamping
				total3[2] *= rangescale;	// scale before clamping

				if (total1 > 255) total1 = 255; else if (total1 < 0) total1 = 0;

				if (total3[0] > 255) total3[0] = 255; else if (total3[0] < 0) total3[0] = 0;
				if (total3[1] > 255) total3[1] = 255; else if (total3[1] < 0) total3[1] = 0;
				if (total3[2] > 255) total3[2] = 255; else if (total3[2] < 0) total3[2] = 0;

				if (worldmaxlight >= 0)
				{
					// Normally default range for min/maxlight
					MaxLight = worldmaxlight * (GlobRange ? rangescale : 0.5);

					if (total1 > MaxLight) total1 = MaxLight;
					if (total3[0] > MaxLight) total3[0] = MaxLight;
					if (total3[1] > MaxLight) total3[1] = MaxLight;
					if (total3[2] > MaxLight) total3[2] = MaxLight;
				}

				//				*out++ = total + 0.5; // Proper roundoff
				*out1++ = total1 + 0.00001; // Compatibility roundoff

				out3[0] = total3[0] + 0.00001;
				out3[1] = total3[1] + 0.00001;
				out3[2] = total3[2] + 0.00001;
				out3 += 3;
			}
		}
	}
}

