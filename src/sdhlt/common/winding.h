#pragma once

#include "cmdlib.h" //--vluzacn


#include "mathtypes.h"
#include "win32fix.h"
#include "mathlib.h"
#include "bspfile.h"
#include "bounding_box.h"

#define MAX_POINTS_ON_WINDING 128
// TODO: FIX THIS STUPID SHIT (MAX_POINTS_ON_WINDING)

#define BASE_WINDING_DISTANCE 9000

#define	SIDE_FRONT		0
#define	SIDE_ON			2
#define	SIDE_BACK		1
#define	SIDE_CROSS		-2

#ifdef SDHLBSP //seedee
#ifndef DOUBLEVEC_T
#error you must add -dDOUBLEVEC_T to the project!
#endif
#define dplane_t plane_t
#define g_dplanes g_mapplanes
typedef struct
{
	vec3_t			normal;
	vec3_t			unused_origin;
	vec_t			dist;
	planetypes		type;
} dplane_t;
extern dplane_t g_dplanes[MAX_INTERNAL_MAP_PLANES];
#endif
class Winding
{
public:
    // General Functions
    void            Print() const;
    void            getPlane(dplane_t& plane) const;
    void            getPlane(vec3_t& normal, vec_t& dist) const;
    vec_t           getArea() const;
    void            getBounds(bounding_box& bounds) const;
    void            getBounds(vec3_t& mins, vec3_t& maxs) const;
    void            getCenter(vec3_t& center) const;
    Winding*        Copy() const;
    void            Check(
		vec_t epsilon = ON_EPSILON
		) const;  // Developer check for validity
    bool            Valid() const;  // Runtime/user/normal check for validity
    void            addPoint(const vec3_t newpoint);
    void            insertPoint(const vec3_t newpoint, const unsigned int offset);

    // Specialized Functions
    void            RemoveColinearPoints(
		vec_t epsilon = ON_EPSILON
		);
    bool            Clip(const dplane_t& split, bool keepon
		, vec_t epsilon = ON_EPSILON
		); // For hlbsp
    void            Clip(const dplane_t& split, Winding** front, Winding** back
		, vec_t epsilon = ON_EPSILON
		);
    void            Clip(const vec3_t normal, const vec_t dist, Winding** front, Winding** back
		, vec_t epsilon = ON_EPSILON
		);
    bool            Chop(const vec3_t normal, const vec_t dist
		, vec_t epsilon = ON_EPSILON
		);
    void            Divide(const dplane_t& split, Winding** front, Winding** back
		, vec_t epsilon = ON_EPSILON
		);
    int             WindingOnPlaneSide(const vec3_t normal, const vec_t dist
		, vec_t epsilon = ON_EPSILON
		);
	void			CopyPoints(vec3_t *points, int &numpoints);

	void			initFromPoints(vec3_t *points, std::uint_least32_t numpoints);
	void			Reset(void);	// Resets the structure

protected:
    void            resize(std::uint_least32_t newsize);

public:
    // Construction
	Winding();										// Do nothing :)
	Winding(vec3_t *points, std::uint_least32_t numpoints);		// Create from raw points
    Winding(const dface_t& face
		, vec_t epsilon = ON_EPSILON
		);
    Winding(const dplane_t& face);
    Winding(const vec3_t normal, const vec_t dist);
    Winding(std::uint_least32_t points);
    Winding(const Winding& other);
    virtual ~Winding();
    Winding& operator=(const Winding& other);

    // Misc
private:
    void initFromPlane(const vec3_t normal, const vec_t dist);

public:
    // Data
    std::uint_least32_t  m_NumPoints;
    vec3_t* m_Points;
protected:
    std::uint_least32_t  m_MaxPoints;
};
