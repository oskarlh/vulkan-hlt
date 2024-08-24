#include "qrad.h"

edgeshare_t     g_edgeshare[MAX_MAP_EDGES];
vec3_t          g_face_centroids[MAX_MAP_EDGES]; // BUG: should this be [MAX_MAP_FACES]?
bool            g_sky_lighting_fix = DEFAULT_SKY_LIGHTING_FIX;

//#define TEXTURE_STEP   16.0

// =====================================================================================
//  PairEdges
// =====================================================================================
typedef struct
{
	int numclipplanes;
	dplane_t *clipplanes;
}
intersecttest_t;
bool TestFaceIntersect (intersecttest_t *t, int facenum)
{
	dface_t *f2 = &g_dfaces[facenum];
	Winding *w = new Winding (*f2);
	int k;
	for (k = 0; k < w->m_NumPoints; k++)
	{
		VectorAdd (w->m_Points[k], g_face_offset[facenum], w->m_Points[k]);
	}
	for (k = 0; k < t->numclipplanes; k++)
	{
		if (!w->Clip (t->clipplanes[k], false
			, ON_EPSILON*4
			))
		{
			break;
		}
	}
	bool intersect = w->m_NumPoints > 0;
	delete w;
	return intersect;
}
intersecttest_t *CreateIntersectTest (const dplane_t *p, int facenum)
{
	dface_t *f = &g_dfaces[facenum];
	intersecttest_t *t;
	t = (intersecttest_t *)malloc (sizeof (intersecttest_t));
	hlassume (t != NULL, assume_NoMemory);
	t->clipplanes = (dplane_t *)malloc (f->numedges * sizeof (dplane_t));
	hlassume (t->clipplanes != NULL, assume_NoMemory);
	t->numclipplanes = 0;
	int j;
	for (j = 0; j < f->numedges; j++)
	{
		// should we use winding instead?
		int edgenum = g_dsurfedges[f->firstedge + j];
		{
			vec3_t v0, v1;
			vec3_t dir, normal;
			if (edgenum < 0)
			{
				VectorCopy (g_dvertexes[g_dedges[-edgenum].v[1]].point, v0);
				VectorCopy (g_dvertexes[g_dedges[-edgenum].v[0]].point, v1);
			}
			else
			{
				VectorCopy (g_dvertexes[g_dedges[edgenum].v[0]].point, v0);
				VectorCopy (g_dvertexes[g_dedges[edgenum].v[1]].point, v1);
			}
			VectorAdd (v0, g_face_offset[facenum], v0);
			VectorAdd (v1, g_face_offset[facenum], v1);
			VectorSubtract (v1, v0, dir);
			CrossProduct (dir, p->normal, normal); // facing inward
			if (!VectorNormalize (normal))
			{
				continue;
			}
			VectorCopy (normal, t->clipplanes[t->numclipplanes].normal);
			t->clipplanes[t->numclipplanes].dist = DotProduct (v0, normal);
			t->numclipplanes++;
		}
	}
	return t;
}
void FreeIntersectTest (intersecttest_t *t)
{
	free (t->clipplanes);
	free (t);
}
void AddFaceForVertexNormal_printerror (const int edgeabs, const int edgeend, dface_t *const f)
{
	if (DEVELOPER_LEVEL_WARNING <= g_developer)
	{
		int i, e;
		Log ("AddFaceForVertexNormal - bad face:\n");
		Log (" edgeabs=%d edgeend=%d\n", edgeabs, edgeend);
		for (i = 0; i < f->numedges; i++)
		{
			e = g_dsurfedges[f->firstedge + i];
			edgeshare_t *es = &g_edgeshare[abs(e)];
			int v0 = g_dedges[abs(e)].v[0], v1 = g_dedges[abs(e)].v[1];
			Log (" e=%d v0=%d(%f,%f,%f) v1=%d(%f,%f,%f) share0=%li share1=%li\n", e,
				v0, g_dvertexes[v0].point[0], g_dvertexes[v0].point[1], g_dvertexes[v0].point[2],
				v1, g_dvertexes[v1].point[0], g_dvertexes[v1].point[1], g_dvertexes[v1].point[2],
				(es->faces[0]==NULL? -1: es->faces[0]-g_dfaces), (es->faces[1]==NULL? -1: es->faces[1]-g_dfaces));
		}
	}
}
int AddFaceForVertexNormal (const int edgeabs, int &edgeabsnext, const int edgeend, int &edgeendnext, dface_t *const f, dface_t *&fnext, vec_t &angle, vec3_t &normal)
// Must guarantee these faces will form a loop or a chain, otherwise will result in endless loop.
//
//   e[end]/enext[endnext]
//  *
//  |\.
//  |a\ fnext
//  |  \,
//  | f \.
//  |    \.
//  e   enext
//
{
	VectorCopy(getPlaneFromFace(f)->normal, normal);
	int vnum = g_dedges[edgeabs].v[edgeend];
	int iedge, iedgenext, edge, edgenext;
	int i, e, count1, count2;
	vec_t dot;
	for (count1 = count2 = 0, i = 0; i < f->numedges; i++)
	{
		e = g_dsurfedges[f->firstedge + i];
		if (g_dedges[abs(e)].v[0] == g_dedges[abs(e)].v[1])
			continue;
		if (abs(e) == edgeabs)
		{
			iedge = i;
			edge = e;
			count1 ++;
		}
		else if (g_dedges[abs(e)].v[0] == vnum || g_dedges[abs(e)].v[1] == vnum)
		{
			iedgenext = i;
			edgenext = e;
			count2 ++;
		}
	}
	if (count1 != 1 || count2 != 1)
	{
		AddFaceForVertexNormal_printerror (edgeabs, edgeend, f);
		return -1;
	}
	int vnum11, vnum12, vnum21, vnum22;
	vec3_t vec1, vec2;
	vnum11 = g_dedges[abs(edge)].v[edge>0?0:1];
	vnum12 = g_dedges[abs(edge)].v[edge>0?1:0];
	vnum21 = g_dedges[abs(edgenext)].v[edgenext>0?0:1];
	vnum22 = g_dedges[abs(edgenext)].v[edgenext>0?1:0];
	if (vnum == vnum12 && vnum == vnum21 && vnum != vnum11 && vnum != vnum22)
	{
		VectorSubtract(g_dvertexes[vnum11].point, g_dvertexes[vnum].point, vec1);
		VectorSubtract(g_dvertexes[vnum22].point, g_dvertexes[vnum].point, vec2);
		edgeabsnext = abs(edgenext);
		edgeendnext = edgenext>0?0:1;
	}
	else if (vnum == vnum11 && vnum == vnum22 && vnum != vnum12 && vnum != vnum21)
	{
		VectorSubtract(g_dvertexes[vnum12].point, g_dvertexes[vnum].point, vec1);
		VectorSubtract(g_dvertexes[vnum21].point, g_dvertexes[vnum].point, vec2);
		edgeabsnext = abs(edgenext);
		edgeendnext = edgenext>0?1:0;
	}
	else
	{
		AddFaceForVertexNormal_printerror (edgeabs, edgeend, f);
		return -1;
	}
	VectorNormalize(vec1);
	VectorNormalize(vec2);
	dot = DotProduct(vec1,vec2);
	dot = dot>1? 1: dot<-1? -1: dot;
	angle = acos(dot);
	edgeshare_t *es = &g_edgeshare[edgeabsnext];
	if (!(es->faces[0] && es->faces[1]))
		return 1;
	if (es->faces[0] == f && es->faces[1] != f)
		fnext = es->faces[1];
	else if (es->faces[1] == f && es->faces[0] != f)
		fnext = es->faces[0];
	else
	{
		AddFaceForVertexNormal_printerror (edgeabs, edgeend, f);
		return -1;
	}
	return 0;
}

static bool TranslateTexToTex (int facenum, int edgenum, int facenum2, matrix_t &m, matrix_t &m_inverse)
	// This function creates a matrix that can translate texture coords in face1 into texture coords in face2.
	// It keeps all points in the common edge invariant. For example, if there is a point in the edge, and in the texture of face1, its (s,t)=(16,0), and in face2, its (s,t)=(128,64), then we must let matrix*(16,0,0)=(128,64,0)
{
	matrix_t worldtotex;
	matrix_t worldtotex2;
	dedge_t *e;
	int i;
	dvertex_t *vert[2];
	vec3_t face_vert[2];
	vec3_t face2_vert[2];
	vec3_t face_axis[2];
	vec3_t face2_axis[2];
	const vec3_t v_up = {0, 0, 1};
	vec_t len;
	vec_t len2;
	matrix_t edgetotex, edgetotex2;
	matrix_t inv, inv2;

	TranslateWorldToTex (facenum, worldtotex);
	TranslateWorldToTex (facenum2, worldtotex2);

	e = &g_dedges[edgenum];
	for (i = 0; i < 2; i++)
	{
		vert[i] = &g_dvertexes[e->v[i]];
		ApplyMatrix (worldtotex, vert[i]->point, face_vert[i]);
		face_vert[i][2] = 0; // this value is naturally close to 0 assuming that the edge is on the face plane, but let's make this more explicit.
		ApplyMatrix (worldtotex2, vert[i]->point, face2_vert[i]);
		face2_vert[i][2] = 0;
	}

	VectorSubtract (face_vert[1], face_vert[0], face_axis[0]);
	len = VectorLength (face_axis[0]);
	CrossProduct (v_up, face_axis[0], face_axis[1]);
	if (CalcMatrixSign (worldtotex) < 0.0) // the three vectors s, t, facenormal are in reverse order
	{
		VectorInverse (face_axis[1]);
	}

	VectorSubtract (face2_vert[1], face2_vert[0], face2_axis[0]);
	len2 = VectorLength (face2_axis[0]);
	CrossProduct (v_up, face2_axis[0], face2_axis[1]);
	if (CalcMatrixSign (worldtotex2) < 0.0)
	{
		VectorInverse (face2_axis[1]);
	}

	VectorCopy (face_axis[0], edgetotex.v[0]); // / v[0][0] v[1][0] \ is a rotation (possibly with a reflection by the edge)
	VectorCopy (face_axis[1], edgetotex.v[1]); // \ v[0][1] v[1][1] / 
	VectorScale (v_up, len, edgetotex.v[2]); // encode the length into the 3rd value of the matrix
	VectorCopy (face_vert[0], edgetotex.v[3]); // map (0,0) into the origin point

	VectorCopy (face2_axis[0], edgetotex2.v[0]);
	VectorCopy (face2_axis[1], edgetotex2.v[1]);
	VectorScale (v_up, len2, edgetotex2.v[2]);
	VectorCopy (face2_vert[0], edgetotex2.v[3]);

	if (!InvertMatrix (edgetotex, inv) || !InvertMatrix (edgetotex2, inv2))
	{
		return false;
	}
	MultiplyMatrix (edgetotex2, inv, m);
	MultiplyMatrix (edgetotex, inv2, m_inverse);

	return true;
}

void            PairEdges()
{
    int             i, j, k;
    dface_t*        f;
    edgeshare_t*    e;

    memset(&g_edgeshare, 0, sizeof(g_edgeshare));

    f = g_dfaces;
    for (i = 0; i < g_numfaces; i++, f++)
    {
		if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
		{
			// special textures don't have lightmaps
			continue;
		}
        for (j = 0; j < f->numedges; j++)
        {
            k = g_dsurfedges[f->firstedge + j];
            if (k < 0)
            {
                e = &g_edgeshare[-k];

                hlassert(e->faces[1] == NULL);
                e->faces[1] = f;
            }
            else
            {
                e = &g_edgeshare[k];

                hlassert(e->faces[0] == NULL);
                e->faces[0] = f;
            }

            if (e->faces[0] && e->faces[1]) {
				// determine if coplanar
				if (e->faces[0]->planenum == e->faces[1]->planenum
					&& e->faces[0]->side == e->faces[1]->side
					) {
						e->coplanar = true;
						VectorCopy(getPlaneFromFace(e->faces[0])->normal, e->interface_normal);
						e->cos_normals_angle = 1.0;
				} else {
                    // see if they fall into a "smoothing group" based on angle of the normals
                    vec3_t          normals[2];

                    VectorCopy(getPlaneFromFace(e->faces[0])->normal, normals[0]);
                    VectorCopy(getPlaneFromFace(e->faces[1])->normal, normals[1]);

                    e->cos_normals_angle = DotProduct(normals[0], normals[1]);

					vec_t smoothvalue;
					int m0 = g_texinfo[e->faces[0]->texinfo].miptex;
					int m1 = g_texinfo[e->faces[1]->texinfo].miptex;
					smoothvalue = qmax (g_smoothvalues[m0], g_smoothvalues[m1]);
					if (m0 != m1)
					{
						smoothvalue = qmax (smoothvalue, g_smoothing_threshold_2);
					}
					if (smoothvalue >= 1.0 - NORMAL_EPSILON)
					{
						smoothvalue = 2.0;
					}
                    if (e->cos_normals_angle > (1.0 - NORMAL_EPSILON))
                    {
                        e->coplanar = true;
						VectorCopy(getPlaneFromFace(e->faces[0])->normal, e->interface_normal);
						e->cos_normals_angle = 1.0;
                    }
                    else if (e->cos_normals_angle >= qmax (smoothvalue - NORMAL_EPSILON, NORMAL_EPSILON))
					{
                        {
                            VectorAdd(normals[0], normals[1], e->interface_normal);
                            VectorNormalize(e->interface_normal);
                        }
                    }
                }
				if (!VectorCompare (g_translucenttextures[g_texinfo[e->faces[0]->texinfo].miptex], g_translucenttextures[g_texinfo[e->faces[1]->texinfo].miptex]))
				{
					e->coplanar = false;
					VectorClear (e->interface_normal);
				}
				{
					int miptex0, miptex1;
					miptex0 = g_texinfo[e->faces[0]->texinfo].miptex;
					miptex1 = g_texinfo[e->faces[1]->texinfo].miptex;
					if (fabs (g_lightingconeinfo[miptex0][0] - g_lightingconeinfo[miptex1][0]) > NORMAL_EPSILON ||
						fabs (g_lightingconeinfo[miptex0][1] - g_lightingconeinfo[miptex1][1]) > NORMAL_EPSILON )
					{
						e->coplanar = false;
						VectorClear (e->interface_normal);
					}
				}
				if (!VectorCompare(e->interface_normal, vec3_origin))
				{
					e->smooth = true;
				}
				if (e->smooth)
				{
					// compute the matrix in advance
					if (!TranslateTexToTex (e->faces[0] - g_dfaces, abs (k), e->faces[1] - g_dfaces, e->textotex[0], e->textotex[1]))
					{
						e->smooth = false;
						e->coplanar = false;
						VectorClear (e->interface_normal);

						dvertex_t *dv = &g_dvertexes[g_dedges[abs(k)].v[0]];
						Developer (DEVELOPER_LEVEL_MEGASPAM, "TranslateTexToTex failed on face %d and %d @(%f,%f,%f)", (int)(e->faces[0] - g_dfaces), (int)(e->faces[1] - g_dfaces), dv->point[0], dv->point[1], dv->point[2]);
					}
				}
            }
        }
    }
	{
		int edgeabs, edgeabsnext;
		int edgeend, edgeendnext;
		int d;
		dface_t *f, *fcurrent, *fnext;
		vec_t angle, angles;
		vec3_t normal, normals;
		vec3_t edgenormal;
		int r, count;
		for (edgeabs = 0; edgeabs < MAX_MAP_EDGES; edgeabs++)
		{
			e = &g_edgeshare[edgeabs];
			if (!e->smooth)
				continue;
			VectorCopy(e->interface_normal, edgenormal);
			if (g_dedges[edgeabs].v[0] == g_dedges[edgeabs].v[1])
			{
				vec3_t errorpos;
				VectorCopy (g_dvertexes[g_dedges[edgeabs].v[0]].point, errorpos);
				VectorAdd (errorpos, g_face_offset[e->faces[0] - g_dfaces], errorpos);
				Developer (DEVELOPER_LEVEL_WARNING, "PairEdges: invalid edge at (%f,%f,%f)", errorpos[0], errorpos[1], errorpos[2]);
				VectorCopy(edgenormal, e->vertex_normal[0]);
				VectorCopy(edgenormal, e->vertex_normal[1]);
			}
			else
			{
				const dplane_t *p0 = getPlaneFromFace (e->faces[0]);
				const dplane_t *p1 = getPlaneFromFace (e->faces[1]);
				intersecttest_t *test0 = CreateIntersectTest (p0, e->faces[0] - g_dfaces);
				intersecttest_t *test1 = CreateIntersectTest (p1, e->faces[1] - g_dfaces);
				for (edgeend = 0; edgeend < 2; edgeend++)
				{
					vec3_t errorpos;
					VectorCopy (g_dvertexes[g_dedges[edgeabs].v[edgeend]].point, errorpos);
					VectorAdd (errorpos, g_face_offset[e->faces[0] - g_dfaces], errorpos);
					angles = 0;
					VectorClear (normals);

					for (d = 0; d < 2; d++)
					{
						f = e->faces[d];
						count = 0, fnext = f, edgeabsnext = edgeabs, edgeendnext = edgeend;
						while (1)
						{
							fcurrent = fnext;
							r = AddFaceForVertexNormal (edgeabsnext, edgeabsnext, edgeendnext, edgeendnext, fcurrent, fnext, angle, normal);
							count++;
							if (r == -1)
							{
								Developer (DEVELOPER_LEVEL_WARNING, "PairEdges: face edges mislink at (%f,%f,%f)", errorpos[0], errorpos[1], errorpos[2]);
								break;
							}
							if (count >= 100)
							{
								Developer (DEVELOPER_LEVEL_WARNING, "PairEdges: faces mislink at (%f,%f,%f)", errorpos[0], errorpos[1], errorpos[2]);
								break;
							}
							if (DotProduct (normal, p0->normal) <= NORMAL_EPSILON || DotProduct(normal, p1->normal) <= NORMAL_EPSILON)
								break;
							vec_t smoothvalue;
							int m0 = g_texinfo[f->texinfo].miptex;
							int m1 = g_texinfo[fcurrent->texinfo].miptex;
							smoothvalue = qmax (g_smoothvalues[m0], g_smoothvalues[m1]);
							if (m0 != m1)
							{
								smoothvalue = qmax (smoothvalue, g_smoothing_threshold_2);
							}
							if (smoothvalue >= 1.0 - NORMAL_EPSILON)
							{
								smoothvalue = 2.0;
							}
							if (DotProduct (edgenormal, normal) < qmax (smoothvalue - NORMAL_EPSILON, NORMAL_EPSILON))
								break;
							if (fcurrent != e->faces[0] && fcurrent != e->faces[1] &&
								(TestFaceIntersect (test0, fcurrent - g_dfaces) || TestFaceIntersect (test1, fcurrent - g_dfaces)))
							{
								Developer (DEVELOPER_LEVEL_WARNING, "Overlapping faces around corner (%f,%f,%f)\n", errorpos[0], errorpos[1], errorpos[2]);
								break;
							}
							angles += angle;
							VectorMA(normals, angle, normal, normals);
							{
								bool in = false;
								if (fcurrent == e->faces[0] || fcurrent == e->faces[1])
								{
									in = true;
								}
								for (facelist_t *l = e->vertex_facelist[edgeend]; l; l = l->next)
								{
									if (fcurrent == l->face)
									{
										in = true;
									}
								}
								if (!in)
								{
									facelist_t *l = (facelist_t *)malloc (sizeof (facelist_t));
									hlassume (l != NULL, assume_NoMemory);
									l->face = fcurrent;
									l->next = e->vertex_facelist[edgeend];
									e->vertex_facelist[edgeend] = l;
								}
							}
							if (r != 0 || fnext == f)
								break;
						}
					}

					if (angles < NORMAL_EPSILON)
					{
						VectorCopy(edgenormal, e->vertex_normal[edgeend]);
						Developer (DEVELOPER_LEVEL_WARNING, "PairEdges: no valid faces at (%f,%f,%f)", errorpos[0], errorpos[1], errorpos[2]);
					}
					else
					{
						VectorNormalize(normals);
						VectorCopy(normals, e->vertex_normal[edgeend]);
					}
				}
				FreeIntersectTest (test0);
				FreeIntersectTest (test1);
			}
			if (e->coplanar)
			{
				if (!VectorCompare (e->vertex_normal[0], e->interface_normal) || !VectorCompare (e->vertex_normal[1], e->interface_normal))
				{
					e->coplanar = false;
				}
			}
		}
	}
}

#define MAX_SINGLEMAP ((MAX_SURFACE_EXTENT+1)*(MAX_SURFACE_EXTENT+1)) //#define	MAX_SINGLEMAP	(18*18*4) //--vluzacn

typedef enum
{
	WALLFLAG_NONE = 0,
	WALLFLAG_NUDGED = 0x1,
	WALLFLAG_BLOCKED = 0x2, // this only happens when the entire face and its surroundings are covered by solid or opaque entities
	WALLFLAG_SHADOWED = 0x4,
}
wallflag_t;

typedef struct
{
    vec_t*          light;
    vec_t           facedist;
    vec3_t          facenormal;
	bool			translucent_b;
	vec3_t			translucent_v;
	int				miptex;

    int             numsurfpt;
    vec3_t          surfpt[MAX_SINGLEMAP];
	vec3_t*			surfpt_position; //[MAX_SINGLEMAP] // surfpt_position[] are valid positions for light tracing, while surfpt[] are positions for getting phong normal and doing patch interpolation
	int*			surfpt_surface; //[MAX_SINGLEMAP] // the face that owns this position
	bool			surfpt_lightoutside[MAX_SINGLEMAP];

    vec3_t          texorg;
    vec3_t          worldtotex[2];                         // s = (world - texorg) . worldtotex[0]
    vec3_t          textoworld[2];                         // world = texorg + s * textoworld[0]
	vec3_t			texnormal;

    vec_t           exactmins[2], exactmaxs[2];

    int             texmins[2], texsize[2];
    int             lightstyles[256];
    int             surfnum;
    dface_t*        face;
	int				lmcache_density; // shared by both s and t direction
	int				lmcache_offset; // shared by both s and t direction
	int				lmcache_side;
	vec3_t			(*lmcache)[ALLSTYLES]; // lm: short for lightmap // don't forget to free!
	vec3_t			*lmcache_normal; // record the phong normals
	int				*lmcache_wallflags; // wallflag_t
	int				lmcachewidth;
	int				lmcacheheight;
}
lightinfo_t;

// =====================================================================================
//  TextureNameFromFace
// =====================================================================================
static const char* TextureNameFromFace(const dface_t* const f)
{
    texinfo_t*      tx;
    miptex_t*       mt;
    int             ofs;

    //
    // check for light emited by texture
    //
    tx = &g_texinfo[f->texinfo];

    ofs = ((dmiptexlump_t*)g_dtexdata)->dataofs[tx->miptex];
    mt = (miptex_t*)((byte*) g_dtexdata + ofs);

	return mt->name;
}

// =====================================================================================
//  CalcFaceExtents
//      Fills in s->texmins[] and s->texsize[]
//      also sets exactmins[] and exactmaxs[]
// =====================================================================================
static void     CalcFaceExtents(lightinfo_t* l)
{
    const int       facenum = l->surfnum;
    dface_t*        s;
    float           mins[2], maxs[2], val; //vec_t           mins[2], maxs[2], val; //vluzacn
    int             i, j, e;
    dvertex_t*      v;
    texinfo_t*      tex;

    s = l->face;

	mins[0] = mins[1] = 99999999;
	maxs[0] = maxs[1] = -99999999;

    tex = &g_texinfo[s->texinfo];

    for (i = 0; i < s->numedges; i++)
    {
        e = g_dsurfedges[s->firstedge + i];
        if (e >= 0)
        {
            v = g_dvertexes + g_dedges[e].v[0];
        }
        else
        {
            v = g_dvertexes + g_dedges[-e].v[1];
        }

        for (j = 0; j < 2; j++)
        {
            val = v->point[0] * tex->vecs[j][0] +
                v->point[1] * tex->vecs[j][1] + v->point[2] * tex->vecs[j][2] + tex->vecs[j][3];
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
        l->exactmins[i] = mins[i];
        l->exactmaxs[i] = maxs[i];

	}
	int bmins[2];
	int bmaxs[2];
	GetFaceExtents (l->surfnum, bmins, bmaxs);
	for (i = 0; i < 2; i++)
	{
		mins[i] = bmins[i];
		maxs[i] = bmaxs[i];
		l->texmins[i] = bmins[i];
		l->texsize[i] = bmaxs[i] - bmins[i];
	}

	if (!(tex->flags & TEX_SPECIAL))
	{
		if ((l->texsize[0] > MAX_SURFACE_EXTENT) || (l->texsize[1] > MAX_SURFACE_EXTENT)
			|| l->texsize[0] < 0 || l->texsize[1] < 0 //--vluzacn
			)
		{
			ThreadLock();
			PrintOnce("\nfor Face %li (texture %s) at ", s - g_dfaces, TextureNameFromFace(s));

			for (i = 0; i < s->numedges; i++)
			{
				e = g_dsurfedges[s->firstedge + i];
				if (e >= 0)
                {
					v = g_dvertexes + g_dedges[e].v[0];
                }
				else
                {
					v = g_dvertexes + g_dedges[-e].v[1];
                }
				vec3_t pos;
				VectorAdd (v->point, g_face_offset[facenum], pos);
				Log ("(%4.3f %4.3f %4.3f) ", pos[0], pos[1], pos[2]);
			}
			Log("\n");

			Error( "Bad surface extents (%d x %d)\nCheck the file ZHLTProblems.html for a detailed explanation of this problem", l->texsize[0], l->texsize[1]);
		}
	}
	// allocate sample light cache
	{
		if (g_extra
			&& !g_fastmode
			)
		{
			l->lmcache_density = 3;
		}
		else
		{
			l->lmcache_density = 1;
		}
		l->lmcache_side = (int)ceil ((0.5 * g_blur * l->lmcache_density - 0.5) * (1 - NORMAL_EPSILON));
		l->lmcache_offset = l->lmcache_side;
		l->lmcachewidth = l->texsize[0] * l->lmcache_density + 1 + 2 * l->lmcache_side;
		l->lmcacheheight = l->texsize[1] * l->lmcache_density + 1 + 2 * l->lmcache_side;
		l->lmcache = (vec3_t (*)[ALLSTYLES])malloc (l->lmcachewidth * l->lmcacheheight * sizeof (vec3_t [ALLSTYLES]));
		hlassume (l->lmcache != NULL, assume_NoMemory);
		l->lmcache_normal = (vec3_t *)malloc (l->lmcachewidth * l->lmcacheheight * sizeof (vec3_t));
		hlassume (l->lmcache_normal != NULL, assume_NoMemory);
		l->lmcache_wallflags = (int *)malloc (l->lmcachewidth * l->lmcacheheight * sizeof (int));
		hlassume (l->lmcache_wallflags != NULL, assume_NoMemory);
		l->surfpt_position = (vec3_t *)malloc (MAX_SINGLEMAP * sizeof (vec3_t));
		l->surfpt_surface = (int *)malloc (MAX_SINGLEMAP * sizeof (int));
		hlassume (l->surfpt_position != NULL && l->surfpt_surface != NULL, assume_NoMemory);
	}
}

// =====================================================================================
//  CalcFaceVectors
//      Fills in texorg, worldtotex. and textoworld
// =====================================================================================
static void     CalcFaceVectors(lightinfo_t* l)
{
    texinfo_t*      tex;
    int             i, j;
    vec3_t          texnormal;
    vec_t           distscale;
    vec_t           dist, len;

    tex = &g_texinfo[l->face->texinfo];

    // convert from float to double
    for (i = 0; i < 2; i++)
    {
        for (j = 0; j < 3; j++)
        {
            l->worldtotex[i][j] = tex->vecs[i][j];
        }
    }

    // calculate a normal to the texture axis.  points can be moved along this
    // without changing their S/T
    CrossProduct(tex->vecs[1], tex->vecs[0], texnormal);
    VectorNormalize(texnormal);

    // flip it towards plane normal
    distscale = DotProduct(texnormal, l->facenormal);
    if (distscale == 0.0)
    {
        const unsigned facenum = l->face - g_dfaces;
    
        ThreadLock();
        Log("Malformed face (%d) normal @ \n", facenum);
        Winding* w = new Winding(*l->face);
        {
            const unsigned numpoints = w->m_NumPoints;
            unsigned x;
            for (x=0; x<numpoints; x++)
            {
                VectorAdd(w->m_Points[x], g_face_offset[facenum], w->m_Points[x]);
            }
        }
        w->Print();
        delete w;
        ThreadUnlock();

        hlassume(false, assume_MalformedTextureFace);
    }

    if (distscale < 0)
    {
        distscale = -distscale;
        VectorSubtract(vec3_origin, texnormal, texnormal);
    }

    // distscale is the ratio of the distance along the texture normal to
    // the distance along the plane normal
    distscale = 1.0 / distscale;

	for (i = 0; i < 2; i++)
	{
		CrossProduct (l->worldtotex[!i], l->facenormal, l->textoworld[i]);
		len = DotProduct (l->textoworld[i], l->worldtotex[i]);
		VectorScale (l->textoworld[i], 1 / len, l->textoworld[i]);
	}

    // calculate texorg on the texture plane
    for (i = 0; i < 3; i++)
    {
        l->texorg[i] = -tex->vecs[0][3] * l->textoworld[0][i] - tex->vecs[1][3] * l->textoworld[1][i];
    }

    // project back to the face plane
    dist = DotProduct(l->texorg, l->facenormal) - l->facedist;
    dist *= distscale;
    VectorMA(l->texorg, -dist, texnormal, l->texorg);
	VectorCopy (texnormal, l->texnormal);

}

// =====================================================================================
//  SetSurfFromST
// =====================================================================================
static void     SetSurfFromST(const lightinfo_t* const l, vec_t* surf, const vec_t s, const vec_t t)
{
    const int       facenum = l->surfnum;
    int             j;

    for (j = 0; j < 3; j++)
    {
        surf[j] = l->texorg[j] + l->textoworld[0][j] * s + l->textoworld[1][j] * t;
    }

    // Adjust for origin-based models
    VectorAdd(surf, g_face_offset[facenum], surf);
}


typedef enum
{
    LightOutside,                                          // Not lit
    LightShifted,                                          // used HuntForWorld on 100% dark face
    LightShiftedInside,                                    // moved to neighbhor on 2nd cleanup pass
    LightNormal,                                           // Normally lit with no movement
    LightPulledInside,                                     // Pulled inside by bleed code adjustments
    LightSimpleNudge,                                      // A simple nudge 1/3 or 2/3 towards center along S or T axist
}
light_flag_t;

// =====================================================================================
//  CalcPoints
//      For each texture aligned grid point, back project onto the plane
//      to get the world xyz value of the sample point
// =====================================================================================
static void		SetSTFromSurf(const lightinfo_t* const l, const vec_t* surf, vec_t& s, vec_t& t)
{
    const int       facenum = l->surfnum;
    int             j;

	s = t = 0;
	for (j = 0; j < 3; j++)
	{
		s += (surf[j] - g_face_offset[facenum][j] - l->texorg[j]) * l->worldtotex[0][j];
		t += (surf[j] - g_face_offset[facenum][j] - l->texorg[j]) * l->worldtotex[1][j];
	}
}

typedef struct
{
	int edgenum; // g_dedges index
	int edgeside;
	int nextfacenum; // where to grow
	bool tried;

	vec3_t point1; // start point
	vec3_t point2; // end point
	vec3_t direction; // normalized; from point1 to point2
	
	bool noseam;
	vec_t distance; // distance from origin
	vec_t distancereduction;
	vec_t flippedangle;
	
	vec_t ratio; // if ratio != 1, seam is unavoidable
	matrix_t prevtonext;
	matrix_t nexttoprev;
}
samplefragedge_t;

typedef struct
{
	dplane_t planes[4];
}
samplefragrect_t;

typedef struct samplefrag_s
{
	samplefrag_s *next; // since this is a node in a list
	samplefrag_s *parentfrag; // where it grew from
	samplefragedge_t *parentedge;
	int facenum; // facenum

	vec_t flippedangle; // copied from parent edge
	bool noseam; // copied from parent edge

	matrix_t coordtomycoord; // v[2][2] > 0, v[2][0] = v[2][1] = v[0][2] = v[1][2] = 0.0
	matrix_t mycoordtocoord;

	vec3_t origin; // original s,t
	vec3_t myorigin; // relative to the texture coordinate on that face
	samplefragrect_t rect; // original rectangle that forms the boundary
	samplefragrect_t myrect; // relative to the texture coordinate on that face

	Winding *winding; // a fragment of the original rectangle in the texture coordinate plane; windings of different frags should not overlap
	dplane_t windingplane; // normal = (0,0,1) or (0,0,-1); if this normal is wrong, point_in_winding() will never return true
	Winding *mywinding; // relative to the texture coordinate on that face
	dplane_t mywindingplane;

	int numedges; // # of candicates for the next growth
	samplefragedge_t *edges; // candicates for the next growth
}
samplefrag_t;

typedef struct
{
	int maxsize;
	int size;
	samplefrag_t *head;
}
samplefraginfo_t;

void ChopFrag (samplefrag_t *frag)
	// fill winding, windingplane, mywinding, mywindingplane, numedges, edges
{
	// get the shape of the fragment by clipping the face using the boundaries
	dface_t *f;
	Winding *facewinding;
	matrix_t worldtotex;
	const vec3_t v_up = {0, 0, 1};

	f = &g_dfaces[frag->facenum];
	facewinding = new Winding (*f);

	TranslateWorldToTex (frag->facenum, worldtotex);
	frag->mywinding = new Winding (facewinding->m_NumPoints);
	for (int x = 0; x < facewinding->m_NumPoints; x++)
	{
		ApplyMatrix (worldtotex, facewinding->m_Points[x], frag->mywinding->m_Points[x]);
		frag->mywinding->m_Points[x][2] = 0.0;
	}
	frag->mywinding->RemoveColinearPoints ();
	VectorCopy (v_up, frag->mywindingplane.normal); // this is the same as applying the worldtotex matrix to the faceplane
	if (CalcMatrixSign (worldtotex) < 0.0)
	{
		frag->mywindingplane.normal[2] *= -1;
	}
	frag->mywindingplane.dist = 0.0;

	for (int x = 0; x < 4 && frag->mywinding->m_NumPoints > 0; x++)
	{
		frag->mywinding->Clip (frag->myrect.planes[x], false);
	}

	frag->winding = new Winding (frag->mywinding->m_NumPoints);
	for (int x = 0; x < frag->mywinding->m_NumPoints; x++)
	{
		ApplyMatrix (frag->mycoordtocoord, frag->mywinding->m_Points[x], frag->winding->m_Points[x]);
	}
	frag->winding->RemoveColinearPoints ();
	VectorCopy (frag->mywindingplane.normal, frag->windingplane.normal);
	if (CalcMatrixSign (frag->mycoordtocoord) < 0.0)
	{
		frag->windingplane.normal[2] *= -1;
	}
	frag->windingplane.dist = 0.0;

	delete facewinding;

	// find the edges where the fragment can grow in the future
	frag->numedges = 0;
	frag->edges = (samplefragedge_t *)malloc (f->numedges * sizeof (samplefragedge_t));
	hlassume (frag->edges != NULL, assume_NoMemory);
	for (int i = 0; i < f->numedges; i++)
	{
		samplefragedge_t *e;
		edgeshare_t *es;
		dedge_t *de;
		dvertex_t *dv1;
		dvertex_t *dv2;
		vec_t frac1, frac2;
		vec_t edgelen;
		vec_t dot, dot1, dot2;
		vec3_t tmp, v, normal;
		const matrix_t *m;
		const matrix_t *m_inverse;
		
		e = &frag->edges[frag->numedges];

		// some basic info
		e->edgenum = abs (g_dsurfedges[f->firstedge + i]);
		e->edgeside = (g_dsurfedges[f->firstedge + i] < 0? 1: 0);
		es = &g_edgeshare[e->edgenum];
		if (!es->smooth)
		{
			continue;
		}
		if (es->faces[e->edgeside] - g_dfaces != frag->facenum)
		{
			Error ("internal error 1 in GrowSingleSampleFrag");
		}
		m = &es->textotex[e->edgeside];
		m_inverse = &es->textotex[1-e->edgeside];
		e->nextfacenum = es->faces[1-e->edgeside] - g_dfaces;
		if (e->nextfacenum == frag->facenum)
		{
			continue; // an invalid edge (usually very short)
		}
		e->tried = false; // because the frag hasn't been linked into the list yet

		// translate the edge points from world to the texture plane of the original frag
		//   so the distances are able to be compared among edges from different frags
		de = &g_dedges[e->edgenum];
		dv1 = &g_dvertexes[de->v[e->edgeside]];
		dv2 = &g_dvertexes[de->v[1-e->edgeside]];
		ApplyMatrix (worldtotex, dv1->point, tmp);
		ApplyMatrix (frag->mycoordtocoord, tmp, e->point1);
		e->point1[2] = 0.0;
		ApplyMatrix (worldtotex, dv2->point, tmp);
		ApplyMatrix (frag->mycoordtocoord, tmp, e->point2);
		e->point2[2] = 0.0;
		VectorSubtract (e->point2, e->point1, e->direction);
		edgelen = VectorNormalize (e->direction);
		if (edgelen <= ON_EPSILON)
		{
			continue;
		}

		// clip the edge
		frac1 = 0;
		frac2 = 1;
		for (int x = 0; x < 4; x++)
		{
			vec_t dot1;
			vec_t dot2;

			dot1 = DotProduct (e->point1, frag->rect.planes[x].normal) - frag->rect.planes[x].dist;
			dot2 = DotProduct (e->point2, frag->rect.planes[x].normal) - frag->rect.planes[x].dist;
			if (dot1 <= ON_EPSILON && dot2 <= ON_EPSILON)
			{
				frac1 = 1;
				frac2 = 0;
			}
			else if (dot1 < 0)
			{
				frac1 = qmax (frac1, dot1 / (dot1 - dot2));
			}
			else if (dot2 < 0)
			{
				frac2 = qmin (frac2, dot1 / (dot1 - dot2));
			}
		}
		if (edgelen * (frac2 - frac1) <= ON_EPSILON)
		{
			continue;
		}
		VectorMA (e->point1, edgelen * frac2, e->direction, e->point2);
		VectorMA (e->point1, edgelen * frac1, e->direction, e->point1);

		// calculate the distance, etc., which are used to determine its priority
		e->noseam = frag->noseam;
		dot = DotProduct (frag->origin, e->direction);
		dot1 = DotProduct (e->point1, e->direction);
		dot2 = DotProduct (e->point2, e->direction);
		dot = qmax (dot1, qmin (dot, dot2));
		VectorMA (e->point1, dot - dot1, e->direction, v);
		VectorSubtract (v, frag->origin, v);
		e->distance = VectorLength (v);
		CrossProduct (e->direction, frag->windingplane.normal, normal);
		VectorNormalize (normal); // points inward
		e->distancereduction = DotProduct (v, normal);
		e->flippedangle = frag->flippedangle + acos (qmin (es->cos_normals_angle, 1.0));

		// calculate the matrix
		e->ratio = (*m_inverse).v[2][2];
		if (e->ratio <= NORMAL_EPSILON || (1 / e->ratio) <= NORMAL_EPSILON)
		{
			Developer (DEVELOPER_LEVEL_SPAM, "TranslateTexToTex failed on face %d and %d @(%f,%f,%f)", frag->facenum, e->nextfacenum, dv1->point[0], dv1->point[1], dv1->point[2]);
			continue;
		}

		if (fabs (e->ratio - 1) < 0.005)
		{
			e->prevtonext = *m;
			e->nexttoprev = *m_inverse;
		}
		else
		{
			e->noseam = false;
			e->prevtonext = *m;
			e->nexttoprev = *m_inverse;
		}

		frag->numedges++;
	}
}

static samplefrag_t *GrowSingleFrag (const samplefraginfo_t *info, samplefrag_t *parent, samplefragedge_t *edge)
{
	samplefrag_t *frag;
	bool overlap;
	int numclipplanes;
	dplane_t *clipplanes;

	frag = (samplefrag_t *)malloc (sizeof (samplefrag_t));
	hlassume (frag != NULL, assume_NoMemory);

	// some basic info
	frag->next = NULL;
	frag->parentfrag = parent;
	frag->parentedge = edge;
	frag->facenum = edge->nextfacenum;

	frag->flippedangle = edge->flippedangle;
	frag->noseam = edge->noseam;

	// calculate the matrix
	MultiplyMatrix (edge->prevtonext, parent->coordtomycoord, frag->coordtomycoord);
	MultiplyMatrix (parent->mycoordtocoord, edge->nexttoprev, frag->mycoordtocoord);

	// fill in origin
	VectorCopy (parent->origin, frag->origin);
	ApplyMatrix (frag->coordtomycoord, frag->origin, frag->myorigin);

	// fill in boundaries
	frag->rect = parent->rect;
	for (int x = 0; x < 4; x++)
	{
		// since a plane's parameters are in the dual coordinate space, we translate the original absolute plane into this relative plane by multiplying the inverse matrix
		ApplyMatrixOnPlane (frag->mycoordtocoord, frag->rect.planes[x].normal, frag->rect.planes[x].dist, frag->myrect.planes[x].normal, frag->myrect.planes[x].dist);
		double len = VectorLength (frag->myrect.planes[x].normal);
		if (!len)
		{
			Developer (DEVELOPER_LEVEL_MEGASPAM, "couldn't translate sample boundaries on face %d", frag->facenum);
			free (frag);
			return NULL;
		}
		VectorScale (frag->myrect.planes[x].normal, 1 / len, frag->myrect.planes[x].normal);
		frag->myrect.planes[x].dist /= len;
	}

	// chop windings and edges
	ChopFrag (frag);

	if (frag->winding->m_NumPoints == 0 || frag->mywinding->m_NumPoints == 0)
	{
		// empty
		delete frag->mywinding;
		delete frag->winding;
		free (frag->edges);
		free (frag);
		return NULL;
	}

	// do overlap test
	
	overlap = false;
	clipplanes = (dplane_t *)malloc (frag->winding->m_NumPoints * sizeof (dplane_t));
	hlassume (clipplanes != NULL, assume_NoMemory);
	numclipplanes = 0;
	for (int x = 0; x < frag->winding->m_NumPoints; x++)
	{
		vec3_t v;
		VectorSubtract (frag->winding->m_Points[(x + 1) % frag->winding->m_NumPoints], frag->winding->m_Points[x], v);
		CrossProduct (v, frag->windingplane.normal, clipplanes[numclipplanes].normal);
		if (!VectorNormalize (clipplanes[numclipplanes].normal))
		{
			continue;
		}
		clipplanes[numclipplanes].dist = DotProduct (frag->winding->m_Points[x], clipplanes[numclipplanes].normal);
		numclipplanes++;
	}
	for (samplefrag_t *f2 = info->head; f2 && !overlap; f2 = f2->next)
	{
		Winding *w = new Winding (*f2->winding);
		for (int x = 0; x < numclipplanes && w->m_NumPoints > 0; x++)
		{
			w->Clip (clipplanes[x], false
					, 4 * ON_EPSILON
					);
		}
		if (w->m_NumPoints > 0)
		{
			overlap = true;
		}
		delete w;
	}
	free (clipplanes);
	if (overlap)
	{
		// in the original texture plane, this fragment overlaps with some existing fragments
		delete frag->mywinding;
		delete frag->winding;
		free (frag->edges);
		free (frag);
		return NULL;
	}

	return frag;
}

static bool FindBestEdge (samplefraginfo_t *info, samplefrag_t *&bestfrag, samplefragedge_t *&bestedge)
{
	samplefrag_t *f;
	samplefragedge_t *e;
	bool found;

	found = false;

	for (f = info->head; f; f = f->next)
	{
		for (e = f->edges; e < f->edges + f->numedges; e++)
		{
			if (e->tried)
			{
				continue;
			}

			bool better;

			if (!found)
			{
				better = true;
			}
			else if ((e->flippedangle < Q_PI + NORMAL_EPSILON) != (bestedge->flippedangle < Q_PI + NORMAL_EPSILON))
			{
				better = ((e->flippedangle < Q_PI + NORMAL_EPSILON) && !(bestedge->flippedangle < Q_PI + NORMAL_EPSILON));
			}
			else if (e->noseam != bestedge->noseam)
			{
				better = (e->noseam && !bestedge->noseam);
			}
			else if (fabs (e->distance - bestedge->distance) > ON_EPSILON)
			{
				better = (e->distance < bestedge->distance);
			}
			else if (fabs (e->distancereduction - bestedge->distancereduction) > ON_EPSILON)
			{
				better = (e->distancereduction > bestedge->distancereduction);
			}
			else
			{
				better = e->edgenum < bestedge->edgenum;
			}

			if (better)
			{
				found = true;
				bestfrag = f;
				bestedge = e;
			}
		}
	}

	return found;
}

static samplefraginfo_t *CreateSampleFrag (int facenum, vec_t s, vec_t t, 
	const vec_t square[2][2],
	int maxsize)
{
	samplefraginfo_t *info;
	const vec3_t v_s = {1, 0, 0};
	const vec3_t v_t = {0, 1, 0};

	info = (samplefraginfo_t *)malloc (sizeof (samplefraginfo_t));
	hlassume (info != NULL, assume_NoMemory);
	info->maxsize = maxsize;
	info->size = 1;
	info->head = (samplefrag_t *)malloc (sizeof (samplefrag_t));
	hlassume (info->head != NULL, assume_NoMemory);

	info->head->next = NULL;
	info->head->parentfrag = NULL;
	info->head->parentedge = NULL;
	info->head->facenum = facenum;
	
	info->head->flippedangle = 0.0;
	info->head->noseam = true;

	MatrixForScale (vec3_origin, 1.0, info->head->coordtomycoord);
	MatrixForScale (vec3_origin, 1.0, info->head->mycoordtocoord);

	info->head->origin[0] = s;
	info->head->origin[1] = t;
	info->head->origin[2] = 0.0;
	VectorCopy (info->head->origin, info->head->myorigin);

	VectorScale (v_s,  1, info->head->rect.planes[0].normal); info->head->rect.planes[0].dist =  square[0][0]; // smin
	VectorScale (v_s, -1, info->head->rect.planes[1].normal); info->head->rect.planes[1].dist = -square[1][0]; // smax
	VectorScale (v_t,  1, info->head->rect.planes[2].normal); info->head->rect.planes[2].dist =  square[0][1]; // tmin
	VectorScale (v_t, -1, info->head->rect.planes[3].normal); info->head->rect.planes[3].dist = -square[1][1]; // tmax
	info->head->myrect = info->head->rect;

	ChopFrag (info->head);

	if (info->head->winding->m_NumPoints == 0 || info->head->mywinding->m_NumPoints == 0)
	{
		// empty
		delete info->head->mywinding;
		delete info->head->winding;
		free (info->head->edges);
		free (info->head);
		info->head = NULL;
		info->size = 0;
	}
	else
	{
		// prune edges
		for (samplefragedge_t *e = info->head->edges; e < info->head->edges + info->head->numedges; e++)
		{
			if (e->nextfacenum == info->head->facenum)
			{
				e->tried = true;
			}
		}
	}

	while (info->size < info->maxsize)
	{
		samplefrag_t *bestfrag;
		samplefragedge_t *bestedge;
		samplefrag_t *newfrag;

		if (!FindBestEdge (info, bestfrag, bestedge))
		{
			break;
		}

		newfrag = GrowSingleFrag (info, bestfrag, bestedge);
		bestedge->tried = true;

		if (newfrag)
		{
			newfrag->next = info->head;
			info->head = newfrag;
			info->size++;

			for (samplefrag_t *f = info->head; f; f = f->next)
			{
				for (samplefragedge_t *e = newfrag->edges; e < newfrag->edges + newfrag->numedges; e++)
				{
					if (e->nextfacenum == f->facenum)
					{
						e->tried = true;
					}
				}
			}
			for (samplefrag_t *f = info->head; f; f = f->next)
			{
				for (samplefragedge_t *e = f->edges; e < f->edges + f->numedges; e++)
				{
					if (e->nextfacenum == newfrag->facenum)
					{
						e->tried = true;
					}
				}
			}
		}
	}

	return info;
}

static bool IsFragEmpty (samplefraginfo_t *fraginfo)
{
	return (fraginfo->size == 0);
}

static void DeleteSampleFrag (samplefraginfo_t *fraginfo)
{
	while (fraginfo->head)
	{
		samplefrag_t *f;

		f = fraginfo->head;
		fraginfo->head = f->next;
		delete f->mywinding;
		delete f->winding;
		free (f->edges);
		free (f);
	}
	free (fraginfo);
}

static light_flag_t SetSampleFromST(vec_t* const point,
									vec_t* const position, // a valid world position for light tracing
									int* const surface, // the face used for phong normal and patch interpolation
									bool *nudged,
									const lightinfo_t* const l, const vec_t original_s, const vec_t original_t,
									const vec_t square[2][2], // {smin, tmin}, {smax, tmax}
									eModelLightmodes lightmode)
{
	light_flag_t LuxelFlag;
	int facenum;
	dface_t *face;
	const dplane_t*	faceplane;
	samplefraginfo_t *fraginfo;
	samplefrag_t *f;

	facenum = l->surfnum;
	face = l->face;
	faceplane = getPlaneFromFace (face);
	
	fraginfo = CreateSampleFrag (facenum, original_s, original_t, 
		square,
		100);
	
	bool found;
	samplefrag_t *bestfrag;
	vec3_t bestpos;
	vec_t bests, bestt;
	vec_t best_dist;
	bool best_nudged;

	found = false;
	for (f = fraginfo->head; f; f = f->next)
	{
		vec3_t pos;
		vec_t s, t;
		vec_t dist;

		bool nudged_one;
		if (!FindNearestPosition (f->facenum, f->mywinding, f->mywindingplane, f->myorigin[0], f->myorigin[1], pos, &s, &t, &dist
									, &nudged_one
									))
		{
			continue;
		}

		bool better;

		if (!found)
		{
			better = true;
		}
		else if (nudged_one != best_nudged)
		{
			better = !nudged_one;
		}
		else if (fabs (dist - best_dist) > 2 * ON_EPSILON)
		{
			better = (dist < best_dist);
		}
		else if (f->noseam != bestfrag->noseam)
		{
			better = (f->noseam && !bestfrag->noseam);
		}
		else
		{
			better = (f->facenum < bestfrag->facenum);
		}

		if (better)
		{
			found = true;
			bestfrag = f;
			VectorCopy (pos, bestpos);
			bests = s;
			bestt = t;
			best_dist = dist;
			best_nudged = nudged_one;
		}
	}
	
	if (found)
	{
		matrix_t worldtotex, textoworld;
		vec3_t tex;

		TranslateWorldToTex (bestfrag->facenum, worldtotex);
		if (!InvertMatrix (worldtotex, textoworld))
		{
			const unsigned facenum = bestfrag->facenum;
			ThreadLock ();
			Log ("Malformed face (%d) normal @ \n", facenum);
			Winding* w = new Winding (g_dfaces[facenum]);
			for (int x = 0; x < w->m_NumPoints; x++)
			{
				VectorAdd (w->m_Points[x], g_face_offset[facenum], w->m_Points[x]);
			}
			w->Print ();
			delete w;
			ThreadUnlock ();
			hlassume (false, assume_MalformedTextureFace);
		}
		
		// point
		tex[0] = bests;
		tex[1] = bestt;
		tex[2] = 0.0;
		{vec3_t v; ApplyMatrix (textoworld, tex, v); VectorCopy (v, point);}
		VectorAdd (point, g_face_offset[bestfrag->facenum], point);
		// position
		VectorCopy (bestpos, position);
		// surface
		*surface = bestfrag->facenum;
		// whether nudged to fit
		*nudged = best_nudged;
		// returned value
		LuxelFlag = LightNormal;
	}
	else
	{
		SetSurfFromST (l, point, original_s, original_t);
		VectorMA (point, DEFAULT_HUNT_OFFSET, faceplane->normal, position);
		*surface = facenum;
		*nudged = true;
		LuxelFlag = LightOutside;
	}

	DeleteSampleFrag (fraginfo);
	
	return LuxelFlag;

}
static void		CalcPoints(lightinfo_t* l)
{
	const int       facenum = l->surfnum;
	const dface_t*  f = g_dfaces + facenum;
	const dplane_t* p = getPlaneFromFace (f);
	const vec_t*    face_delta = g_face_offset[facenum];
	const eModelLightmodes lightmode = g_face_lightmode[facenum];
	const int       h = l->texsize[1] + 1;
	const int       w = l->texsize[0] + 1;
	const vec_t     starts = l->texmins[0] * TEXTURE_STEP;
	const vec_t     startt = l->texmins[1] * TEXTURE_STEP;
	light_flag_t    LuxelFlags[MAX_SINGLEMAP];
	light_flag_t*   pLuxelFlags;
	vec_t           us, ut;
	vec_t*          surf;
	int             s, t;
	l->numsurfpt = w * h;
	for (t = 0; t < h; t++)
	{
		for (s = 0; s < w; s++)
		{
			surf = l->surfpt[s+w*t];
			pLuxelFlags = &LuxelFlags[s+w*t];
			us = starts + s * TEXTURE_STEP;
			ut = startt + t * TEXTURE_STEP;
			vec_t square[2][2];
			square[0][0] = us - TEXTURE_STEP;
			square[0][1] = ut - TEXTURE_STEP;
			square[1][0] = us + TEXTURE_STEP;
			square[1][1] = ut + TEXTURE_STEP;
			bool nudged;
			*pLuxelFlags = SetSampleFromST (surf,
											l->surfpt_position[s+w*t], &l->surfpt_surface[s+w*t],
											&nudged,
											l, us, ut,
											square,
											lightmode);
		}
	}
    {
		int i, n;
		int s_other, t_other;
		light_flag_t* pLuxelFlags_other;
		vec_t* surf_other;
		bool adjusted;
		for (i = 0; i < h + w; i++)
		{ // propagate valid light samples
			adjusted = false;
			for (t = 0; t < h; t++)
			{
				for (s = 0; s < w; s++)
				{
					surf = l->surfpt[s+w*t];
					pLuxelFlags = &LuxelFlags[s+w*t];
					if (*pLuxelFlags != LightOutside)
						continue;
					for (n = 0; n < 4; n++)
					{
						switch (n)
						{
						case 0: s_other = s + 1; t_other = t; break;
						case 1: s_other = s - 1; t_other = t; break;
						case 2: s_other = s; t_other = t + 1; break;
						case 3: s_other = s; t_other = t - 1; break;
						}
						if (t_other < 0 || t_other >= h || s_other < 0 || s_other >= w)
							continue;
						surf_other = l->surfpt[s_other+w*t_other];
						pLuxelFlags_other = &LuxelFlags[s_other+w*t_other];
						if (*pLuxelFlags_other != LightOutside && *pLuxelFlags_other != LightShifted)
						{
							*pLuxelFlags = LightShifted;
							VectorCopy (surf_other, surf);
							VectorCopy (l->surfpt_position[s_other+w*t_other], l->surfpt_position[s+w*t]);
							l->surfpt_surface[s+w*t] = l->surfpt_surface[s_other+w*t_other];
							adjusted = true;
							break;
						}
					}
				}
			}
			for (t = 0; t < h; t++)
			{
				for (s = 0; s < w; s++)
				{
					pLuxelFlags = &LuxelFlags[s+w*t];
					if (*pLuxelFlags == LightShifted)
					{
						*pLuxelFlags = LightShiftedInside;
					}
				}
			}
			if (!adjusted)
				break;
		}
	}
	for (int i = 0; i < MAX_SINGLEMAP; i++)
	{
		l->surfpt_lightoutside[i] = (LuxelFlags[i] == LightOutside);
	}
}

//==============================================================

typedef struct
{
    vec3_t          pos;
    vec3_t          light;
	int				surface; // this sample can grow into another face
}
sample_t;

typedef struct
{
    int             numsamples;
    sample_t*       samples[MAXLIGHTMAPS];
}
facelight_t;

static directlight_t* directlights[MAX_MAP_LEAFS];
static facelight_t facelight[MAX_MAP_FACES];
static int      numdlights;


// =====================================================================================
//  CreateDirectLights
// =====================================================================================
void            CreateDirectLights()
{
    unsigned        i;
    patch_t*        p;
    directlight_t*  dl;
    dleaf_t*        leaf;
    int             leafnum;
    entity_t*       e;
    entity_t*       e2;
    const char*     name;
    const char*     target;
    float           angle;
    vec3_t          dest;


    numdlights = 0;
	int styleused[ALLSTYLES];
	memset (styleused, 0, ALLSTYLES * sizeof(styleused[0]));
	styleused[0] = true;
	int numstyles = 1;

    //
    // surfaces
    //
    for (i = 0, p = g_patches; i < g_num_patches; i++, p++)
    {
		if (p->emitstyle >= 0 && p->emitstyle < ALLSTYLES)
		{
			if (styleused[p->emitstyle] == false)
			{
				styleused[p->emitstyle] = true;
				numstyles++;
			}
		}
        if (
			DotProduct (p->baselight, p->texturereflectivity) / 3
			> 0.0
			&& !(g_face_texlights[p->faceNumber]
				&& *ValueForKey (g_face_texlights[p->faceNumber], "_scale")
				&& FloatForKey (g_face_texlights[p->faceNumber], "_scale") <= 0)
			) //LRC
        {
            numdlights++;
            dl = (directlight_t*)calloc(1, sizeof(directlight_t));

			hlassume (dl != NULL, assume_NoMemory);

            VectorCopy(p->origin, dl->origin);

            leaf = PointInLeaf(dl->origin);
            leafnum = leaf - g_dleafs;

            dl->next = directlights[leafnum];
            directlights[leafnum] = dl;
            dl->style = p->emitstyle; //LRC
			dl->topatch = false;
			if (!p->emitmode)
			{
				dl->topatch = true;
			}
			if (g_fastmode)
			{
				dl->topatch = true;
			}
			dl->patch_area = p->area;
			dl->patch_emitter_range = p->emitter_range;
			dl->patch = p;
			dl->texlightgap = g_texlightgap;
			if (g_face_texlights[p->faceNumber] && *ValueForKey (g_face_texlights[p->faceNumber], "_texlightgap"))
			{
				dl->texlightgap = FloatForKey (g_face_texlights[p->faceNumber], "_texlightgap");
			}
			dl->stopdot = 0.0;
			dl->stopdot2 = 0.0;
			if (g_face_texlights[p->faceNumber])
			{
				if (*ValueForKey (g_face_texlights[p->faceNumber], "_cone"))
				{
					dl->stopdot = FloatForKey (g_face_texlights[p->faceNumber], "_cone");
					dl->stopdot = dl->stopdot >= 90? 0: (float)cos (dl->stopdot / 180 * Q_PI);
				}
				if (*ValueForKey (g_face_texlights[p->faceNumber], "_cone2"))
				{
					dl->stopdot2 = FloatForKey (g_face_texlights[p->faceNumber], "_cone2");
					dl->stopdot2 = dl->stopdot2 >= 90? 0: (float)cos (dl->stopdot2 / 180 * Q_PI);
				}
				if (dl->stopdot2 > dl->stopdot)
					dl->stopdot2 = dl->stopdot;
			}

            dl->type = emit_surface;
            VectorCopy(getPlaneFromFaceNumber(p->faceNumber)->normal, dl->normal);
            VectorCopy(p->baselight, dl->intensity); //LRC
			if (g_face_texlights[p->faceNumber])
			{
				if (*ValueForKey (g_face_texlights[p->faceNumber], "_scale"))
				{
					vec_t scale = FloatForKey (g_face_texlights[p->faceNumber], "_scale");
					VectorScale (dl->intensity, scale, dl->intensity);
				}
			}
            VectorScale(dl->intensity, p->area, dl->intensity);
			VectorScale (dl->intensity, p->exposure, dl->intensity);
			VectorScale (dl->intensity, 1.0 / Q_PI, dl->intensity);
			VectorMultiply (dl->intensity, p->texturereflectivity, dl->intensity);
        
			dface_t *f = &g_dfaces[p->faceNumber];
			if (g_face_entity[p->faceNumber] - g_entities != 0 && !strncasecmp (GetTextureByNumber (f->texinfo), "!", 1))
			{
				directlight_t *dl2;
				numdlights++;
				dl2 = (directlight_t *)calloc (1, sizeof (directlight_t));
				hlassume (dl2 != NULL, assume_NoMemory);
				*dl2 = *dl;
				VectorMA (dl->origin, -2, dl->normal, dl2->origin);
				VectorSubtract (vec3_origin, dl->normal, dl2->normal);
				leaf = PointInLeaf (dl2->origin);
				leafnum = leaf - g_dleafs;
				dl2->next = directlights[leafnum];
				directlights[leafnum] = dl2;
			}
        }

        //LRC        VectorClear(p->totallight[0]);
    }

    //
    // entities
    //
    for (i = 0; i < (unsigned)g_numentities; i++)
    {
        const char*     pLight;
        double          r, g, b, scaler;
        float           l1;
        int             argCnt;

        e = &g_entities[i];
        name = ValueForKey(e, "classname");
        if (strncmp(name, "light", 5))
            continue;
		{
			int style = IntForKey (e, "style");
			if (style < 0)
			{
				style = -style;
			}
			style = (unsigned char)style;
			if (style > 0 && style < ALLSTYLES && *ValueForKey (e, "zhlt_stylecoring"))
			{
				g_corings[style] = FloatForKey (e, "zhlt_stylecoring");
			}
		}
		if (!strcmp (name, "light_shadow")
			|| !strcmp (name, "light_bounce")
			)
		{
			int style = IntForKey (e, "style");
			if (style < 0)
			{
				style = -style;
			}
			style = (unsigned char)style;
			if (style >= 0 && style < ALLSTYLES)
			{
				if (styleused[style] == false)
				{
					styleused[style] = true;
					numstyles++;
				}
			}
			continue;
		}
		if (!strcmp (name, "light_surface"))
		{
			continue;
		}

        numdlights++;
        dl = (directlight_t*)calloc(1, sizeof(directlight_t));

		hlassume (dl != NULL, assume_NoMemory);

        GetVectorForKey(e, "origin", dl->origin);

        leaf = PointInLeaf(dl->origin);
        leafnum = leaf - g_dleafs;

        dl->next = directlights[leafnum];
        directlights[leafnum] = dl;

        dl->style = IntForKey(e, "style");
        if (dl->style < 0) 
            dl->style = -dl->style; //LRC
		dl->style = (unsigned char)dl->style;
		if (dl->style >= ALLSTYLES)
		{
			Error ("invalid light style: style (%d) >= ALLSTYLES (%d)", dl->style, ALLSTYLES);
		}
		if (dl->style >= 0 && dl->style < ALLSTYLES)
		{
			if (styleused[dl->style] == false)
			{
				styleused[dl->style] = true;
				numstyles++;
			}
		}
		dl->topatch = false;
		if (IntForKey (e, "_fast") == 1)
		{
			dl->topatch = true;
		}
		if (g_fastmode)
		{
			dl->topatch = true;
		}
        pLight = ValueForKey(e, "_light");
        // scanf into doubles, then assign, so it is vec_t size independent
        r = g = b = scaler = 0;
        argCnt = sscanf(pLight, "%lf %lf %lf %lf", &r, &g, &b, &scaler);
        dl->intensity[0] = (float)r;
        if (argCnt == 1)
        {
            // The R,G,B values are all equal.
            dl->intensity[1] = dl->intensity[2] = (float)r;
        }
        else if (argCnt == 3 || argCnt == 4)
        {
            // Save the other two G,B values.
            dl->intensity[1] = (float)g;
            dl->intensity[2] = (float)b;

            // Did we also get an "intensity" scaler value too?
            if (argCnt == 4)
            {
                // Scale the normalized 0-255 R,G,B values by the intensity scaler
                dl->intensity[0] = dl->intensity[0] / 255 * (float)scaler;
                dl->intensity[1] = dl->intensity[1] / 255 * (float)scaler;
                dl->intensity[2] = dl->intensity[2] / 255 * (float)scaler;
            }
        }
        else
        {
            Log("light at (%f,%f,%f) has bad or missing '_light' value : '%s'\n",
                dl->origin[0], dl->origin[1], dl->origin[2], pLight);
            continue;
        }

        dl->fade = FloatForKey(e, "_fade");
        if (dl->fade == 0.0)
        {
            dl->fade = g_fade;
        }


        target = ValueForKey(e, "target");

        if (!strcmp(name, "light_spot") || !strcmp(name, "light_environment") || target[0])
        {
            if (!VectorAvg(dl->intensity))
            {
            }
            dl->type = emit_spotlight;
            dl->stopdot = FloatForKey(e, "_cone");
            if (!dl->stopdot)
            {
                dl->stopdot = 10;
            }
            dl->stopdot2 = FloatForKey(e, "_cone2");
            if (!dl->stopdot2)
            {
                dl->stopdot2 = dl->stopdot;
            }
            if (dl->stopdot2 < dl->stopdot)
            {
                dl->stopdot2 = dl->stopdot;
            }
            dl->stopdot2 = (float)cos(dl->stopdot2 / 180 * Q_PI);
            dl->stopdot = (float)cos(dl->stopdot / 180 * Q_PI);

            if (!FindTargetEntity(target)) //--vluzacn
            {
                Warning("light at (%i %i %i) has missing target",
                        (int)dl->origin[0], (int)dl->origin[1], (int)dl->origin[2]);
				target = "";
            }
            if (target[0])
            {                                              // point towards target
                e2 = FindTargetEntity(target);
                if (!e2)
                {
                    Warning("light at (%i %i %i) has missing target",
                            (int)dl->origin[0], (int)dl->origin[1], (int)dl->origin[2]);
                }
                else
                {
                    GetVectorForKey(e2, "origin", dest);
                    VectorSubtract(dest, dl->origin, dl->normal);
                    VectorNormalize(dl->normal);
                }
            }
            else
            {                                              // point down angle
                vec3_t          vAngles;

                GetVectorForKey(e, "angles", vAngles);

                angle = (float)FloatForKey(e, "angle");
                if (angle == ANGLE_UP)
                {
                    dl->normal[0] = dl->normal[1] = 0;
                    dl->normal[2] = 1;
                }
                else if (angle == ANGLE_DOWN)
                {
                    dl->normal[0] = dl->normal[1] = 0;
                    dl->normal[2] = -1;
                }
                else
                {
                    // if we don't have a specific "angle" use the "angles" YAW
                    if (!angle)
                    {
                        angle = vAngles[1];
                    }

                    dl->normal[2] = 0;
                    dl->normal[0] = (float)cos(angle / 180 * Q_PI);
                    dl->normal[1] = (float)sin(angle / 180 * Q_PI);
                }

                angle = FloatForKey(e, "pitch");
                if (!angle)
                {
                    // if we don't have a specific "pitch" use the "angles" PITCH
                    angle = vAngles[0];
                }

                dl->normal[2] = (float)sin(angle / 180 * Q_PI);
                dl->normal[0] *= (float)cos(angle / 180 * Q_PI);
                dl->normal[1] *= (float)cos(angle / 180 * Q_PI);
            }

            if (FloatForKey(e, "_sky") || !strcmp(name, "light_environment"))
            {
				// -----------------------------------------------------------------------------------
				// Changes by Adam Foster - afoster@compsoc.man.ac.uk
				// diffuse lighting hack - most of the following code nicked from earlier
				// need to get diffuse intensity from new _diffuse_light key
				//
				// What does _sky do for spotlights, anyway?
				// -----------------------------------------------------------------------------------
				pLight = ValueForKey(e, "_diffuse_light");
        		r = g = b = scaler = 0;
        		argCnt = sscanf(pLight, "%lf %lf %lf %lf", &r, &g, &b, &scaler);
        		dl->diffuse_intensity[0] = (float)r;
        		if (argCnt == 1)
        		{
            		// The R,G,B values are all equal.
            		dl->diffuse_intensity[1] = dl->diffuse_intensity[2] = (float)r;
        		}
        		else if (argCnt == 3 || argCnt == 4)
        		{
            		// Save the other two G,B values.
            		dl->diffuse_intensity[1] = (float)g;
            		dl->diffuse_intensity[2] = (float)b;

            		// Did we also get an "intensity" scaler value too?
      		    	if (argCnt == 4)
     	       		{
                		// Scale the normalized 0-255 R,G,B values by the intensity scaler
                		dl->diffuse_intensity[0] = dl->diffuse_intensity[0] / 255 * (float)scaler;
                		dl->diffuse_intensity[1] = dl->diffuse_intensity[1] / 255 * (float)scaler;
                		dl->diffuse_intensity[2] = dl->diffuse_intensity[2] / 255 * (float)scaler;
            		}
        		}
				else
        		{
					// backwards compatibility with maps without _diffuse_light

					dl->diffuse_intensity[0] = dl->intensity[0];
					dl->diffuse_intensity[1] = dl->intensity[1];
					dl->diffuse_intensity[2] = dl->intensity[2];
        		}
				// -----------------------------------------------------------------------------------
				pLight = ValueForKey(e, "_diffuse_light2");
        		r = g = b = scaler = 0;
        		argCnt = sscanf(pLight, "%lf %lf %lf %lf", &r, &g, &b, &scaler);
        		dl->diffuse_intensity2[0] = (float)r;
        		if (argCnt == 1)
        		{
            		// The R,G,B values are all equal.
            		dl->diffuse_intensity2[1] = dl->diffuse_intensity2[2] = (float)r;
        		}
        		else if (argCnt == 3 || argCnt == 4)
        		{
            		// Save the other two G,B values.
            		dl->diffuse_intensity2[1] = (float)g;
            		dl->diffuse_intensity2[2] = (float)b;

            		// Did we also get an "intensity" scaler value too?
      		    	if (argCnt == 4)
     	       		{
                		// Scale the normalized 0-255 R,G,B values by the intensity scaler
                		dl->diffuse_intensity2[0] = dl->diffuse_intensity2[0] / 255 * (float)scaler;
                		dl->diffuse_intensity2[1] = dl->diffuse_intensity2[1] / 255 * (float)scaler;
                		dl->diffuse_intensity2[2] = dl->diffuse_intensity2[2] / 255 * (float)scaler;
            		}
        		}
				else
        		{
					dl->diffuse_intensity2[0] = dl->diffuse_intensity[0];
					dl->diffuse_intensity2[1] = dl->diffuse_intensity[1];
					dl->diffuse_intensity2[2] = dl->diffuse_intensity[2];
        		}

                dl->type = emit_skylight;
                dl->stopdot2 = FloatForKey(e, "_sky");     // hack stopdot2 to a sky key number
				dl->sunspreadangle = FloatForKey (e, "_spread");
				if (!g_allow_spread)
				{
					dl->sunspreadangle = 0;
				}
				if (dl->sunspreadangle < 0.0 || dl->sunspreadangle > 180)
				{
					Error ("Invalid spread angle '%s'. Please use a number between 0 and 180.\n", ValueForKey (e, "_spread"));
				}
				if (dl->sunspreadangle > 0.0)
				{
					int i;
					vec_t testangle = dl->sunspreadangle;
					if (dl->sunspreadangle < SUNSPREAD_THRESHOLD)
					{
						testangle = SUNSPREAD_THRESHOLD; // We will later centralize all the normals we have collected.
					}
					{
						vec_t totalweight = 0;
						int count;
						vec_t testdot = cos (testangle * (Q_PI / 180.0));
						for (count = 0, i = 0; i < g_numskynormals[SUNSPREAD_SKYLEVEL]; i++)
						{
							vec3_t &testnormal = g_skynormals[SUNSPREAD_SKYLEVEL][i];
							vec_t dot = DotProduct (dl->normal, testnormal);
							if (dot >= testdot - NORMAL_EPSILON)
							{
								totalweight += qmax (0, dot - testdot) * g_skynormalsizes[SUNSPREAD_SKYLEVEL][i]; // This is not the right formula when dl->sunspreadangle < SUNSPREAD_THRESHOLD, but it gives almost the same result as the right one.
								count++;
							}
						}
						if (count <= 10 || totalweight <= NORMAL_EPSILON)
						{
							Error ("collect spread normals: internal error: can not collect enough normals.");
						}
						dl->numsunnormals = count;
						dl->sunnormals = (vec3_t *)malloc (count * sizeof (vec3_t));
						dl->sunnormalweights = (vec_t *)malloc (count * sizeof (vec_t));
						hlassume (dl->sunnormals != NULL, assume_NoMemory);
						hlassume (dl->sunnormalweights != NULL, assume_NoMemory);
						for (count = 0, i = 0; i < g_numskynormals[SUNSPREAD_SKYLEVEL]; i++)
						{
							vec3_t &testnormal = g_skynormals[SUNSPREAD_SKYLEVEL][i];
							vec_t dot = DotProduct (dl->normal, testnormal);
							if (dot >= testdot - NORMAL_EPSILON)
							{
								if (count >= dl->numsunnormals)
								{
									Error ("collect spread normals: internal error.");
								}
								VectorCopy (testnormal, dl->sunnormals[count]);
								dl->sunnormalweights[count] = qmax (0, dot - testdot) * g_skynormalsizes[SUNSPREAD_SKYLEVEL][i] / totalweight;
								count++;
							}
						}
						if (count != dl->numsunnormals)
						{
							Error ("collect spread normals: internal error.");
						}
					}
					if (dl->sunspreadangle < SUNSPREAD_THRESHOLD)
					{
						for (i = 0; i < dl->numsunnormals; i++)
						{
							vec3_t tmp;
							VectorScale (dl->sunnormals[i], 1 / DotProduct (dl->sunnormals[i], dl->normal), tmp);
							VectorSubtract (tmp, dl->normal, tmp);
							VectorMA (dl->normal, dl->sunspreadangle / SUNSPREAD_THRESHOLD, tmp, dl->sunnormals[i]);
							VectorNormalize (dl->sunnormals[i]);
						}
					}
				}
				else
				{
					dl->numsunnormals = 1;
					dl->sunnormals = (vec3_t *)malloc (sizeof (vec3_t));
					dl->sunnormalweights = (vec_t *)malloc (sizeof (vec_t));
					hlassume (dl->sunnormals != NULL, assume_NoMemory);
					hlassume (dl->sunnormalweights != NULL, assume_NoMemory);
					VectorCopy (dl->normal, dl->sunnormals[0]);
					dl->sunnormalweights[0] = 1.0;
				}
            }
        }
        else
        {
            if (!VectorAvg(dl->intensity))
			{
			}
            dl->type = emit_point;
        }

        if (dl->type != emit_skylight)
        {
			//why? --vluzacn
            l1 = qmax(dl->intensity[0], qmax(dl->intensity[1], dl->intensity[2]));
            l1 = l1 * l1 / 10;

            dl->intensity[0] *= l1;
            dl->intensity[1] *= l1;
            dl->intensity[2] *= l1;
        }
    }

	int countnormallights = 0, countfastlights = 0;
	{
		int l;
		for (l = 0; l < 1 + g_dmodels[0].visleafs; l++)
		{
			for (dl = directlights[l]; dl; dl = dl->next)
			{
				switch (dl->type)
				{
				case emit_surface:
				case emit_point:
				case emit_spotlight:
					if (!VectorCompare (dl->intensity, vec3_origin))
					{
						if (dl->topatch)
						{
							countfastlights++;
						}
						else
						{
							countnormallights++;
						}
					}
					break;
				case emit_skylight:
					if (!VectorCompare (dl->intensity, vec3_origin))
					{
						if (dl->topatch)
						{
							countfastlights++;
							if (dl->sunspreadangle > 0.0)
							{
								countfastlights--;
								countfastlights += dl->numsunnormals;
							}
						}
						else
						{
							countnormallights++;
							if (dl->sunspreadangle > 0.0)
							{
								countnormallights--;
								countnormallights += dl->numsunnormals;
							}
						}
					}
					if (g_indirect_sun > 0 && !VectorCompare (dl->diffuse_intensity, vec3_origin))
					{
						if (g_softsky)
						{
							countfastlights += g_numskynormals[SKYLEVEL_SOFTSKYON];
						}
						else
						{
							countfastlights += g_numskynormals[SKYLEVEL_SOFTSKYOFF];
						}
					}
					break;
				default:
					hlassume(false, assume_BadLightType);
					break;
				}
			}
		}
	}
    Log("%i direct lights and %i fast direct lights\n", countnormallights, countfastlights);
	Log("%i light styles\n", numstyles);
	// move all emit_skylight to leaf 0 (the solid leaf)
	if (g_sky_lighting_fix)
	{
		directlight_t *skylights = NULL;
		int l;
		for (l = 0; l < 1 + g_dmodels[0].visleafs; l++)
		{
			directlight_t **pdl;
			for (dl = directlights[l], pdl = &directlights[l]; dl; dl = *pdl)
			{
				if (dl->type == emit_skylight)
				{
					*pdl = dl->next;
					dl->next = skylights;
					skylights = dl;
				}
				else
				{
					pdl = &dl->next;
				}
			}
		}
        while ((dl = directlights[0]) != NULL)
        {
			// since they are in leaf 0, they won't emit a light anyway
            directlights[0] = dl->next;
            free(dl);
        }
		directlights[0] = skylights;
	}
	if (g_sky_lighting_fix)
	{
		int countlightenvironment = 0;
		int countinfosunlight = 0;
		for (int i = 0; i < g_numentities; i++)
		{
			entity_t *e = &g_entities[i];
			const char *classname = ValueForKey (e, "classname");
			if (!strcmp (classname, "light_environment"))
			{
				countlightenvironment++;
			}
			if (!strcmp (classname, "info_sunlight"))
			{
				countinfosunlight++;
			}
		}
		if (countlightenvironment > 1 && countinfosunlight == 0)
		{
			// because the map is lit by more than one light_environments, but the game can only recognize one of them when setting sv_skycolor and sv_skyvec.
			Warning ("More than one light_environments are in use. Add entity info_sunlight to clarify the sunlight's brightness for in-game model(.mdl) rendering.");
		}
	}
}

// =====================================================================================
//  DeleteDirectLights
// =====================================================================================
void            DeleteDirectLights()
{
    int             l;
    directlight_t*  dl;

	for (l = 0; l < 1 + g_dmodels[0].visleafs; l++)
    {
        dl = directlights[l];
        while (dl)
        {
            directlights[l] = dl->next;
            free(dl);
            dl = directlights[l];
        }
    }

    // AJM: todo: strip light entities out at this point
	// vluzacn: hlvis and hlrad must not modify entity data, because the following procedures are supposed to produce the same bsp file:
	//  1> hlcsg -> hlbsp -> hlvis -> hlrad  (a normal compile)
	//  2) hlcsg -> hlbsp -> hlvis -> hlrad -> hlcsg -onlyents
	//  3) hlcsg -> hlbsp -> hlvis -> hlrad -> hlcsg -onlyents -> hlrad
}

// =====================================================================================
//  GatherSampleLight
// =====================================================================================
int		g_numskynormals[SKYLEVELMAX+1];
vec3_t	*g_skynormals[SKYLEVELMAX+1];
vec_t	*g_skynormalsizes[SKYLEVELMAX+1];
typedef double point_t[3];
typedef struct {int point[2]; bool divided; int child[2];} edge_t;
typedef struct {int edge[3]; int dir[3];} triangle_t;
void CopyToSkynormals (int skylevel, int numpoints, point_t *points, int numedges, edge_t *edges, int numtriangles, triangle_t *triangles)
{
	hlassume (numpoints == (1 << (2 * skylevel)) + 2, assume_first);
	hlassume (numedges == (1 << (2 * skylevel)) * 4 - 4 , assume_first);
	hlassume (numtriangles == (1 << (2 * skylevel)) * 2, assume_first);
	g_numskynormals[skylevel] = numpoints;
	g_skynormals[skylevel] = (vec3_t *)malloc (numpoints * sizeof (vec3_t));
	g_skynormalsizes[skylevel] = (vec_t *)malloc (numpoints * sizeof (vec_t));
	hlassume (g_skynormals[skylevel] != NULL, assume_NoMemory);
	hlassume (g_skynormalsizes[skylevel] != NULL, assume_NoMemory);
	int j, k;
	for (j = 0; j < numpoints; j++)
	{
		VectorCopy (points[j], g_skynormals[skylevel][j]);
		g_skynormalsizes[skylevel][j] = 0;
	}
	double totalsize = 0;
	for (j = 0; j < numtriangles; j++)
	{
		int pt[3];
		for (k = 0; k < 3; k++)
		{
			pt[k] = edges[triangles[j].edge[k]].point[triangles[j].dir[k]];
		}
		double currentsize;
		double tmp[3];
		CrossProduct (points[pt[0]], points[pt[1]], tmp);
		currentsize = DotProduct (tmp, points[pt[2]]);
		hlassume (currentsize > 0, assume_first);
		g_skynormalsizes[skylevel][pt[0]] += currentsize / 3.0;
		g_skynormalsizes[skylevel][pt[1]] += currentsize / 3.0;
		g_skynormalsizes[skylevel][pt[2]] += currentsize / 3.0;
		totalsize += currentsize;
	}
	for (j = 0; j < numpoints; j++)
	{
		g_skynormalsizes[skylevel][j] /= totalsize;
	}
#if 0
	printf ("g_numskynormals[%i]=%i\n", skylevel, g_numskynormals[skylevel]);
	for (j = 0; j < numpoints; j += (numpoints / 20 + 1))
	{
		printf ("g_skynormals[%i][%i]=%1.3f,%1.3f,%1.3f g_skynormalsizes[%i][%i]=%f\n",
			skylevel, j, g_skynormals[skylevel][j][0], g_skynormals[skylevel][j][1], g_skynormals[skylevel][j][2],
			skylevel, j, g_skynormalsizes[skylevel][j]);
	}
#endif
}
void BuildDiffuseNormals ()
{
	int i, j, k;
	g_numskynormals[0] = 0;
	g_skynormals[0] = NULL; //don't use this
	g_skynormalsizes[0] = NULL;
	int numpoints = 6;
	point_t *points = (point_t *)malloc (((1 << (2 * SKYLEVELMAX)) + 2) * sizeof (point_t));
	hlassume (points != NULL, assume_NoMemory);
	points[0][0] = 1, points[0][1] = 0, points[0][2] = 0;
	points[1][0] = -1,points[1][1] = 0, points[1][2] = 0;
	points[2][0] = 0, points[2][1] = 1, points[2][2] = 0;
	points[3][0] = 0, points[3][1] = -1,points[3][2] = 0;
	points[4][0] = 0, points[4][1] = 0, points[4][2] = 1;
	points[5][0] = 0, points[5][1] = 0, points[5][2] = -1;
	int numedges = 12;
	edge_t *edges = (edge_t *)malloc (((1 << (2 * SKYLEVELMAX)) * 4 - 4) * sizeof (edge_t));
	hlassume (edges != NULL, assume_NoMemory);
	edges[0].point[0] = 0, edges[0].point[1] = 2, edges[0].divided = false;
	edges[1].point[0] = 2, edges[1].point[1] = 1, edges[1].divided = false;
	edges[2].point[0] = 1, edges[2].point[1] = 3, edges[2].divided = false;
	edges[3].point[0] = 3, edges[3].point[1] = 0, edges[3].divided = false;
	edges[4].point[0] = 2, edges[4].point[1] = 4, edges[4].divided = false;
	edges[5].point[0] = 4, edges[5].point[1] = 3, edges[5].divided = false;
	edges[6].point[0] = 3, edges[6].point[1] = 5, edges[6].divided = false;
	edges[7].point[0] = 5, edges[7].point[1] = 2, edges[7].divided = false;
	edges[8].point[0] = 4, edges[8].point[1] = 0, edges[8].divided = false;
	edges[9].point[0] = 0, edges[9].point[1] = 5, edges[9].divided = false;
	edges[10].point[0] = 5, edges[10].point[1] = 1, edges[10].divided = false;
	edges[11].point[0] = 1, edges[11].point[1] = 4, edges[11].divided = false;
	int numtriangles = 8;
	triangle_t *triangles = (triangle_t *)malloc (((1 << (2 * SKYLEVELMAX)) * 2) * sizeof (triangle_t));
	hlassume (triangles != NULL, assume_NoMemory);
	triangles[0].edge[0] = 0, triangles[0].dir[0] = 0, triangles[0].edge[1] = 4, triangles[0].dir[1] = 0, triangles[0].edge[2] = 8, triangles[0].dir[2] = 0;
	triangles[1].edge[0] = 1, triangles[1].dir[0] = 0, triangles[1].edge[1] = 11, triangles[1].dir[1] = 0, triangles[1].edge[2] = 4, triangles[1].dir[2] = 1;
	triangles[2].edge[0] = 2, triangles[2].dir[0] = 0, triangles[2].edge[1] = 5, triangles[2].dir[1] = 1, triangles[2].edge[2] = 11, triangles[2].dir[2] = 1;
	triangles[3].edge[0] = 3, triangles[3].dir[0] = 0, triangles[3].edge[1] = 8, triangles[3].dir[1] = 1, triangles[3].edge[2] = 5, triangles[3].dir[2] = 0;
	triangles[4].edge[0] = 0, triangles[4].dir[0] = 1, triangles[4].edge[1] = 9, triangles[4].dir[1] = 0, triangles[4].edge[2] = 7, triangles[4].dir[2] = 0;
	triangles[5].edge[0] = 1, triangles[5].dir[0] = 1, triangles[5].edge[1] = 7, triangles[5].dir[1] = 1, triangles[5].edge[2] = 10, triangles[5].dir[2] = 0;
	triangles[6].edge[0] = 2, triangles[6].dir[0] = 1, triangles[6].edge[1] = 10, triangles[6].dir[1] = 1, triangles[6].edge[2] = 6, triangles[6].dir[2] = 1;
	triangles[7].edge[0] = 3, triangles[7].dir[0] = 1, triangles[7].edge[1] = 6, triangles[7].dir[1] = 0, triangles[7].edge[2] = 9, triangles[7].dir[2] = 1;
	CopyToSkynormals (1, numpoints, points, numedges, edges, numtriangles, triangles);
	for (i = 1; i < SKYLEVELMAX; i++)
	{
		int oldnumedges = numedges;
		for (j = 0; j < oldnumedges; j++)
		{
			if (!edges[j].divided)
			{
				hlassume (numpoints < (1 << (2 * SKYLEVELMAX)) + 2, assume_first);
				point_t mid;
				double len;
				VectorAdd (points[edges[j].point[0]], points[edges[j].point[1]], mid);
				len = sqrt (DotProduct (mid, mid));
				hlassume (len > 0.2, assume_first);
				VectorScale (mid, 1 / len, mid);
				int p2 = numpoints;
				VectorCopy (mid, points[numpoints]);
				numpoints++;
				hlassume (numedges < (1 << (2 * SKYLEVELMAX)) * 4 - 4, assume_first);
				edges[j].child[0] = numedges;
				edges[numedges].divided = false;
				edges[numedges].point[0] = edges[j].point[0];
				edges[numedges].point[1] = p2;
				numedges++;
				hlassume (numedges < (1 << (2 * SKYLEVELMAX)) * 4 - 4, assume_first);
				edges[j].child[1] = numedges;
				edges[numedges].divided = false;
				edges[numedges].point[0] = p2;
				edges[numedges].point[1] = edges[j].point[1];
				numedges++;
				edges[j].divided = true;
			}
		}
		int oldnumtriangles = numtriangles;
		for (j = 0; j < oldnumtriangles; j++)
		{
			int mid[3];
			for (k = 0; k < 3; k++)
			{
				hlassume (numtriangles < (1 << (2 * SKYLEVELMAX)) * 2, assume_first);
				mid[k] = edges[edges[triangles[j].edge[k]].child[0]].point[1];
				triangles[numtriangles].edge[0] = edges[triangles[j].edge[k]].child[1 - triangles[j].dir[k]];
				triangles[numtriangles].dir[0] = triangles[j].dir[k];
				triangles[numtriangles].edge[1] = edges[triangles[j].edge[(k+1)%3]].child[triangles[j].dir[(k+1)%3]];
				triangles[numtriangles].dir[1] = triangles[j].dir[(k+1)%3];
				triangles[numtriangles].edge[2] = numedges + k;
				triangles[numtriangles].dir[2] = 1;
				numtriangles++;
			}
			for (k = 0; k < 3; k++)
			{
				hlassume (numedges < (1 << (2 * SKYLEVELMAX)) * 4 - 4, assume_first);
				triangles[j].edge[k] = numedges;
				triangles[j].dir[k] = 0;
				edges[numedges].divided = false;
				edges[numedges].point[0] = mid[k];
				edges[numedges].point[1] = mid[(k+1)%3];
				numedges++;
			}
		}
		CopyToSkynormals (i + 1, numpoints, points, numedges, edges, numtriangles, triangles);
	}
	free (points);
	free (edges);
	free (triangles);
}
static void     GatherSampleLight(const vec3_t pos, const byte* const pvs, const vec3_t normal, vec3_t* sample
								  , byte* styles
								  , int step
								  , int miptex
								  , int texlightgap_surfacenum
								  )
{
    int             i;
    directlight_t*  l;
    vec3_t          delta;
    float           dot, dot2;
    float           dist;
    float           ratio;
#ifdef HLRAD_OPACITY // AJM
    float           l_opacity;
#endif
    int             style_index;
	int				step_match;
	bool			sky_used = false;
	vec3_t			testline_origin;
	vec3_t			adds[ALLSTYLES];
	int				style;
	memset (adds, 0, ALLSTYLES * sizeof(vec3_t));
	bool			lighting_diversify;
	vec_t			lighting_power;
	vec_t			lighting_scale;
	lighting_power = g_lightingconeinfo[miptex][0];
	lighting_scale = g_lightingconeinfo[miptex][1];
	lighting_diversify = (lighting_power != 1.0 || lighting_scale != 1.0);
	vec3_t			texlightgap_textoworld[2];
	// calculates textoworld
	{
		dface_t *f = &g_dfaces[texlightgap_surfacenum];
		const dplane_t *dp = getPlaneFromFace (f);
		texinfo_t *tex = &g_texinfo[f->texinfo];
		int x;
		vec_t len;

		for (x = 0; x < 2; x++)
		{
			CrossProduct (tex->vecs[1 - x], dp->normal, texlightgap_textoworld[x]);
			len = DotProduct (texlightgap_textoworld[x], tex->vecs[x]);
			if (fabs (len) < NORMAL_EPSILON)
			{
				VectorClear (texlightgap_textoworld[x]);
			}
			else
			{
				VectorScale (texlightgap_textoworld[x], 1 / len, texlightgap_textoworld[x]);
			}
		}
	}

    for (i = 0; i < 1 + g_dmodels[0].visleafs; i++)
    {
        l = directlights[i];
        if (l)
		{
            if (i == 0? g_sky_lighting_fix: pvs[(i - 1) >> 3] & (1 << ((i - 1) & 7)))
            {
                for (; l; l = l->next)
                {
                    // skylights work fundamentally differently than normal lights
                    if (l->type == emit_skylight)
                    {
						if (!g_sky_lighting_fix)
						{
							if (sky_used)
							{
								continue;
							}
							sky_used = true;
						}
						do // add sun light
						{
							// check step
							step_match = (int)l->topatch;
							if (step != step_match)
								continue;
							// check intensity
							if (!(l->intensity[0] || l->intensity[1] || l->intensity[2]))
								continue;
						  // loop over the normals
						  for (int j = 0; j < l->numsunnormals; j++)
						  {
							// make sure the angle is okay
							dot = -DotProduct (normal, l->sunnormals[j]);
							if (dot <= NORMAL_EPSILON) //ON_EPSILON / 10 //--vluzacn
							{
								continue;
							}

							// search back to see if we can hit a sky brush
							VectorScale (l->sunnormals[j], -BOGUS_RANGE, delta);
							VectorAdd(pos, delta, delta);
							vec3_t skyhit;
							VectorCopy (delta, skyhit);
							if (TestLine(pos, delta
								, skyhit
								) != CONTENTS_SKY)
							{
								continue;                      // occluded
							}

							vec3_t transparency;
							int opaquestyle;
							if (TestSegmentAgainstOpaqueList(pos, 
								skyhit
								, transparency
								, opaquestyle
								))
							{
								continue;
							}

							vec3_t add_one;
							if (lighting_diversify)
							{
								dot = lighting_scale * pow (dot, lighting_power);
							}
							VectorScale (l->intensity, dot * l->sunnormalweights[j], add_one);
							VectorMultiply(add_one, transparency, add_one);
							// add to the total brightness of this sample
							style = l->style;
							if (opaquestyle != -1)
							{
								if (style == 0 || style == opaquestyle)
									style = opaquestyle;
								else
									continue; // dynamic light of other styles hits this toggleable opaque entity, then it completely vanishes.
							}
							VectorAdd (adds[style], add_one, adds[style]);
						  } // (loop over the normals)
						}
						while (0);
						do // add sky light
						{
							// check step
							step_match = 0;
							if (g_softsky)
								step_match = 1;
							if (g_fastmode)
								step_match = 1;
							if (step != step_match)
								continue;
							// check intensity
							if (g_indirect_sun <= 0.0 ||
								VectorCompare (
									l->diffuse_intensity,
									vec3_origin)
								&& VectorCompare (l->diffuse_intensity2, vec3_origin)
								)
								continue;

							vec3_t sky_intensity;

							// loop over the normals
							vec3_t *skynormals = g_skynormals[g_softsky?SKYLEVEL_SOFTSKYON:SKYLEVEL_SOFTSKYOFF];
							vec_t *skyweights = g_skynormalsizes[g_softsky?SKYLEVEL_SOFTSKYON:SKYLEVEL_SOFTSKYOFF];
							for (int j = 0; j < g_numskynormals[g_softsky?SKYLEVEL_SOFTSKYON:SKYLEVEL_SOFTSKYOFF]; j++)
							{
								// make sure the angle is okay
								dot = -DotProduct (normal, skynormals[j]);
								if (dot <= NORMAL_EPSILON) //ON_EPSILON / 10 //--vluzacn
								{
									continue;
								}

								// search back to see if we can hit a sky brush
								VectorScale (skynormals[j], -BOGUS_RANGE, delta);
								VectorAdd(pos, delta, delta);
								vec3_t skyhit;
								VectorCopy (delta, skyhit);
								if (TestLine(pos, delta
									, skyhit
									) != CONTENTS_SKY)
								{
									continue;                                  // occluded
								}

								vec3_t transparency;
								int opaquestyle;
								if (TestSegmentAgainstOpaqueList(pos, 
									skyhit
									, transparency
									, opaquestyle
									))
								{
									continue;
								}

								vec_t factor = qmin (qmax (0.0, (1 - DotProduct (l->normal, skynormals[j])) / 2), 1.0); // how far this piece of sky has deviated from the sun
								VectorScale (l->diffuse_intensity, 1 - factor, sky_intensity);
								VectorMA (sky_intensity, factor, l->diffuse_intensity2, sky_intensity);
								VectorScale (sky_intensity, skyweights[j] * g_indirect_sun / 2, sky_intensity);
								vec3_t add_one;
								if (lighting_diversify)
								{
									dot = lighting_scale * pow (dot, lighting_power);
								}
								VectorScale(sky_intensity, dot, add_one);
								VectorMultiply(add_one, transparency, add_one);
								// add to the total brightness of this sample
								style = l->style;
								if (opaquestyle != -1)
								{
									if (style == 0 || style == opaquestyle)
										style = opaquestyle;
									else
										continue; // dynamic light of other styles hits this toggleable opaque entity, then it completely vanishes.
								}
								VectorAdd (adds[style], add_one, adds[style]);
							} // (loop over the normals)

						}
						while (0);

                    }
                    else // not emit_skylight
                    {
						step_match = (int)l->topatch;
						if (step != step_match)
							continue;
						if (!(l->intensity[0] || l->intensity[1] || l->intensity[2]))
							continue;
						VectorCopy (l->origin, testline_origin);
                        float           denominator;

                        VectorSubtract(l->origin, pos, delta);
						if (l->type == emit_surface)
						{
							// move emitter back to its plane
							VectorMA (delta, -PATCH_HUNT_OFFSET, l->normal, delta);
						}
                        dist = VectorNormalize(delta);
                        dot = DotProduct(delta, normal);
                        //                        if (dot <= 0.0)
                        //                            continue;

                        if (dist < 1.0)
                        {
                            dist = 1.0;
                        }

						denominator = dist * dist * l->fade;

						vec3_t add;
                        switch (l->type)
                        {
                        case emit_point:
                        {
							if (dot <= NORMAL_EPSILON)
							{
								continue;
							}
							vec_t denominator = dist * dist * l->fade;
							if (lighting_diversify)
							{
								dot = lighting_scale * pow (dot, lighting_power);
							}
                            ratio = dot / denominator;
                            VectorScale(l->intensity, ratio, add);
                            break;
                        }

                        case emit_surface:
                        {
							bool light_behind_surface = false;
							if (dot <= NORMAL_EPSILON)
							{
								light_behind_surface = true;
							}
							if (lighting_diversify
								&& !light_behind_surface
								)
							{
								dot = lighting_scale * pow (dot, lighting_power);
							}
                            dot2 = -DotProduct(delta, l->normal);
							// discard the texlight if the spot is too close to the texlight plane
							if (l->texlightgap > 0)
							{
								vec_t test;

								test = dot2 * dist; // distance from spot to texlight plane;
								test -= l->texlightgap * fabs (DotProduct (l->normal, texlightgap_textoworld[0])); // maximum distance reduction if the spot is allowed to shift l->texlightgap pixels along s axis
								test -= l->texlightgap * fabs (DotProduct (l->normal, texlightgap_textoworld[1])); // maximum distance reduction if the spot is allowed to shift l->texlightgap pixels along t axis
								if (test < -ON_EPSILON)
								{
									continue;
								}
							}
							if (dot2 * dist <= MINIMUM_PATCH_DISTANCE)
							{
								continue;
							}
							vec_t range = l->patch_emitter_range;
							if (l->stopdot > 0.0) // stopdot2 > 0.0 or stopdot > 0.0
							{
								vec_t range_scale;
								range_scale = 1 - l->stopdot2 * l->stopdot2;
								range_scale = 1 / sqrt (qmax (NORMAL_EPSILON, range_scale));
								// range_scale = 1 / sin (cone2)
								range_scale = qmin (range_scale, 2); // restrict this to 2, because skylevel has limit.
								range *= range_scale; // because smaller cones are more likely to create the ugly grid effect.

								if (dot2 <= l->stopdot2 + NORMAL_EPSILON)
								{
									if (dist >= range) // use the old method, which will merely give 0 in this case
									{
										continue;
									}
									ratio = 0.0;
								}
								else if (dot2 <= l->stopdot)
								{
									ratio = dot * dot2 * (dot2 - l->stopdot2) / (dist * dist * (l->stopdot - l->stopdot2));
								}
								else
								{
									ratio = dot * dot2 / (dist * dist);
								}
							}
							else
							{
								ratio = dot * dot2 / (dist * dist);
							}
							
							// analogous to the one in MakeScales
							// 0.4f is tested to be able to fully eliminate bright spots
							if (ratio * l->patch_area > 0.4f)
							{
								ratio = 0.4f / l->patch_area;
							}
							if (dist < range - ON_EPSILON)
							{ // do things slow
								if (light_behind_surface)
								{
									dot = 0.0;
									ratio = 0.0;
								}
								GetAlternateOrigin (pos, normal, l->patch, testline_origin);
								vec_t sightarea;
								int skylevel = l->patch->emitter_skylevel;
								if (l->stopdot > 0.0) // stopdot2 > 0.0 or stopdot > 0.0
								{
									const vec_t *emitnormal = getPlaneFromFaceNumber (l->patch->faceNumber)->normal;
									if (l->stopdot2 >= 0.8) // about 37deg
									{
										skylevel += 1; // because the range is larger
									}
									sightarea = CalcSightArea_SpotLight (pos, normal, l->patch->winding, emitnormal, l->stopdot, l->stopdot2, skylevel
										, lighting_power, lighting_scale
										); // because we have doubled the range
								}
								else
								{
									sightarea = CalcSightArea (pos, normal, l->patch->winding, skylevel
										, lighting_power, lighting_scale
										);
								}

								vec_t frac = dist / range;
								frac = (frac - 0.5) * 2; // make a smooth transition between the two methods
								frac = qmax (0, qmin (frac, 1));

								vec_t ratio2 = (sightarea / l->patch_area); // because l->patch->area has been multiplied into l->intensity
								ratio = frac * ratio + (1 - frac) * ratio2;
							}
							else
							{
								if (light_behind_surface)
								{
									continue;
								}
							}
                            VectorScale(l->intensity, ratio, add);
                            break;
                        }

                        case emit_spotlight:
                        {
							if (dot <= NORMAL_EPSILON)
							{
								continue;
							}
                            dot2 = -DotProduct(delta, l->normal);
                            if (dot2 <= l->stopdot2)
                            {
                                continue;                  // outside light cone
                            }

                            // Variable power falloff (1 = inverse linear, 2 = inverse square
                            vec_t           denominator = dist * l->fade;
                            {
                                denominator *= dist;
                            }
							if (lighting_diversify)
							{
								dot = lighting_scale * pow (dot, lighting_power);
							}
                            ratio = dot * dot2 / denominator;

                            if (dot2 <= l->stopdot)
                            {
                                ratio *= (dot2 - l->stopdot2) / (l->stopdot - l->stopdot2);
                            }
                            VectorScale(l->intensity, ratio, add);
                            break;
                        }

                        default:
                        {
                            hlassume(false, assume_BadLightType);
                            break;
                        }
                        }
						if (TestLine (pos, 
							testline_origin
							) != CONTENTS_EMPTY)
						{
							continue;
						}
						vec3_t transparency;
						int opaquestyle;
						if (TestSegmentAgainstOpaqueList (pos, 
							testline_origin
							, transparency
							, opaquestyle))
						{
							continue;
						}
						VectorMultiply (add, transparency, add);
						// add to the total brightness of this sample
						style = l->style;
						if (opaquestyle != -1)
						{
							if (style == 0 || style == opaquestyle)
								style = opaquestyle;
							else
								continue; // dynamic light of other styles hits this toggleable opaque entity, then it completely vanishes.
						}
						VectorAdd (adds[style], add, adds[style]);
                    } // end emit_skylight

                }
            }
        }
    }

	for (style = 0; style < ALLSTYLES; ++style)
	{
		if (VectorMaximum(adds[style]) > g_corings[style] * 0.1)
		{
			for (style_index = 0; style_index < ALLSTYLES; style_index++)
			{
				if (styles[style_index] == style || styles[style_index] == 255)
				{
					break;
				}
			}

			if (style_index == ALLSTYLES) // shouldn't happen
			{
				if (++stylewarningcount >= stylewarningnext)
				{
					stylewarningnext = stylewarningcount * 2;
					Warning("Too many direct light styles on a face(%f,%f,%f)", pos[0], pos[1], pos[2]);
					Warning(" total %d warnings for too many styles", stylewarningcount);
				}
				return;
			}

			if (styles[style_index] == 255)
			{
				styles[style_index] = style;
			}

			VectorAdd(sample[style_index], adds[style], sample[style_index]);
		}
		else
		{
			if (VectorMaximum (adds[style]) > g_maxdiscardedlight + NORMAL_EPSILON)
			{
				ThreadLock ();
				if (VectorMaximum (adds[style]) > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = VectorMaximum (adds[style]);
					VectorCopy (pos, g_maxdiscardedpos);
				}
				ThreadUnlock ();
			}
		}
	}
}

// =====================================================================================
//  AddSampleToPatch
//      Take the sample's collected light and add it back into the apropriate patch for the radiosity pass.
// =====================================================================================
static void AddSamplesToPatches (const sample_t **samples, const unsigned char *styles, int facenum, const lightinfo_t *l)
{
	patch_t *patch;
	int i, j, m, k;
	int numtexwindings;
	Winding **texwindings;

	numtexwindings = 0;
	for (patch = g_face_patches[facenum]; patch; patch = patch->next)
	{
		numtexwindings++;
	}
	texwindings = (Winding **)malloc (numtexwindings * sizeof (Winding *));
	hlassume (texwindings != NULL, assume_NoMemory);

	// translate world winding into winding in s,t plane
	for (j = 0, patch = g_face_patches[facenum]; j < numtexwindings; j++, patch = patch->next)
	{
		Winding *w = new Winding (patch->winding->m_NumPoints);
		for (int x = 0; x < w->m_NumPoints; x++)
		{
			vec_t s, t;
			SetSTFromSurf (l, patch->winding->m_Points[x], s, t);
			w->m_Points[x][0] = s;
			w->m_Points[x][1] = t;
			w->m_Points[x][2] = 0.0;
		}
		w->RemoveColinearPoints ();
		texwindings[j] = w;
	}
	
	for (i = 0; i < l->numsurfpt; i++)
	{
		// prepare clip planes
		vec_t s_vec, t_vec;
		s_vec = l->texmins[0] * TEXTURE_STEP + (i % (l->texsize[0] + 1)) * TEXTURE_STEP;
		t_vec = l->texmins[1] * TEXTURE_STEP + (i / (l->texsize[0] + 1)) * TEXTURE_STEP;

		dplane_t clipplanes[4];
		VectorClear (clipplanes[0].normal);
		clipplanes[0].normal[0] = 1;
		clipplanes[0].dist = s_vec - 0.5 * TEXTURE_STEP;
		VectorClear (clipplanes[1].normal);
		clipplanes[1].normal[0] = -1;
		clipplanes[1].dist = -(s_vec + 0.5 * TEXTURE_STEP);
		VectorClear (clipplanes[2].normal);
		clipplanes[2].normal[1] = 1;
		clipplanes[2].dist = t_vec - 0.5 * TEXTURE_STEP;
		VectorClear (clipplanes[3].normal);
		clipplanes[3].normal[1] = -1;
		clipplanes[3].dist = -(t_vec + 0.5 * TEXTURE_STEP);

		// clip each patch
		for (j = 0, patch = g_face_patches[facenum]; j < numtexwindings; j++, patch = patch->next)
		{
			Winding *w = new Winding (*texwindings[j]);
			for (k = 0; k < 4; k++)
			{
				if (w->m_NumPoints)
				{
					w->Clip (clipplanes[k], false);
				}
			}
			if (w->m_NumPoints)
			{
				// add sample to patch
				vec_t area = w->getArea () / (TEXTURE_STEP * TEXTURE_STEP);
				patch->samples += area;
				for (m = 0; m < ALLSTYLES && styles[m] != 255; m++)
				{
					int style = styles[m];
					const sample_t *s = &samples[m][i];
					for (k = 0; k < ALLSTYLES && patch->totalstyle_all[k] != 255; k++)
					{
						if (patch->totalstyle_all[k] == style)
						{
							break;
						}
					}
					if (k == ALLSTYLES)
					{
						if (++stylewarningcount >= stylewarningnext)
						{
							stylewarningnext = stylewarningcount * 2;
							Warning("Too many direct light styles on a face(?,?,?)\n");
							Warning(" total %d warnings for too many styles", stylewarningcount);
						}
					}
					else
					{
						if (patch->totalstyle_all[k] == 255)
						{
							patch->totalstyle_all[k] = style;
						}
						VectorMA (patch->samplelight_all[k], area, s->light, patch->samplelight_all[k]);
					}
				}
			}
			delete w;
		}
	}

	for (j = 0; j < numtexwindings; j++)
	{
		delete texwindings[j];
	}
	free (texwindings);
}

// =====================================================================================
//  GetPhongNormal
// =====================================================================================
void            GetPhongNormal(int facenum, const vec3_t spot, vec3_t phongnormal)
{
    int             j;
	int				s; // split every edge into two parts
    const dface_t*  f = g_dfaces + facenum;
    const dplane_t* p = getPlaneFromFace(f);
    vec3_t          facenormal;

    VectorCopy(p->normal, facenormal);
    VectorCopy(facenormal, phongnormal);

    {
        // Calculate modified point normal for surface
        // Use the edge normals iff they are defined.  Bend the surface towards the edge normal(s)
        // Crude first attempt: find nearest edge normal and do a simple interpolation with facenormal.
        // Second attempt: find edge points+center that bound the point and do a three-point triangulation(baricentric)
        // Better third attempt: generate the point normals for all vertices and do baricentric triangulation.

        for (j = 0; j < f->numedges; j++)
        {
            vec3_t          p1;
            vec3_t          p2;
            vec3_t          v1;
            vec3_t          v2;
            vec3_t          vspot;
            unsigned        prev_edge;
            unsigned        next_edge;
            int             e;
            int             e1;
            int             e2;
            edgeshare_t*    es;
            edgeshare_t*    es1;
            edgeshare_t*    es2;
            float           a1;
            float           a2;
            float           aa;
            float           bb;
            float           ab;

            if (j)
            {
                prev_edge = f->firstedge + ((j + f->numedges - 1) % f->numedges);
            }
            else
            {
                prev_edge = f->firstedge + f->numedges - 1;
            }

            if ((j + 1) != f->numedges)
            {
                next_edge = f->firstedge + ((j + 1) % f->numedges);
            }
            else
            {
                next_edge = f->firstedge;
            }

            e = g_dsurfedges[f->firstedge + j];
            e1 = g_dsurfedges[prev_edge];
            e2 = g_dsurfedges[next_edge];

            es = &g_edgeshare[abs(e)];
            es1 = &g_edgeshare[abs(e1)];
            es2 = &g_edgeshare[abs(e2)];

			if ((!es->smooth || es->coplanar) && (!es1->smooth || es1->coplanar) && (!es2->smooth || es2->coplanar))
            {
                continue;
            }

            if (e > 0)
            {
                VectorCopy(g_dvertexes[g_dedges[e].v[0]].point, p1);
                VectorCopy(g_dvertexes[g_dedges[e].v[1]].point, p2);
            }
            else
            {
                VectorCopy(g_dvertexes[g_dedges[-e].v[1]].point, p1);
                VectorCopy(g_dvertexes[g_dedges[-e].v[0]].point, p2);
            }

            // Adjust for origin-based models
            VectorAdd(p1, g_face_offset[facenum], p1);
            VectorAdd(p2, g_face_offset[facenum], p2);
		for (s = 0; s < 2; s++)
		{
			vec3_t s1, s2;
			if (s == 0)
			{
				VectorCopy(p1, s1);
			}
			else
			{
				VectorCopy(p2, s1);
			}

			VectorAdd(p1,p2,s2); // edge center
			VectorScale(s2,0.5,s2);

            VectorSubtract(s1, g_face_centroids[facenum], v1);
            VectorSubtract(s2, g_face_centroids[facenum], v2);
            VectorSubtract(spot, g_face_centroids[facenum], vspot);

            aa = DotProduct(v1, v1);
            bb = DotProduct(v2, v2);
            ab = DotProduct(v1, v2);
            a1 = (bb * DotProduct(v1, vspot) - ab * DotProduct(vspot, v2)) / (aa * bb - ab * ab);
            a2 = (DotProduct(vspot, v2) - a1 * ab) / bb;

            // Test center to sample vector for inclusion between center to vertex vectors (Use dot product of vectors)
            if (a1 >= -0.01 && a2 >= -0.01)
            {
                // calculate distance from edge to pos
                vec3_t          n1, n2;
                vec3_t          temp;

				if (es->smooth)
					if (s == 0)
					{VectorCopy(es->vertex_normal[e>0?0:1], n1);}
					else
					{VectorCopy(es->vertex_normal[e>0?1:0], n1);}
				else if (s == 0 && es1->smooth)
				{VectorCopy(es1->vertex_normal[e1>0?1:0], n1);}
				else if (s == 1 && es2->smooth)
				{VectorCopy(es2->vertex_normal[e2>0?0:1], n1);}
				else
				{VectorCopy(facenormal, n1);}

				if (es->smooth)
				{VectorCopy(es->interface_normal, n2);}
				else
				{VectorCopy(facenormal, n2);}

                // Interpolate between the center and edge normals based on sample position
                VectorScale(facenormal, 1.0 - a1 - a2, phongnormal);
                VectorScale(n1, a1, temp);
                VectorAdd(phongnormal, temp, phongnormal);
                VectorScale(n2, a2, temp);
                VectorAdd(phongnormal, temp, phongnormal);
                VectorNormalize(phongnormal);
                break;
            }
		} // s=0,1
        }
    }
}

const vec3_t    s_circuscolors[] = {
    {100000.0,  100000.0,   100000.0},                              // white
    {100000.0,  0.0,        0.0     },                              // red
    {0.0,       100000.0,   0.0     },                              // green
    {0.0,       0.0,        100000.0},                              // blue
    {0.0,       100000.0,   100000.0},                              // cyan
    {100000.0,  0.0,        100000.0},                              // magenta
    {100000.0,  100000.0,   0.0     }                               // yellow
};

// =====================================================================================
//  BuildFacelights
// =====================================================================================
void CalcLightmap (lightinfo_t *l, byte *styles)
{
	int facenum;
	int i, j;
	byte pvs[(MAX_MAP_LEAFS + 7) / 8];
	int lastoffset;
	byte pvs2[(MAX_MAP_LEAFS + 7) / 8];
	int lastoffset2;

	facenum = l->surfnum;
	memset (l->lmcache, 0, l->lmcachewidth * l->lmcacheheight * sizeof (vec3_t [ALLSTYLES]));

	// for each sample whose light we need to calculate
	for (i = 0; i < l->lmcachewidth * l->lmcacheheight; i++)
	{
		vec_t s, t;
		vec_t s_vec, t_vec;
		int nearest_s, nearest_t;
		vec3_t spot;
		vec_t square[2][2];  // the max possible range in which this sample point affects the lighting on a face
		vec3_t surfpt; // the point on the surface (with no HUNT_OFFSET applied), used for getting phong normal and doing patch interpolation
		int surface;
		vec3_t pointnormal;
		bool blocked;
		vec3_t spot2;
		vec3_t pointnormal2;
		vec3_t *sampled;
		vec3_t *normal_out;
		bool nudged;
		int *wallflags_out;

		// prepare input parameter and output parameter
		{
			s = ((i % l->lmcachewidth) - l->lmcache_offset) / (vec_t)l->lmcache_density;
			t = ((i / l->lmcachewidth) - l->lmcache_offset) / (vec_t)l->lmcache_density;
			s_vec = l->texmins[0] * TEXTURE_STEP + s * TEXTURE_STEP;
			t_vec = l->texmins[1] * TEXTURE_STEP + t * TEXTURE_STEP;
			nearest_s = qmax (0, qmin ((int)floor (s + 0.5), l->texsize[0]));
			nearest_t = qmax (0, qmin ((int)floor (t + 0.5), l->texsize[1]));
			sampled = l->lmcache[i];
			normal_out = &l->lmcache_normal[i];
			wallflags_out = &l->lmcache_wallflags[i];
//
// The following graph illustrates the range in which a sample point can affect the lighting of a face when g_blur = 1.5 and g_extra = on
//              X : the sample point. They are placed on every TEXTURE_STEP/lmcache_density (=16.0/3) texture pixels. We calculate light for each sample point, which is the main time sink.
//              + : the lightmap pixel. They are placed on every TEXTURE_STEP (=16.0) texture pixels, which is hard coded inside the GoldSrc engine. Their brightness are averaged from the sample points in a square with size g_blur*TEXTURE_STEP.
//              o : indicates that this lightmap pixel is affected by the sample point 'X'. The higher g_blur, the more 'o'.
//       |/ / / | : indicates that the brightness of this area is affected by the lightmap pixels 'o' and hence by the sample point 'X'. This is because the engine uses bilinear interpolation to display the lightmap.
//
//    ==============================================================================================================================================
//    || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + ||
//    ||                                 ||                                 ||                                 ||                                 ||
//    ||                                 ||                                 ||                                 ||                                 ||
//    || +     +-----+-----+     +     + || +     +-----+-----+-----+     + || +     +-----+-----+-----+     + || +     +     +-----+-----+     + ||
//    ||       | / / / / / |             ||       | / / / / / / / / |       ||       | / / / / / / / / |       ||             | / / / / / |       ||
//    ||       |/ / / / / /|             ||       |/ / / / / / / / /|       ||       |/ / / / / / / / /|       ||             |/ / / / / /|       ||
//    || +     + / / X / / +     +     + || +     + / / o X / o / / +     + || +     + / / o / X o / / +     + || +     +     + / / X / / +     + ||
//    ||       |/ / / / / /|             ||       |/ / / / / / / / /|       ||       |/ / / / / / / / /|       ||             |/ / / / / /|       ||
//    ||       | / / / / / |             ||       | / / / / / / / / |       ||       | / / / / / / / / |       ||             | / / / / / |       ||
//    || +     +-----+-----+     +     + || +     +-----+-----+-----+     + || +     +-----+-----+-----+     + || +     +     +-----+-----+     + ||
//    ||                                 ||                                 ||                                 ||                                 ||
//    ||                                 ||                                 ||                                 ||                                 ||
//    || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + ||
//    ==============================================================================================================================================
//    || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + ||
//    ||                                 ||                                 ||                                 ||                                 ||
//    ||                                 ||                                 ||                                 ||                                 ||
//    || +     +-----+-----+     +     + || +     +-----+-----+-----+     + || +     +-----+-----+-----+     + || +     +     +-----+-----+     + ||
//    ||       | / / / / / |             ||       | / / / / / / / / |       ||       | / / / / / / / / |       ||             | / / / / / |       ||
//    ||       |/ / / / / /|             ||       |/ / / / / / / / /|       ||       |/ / / / / / / / /|       ||             |/ / / / / /|       ||
//    || +     + / / o / / +     +     + || +     + / / o / / o / / +     + || +     + / / o / / o / / +     + || +     +     + / / o / / +     + ||
//    ||       |/ / /X/ / /|             ||       |/ / / /X/ / / / /|       ||       |/ / / / /X/ / / /|       ||             |/ / /X/ / /|       ||
//    ||       | / / / / / |             ||       | / / / / / / / / |       ||       | / / / / / / / / |       ||             | / / / / / |       ||
//    || +     +/ / /o/ / /+     +     + || +     +/ / /o/ / /o/ / /+     + || +     +/ / /o/ / /o/ / /+     + || +     +     +/ / /o/ / /+     + ||
//    ||       | / / / / / |             ||       | / / / / / / / / |       ||       | / / / / / / / / |       ||             | / / / / / |       ||
//    ||       |/ / / / / /|             ||       |/ / / / / / / / /|       ||       |/ / / / / / / / /|       ||             |/ / / / / /|       ||
//    || +     +-----+-----+     +     + || +     +-----+-----+-----+     + || +     +-----+-----+-----+     + || +     +     +-----+-----+     + ||
//    ==============================================================================================================================================
//
			square[0][0] = l->texmins[0] * TEXTURE_STEP + ceil (s - (l->lmcache_side + 0.5) / (vec_t)l->lmcache_density) * TEXTURE_STEP - TEXTURE_STEP;
			square[0][1] = l->texmins[1] * TEXTURE_STEP + ceil (t - (l->lmcache_side + 0.5) / (vec_t)l->lmcache_density) * TEXTURE_STEP - TEXTURE_STEP;
			square[1][0] = l->texmins[0] * TEXTURE_STEP + floor (s + (l->lmcache_side + 0.5) / (vec_t)l->lmcache_density) * TEXTURE_STEP + TEXTURE_STEP;
			square[1][1] = l->texmins[1] * TEXTURE_STEP + floor (t + (l->lmcache_side + 0.5) / (vec_t)l->lmcache_density) * TEXTURE_STEP + TEXTURE_STEP;
		}
		// find world's position for the sample
		{
			{
				blocked = false;
				if (SetSampleFromST (
									surfpt, spot, &surface,
									&nudged,
									l, s_vec, t_vec,
									square,
									g_face_lightmode[facenum]) == LightOutside)
				{
					j = nearest_s + (l->texsize[0] + 1) * nearest_t;
					if (l->surfpt_lightoutside[j])
					{
						blocked = true;
					}
					else
					{
						// the area this light sample has effect on is completely covered by solid, so take whatever valid position.
						VectorCopy (l->surfpt[j], surfpt);
						VectorCopy (l->surfpt_position[j], spot);
						surface = l->surfpt_surface[j];
					}
				}
			}
			if (l->translucent_b)
			{
				const dplane_t *surfaceplane = getPlaneFromFaceNumber (surface);
				Winding *surfacewinding = new Winding (g_dfaces[surface]);
				
				VectorCopy (spot, spot2);
				for (int x = 0; x < surfacewinding->m_NumPoints; x++)
				{
					VectorAdd (surfacewinding->m_Points[x], g_face_offset[surface], surfacewinding->m_Points[x]);
				}
				if (!point_in_winding_noedge (*surfacewinding, *surfaceplane, spot2, 0.2))
				{
					snap_to_winding_noedge (*surfacewinding, *surfaceplane, spot2, 0.2, 4 * 0.2);
				}
				VectorMA (spot2, -(g_translucentdepth + 2 * DEFAULT_HUNT_OFFSET), surfaceplane->normal, spot2);

				delete surfacewinding;
			}
			*wallflags_out = WALLFLAG_NONE;
			if (blocked)
			{
				*wallflags_out |= (WALLFLAG_BLOCKED | WALLFLAG_NUDGED);
			}
			if (nudged)
			{
				*wallflags_out |= WALLFLAG_NUDGED;
			}
		}
		// calculate normal for the sample
		{
			GetPhongNormal (surface, surfpt, pointnormal);
			if (l->translucent_b)
			{
				VectorSubtract (vec3_origin, pointnormal, pointnormal2);
			}
			VectorCopy (pointnormal, *normal_out);
		}
		// calculate visibility for the sample
		{
			if (!g_visdatasize)
			{
				if (i == 0)
				{
					memset (pvs, 255, (g_dmodels[0].visleafs + 7) / 8);
				}
			}
			else
			{
				dleaf_t *leaf = PointInLeaf(spot);
				int thisoffset = leaf->visofs;
				if (i == 0 || thisoffset != lastoffset)
				{
					if (thisoffset == -1)
					{
						memset (pvs, 0, (g_dmodels[0].visleafs + 7) / 8);
					}
					else
					{
						DecompressVis(&g_dvisdata[leaf->visofs], pvs, sizeof(pvs));
					}
				}
				lastoffset = thisoffset;
			}
			if (l->translucent_b)
			{
				if (!g_visdatasize)
				{
					if (i == 0)
					{
						memset (pvs2, 255, (g_dmodels[0].visleafs + 7) / 8);
					}
				}
				else
				{
					dleaf_t *leaf2 = PointInLeaf(spot2);
					int thisoffset2 = leaf2->visofs;
					if (i == 0 || thisoffset2 != lastoffset2)
					{
						if (thisoffset2 == -1)
						{
							memset (pvs2, 0, (g_dmodels[0].visleafs + 7) / 8);
						}
						else
						{
							DecompressVis(&g_dvisdata[leaf2->visofs], pvs2, sizeof(pvs2));
						}
					}
					lastoffset2 = thisoffset2;
				}
			}
		}
		// gather light
		{
			if (!blocked)
			{
				GatherSampleLight(spot, pvs, pointnormal, sampled
					, styles
					, 0
					, l->miptex
					, surface
					);
			}
			if (l->translucent_b)
			{
				vec3_t sampled2[ALLSTYLES];
				memset (sampled2, 0, ALLSTYLES * sizeof (vec3_t));
				if (!blocked)
				{
					GatherSampleLight(spot2, pvs2, pointnormal2, sampled2
						, styles
						, 0
						, l->miptex
						, surface
						);
				}
				for (j = 0; j < ALLSTYLES && styles[j] != 255; j++)
				{
					for (int x = 0; x < 3; x++)
					{
						sampled[j][x] = (1.0 - l->translucent_v[x]) * sampled[j][x] + l->translucent_v[x] * sampled2[j][x];
					}
				}
			}
			if (g_drawnudge)
			{
				for (j = 0; j < ALLSTYLES && styles[j] != 255; j++)
				{
					if (blocked && styles[j] == 0)
					{
						sampled[j][0] = 200;
						sampled[j][1] = 0;
						sampled[j][2] = 0;
					}
					else if (nudged && styles[j] == 0) // we assume style 0 is always present
					{
						VectorFill (sampled[j], 100);
					}
					else
					{
						VectorClear (sampled[j]);
					}
				}
			}
		}
	}
}
void            BuildFacelights(const int facenum)
{
    dface_t*        f;
	unsigned char	f_styles[ALLSTYLES];
	sample_t		*fl_samples[ALLSTYLES];
    lightinfo_t     l;
    int             i;
    int             j;
    int             k;
    sample_t*       s;
    vec_t*          spot;
    patch_t*        patch;
    const dplane_t* plane;
    byte            pvs[(MAX_MAP_LEAFS + 7) / 8];
    int             thisoffset = -1, lastoffset = -1;
    int             lightmapwidth;
    int             lightmapheight;
    int             size;
	vec3_t			spot2, normal2;
	vec3_t			delta;
	byte			pvs2[(MAX_MAP_LEAFS + 7) / 8];
	int				thisoffset2 = -1, lastoffset2 = -1;

	int				*sample_wallflags;

    f = &g_dfaces[facenum];

    //
    // some surfaces don't need lightmaps
    //
    f->lightofs = -1;
    for (j = 0; j < ALLSTYLES; j++)
    {
        f_styles[j] = 255;
    }

    if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
    {
		for (j = 0; j < MAXLIGHTMAPS; j++)
		{
			f->styles[j] = 255;
		}
        return;                                            // non-lit texture
    }

	f_styles[0] = 0;
	if (g_face_patches[facenum] && g_face_patches[facenum]->emitstyle)
	{
		f_styles[1] = g_face_patches[facenum]->emitstyle;
	}

    memset(&l, 0, sizeof(l));

    l.surfnum = facenum;
    l.face = f;

	VectorCopy (g_translucenttextures[g_texinfo[f->texinfo].miptex], l.translucent_v);
	l.translucent_b = !VectorCompare (l.translucent_v, vec3_origin);
	l.miptex = g_texinfo[f->texinfo].miptex;

    //
    // rotate plane
    //
    plane = getPlaneFromFace(f);
    VectorCopy(plane->normal, l.facenormal);
    l.facedist = plane->dist;

    CalcFaceVectors(&l);
    CalcFaceExtents(&l);
    CalcPoints(&l);
	CalcLightmap (&l
		, f_styles
		);

    lightmapwidth = l.texsize[0] + 1;
    lightmapheight = l.texsize[1] + 1;

    size = lightmapwidth * lightmapheight;
    hlassume(size <= MAX_SINGLEMAP, assume_MAX_SINGLEMAP);

    facelight[facenum].numsamples = l.numsurfpt;

	for (k = 0; k < ALLSTYLES; k++)
	{
		fl_samples[k] = (sample_t *)calloc (l.numsurfpt, sizeof(sample_t));
		hlassume (fl_samples[k] != NULL, assume_NoMemory);
	}
	for (patch = g_face_patches[facenum]; patch; patch = patch->next)
	{
		hlassume (patch->totalstyle_all = (unsigned char *)malloc (ALLSTYLES * sizeof (unsigned char)), assume_NoMemory);
		hlassume (patch->samplelight_all = (vec3_t *)malloc (ALLSTYLES * sizeof (vec3_t)), assume_NoMemory);
		hlassume (patch->totallight_all = (vec3_t *)malloc (ALLSTYLES * sizeof (vec3_t)), assume_NoMemory);
		hlassume (patch->directlight_all = (vec3_t *)malloc (ALLSTYLES * sizeof (vec3_t)), assume_NoMemory);
		for (j = 0; j < ALLSTYLES; j++)
		{
			patch->totalstyle_all[j] = 255;
			VectorClear (patch->samplelight_all[j]);
			VectorClear (patch->totallight_all[j]);
			VectorClear (patch->directlight_all[j]);
		}
		patch->totalstyle_all[0] = 0;
	}

	sample_wallflags = (int *)malloc ((2 * l.lmcache_side + 1) * (2 * l.lmcache_side + 1) * sizeof (int));
    spot = l.surfpt[0];
    for (i = 0; i < l.numsurfpt; i++, spot += 3)
    {

        for (k = 0; k < ALLSTYLES; k++)
        {
            VectorCopy(spot, fl_samples[k][i].pos);
			fl_samples[k][i].surface = l.surfpt_surface[i];
        }

		int s, t, pos;
		int s_center, t_center;
		vec_t sizehalf;
		vec_t weighting, subsamples;
		vec3_t centernormal;
		vec_t weighting_correction;
		int pass;
		s_center = (i % lightmapwidth) * l.lmcache_density + l.lmcache_offset;
		t_center = (i / lightmapwidth) * l.lmcache_density + l.lmcache_offset;
		sizehalf = 0.5 * g_blur * l.lmcache_density;
		subsamples = 0.0;
		VectorCopy (l.lmcache_normal[s_center + l.lmcachewidth * t_center], centernormal);
		if (g_bleedfix && !g_drawnudge)
		{
			int s_origin = s_center;
			int t_origin = t_center;
			for (s = s_center - l.lmcache_side; s <= s_center + l.lmcache_side; s++)
			{
				for (t = t_center - l.lmcache_side; t <= t_center + l.lmcache_side; t++)
				{
					int *pwallflags = &sample_wallflags[(s - s_center + l.lmcache_side) + (2 * l.lmcache_side + 1) * (t - t_center + l.lmcache_side)];
					*pwallflags = l.lmcache_wallflags[s + l.lmcachewidth * t];
				}
			}
			// project the "shadow" from the origin point
			for (s = s_center - l.lmcache_side; s <= s_center + l.lmcache_side; s++)
			{
				for (t = t_center - l.lmcache_side; t <= t_center + l.lmcache_side; t++)
				{
					int *pwallflags = &sample_wallflags[(s - s_center + l.lmcache_side) + (2 * l.lmcache_side + 1) * (t - t_center + l.lmcache_side)];
					int coord[2] = {s - s_origin, t - t_origin};
					int axis = abs(coord[0]) >= abs(coord[1])? 0: 1;
					int sign = coord[axis] >= 0? 1: -1;
					bool blocked1 = false;
					bool blocked2 = false;
					for (int dist = 1; dist < abs (coord[axis]); dist++)
					{
						int test1[2];
						int test2[2];
						test1[axis] = test2[axis] = sign * dist;
						double intercept = (double)coord[1-axis] * (double)test1[axis] / (double)coord[axis];
						test1[1-axis] = (int)floor (intercept + 0.01);
						test2[1-axis] = (int)ceil (intercept - 0.01);
						if (abs (test1[0] + s_origin - s_center) > l.lmcache_side || abs (test1[1] + t_origin - t_center) > l.lmcache_side ||
							abs (test2[0] + s_origin - s_center) > l.lmcache_side || abs (test2[1] + t_origin - t_center) > l.lmcache_side )
						{
							Warning ("HLRAD_AVOIDWALLBLEED: internal error. Contact vluzacn@163.com concerning this issue.");
							continue;
						}
						int wallflags1 = sample_wallflags[(test1[0] + s_origin - s_center + l.lmcache_side) + (2 * l.lmcache_side + 1) * (test1[1] + t_origin - t_center + l.lmcache_side)];
						int wallflags2 = sample_wallflags[(test2[0] + s_origin - s_center + l.lmcache_side) + (2 * l.lmcache_side + 1) * (test2[1] + t_origin - t_center + l.lmcache_side)];
						if (wallflags1 & WALLFLAG_NUDGED)
						{
							blocked1 = true;
						}
						if (wallflags2 & WALLFLAG_NUDGED)
						{
							blocked2 = true;
						}
					}
					if (blocked1 && blocked2)
					{
						*pwallflags |= WALLFLAG_SHADOWED;
					}
				}
			}
		}
	  for (pass = 0; pass < 2; pass++)
	  {
		for (s = s_center - l.lmcache_side; s <= s_center + l.lmcache_side; s++)
		{
			for (t = t_center - l.lmcache_side; t <= t_center + l.lmcache_side; t++)
			{
				weighting = (qmin (0.5, sizehalf - (s - s_center)) - qmax (-0.5, -sizehalf - (s - s_center)))
					* (qmin (0.5, sizehalf - (t - t_center)) - qmax (-0.5, -sizehalf - (t - t_center)));
				if (g_bleedfix && !g_drawnudge)
				{
					int wallflags = sample_wallflags[(s - s_center + l.lmcache_side) + (2 * l.lmcache_side + 1) * (t - t_center + l.lmcache_side)];
					if (wallflags & (WALLFLAG_BLOCKED | WALLFLAG_SHADOWED))
					{
						continue;
					}
					if (wallflags & WALLFLAG_NUDGED)
					{
						if (pass == 0)
						{
							continue;
						}
					}
				}
				pos = s + l.lmcachewidth * t;
				// when blur distance (g_blur) is large, the subsample can be very far from the original lightmap sample (aligned with interval TEXTURE_STEP (16.0))
				// in some cases such as a thin cylinder, the subsample can even grow into the opposite side
				// as a result, when exposed to a directional light, the light on the cylinder may "leak" into the opposite dark side
				// this correction limits the effect of blur distance when the normal changes very fast
				// this correction will not break the smoothness that HLRAD_GROWSAMPLE ensures
				weighting_correction = DotProduct (l.lmcache_normal[pos], centernormal);
				weighting_correction = (weighting_correction > 0)? weighting_correction * weighting_correction: 0;
				weighting = weighting * weighting_correction;
				for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
				{
					VectorMA (fl_samples[j][i].light, weighting, l.lmcache[pos][j], fl_samples[j][i].light);
				}
				subsamples += weighting;
			}
		}
		if (subsamples > NORMAL_EPSILON)
		{
			break;
		}
		else
		{
			subsamples = 0.0;
			for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
			{
				VectorClear (fl_samples[j][i].light);
			}
		}
	  }
		if (subsamples > 0)
		{
			for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
			{
				VectorScale (fl_samples[j][i].light, 1.0 / subsamples, fl_samples[j][i].light);
			}
		}
    } // end of i loop
	free (sample_wallflags);

    // average up the direct light on each patch for radiosity
	AddSamplesToPatches ((const sample_t **)fl_samples, f_styles, facenum, &l);
    {
        for (patch = g_face_patches[facenum]; patch; patch = patch->next)
        {
            //LRC:
			unsigned istyle;
			if (patch->samples <= ON_EPSILON * ON_EPSILON)
				patch->samples = 0.0;
			if (patch->samples)
			{
				for (istyle = 0; istyle < ALLSTYLES && patch->totalstyle_all[istyle] != 255; istyle++)
				{
					vec3_t v;
					VectorScale (patch->samplelight_all[istyle], 1.0f / patch->samples, v);
					VectorAdd (patch->directlight_all[istyle], v, patch->directlight_all[istyle]);
				}
			}
            //LRC (ends)
        }
    }
	for (patch = g_face_patches[facenum]; patch; patch = patch->next)
	{
		// get the PVS for the pos to limit the number of checks
		if (!g_visdatasize)
		{
			memset (pvs, 255, (g_dmodels[0].visleafs + 7) / 8);
			lastoffset = -1;
		}
		else
		{
			dleaf_t*        leaf = PointInLeaf(patch->origin);

			thisoffset = leaf->visofs;
			if (patch == g_face_patches[facenum] || thisoffset != lastoffset)
			{
				if (thisoffset == -1)
				{
					memset (pvs, 0, (g_dmodels[0].visleafs + 7) / 8);
				}
				else
				{
					DecompressVis(&g_dvisdata[leaf->visofs], pvs, sizeof(pvs));
				}
			}
			lastoffset = thisoffset;
		}
		if (l.translucent_b)
		{
			if (!g_visdatasize)
			{
				memset (pvs2, 255, (g_dmodels[0].visleafs + 7) / 8);
				lastoffset2 = -1;
			}
			else
			{
				VectorMA (patch->origin, -(g_translucentdepth+2*PATCH_HUNT_OFFSET), l.facenormal, spot2);
				dleaf_t*        leaf2 = PointInLeaf(spot2);

				thisoffset2 = leaf2->visofs;
				if (l.numsurfpt == 0 || thisoffset2 != lastoffset2)
				{
					if (thisoffset2 == -1)
					{
						memset (pvs2, 0, (g_dmodels[0].visleafs + 7) / 8);
					}
					else
					{
						DecompressVis(&g_dvisdata[leaf2->visofs], pvs2, sizeof(pvs2));
					}
				}
				lastoffset2 = thisoffset2;
			}
			vec3_t frontsampled[ALLSTYLES], backsampled[ALLSTYLES];
			for (j = 0; j < ALLSTYLES; j++)
			{
				VectorClear (frontsampled[j]);
				VectorClear (backsampled[j]);
			}
			VectorSubtract (vec3_origin, l.facenormal, normal2);
			GatherSampleLight (patch->origin, pvs, l.facenormal, frontsampled, 
				patch->totalstyle_all
				, 1
				, l.miptex
				, facenum
				);
			GatherSampleLight (spot2, pvs2, normal2, backsampled, 
				patch->totalstyle_all
				, 1
				, l.miptex
				, facenum
				);
			for (j = 0; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
			{
				for (int x = 0; x < 3; x++)
				{
					patch->totallight_all[j][x] += (1.0 - l.translucent_v[x]) * frontsampled[j][x] + l.translucent_v[x] * backsampled[j][x];
				}
			}
		}
		else
		{
			GatherSampleLight (patch->origin, pvs, l.facenormal, 
				patch->totallight_all, 
				patch->totalstyle_all
				, 1
				, l.miptex
				, facenum
				);
		}
	}

    // add an ambient term if desired
    if (g_ambient[0] || g_ambient[1] || g_ambient[2])
    {
		for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
		{
			if (f_styles[j] == 0)
			{
				s = fl_samples[j];
                for (i = 0; i < l.numsurfpt; i++, s++)
                {
                    VectorAdd(s->light, g_ambient, s->light);
                }
                break;
            }
        }

    }

    // add circus lighting for finding black lightmaps
    if (g_circus)
    {
		for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
		{
			if (f_styles[j] == 0)
			{
                int             amt = 7;

				s = fl_samples[j];

                while ((l.numsurfpt % amt) == 0)
                {
                    amt--;
                }
                if (amt < 2)
                {
                    amt = 7;
                }

                for (i = 0; i < l.numsurfpt; i++, s++)
                {
                    if ((s->light[0] == 0) && (s->light[1] == 0) && (s->light[2] == 0))
                    {
                        VectorAdd(s->light, s_circuscolors[i % amt], s->light);
                    }
                }
                break;
            }
        }
    }

    // light from dlight_threshold and above is sent out, but the
    // texture itself should still be full bright

    // if( VectorAvg( face_patches[facenum]->baselight ) >= dlight_threshold)       // Now all lighted surfaces glow
    {
        //LRC:
		if (g_face_patches[facenum])
		{
			for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
			{
				if (f_styles[j] == g_face_patches[facenum]->emitstyle)
				{
					break;
				}
			}
			if (j == ALLSTYLES)
			{
				if (++stylewarningcount >= stylewarningnext)
				{
					stylewarningnext = stylewarningcount * 2;
					Warning("Too many direct light styles on a face(?,?,?)");
					Warning(" total %d warnings for too many styles", stylewarningcount);
				}
			}
			else
			{
				if (f_styles[j] == 255)
				{
					f_styles[j] = g_face_patches[facenum]->emitstyle;
				}

				s = fl_samples[j];
				for (i = 0; i < l.numsurfpt; i++, s++)
				{
					VectorAdd(s->light, g_face_patches[facenum]->baselight, s->light);
				}
			}
		}
        //LRC (ends)
    }
	// samples
	{
		facelight_t *fl = &facelight[facenum];
		vec_t maxlights[ALLSTYLES];
		for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
		{
			maxlights[j] = 0;
			for (i = 0; i < fl->numsamples; i++)
			{
				vec_t b = VectorMaximum (fl_samples[j][i].light);
				maxlights[j] = qmax (maxlights[j], b);
			}
			if (maxlights[j] <= g_corings[f_styles[j]] * 0.1) // light is too dim, discard this style to reduce RAM usage
			{
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					ThreadLock ();
					if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
					{
						g_maxdiscardedlight = maxlights[j];
						VectorCopy (g_face_centroids[facenum], g_maxdiscardedpos);
					}
					ThreadUnlock ();
				}
				maxlights[j] = 0;
			}
		}
		for (k = 0; k < MAXLIGHTMAPS; k++)
		{
			int bestindex = -1;
			if (k == 0)
			{
				bestindex = 0;
			}
			else
			{
				vec_t bestmaxlight = 0;
				for (j = 1; j < ALLSTYLES && f_styles[j] != 255; j++)
				{
					if (maxlights[j] > bestmaxlight + NORMAL_EPSILON)
					{
						bestmaxlight = maxlights[j];
						bestindex = j;
					}
				}
			}
			if (bestindex != -1)
			{
				maxlights[bestindex] = 0;
				f->styles[k] = f_styles[bestindex];
				fl->samples[k] = (sample_t *)malloc (fl->numsamples * sizeof (sample_t));
				hlassume (fl->samples[k] != NULL, assume_NoMemory);
				memcpy (fl->samples[k], fl_samples[bestindex], fl->numsamples * sizeof (sample_t));
			}
			else
			{
				f->styles[k] = 255;
				fl->samples[k] = NULL;
			}
		}
		for (j = 1; j < ALLSTYLES && f_styles[j] != 255; j++)
		{
			if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
			{
				ThreadLock ();
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = maxlights[j];
					VectorCopy (g_face_centroids[facenum], g_maxdiscardedpos);
				}
				ThreadUnlock ();
			}
		}
		for (j = 0; j < ALLSTYLES; j++)
		{
			free (fl_samples[j]);
		}
	}
	// patches
	for (patch = g_face_patches[facenum]; patch; patch = patch->next)
	{
		vec_t maxlights[ALLSTYLES];
		for (j = 0; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
		{
			maxlights[j] = VectorMaximum (patch->totallight_all[j]);
		}
		for (k = 0; k < MAXLIGHTMAPS; k++)
		{
			int bestindex = -1;
			if (k == 0)
			{
				bestindex = 0;
			}
			else
			{
				vec_t bestmaxlight = 0;
				for (j = 1; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
				{
					if (maxlights[j] > bestmaxlight + NORMAL_EPSILON)
					{
						bestmaxlight = maxlights[j];
						bestindex = j;
					}
				}
			}
			if (bestindex != -1)
			{
				maxlights[bestindex] = 0;
				patch->totalstyle[k] = patch->totalstyle_all[bestindex];
				VectorCopy (patch->totallight_all[bestindex], patch->totallight[k]);
			}
			else
			{
				patch->totalstyle[k] = 255;
			}
		}
		for (j = 1; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
		{
			if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
			{
				ThreadLock ();
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = maxlights[j];
					VectorCopy (patch->origin, g_maxdiscardedpos);
				}
				ThreadUnlock ();
			}
		}
		for (j = 0; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
		{
			maxlights[j] = VectorMaximum (patch->directlight_all[j]);
		}
		for (k = 0; k < MAXLIGHTMAPS; k++)
		{
			int bestindex = -1;
			if (k == 0)
			{
				bestindex = 0;
			}
			else
			{
				vec_t bestmaxlight = 0;
				for (j = 1; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
				{
					if (maxlights[j] > bestmaxlight + NORMAL_EPSILON)
					{
						bestmaxlight = maxlights[j];
						bestindex = j;
					}
				}
			}
			if (bestindex != -1)
			{
				maxlights[bestindex] = 0;
				patch->directstyle[k] = patch->totalstyle_all[bestindex];
				VectorCopy (patch->directlight_all[bestindex], patch->directlight[k]);
			}
			else
			{
				patch->directstyle[k] = 255;
			}
		}
		for (j = 1; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
		{
			if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
			{
				ThreadLock ();
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = maxlights[j];
					VectorCopy (patch->origin, g_maxdiscardedpos);
				}
				ThreadUnlock ();
			}
		}
		free (patch->totalstyle_all);
		patch->totalstyle_all = NULL;
		free (patch->samplelight_all);
		patch->samplelight_all = NULL;
		free (patch->totallight_all);
		patch->totallight_all = NULL;
		free (patch->directlight_all);
		patch->directlight_all = NULL;
	}
	free (l.lmcache);
	free (l.lmcache_normal);
	free (l.lmcache_wallflags);
	free (l.surfpt_position);
	free (l.surfpt_surface);
}

// =====================================================================================
//  PrecompLightmapOffsets
// =====================================================================================
void            PrecompLightmapOffsets()
{
    int             facenum;
    dface_t*        f;
    facelight_t*    fl;
    int             lightstyles;

    int             i; //LRC
	patch_t*        patch; //LRC

    g_lightdatasize = 0;

    for (facenum = 0; facenum < g_numfaces; facenum++)
    {
        f = &g_dfaces[facenum];
        fl = &facelight[facenum];

        if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
        {
            continue;                                      // non-lit texture
        }


		{
			int i, j, k;
			vec_t maxlights[ALLSTYLES];
			{
				vec3_t maxlights1[ALLSTYLES];
				vec3_t maxlights2[ALLSTYLES];
				for (j = 0; j < ALLSTYLES; j++)
				{
					VectorClear (maxlights1[j]);
					VectorClear (maxlights2[j]);
				}
				for (k = 0; k < MAXLIGHTMAPS && f->styles[k] != 255; k++)
				{
					for (i = 0; i < fl->numsamples; i++)
					{
						VectorCompareMaximum (maxlights1[f->styles[k]], fl->samples[k][i].light, maxlights1[f->styles[k]]);
					}
				}
				int numpatches;
				const int *patches;
				GetTriangulationPatches (facenum, &numpatches, &patches); // collect patches and their neighbors

				for (i = 0; i < numpatches; i++)
				{
					patch = &g_patches[patches[i]];
					for (k = 0; k < MAXLIGHTMAPS && patch->totalstyle[k] != 255; k++)
					{
						VectorCompareMaximum (maxlights2[patch->totalstyle[k]], patch->totallight[k], maxlights2[patch->totalstyle[k]]);
					}
				}
				for (j = 0; j < ALLSTYLES; j++)
				{
					vec3_t v;
					VectorAdd (maxlights1[j], maxlights2[j], v);
					maxlights[j] = VectorMaximum (v);
					if (maxlights[j] <= g_corings[j] * 0.01)
					{
						if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
						{
							g_maxdiscardedlight = maxlights[j];
							VectorCopy (g_face_centroids[facenum], g_maxdiscardedpos);
						}
						maxlights[j] = 0;
					}
				}
			}
			unsigned char oldstyles[MAXLIGHTMAPS];
			sample_t *oldsamples[MAXLIGHTMAPS];
			for (k = 0; k < MAXLIGHTMAPS; k++)
			{
				oldstyles[k] = f->styles[k];
				oldsamples[k] = fl->samples[k];
			}
			for (k = 0; k < MAXLIGHTMAPS; k++)
			{
				unsigned char beststyle = 255;
				if (k == 0)
				{
					beststyle = 0;
				}
				else
				{
					vec_t bestmaxlight = 0;
					for (j = 1; j < ALLSTYLES; j++)
					{
						if (maxlights[j] > bestmaxlight + NORMAL_EPSILON)
						{
							bestmaxlight = maxlights[j];
							beststyle = j;
						}
					}
				}
				if (beststyle != 255)
				{
					maxlights[beststyle] = 0;
					f->styles[k] = beststyle;
					fl->samples[k] = (sample_t *)malloc (fl->numsamples * sizeof (sample_t));
					hlassume (fl->samples[k] != NULL, assume_NoMemory);
					for (i = 0; i < MAXLIGHTMAPS && oldstyles[i] != 255; i++)
					{
						if (oldstyles[i] == f->styles[k])
						{
							break;
						}
					}
					if (i < MAXLIGHTMAPS && oldstyles[i] != 255)
					{
						memcpy (fl->samples[k], oldsamples[i], fl->numsamples * sizeof (sample_t));
					}
					else
					{
						memcpy (fl->samples[k], oldsamples[0], fl->numsamples * sizeof (sample_t)); // copy 'sample.pos' from style 0 to the new style - because 'sample.pos' is actually the same for all styles! (why did we decide to store it in many places?)
						for (j = 0; j < fl->numsamples; j++)
						{
							VectorClear (fl->samples[k][j].light);
						}
					}
				}
				else
				{
					f->styles[k] = 255;
					fl->samples[k] = NULL;
				}
			}
			for (j = 1; j < ALLSTYLES; j++)
			{
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = maxlights[j];
					VectorCopy (g_face_centroids[facenum], g_maxdiscardedpos);
				}
			}
			for (k = 0; k < MAXLIGHTMAPS && oldstyles[k] != 255; k++)
			{
				free (oldsamples[k]);
			}
		}

        for (lightstyles = 0; lightstyles < MAXLIGHTMAPS; lightstyles++)
        {
            if (f->styles[lightstyles] == 255)
            {
                break;
            }
        }

        if (!lightstyles)
        {
            continue;
        }

        f->lightofs = g_lightdatasize;
        g_lightdatasize += fl->numsamples * 3 * lightstyles;
		hlassume (g_lightdatasize <= g_max_map_lightdata, assume_MAX_MAP_LIGHTING); //lightdata

    }
}
void ReduceLightmap ()
{
	byte *oldlightdata = (byte *)malloc (g_lightdatasize);
	hlassume (oldlightdata != NULL, assume_NoMemory);
	memcpy (oldlightdata, g_dlightdata, g_lightdatasize);
	g_lightdatasize = 0;

	int facenum;
	for (facenum = 0; facenum < g_numfaces; facenum++)
	{
		dface_t *f = &g_dfaces[facenum];
		facelight_t *fl = &facelight[facenum];
		if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
		{
			continue;                                      // non-lit texture
		}
		// just need to zero the lightmap so that it won't contribute to lightdata size
		if (IntForKey (g_face_entity[facenum], "zhlt_striprad"))
		{
			f->lightofs = g_lightdatasize;
			for (int k = 0; k < MAXLIGHTMAPS; k++)
			{
				f->styles[k] = 255;
			}
			continue;
		}
#if 0 //debug. --vluzacn
		const char *lightmapcolor = ValueForKey (g_face_entity[facenum], "zhlt_rad");
		if (*lightmapcolor)
		{
			hlassume (MAXLIGHTMAPS == 4, assume_first);
			int styles[4], values[4][3];
			if (sscanf (lightmapcolor, "%d=%d,%d,%d %d=%d,%d,%d %d=%d,%d,%d %d=%d,%d,%d"
					, &styles[0], &values[0][0], &values[0][1], &values[0][2]
					, &styles[1], &values[1][0], &values[1][1], &values[1][2]
					, &styles[2], &values[2][0], &values[2][1], &values[2][2]
					, &styles[3], &values[3][0], &values[3][1], &values[3][2]
				) != 16)
			{
				Error ("Bad value for 'zhlt_rad'.");
			}
			f->lightofs = g_lightdatasize;
			int i, k;
			for (k = 0; k < 4; k++)
			{
				f->styles[k] = 255;
			}
			for (k = 0; k < 4 && styles[k] != 255; k++)
			{
				f->styles[k] = styles[k];
				hlassume (g_lightdatasize + fl->numsamples * 3 <= g_max_map_lightdata, assume_MAX_MAP_LIGHTING);
				for (i = 0; i < fl->numsamples; i++)
				{
					VectorCopy (values[k], (byte *)&g_dlightdata[g_lightdatasize + i * 3]);
				}
				g_lightdatasize += fl->numsamples * 3;
			}
			continue;
		}
#endif
		if (f->lightofs == -1)
		{
			continue;
		}

		int i, k;
		int oldofs;
		unsigned char oldstyles[MAXLIGHTMAPS];
		oldofs = f->lightofs;
		f->lightofs = g_lightdatasize;
		for (k = 0; k < MAXLIGHTMAPS; k++)
		{
			oldstyles[k] = f->styles[k];
			f->styles[k] = 255;
		}
		int numstyles = 0;
		for (k = 0; k < MAXLIGHTMAPS && oldstyles[k] != 255; k++)
		{
			unsigned char maxb = 0;
			for (i = 0; i < fl->numsamples; i++)
			{
				unsigned char *v = &oldlightdata[oldofs + fl->numsamples * 3 * k + i * 3];
				maxb = qmax (maxb, VectorMaximum (v));
			}
			if (maxb <= 0) // black
			{
				continue;
			}
			f->styles[numstyles] = oldstyles[k];
			hlassume (g_lightdatasize + fl->numsamples * 3 * (numstyles + 1) <= g_max_map_lightdata, assume_MAX_MAP_LIGHTING);
			memcpy (&g_dlightdata[f->lightofs + fl->numsamples * 3 * numstyles], &oldlightdata[oldofs + fl->numsamples * 3 * k], fl->numsamples * 3);
			numstyles++;
		}
		g_lightdatasize += fl->numsamples * 3 * numstyles;
	}
	free (oldlightdata);
}


// Change the sample light right under a mdl file entity's origin.
// Use this when "mdl" in shadow has incorrect brightness.

const int MLH_MAXFACECOUNT = 16;
const int MLH_MAXSAMPLECOUNT = 4;
const vec_t MLH_LEFT = 0;
const vec_t MLH_RIGHT = 1;

typedef struct
{
	vec3_t origin;
	vec3_t floor;
	struct
	{
		int num;
		struct
		{
			bool exist;
			int seq;
		}
		style[ALLSTYLES];
		struct
		{
			int num;
			vec3_t pos;
			unsigned char* (style[ALLSTYLES]);
		}
		sample[MLH_MAXSAMPLECOUNT];
		int samplecount;
	}
	face[MLH_MAXFACECOUNT];
	int facecount;
} mdllight_t;

int MLH_AddFace (mdllight_t *ml, int facenum)
{
	dface_t *f = &g_dfaces[facenum];
	int i, j;
	for (i = 0; i < ml->facecount; i++)
	{
		if (ml->face[i].num == facenum)
		{
			return -1;
		}
	}
	if (ml->facecount >= MLH_MAXFACECOUNT)
	{
		return -1;
	}
	i = ml->facecount;
	ml->facecount++;
	ml->face[i].num = facenum;
	ml->face[i].samplecount = 0;
	for (j = 0; j < ALLSTYLES; j++)
	{
		ml->face[i].style[j].exist = false;
	}
	for (j = 0; j < MAXLIGHTMAPS && f->styles[j] != 255; j++)
	{
		ml->face[i].style[f->styles[j]].exist = true;
		ml->face[i].style[f->styles[j]].seq = j;
	}
	return i;
}
void MLH_AddSample (mdllight_t *ml, int facenum, int w, int h, int s, int t, const vec3_t pos)
{
	dface_t *f = &g_dfaces[facenum];
	int i, j;
	int r = MLH_AddFace (ml, facenum);
	if (r == -1)
	{
		return;
	}
	int size = w * h;
	int num = s + w * t;
	for (i = 0; i < ml->face[r].samplecount; i++)
	{
		if (ml->face[r].sample[i].num == num)
		{
			return;
		}
	}
	if (ml->face[r].samplecount >= MLH_MAXSAMPLECOUNT)
	{
		return;
	}
	i = ml->face[r].samplecount;
	ml->face[r].samplecount++;
	ml->face[r].sample[i].num = num;
	VectorCopy (pos, ml->face[r].sample[i].pos);
	for (j = 0; j < ALLSTYLES; j++)
	{
		if (ml->face[r].style[j].exist)
		{
			ml->face[r].sample[i].style[j] = &g_dlightdata[f->lightofs + (num + size * ml->face[r].style[j].seq) * 3];
		}
	}
}
void MLH_CalcExtents (const dface_t *f, int *texturemins, int *extents)
{
	int bmins[2];
	int bmaxs[2];
	int i;

	GetFaceExtents (f - g_dfaces, bmins, bmaxs);
	for (i = 0; i < 2; i++)
	{
		texturemins[i] = bmins[i] * TEXTURE_STEP;
		extents[i] = (bmaxs[i] - bmins[i]) * TEXTURE_STEP;
	}
}
void MLH_GetSamples_r (mdllight_t *ml, int nodenum, const float *start, const float *end)
{
	if (nodenum < 0)
		return;
	dnode_t *node = &g_dnodes[nodenum];
	dplane_t *plane;
	float front, back, frac;
	float mid[3];
	int side;
	plane = &g_dplanes[node->planenum];
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;
	if ((back < 0) == side)
	{
		MLH_GetSamples_r (ml, node->children[side], start, end);
		return;
	}
	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;
	MLH_GetSamples_r (ml, node->children[side], start, mid);
	if (ml->facecount > 0)
	{
		return;
	}
	{
		int i;
		for (i = 0; i < node->numfaces; i++)
		{
			dface_t *f = &g_dfaces[node->firstface + i];
			texinfo_t *tex = &g_texinfo[f->texinfo];
			const char *texname = GetTextureByNumber (f->texinfo);
			if (!strncmp (texname, "sky", 3))
			{
				continue;
			}
			if (f->lightofs == -1)
			{
				continue;
			}
			int s = (int)(DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3]);
			int t = (int)(DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3]);
			int texturemins[2], extents[2];
			MLH_CalcExtents (f, texturemins, extents);
			if (s < texturemins[0] || t < texturemins[1])
			{
				continue;
			}
			int ds = s - texturemins[0];
			int dt = t - texturemins[1];
			if (ds > extents[0] || dt > extents[1])
			{
				continue;
			}
			ds >>= 4;
			dt >>= 4;
			MLH_AddSample (ml, node->firstface + i, extents[0] / TEXTURE_STEP + 1, extents[1] / TEXTURE_STEP + 1, ds, dt, mid);
			break;
		}
	}
	if (ml->facecount > 0)
	{
		VectorCopy (mid, ml->floor);
		return;
	}
	MLH_GetSamples_r (ml, node->children[!side], mid, end);
}
void MLH_mdllightCreate (mdllight_t *ml)
{
	// code from Quake
	float p[3];
	float end[3];
	ml->facecount = 0;
	VectorCopy (ml->origin, ml->floor);
	VectorCopy (ml->origin, p);
	VectorCopy (ml->origin, end);
	end[2] -= 2048;
	MLH_GetSamples_r (ml, 0, p, end);
}

int MLH_CopyLight (const vec3_t from, const vec3_t to)
{
	int i, j, k, count = 0;
	mdllight_t mlfrom, mlto;
	VectorCopy (from, mlfrom.origin);
	VectorCopy (to, mlto.origin);
	MLH_mdllightCreate (&mlfrom);
	MLH_mdllightCreate (&mlto);
	if (mlfrom.facecount == 0 || mlfrom.face[0].samplecount == 0)
		return -1;
	for (i = 0; i < mlto.facecount; ++i)
		for (j = 0; j < mlto.face[i].samplecount; ++j, ++count)
			for (k = 0; k < ALLSTYLES; ++k)
				if (mlto.face[i].style[k].exist && mlfrom.face[0].style[k].exist)
				{
					VectorCopy (mlfrom.face[0].sample[0].style[k],mlto.face[i].sample[j].style[k]);
					Developer (DEVELOPER_LEVEL_SPAM, "Mdl Light Hack: face (%d) sample (%d) style (%d) position (%f,%f,%f)\n",
						mlto.face[i].num, mlto.face[i].sample[j].num, k, 
						mlto.face[i].sample[j].pos[0], mlto.face[i].sample[j].pos[1], mlto.face[i].sample[j].pos[2]);
				}
	Developer (DEVELOPER_LEVEL_MESSAGE, "Mdl Light Hack: %d sample light copied from (%f,%f,%f) to (%f,%f,%f)\n", 
		count, mlfrom.floor[0], mlfrom.floor[1], mlfrom.floor[2], mlto.floor[0], mlto.floor[1], mlto.floor[2]);
	return count;
}

void MdlLightHack ()
{
	int ient;
	entity_t *ent1, *ent2;
	vec3_t origin1, origin2;
	const char *target;
	int used = 0, countent = 0, countsample = 0, r;
	for (ient = 0; ient < g_numentities; ++ient)
	{
		ent1 = &g_entities[ient];
		target = ValueForKey (ent1, "zhlt_copylight");
		if (!strcmp (target, ""))
			continue;
		used = 1;
		ent2 = FindTargetEntity (target);
		if (ent2 == NULL)
		{
			Warning ("target entity '%s' not found", target);
			continue;
		}
		GetVectorForKey (ent1, "origin", origin1);
		GetVectorForKey (ent2, "origin", origin2);
		r = MLH_CopyLight (origin2, origin1);
		if (r < 0)
			Warning ("can not copy light from (%f,%f,%f)", origin2[0], origin2[1], origin2[2]);
		else
		{
			countent += 1;
			countsample += r;
		}
	}
	if (used)
		Log ("Adjust mdl light: modified %d samples for %d entities\n", countsample, countent);
}


typedef struct facelightlist_s
{
	int facenum;
	facelightlist_s *next;
}
facelightlist_t;

static facelightlist_t *g_dependentfacelights[MAX_MAP_FACES];

// =====================================================================================
//  CreateFacelightDependencyList
// =====================================================================================
void CreateFacelightDependencyList ()
{
	int facenum;
	dface_t *f;
	facelight_t *fl;
	int i;
	int k;
	int surface;
	facelightlist_t *item;

	for (i = 0; i < MAX_MAP_FACES; i++)
	{
		g_dependentfacelights[i] = NULL;
	}

	// for each face
	for (facenum = 0; facenum < g_numfaces; facenum++)
	{
		f = &g_dfaces[facenum];
		fl = &facelight[facenum];
		if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
		{
			continue;
		}

		for (k = 0; k < MAXLIGHTMAPS && f->styles[k] != 255; k++)
		{
			for (i = 0; i < fl->numsamples; i++)
			{
				surface = fl->samples[k][i].surface; // that surface contains at least one sample from this face
				if (0 <= surface && surface < g_numfaces)
				{
					// insert this face into the dependency list of that surface
					for (item = g_dependentfacelights[surface]; item != NULL; item = item->next)
					{
						if (item->facenum == facenum)
							break;
					}
					if (item)
					{
						continue;
					}

					item = (facelightlist_t *)malloc (sizeof (facelightlist_t));
					hlassume (item != NULL, assume_NoMemory);
					item->facenum = facenum;
					item->next = g_dependentfacelights[surface];
					g_dependentfacelights[surface] = item;
				}
			}
		}
	}
}

// =====================================================================================
//  FreeFacelightDependencyList
// =====================================================================================
void FreeFacelightDependencyList ()
{
	int i;
	facelightlist_t *item;

	for (i = 0; i < MAX_MAP_FACES; i++)
	{
		while (g_dependentfacelights[i])
		{
			item = g_dependentfacelights[i];
			g_dependentfacelights[i] = item->next;
			free (item);
		}
	}
}

// =====================================================================================
//  ScaleDirectLights
// =====================================================================================
void ScaleDirectLights ()
{
	int facenum;
	dface_t *f;
	facelight_t *fl;
	int i;
	int k;
	sample_t *samp;

	for (facenum = 0; facenum < g_numfaces; facenum++)
	{
		f = &g_dfaces[facenum];

		if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
		{
			continue;
		}

		fl = &facelight[facenum];

		for (k = 0; k < MAXLIGHTMAPS && f->styles[k] != 255; k++)
		{
			for (i = 0; i < fl->numsamples; i++)
			{
				samp = &fl->samples[k][i];
				VectorScale (samp->light, g_direct_scale, samp->light);
			}
		}
	}
}

// =====================================================================================
//  AddPatchLights
//    This function is run multithreaded
// =====================================================================================
void AddPatchLights (int facenum)
{
	dface_t *f;
	facelightlist_t *item;
	dface_t *f_other;
	facelight_t *fl_other;
	int k;
	int i;
	sample_t *samp;

	f = &g_dfaces[facenum];

	if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
	{
		return;
	}

	
	for (item = g_dependentfacelights[facenum]; item != NULL; item = item->next)
	{
		f_other = &g_dfaces[item->facenum];
		fl_other = &facelight[item->facenum];
		for (k = 0; k < MAXLIGHTMAPS && f_other->styles[k] != 255; k++)
		{
			for (i = 0; i < fl_other->numsamples; i++)
			{
				samp = &fl_other->samples[k][i];
				if (samp->surface != facenum)
				{ // the sample is not in this surface
					continue;
				}

				{
					vec3_t v;

					int style = f_other->styles[k];
					InterpolateSampleLight (samp->pos, samp->surface, 1, &style, &v
											);

					VectorAdd (samp->light, v, v);
					if (VectorMaximum (v) >= g_corings[f_other->styles[k]])
					{
						VectorCopy (v, samp->light);
					}
					else
					{
						if (VectorMaximum (v) > g_maxdiscardedlight + NORMAL_EPSILON)
						{
							ThreadLock ();
							if (VectorMaximum (v) > g_maxdiscardedlight + NORMAL_EPSILON)
							{
								g_maxdiscardedlight = VectorMaximum (v);
								VectorCopy (samp->pos, g_maxdiscardedpos);
							}
							ThreadUnlock ();
						}
					}
				}
			} // loop samples
		}
	}

}

// =====================================================================================
//  FinalLightFace
//      Add the indirect lighting on top of the direct lighting and save into final map format
// =====================================================================================
void            FinalLightFace(const int facenum)
{
	if (facenum == 0 && g_drawsample)
	{
		char name[_MAX_PATH+20];
		snprintf (name, sizeof(name), "%s_sample.pts", g_Mapname);
		Log ("Writing '%s' ...\n", name);
		FILE *f;
		f = fopen(name, "w");
		if (f)
		{
			const int pos_count = 15;
			const vec3_t pos[pos_count] = {{0,0,0},{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},{1,0,0},{0,0,1},{-1,0,0},{0,0,-1},{0,-1,0},{0,0,1},{0,1,0},{0,0,-1},{1,0,0},{0,0,0}};
			int i, j, k;
			vec3_t v, dist;
			for (i = 0; i < g_numfaces; ++i)
			{
				const facelight_t *fl=&facelight[i];
				for (j = 0; j < fl->numsamples; ++j)
				{
					VectorCopy (fl->samples[0][j].pos, v);
					VectorSubtract (v, g_drawsample_origin, dist);
					if (DotProduct (dist, dist) < g_drawsample_radius * g_drawsample_radius)
					{
						for (k = 0; k < pos_count; ++k)
							fprintf (f, "%g %g %g\n", v[0]+pos[k][0], v[1]+pos[k][1], v[2]+pos[k][2]);
					}
				}
			}
			fclose(f);
			Log ("OK.\n");
		}
		else
			Log ("Error.\n");
	}
    int             i, j, k;
    vec3_t          lb, v;
    facelight_t*    fl;
    sample_t*       samp;
    float           minlight;
    int             lightstyles;
    dface_t*        f;
	vec3_t			*original_basiclight;
	int				(*final_basiclight)[3];
	int				lbi[3];

    // ------------------------------------------------------------------------
    // Changes by Adam Foster - afoster@compsoc.man.ac.uk
    float           temp_rand;
    // ------------------------------------------------------------------------

    f = &g_dfaces[facenum];
    fl = &facelight[facenum];

    if (g_texinfo[f->texinfo].flags & TEX_SPECIAL)
    {
        return;                                            // non-lit texture
    }

    for (lightstyles = 0; lightstyles < MAXLIGHTMAPS; lightstyles++)
    {
        if (f->styles[lightstyles] == 255)
        {
            break;
        }
    }

    if (!lightstyles)
    {
        return;
    }

    //
    // set up the triangulation
    //
    //
    // sample the triangulation
    //
    minlight = FloatForKey(g_face_entity[facenum], "_minlight") * 255; //seedee
	minlight = (minlight > 255) ? 255 : minlight;

	const char* texname = GetTextureByNumber(f->texinfo);

	if (!strncasecmp(texname, "%", 1)) //If texture name has % flag //seedee
	{
		size_t texnameLength = strlen(texname);

		if (texnameLength > 1)
		{
			char* minlightValue = new char[texnameLength + 1];
			int valueIndex = 0;
			int i = 1;

			if (texname[i] >= '0' && texname[i] <= '9') //Loop until non-digit is found or we run out of space
			{
				while (texname[i] != '\0' && texname[i] >= '0' && texname[i] <= '9' && valueIndex < texnameLength)
				{
					minlightValue[valueIndex++] = texname[i++];
				}
				minlightValue[valueIndex] = '\0';
				minlight = atoi(minlightValue);
				delete[] minlightValue;
				minlight = (minlight > 255) ? 255 : minlight;
			}
		}
		else
		{
			minlight = 255;
		}
	}
	minlight_i it;

	for (it = s_minlights.begin(); it != s_minlights.end(); it++)
	{
		if (!strcasecmp(texname, it->name.c_str()))
		{
			float minlightValue = it->value * 255.0f;
			minlight = static_cast<int>(minlightValue);
			minlight = (minlight > 255) ? 255 : minlight;
		}
	}
	original_basiclight = (vec3_t *)calloc (fl->numsamples, sizeof(vec3_t));
	final_basiclight = (int (*)[3])calloc (fl->numsamples, sizeof(int [3]));
	hlassume (original_basiclight != NULL, assume_NoMemory);
	hlassume (final_basiclight != NULL, assume_NoMemory);
    for (k = 0; k < lightstyles; k++)
    {
        samp = fl->samples[k];
        for (j = 0; j < fl->numsamples; j++, samp++)
        {
			VectorCopy (samp->light, lb);
			if (f->styles[0] != 0)
			{
				Warning ("wrong f->styles[0]");
			}
			VectorCompareMaximum (lb, vec3_origin, lb);
			if (k == 0)
			{
				VectorCopy (lb, original_basiclight[j]);
			}
			else
			{
				VectorAdd (lb, original_basiclight[j], lb);
			}
            // ------------------------------------------------------------------------
	        // Changes by Adam Foster - afoster@compsoc.man.ac.uk
	        // colour lightscale:
	        lb[0] *= g_colour_lightscale[0];
	        lb[1] *= g_colour_lightscale[1];
	        lb[2] *= g_colour_lightscale[2];
            // ------------------------------------------------------------------------

            // clip from the bottom first
            for (i = 0; i < 3; i++)
            {
                if (lb[i] < minlight)
                {
                    lb[i] = minlight;
                }
            }


	        // ------------------------------------------------------------------------
	        // Changes by Adam Foster - afoster@compsoc.man.ac.uk

            // AJM: your code is formatted really wierd, and i cant understand a damn thing. 
            //      so i reformatted it into a somewhat readable "normal" fashion. :P

	        if ( g_colour_qgamma[0] != 1.0 ) 
		        lb[0] = (float) pow(lb[0] / 256.0f, g_colour_qgamma[0]) * 256.0f;

	        if ( g_colour_qgamma[1] != 1.0 ) 
		        lb[1] = (float) pow(lb[1] / 256.0f, g_colour_qgamma[1]) * 256.0f;

	        if ( g_colour_qgamma[2] != 1.0 ) 
		        lb[2] = (float) pow(lb[2] / 256.0f, g_colour_qgamma[2]) * 256.0f;

	        // Two different ways of adding noise to the lightmap - colour jitter
	        // (red, green and blue channels are independent), and mono jitter
	        // (monochromatic noise). For simulating dithering, on the cheap. :)

	        // Tends to create seams between adjacent polygons, so not ideal.

	        // Got really weird results when it was set to limit values to 256.0f - it
	        // was as if r, g or b could wrap, going close to zero.

			
			// clip from the top
			{
				vec_t max = VectorMaximum (lb);
				if (g_limitthreshold >= 0 && max > g_limitthreshold)
				{
					if (!g_drawoverload)
					{
						VectorScale (lb, g_limitthreshold / max, lb);
					}
				}
				else
				{
					if (g_drawoverload)
					{
						VectorScale (lb, 0.1, lb); // darken good points
					}
				}
			}
			for (i = 0; i < 3; ++i)
				if (lb[i] < g_minlight)
					lb[i] = g_minlight;
	        // ------------------------------------------------------------------------
			for (i = 0; i < 3; ++i)
			{
				lbi[i] = (int) floor (lb[i] + 0.5);
				if (lbi[i] < 0) lbi[i] = 0;
			}
			if (k == 0)
			{
				VectorCopy (lbi, final_basiclight[j]);
			}
			else
			{
				VectorSubtract (lbi, final_basiclight[j], lbi);
			}
			if (k == 0)
			{
				if (g_colour_jitter_hack[0] || g_colour_jitter_hack[1] || g_colour_jitter_hack[2]) 
					for (i = 0; i < 3; i++) 
						lbi[i] += g_colour_jitter_hack[i] * ((float)rand() / RAND_MAX - 0.5);
				if (g_jitter_hack[0] || g_jitter_hack[1] || g_jitter_hack[2]) 
				{
					temp_rand = (float)rand() / RAND_MAX - 0.5;
					for (i = 0; i < 3; i++) 
						lbi[i] += g_jitter_hack[i] * temp_rand;
				}
			}
			for (i = 0; i < 3; ++i)
			{
				if (lbi[i] < 0) lbi[i] = 0;
				if (lbi[i] > 255) lbi[i] = 255;
			}
            {
                unsigned char* colors = &g_dlightdata[f->lightofs + k * fl->numsamples * 3 + j * 3];

                colors[0] = (unsigned char)lbi[0];
                colors[1] = (unsigned char)lbi[1];
                colors[2] = (unsigned char)lbi[2];
            }
        }
    }
	free (original_basiclight);
	free (final_basiclight);

}


//LRC
vec3_t    totallight_default = { 0, 0, 0 };

//LRC - utility for getting the right totallight value from a patch
vec3_t* GetTotalLight(patch_t* patch, int style
	)
{
	int i;
	for (i = 0; i < MAXLIGHTMAPS && patch->totalstyle[i] != 255; i++)
	{
		if (patch->totalstyle[i] == style)
		{
			return &(patch->totallight[i]);
		}
	}
	return &totallight_default;
}

