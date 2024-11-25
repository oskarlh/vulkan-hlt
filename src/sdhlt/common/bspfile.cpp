#include <cstddef>
#include <filesystem>
#include <numbers>
#include <span>

#include "cmdlib.h"
#include "filelib.h"
#include "messages.h"
#include "hlassert.h"
#include "log.h"
#include "mathlib.h"
#include "bspfile.h"
#include "scriplib.h"
#include "blockmem.h"
#include "cli_option_defaults.h"

//=============================================================================

std::ptrdiff_t g_max_map_miptex = cli_option_defaults::max_map_miptex;
std::ptrdiff_t g_max_map_lightdata = cli_option_defaults::max_map_lightdata;

int             g_nummodels;
dmodel_t        g_dmodels[MAX_MAP_MODELS];
int             g_dmodels_checksum;

int             g_visdatasize;
byte            g_dvisdata[MAX_MAP_VISIBILITY];
int             g_dvisdata_checksum;

int             g_lightdatasize;
byte*           g_dlightdata;
int             g_dlightdata_checksum;

int             g_texdatasize;
byte*           g_dtexdata;                                  // (dmiptexlump_t)
int             g_dtexdata_checksum;

int             g_entdatasize;
char            g_dentdata[MAX_MAP_ENTSTRING];
int             g_dentdata_checksum;

int             g_numleafs;
dleaf_t         g_dleafs[MAX_MAP_LEAFS];
int             g_dleafs_checksum;

int             g_numplanes;
dplane_t        g_dplanes[MAX_INTERNAL_MAP_PLANES];
int             g_dplanes_checksum;

int             g_numvertexes;
dvertex_t       g_dvertexes[MAX_MAP_VERTS];
int             g_dvertexes_checksum;

int             g_numnodes;
dnode_t         g_dnodes[MAX_MAP_NODES];
int             g_dnodes_checksum;

int             g_numtexinfo;

texinfo_t       g_texinfo[MAX_INTERNAL_MAP_TEXINFO];
int             g_texinfo_checksum;

int             g_numfaces;
dface_t         g_dfaces[MAX_MAP_FACES];
int             g_dfaces_checksum;

int				g_iWorldExtent = 65536; // ENGINE_ENTITY_RANGE; // -worldextent // seedee

int             g_numclipnodes;
dclipnode_t     g_dclipnodes[MAX_MAP_CLIPNODES];
int             g_dclipnodes_checksum;

int             g_numedges;
dedge_t         g_dedges[MAX_MAP_EDGES];
int             g_dedges_checksum;

int             g_nummarksurfaces;
unsigned short  g_dmarksurfaces[MAX_MAP_MARKSURFACES];
int             g_dmarksurfaces_checksum;

int             g_numsurfedges;
int             g_dsurfedges[MAX_MAP_SURFEDGES];
int             g_dsurfedges_checksum;

int             g_numentities;
entity_t        g_entities[MAX_MAP_ENTITIES];

/*
 * ===============
 * FastChecksum
 * ===============
 */

static int      FastChecksum(const void* const buffer, int bytes)
{
    int             checksum = 0;
    char*           buf = (char*)buffer;

    while (bytes--)
    {
        checksum = rotl(checksum, 4) ^ (*buf);
        buf++;
    }

    return checksum;
}

/*
 * ===============
 * CompressVis
 * ===============
 */
int             CompressVis(const byte* const src, const unsigned int src_length, byte* dest, unsigned int dest_length)
{
    unsigned int    j;
    byte*           dest_p = dest;
    unsigned int    current_length = 0;

    for (j = 0; j < src_length; j++)
    {
        current_length++;
        hlassume(current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW);

        *dest_p = src[j];
        dest_p++;

        if (src[j])
        {
            continue;
        }

        unsigned char   rep = 1;

        for (j++; j < src_length; j++)
        {
            if (src[j] || rep == 255)
            {
                break;
            }
            else
            {
                rep++;
            }
        }
        current_length++;
        hlassume(current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW);

        *dest_p = rep;
        dest_p++;
        j--;
    }

    return dest_p - dest;
}

// =====================================================================================
//  DecompressVis
//      
// =====================================================================================
void            DecompressVis(const byte* src, byte* const dest, const unsigned int dest_length)
{
    unsigned int    current_length = 0;
    int             c;
    byte*           out;
    int             row;

	row = (g_dmodels[0].visleafs + 7) >> 3; // same as the length used by VIS program in CompressVis
	// The wrong size will cause DecompressVis to spend extremely long time once the source pointer runs into the invalid area in g_dvisdata (for example, in BuildFaceLights, some faces could hang for a few seconds), and sometimes to crash.

    out = dest;

    do
	{
		hlassume (src - g_dvisdata < g_visdatasize, assume_DECOMPRESSVIS_OVERFLOW);

        if (*src)
        {
            current_length++;
            hlassume(current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW);

            *out = *src;
            out++;
            src++;
            continue;
        }
		
		hlassume (&src[1] - g_dvisdata < g_visdatasize, assume_DECOMPRESSVIS_OVERFLOW);

        c = src[1];
        src += 2;
        while (c)
        {
            current_length++;
            hlassume(current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW);

            *out = 0;
            out++;
            c--;

            if (out - dest >= row)
            {
                return;
            }
        }
    }
    while (out - dest < row);
}

//
// =====================================================================================
//

// =====================================================================================
//  SwapBSPFile
//      byte swaps all data in a bsp file
// =====================================================================================
static void     SwapBSPFile(const bool todisk)
{
    int             i, j, c;
    dmodel_t*       d;
    dmiptexlump_t*  mtl;

    // models       
    for (i = 0; i < g_nummodels; i++)
    {
        d = &g_dmodels[i];

        for (j = 0; j < MAX_MAP_HULLS; j++)
        {
            d->headnode[j] = LittleLong(d->headnode[j]);
        }

        d->visleafs = LittleLong(d->visleafs);
        d->firstface = LittleLong(d->firstface);
        d->numfaces = LittleLong(d->numfaces);

        for (j = 0; j < 3; j++)
        {
            d->mins[j] = LittleFloat(d->mins[j]);
            d->maxs[j] = LittleFloat(d->maxs[j]);
            d->origin[j] = LittleFloat(d->origin[j]);
        }
    }

    //
    // vertexes
    //
    for (i = 0; i < g_numvertexes; i++)
    {
        for (j = 0; j < 3; j++)
        {
            g_dvertexes[i].point[j] = LittleFloat(g_dvertexes[i].point[j]);
        }
    }

    //
    // planes
    //      
    for (i = 0; i < g_numplanes; i++)
    {
        for (j = 0; j < 3; j++)
        {
            g_dplanes[i].normal[j] = LittleFloat(g_dplanes[i].normal[j]);
        }
        g_dplanes[i].dist = LittleFloat(g_dplanes[i].dist);
        g_dplanes[i].type = (planetypes)LittleLong(g_dplanes[i].type);
    }

    //
    // texinfos
    //      
    for (i = 0; i < g_numtexinfo; i++)
    {
        for (j = 0; j < 8; j++)
        {
            g_texinfo[i].vecs[0][j] = LittleFloat(g_texinfo[i].vecs[0][j]);
        }
        g_texinfo[i].miptex = LittleLong(g_texinfo[i].miptex);
        g_texinfo[i].flags = LittleLong(g_texinfo[i].flags);
    }

    //
    // faces
    //
    for (i = 0; i < g_numfaces; i++)
    {
        g_dfaces[i].texinfo = LittleShort(g_dfaces[i].texinfo);
        g_dfaces[i].planenum = LittleShort(g_dfaces[i].planenum);
        g_dfaces[i].side = LittleShort(g_dfaces[i].side);
        g_dfaces[i].lightofs = LittleLong(g_dfaces[i].lightofs);
        g_dfaces[i].firstedge = LittleLong(g_dfaces[i].firstedge);
        g_dfaces[i].numedges = LittleShort(g_dfaces[i].numedges);
    }

    //
    // nodes
    //
    for (i = 0; i < g_numnodes; i++)
    {
        g_dnodes[i].planenum = LittleLong(g_dnodes[i].planenum);
        for (j = 0; j < 3; j++)
        {
            g_dnodes[i].mins[j] = LittleShort(g_dnodes[i].mins[j]);
            g_dnodes[i].maxs[j] = LittleShort(g_dnodes[i].maxs[j]);
        }
        g_dnodes[i].children[0] = LittleShort(g_dnodes[i].children[0]);
        g_dnodes[i].children[1] = LittleShort(g_dnodes[i].children[1]);
        g_dnodes[i].firstface = LittleShort(g_dnodes[i].firstface);
        g_dnodes[i].numfaces = LittleShort(g_dnodes[i].numfaces);
    }

    //
    // leafs
    //
    for (i = 0; i < g_numleafs; i++)
    {
        g_dleafs[i].contents = LittleLong(g_dleafs[i].contents);
        for (j = 0; j < 3; j++)
        {
            g_dleafs[i].mins[j] = LittleShort(g_dleafs[i].mins[j]);
            g_dleafs[i].maxs[j] = LittleShort(g_dleafs[i].maxs[j]);
        }

        g_dleafs[i].firstmarksurface = LittleShort(g_dleafs[i].firstmarksurface);
        g_dleafs[i].nummarksurfaces = LittleShort(g_dleafs[i].nummarksurfaces);
        g_dleafs[i].visofs = LittleLong(g_dleafs[i].visofs);
    }

    //
    // clipnodes
    //
    for (i = 0; i < g_numclipnodes; i++)
    {
        g_dclipnodes[i].planenum = LittleLong(g_dclipnodes[i].planenum);
        g_dclipnodes[i].children[0] = LittleShort(g_dclipnodes[i].children[0]);
        g_dclipnodes[i].children[1] = LittleShort(g_dclipnodes[i].children[1]);
    }

    //
    // miptex
    //
    if (g_texdatasize)
    {
        mtl = (dmiptexlump_t*)g_dtexdata;
        if (todisk)
        {
            c = mtl->nummiptex;
        }
        else
        {
            c = LittleLong(mtl->nummiptex);
        }
        mtl->nummiptex = LittleLong(mtl->nummiptex);
        for (i = 0; i < c; i++)
        {
            mtl->dataofs[i] = LittleLong(mtl->dataofs[i]);
        }
    }

    //
    // marksurfaces
    //
    for (i = 0; i < g_nummarksurfaces; i++)
    {
        g_dmarksurfaces[i] = LittleShort(g_dmarksurfaces[i]);
    }

    //
    // surfedges
    //
    for (i = 0; i < g_numsurfedges; i++)
    {
        g_dsurfedges[i] = LittleLong(g_dsurfedges[i]);
    }

    //
    // edges
    //
    for (i = 0; i < g_numedges; i++)
    {
        g_dedges[i].v[0] = LittleShort(g_dedges[i].v[0]);
        g_dedges[i].v[1] = LittleShort(g_dedges[i].v[1]);
    }
}

// =====================================================================================
//  CopyLump
//      balh
// =====================================================================================
static int      CopyLump(int lump, void* dest, int size, const dheader_t* const header)
{
    int             length, ofs;

    length = header->lumps[lump].filelen;
    ofs = header->lumps[lump].fileofs;

    if (length % size)
    {
        Error("LoadBSPFile: odd lump size");
    }
	
	//special handling for tex and lightdata to keep things from exploding - KGP
	if(lump == LUMP_TEXTURES && dest == (void*)g_dtexdata)
	{ hlassume(g_max_map_miptex > length,assume_MAX_MAP_MIPTEX); }
	else if(lump == LUMP_LIGHTING && dest == (void*)g_dlightdata)
	{ hlassume(g_max_map_lightdata > length,assume_MAX_MAP_LIGHTING); }

    memcpy(dest, (byte*) header + ofs, length);

    return length / size;
}


// =====================================================================================
//  LoadBSPFile
//      balh
// =====================================================================================
void            LoadBSPFile(const std::filesystem::path& filename)
{
    dheader_t* header;
    LoadFile(filename, (char**)&header);
    LoadBSPImage(header);
}

// =====================================================================================
//  LoadBSPImage
//      balh
// =====================================================================================
void            LoadBSPImage(dheader_t* const header)
{
    unsigned int     i;

    // swap the header
    for (i = 0; i < sizeof(dheader_t) / 4; i++)
    {
        ((int*)header)[i] = LittleLong(((int*)header)[i]);
    }

    if (header->version != BSPVERSION)
    {
        Error("BSP is version %i, not %i", header->version, BSPVERSION);
    }

    g_nummodels = CopyLump(LUMP_MODELS, g_dmodels, sizeof(dmodel_t), header);
    g_numvertexes = CopyLump(LUMP_VERTEXES, g_dvertexes, sizeof(dvertex_t), header);
    g_numplanes = CopyLump(LUMP_PLANES, g_dplanes, sizeof(dplane_t), header);
    g_numleafs = CopyLump(LUMP_LEAFS, g_dleafs, sizeof(dleaf_t), header);
    g_numnodes = CopyLump(LUMP_NODES, g_dnodes, sizeof(dnode_t), header);
    g_numtexinfo = CopyLump(LUMP_TEXINFO, g_texinfo, sizeof(texinfo_t), header);
    g_numclipnodes = CopyLump(LUMP_CLIPNODES, g_dclipnodes, sizeof(dclipnode_t), header);
    g_numfaces = CopyLump(LUMP_FACES, g_dfaces, sizeof(dface_t), header);
    g_nummarksurfaces = CopyLump(LUMP_MARKSURFACES, g_dmarksurfaces, sizeof(g_dmarksurfaces[0]), header);
    g_numsurfedges = CopyLump(LUMP_SURFEDGES, g_dsurfedges, sizeof(g_dsurfedges[0]), header);
    g_numedges = CopyLump(LUMP_EDGES, g_dedges, sizeof(dedge_t), header);
    g_texdatasize = CopyLump(LUMP_TEXTURES, g_dtexdata, 1, header);
    g_visdatasize = CopyLump(LUMP_VISIBILITY, g_dvisdata, 1, header);
    g_lightdatasize = CopyLump(LUMP_LIGHTING, g_dlightdata, 1, header);
    g_entdatasize = CopyLump(LUMP_ENTITIES, g_dentdata, 1, header);

    Free(header);                                          // everything has been copied out

    //
    // swap everything
    //      
    SwapBSPFile(false);

    g_dmodels_checksum = FastChecksum(g_dmodels, g_nummodels * sizeof(g_dmodels[0]));
    g_dvertexes_checksum = FastChecksum(g_dvertexes, g_numvertexes * sizeof(g_dvertexes[0]));
    g_dplanes_checksum = FastChecksum(g_dplanes, g_numplanes * sizeof(g_dplanes[0]));
    g_dleafs_checksum = FastChecksum(g_dleafs, g_numleafs * sizeof(g_dleafs[0]));
    g_dnodes_checksum = FastChecksum(g_dnodes, g_numnodes * sizeof(g_dnodes[0]));
    g_texinfo_checksum = FastChecksum(g_texinfo, g_numtexinfo * sizeof(g_texinfo[0]));
    g_dclipnodes_checksum = FastChecksum(g_dclipnodes, g_numclipnodes * sizeof(g_dclipnodes[0]));
    g_dfaces_checksum = FastChecksum(g_dfaces, g_numfaces * sizeof(g_dfaces[0]));
    g_dmarksurfaces_checksum = FastChecksum(g_dmarksurfaces, g_nummarksurfaces * sizeof(g_dmarksurfaces[0]));
    g_dsurfedges_checksum = FastChecksum(g_dsurfedges, g_numsurfedges * sizeof(g_dsurfedges[0]));
    g_dedges_checksum = FastChecksum(g_dedges, g_numedges * sizeof(g_dedges[0]));
    g_dtexdata_checksum = FastChecksum(g_dtexdata, g_numedges * sizeof(g_dtexdata[0]));
    g_dvisdata_checksum = FastChecksum(g_dvisdata, g_visdatasize * sizeof(g_dvisdata[0]));
    g_dlightdata_checksum = FastChecksum(g_dlightdata, g_lightdatasize * sizeof(g_dlightdata[0]));
    g_dentdata_checksum = FastChecksum(g_dentdata, g_entdatasize * sizeof(g_dentdata[0]));
}

//
// =====================================================================================
//

// =====================================================================================
//  AddLump
//      balh
// =====================================================================================
static void     AddLump(int lumpnum, void* data, int len, dheader_t* header, FILE* bspfile)
{
    lump_t* lump =&header->lumps[lumpnum];
    lump->fileofs = LittleLong(ftell(bspfile));
    lump->filelen = LittleLong(len);
    SafeWrite(bspfile, data, (len + 3) & ~3);
}

// =====================================================================================
//  WriteBSPFile
//      Swaps the bsp file in place, so it should not be referenced again
// =====================================================================================
void            WriteBSPFile(const std::filesystem::path& filename)
{
    dheader_t       outheader;
    dheader_t*      header;
    FILE*           bspfile;

    header = &outheader;
    memset(header, 0, sizeof(dheader_t));

    SwapBSPFile(true);

    header->version = LittleLong(BSPVERSION);

    bspfile = SafeOpenWrite(filename.c_str());
    SafeWrite(bspfile, header, sizeof(dheader_t));         // overwritten later

    //      LUMP TYPE       DATA            LENGTH                              HEADER  BSPFILE   
    AddLump(LUMP_PLANES,    g_dplanes,      g_numplanes * sizeof(dplane_t),     header, bspfile);
    AddLump(LUMP_LEAFS,     g_dleafs,       g_numleafs * sizeof(dleaf_t),       header, bspfile);
    AddLump(LUMP_VERTEXES,  g_dvertexes,    g_numvertexes * sizeof(dvertex_t),  header, bspfile);
    AddLump(LUMP_NODES,     g_dnodes,       g_numnodes * sizeof(dnode_t),       header, bspfile);
    AddLump(LUMP_TEXINFO,   g_texinfo,      g_numtexinfo * sizeof(texinfo_t),   header, bspfile);
    AddLump(LUMP_FACES,     g_dfaces,       g_numfaces * sizeof(dface_t),       header, bspfile);
    AddLump(LUMP_CLIPNODES, g_dclipnodes,   g_numclipnodes * sizeof(dclipnode_t), header, bspfile);

    AddLump(LUMP_MARKSURFACES, g_dmarksurfaces, g_nummarksurfaces * sizeof(g_dmarksurfaces[0]), header, bspfile);
    AddLump(LUMP_SURFEDGES, g_dsurfedges,   g_numsurfedges * sizeof(g_dsurfedges[0]), header, bspfile);
    AddLump(LUMP_EDGES,     g_dedges,       g_numedges * sizeof(dedge_t),       header, bspfile);
    AddLump(LUMP_MODELS,    g_dmodels,      g_nummodels * sizeof(dmodel_t),     header, bspfile);

    AddLump(LUMP_LIGHTING,  g_dlightdata,   g_lightdatasize,                    header, bspfile);
    AddLump(LUMP_VISIBILITY,g_dvisdata,     g_visdatasize,                      header, bspfile);
    AddLump(LUMP_ENTITIES,  g_dentdata,     g_entdatasize,                      header, bspfile);
    AddLump(LUMP_TEXTURES,  g_dtexdata,     g_texdatasize,                      header, bspfile);

    fseek(bspfile, 0, SEEK_SET);
    SafeWrite(bspfile, header, sizeof(dheader_t));

    fclose(bspfile);
}


// =====================================================================================
//  GetFaceExtents
// =====================================================================================

float CalculatePointVecsProduct (const volatile float *point, const volatile float *vecs)
{
	volatile double val;
	volatile double tmp;

	val = (double)point[0] * (double)vecs[0]; // always do one operation at a time and save to memory
	tmp = (double)point[1] * (double)vecs[1];
	val = val + tmp;
	tmp = (double)point[2] * (double)vecs[2];
	val = val + tmp;
	val = val + (double)vecs[3];

	return (float)val;
}

bool CalcFaceExtents_test ()
{
	const int numtestcases = 6;
	volatile float testcases[numtestcases][8] = {
		{1, 1, 1, 1, 0.375 * std::numeric_limits<double>::epsilon(), 0.375 * std::numeric_limits<double>::epsilon(), -1, 0},
		{1, 1, 1, 0.375 * std::numeric_limits<double>::epsilon(), 0.375 * std::numeric_limits<double>::epsilon(), 1, -1, std::numeric_limits<double>::epsilon()},
		{std::numeric_limits<double>::epsilon(), std::numeric_limits<double>::epsilon(), 1, 0.375, 0.375, 1, -1, std::numeric_limits<double>::epsilon()},
		{1, 1, 1, 1, 1, 0.375 * std::numeric_limits<float>::epsilon(), -2, 0.375 * std::numeric_limits<float>::epsilon()},
		{1, 1, 1, 1, 0.375 * std::numeric_limits<float>::epsilon(), 1, -2, 0.375 * std::numeric_limits<float>::epsilon()},
		{1, 1, 1, 0.375 * std::numeric_limits<float>::epsilon(), 1, 1, -2, 0.375 * std::numeric_limits<float>::epsilon()}};
	bool ok;

	// If the test failed, please check:
	//   1. whether the calculation is performed on FPU
	//   2. whether the register precision is too low

	ok = true;
	for (int i = 0; i < 6; i++)
	{
		float val = CalculatePointVecsProduct (&testcases[i][0], &testcases[i][3]);
		if (val != testcases[i][7])
		{
			Warning ("internal error: CalcFaceExtents_test failed on case %d (%.20f != %.20f).", i, val, testcases[i][7]);
			ok = false;
		}
	}
	return ok;
}

void GetFaceExtents (int facenum, int mins_out[2], int maxs_out[2])
{
	dface_t *f;
	float mins[2], maxs[2], val;
	int i, j, e;
	dvertex_t *v;
	texinfo_t *tex;
	int bmins[2], bmaxs[2];

	f = &g_dfaces[facenum];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = &g_texinfo[ParseTexinfoForFace (f)];

	for (i = 0; i < f->numedges; i++)
	{
		e = g_dsurfedges[f->firstedge + i];
		if (e >= 0)
		{
			v = &g_dvertexes[g_dedges[e].v[0]];
		}
		else
		{
			v = &g_dvertexes[g_dedges[-e].v[1]];
		}
		for (j = 0; j < 2; j++)
		{
			// The old code: val = v->point[0] * tex->vecs[j][0] + v->point[1] * tex->vecs[j][1] + v->point[2] * tex->vecs[j][2] + tex->vecs[j][3];
			//   was meant to be compiled for x86 under MSVC (prior to VS 11), so the intermediate values were stored as 64-bit double by default.
			// The new code will produce the same result as the old code, but it's portable for different platforms.
			// See this article for details: Intermediate Floating-Point Precision by Bruce-Dawson http://www.altdevblogaday.com/2012/03/22/intermediate-floating-point-precision/

			// The essential reason for having this ugly code is to get exactly the same value as the counterpart of game engine.
			// The counterpart of game engine is the function CalcFaceExtents in HLSDK.
			// So we must also know how Valve compiles HLSDK. I think Valve compiles HLSDK with VC6.0 in the past.
			val = CalculatePointVecsProduct (v->point, tex->vecs[j]);
			if (val < mins[j])
			{
				mins[j] = val;
			}
			if (val > maxs[j])
			{
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++)
	{
		bmins[i] = (int)floor (mins[i] / TEXTURE_STEP);
		bmaxs[i] = (int)ceil (maxs[i] / TEXTURE_STEP);
	}

	for (i = 0; i < 2; i++)
	{
		mins_out[i] = bmins[i];
		maxs_out[i] = bmaxs[i];
	}
}

// =====================================================================================
//  WriteExtentFile
// =====================================================================================
void WriteExtentFile (const std::filesystem::path& filename)
{
	FILE *f;
	f = fopen (filename.c_str(), "w");
	if (!f)
	{
		Error ("Error opening %s: %s", filename.c_str(), strerror(errno));
	}
	fprintf (f, "%i\n", g_numfaces);
	for (int i = 0; i < g_numfaces; i++)
	{
		int mins[2];
		int maxs[2];
		GetFaceExtents (i, mins, maxs);
		fprintf (f, "%i %i %i %i\n", mins[0], mins[1], maxs[0], maxs[1]);
	}
	fclose (f);
}


//
// =====================================================================================
//
const int BLOCK_WIDTH = 128;
const int BLOCK_HEIGHT = 128;
typedef struct lightmapblock_s
{
	lightmapblock_s *next;
	bool used;
	int allocated[BLOCK_WIDTH];
}
lightmapblock_t;
void DoAllocBlock (lightmapblock_t *blocks, int w, int h)
{
	lightmapblock_t *block;
	// code from Quake
	int i, j;
	int best, best2;
	int x, y;
	if (w < 1 || h < 1)
	{
		Error ("DoAllocBlock: internal error.");
	}
	for (block = blocks; block; block = block->next)
	{
		best = BLOCK_HEIGHT;
		for (i = 0; i < BLOCK_WIDTH - w; i++)
		{
			best2 = 0;
			for (j = 0; j < w; j++)
			{
				if (block->allocated[i + j] >= best)
					break;
				if (block->allocated[i + j] > best2)
					best2 = block->allocated[i + j];
			}
			if (j == w)
			{
				x = i;
				y = best = best2;
			}
		}
		if (best + h <= BLOCK_HEIGHT)
		{
			block->used = true;
			for (i = 0; i < w; i++)
			{
				block->allocated[x + i] = best + h;
			}
			return;
		}
		if (!block->next)
		{ // need to allocate a new block
			if (!block->used)
			{
				Warning ("CountBlocks: invalid extents %dx%d", w, h);
				return;
			}
			block->next = (lightmapblock_t *)malloc (sizeof (lightmapblock_t));
			hlassume (block->next != nullptr, assume_NoMemory);
			memset (block->next, 0, sizeof (lightmapblock_t));
		}
	}
}
int CountBlocks ()
{
	lightmapblock_t *blocks;
	blocks = (lightmapblock_t *)malloc (sizeof (lightmapblock_t));
	hlassume (blocks != nullptr, assume_NoMemory);
	memset (blocks, 0, sizeof (lightmapblock_t));
	int k;
	for (k = 0; k < g_numfaces; k++)
	{
		dface_t *f = &g_dfaces[k];
		const char *texname =  GetTextureByNumber (ParseTexinfoForFace (f));
		if (!strncmp (texname, "sky", 3) //sky, no lightmap allocation.
			|| !strncmp (texname, "!", 1) || !strncasecmp (texname, "water", 5) || !strncasecmp (texname, "laser", 5) //water, no lightmap allocation.
			|| (g_texinfo[ParseTexinfoForFace (f)].flags & TEX_SPECIAL) //aaatrigger, I don't know.
			)
		{
			continue;
		}
		int extents[2];
		vec3_t point;
		{
			int bmins[2];
			int bmaxs[2];
			int i;
			GetFaceExtents (k, bmins, bmaxs);
			for (i = 0; i < 2; i++)
			{
				extents[i] = (bmaxs[i] - bmins[i]) * TEXTURE_STEP;
			}

			VectorClear (point);
			if (f->numedges > 0)
			{
				int e = g_dsurfedges[f->firstedge];
				dvertex_t *v = &g_dvertexes[g_dedges[abs (e)].v[e >= 0? 0: 1]];
				VectorCopy (v->point, point);
			}
		}
		if (extents[0] < 0 || extents[1] < 0 || extents[0] > qmax (512, MAX_SURFACE_EXTENT * TEXTURE_STEP) || extents[1] > qmax (512, MAX_SURFACE_EXTENT * TEXTURE_STEP))
			// the default restriction from the engine is 512, but place 'max (512, MAX_SURFACE_EXTENT * TEXTURE_STEP)' here in case someone raise the limit
		{
			Warning ("Bad surface extents %d/%d at position (%.0f,%.0f,%.0f)", extents[0], extents[1], point[0], point[1], point[2]);
			continue;
		}
		DoAllocBlock (blocks, (extents[0] / TEXTURE_STEP) + 1, (extents[1] / TEXTURE_STEP) + 1);
	}
	int count = 0;
	lightmapblock_t *next;
	for (; blocks; blocks = next)
	{
		if (blocks->used)
		{
			count++;
		}
		next = blocks->next;
		free (blocks);
	}
	return count;
}
bool NoWadTextures ()
{
	// copied from loadtextures.cpp
	int numtextures = g_texdatasize? ((dmiptexlump_t *)g_dtexdata)->nummiptex: 0;
	for (int i = 0; i < numtextures; i++)
	{
		int offset = ((dmiptexlump_t *)g_dtexdata)->dataofs[i];
		int size = g_texdatasize - offset;
		if (offset < 0 || size < (int)sizeof (miptex_t))
		{
			// missing textures have ofs -1
			continue;
		}
		miptex_t *mt = (miptex_t *)&g_dtexdata[offset];
		if (!mt->offsets[0]) //Check for valid mip texture
		{
			return false;
		}
	}
	return true;
}
char *FindWadValue ()
	// return NULL for syntax error
	// this function needs to be as stable as possible because it might be called from ripent
{
	int linestart, lineend;
	bool inentity = false;
	for (linestart = 0; linestart < g_entdatasize; )
	{
		for (lineend = linestart; lineend < g_entdatasize; lineend++)
			if (g_dentdata[lineend] == '\r' || g_dentdata[lineend] == '\n')
				break;
		if (lineend == linestart + 1)
		{
			if (g_dentdata[linestart] == '{')
			{
				if (inentity)
					return nullptr;
				inentity = true;
			}
			else if (g_dentdata[linestart] == '}')
			{
				if (!inentity)
					return nullptr;
				inentity = false;
				return _strdup (""); // only parse the first entity
			}
			else
				return nullptr;
		}
		else
		{
			if (!inentity)
				return nullptr;
			int quotes[4];
			int i, j;
			for (i = 0, j = linestart; i < 4; i++, j++)
			{
				for (; j < lineend; j++)
					if (g_dentdata[j] == '\"')
						break;
				if (j >= lineend)
					break;
				quotes[i] = j;
			}
			if (i != 4 || quotes[0] != linestart || quotes[3] != lineend - 1)
			{
				return nullptr;
			}
			if (quotes[1] - (quotes[0] + 1) == (int)strlen ("wad") && !strncmp (&g_dentdata[quotes[0] + 1], "wad", strlen ("wad")))
			{
				int len = quotes[3] - (quotes[2] + 1);
				char *value = (char *)malloc (len + 1);
				hlassume (value != nullptr, assume_NoMemory);
				memcpy (value, &g_dentdata[quotes[2] + 1], len);
				value[len] = '\0';
				return value;
			}
		}
		for (linestart = lineend; linestart < g_entdatasize; linestart++)
			if (g_dentdata[linestart] != '\r' && g_dentdata[linestart] != '\n')
				break;
	}
	return nullptr;
}

#define ENTRIES(a)		(sizeof(a)/sizeof(*(a)))
#define ENTRYSIZE(a)	(sizeof(*(a)))

// =====================================================================================
//  ArrayUsage
//      blah
// =====================================================================================
static int      ArrayUsage(const char* const szItem, const int items, const int maxitems, const int itemsize)
{
    float           percentage = maxitems ? items * 100.0 / maxitems : 0.0;

    Log("%-13s %7i/%-7i %8i/%-8i (%4.1f%%)\n", szItem, items, maxitems, items * itemsize, maxitems * itemsize, percentage);

    return items * itemsize;
}

// =====================================================================================
//  GlobUsage
//      pritn out global ussage line in chart
// =====================================================================================
static int      GlobUsage(const char* const szItem, const int itemstorage, const int maxstorage)
{
    float           percentage = maxstorage ? itemstorage * 100.0 / maxstorage : 0.0;

    Log("%-13s    [variable]   %8i/%-8i (%4.1f%%)\n", szItem, itemstorage, maxstorage, percentage);

    return itemstorage;
}

// =====================================================================================
//  PrintBSPFileSizes
//      Dumps info about current file
// =====================================================================================
void            PrintBSPFileSizes()
{
    int             numtextures = g_texdatasize ? ((dmiptexlump_t*)g_dtexdata)->nummiptex : 0;
    int             totalmemory = 0;
	int numallocblocks = CountBlocks ();
	int maxallocblocks = 64;
	bool nowadtextures = NoWadTextures (); // We don't have this check at hlcsg, because only legacy compile tools don't empty "wad" value in "-nowadtextures" compiles.
	char *wadvalue = FindWadValue ();

    Log("\n");
    Log("Object names  Objects/Maxobjs  Memory / Maxmem  Fullness\n");
    Log("------------  ---------------  ---------------  --------\n");

    totalmemory += ArrayUsage("models", g_nummodels, ENTRIES(g_dmodels), ENTRYSIZE(g_dmodels));
    totalmemory += ArrayUsage("planes", g_numplanes, MAX_MAP_PLANES, ENTRYSIZE(g_dplanes));
    totalmemory += ArrayUsage("vertexes", g_numvertexes, ENTRIES(g_dvertexes), ENTRYSIZE(g_dvertexes));
    totalmemory += ArrayUsage("nodes", g_numnodes, ENTRIES(g_dnodes), ENTRYSIZE(g_dnodes));
    totalmemory += ArrayUsage("texinfos", g_numtexinfo, MAX_MAP_TEXINFO, ENTRYSIZE(g_texinfo));
    totalmemory += ArrayUsage("faces", g_numfaces, ENTRIES(g_dfaces), ENTRYSIZE(g_dfaces));
	totalmemory += ArrayUsage("* worldfaces", (g_nummodels > 0? g_dmodels[0].numfaces: 0), MAX_MAP_WORLDFACES, 0);
    totalmemory += ArrayUsage("clipnodes", g_numclipnodes, ENTRIES(g_dclipnodes), ENTRYSIZE(g_dclipnodes));
    totalmemory += ArrayUsage("leaves", g_numleafs, MAX_MAP_LEAFS, ENTRYSIZE(g_dleafs));
    totalmemory += ArrayUsage("* worldleaves", (g_nummodels > 0? g_dmodels[0].visleafs: 0), MAX_MAP_LEAFS_ENGINE, 0);
    totalmemory += ArrayUsage("marksurfaces", g_nummarksurfaces, ENTRIES(g_dmarksurfaces), ENTRYSIZE(g_dmarksurfaces));
    totalmemory += ArrayUsage("surfedges", g_numsurfedges, ENTRIES(g_dsurfedges), ENTRYSIZE(g_dsurfedges));
    totalmemory += ArrayUsage("edges", g_numedges, ENTRIES(g_dedges), ENTRYSIZE(g_dedges));

    totalmemory += GlobUsage("texdata", g_texdatasize, g_max_map_miptex);
    totalmemory += GlobUsage("lightdata", g_lightdatasize, g_max_map_lightdata);
    totalmemory += GlobUsage("visdata", g_visdatasize, sizeof(g_dvisdata));
    totalmemory += GlobUsage("entdata", g_entdatasize, sizeof(g_dentdata));
	if (numallocblocks == -1)
	{
		Log ("* AllocBlock    [ not available to the " PLATFORM_VERSION " version ]\n");
	}
	else
	{
	totalmemory += ArrayUsage ("* AllocBlock", numallocblocks, maxallocblocks, 0);
	}

    Log("%i textures referenced\n", numtextures);

    Log("=== Total BSP file data space used: %d bytes ===\n\n", totalmemory);
	if (nowadtextures)
	{
		Log ("No wad files required to run the map\n");
	}
	else if (wadvalue == nullptr)
	{
		Log ("Wad files required to run the map: (Couldn't parse wad keyvalue from entity data)\n");
	}
	else //If we have any wads still required //seedee
	{
		Log("Wad files required to run the map\n");
		Log("---------------------------------\n");
		size_t length = strlen(wadvalue) + 1;
		char* wadvalueNewline = new char[length]; //+1 for null terminator
		safe_strncpy(wadvalueNewline, wadvalue, length); //Defensive copy

		for (size_t i = 0; i < length; ++i) {
			if (wadvalueNewline[i] == ';') {
				wadvalueNewline[i] = '\n';
			}
		}
		Log("%s", wadvalueNewline);
		delete[] wadvalueNewline;
		Log("---------------------------------\n\n");
	}
	if (wadvalue)
	{
		free (wadvalue);
	}
}


// =====================================================================================
//  ParseImplicitTexinfoFromTexture
//      purpose: get the actual texinfo for a face. the tools shouldn't directly use f->texinfo after embedlightmap is done
// =====================================================================================
int	ParseImplicitTexinfoFromTexture (int miptex)
{
	int texinfo;
	int numtextures = g_texdatasize? ((dmiptexlump_t *)g_dtexdata)->nummiptex: 0;
	int offset;
	int size;
	miptex_t *mt;
	char name[16];
	
	if (miptex < 0 || miptex >= numtextures)
	{
		Warning ("ParseImplicitTexinfoFromTexture: internal error: invalid texture number %d.", miptex);
		return -1;
	}
	offset = ((dmiptexlump_t *)g_dtexdata)->dataofs[miptex];
	size = g_texdatasize - offset;
	if (offset < 0 || g_dtexdata + offset < (byte *)&((dmiptexlump_t *)g_dtexdata)->dataofs[numtextures] ||
		size < (int)sizeof (miptex_t))
	{
		return -1;
	}

	mt = (miptex_t *)&g_dtexdata[offset];
	safe_strncpy (name, mt->name, 16);
	
	if (!(strlen (name) >= 6 && !strncasecmp (&name[1], "_rad", 4) && '0' <= name[5] && name[5] <= '9'))
	{
		return -1;
	}

	texinfo = atoi (&name[5]);
	if (texinfo < 0 || texinfo >= g_numtexinfo)
	{
		Warning ("Invalid index of original texinfo: %d parsed from texture name '%s'.", texinfo, name);
		return -1;
	}

	return texinfo;
}

int ParseTexinfoForFace (const dface_t *f)
{
	int texinfo;
	int miptex;
	int texinfo2;

	texinfo = f->texinfo;
	miptex = g_texinfo[texinfo].miptex;
	if (miptex != -1)
	{
		texinfo2 = ParseImplicitTexinfoFromTexture (miptex);
		if (texinfo2 != -1)
		{
			texinfo = texinfo2;
		}
	}

	return texinfo;
}

// =====================================================================================
//  DeleteEmbeddedLightmaps
//      removes all "?_rad*" textures that are created by hlrad
//      this function does nothing if the map has no textures with name "?_rad*"
// =====================================================================================
void DeleteEmbeddedLightmaps ()
{
	int countrestoredfaces = 0;
	int countremovedtexinfos = 0;
	int countremovedtextures = 0;
	int numtextures = g_texdatasize? ((dmiptexlump_t *)g_dtexdata)->nummiptex: 0;

	// Step 1: parse the original texinfo index stored in each "?_rad*" texture
	//         and restore the texinfo for the faces that have had their lightmap embedded

	for (int i = 0; i < g_numfaces; i++)
	{
		dface_t *f = &g_dfaces[i];
		const int texinfo = ParseTexinfoForFace (f);
		if (texinfo != f->texinfo)
		{
			f->texinfo = texinfo;
			countrestoredfaces++;
		}
	}

	// Step 2: remove redundant texinfo
	{
		bool *texinfoused = (bool *)malloc (g_numtexinfo * sizeof (bool));
		hlassume (texinfoused != nullptr, assume_NoMemory);

		for (int i = 0; i < g_numtexinfo; i++)
		{
			texinfoused[i] = false;
		}
		for (int i = 0; i < g_numfaces; i++)
		{
			const auto texinfo = g_dfaces[i].texinfo;

			if (texinfo < 0 || texinfo >= g_numtexinfo)
			{
				continue;
			}
			texinfoused[texinfo] = true;
		}
		int i;
		for (i = g_numtexinfo - 1; i > -1; i--)
		{
			const auto miptex = g_texinfo[i].miptex;

			if (texinfoused[i])
			{
				break; // still used by a face; should not remove this texinfo
			}
			if (miptex < 0 || miptex >= numtextures)
			{
				break; // invalid; should not remove this texinfo
			}
			if (ParseImplicitTexinfoFromTexture (miptex) == -1)
			{
				break; // not added by hlrad; should not remove this texinfo
			}
			countremovedtexinfos++;
		}
		g_numtexinfo = i + 1; // shrink g_texinfo
		free (texinfoused);
	}

	// Step 3: remove redundant textures
	{
		int numremaining; // number of remaining textures
		bool *textureused = (bool *)malloc (numtextures * sizeof (bool));
		hlassume (textureused != nullptr, assume_NoMemory);

		for (int i = 0; i < numtextures; i++)
		{
			textureused[i] = false;
		}
		for (int i = 0; i < g_numtexinfo; i++)
		{
			const auto miptex = g_texinfo[i].miptex;

			if (miptex < 0 || miptex >= numtextures)
			{
				continue;
			}
			textureused[miptex] = true;
		}
		int i;
		for (i = numtextures - 1; i > -1; i--)
		{
			if (textureused[i] || ParseImplicitTexinfoFromTexture (i) == -1)
			{
				break; // should not remove this texture
			}
			countremovedtextures++;
		}
		numremaining = i + 1;
		free (textureused);

		if (numremaining < numtextures)
		{
			dmiptexlump_t *texdata = (dmiptexlump_t *)g_dtexdata;
			byte *dataaddr = (byte *)&texdata->dataofs[texdata->nummiptex];
			int datasize = (g_dtexdata + texdata->dataofs[numremaining]) - dataaddr;
			byte *newdataaddr = (byte *)&texdata->dataofs[numremaining];
			memmove (newdataaddr, dataaddr, datasize);
			g_texdatasize = (newdataaddr + datasize) - g_dtexdata;
			texdata->nummiptex = numremaining;
			for (i = 0; i < numremaining; i++)
			{
				if (texdata->dataofs[i] < 0) // bad texture
				{
					continue;
				}
				texdata->dataofs[i] += newdataaddr - dataaddr;
			}

			numtextures = texdata->nummiptex;
		}
	}

	if (countrestoredfaces > 0 || countremovedtexinfos > 0 || countremovedtextures > 0)
	{
		Log ("DeleteEmbeddedLightmaps: restored %d faces, removed %d texinfos and %d textures.\n",
				countrestoredfaces, countremovedtexinfos, countremovedtextures);
	}
}


// =====================================================================================
//  ParseEpair
//      entity key/value pairs
// =====================================================================================
std::unique_ptr<epair_t>        ParseEpair()
{
	std::unique_ptr<epair_t> e = std::make_unique<epair_t>();

    if (strlen(g_token) >= MAX_KEY - 1)
        Error("ParseEpair: Key token too long (%i > MAX_KEY)", (int)strlen(g_token));

    e->key = std::u8string((const char8_t*) g_token);
    GetToken(false);

    if (strlen(g_token) >= MAX_VAL - 1) //MAX_VALUE //vluzacn
        Error("ParseEpar: Value token too long (%i > MAX_VALUE)", (int)strlen(g_token));

    e->value = std::u8string((const char8_t*) g_token);

    return e;
}

/*
 * ================
 * ParseEntity
 * ================
 */

// AJM: each tool should have its own version of GetParamsFromEnt which parseentity calls
extern void     GetParamsFromEnt(entity_t* mapent);

bool            ParseEntity()
{

    if (!GetToken(true))
    {
        return false;
    }

    if (strcmp(g_token, "{"))
    {
        Error("ParseEntity: { not found");
    }

    if (g_numentities == MAX_MAP_ENTITIES)
    {
        Error("g_numentities == MAX_MAP_ENTITIES");
    }

    entity_t* mapent = &g_entities[g_numentities];
    g_numentities++;

    while (1)
    {
        if (!GetToken(true))
        {
            Error("ParseEntity: EOF without closing brace");
        }
        if (!strcmp(g_token, "}"))
        {
            break;
        }
        std::unique_ptr<epair_t> e = ParseEpair();
        e->next = mapent->epairs;
        mapent->epairs = e.release();
    }

    if (key_value_is(mapent, u8"classname", u8"info_compile_parameters"))
    {
        Log("Map entity info_compile_parameters detected, using compile settings\n");
        GetParamsFromEnt(mapent);
    }
	// ugly code
	if (key_value_starts_with(mapent, u8"classname", u8"light") && key_value_is_not_empty(mapent, u8"_tex"))
	{
		SetKeyValue (mapent, u8"convertto", value_for_key(mapent, u8"classname"));
		SetKeyValue (mapent, u8"classname", u8"light_surface");
	}
	if (key_value_is(mapent, u8"convertfrom", u8"light_shadow")
		|| key_value_is (mapent, u8"convertfrom", u8"light_bounce")
		)
	{
		SetKeyValue (mapent, u8"convertto", value_for_key (mapent, u8"classname"));
		SetKeyValue (mapent, u8"classname", value_for_key (mapent, u8"convertfrom"));
		DeleteKey (mapent, u8"convertfrom");
	}
	if (classname_is(mapent, u8"light_environment") &&
		key_value_is(mapent, u8"convertfrom", u8"info_sunlight"))
	{
		DeleteAllKeys(mapent);
		memset (mapent, 0, sizeof(entity_t));
		g_numentities--;
		return true;
	}
	if (classname_is (mapent, u8"light_environment") &&
		IntForKey (mapent, u8"_fake"))
	{
		SetKeyValue (mapent, u8"classname", u8"info_sunlight");
	}

    return true;
}

// =====================================================================================
//  ParseEntities
//      Parses the dentdata string into entities
// =====================================================================================
void            ParseEntities()
{
    g_numentities = 0;
    ParseFromMemory(g_dentdata, g_entdatasize);

    while (ParseEntity())
    {
    }
}

// =====================================================================================
//  UnparseEntities
//      Generates the dentdata string from all the entities
// =====================================================================================
int anglesforvector (float angles[3], const float vector[3])
{
	float z = vector[2], r = sqrt (vector[0] * vector[0] + vector[1] * vector[1]);
	float tmp;
	if (sqrt (z*z + r*r) < NORMAL_EPSILON)
	{
		return -1;
	}
	else
	{
		tmp = sqrt (z*z + r*r);
		z /= tmp, r /= tmp;
		if (r < NORMAL_EPSILON)
		{
			if (z < 0)
			{
				angles[0] = -90, angles[1] = 0;
			}
			else
			{
				angles[0] = 90, angles[1] = 0;
			}
		}
		else
		{
			// std::numbers::pi_v<double> could be unnecessary precision, try using
			// std::numbers::pi_v<float> instead. -- Oskar
			angles[0] = atan (z / r) / std::numbers::pi_v<double> * 180;
			float x = vector[0], y = vector[1];
			tmp = sqrt (x*x + y*y);
			x /= tmp, y /= tmp;
			if (x < -1 + NORMAL_EPSILON)
			{
				angles[1] = -180;
			}
			else
			{
				if (y >= 0)
				{
					angles[1] = 2 * atan (y / (1+x)) / std::numbers::pi_v<double> * 180;
				}
				else
				{
					angles[1] = 2 * atan (y / (1+x)) / std::numbers::pi_v<double> * 180 + 360;
				}
			}
		}
	}
	angles[2] = 0;
	return 0;
}
void            UnparseEntities()
{
    char*           buf;
    char*           end;
    epair_t*        ep;
    char            line[MAXTOKEN];
    int             i;

    buf = g_dentdata;
    end = buf;
    *end = 0;

	for (i = 0; i < g_numentities; i++)
	{
		entity_t *mapent = &g_entities[i];
		if (
			classname_is(mapent, u8"info_sunlight") ||
			classname_is(mapent, u8"light_environment"))
		{
			float vec[3] = {0,0,0};
			{
				sscanf ((const char*) ValueForKey (mapent, u8"angles"), "%f %f %f", &vec[0], &vec[1], &vec[2]);
				float pitch = FloatForKey(mapent, u8"pitch");
				if (pitch)
					vec[0] = pitch;

				std::u8string_view target = value_for_key (mapent, u8"target");
				if (!target.empty())
				{
					entity_t *targetent = FindTargetEntity (target);
					if (targetent)
					{
						float origin1[3] = {0,0,0}, origin2[3] = {0,0,0}, normal[3];
						sscanf ((const char*) ValueForKey (mapent, u8"origin"), "%f %f %f", &origin1[0], &origin1[1], &origin1[2]);
						sscanf ((const char*) ValueForKey (targetent, u8"origin"), "%f %f %f", &origin2[0], &origin2[1], &origin2[2]);
						VectorSubtract (origin2, origin1, normal);
						anglesforvector (vec, normal);
					}
				}
			}
			char stmp[1024];
			safe_snprintf (stmp, 1024, "%g %g %g", vec[0], vec[1], vec[2]);
			SetKeyValue (mapent, u8"angles", (const char8_t*) stmp);
			DeleteKey (mapent, u8"pitch");

			if (!strcmp ((const char*) ValueForKey (mapent, u8"classname"), "info_sunlight"))
			{
				if (g_numentities == MAX_MAP_ENTITIES)
				{
					Error("g_numentities == MAX_MAP_ENTITIES");
				}
				entity_t *newent = &g_entities[g_numentities++];
				newent->epairs = mapent->epairs;
				SetKeyValue (newent, u8"classname",u8"light_environment");
				SetKeyValue (newent, u8"_fake", u8"1");
				mapent->epairs = nullptr;
			}
		}
	}
    for (i = 0; i < g_numentities; i++)
	{
		entity_t *mapent = &g_entities[i];
		if (!strcmp ((const char*) ValueForKey (mapent, u8"classname"), "light_shadow")
			|| !strcmp ((const char*) ValueForKey (mapent, u8"classname"), "light_bounce")
			)
		{
			SetKeyValue (mapent, u8"convertfrom", ValueForKey (mapent, u8"classname"));
			SetKeyValue (mapent, u8"classname", (*ValueForKey (mapent, u8"convertto")? ValueForKey (mapent, u8"convertto"): u8"light"));
			DeleteKey (mapent, u8"convertto");
		}
	}
	// ugly code
	for (i = 0; i < g_numentities; i++)
	{
		entity_t *mapent = &g_entities[i];
		if (classname_is(mapent, u8"light_surface"))
		{
			if (key_value_is_empty(mapent, u8"_tex"))
			{
				SetKeyValue (mapent, u8"_tex", u8"                ");
			}
			std::u8string_view newclassname = value_for_key (mapent, u8"convertto");
			if (newclassname.empty())
			{
				SetKeyValue (mapent, u8"classname", u8"light");
			}
			else if (!newclassname.starts_with(u8"light"))
			{
				Error ("New classname for 'light_surface' should begin with 'light' not '%s'.\n", (const char*) newclassname.data());
			}
			else
			{
				SetKeyValue (mapent, u8"classname", newclassname);
			}
			DeleteKey (mapent, u8"convertto");
		}
	}
#ifdef SDHLCSG //seedee
	extern bool g_nolightopt;
	if (!g_nolightopt)
	{
		int i, j;
		int count = 0;
		bool *lightneedcompare = (bool *)malloc (g_numentities * sizeof (bool));
		hlassume (lightneedcompare != nullptr, assume_NoMemory);
		memset (lightneedcompare, 0, g_numentities * sizeof(bool));
		for (i = g_numentities - 1; i > -1; i--)
		{
			entity_t *ent = &g_entities[i];
			const char8_t *classname = ValueForKey (ent, u8"classname");
			const char8_t *targetname = ValueForKey (ent, u8"targetname");
			int style = IntForKey (ent, u8"style");
			if (!targetname[0] || strcmp ((const char*) classname, "light") && strcmp ((const char*) classname, "light_spot") && strcmp ((const char*) classname, "light_environment"))
				continue;
			for (j = i + 1; j < g_numentities; j++)
			{
				if (!lightneedcompare[j])
					continue;
				entity_t *ent2 = &g_entities[j];
				const char8_t *targetname2 = ValueForKey (ent2, u8"targetname");
				int style2 = IntForKey (ent2, u8"style");
				if (style == style2 && !strcmp ((const char*) targetname, (const char*) targetname2))
					break;
			}
			if (j < g_numentities)
			{
				DeleteKey (ent, u8"targetname");
				count++;
			}
			else
			{
				lightneedcompare[i] = true;
			}
		}
		if (count > 0)
		{
			Log ("%d redundant named lights optimized.\n", count);
		}
		free (lightneedcompare);
	}
#endif
    for (i = 0; i < g_numentities; i++)
    {
        ep = g_entities[i].epairs;
        if (!ep)
        {
            continue;                                      // ent got removed
        }

        strcat(end, "{\n");
        end += 2;

        for (ep = g_entities[i].epairs; ep; ep = ep->next)
        {
            snprintf(line, sizeof(line), "\"%s\" \"%s\"\n", (const char*) ep->key.c_str(), (const char*) ep->value.c_str());
            strcat(end, line);
            end += strlen(line);
        }
        strcat(end, "}\n");
        end += 2;

        if (end > buf + MAX_MAP_ENTSTRING)
        {
            Error("Entity text too long");
        }
    }
    g_entdatasize = end - buf + 1;
}

void DeleteAllKeys(entity_t* ent)
{
	epair_t **pep;
	for (pep = &ent->epairs; *pep; pep = &(*pep)->next)
	{
		epair_t *ep = *pep;
		*pep = ep->next;
		delete ep;
	}
}
void DeleteKey(entity_t* ent, std::u8string_view key)
{
	epair_t **pep;
	for (pep = &ent->epairs; *pep; pep = &(*pep)->next)
	{
		if ((*pep)->key == key)
		{
			epair_t *ep = *pep;
			*pep = ep->next;
			delete ep;
			return;
		}
	}
}
void SetKeyValue(entity_t* ent, std::u8string_view key, std::u8string_view value) {
	if (value.empty())
	{
		DeleteKey (ent, key);
		return;
	}
    for (epair_t* ep = ent->epairs; ep; ep = ep->next)
    {
        if (ep->key == key)
        {
			ep->value = std::move(value);
            return;
        }
    }
    std::unique_ptr<epair_t> ep = std::make_unique<epair_t>();
    ep->next = ent->epairs;
    ep->key = key;
    ep->value = std::move(value);
    ent->epairs = ep.release();
}

// =====================================================================================
//  ValueForKey
//      returns the value for a passed entity and key
// =====================================================================================
const char8_t*     ValueForKey(const entity_t* const ent, std::u8string_view key)
{
    epair_t*        ep;

    for (ep = ent->epairs; ep; ep = ep->next)
    {
        if (ep->key == key)
        {
            return ep->value.c_str();
        }
    }
    return u8"";
}


std::u8string_view value_for_key(const entity_t* const ent, std::u8string_view key)
{
	// TODO: This is inefficient. When ValueForKey is no longer used, move its code in here
	// but without the .c_str()
    return ValueForKey(ent, key);
}


bool key_value_is_not_empty(const entity_t* const ent, std::u8string_view key)
{
	return !key_value_is_empty(ent, key);
}
bool key_value_is_empty(const entity_t* const ent, std::u8string_view key)
{
	return value_for_key(ent, key).empty();
}
bool key_value_is(const entity_t* const ent, std::u8string_view key, std::u8string_view value)
{
	return value_for_key(ent, key) == value;
}
bool key_value_starts_with(const entity_t* const ent, std::u8string_view key, std::u8string_view prefix)
{
	return value_for_key(ent, key).starts_with(prefix);
}
bool classname_is(const entity_t* const ent, std::u8string_view classname)
{
	return key_value_is(ent, u8"classname", classname);
}


// =====================================================================================
//  IntForKey
// =====================================================================================
int             IntForKey(const entity_t* const ent, std::u8string_view key)
{
    return atoi((const char*) ValueForKey(ent, key));
}

// =====================================================================================
//  FloatForKey
// =====================================================================================
vec_t           FloatForKey(const entity_t* const ent, std::u8string_view key)
{
    return atof((const char*) ValueForKey(ent, key));
}

// =====================================================================================
//  GetVectorForKey
//      returns value for key in vec[0-2]
// =====================================================================================
void            GetVectorForKey(const entity_t* const ent, std::u8string_view key, vec3_t vec)
{
    double          v1, v2, v3;

    const char* k = (const char*) ValueForKey(ent, key);
    // scanf into doubles, then assign, so it is vec_t size independent
    v1 = v2 = v3 = 0;
    sscanf(k, "%lf %lf %lf", &v1, &v2, &v3);
    vec[0] = v1;
    vec[1] = v2;
    vec[2] = v3;
}

// =====================================================================================
//  FindTargetEntity
//      
// =====================================================================================
entity_t *FindTargetEntity(std::u8string_view target) {
	for(entity_t& ent : std::span(&g_entities[0], g_numentities)) {
        if (key_value_is(&ent, u8"targetname", target)) {
            return &ent;
        }
	}
    return nullptr;
}


void            dtexdata_init()
{
    g_dtexdata = (byte*)AllocBlock(g_max_map_miptex);
    hlassume(g_dtexdata != nullptr, assume_NoMemory);
	g_dlightdata = (byte*)AllocBlock(g_max_map_lightdata);
	hlassume(g_dlightdata != nullptr, assume_NoMemory);
}

void dtexdata_free()
{
    FreeBlock(g_dtexdata);
    g_dtexdata = nullptr;
	FreeBlock(g_dlightdata);
	g_dlightdata = nullptr;
}

// =====================================================================================
//  GetTextureByNumber
//      Touchy function, can fail with a page fault if all the data isnt kosher 
//      (i.e. map was compiled with missing textures)
// =====================================================================================
static char emptystring[1] = {'\0'};
char*           GetTextureByNumber(int texturenumber)
{
	if (texturenumber == -1)
		return emptystring;
    texinfo_t*      info;
    miptex_t*       miptex;
    int             ofs;

    info = &g_texinfo[texturenumber];
    ofs = ((dmiptexlump_t*)g_dtexdata)->dataofs[info->miptex];
    miptex = (miptex_t*)(&g_dtexdata[ofs]);

    return miptex->name;
}

// =====================================================================================
//  EntityForModel
//      returns entity addy for given modelnum
// =====================================================================================
entity_t*       EntityForModel(const int modnum)
{
    char            name[16];

    snprintf(name, sizeof(name), "*%i", modnum);
    // search the entities for one using modnum
    for (int i = 0; i < g_numentities; i++)
    {
        const char* s = (const char*) ValueForKey(&g_entities[i], u8"model");
        if (!strcmp(s, name))
        {
            return &g_entities[i];
        }
    }

    return &g_entities[0];
}