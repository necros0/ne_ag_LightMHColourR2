
#include "cmdlib.h"
#include "mathlib.h"
#include "bspfile.h"

//=============================================================================

int			nummodels;
dmodel_t	*dmodels;

int			visdatasize;
byte		*dvisdata;

int			lightdatasize1;
int			lightdatasize3;
byte		*dlightdata1;
byte		*dlightdata3;

int			texdatasize;
byte		*dtexdata; // (dmiptexlump_t)

int			entdatasize;
char		*dentdata;

int			numleafs;
dleaf_t		*dleafs;

int			numplanes;
dplane_t	*dplanes;

int			numvertexes;
dvertex_t	*dvertexes;

int			numnodes;
dnode_t		*dnodes;

int			numtexinfo;
texinfo_t	*texinfo;

int			numfaces;
dface_t		*dfaces;

int			numclipnodes;
dclipnode_t	*dclipnodes;

int			numedges;
dedge_t		*dedges;

int			nummarksurfaces;
unsigned short	*dmarksurfaces;

int			numsurfedges;
int			*dsurfedges;

//=============================================================================

static unsigned RecursLevel;

/*
=============
ChkNode

Validates bsp node tree
=============
*/
static void ChkNode (int nodenum)
{
	dnode_t *dn;
	int	i;

	if (++RecursLevel > 4096) // 512 seems enough for huge maps and 8192 might create stack overflow in the engine
		Error ("ChkNode: excessive tree depth (max = %d)", 4096);

	if (nodenum < 0)
	{
		int leaf = -nodenum - 1;

		if (leaf >= numleafs)
			Error ("ChkNode: leaf out of bounds (%d, max = %d)", leaf, numleafs);

		--RecursLevel;
		return;
	}

	if (nodenum >= numnodes)
		Error ("ChkNode: node out of bounds (%d, max = %d)", nodenum, numnodes);

	dn = dnodes + nodenum;

	for (i = 0; i < 2; ++i)
		ChkNode (dn->children[i]);

	--RecursLevel;
}

/*
=============
ChkBSPFile

Validates data in bsp
=============
*/
void ChkBSPFile (void)
{
	int i, j;

	//
	// faces
	//
	for (i = 0; i < numfaces; i++)
	{
		if (dfaces[i].texinfo < 0 || dfaces[i].texinfo >= numtexinfo)
			Error ("ChkBSPFile: texinfo out of bounds (%d, max = %d)", dfaces[i].texinfo, numtexinfo);

		if (dfaces[i].planenum < 0 || dfaces[i].planenum >= numplanes)
			Error ("ChkBSPFile: face->planenum out of bounds (%d, max = %d)", dfaces[i].planenum, numplanes);

		if (dfaces[i].firstedge < 0 || dfaces[i].numedges < 0 || dfaces[i].firstedge + dfaces[i].numedges > numsurfedges)
			Error ("ChkBSPFile: firstedge/numedges out of bounds (%d/%d, max = %d)", dfaces[i].firstedge, dfaces[i].numedges, numsurfedges);
	}

	//
	// nodes
	//
	for (i = 0; i < numnodes; i++)
	{
		if (dnodes[i].planenum < 0 || dnodes[i].planenum >= numplanes)
			Error ("ChkBSPFile: node->planenum out of bounds (%d, max = %d)", dnodes[i].planenum, numplanes);

		if (dnodes[i].firstface + dnodes[i].numfaces > numfaces)
			Error ("ChkBSPFile: firstface/numfaces out of bounds (%d/%d, max = %d)", dnodes[i].firstface, dnodes[i].numfaces, numfaces);
	}

	RecursLevel = 0;
	ChkNode (0);

	//
	// miptex
	//
	if (texdatasize)
	{
		dmiptexlump_t *mtl = (dmiptexlump_t *) dtexdata;
		int	      c = mtl->nummiptex;

		if ((byte *) &mtl->dataofs[c] - (byte *) mtl > (unsigned) texdatasize)
			Error ("ChkBSPFile: texture index %d is outside texdata size %d", c, texdatasize);

		for (i = 0; i < c; i++)
		{
			int offset = mtl->dataofs[i];

			if (offset != -1 && offset >= (unsigned) texdatasize)
				Error ("ChkBSPFile: texture offset %u is outside texdata size %d", offset, texdatasize);
		}
	}

	//
	// surfedges
	//
	for (i = 0; i < numsurfedges; i++)
	{
		int aedge = abs (dsurfedges[i]);

		if (aedge >= numedges)
			Error ("ChkBSPFile: edge out of bounds (%d, max = %d)", aedge, numedges);
	}

	//
	// edges
	//
	for (i = 0; i < numedges; i++)
	{
		for (j = 0; j < 2; j++)
		{
			if (i == 0 && j == 0)
				continue; // Why is sometimes this edge invalid?

			if (dedges[i].v[j] >= numvertexes)
				Error ("ChkBSPFile: vertex out of bounds (%d, max = %d)", dedges[i].v[j], numvertexes);
		}
	}
}

/*
=============
SwapBSPFile

Byte swaps all data in a bsp file.
=============
*/
void SwapBSPFile (qboolean todisk)
{
	int				i, j, c;
	dmodel_t		*d;
	dmiptexlump_t	*mtl;


	// models
	for (i = 0; i < nummodels; i++)
	{
		d = &dmodels[i];

		for (j = 0; j < MAX_MAP_HULLS; j++)
			d->headnode[j] = LittleLong (d->headnode[j]);

		d->visleafs = LittleLong (d->visleafs);
		d->firstface = LittleLong (d->firstface);
		d->numfaces = LittleLong (d->numfaces);

		for (j = 0; j < 3; j++)
		{
			d->mins[j] = LittleFloat (d->mins[j]);
			d->maxs[j] = LittleFloat (d->maxs[j]);
			d->origin[j] = LittleFloat (d->origin[j]);
		}
	}

	//
	// vertexes
	//
	for (i = 0; i < numvertexes; i++)
	{
		for (j = 0; j < 3; j++)
			dvertexes[i].point[j] = LittleFloat (dvertexes[i].point[j]);
	}

	//
	// planes
	//
	for (i = 0; i < numplanes; i++)
	{
		for (j = 0; j < 3; j++)
			dplanes[i].normal[j] = LittleFloat (dplanes[i].normal[j]);

		dplanes[i].dist = LittleFloat (dplanes[i].dist);
		dplanes[i].type = LittleLong (dplanes[i].type);
	}

	//
	// texinfos
	//
	for (i = 0; i < numtexinfo; i++)
	{
		for (j = 0; j < 8; j++)
			texinfo[i].vecs[0][j] = LittleFloat (texinfo[i].vecs[0][j]);

		texinfo[i].miptex = LittleLong (texinfo[i].miptex);
		texinfo[i].flags = LittleLong (texinfo[i].flags);
	}

	//
	// faces
	//
	for (i = 0; i < numfaces; i++)
	{
		dfaces[i].texinfo = LittleShort (dfaces[i].texinfo);
		dfaces[i].planenum = LittleShort (dfaces[i].planenum);
		dfaces[i].side = LittleShort (dfaces[i].side);
		dfaces[i].lightofs = LittleLong (dfaces[i].lightofs);
		dfaces[i].firstedge = LittleLong (dfaces[i].firstedge);
		dfaces[i].numedges = LittleShort (dfaces[i].numedges);
	}

	//
	// nodes
	//
	for (i = 0; i < numnodes; i++)
	{
		dnodes[i].planenum = LittleLong (dnodes[i].planenum);

		for (j = 0; j < 3; j++)
		{
			dnodes[i].mins[j] = LittleShort (dnodes[i].mins[j]);
			dnodes[i].maxs[j] = LittleShort (dnodes[i].maxs[j]);
		}

		dnodes[i].children[0] = LittleShort (dnodes[i].children[0]);
		dnodes[i].children[1] = LittleShort (dnodes[i].children[1]);
		dnodes[i].firstface = LittleShort (dnodes[i].firstface);
		dnodes[i].numfaces = LittleShort (dnodes[i].numfaces);
	}

	//
	// leafs
	//
	for (i = 0; i < numleafs; i++)
	{
		dleafs[i].contents = LittleLong (dleafs[i].contents);

		for (j = 0; j < 3; j++)
		{
			dleafs[i].mins[j] = LittleShort (dleafs[i].mins[j]);
			dleafs[i].maxs[j] = LittleShort (dleafs[i].maxs[j]);
		}

		dleafs[i].firstmarksurface = LittleShort (dleafs[i].firstmarksurface);
		dleafs[i].nummarksurfaces = LittleShort (dleafs[i].nummarksurfaces);
		dleafs[i].visofs = LittleLong (dleafs[i].visofs);
	}

	//
	// clipnodes
	//
	for (i = 0; i < numclipnodes; i++)
	{
		dclipnodes[i].planenum = LittleLong (dclipnodes[i].planenum);
		dclipnodes[i].children[0] = LittleShort (dclipnodes[i].children[0]);
		dclipnodes[i].children[1] = LittleShort (dclipnodes[i].children[1]);
	}

	//
	// miptex
	//
	if (texdatasize)
	{
		mtl = (dmiptexlump_t *) dtexdata;

		if (todisk)
			c = mtl->nummiptex;
		else
			c = LittleLong (mtl->nummiptex);

		mtl->nummiptex = LittleLong (mtl->nummiptex);

		for (i = 0; i < c; i++)
			mtl->dataofs[i] = LittleLong (mtl->dataofs[i]);
	}

	//
	// marksurfaces
	//
	for (i = 0; i < nummarksurfaces; i++)
		dmarksurfaces[i] = LittleShort (dmarksurfaces[i]);

	//
	// surfedges
	//
	for (i = 0; i < numsurfedges; i++)
		dsurfedges[i] = LittleLong (dsurfedges[i]);

	//
	// edges
	//
	for (i = 0; i < numedges; i++)
	{
		dedges[i].v[0] = LittleShort (dedges[i].v[0]);
		dedges[i].v[1] = LittleShort (dedges[i].v[1]);
	}
}

dheader_t *header;
int	  FSize;

int CopyLump (int lump, void **dest, int size, char object[])
{
	int length, ofs;

	length = header->lumps[lump].filelen;
	ofs = header->lumps[lump].fileofs;

	if (length % size)
		Error ("LoadBSPFile: odd %s lump size", object);

	if (length > 0 && ofs + length > FSize)
		Error ("LoadBSPFile: %s offset+length %d is outside file size %d", object, ofs + length, FSize);

	if (*dest != NULL)
		free (*dest);

	*dest = malloc (length + 1); // One extra byte just in case ...

	memcpy (*dest, (byte *) header + ofs, length);
	* ((byte *) *dest + length) = 0; // Entity lump seems to require this

	return length / size;
}

/*
=============
LoadBSPFile
=============
*/
void	LoadBSPFile (char *filename)
{
	int i;

	//
	// load the file header
	//
	if (header)
	{
		// After error, we might come back here; avoid leaks
		free (header);
		header = NULL;
	}

	FSize = LoadFile (filename, (void **) &header);

	// swap the header
	for (i = 0; i < sizeof (dheader_t) / 4; i++)
		((int *) header) [i] = LittleLong (((int *) header) [i]);

	if (header->version != BSPVERSION)
		Error ("%s is version %i, not %i", filename, header->version, BSPVERSION);

	nummodels = CopyLump (LUMP_MODELS, &dmodels, sizeof (dmodel_t), "Model");
	numvertexes = CopyLump (LUMP_VERTEXES, &dvertexes, sizeof (dvertex_t), "Vertex");
	numplanes = CopyLump (LUMP_PLANES, &dplanes, sizeof (dplane_t), "Plane");
	numleafs = CopyLump (LUMP_LEAFS, &dleafs, sizeof (dleaf_t), "Leaf");
	numnodes = CopyLump (LUMP_NODES, &dnodes, sizeof (dnode_t), "Node");
	numtexinfo = CopyLump (LUMP_TEXINFO, &texinfo, sizeof (texinfo_t), "Texinfo");
	numclipnodes = CopyLump (LUMP_CLIPNODES, &dclipnodes, sizeof (dclipnode_t), "Clipnode");
	numfaces = CopyLump (LUMP_FACES, &dfaces, sizeof (dface_t), "Face");
	nummarksurfaces = CopyLump (LUMP_MARKSURFACES, &dmarksurfaces, sizeof (dmarksurfaces[0]), "Marksurface");
	numsurfedges = CopyLump (LUMP_SURFEDGES, &dsurfedges, sizeof (dsurfedges[0]), "Surfedge");
	numedges = CopyLump (LUMP_EDGES, &dedges, sizeof (dedge_t), "Edge");

	texdatasize = CopyLump (LUMP_TEXTURES, &dtexdata, 1, "Texture");
	visdatasize = CopyLump (LUMP_VISIBILITY, &dvisdata, 1, "Visdata");
	lightdatasize1 = CopyLump (LUMP_LIGHTING, &dlightdata1, 1, "Lightdata");
	entdatasize = CopyLump (LUMP_ENTITIES, &dentdata, 1, "Entdata");

	free (header);		// everything has been copied out
	header = NULL;

	//
	// swap everything
	//
	SwapBSPFile (false);
	ChkBSPFile ();
}

//============================================================================

FILE		*wadfile;
dheader_t	outheader;

void AddLump (int lumpnum, void *data, int len)
{
	lump_t *lump;
	int    extra;
	byte   padd[4] = {0, 0, 0, 0};

	lump = &header->lumps[lumpnum];

	lump->fileofs = LittleLong (ftell (wadfile));
	lump->filelen = LittleLong (len);
	SafeWrite (wadfile, data, len);

	extra = ((len + 3) & ~3) - len;

	if (extra > 0)
		SafeWrite (wadfile, padd, extra); // Padd with zeroes to even 4-byte boundary
}

/*
=============
WriteBSPFile

Swaps the bsp file in place, so it should not be referenced again
=============
*/
void	WriteBSPFile (char *filename)
{
	int i;
	char litfilename[256];
	qboolean writelit = false;

	strcpy (litfilename, filename);

	for (i = strlen (litfilename); i; i--)
	{
		if (litfilename[i] == '.')
		{
			litfilename[i + 1] = 'l';
			litfilename[i + 2] = 'i';
			litfilename[i + 3] = 't';
			litfilename[i + 4] = 0;
			writelit = true;
			break;
		}
	}

	header = &outheader;
	memset (header, 0, sizeof (dheader_t));

	SwapBSPFile (true);

	header->version = LittleLong (BSPVERSION);

	wadfile = SafeOpenWrite (filename);
	SafeWrite (wadfile, header, sizeof (dheader_t));	// overwritten later

	AddLump (LUMP_PLANES, dplanes, numplanes * sizeof (dplane_t));
	AddLump (LUMP_LEAFS, dleafs, numleafs * sizeof (dleaf_t));
	AddLump (LUMP_VERTEXES, dvertexes, numvertexes * sizeof (dvertex_t));
	AddLump (LUMP_NODES, dnodes, numnodes * sizeof (dnode_t));
	AddLump (LUMP_TEXINFO, texinfo, numtexinfo * sizeof (texinfo_t));
	AddLump (LUMP_FACES, dfaces, numfaces * sizeof (dface_t));
	AddLump (LUMP_CLIPNODES, dclipnodes, numclipnodes * sizeof (dclipnode_t));
	AddLump (LUMP_MARKSURFACES, dmarksurfaces, nummarksurfaces * sizeof (dmarksurfaces[0]));
	AddLump (LUMP_SURFEDGES, dsurfedges, numsurfedges * sizeof (dsurfedges[0]));
	AddLump (LUMP_EDGES, dedges, numedges * sizeof (dedge_t));
	AddLump (LUMP_MODELS, dmodels, nummodels * sizeof (dmodel_t));

	AddLump (LUMP_LIGHTING, dlightdata1, lightdatasize1);
	AddLump (LUMP_VISIBILITY, dvisdata, visdatasize);
	AddLump (LUMP_ENTITIES, dentdata, entdatasize);
	AddLump (LUMP_TEXTURES, dtexdata, texdatasize);

	fseek (wadfile, 0, SEEK_SET);
	SafeWrite (wadfile, header, sizeof (dheader_t));
	fclose (wadfile);

	if (writelit && dlightdata3)
	{
		int hdr;

		wadfile = SafeOpenWrite (litfilename);

		hdr = 0x54494C51;
		SafeWrite (wadfile, &hdr, sizeof (int));
		hdr = 1;
		SafeWrite (wadfile, &hdr, sizeof (int));
		SafeWrite (wadfile, dlightdata3, lightdatasize1 * 3);
		fclose (wadfile);
	}
}

//============================================================================

/*
=============
PrintBSPFileSizes

Dumps info about current file
=============
*/
void PrintBSPFileSizes (void)
{
	printf ("%6i planes      %7i\n"
			, numplanes, (int) (numplanes * sizeof (dplane_t)));
	printf ("%6i vertexes    %7i\n"
			, numvertexes, (int) (numvertexes * sizeof (dvertex_t)));
	printf ("%6i nodes       %7i\n"
			, numnodes, (int) (numnodes * sizeof (dnode_t)));
	printf ("%6i texinfo     %7i\n"
			, numtexinfo, (int) (numtexinfo * sizeof (texinfo_t)));
	printf ("%6i faces       %7i\n"
			, numfaces, (int) (numfaces * sizeof (dface_t)));
	printf ("%6i clipnodes   %7i\n"
			, numclipnodes, (int) (numclipnodes * sizeof (dclipnode_t)));
	printf ("%6i leafs       %7i\n"
			, numleafs, (int) (numleafs * sizeof (dleaf_t)));
	printf ("%6i marksurfaces%7i\n"
			, nummarksurfaces, (int) (nummarksurfaces * sizeof (dmarksurfaces[0])));
	printf ("%6i surfedges   %7i\n"
			, numsurfedges, (int) (numsurfedges * sizeof (dsurfedges[0])));
	printf ("%6i edges       %7i\n"
			, numedges, (int) (numedges * sizeof (dedge_t)));
	printf ("%6i models      %7i\n"
			, nummodels, (int) (nummodels * sizeof (dmodel_t)));

	printf ("%6i textures  %9i\n", texdatasize ? ((dmiptexlump_t *) dtexdata)->nummiptex : 0, texdatasize);
	printf ("%6s lightdata %9i\n", "", lightdatasize1);
	printf ("%6s visdata   %9i\n", "", visdatasize);
	printf ("%6s entdata   %9i\n", "", entdatasize);
}


