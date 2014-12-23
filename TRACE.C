// trace.c

#include "light.h"

typedef struct tnode_s
{
	int	type;
	vec3_t	normal;
	float	dist;
	int	children[2];
} tnode_t;

tnode_t		*tnodes, *tnode_p;

/*
==============
MakeTnode

Converts the disk node structure into the efficient tracing structure
==============
*/
void MakeTnode (int nodenum)
{
	tnode_t	 *t;
	dplane_t *plane;
	int	 i;
	dnode_t  *node;

	t = tnode_p++;

	if (t - tnodes >= numnodes)
		Error ("MakeTnode: too many tnodes (max = %d)", numnodes);

	node = dnodes + nodenum;

	plane = dplanes + node->planenum;

	t->type = plane->type;
	VectorCopy (plane->normal, t->normal);
	t->dist = plane->dist;

	for (i = 0; i < 2; i++)
	{
		if (node->children[i] < 0)
		{
			t->children[i] = dleafs[-node->children[i] - 1].contents;

			if (SolidSky && t->children[i] == CONTENTS_SOLID)
			{
				if (!strncmp (GetTexName (dfaces[node->firstface].texinfo), "sky", 3))
					t->children[i] = CONTENTS_SKY; // Simulate real sky
			}
		}
		else
		{
			t->children[i] = tnode_p - tnodes;
			MakeTnode (node->children[i]);
		}
	}
}

/*
=============
MakeTnodes

Loads the node structure out of a .bsp file to be used for light occlusion
=============
*/
void MakeTnodes (dmodel_t *bm)
{
	if (numnodes > MAX_MAP_NODES)
		Error ("MakeTnodes: too many nodes (%d, max = %d)", numnodes, MAX_MAP_NODES);

	tnode_p = tnodes = malloc (numnodes * sizeof (tnode_t));

	MakeTnode (0);
}

/*
==============================================================================

LINE TRACING

The major lighting operation is a point to point visibility test, performed
by recursive subdivision of the line by the BSP tree.

==============================================================================
*/

typedef struct
{
	vec3_t	backpt;
	int	side;
	int	node;
} tracestack_t;

#define MAX_TSTACK 128 //64

/*
==============
TestLineOrSky
==============
*/
qboolean TestLineOrSky (vec3_t start, vec3_t stop, qboolean sky_test)
{
	int	     node;
	float	     front, back;
	tracestack_t *tstack_p;
	int	     side;
	float 	     frontx, fronty, frontz, backx, backy, backz;
	tracestack_t tracestack[MAX_TSTACK];
	tnode_t	     *tnode;

	//    	tracestack_t *tstack_max = tracestack + MAX_TSTACK;

	frontx = start[0];
	fronty = start[1];
	frontz = start[2];
	backx = stop[0];
	backy = stop[1];
	backz = stop[2];

	tstack_p = tracestack;
	node = 0;

	while (1)
	{
		while (node < 0 && node != CONTENTS_SOLID && (!sky_test || node != CONTENTS_SKY))
		{
			// pop up the stack for a back side
			tstack_p--;

			if (tstack_p < tracestack)
				return !sky_test; // no obstructions

			// set the hit point for this plane

			frontx = backx;
			fronty = backy;
			frontz = backz;

			// go down the back side

			backx = tstack_p->backpt[0];
			backy = tstack_p->backpt[1];
			backz = tstack_p->backpt[2];

			node = tnodes[tstack_p->node].children[!tstack_p->side];
		}

		if (node < 0) // Speed
		{
			if (node == CONTENTS_SOLID)
				return false; // DONE!
			else if (node == CONTENTS_SKY && sky_test)
				return true;  // DONE!
		}

		tnode = &tnodes[node];

		switch (tnode->type)
		{
		case PLANE_X:
			front = frontx - tnode->dist;
			back = backx - tnode->dist;
			break;
		case PLANE_Y:
			front = fronty - tnode->dist;
			back = backy - tnode->dist;
			break;
		case PLANE_Z:
			front = frontz - tnode->dist;
			back = backz - tnode->dist;
			break;
		default:
			front = (frontx * tnode->normal[0] + fronty * tnode->normal[1] + frontz * tnode->normal[2]) - tnode->dist;
			back = (backx * tnode->normal[0] + backy * tnode->normal[1] + backz * tnode->normal[2]) - tnode->dist;
			break;
		}

		if (back > -ON_EPSILON && front > -ON_EPSILON) // Changed test order for extra speed
		{
			node = tnode->children[0];
			continue;
		}

		if (front < ON_EPSILON && back < ON_EPSILON)
		{
			node = tnode->children[1];
			continue;
		}

		side = front < 0;

		front = front / (front - back);

		tstack_p->node = node;
		tstack_p->side = side;
		tstack_p->backpt[0] = backx;
		tstack_p->backpt[1] = backy;
		tstack_p->backpt[2] = backz;

		tstack_p++;

		// This test is too speed expensive
		//		if (tstack_p >= tstack_max)
		//		    	Error ("TestLineOrSky: tstack overflow, max = %d", MAX_TSTACK);

		backx = frontx + front * (backx - frontx);
		backy = fronty + front * (backy - fronty);
		backz = frontz + front * (backz - frontz);

		node = tnode->children[side];
	}
}

/*
=================
TestLine/TestSky
=================
Returns true if the ray cast from point 'start' in the
direction of vector 'dirn' hits a CONTENTS_SKY node before
a CONTENTS_SOLID node.

Wrapper functions for testing LOS between two points (TestLine)
and testing LOS to a sky brush along a direction vector (TestSky)
*/

qboolean TestLine (vec3_t start, vec3_t stop)
{
	return TestLineOrSky (start, stop, false);
}

qboolean TestSky (vec3_t start, vec3_t dirn)
{
	vec3_t stop;

	VectorAdd (dirn, start, stop);

	//return TestLineOrSky (start, stop, true);
	return TestLineOrSky (start, stop, !FakeGIMode);
}
