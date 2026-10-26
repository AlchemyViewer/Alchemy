/**
 * @file llvolume.cpp
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llmemory.h"
#include "llmath.h"

#include <set>
#if !LL_WINDOWS
#include <stdint.h>
#endif
#include <cmath>
#include <unordered_map>

#include "llerror.h"

#include "llvolumemgr.h"
#include "v2math.h"
#include "v3math.h"
#include "v4math.h"
#include "m4math.h"
#include "m3math.h"
#include "llmatrix3a.h"
#include "lloctree.h"
#include "llvolume.h"
#include "llstl.h"
#include "llsdserialize.h"
#include "llvector4a.h"
#include "llmatrix4a.h"
#include "alscratcharena.h"
#include "alsimdkernels.h"
#include "lltimer.h"
#include "llvolumeoctree.h"
#include "workqueue.h"

#include <meshoptimizer.h>

#include <bit>

constexpr F32 MIN_CUT_DELTA = 0.02f;

constexpr F32 HOLLOW_MIN = 0.f;
constexpr F32 HOLLOW_MAX = 0.95f;
constexpr F32 HOLLOW_MAX_SQUARE = 0.7f;

constexpr F32 TWIST_MIN = -1.f;
constexpr F32 TWIST_MAX =  1.f;

constexpr F32 RATIO_MIN = 0.f;
constexpr F32 RATIO_MAX = 2.f; // Tom Y: Inverted sense here: 0 = top taper, 2 = bottom taper

constexpr F32 HOLE_X_MIN= 0.01f;
constexpr F32 HOLE_X_MAX= 1.0f;

constexpr F32 HOLE_Y_MIN= 0.01f;
constexpr F32 HOLE_Y_MAX= 0.5f;

constexpr F32 SHEAR_MIN = -0.5f;
constexpr F32 SHEAR_MAX =  0.5f;

constexpr F32 REV_MIN = 1.f;
constexpr F32 REV_MAX = 4.f;

constexpr F32 TAPER_MIN = -1.f;
constexpr F32 TAPER_MAX =  1.f;

constexpr F32 SKEW_MIN  = -0.95f;
constexpr F32 SKEW_MAX  =  0.95f;

constexpr F32 SCULPT_MIN_AREA = 0.002f;
constexpr S32 SCULPT_MIN_AREA_DETAIL = 1;

bool gDebugGL = false; // See settings.xml "RenderDebugGL"

bool check_same_clock_dir( const LLVector3& pt1, const LLVector3& pt2, const LLVector3& pt3, const LLVector3& norm)
{
    LLVector3 test = (pt2-pt1)%(pt3-pt2);

    //answer
    if(test * norm < 0)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool LLLineSegmentBoxIntersect(const LLVector3& start, const LLVector3& end, const LLVector3& center, const LLVector3& size)
{
    return LLLineSegmentBoxIntersect(start.mV, end.mV, center.mV, size.mV);
}

bool LLLineSegmentBoxIntersect(const F32* start, const F32* end, const F32* center, const F32* size)
{
    F32 fAWdU[3]{};
    F32 dir[3]{};
    F32 diff[3]{};

    for (U32 i = 0; i < 3; i++)
    {
        dir[i] = 0.5f * (end[i] - start[i]);
        diff[i] = (0.5f * (end[i] + start[i])) - center[i];
        fAWdU[i] = fabsf(dir[i]);
        if(fabsf(diff[i])>size[i] + fAWdU[i]) return false;
    }

    float f;
    f = dir[1] * diff[2] - dir[2] * diff[1];    if(fabsf(f)>size[1]*fAWdU[2] + size[2]*fAWdU[1])  return false;
    f = dir[2] * diff[0] - dir[0] * diff[2];    if(fabsf(f)>size[0]*fAWdU[2] + size[2]*fAWdU[0])  return false;
    f = dir[0] * diff[1] - dir[1] * diff[0];    if(fabsf(f)>size[0]*fAWdU[1] + size[1]*fAWdU[0])  return false;

    return true;
}

// Finds tangent vec based on three vertices with texture coordinates.
// Fills in dummy values if the triangle has degenerate texture coordinates.
void calc_tangent_from_triangle(
    LLVector4a&         normal,
    LLVector4a&         tangent_out,
    const LLVector4a& v1,
    const LLVector2&  w1,
    const LLVector4a& v2,
    const LLVector2&  w2,
    const LLVector4a& v3,
    const LLVector2&  w3)
{
    const F32* v1ptr = v1.getF32ptr();
    const F32* v2ptr = v2.getF32ptr();
    const F32* v3ptr = v3.getF32ptr();

    float x1 = v2ptr[0] - v1ptr[0];
    float x2 = v3ptr[0] - v1ptr[0];
    float y1 = v2ptr[1] - v1ptr[1];
    float y2 = v3ptr[1] - v1ptr[1];
    float z1 = v2ptr[2] - v1ptr[2];
    float z2 = v3ptr[2] - v1ptr[2];

    float s1 = w2.mV[0] - w1.mV[0];
    float s2 = w3.mV[0] - w1.mV[0];
    float t1 = w2.mV[1] - w1.mV[1];
    float t2 = w3.mV[1] - w1.mV[1];

    F32 rd = s1*t2-s2*t1;

    float r = ((rd*rd) > FLT_EPSILON) ? (1.0f / rd)
                                                : ((rd > 0.0f) ? 1024.f : -1024.f); //some made up large ratio for division by zero

    llassert(llfinite(r));
    llassert(!llisnan(r));

    LLVector4a sdir(
        (t2 * x1 - t1 * x2) * r,
        (t2 * y1 - t1 * y2) * r,
        (t2 * z1 - t1 * z2) * r);

    LLVector4a tdir(
        (s1 * x2 - s2 * x1) * r,
        (s1 * y2 - s2 * y1) * r,
        (s1 * z2 - s2 * z1) * r);

    LLVector4a  n = normal;
    LLVector4a  t = sdir;

    LLVector4a ncrosst;
    ncrosst.setCross3(n,t);

    // Gram-Schmidt orthogonalize
    n.mul(n.dot3(t).getF32());

    LLVector4a tsubn;
    tsubn.setSub(t,n);

    if (tsubn.dot3(tsubn).getF32() > F_APPROXIMATELY_ZERO)
    {
        tsubn.normalize3fast_checked();

        // Calculate handedness
        F32 handedness = ncrosst.dot3(tdir).getF32() < 0.f ? -1.f : 1.f;

        tsubn.getF32ptr()[3] = handedness;

        tangent_out = tsubn;
    }
    else
    {
        // degenerate, make up a value
        //
        tangent_out.set(0,0,1,1);
    }

}


// intersect test between triangle vert0, vert1, vert2 and a ray from orig in direction dir.
// returns true if intersecting and returns barycentric coordinates in intersection_a, intersection_b,
// and returns the intersection point along dir in intersection_t.

// Moller-Trumbore algorithm
bool LLTriangleRayIntersect(const LLVector4a& vert0, const LLVector4a& vert1, const LLVector4a& vert2, const LLVector4a& orig, const LLVector4a& dir,
                            F32& intersection_a, F32& intersection_b, F32& intersection_t)
{

    /* find vectors for two edges sharing vert0 */
    LLVector4a edge1;
    edge1.setSub(vert1, vert0);

    LLVector4a edge2;
    edge2.setSub(vert2, vert0);

    /* begin calculating determinant - also used to calculate U parameter */
    LLVector4a pvec;
    pvec.setCross3(dir, edge2);

    /* if determinant is near zero, ray lies in plane of triangle */
    LLVector4a det;
    det.setAllDot3(edge1, pvec);

    if (det.greaterEqual(LLVector4a::getEpsilon()).getGatheredBits() & 0x7)
    {
        /* calculate distance from vert0 to ray origin */
        LLVector4a tvec;
        tvec.setSub(orig, vert0);

        /* calculate U parameter and test bounds */
        LLVector4a u;
        u.setAllDot3(tvec,pvec);

        if ((u.greaterEqual(LLVector4a::getZero()).getGatheredBits() & 0x7) &&
            (u.lessEqual(det).getGatheredBits() & 0x7))
        {
            /* prepare to test V parameter */
            LLVector4a qvec;
            qvec.setCross3(tvec, edge1);

            /* calculate V parameter and test bounds */
            LLVector4a v;
            v.setAllDot3(dir, qvec);


            //if (!(v < 0.f || u + v > det))

            LLVector4a sum_uv;
            sum_uv.setAdd(u, v);

            S32 v_gequal = v.greaterEqual(LLVector4a::getZero()).getGatheredBits() & 0x7;
            S32 sum_lequal = sum_uv.lessEqual(det).getGatheredBits() & 0x7;

            if (v_gequal  && sum_lequal)
            {
                /* calculate t, scale parameters, ray intersects triangle */
                LLVector4a t;
                t.setAllDot3(edge2,qvec);

                t.div(det);
                u.div(det);
                v.div(det);

                intersection_a = u[0];
                intersection_b = v[0];
                intersection_t = t[0];
                return true;
            }
        }
    }

    return false;
}

bool LLTriangleRayIntersectTwoSided(const LLVector4a& vert0, const LLVector4a& vert1, const LLVector4a& vert2, const LLVector4a& orig, const LLVector4a& dir,
                            F32& intersection_a, F32& intersection_b, F32& intersection_t)
{
    F32 u, v, t;

    /* find vectors for two edges sharing vert0 */
    LLVector4a edge1;
    edge1.setSub(vert1, vert0);


    LLVector4a edge2;
    edge2.setSub(vert2, vert0);

    /* begin calculating determinant - also used to calculate U parameter */
    LLVector4a pvec;
    pvec.setCross3(dir, edge2);

    /* if determinant is near zero, ray lies in plane of triangle */
    F32 det = edge1.dot3(pvec).getF32();


    if (det > -F_APPROXIMATELY_ZERO && det < F_APPROXIMATELY_ZERO)
    {
        return false;
    }

    F32 inv_det = 1.f / det;

    /* calculate distance from vert0 to ray origin */
    LLVector4a tvec;
    tvec.setSub(orig, vert0);

    /* calculate U parameter and test bounds */
    u = (tvec.dot3(pvec).getF32()) * inv_det;
    if (u < 0.f || u > 1.f)
    {
        return false;
    }

    /* prepare to test V parameter */
    tvec.sub(edge1);

    /* calculate V parameter and test bounds */
    v = (dir.dot3(tvec).getF32()) * inv_det;

    if (v < 0.f || u + v > 1.f)
    {
        return false;
    }

    /* calculate t, ray intersects triangle */
    t = (edge2.dot3(tvec).getF32()) * inv_det;

    intersection_a = u;
    intersection_b = v;
    intersection_t = t;


    return true;
}

//-------------------------------------------------------------------
// statics
//-------------------------------------------------------------------


//----------------------------------------------------

LLProfile::Face* LLProfile::addCap(S16 faceID)
{
    Face *face   = vector_append(mFaces, 1);

    face->mIndex = 0;
    face->mCount = mTotal;
    face->mScaleU= 1.0f;
    face->mCap   = true;
    face->mFaceID = faceID;
    return face;
}

LLProfile::Face* LLProfile::addFace(S32 i, S32 count, F32 scaleU, S16 faceID, bool flat)
{
    Face *face   = vector_append(mFaces, 1);

    face->mIndex = i;
    face->mCount = count;
    face->mScaleU= scaleU;

    face->mFlat = flat;
    face->mCap   = false;
    face->mFaceID = faceID;
    return face;
}

//static
S32 LLProfile::getNumNGonPoints(const LLProfileParams& params, S32 sides, F32 offset, F32 bevel, F32 ang_scale, S32 split)
{ // this is basically LLProfile::genNGon stripped down to only the operations that influence the number of points
    S32 np = 0;

    // Generate an n-sided "circular" path.
    // 0 is (1,0), and we go counter-clockwise along a circular path from there.
    F32 t, t_step, t_first, t_fraction;

    F32 begin  = params.getBegin();
    F32 end    = params.getEnd();

    t_step = 1.0f / sides;

    t_first = floor(begin * sides) / (F32)sides;

    // pt1 is the first point on the fractional face.
    // Starting t and ang values for the first face
    t = t_first;

    // Increment to the next point.
    // pt2 is the end point on the fractional face
    t += t_step;

    t_fraction = (begin - t_first)*sides;

    // Only use if it's not almost exactly on an edge.
    if (t_fraction < 0.9999f)
    {
        np++;
    }

    // There's lots of potential here for floating point error to generate unneeded extra points - DJS 04/05/02
    while (t < end)
    {
        // Iterate through all the integer steps of t.
        np++;

        t += t_step;
    }

    t_fraction = (end - (t - t_step))*sides;

    // Find the fraction that we need to add to the end point.
    t_fraction = (end - (t - t_step))*sides;
    if (t_fraction > 0.0001f)
    {
        np++;
    }

    // If we're sliced, the profile is open.
    if ((end - begin)*ang_scale < 0.99f)
    {
        if (params.getHollow() <= 0)
        {
            // put center point if not hollow.
            np++;
        }
    }

    return np;
}

// What is the bevel parameter used for? - DJS 04/05/02
// Bevel parameter is currently unused but presumedly would support
// filleted and chamfered corners
bool LLProfile::genNGon(const LLProfileParams& params, S32 sides, F32 offset, F32 bevel, F32 ang_scale, S32 split)
{
    // Generate an n-sided "circular" path.
    // 0 is (1,0), and we go counter-clockwise along a circular path from there.
    constexpr F32 tableScale[] = { 1, 1, 1, 0.5f, 0.707107f, 0.53f, 0.525f, 0.5f };
    F32 scale = 0.5f;
    F32 t, t_step, t_first, t_fraction, ang, ang_step;
    LLVector4a pt1,pt2;

    F32 begin  = params.getBegin();
    F32 end    = params.getEnd();

    t_step = 1.0f / sides;
    ang_step = 2.0f*F_PI*t_step*ang_scale;

    // Scale to have size "match" scale.  Compensates to get object to generally fill bounding box.

    S32 total_sides = ll_round(sides / ang_scale);  // Total number of sides all around

    if (total_sides < 8)
    {
        scale = tableScale[total_sides];
    }

    t_first = floor(begin * sides) / (F32)sides;

    // Room for every point at once: a cut point at each end, a point at each
    // step with its split points before it, and the cut profile's centre.
    // The array would otherwise grow, and move, a few times on the way.
    const U32 room = mProfile.size() + (U32)((sides + 2) * (split + 1)) + 2;
    if (mProfile.mCapacity < room)
    {
        const U32 count = mProfile.size();
        mProfile.resize(room);
        mProfile.resize(count);
    }

    // A cut point that falls between two steps lies on the chord of its
    // step; its tangent is the circle's at the cut's angle.
    auto keep_cut = [&](F32 cut)
    {
        if (mCutCount < (S32)LL_ARRAY_SIZE(mCutPoints))
        {
            const F32 at = 2.0f*F_PI*(cut*ang_scale + offset);
            mCutPoints[mCutCount] = mProfile.size();
            mCutTangents[mCutCount].set(-sin(at)*scale, cos(at)*scale, 0.f);
            ++mCutCount;
        }
    };

    // pt1 is the first point on the fractional face.
    // Starting t and ang values for the first face
    t = t_first;
    ang = 2.0f*F_PI*(t*ang_scale + offset);
    pt1.set(cos(ang)*scale,sin(ang)*scale, t);

    // Increment to the next point.
    // pt2 is the end point on the fractional face
    t += t_step;
    ang += ang_step;
    pt2.set(cos(ang)*scale,sin(ang)*scale,t);

    t_fraction = (begin - t_first)*sides;

    // Only use if it's not almost exactly on an edge.
    const bool begin_point = t_fraction < 0.9999f;
    if (begin_point)
    {
        LLVector4a new_pt;
        new_pt.setLerp(pt1, pt2, t_fraction);
        if (t_fraction > 0.0001f)
        {
            keep_cut(begin);
        }
        mProfile.push_back(new_pt);
    }

    // There's lots of potential here for floating point error to generate unneeded extra points - DJS 04/05/02
    while (t < end)
    {
        // Iterate through all the integer steps of t.
        pt1.set(cos(ang)*scale,sin(ang)*scale,t);

        if (mProfile.size() > 0) {
            LLVector4a p = mProfile[mProfile.size()-1];
            for (S32 i = 0; i < split && mProfile.size() > 0; i++) {
                //mProfile.push_back(p+(pt1-p) * 1.0f/(float)(split+1) * (float)(i+1));
                LLVector4a new_pt;
                new_pt.setSub(pt1, p);
                new_pt.mul(1.0f/(float)(split+1) * (float)(i+1));
                new_pt.add(p);
                mProfile.push_back(new_pt);
            }
        }
        mProfile.push_back(pt1);

        t += t_step;
        ang += ang_step;
    }

    t_fraction = (end - (t - t_step))*sides;

    // pt1 is the first point on the fractional face
    // pt2 is the end point on the fractional face
    pt2.set(cos(ang)*scale,sin(ang)*scale,t);

    // Find the fraction that we need to add to the end point.
    t_fraction = (end - (t - t_step))*sides;
    if (t_fraction > 0.0001f)
    {
        LLVector4a new_pt;
        new_pt.setLerp(pt1, pt2, t_fraction);

        if (mProfile.size() > 0) {
            LLVector4a p = mProfile[mProfile.size()-1];
            for (S32 i = 0; i < split && mProfile.size() > 0; i++) {
                //mProfile.push_back(p+(new_pt-p) * 1.0f/(float)(split+1) * (float)(i+1));

                LLVector4a pt1;
                pt1.setSub(new_pt, p);
                pt1.mul(1.0f/(float)(split+1) * (float)(i+1));
                pt1.add(p);
                mProfile.push_back(pt1);
            }
        }
        if (t_fraction < 0.9999f)
        {
            keep_cut(end);
        }
        mProfile.push_back(new_pt);
    }

    // If we're sliced, the profile is open.
    if ((end - begin)*ang_scale < 0.99f)
    {
        if ((end - begin)*ang_scale > 0.5f)
        {
            mConcave = true;
        }
        else
        {
            mConcave = false;
        }
        mOpen = true;
        if (params.getHollow() <= 0)
        {
            // put center point if not hollow.
            mProfile.push_back(LLVector4a(0,0,0));
        }
    }
    else
    {
        // The profile isn't open.
        mOpen = false;
        mConcave = false;
    }

    mTotal = mProfile.size();
    return begin_point;
}

// Hollow is percent of the original bounding box, not of this particular
// profile's geometry.  Thus, a swept triangle needs lower hollow values than
// a swept square.
LLProfile::Face* LLProfile::addHole(const LLProfileParams& params, bool flat, F32 sides, F32 offset, F32 box_hollow, F32 ang_scale, S32 split)
{
    // Note that addHole will NOT work for non-"circular" profiles, if we ever decide to use them.

    // Total add has number of vertices on outside.
    mTotalOut = mTotal;

    // Why is the "bevel" parameter -1? DJS 04/05/02
    const bool begin_point = genNGon(params, llfloor(sides),offset,-1, ang_scale, split);

    Face *face = addFace(mTotalOut, mTotal-mTotalOut,0,LL_FACE_INNER_SIDE, flat);

    static thread_local LLAlignedArray<LLVector4a,64> pt;
    pt.resize(mTotal) ;

    for (S32 i=mTotalOut;i<mTotal;i++)
    {
        pt[i] = mProfile[i];
        pt[i].mul(box_hollow);
    }

    // A hole stays inside its wall. A point that would go through it, as a
    // square hole's corners do above 0.707 in a round profile or in a
    // triangle, is drawn in along its own direction to where it meets the
    // wall, which is of no thickness there. The wall is convex about the
    // origin, so the ray from the origin through a point leaves it where it
    // first crosses the line of any of its edges. A point nearer the origin
    // than an edge's line cannot cross it, and an edge of no length or one
    // whose line runs through the origin stops no ray, so the points are
    // only tried where the hole's farthest reaches the line of some edge.
    F32 farthest2 = 0.f;
    for (S32 i=mTotalOut;i<mTotal;i++)
    {
        farthest2 = llmax(farthest2, pt[i][0] * pt[i][0] + pt[i][1] * pt[i][1]);
    }
    bool may_reach = false;
    for (S32 k = 0; k + 1 < mTotalOut && !may_reach; k++)
    {
        const F32 ex = mProfile[k + 1][0] - mProfile[k][0];
        const F32 ey = mProfile[k + 1][1] - mProfile[k][1];
        const F32 toward = mProfile[k][0] * ey - mProfile[k][1] * ex;
        const F32 length2 = ex * ex + ey * ey;
        if (length2 == 0.f || toward == 0.f)
        {
            continue;
        }
        // The line is |toward| / |e| from the origin, whichever way the
        // profile winds.
        may_reach = farthest2 * length2 * 1.0001f >= toward * toward;
    }
    for (S32 i=mTotalOut;i<mTotal && may_reach;i++)
    {
        const F32 px = pt[i][0];
        const F32 py = pt[i][1];
        F32 nearest = 1.f;
        for (S32 k = 0; k + 1 < mTotalOut; k++)
        {
            // Where the ray s * p crosses the line a + u * e: at s = toward /
            // across. A point well inside the line, on the origin's side,
            // crosses it beyond itself.
            const F32 ax = mProfile[k][0];
            const F32 ay = mProfile[k][1];
            const F32 ex = mProfile[k + 1][0] - ax;
            const F32 ey = mProfile[k + 1][1] - ay;
            const F32 toward = ax * ey - ay * ex;
            const F32 across = px * ey - py * ex;
            if ((toward > 0.f && toward - across > 1e-6f) || across == 0.f)
            {
                continue;
            }
            const F32 s = toward / across;
            if (s > 0.f)
            {
                nearest = llmin(nearest, s);
            }
        }
        if (nearest < 1.f)
        {
            pt[i].getF32ptr()[0] = px * nearest;
            pt[i].getF32ptr()[1] = py * nearest;
            mHoleTouchesWall = true;
        }
    }

    S32 j=mTotal-1;
    for (S32 i=mTotalOut;i<mTotal;i++)
    {
        mProfile[i] = pt[j--];
    }

    // The hole runs the other way round, and so do its cut points and their
    // tangents.
    mHoleBegin = mTotalOut;
    for (S32 c = 0; c < mCutCount; ++c)
    {
        if (mCutPoints[c] >= mTotalOut)
        {
            mCutPoints[c] = mTotalOut + mTotal - 1 - mCutPoints[c];
            mCutTangents[c].negate();
        }
    }

    // A flat hole's corners. genNGon laid the points out as its begin point,
    // where it has one, then split points and a corner by turns, split
    // points first, to its end; they run the other way here. A hole drawn
    // in to its wall may fold at any point.
    if (flat)
    {
        const S32 count = mTotal - mTotalOut;
        const S32 step = mHoleTouchesWall ? 1 : split + 1;
        const S32 first_laid = (begin_point ? 1 : 0) + split;
        face->mCornerStep = step;
        face->mFirstCorner = ((count - 1 - first_laid) % step + step) % step;
    }

    for (S32 i=0;i<(S32)mFaces.size();i++)
    {
        if (mFaces[i].mCap)
        {
            mFaces[i].mCount *= 2;
        }
    }

    return face;
}

//static
S32 LLProfile::getNumPoints(const LLProfileParams& params, bool path_open,F32 detail, S32 split,
                         bool is_sculpted, S32 sculpt_size)
{ // this is basically LLProfile::generate stripped down to only operations that influence the number of points
    if (detail < MIN_LOD)
    {
        detail = MIN_LOD;
    }

    // Generate the face data
    F32 hollow = params.getHollow();

    S32 np = 0;

    switch (params.getCurveType() & LL_PCODE_PROFILE_MASK)
    {
    case LL_PCODE_PROFILE_SQUARE:
        {
            np = getNumNGonPoints(params, 4,-0.375, 0, 1, split);

            if (hollow)
            {
                np *= 2;
            }
        }
        break;
    case  LL_PCODE_PROFILE_ISOTRI:
    case  LL_PCODE_PROFILE_RIGHTTRI:
    case  LL_PCODE_PROFILE_EQUALTRI:
        {
            np = getNumNGonPoints(params, 3,0, 0, 1, split);

            if (hollow)
            {
                np *= 2;
            }
        }
        break;
    case LL_PCODE_PROFILE_CIRCLE:
        {
            // If this has a square hollow, we should adjust the
            // number of faces a bit so that the geometry lines up.
            U8 hole_type=0;
            F32 circle_detail = MIN_DETAIL_FACES * detail;
            if (hollow)
            {
                hole_type = params.getCurveType() & LL_PCODE_HOLE_MASK;
                if (hole_type == LL_PCODE_HOLE_SQUARE)
                {
                    // Snap to the next multiple of four sides,
                    // so that corners line up.
                    circle_detail = llceil(circle_detail / 4.0f) * 4.0f;
                }
            }

            S32 sides = (S32)circle_detail;

            if (is_sculpted)
                sides = sculpt_size;

            np = getNumNGonPoints(params, sides);

            if (hollow)
            {
                np *= 2;
            }
        }
        break;
    case LL_PCODE_PROFILE_CIRCLE_HALF:
        {
            // If this has a square hollow, we should adjust the
            // number of faces a bit so that the geometry lines up.
            U8 hole_type=0;
            // Number of faces is cut in half because it's only a half-circle.
            F32 circle_detail = MIN_DETAIL_FACES * detail * 0.5f;
            if (hollow)
            {
                hole_type = params.getCurveType() & LL_PCODE_HOLE_MASK;
                if (hole_type == LL_PCODE_HOLE_SQUARE)
                {
                    // Snap to the next multiple of four sides (div 2),
                    // so that corners line up.
                    circle_detail = llceil(circle_detail / 2.0f) * 2.0f;
                }
            }
            np = getNumNGonPoints(params, llfloor(circle_detail), 0.5f, 0.f, 0.5f);

            if (hollow)
            {
                np *= 2;
            }

            // Special case for openness of sphere
            if ((params.getEnd() - params.getBegin()) < 1.f)
            {
            }
            else if (!hollow)
            {
                np++;
            }
        }
        break;
    default:
       break;
    };


    return np;
}


bool LLProfile::generate(const LLProfileParams& params, bool path_open,F32 detail, S32 split,
                         bool is_sculpted, S32 sculpt_size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if ((!mDirty) && (!is_sculpted))
    {
        return false;
    }
    mDirty = false;

    if (detail < MIN_LOD)
    {
        LL_INFOS() << "Generating profile with LOD < MIN_LOD.  CLAMPING" << LL_ENDL;
        detail = MIN_LOD;
    }

    mProfile.resize(0);
    mFaces.resize(0);
    mHoleTouchesWall = false;
    mCutCount = 0;
    mHoleBegin = S32_MAX;

    // Generate the face data
    S32 i;
    F32 begin = params.getBegin();
    F32 end = params.getEnd();
    F32 hollow = params.getHollow();

    // Quick validation to eliminate some server crashes.
    if (begin > end - 0.01f)
    {
        LL_WARNS() << "LLProfile::generate() assertion failed (begin >= end)" << LL_ENDL;
        return false;
    }

    S32 face_num = 0;

    switch (params.getCurveType() & LL_PCODE_PROFILE_MASK)
    {
    case LL_PCODE_PROFILE_SQUARE:
        {
            genNGon(params, 4,-0.375, 0, 1, split);
            if (path_open)
            {
                addCap (LL_FACE_PATH_BEGIN);
            }

            for (i = llfloor(begin * 4.f); i < llfloor(end * 4.f + .999f); i++)
            {
                addFace((face_num++) * (split +1), split+2, 1, LL_FACE_OUTER_SIDE_0 << i, true);
            }

            LLVector4a scale(1,1,4,1);

            for (i = 0; i <(S32) mProfile.size(); i++)
            {
                // Scale by 4 to generate proper tex coords.
                mProfile[i].mul(scale);
                llassert(mProfile[i].isFinite3());
            }

            if (hollow)
            {
                switch (params.getCurveType() & LL_PCODE_HOLE_MASK)
                {
                case LL_PCODE_HOLE_TRIANGLE:
                    // This offset is not correct, but we can't change it now... DK 11/17/04
                    addHole(params, true, 3, -0.375f, hollow, 1.f, split);
                    break;
                case LL_PCODE_HOLE_CIRCLE:
                    // TODO: Compute actual detail levels for cubes
                    addHole(params, false, MIN_DETAIL_FACES * detail, -0.375f, hollow, 1.f);
                    break;
                case LL_PCODE_HOLE_SAME:
                case LL_PCODE_HOLE_SQUARE:
                default:
                    addHole(params, true, 4, -0.375f, hollow, 1.f, split);
                    break;
                }
            }

            if (path_open) {
                mFaces[0].mCount = mTotal;
            }
        }
        break;
    case  LL_PCODE_PROFILE_ISOTRI:
    case  LL_PCODE_PROFILE_RIGHTTRI:
    case  LL_PCODE_PROFILE_EQUALTRI:
        {
            genNGon(params, 3,0, 0, 1, split);
            LLVector4a scale(1,1,3,1);
            for (i = 0; i <(S32) mProfile.size(); i++)
            {
                // Scale by 3 to generate proper tex coords.
                mProfile[i].mul(scale);
                llassert(mProfile[i].isFinite3());
            }

            if (path_open)
            {
                addCap(LL_FACE_PATH_BEGIN);
            }

            for (i = llfloor(begin * 3.f); i < llfloor(end * 3.f + .999f); i++)
            {
                addFace((face_num++) * (split +1), split+2, 1, LL_FACE_OUTER_SIDE_0 << i, true);
            }
            if (hollow)
            {
                // Swept triangles need smaller hollowness values,
                // because the triangle doesn't fill the bounding box.
                F32 triangle_hollow = hollow / 2.f;

                switch (params.getCurveType() & LL_PCODE_HOLE_MASK)
                {
                case LL_PCODE_HOLE_CIRCLE:
                    // TODO: Actually generate level of detail for triangles
                    addHole(params, false, MIN_DETAIL_FACES * detail, 0, triangle_hollow, 1.f);
                    break;
                case LL_PCODE_HOLE_SQUARE:
                    addHole(params, true, 4, 0, triangle_hollow, 1.f, split);
                    break;
                case LL_PCODE_HOLE_SAME:
                case LL_PCODE_HOLE_TRIANGLE:
                default:
                    addHole(params, true, 3, 0, triangle_hollow, 1.f, split);
                    break;
                }
            }
        }
        break;
    case LL_PCODE_PROFILE_CIRCLE:
        {
            // If this has a square hollow, we should adjust the
            // number of faces a bit so that the geometry lines up.
            U8 hole_type=0;
            F32 circle_detail = MIN_DETAIL_FACES * detail;
            if (hollow)
            {
                hole_type = params.getCurveType() & LL_PCODE_HOLE_MASK;
                if (hole_type == LL_PCODE_HOLE_SQUARE)
                {
                    // Snap to the next multiple of four sides,
                    // so that corners line up.
                    circle_detail = llceil(circle_detail / 4.0f) * 4.0f;
                }
            }

            S32 sides = (S32)circle_detail;

            if (is_sculpted)
                sides = sculpt_size;

            genNGon(params, sides);

            if (path_open)
            {
                addCap (LL_FACE_PATH_BEGIN);
            }

            if (mOpen && !hollow)
            {
                addFace(0,mTotal-1,0,LL_FACE_OUTER_SIDE_0, false);
            }
            else
            {
                addFace(0,mTotal,0,LL_FACE_OUTER_SIDE_0, false);
            }

            if (hollow)
            {
                switch (hole_type)
                {
                case LL_PCODE_HOLE_SQUARE:
                    addHole(params, true, 4, 0, hollow, 1.f, split);
                    break;
                case LL_PCODE_HOLE_TRIANGLE:
                    addHole(params, true, 3, 0, hollow, 1.f, split);
                    break;
                case LL_PCODE_HOLE_CIRCLE:
                case LL_PCODE_HOLE_SAME:
                default:
                    addHole(params, false, circle_detail, 0, hollow, 1.f);
                    break;
                }
            }
        }
        break;
    case LL_PCODE_PROFILE_CIRCLE_HALF:
        {
            // If this has a square hollow, we should adjust the
            // number of faces a bit so that the geometry lines up.
            U8 hole_type=0;
            // Number of faces is cut in half because it's only a half-circle.
            F32 circle_detail = MIN_DETAIL_FACES * detail * 0.5f;
            if (hollow)
            {
                hole_type = params.getCurveType() & LL_PCODE_HOLE_MASK;
                if (hole_type == LL_PCODE_HOLE_SQUARE)
                {
                    // Snap to the next multiple of four sides (div 2),
                    // so that corners line up.
                    circle_detail = llceil(circle_detail / 2.0f) * 2.0f;
                }
            }
            genNGon(params, llfloor(circle_detail), 0.5f, 0.f, 0.5f);
            if (path_open)
            {
                addCap(LL_FACE_PATH_BEGIN);
            }
            if (mOpen && !params.getHollow())
            {
                addFace(0,mTotal-1,0,LL_FACE_OUTER_SIDE_0, false);
            }
            else
            {
                addFace(0,mTotal,0,LL_FACE_OUTER_SIDE_0, false);
            }

            if (hollow)
            {
                switch (hole_type)
                {
                case LL_PCODE_HOLE_SQUARE:
                    addHole(params, true, 2, 0.5f, hollow, 0.5f, split);
                    break;
                case LL_PCODE_HOLE_TRIANGLE:
                    addHole(params, true, 3,  0.5f, hollow, 0.5f, split);
                    break;
                case LL_PCODE_HOLE_CIRCLE:
                case LL_PCODE_HOLE_SAME:
                default:
                    addHole(params, false, circle_detail,  0.5f, hollow, 0.5f);
                    break;
                }
            }

            // Special case for openness of sphere
            if ((params.getEnd() - params.getBegin()) < 1.f)
            {
                mOpen = true;
            }
            else if (!hollow)
            {
                mOpen = false;
                mProfile.push_back(mProfile[0]);
                mTotal++;
            }
        }
        break;
    default:
        LL_ERRS() << "Unknown profile: getCurveType()=" << params.getCurveType() << LL_ENDL;
        break;
    };

    if (path_open)
    {
        addCap(LL_FACE_PATH_END); // bottom
    }

    if ( mOpen) // interior edge caps
    {
        addFace(mTotal-1, 2,0.5,LL_FACE_PROFILE_BEGIN, true);

        if (hollow)
        {
            addFace(mTotalOut-1, 2,0.5,LL_FACE_PROFILE_END, true);
        }
        else
        {
            addFace(mTotal-2, 2,0.5,LL_FACE_PROFILE_END, true);
        }
    }

    return true;
}



bool LLProfileParams::importFile(LLFILE *fp)
{
    const S32 BUFSIZE = 16384;
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    // *NOTE: changing the size or type of these buffers will require
    // changing the sscanf below.
    char keyword[256];  /* Flawfinder: ignore */
    char valuestr[256]; /* Flawfinder: ignore */
    keyword[0] = 0;
    valuestr[0] = 0;
    F32 tempF32;
    U32 tempU32;

    while (!feof(fp))
    {
        if (fgets(buffer, BUFSIZE, fp) == NULL)
        {
            buffer[0] = '\0';
        }

        sscanf( /* Flawfinder: ignore */
            buffer,
            " %255s %255s",
            keyword, valuestr);
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("curve", keyword))
        {
            sscanf(valuestr,"%d",&tempU32);
            setCurveType((U8) tempU32);
        }
        else if (!strcmp("begin",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setBegin(tempF32);
        }
        else if (!strcmp("end",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setEnd(tempF32);
        }
        else if (!strcmp("hollow",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setHollow(tempF32);
        }
        else
        {
            LL_WARNS() << "unknown keyword " << keyword << " in profile import" << LL_ENDL;
        }
    }

    return true;
}


bool LLProfileParams::exportFile(LLFILE *fp) const
{
    fprintf(fp,"\t\tprofile 0\n");
    fprintf(fp,"\t\t{\n");
    fprintf(fp,"\t\t\tcurve\t%d\n", getCurveType());
    fprintf(fp,"\t\t\tbegin\t%g\n", getBegin());
    fprintf(fp,"\t\t\tend\t%g\n", getEnd());
    fprintf(fp,"\t\t\thollow\t%g\n", getHollow());
    fprintf(fp, "\t\t}\n");
    return true;
}


bool LLProfileParams::importLegacyStream(std::istream& input_stream)
{
    const S32 BUFSIZE = 16384;
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    // *NOTE: changing the size or type of these buffers will require
    // changing the sscanf below.
    char keyword[256];  /* Flawfinder: ignore */
    char valuestr[256]; /* Flawfinder: ignore */
    keyword[0] = 0;
    valuestr[0] = 0;
    F32 tempF32;
    U32 tempU32;

    while (input_stream.good())
    {
        input_stream.getline(buffer, BUFSIZE);
        sscanf( /* Flawfinder: ignore */
            buffer,
            " %255s %255s",
            keyword,
            valuestr);
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("curve", keyword))
        {
            sscanf(valuestr,"%d",&tempU32);
            setCurveType((U8) tempU32);
        }
        else if (!strcmp("begin",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setBegin(tempF32);
        }
        else if (!strcmp("end",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setEnd(tempF32);
        }
        else if (!strcmp("hollow",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setHollow(tempF32);
        }
        else
        {
        LL_WARNS() << "unknown keyword " << keyword << " in profile import" << LL_ENDL;
        }
    }

    return true;
}


bool LLProfileParams::exportLegacyStream(std::ostream& output_stream) const
{
    output_stream <<"\t\tprofile 0\n";
    output_stream <<"\t\t{\n";
    output_stream <<"\t\t\tcurve\t" << (S32) getCurveType() << "\n";
    output_stream <<"\t\t\tbegin\t" << getBegin() << "\n";
    output_stream <<"\t\t\tend\t" << getEnd() << "\n";
    output_stream <<"\t\t\thollow\t" << getHollow() << "\n";
    output_stream << "\t\t}\n";
    return true;
}

LLSD LLProfileParams::asLLSD() const
{
    LLSD sd;

    sd["curve"] = getCurveType();
    sd["begin"] = getBegin();
    sd["end"] = getEnd();
    sd["hollow"] = getHollow();
    return sd;
}

bool LLProfileParams::fromLLSD(LLSD& sd)
{
    setCurveType(sd["curve"].asInteger());
    setBegin((F32)sd["begin"].asReal());
    setEnd((F32)sd["end"].asReal());
    setHollow((F32)sd["hollow"].asReal());
    return true;
}

void LLProfileParams::copyParams(const LLProfileParams &params)
{
    setCurveType(params.getCurveType());
    setBegin(params.getBegin());
    setEnd(params.getEnd());
    setHollow(params.getHollow());
}


LLPath::~LLPath()
{
}

S32 LLPath::getNumNGonPoints(const LLPathParams& params, S32 sides, F32 startOff, F32 end_scale, F32 twist_scale)
{ //this is basically LLPath::genNGon stripped down to only operations that influence the number of points added
    S32 ret = 0;

    F32 step= 1.0f / sides;
    F32 t   = params.getBegin();
    ret = 1;

    t+=step;

    // Snap to a quantized parameter, so that cut does not
    // affect most sample points.
    t = ((S32)(t * sides)) / (F32)sides;

    // Run through the non-cut dependent points, as genNGon does.
    const F32 end_before = params.getEnd() - step * 0.01f;
    while (t < end_before)
    {
        ret++;
        t+=step;
    }

    ret++;

    return ret;
}

namespace
{
    // A station's frame from the first two rows of its rotation, R, its
    // scale, S, and how fast each changes: M = S R, and M' = S' R + S R'.
    void set_frame(LLPath::PathFrame& frame, const LLVector4a& row0, const LLVector4a& row1, F32 scale_x, F32 scale_y, F32 scale_x_rate,
                   F32 scale_y_rate, const LLVector4a& row0_rate, const LLVector4a& row1_rate, const LLVector4a& velocity)
    {
        LLVector4a turning;
        frame.mRow[0] = row0;
        frame.mRow[0].mul(scale_x);
        frame.mRow[1] = row1;
        frame.mRow[1].mul(scale_y);
        frame.mRate[0] = row0;
        frame.mRate[0].mul(scale_x_rate);
        turning = row0_rate;
        turning.mul(scale_x);
        frame.mRate[0].add(turning);
        frame.mRate[1] = row1;
        frame.mRate[1].mul(scale_y_rate);
        turning = row1_rate;
        turning.mul(scale_y);
        frame.mRate[1].add(turning);
        frame.mVelocity = velocity;
    }

    // A circular path as LLPath::genNGon lays it out: its station at any t,
    // and how fast the station changes there, per unit of t. A station's
    // rotation is a twist about the profile's z, then a turn about the
    // path's x, each at a steady rate about its own axis, so it changes as
    // twist' Kz R + turn' R Kx: Kz R is R's second row, its first negated,
    // and nothing, and R Kx crosses x with each row.
    struct CirclePath
    {
        F32 revolutions;
        F32 skew;
        F32 shear_x;
        F32 shear_y;
        F32 hole_x;
        F32 hole_y;
        F32 taper_x_begin;
        F32 taper_x_end;
        F32 taper_y_begin;
        F32 taper_y_end;
        F32 radius_start;
        F32 radius_end;
        F32 twist_begin;
        F32 twist_end;

        void station(F32 t, LLPath::PathPt& pt, LLPath::PathFrame* frame) const
        {
            const F32 ang = 2.0f*F_PI*revolutions * t;
            const F32 c   = cos(ang)*lerp(radius_start, radius_end, t);
            const F32 s   = sin(ang)*lerp(radius_start, radius_end, t);

            pt.mPos.set(0 + lerp(0.f, shear_x, s)
                              + lerp(-skew ,skew, t) * 0.5f,
                        c + lerp(0.f, shear_y, s),
                        s);

            pt.mScale.set(hole_x * lerp(taper_x_begin, taper_x_end, t),
                          hole_y * lerp(taper_y_begin, taper_y_end, t),
                          0.f, 1.f);
            pt.mTexT  = t;

            // Twist rotates the path along the x,y plane (I think) - DJS 04/05/02
            LLQuaternion twist, qang;
            twist.setQuat  (lerp(twist_begin,twist_end,t) * 2.f * F_PI - F_PI,0,0,1);
            // Rotate the point around the circle's center.
            qang.setQuat   (ang,LLVector3(1.f, 0.f, 0.f));
            LLMatrix3 rot(twist*qang);
            pt.mRot.set(rot);

            if (!frame)
            {
                return;
            }

            // The turn's cosine and sine are the rotation's last row, (0,
            // -sin, cos), which the twist about z leaves as it is.
            const LLVector4a& row0 = pt.mRot.getRow<0>();
            const LLVector4a& row1 = pt.mRot.getRow<1>();
            const LLVector4a& row2 = pt.mRot.getRow<2>();
            const F32 turn_rate = 2.0f*F_PI*revolutions;
            const F32 radius_rate = radius_end - radius_start;
            const F32 dc = -s*turn_rate + row2[2]*radius_rate;
            const F32 ds = c*turn_rate - row2[1]*radius_rate;
            const LLVector4a velocity(shear_x * ds + skew, dc + shear_y * ds, ds);

            const F32 twist_rate = (twist_end - twist_begin) * 2.f * F_PI;
            const LLVector4a x_axis(1.f, 0.f, 0.f);
            LLVector4a row0_rate, row1_rate, twisting;
            row0_rate.setCross3(x_axis, row0);
            row0_rate.mul(turn_rate);
            twisting = row1;
            twisting.mul(twist_rate);
            row0_rate.add(twisting);
            row1_rate.setCross3(x_axis, row1);
            row1_rate.mul(turn_rate);
            twisting = row0;
            twisting.mul(twist_rate);
            row1_rate.sub(twisting);
            set_frame(*frame, row0, row1, pt.mScale[0], pt.mScale[1], hole_x * (taper_x_end - taper_x_begin), hole_y * (taper_y_end - taper_y_begin),
                      row0_rate, row1_rate, velocity);
        }
    };
}

void LLPath::genNGon(const LLPathParams& params, S32 sides, F32 startOff, F32 end_scale, F32 twist_scale, bool is_sculpted)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    // Generates a circular path, starting at (1, 0, 0), counterclockwise along the xz plane.
    constexpr F32 tableScale[] = { 1, 1, 1, 0.5f, 0.707107f, 0.53f, 0.525f, 0.5f };

    CirclePath circle;
    circle.revolutions = params.getRevolutions();
    circle.skew        = params.getSkew();
    circle.shear_x     = params.getShear().mV[VX];
    circle.shear_y     = params.getShear().mV[VY];
    F32 skew_mag       = fabs(circle.skew);
    circle.hole_x      = params.getScaleX() * (1.0f - skew_mag);
    circle.hole_y      = params.getScaleY();

    // Calculate taper begin/end for x,y (Negative means taper the beginning)
    circle.taper_x_begin = 1.0f;
    circle.taper_x_end   = 1.0f - params.getTaperX();
    circle.taper_y_begin = 1.0f;
    circle.taper_y_end   = 1.0f - params.getTaperY();

    if ( circle.taper_x_end > 1.0f )
    {
        // Flip tapering.
        circle.taper_x_begin = 2.0f - circle.taper_x_end;
        circle.taper_x_end   = 1.0f;
    }
    if ( circle.taper_y_end > 1.0f )
    {
        // Flip tapering.
        circle.taper_y_begin = 2.0f - circle.taper_y_end;
        circle.taper_y_end   = 1.0f;
    }

    // For spheres, the radius is usually zero.
    F32 radius_start = 0.5f;
    if (sides < 8)
    {
        radius_start = tableScale[sides];
    }

    // Scale the radius to take the hole size into account.
    radius_start *= 1.0f - circle.hole_y;

    // Now check the radius offset to calculate the start,end radius.  (Negative means
    // decrease the start radius instead).
    F32 radius_end    = radius_start;
    F32 radius_offset = params.getRadiusOffset();
    if (radius_offset < 0.f)
    {
        radius_start *= 1.f + radius_offset;
    }
    else
    {
        radius_end   *= 1.f - radius_offset;
    }
    circle.radius_start = radius_start;
    circle.radius_end   = radius_end;

    // Is the path NOT a closed loop?
    mOpen = ( (params.getEnd()*end_scale - params.getBegin() < 1.0f) ||
              (skew_mag > 0.001f) ||
              (fabs(circle.taper_x_end - circle.taper_x_begin) > 0.001f) ||
              (fabs(circle.taper_y_end - circle.taper_y_begin) > 0.001f) ||
              (fabs(radius_end - radius_start) > 0.001f) );

    circle.twist_begin = params.getTwistBegin() * twist_scale;
    circle.twist_end   = params.getTwist() * twist_scale;

    // A sculpt's normals come from its points.
    mHasFrames = !is_sculpted;
    auto station = [&](F32 t)
    {
        PathPt& pt = *mPath.append(1);
        circle.station(t, pt, is_sculpted ? nullptr : &pt.mFrame);
    };

    // Room for every station at once, the begin's, a step's each and the
    // end's: the array would otherwise grow, and move, a few times on the
    // way.
    const U32 room = (U32)sides + 3;
    if (mPath.mCapacity < room)
    {
        const U32 count = mPath.size();
        mPath.resize(room);
        mPath.resize(count);
    }

    // We run through this once before the main loop, to make sure
    // the path begins at the correct cut.
    F32 step= 1.0f / sides;
    F32 t   = params.getBegin();
    station(t);

    t+=step;

    // Snap to a quantized parameter, so that cut does not
    // affect most sample points.
    t = ((S32)(t * sides)) / (F32)sides;

    // A step the snap brings within a hundredth of a step of the begin cut,
    // as it does a cut just short of a step, or the first step where the
    // rounding of the step's size takes it back to the cut, is the begin's
    // own: it would leave a row with no height after the begin's. A sculpt
    // keeps it, as it keeps the end's, and so does a circle2 path, a spiral
    // whose stations alternate sides: leaving one out would turn every
    // station after it the other way.
    if (!is_sculpted && (params.getCurveType() & 0xf0) == LL_PCODE_PATH_CIRCLE && t - params.getBegin() < step * 0.01f)
    {
        t += step;
    }

    // Run through the non-cut dependent points. A point within a hundredth
    // of a step of the end cut is the end's own, short of it only by the
    // rounding the steps add up to: it would leave a row with no height
    // before the end's. A sculpt keeps it: its rows are the rows of its map
    // it samples, one a station, as every viewer samples them.
    const F32 end_before = is_sculpted ? params.getEnd() : params.getEnd() - step * 0.01f;
    while (t < end_before)
    {
        station(t);
        t+=step;
    }

    // Make one final pass for the end cut.
    station(params.getEnd());

    mTotal = mPath.size();
}

const LLVector2 LLPathParams::getBeginScale() const
{
    LLVector2 begin_scale(1.f, 1.f);
    if (getScaleX() > 1)
    {
        begin_scale.mV[0] = 2-getScaleX();
    }
    if (getScaleY() > 1)
    {
        begin_scale.mV[1] = 2-getScaleY();
    }
    return begin_scale;
}

const LLVector2 LLPathParams::getEndScale() const
{
    LLVector2 end_scale(1.f, 1.f);
    if (getScaleX() < 1)
    {
        end_scale.mV[0] = getScaleX();
    }
    if (getScaleY() < 1)
    {
        end_scale.mV[1] = getScaleY();
    }
    return end_scale;
}

S32 LLPath::getNumPoints(const LLPathParams& params, F32 detail)
{ // this is basically LLPath::generate stripped down to only the operations that influence the number of points
    if (detail < MIN_LOD)
    {
        detail = MIN_LOD;
    }

    S32 np = 2; // hardcode for line

    // Is this 0xf0 mask really necessary?  DK 03/02/05

    switch (params.getCurveType() & 0xf0)
    {
    default:
    case LL_PCODE_PATH_LINE:
        {
            // Take the begin/end twist into account for detail.
            np    = llfloor(fabs(params.getTwistBegin() - params.getTwist()) * 3.5f * (detail-0.5f)) + 2;
        }
        break;

    case LL_PCODE_PATH_CIRCLE:
        {
            // Increase the detail as the revolutions and twist increase.
            F32 twist_mag = fabs(params.getTwistBegin() - params.getTwist());

            S32 sides = (S32)llfloor(llfloor((MIN_DETAIL_FACES * detail + twist_mag * 3.5f * (detail-0.5f))) * params.getRevolutions());

            np = sides;
        }
        break;

    case LL_PCODE_PATH_CIRCLE2:
        {
            //genNGon(params, llfloor(MIN_DETAIL_FACES * detail), 4.f, 0.f);
            np = getNumNGonPoints(params, llfloor(MIN_DETAIL_FACES * detail));
        }
        break;

    case LL_PCODE_PATH_TEST:

        np     = 5;
        break;
    };

    return np;
}

bool LLPath::generate(const LLPathParams& params, F32 detail, S32 split,
                      bool is_sculpted, S32 sculpt_size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if ((!mDirty) && (!is_sculpted))
    {
        return false;
    }

    if (detail < MIN_LOD)
    {
        LL_INFOS() << "Generating path with LOD < MIN!  Clamping to 1" << LL_ENDL;
        detail = MIN_LOD;
    }

    mDirty = false;
    S32 np = 2; // hardcode for line

    mPath.resize(0);
    mHasFrames = false;
    mFramesRepeat = false;
    mFramesTurn = false;
    mOpen = true;

    // Is this 0xf0 mask really necessary?  DK 03/02/05
    switch (params.getCurveType() & 0xf0)
    {
    default:
    case LL_PCODE_PATH_LINE:
        {
            // Take the begin/end twist into account for detail.
            np    = llfloor(fabs(params.getTwistBegin() - params.getTwist()) * 3.5f * (detail-0.5f)) + 2;
            if (np < split+2)
            {
                np = split+2;
            }

            mStep = 1.0f / (np-1);

            mPath.resize(np);

            LLVector2 start_scale = params.getBeginScale();
            LLVector2 end_scale = params.getEndScale();

            // Along a line every station moves, scales and twists at one
            // rate: the rotation turns about z, and so changes as
            // twist' Kz R, R's second row and its first negated.
            const F32 twist_rate = F_PI * params.getTwist() - F_PI * params.getTwistBegin();
            const F32 scale_x_rate = end_scale.mV[0] - start_scale.mV[0];
            const F32 scale_y_rate = end_scale.mV[1] - start_scale.mV[1];
            const LLVector4a velocity(params.getShear().mV[VX], params.getShear().mV[VY], 1.f);
            mHasFrames = !is_sculpted;
            mFramesTurn = mHasFrames && scale_x_rate == 0.f && scale_y_rate == 0.f && params.getShear().mV[VX] == 0.f &&
                          params.getShear().mV[VY] == 0.f;
            mFramesRepeat = mHasFrames && twist_rate == 0.f && scale_x_rate == 0.f && scale_y_rate == 0.f;

            for (S32 i=0;i<np;i++)
            {
                F32 t = lerp(params.getBegin(),params.getEnd(),(F32)i * mStep);
                mPath[i].mPos.set(lerp(0.f, params.getShear().mV[VX], t),
                                     lerp(0.f ,params.getShear().mV[VY], t),
                                     t - 0.5f);
                LLQuaternion quat;
                quat.setQuat(lerp(F_PI * params.getTwistBegin(),F_PI * params.getTwist(),t),0,0,1);
                LLMatrix3 tmp(quat);
                mPath[i].mRot.set(tmp);
                mPath[i].mScale.set(lerp(start_scale.mV[0],end_scale.mV[0],t),
                                    lerp(start_scale.mV[1],end_scale.mV[1],t),
                                    0,1);
                mPath[i].mTexT        = t;

                if (!is_sculpted)
                {
                    const LLVector4a& row0 = mPath[i].mRot.getRow<0>();
                    const LLVector4a& row1 = mPath[i].mRot.getRow<1>();
                    LLVector4a row0_rate = row1;
                    row0_rate.mul(twist_rate);
                    LLVector4a row1_rate = row0;
                    row1_rate.mul(-twist_rate);
                    set_frame(mPath[i].mFrame, row0, row1, mPath[i].mScale[0], mPath[i].mScale[1], scale_x_rate, scale_y_rate, row0_rate, row1_rate,
                              velocity);
                }
            }
        }
        break;

    case LL_PCODE_PATH_CIRCLE:
        {
            // Increase the detail as the revolutions and twist increase.
            F32 twist_mag = fabs(params.getTwistBegin() - params.getTwist());

            S32 sides = (S32)llfloor(llfloor((MIN_DETAIL_FACES * detail + twist_mag * 3.5f * (detail-0.5f))) * params.getRevolutions());

            if (is_sculpted)
                sides = llmax(sculpt_size, 1);

            if (0 < sides)
                genNGon(params, sides, 0.f, 1.f, 1.f, is_sculpted);
        }
        break;

    case LL_PCODE_PATH_CIRCLE2:
        {
            if (params.getEnd() - params.getBegin() >= 0.99f &&
                params.getScaleX() >= .99f)
            {
                mOpen = false;
            }

            //genNGon(params, llfloor(MIN_DETAIL_FACES * detail), 4.f, 0.f);
            genNGon(params, llfloor(MIN_DETAIL_FACES * detail));
            mHasFrames = false;

            F32 toggle = 0.5f;
            for (S32 i=0;i<(S32)mPath.size();i++)
            {
                mPath[i].mPos.getF32ptr()[0] = toggle;
                if (toggle == 0.5f)
                    toggle = -0.5f;
                else
                    toggle = 0.5f;
            }
        }

        break;

    case LL_PCODE_PATH_TEST:

        np     = 5;
        mStep = 1.0f / (np-1);

        mPath.resize(np);

        for (S32 i=0;i<np;i++)
        {
            F32 t = (F32)i * mStep;
            mPath[i].mPos.set(0,
                                lerp(0.f,  -sin(F_PI*params.getTwist() * t) * 0.5f, t),
                                lerp(-0.5f, cos(F_PI*params.getTwist() * t) * 0.5f, t));
            mPath[i].mScale.set(lerp(1.f, params.getScale().mV[VX], t),
                                lerp(1.f, params.getScale().mV[VY], t), 0.f, 1.f);
            mPath[i].mTexT  = t;
            LLQuaternion quat;
            quat.setQuat(F_PI * params.getTwist() * t,1,0,0);
            LLMatrix3 tmp(quat);
            mPath[i].mRot.set(tmp);
        }

        break;
    };

    if (params.getTwist() != params.getTwistBegin()) mOpen = true;

    //if ((int(fabsf(params.getTwist() - params.getTwistBegin())*100))%100 != 0) {
    //  mOpen = true;
    //}

    return true;
}

bool LLDynamicPath::generate(const LLPathParams& params, F32 detail, S32 split,
                             bool is_sculpted, S32 sculpt_size)
{
    mOpen = true; // Draw end caps
    if (getPathLength() == 0)
    {
        // Path hasn't been generated yet.
        // Some algorithms later assume at least TWO path points.
        resizePath(2);
        LLQuaternion quat;
        quat.setQuat(0,0,0);
        LLMatrix3 tmp(quat);

        for (U32 i = 0; i < 2; i++)
        {
            mPath[i].mPos.set(0, 0, 0);
            mPath[i].mRot.set(tmp);
            mPath[i].mScale.set(1, 1, 0, 1);
            mPath[i].mTexT = 0;
        }
    }

    return true;
}


bool LLPathParams::importFile(LLFILE *fp)
{
    const S32 BUFSIZE = 16384;
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    // *NOTE: changing the size or type of these buffers will require
    // changing the sscanf below.
    char keyword[256];  /* Flawfinder: ignore */
    char valuestr[256]; /* Flawfinder: ignore */
    keyword[0] = 0;
    valuestr[0] = 0;

    F32 tempF32;
    F32 x, y;
    U32 tempU32;

    while (!feof(fp))
    {
        if (fgets(buffer, BUFSIZE, fp) == NULL)
        {
            buffer[0] = '\0';
        }

        sscanf( /* Flawfinder: ignore */
            buffer,
            " %255s %255s",
            keyword, valuestr);
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("curve", keyword))
        {
            sscanf(valuestr,"%d",&tempU32);
            setCurveType((U8) tempU32);
        }
        else if (!strcmp("begin",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setBegin(tempF32);
        }
        else if (!strcmp("end",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setEnd(tempF32);
        }
        else if (!strcmp("scale",keyword))
        {
            // Legacy for one dimensional scale per path
            sscanf(valuestr,"%g",&tempF32);
            setScale(tempF32, tempF32);
        }
        else if (!strcmp("scale_x", keyword))
        {
            sscanf(valuestr, "%g", &x);
            setScaleX(x);
        }
        else if (!strcmp("scale_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setScaleY(y);
        }
        else if (!strcmp("shear_x", keyword))
        {
            sscanf(valuestr, "%g", &x);
            setShearX(x);
        }
        else if (!strcmp("shear_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setShearY(y);
        }
        else if (!strcmp("twist",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setTwist(tempF32);
        }
        else if (!strcmp("twist_begin", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTwistBegin(y);
        }
        else if (!strcmp("radius_offset", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setRadiusOffset(y);
        }
        else if (!strcmp("taper_x", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTaperX(y);
        }
        else if (!strcmp("taper_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTaperY(y);
        }
        else if (!strcmp("revolutions", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setRevolutions(y);
        }
        else if (!strcmp("skew", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setSkew(y);
        }
        else
        {
            LL_WARNS() << "unknown keyword " << " in path import" << LL_ENDL;
        }
    }
    return true;
}


bool LLPathParams::exportFile(LLFILE *fp) const
{
    fprintf(fp, "\t\tpath 0\n");
    fprintf(fp, "\t\t{\n");
    fprintf(fp, "\t\t\tcurve\t%d\n", getCurveType());
    fprintf(fp, "\t\t\tbegin\t%g\n", getBegin());
    fprintf(fp, "\t\t\tend\t%g\n", getEnd());
    fprintf(fp, "\t\t\tscale_x\t%g\n", getScaleX() );
    fprintf(fp, "\t\t\tscale_y\t%g\n", getScaleY() );
    fprintf(fp, "\t\t\tshear_x\t%g\n", getShearX() );
    fprintf(fp, "\t\t\tshear_y\t%g\n", getShearY() );
    fprintf(fp,"\t\t\ttwist\t%g\n", getTwist());

    fprintf(fp,"\t\t\ttwist_begin\t%g\n", getTwistBegin());
    fprintf(fp,"\t\t\tradius_offset\t%g\n", getRadiusOffset());
    fprintf(fp,"\t\t\ttaper_x\t%g\n", getTaperX());
    fprintf(fp,"\t\t\ttaper_y\t%g\n", getTaperY());
    fprintf(fp,"\t\t\trevolutions\t%g\n", getRevolutions());
    fprintf(fp,"\t\t\tskew\t%g\n", getSkew());

    fprintf(fp, "\t\t}\n");
    return true;
}


bool LLPathParams::importLegacyStream(std::istream& input_stream)
{
    const S32 BUFSIZE = 16384;
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    // *NOTE: changing the size or type of these buffers will require
    // changing the sscanf below.
    char keyword[256];  /* Flawfinder: ignore */
    char valuestr[256]; /* Flawfinder: ignore */
    keyword[0] = 0;
    valuestr[0] = 0;

    F32 tempF32;
    F32 x, y;
    U32 tempU32;

    while (input_stream.good())
    {
        input_stream.getline(buffer, BUFSIZE);
        sscanf( /* Flawfinder: ignore */
            buffer,
            " %255s %255s",
            keyword, valuestr);
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("curve", keyword))
        {
            sscanf(valuestr,"%d",&tempU32);
            setCurveType((U8) tempU32);
        }
        else if (!strcmp("begin",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setBegin(tempF32);
        }
        else if (!strcmp("end",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setEnd(tempF32);
        }
        else if (!strcmp("scale",keyword))
        {
            // Legacy for one dimensional scale per path
            sscanf(valuestr,"%g",&tempF32);
            setScale(tempF32, tempF32);
        }
        else if (!strcmp("scale_x", keyword))
        {
            sscanf(valuestr, "%g", &x);
            setScaleX(x);
        }
        else if (!strcmp("scale_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setScaleY(y);
        }
        else if (!strcmp("shear_x", keyword))
        {
            sscanf(valuestr, "%g", &x);
            setShearX(x);
        }
        else if (!strcmp("shear_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setShearY(y);
        }
        else if (!strcmp("twist",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setTwist(tempF32);
        }
        else if (!strcmp("twist_begin", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTwistBegin(y);
        }
        else if (!strcmp("radius_offset", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setRadiusOffset(y);
        }
        else if (!strcmp("taper_x", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTaperX(y);
        }
        else if (!strcmp("taper_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTaperY(y);
        }
        else if (!strcmp("revolutions", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setRevolutions(y);
        }
        else if (!strcmp("skew", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setSkew(y);
        }
        else
        {
            LL_WARNS() << "unknown keyword " << " in path import" << LL_ENDL;
        }
    }
    return true;
}


bool LLPathParams::exportLegacyStream(std::ostream& output_stream) const
{
    output_stream << "\t\tpath 0\n";
    output_stream << "\t\t{\n";
    output_stream << "\t\t\tcurve\t" << (S32) getCurveType() << "\n";
    output_stream << "\t\t\tbegin\t" << getBegin() << "\n";
    output_stream << "\t\t\tend\t" << getEnd() << "\n";
    output_stream << "\t\t\tscale_x\t" << getScaleX()  << "\n";
    output_stream << "\t\t\tscale_y\t" << getScaleY()  << "\n";
    output_stream << "\t\t\tshear_x\t" << getShearX()  << "\n";
    output_stream << "\t\t\tshear_y\t" << getShearY()  << "\n";
    output_stream <<"\t\t\ttwist\t" << getTwist() << "\n";

    output_stream <<"\t\t\ttwist_begin\t" << getTwistBegin() << "\n";
    output_stream <<"\t\t\tradius_offset\t" << getRadiusOffset() << "\n";
    output_stream <<"\t\t\ttaper_x\t" << getTaperX() << "\n";
    output_stream <<"\t\t\ttaper_y\t" << getTaperY() << "\n";
    output_stream <<"\t\t\trevolutions\t" << getRevolutions() << "\n";
    output_stream <<"\t\t\tskew\t" << getSkew() << "\n";

    output_stream << "\t\t}\n";
    return true;
}

LLSD LLPathParams::asLLSD() const
{
    LLSD sd = LLSD();
    sd["curve"] = getCurveType();
    sd["begin"] = getBegin();
    sd["end"] = getEnd();
    sd["scale_x"] = getScaleX();
    sd["scale_y"] = getScaleY();
    sd["shear_x"] = getShearX();
    sd["shear_y"] = getShearY();
    sd["twist"] = getTwist();
    sd["twist_begin"] = getTwistBegin();
    sd["radius_offset"] = getRadiusOffset();
    sd["taper_x"] = getTaperX();
    sd["taper_y"] = getTaperY();
    sd["revolutions"] = getRevolutions();
    sd["skew"] = getSkew();

    return sd;
}

bool LLPathParams::fromLLSD(LLSD& sd)
{
    setCurveType(sd["curve"].asInteger());
    setBegin((F32)sd["begin"].asReal());
    setEnd((F32)sd["end"].asReal());
    setScaleX((F32)sd["scale_x"].asReal());
    setScaleY((F32)sd["scale_y"].asReal());
    setShearX((F32)sd["shear_x"].asReal());
    setShearY((F32)sd["shear_y"].asReal());
    setTwist((F32)sd["twist"].asReal());
    setTwistBegin((F32)sd["twist_begin"].asReal());
    setRadiusOffset((F32)sd["radius_offset"].asReal());
    setTaperX((F32)sd["taper_x"].asReal());
    setTaperY((F32)sd["taper_y"].asReal());
    setRevolutions((F32)sd["revolutions"].asReal());
    setSkew((F32)sd["skew"].asReal());
    return true;
}

void LLPathParams::copyParams(const LLPathParams &params)
{
    setCurveType(params.getCurveType());
    setBegin(params.getBegin());
    setEnd(params.getEnd());
    setScale(params.getScaleX(), params.getScaleY() );
    setShear(params.getShearX(), params.getShearY() );
    setTwist(params.getTwist());
    setTwistBegin(params.getTwistBegin());
    setRadiusOffset(params.getRadiusOffset());
    setTaper( params.getTaperX(), params.getTaperY() );
    setRevolutions(params.getRevolutions());
    setSkew(params.getSkew());
}

LLProfile::~LLProfile()
{
}



LLVolume::LLVolume(const LLVolumeParams &params, const F32 detail, const bool generate_single_face, const bool is_unique)
    : mParams(params)
{
    mUnique = is_unique;
    mLODInvariant = false;
    mFaceMask = 0x0;
    mDetail = detail;
    mSculptLevel = -2;
    mSurfaceArea = 1.f; //only calculated for sculpts, defaults to 1 for all other prims
    mIsMeshAssetLoaded = false;
    mIsMeshAssetUnavaliable = false;
    mLODScaleBias.setVec(1,1,1);
    mHullPoints = nullptr;
    mHullIndices = nullptr;
    mNumHullPoints = 0;
    mNumHullIndices = 0;

    // set defaults
    if (mParams.getPathParams().getCurveType() == LL_PCODE_PATH_FLEXIBLE)
    {
        mPathp = new LLDynamicPath();
    }
    else
    {
        mPathp = new LLPath();
    }
    mProfilep = new LLProfile();

    mGenerateSingleFace = generate_single_face;

    generate();

    if ((mParams.getSculptID().isNull() && mParams.getSculptType() == LL_SCULPT_TYPE_NONE) || mParams.getSculptType() == LL_SCULPT_TYPE_MESH)
    {
        createVolumeFaces();
    }
}

void LLVolume::resizePath(S32 length)
{
    mPathp->resizePath(length);
    mVolumeFaces.clear();
    setDirty();
}

void LLVolume::regen()
{
    generate();
    createVolumeFaces();
}

void LLVolume::genTangents(S32 face)
{
    // generate legacy tangents for the specified face
    llassert(!isMeshAssetLoaded() || mVolumeFaces[face].mTangents != nullptr); // if this is a complete mesh asset, we should already have tangents
    mVolumeFaces[face].createTangents();
}

LLVolume::~LLVolume()
{
    delete mPathp;

    delete mProfilep;

    mPathp = NULL;
    mProfilep = NULL;
    mVolumeFaces.clear();

    ll_aligned_free_16(mHullPoints);
    mHullPoints = NULL;
    ll_aligned_free_16(mHullIndices);
    mHullIndices = NULL;
}

namespace
{
    // Which details split a prim's profile edges.
    enum class ProfileSplit
    {
        // Every detail splits them as it asks.
        BY_DETAIL,
        // None does.
        NONE,
        // None does unless the profile's hole is drawn in to its wall, which
        // takes the split its detail asks for.
        NONE_UNLESS_HOLE_TOUCHES_WALL,
    };

    ProfileSplit profile_split(const LLVolumeParams& params)
    {
        const LLPathParams& path_params = params.getPathParams();
        if (path_params.getCurveType() != LL_PCODE_PATH_LINE)
        {
            return ProfileSplit::BY_DETAIL;
        }

        // A straight path tapering a square or a triangle whose hole type is
        // the default, the profile's own shape.
        const U8 profile_curve = params.getProfileParams().getCurveType();
        const bool tapered = path_params.getScale().mV[0] != 1.0f || path_params.getScale().mV[1] != 1.0f;
        if (tapered)
        {
            return profile_curve == LL_PCODE_PROFILE_SQUARE || profile_curve == LL_PCODE_PROFILE_ISOTRI ||
                           profile_curve == LL_PCODE_PROFILE_EQUALTRI || profile_curve == LL_PCODE_PROFILE_RIGHTTRI
                       ? ProfileSplit::NONE
                       : ProfileSplit::BY_DETAIL;
        }

        // A straight path that neither twists nor tapers sweeps every profile
        // edge into a parallelogram, and every face is flat or ruled straight
        // along the path: positions and UVs are affine over each quad, and the
        // split points and the rings between the ends lie on the triangles the
        // ends make. A hole drawn in to its wall is the exception: the wall
        // draws in some of its points and not others, its edges no longer run
        // straight between its corners, and its split points shape it.
        return path_params.getTwistBegin() == path_params.getTwist() ? ProfileSplit::NONE_UNLESS_HOLE_TOUCHES_WALL : ProfileSplit::BY_DETAIL;
    }

    // Whether a prim's parameters build it the same at every detail, as far
    // as they say without laying its profile out.
    enum class LODInvariance
    {
        NONE,
        INVARIANT,
        INVARIANT_UNLESS_HOLE_TOUCHES_WALL,
    };

    LODInvariance lod_invariance(const LLVolumeParams& params)
    {
        // A sculpt or a mesh has a resolution of its own.
        if (params.getSculptType() != LL_SCULPT_TYPE_NONE || params.getSculptID().notNull())
        {
            return LODInvariance::NONE;
        }

        // The profile must take no split at any detail, and the path its two
        // ends alone, which a line path does where it does not twist.
        const ProfileSplit split = profile_split(params);
        const LLPathParams& path_params = params.getPathParams();
        if (split == ProfileSplit::BY_DETAIL || path_params.getTwistBegin() != path_params.getTwist())
        {
            return LODInvariance::NONE;
        }

        // Unsplit, a square or a triangle has the same points at every
        // detail, and so does a hole of straight sides; a round hole has as
        // many sides as its detail asks for.
        const LLProfileParams& profile_params = params.getProfileParams();
        const U8 profile_curve = profile_params.getCurveType() & LL_PCODE_PROFILE_MASK;
        if (profile_curve != LL_PCODE_PROFILE_SQUARE && profile_curve != LL_PCODE_PROFILE_ISOTRI && profile_curve != LL_PCODE_PROFILE_EQUALTRI &&
            profile_curve != LL_PCODE_PROFILE_RIGHTTRI)
        {
            return LODInvariance::NONE;
        }
        if (profile_params.getHollow() == 0.f)
        {
            return LODInvariance::INVARIANT;
        }
        if ((profile_params.getCurveType() & LL_PCODE_HOLE_MASK) == LL_PCODE_HOLE_CIRCLE)
        {
            return LODInvariance::NONE;
        }
        return split == ProfileSplit::NONE ? LODInvariance::INVARIANT : LODInvariance::INVARIANT_UNLESS_HOLE_TOUCHES_WALL;
    }

    // Whether a prim is LOD-invariant, from what its parameters say and its
    // profile as LLVolume::generate first lays it out, without split points.
    bool keeps_lod_invariance(LODInvariance invariance, const LLProfile& profile)
    {
        return invariance == LODInvariance::INVARIANT || (invariance == LODInvariance::INVARIANT_UNLESS_HOLE_TOUCHES_WALL && !profile.holeTouchesWall());
    }
}

// static
bool LLVolume::isLODInvariant(const LLVolumeParams& params)
{
    const LODInvariance invariance = lod_invariance(params);
    if (invariance == LODInvariance::NONE)
    {
        return false;
    }

    // The profile as generate() first lays it out.
    thread_local LLProfile profile;
    profile.setDirty();
    return profile.generate(params.getProfileParams(), true) && keeps_lod_invariance(invariance, profile);
}

bool LLVolume::generate()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    llassert_always(mProfilep);

    //Added 10.03.05 Dave Parks
    // Split is a parameter to LLProfile::generate that tesselates edges on the profile
    // to prevent lighting and texture interpolation errors on triangles that are
    // stretched due to twisting or scaling on the path.
    S32 split = (S32) ((mDetail)*0.66f);

    const ProfileSplit split_rule = profile_split(mParams);
    const S32 lighting_split = split_rule == ProfileSplit::NONE ? 0 : split;
    if (split_rule != ProfileSplit::BY_DETAIL)
    {
        split = 0;
    }

    mLODScaleBias.setVec(0.5f, 0.5f, 0.5f);

    F32 profile_detail = mDetail;
    F32 path_detail = mDetail;

    if ((mParams.getSculptType() & LL_SCULPT_TYPE_MASK) != LL_SCULPT_TYPE_MESH)
    {
        U8 path_type = mParams.getPathParams().getCurveType();
        U8 profile_type = mParams.getProfileParams().getCurveType();
        if (path_type == LL_PCODE_PATH_LINE && profile_type == LL_PCODE_PROFILE_CIRCLE)
        {
            //cylinders don't care about Z-Axis
            mLODScaleBias.setVec(0.6f, 0.6f, 0.0f);
        }
        else if (path_type == LL_PCODE_PATH_CIRCLE)
        {
            mLODScaleBias.setVec(0.6f, 0.6f, 0.6f);
        }
    }

    bool regenPath = mPathp->generate(mParams.getPathParams(), path_detail, split);
    bool regenProf = mProfilep->generate(mParams.getProfileParams(), mPathp->isOpen(),profile_detail, split);
    if (regenProf)
    {
        mLODInvariant = !mUnique && !mGenerateSingleFace && keeps_lod_invariance(lod_invariance(mParams), *mProfilep);
    }
    if (regenProf && split < lighting_split && mProfilep->holeTouchesWall())
    {
        mProfilep->setDirty();
        mProfilep->generate(mParams.getProfileParams(), mPathp->isOpen(), profile_detail, lighting_split);
    }

    if (regenPath || regenProf )
    {
        S32 sizeS = mPathp->mPath.size();
        S32 sizeT = mProfilep->mProfile.size();

        mMesh.resize(sizeT * sizeS);

        //generate vertex positions

        // Run along the path.
        LLVector4a* dst = mMesh.mArray;

        for (S32 s = 0; s < sizeS; ++s)
        {
            F32* scale = mPathp->mPath[s].mScale.getF32ptr();

            F32 sc [] =
            { scale[0], 0, 0, 0,
                0, scale[1], 0, 0,
                0, 0, scale[2], 0,
                    0, 0, 0, 1 };

            // Was: LLMatrix4 rot((F32*) mPathp->mPath[s].mRot.mMatrix) -- reading
            // LLVector4a[4] through F32* is strict-aliasing UB. toMatrix4()
            // round-trips via SSE unaligned stores, which is well-defined.
            LLMatrix4 rot = mPathp->mPath[s].mRot.toMatrix4();
            LLMatrix4 scale_mat(sc);

            scale_mat *= rot;

            LLMatrix4a rot_mat;
            rot_mat.set(scale_mat);

            LLVector4a offset = mPathp->mPath[s].mPos;

            // hack to work around MAINT-5660 for debug until we can suss out
            // what is wrong with the path generated that inserts NaNs...
            if (!offset.isFinite3())
            {
                offset.clear();
            }

            // Run along the profile.
            alsimd::transform_directions_offset(rot_mat, offset, mProfilep->mProfile.mArray, dst, sizeT);
            dst += sizeT;
        }

        for (std::vector<LLProfile::Face>::iterator iter = mProfilep->mFaces.begin();
             iter != mProfilep->mFaces.end(); ++iter)
        {
            LLFaceID id = iter->mFaceID;
            mFaceMask |= id;
        }

        return true;
    }

    return false;
}

bool LLVolumeFace::VertexData::operator<(const LLVolumeFace::VertexData& rhs)const
{
    const F32* lp = this->getPosition().getF32ptr();
    const F32* rp = rhs.getPosition().getF32ptr();

    if (lp[0] != rp[0])
    {
        return lp[0] < rp[0];
    }

    if (rp[1] != lp[1])
    {
        return lp[1] < rp[1];
    }

    if (rp[2] != lp[2])
    {
        return lp[2] < rp[2];
    }

    lp = getNormal().getF32ptr();
    rp = rhs.getNormal().getF32ptr();

    if (lp[0] != rp[0])
    {
        return lp[0] < rp[0];
    }

    if (rp[1] != lp[1])
    {
        return lp[1] < rp[1];
    }

    if (rp[2] != lp[2])
    {
        return lp[2] < rp[2];
    }

    if (mTexCoord.mV[0] != rhs.mTexCoord.mV[0])
    {
        return mTexCoord.mV[0] < rhs.mTexCoord.mV[0];
    }

    return mTexCoord.mV[1] < rhs.mTexCoord.mV[1];
}

bool LLVolumeFace::VertexData::operator==(const LLVolumeFace::VertexData& rhs)const
{
    return mPosition.equals3(rhs.mPosition) &&
            mNormal.equals3(rhs.mNormal) &&
            mTexCoord == rhs.mTexCoord;
}

bool LLVolume::unpackVolumeFaces(std::istream& is, S32 size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    // Sanity-check before even trying to decompress
    constexpr S32 MAX_MESH_COMPRESSED_SIZE = 128 * 1024 * 1024; // 128 MB
    const LLUUID& mesh_id = getParams().getSculptID();

    if (size <= 0 || size > MAX_MESH_COMPRESSED_SIZE)
    {
        LL_WARNS("MeshStreaming") << "Rejecting implausible compressed mesh size " << size
            << " for mesh id " << mesh_id << LL_ENDL;
        return false;
    }

    //input stream is now pointing at a zlib compressed block of LLSD
    //decompress block
    try
    {
        LLSD mdl;
        U32 uzip_result = LLUZipHelper::unzip_llsd(mdl, is, size);
        if (uzip_result != LLUZipHelper::ZR_OK)
        {
            LL_DEBUGS("MeshStreaming") << "Failed to unzip LLSD blob for LoD with code " << uzip_result << " , will probably fetch from sim again." << LL_ENDL;
            return false;
        }
        return unpackVolumeFacesInternal(mdl);
    }
    catch (const std::bad_alloc&)
    {
        constexpr S32 SMALL_MESH_THRESHOLD = 4000;
        if (size < SMALL_MESH_THRESHOLD)
        {
            // showOutOfMemory and LL_ERRS must run on the main thread.
            // Post to mainloop WorkQueue, mirroring LLThread::tryRun().
            LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
            bool done = false;
            if (main_queue)
            {
                const LLUUID mesh_id_copy = mesh_id; // capture by value for the lambda
                done = main_queue->post([mesh_id_copy, size]()
                {
                    LLError::LLUserWarningMsg::showOutOfMemory();
                    LL_ERRS("MeshStreaming") << "Out of memory unpacking mesh id " << mesh_id_copy
                        << " of compressed size " << size << LL_ENDL;
                });
            }
            if (!done)
            {
                // No main queue available (e.g. during shutdown)
                LL_WARNS("MeshStreaming") << "Out of memory unpacking mesh id " << mesh_id
                    << " of compressed size " << size << " (main queue unavailable)" << LL_ENDL;
            }
        }
        else
        {
            LL_WARNS("MeshStreaming") << "Out of memory unpacking mesh id " << mesh_id
                << " of compressed size " << size << LL_ENDL;
        }
        return false;
    }
    catch (const std::exception& e)
    {
        LL_WARNS("MeshStreaming") << "Exception unpacking mesh id " << mesh_id
            << " of compressed size " << size << ": " << e.what() << LL_ENDL;
        return false;
    }
    catch (...)
    {
        LL_WARNS("MeshStreaming") << "Unknown exception unpacking mesh id " << mesh_id
            << " of compressed size " << size << LL_ENDL;
        return false;
    }
}

bool LLVolume::unpackVolumeFaces(U8* in_data, S32 size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;
    constexpr S32 MAX_MESH_COMPRESSED_SIZE = 128 * 1024 * 1024; // 128 MB
    const LLUUID& mesh_id = getParams().getSculptID();

    if (!in_data || size <= 0 || size > MAX_MESH_COMPRESSED_SIZE)
    {
        LL_WARNS("MeshStreaming") << "Rejecting implausible compressed mesh size " << size
            << " for mesh id " << mesh_id << LL_ENDL;
        return false;
    }

    //input data is now pointing at a zlib compressed block of LLSD
    //decompress block
    try
    {
        LLSD mdl;
        U32 uzip_result = LLUZipHelper::unzip_llsd(mdl, in_data, size);
        if (uzip_result != LLUZipHelper::ZR_OK)
        {
            LL_DEBUGS("MeshStreaming") << "Failed to unzip LLSD blob for LoD, mesh id " << mesh_id
                << ", code " << uzip_result << " , will probably fetch from sim again." << LL_ENDL;
            return false;
        }
        return unpackVolumeFacesInternal(mdl);
    }
    catch (const std::bad_alloc&)
    {
        constexpr S32 SMALL_MESH_THRESHOLD = 4000;
        if (size < SMALL_MESH_THRESHOLD)
        {
            // showOutOfMemory and LL_ERRS must run on the main thread.
            // Post to mainloop WorkQueue, mirroring LLThread::tryRun().
            LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
            bool done = false;
            if (main_queue)
            {
                const LLUUID mesh_id_copy = mesh_id; // capture by value for the lambda
                done = main_queue->post([mesh_id_copy, size]()
                {
                    LLError::LLUserWarningMsg::showOutOfMemory();
                    LL_ERRS("MeshStreaming") << "Out of memory unpacking mesh id " << mesh_id_copy
                        << " of compressed size " << size << LL_ENDL;
                });
            }
            if (!done)
            {
                // No main queue available (e.g. during shutdown)
                LL_WARNS("MeshStreaming") << "Out of memory unpacking mesh id " << mesh_id
                    << " of compressed size " << size << " (main queue unavailable)" << LL_ENDL;
            }
        }
        else
        {
            LL_WARNS("MeshStreaming") << "Out of memory unpacking mesh id " << mesh_id
                << " of compressed size " << size << LL_ENDL;
        }
        return false;
    }
    catch (const std::exception& e)
    {
        LL_WARNS("MeshStreaming") << "Exception unpacking mesh id " << mesh_id
            << " of compressed size " << size << ": " << e.what() << LL_ENDL;
        return false;
    }
    catch (...)
    {
        LL_WARNS("MeshStreaming") << "Unknown exception unpacking mesh id " << mesh_id
            << " of compressed size " << size << LL_ENDL;
        return false;
    }
}

bool LLVolume::unpackVolumeFacesInternal(const LLSD& mdl)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;
    if (!decodeVolumeFaces(mdl))
    {
        return false;
    }

    if (!cacheOptimize(true))
    {
        // Out of memory?
        LL_WARNS() << "Failed to optimize!" << LL_ENDL;
        mVolumeFaces.clear();
        return false;
    }

    mSculptLevel = 0;  // success!

    return true;
}

bool LLVolume::decodeVolumeFaces(const LLSD& mdl)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;
    {
        auto face_count = mdl.size();

        if (face_count == 0)
        { //no faces unpacked, treat as failed decode
            LL_WARNS() << "found no faces!" << LL_ENDL;
            return false;
        }

        // A face of no indices costs nothing against the index budget, but
        // is made all the same.
        if (face_count > (size_t)LL_SCULPT_MESH_MAX_FACES)
        {
            LL_WARNS() << "Invalid face count " << face_count << " exceeds maximum" << LL_ENDL;
            mVolumeFaces.clear();
            return false;
        }

        // The LOD's indices between its faces, summed before any face is
        // made.
        size_t lod_indices = 0;
        for (size_t i = 0; i < face_count; ++i)
        {
            if (!mdl[i].has("NoGeometry"))
            {
                lod_indices += mdl[i]["TriangleList"].asBinary().size() / sizeof(U16);
            }
        }
        if (lod_indices > (size_t)LL_SCULPT_MESH_MAX_LOD_INDICES)
        {
            LL_WARNS() << "Invalid index count " << lod_indices << " exceeds maximum for a LOD of " << face_count << " faces" << LL_ENDL;
            mVolumeFaces.clear();
            return false;
        }

        mVolumeFaces.resize(face_count);

        for (size_t i = 0; i < face_count; ++i)
        {
            LLVolumeFace& face = mVolumeFaces[i];

            if (mdl[i].has("NoGeometry"))
            { //face has no geometry, continue
                face.resizeIndices(3);
                face.resizeVertices(1);
                face.mPositions->clear();
                face.mNormals->clear();
                face.mTexCoords->setZero();
                memset(face.mIndices, 0, sizeof(U16)*3);
                continue;
            }

            const LLSD::Binary& pos = mdl[i]["Position"].asBinary();
            const LLSD::Binary& norm = mdl[i]["Normal"].asBinary();
#if 0 // keep this code for now in case we decide to add support for on-the-wire tangents
            const LLSD::Binary& tangent = mdl[i]["Tangent"].asBinary();
#endif
            const LLSD::Binary& tc = mdl[i]["TexCoord0"].asBinary();
            const LLSD::Binary& idx = mdl[i]["TriangleList"].asBinary();

            //copy out indices
            auto num_indices = idx.size() / 2;
            const S32 indices_to_discard = num_indices % 3;
            if (indices_to_discard > 0)
            {
                // Invalid number of triangle indices
                LL_WARNS() << "Incomplete triangle discarded from face! Indices count " << num_indices << " was not divisible by 3. face index: " << i << " Total: " << face_count << LL_ENDL;
                num_indices -= indices_to_discard;
            }

            if (num_indices > (size_t)LL_SCULPT_MESH_MAX_FACE_INDICES)
            {
                LL_WARNS() << "Invalid index count " << num_indices << " exceeds maximum for face index: " << i << LL_ENDL;
                mVolumeFaces.clear();
                return false;
            }

            face.resizeIndices(static_cast<S32>(num_indices));

            if (num_indices > 2 && !face.mIndices)
            {
                LL_WARNS() << "Failed to allocate " << num_indices << " indices for face index: " << i << " Total: " << face_count << LL_ENDL;
                continue;
            }

            if (idx.empty() || face.mNumIndices < 3)
            { //why is there an empty index list?
                LL_WARNS() << "Empty face present! Face index: " << i << " Total: " << face_count << LL_ENDL;
                continue;
            }

            // idx is std::vector<U8>; the on-wire indices are pairs of bytes
            // representing little-endian U16 values. Casting U8* -> U16* and
            // dereferencing is strict-aliasing UB (you may not access a
            // char-typed object through a non-char pointer). std::memcpy is
            // defined and compiles down to a load on every supported target.
            {
                const U8* indices_bytes = idx.data();
                LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("unpackVolumeFaces - indices");
                for (U32 j = 0; j < num_indices; ++j)
                {
                    U16 idx_v;
                    std::memcpy(&idx_v, indices_bytes + j * sizeof(U16), sizeof(U16));
                    face.mIndices[j] = idx_v;
                }
            }

            //copy out vertices
            U32 num_verts = static_cast<U32>(pos.size())/(3*2);
            if (num_verts == 0)
            {
                LL_WARNS() << "Zero vertices for face index: " << i << LL_ENDL;
                face.resizeIndices(3);
                face.resizeVertices(1);
                face.mPositions->clear();
                face.mNormals->clear();
                face.mTexCoords->setZero();
                memset(face.mIndices, 0, sizeof(U16) * 3);
                continue;
            }

            if (num_verts > 65535) // U16 indices
            {
                LL_WARNS() << "Invalid vertex count " << num_verts << " exceeds maximum for face index: " << i << LL_ENDL;
                mVolumeFaces.clear();
                return false;
            }

            face.resizeVertices(num_verts);

            if (num_verts > 0 && !face.mPositions)
            {
                LL_WARNS() << "Failed to allocate " << num_verts << " vertices for face index: " << i << " Total: " << face_count << LL_ENDL;
                face.resizeVertices(0);
                face.resizeIndices(0);
                continue;
            }

            LLVector3 minp;
            LLVector3 maxp;
            LLVector2 min_tc;
            LLVector2 max_tc;

            minp.setValue(mdl[i]["PositionDomain"]["Min"]);
            maxp.setValue(mdl[i]["PositionDomain"]["Max"]);
            LLVector4a min_pos, max_pos;
            min_pos.load3(minp.mV);
            max_pos.load3(maxp.mV);

            min_tc.setValue(mdl[i]["TexCoord0Domain"]["Min"]);
            max_tc.setValue(mdl[i]["TexCoord0Domain"]["Max"]);

            //unpack normalized scale/translation
            if (mdl[i].has("NormalizedScale"))
            {
                face.mNormalizedScale.setValue(mdl[i]["NormalizedScale"]);
            }
            else
            {
                face.mNormalizedScale.set(1, 1, 1);
            }

            LLVector4a pos_range;
            pos_range.setSub(max_pos, min_pos);
            LLVector2 tc_range2 = max_tc - min_tc;

            LLVector4a tc_range;
            tc_range.set(tc_range2[0], tc_range2[1], tc_range2[0], tc_range2[1]);
            LLVector4a min_tc4(min_tc[0], min_tc[1], min_tc[0], min_tc[1]);

            // mTexCoords is LLVector2* into a 16-byte-aligned slab holding
            // two texture coordinates per vector
            F32* tc_out = (F32*) face.mTexCoords;

            // each quantized value is its sixteen bits over 65535, times
            // the range, plus the minimum; the range over 65535 is one
            // multiply per lane
            const LLVector4a u16_step(1.f / 65535.f);

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("unpackVolumeFaces - positions");
                LLVector4a pos_scale;
                pos_scale.setMul(pos_range, u16_step);
                alsimd::dequantize_u16x3(pos.data(), num_verts, pos_scale, min_pos, face.mPositions);
            }

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("unpackVolumeFaces - normals");
                // An array short of the positions' count is taken as none.
                if (norm.size() >= (size_t)num_verts * 3 * sizeof(U16))
                {
                    // from sixteen bits to -1..1
                    alsimd::dequantize_u16x3(norm.data(), num_verts, LLVector4a(2.f / 65535.f), LLVector4a(-1.f), face.mNormals);
                }
                else
                {
                    LLVector4a* norm_out = face.mNormals;
                    for (U32 j = 0; j < num_verts; ++j)
                    {
                        norm_out->clear();
                        norm_out++;
                    }
                }
            }

#if 0 // keep this code for now in case we decide to add support for on-the-wire tangents
            {
                if (!tangent.empty())
                {
                    face.allocateTangents(face.mNumVertices);
                    const U8* t_bytes = tangent.data();

                    // NOTE: tangents coming from the asset may not be mikkt space, but they should always be used by the GLTF shaders to
                    // maintain compliance with the GLTF spec
                    LLVector4a* t_out = face.mTangents;

                    for (U32 j = 0; j < num_verts; ++j)
                    {
                        U16 t[4];
                        std::memcpy(t, t_bytes + j * sizeof(t), sizeof(t));
                        t_out->set((F32)t[0], (F32)t[1], (F32)t[2], (F32) t[3]);
                        t_out->div(65535.f);
                        t_out->mul(2.f);
                        t_out->sub(1.f);

                        F32* tp = t_out->getF32ptr();
                        tp[3] = tp[3] < 0.f ? -1.f : 1.f;

                        t_out++;
                    }
                }
            }
#endif

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("unpackVolumeFaces - texcoords");
                if (tc.size() >= (size_t)num_verts * 2 * sizeof(U16))
                {
                    LLVector4a tc_scale;
                    tc_scale.setMul(tc_range, u16_step);
                    alsimd::dequantize_u16x2(tc.data(), num_verts, tc_scale, min_tc4, tc_out);
                }
                else
                {
                    LLVector4a zero;
                    zero.clear();
                    for (U32 j = 0; j < num_verts; j += 2)
                    {
                        zero.store4a(tc_out);
                        tc_out += 4;
                    }
                }
            }

            if (mdl[i].has("Weights"))
            {
                face.allocateWeights(num_verts);
                if (!face.mWeights && num_verts)
                {
                    LL_WARNS() << "Failed to allocate " << num_verts << " weights for face index: " << i << " Total: " << face_count << LL_ENDL;
                    face.resizeIndices(0);
                    face.resizeVertices(0);
                    continue;
                }

                const LLSD::Binary& weights = mdl[i]["Weights"].asBinary();
                LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("unpackVolumeFaces - weights");

                U32 idx = 0;

                U32 cur_vertex = 0;
                while (idx < weights.size() && cur_vertex < num_verts)
                {
                    const U8 END_INFLUENCES = 0xFF;
                    U8 joint = weights[idx++];

                    U32 cur_influence = 0;
                    LLVector4 wght(0,0,0,0);
                    U32 joints[4] = {0,0,0,0};
                    LLVector4 joints_with_weights(0,0,0,0);

                    while (joint != END_INFLUENCES)
                    {
                        // An influence is its joint and two bytes of weight;
                        // an array that ends inside one ends there, the
                        // vertex keeping the influences before it.
                        if (weights.size() - idx < 2)
                        {
                            idx = (U32)weights.size();
                            break;
                        }
                        U16 influence = weights[idx++];
                        influence |= ((U16) weights[idx++] << 8);

                        F32 w = llclamp((F32) influence / 65535.f, 0.001f, 0.999f);
                        wght.mV[cur_influence] = w;
                        joints[cur_influence] = joint;
                        cur_influence++;

                        if (cur_influence >= 4 || idx >= weights.size())
                        {
                            joint = END_INFLUENCES;
                        }
                        else
                        {
                            joint = weights[idx++];
                        }
                    }
                    F32 wsum = wght.mV[VX] + wght.mV[VY] + wght.mV[VZ] + wght.mV[VW];
                    if (wsum <= 0.f)
                    {
                        wght = LLVector4(0.999f,0.f,0.f,0.f);
                    }
                    for (U32 k=0; k<4; k++)
                    {
                        F32 f_combined = (F32) joints[k] + wght[k];
                        joints_with_weights[k] = f_combined;
                        // Any weights we added above should wind up non-zero and applied to a specific bone.
                        // A failure here would indicate a floating point precision error in the math.
                        llassert((k >= cur_influence) || (f_combined - S32(f_combined) > 0.0f));
                    }
                    face.mWeights[cur_vertex].loadua(joints_with_weights.mV);

                    cur_vertex++;
                }

                if (cur_vertex != num_verts || idx != weights.size())
                {
                    LL_WARNS() << "Vertex weight count does not match vertex count!" << LL_ENDL;
                }

                // A vertex the weights run out before takes the weight of a
                // vertex with no influence, rather than what the allocation
                // held.
                for (; cur_vertex < num_verts; ++cur_vertex)
                {
                    face.mWeights[cur_vertex].set(0.999f, 0.f, 0.f, 0.f);
                }

            }

            // modifier flags?
            bool do_mirror = (mParams.getSculptType() & LL_SCULPT_FLAG_MIRROR);
            bool do_invert = (mParams.getSculptType() &LL_SCULPT_FLAG_INVERT);


            // translate to actions:
            bool do_reflect_x = false;
            bool do_reverse_triangles = false;
            bool do_invert_normals = false;

            if (do_mirror)
            {
                do_reflect_x = true;
                do_reverse_triangles = !do_reverse_triangles;
            }

            if (do_invert)
            {
                do_invert_normals = true;
                do_reverse_triangles = !do_reverse_triangles;
            }

            // now do the work

            if (do_reflect_x)
            {
                LLVector4a* p = (LLVector4a*) face.mPositions;
                LLVector4a* n = (LLVector4a*) face.mNormals;

                for (S32 i = 0; i < face.mNumVertices; i++)
                {
                    p[i].mul(-1.0f);
                    n[i].mul(-1.0f);
                }
            }

            if (do_invert_normals)
            {
                LLVector4a* n = (LLVector4a*) face.mNormals;

                for (S32 i = 0; i < face.mNumVertices; i++)
                {
                    n[i].mul(-1.0f);
                }
            }

            if (do_reverse_triangles)
            {
                for (S32 j = 0; j < face.mNumIndices; j += 3)
                {
                    // swap the 2nd and 3rd index
                    S32 swap = face.mIndices[j+1];
                    face.mIndices[j+1] = face.mIndices[j+2];
                    face.mIndices[j+2] = swap;
                }
            }

            //calculate bounding box
            // VFExtents change
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("unpackVolumeFaces - extents");
                LLVector4a& min = face.mExtents[0];
                LLVector4a& max = face.mExtents[1];

                if (face.mNumVertices < 3)
                { //empty face, use a dummy 1cm (at 1m scale) bounding box
                    min.splat(-0.005f);
                    max.splat(0.005f);
                }
                else
                {
                    alsimd::extents(face.mPositions, face.mNumVertices, min, max);

                    if (face.mTexCoords)
                    {
                        LLVector2& min_tc = face.mTexCoordExtents[0];
                        LLVector2& max_tc = face.mTexCoordExtents[1];

                        min_tc = face.mTexCoords[0];
                        max_tc = face.mTexCoords[0];

                        for (S32 j = 1; j < face.mNumVertices; ++j)
                        {
                            update_min_max(min_tc, max_tc, face.mTexCoords[j]);
                        }
                    }
                    else
                    {
                        face.mTexCoordExtents[0].set(0,0);
                        face.mTexCoordExtents[1].set(1,1);
                    }
                }
            }
        }
    }

    return true;
}


bool LLVolume::isMeshAssetLoaded() const
{
    return mIsMeshAssetLoaded;
}

void LLVolume::setMeshAssetLoaded(bool loaded)
{
    mIsMeshAssetLoaded = loaded;
    if (loaded)
    {
        mIsMeshAssetUnavaliable = false;
    }
}

void LLVolume::setMeshAssetUnavaliable(bool unavaliable)
{
    // Don't set it if at least one lod loaded
    if (!mIsMeshAssetLoaded)
    {
        mIsMeshAssetUnavaliable = unavaliable;
    }
}

bool LLVolume::isMeshAssetUnavaliable() const
{
    return mIsMeshAssetUnavaliable;
}

void LLVolume::copyFacesTo(std::vector<LLVolumeFace> &faces) const
{
    faces = mVolumeFaces;
}

void LLVolume::copyFacesFrom(const std::vector<LLVolumeFace> &faces)
{
    mVolumeFaces = faces;
    mSculptLevel = 0;
}

void LLVolume::copyVolumeFaces(const LLVolume* volume)
{
    mVolumeFaces = volume->mVolumeFaces;
    mSculptLevel = 0;
}

bool LLVolume::cacheOptimize(bool gen_tangents)
{
    for (S32 i = 0; i < mVolumeFaces.size(); ++i)
    {
        if (!mVolumeFaces[i].cacheOptimize(gen_tangents))
        {
            return false;
        }
    }
    return true;
}


S32 LLVolume::getNumFaces() const
{
    return mIsMeshAssetLoaded ? getNumVolumeFaces() : (S32)mProfilep->mFaces.size();
}


void LLVolume::createVolumeFaces()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if (mGenerateSingleFace)
    {
        // do nothing
    }
    else
    {
        // A partial build rewrites each face's vertices in the buffers it
        // has and keeps its indices, so it is made only where every face is
        // laid out as the last build laid it out: the same type, profile
        // range and path length, and so the same counts. Anything else is a
        // full build of every face.
        S32 num_faces = getNumFaces();
        bool partial_build = num_faces == (S32)mVolumeFaces.size();
        mVolumeFaces.resize(num_faces);

        // Initialize volume faces with parameter data
        for (S32 i = 0; i < (S32)mVolumeFaces.size(); i++)
        {
            LLVolumeFace& vf = mVolumeFaces[i];
            LLProfile::Face& face = mProfilep->mFaces[i];
            S32 num_s = face.mCount;
            if (num_s < 0)
            {
                LL_ERRS() << "Volume face corruption detected." << LL_ENDL;
            }

            // Set the type mask bits correctly
            U32 type_mask = 0;
            if (mParams.getProfileParams().getHollow() > 0)
            {
                type_mask |= LLVolumeFace::HOLLOW_MASK;
            }
            if (mProfilep->isOpen())
            {
                type_mask |= LLVolumeFace::OPEN_MASK;
            }
            if (face.mCap)
            {
                type_mask |= LLVolumeFace::CAP_MASK;
                if (face.mFaceID == LL_FACE_PATH_BEGIN)
                {
                    type_mask |= LLVolumeFace::TOP_MASK;
                }
                else
                {
                    llassert(face.mFaceID == LL_FACE_PATH_END);
                    type_mask |= LLVolumeFace::BOTTOM_MASK;
                }
            }
            else if (face.mFaceID & (LL_FACE_PROFILE_BEGIN | LL_FACE_PROFILE_END))
            {
                type_mask |= LLVolumeFace::FLAT_MASK | LLVolumeFace::END_MASK;
            }
            else
            {
                type_mask |= LLVolumeFace::SIDE_MASK;
                if (face.mFlat)
                {
                    type_mask |= LLVolumeFace::FLAT_MASK;
                }
                if (face.mFaceID & LL_FACE_INNER_SIDE)
                {
                    type_mask |= LLVolumeFace::INNER_MASK;
                    if (face.mFlat && num_s > 2)
                    {
                        // A flat inner side has a column for each of its
                        // points and a second for each corner, where its
                        // normals turn.
                        for (S32 point = 1; point + 1 < face.mCount; ++point)
                        {
                            if (face.isCorner(point))
                            {
                                ++num_s;
                            }
                        }
                        if (num_s < 0)
                        {
                            LL_ERRS() << "Volume face corruption detected." << LL_ENDL;
                        }
                    }
                }
                else
                {
                    type_mask |= LLVolumeFace::OUTER_MASK;
                }
            }

            const S32 num_t = getPath().mPath.size();
            if (vf.mTypeMask != type_mask || vf.mBeginS != face.mIndex || vf.mNumS != num_s || vf.mNumT != num_t)
            {
                partial_build = false;
            }

            vf.mTypeMask = type_mask;
            vf.mBeginS = face.mIndex;
            vf.mNumS = num_s;
            vf.mBeginT = 0;
            vf.mNumT = num_t;
            vf.mID = i;
        }

        for (face_list_t::iterator iter = mVolumeFaces.begin();
             iter != mVolumeFaces.end(); ++iter)
        {
            (*iter).create(this, partial_build);
        }
    }
}


inline LLVector4a sculpt_rgb_to_vector(U8 r, U8 g, U8 b)
{
    // maps RGB values to vector values [0..255] -> [-0.5..0.5]
    LLVector4a value;
    LLVector4a sub(0.5f, 0.5f, 0.5f);

    value.set(r,g,b);
    value.mul(1.f/255.f);
    value.sub(sub);

    return value;
}

inline U32 sculpt_xy_to_index(U32 x, U32 y, U16 sculpt_width, U16 sculpt_height, S8 sculpt_components)
{
    U32 index = (x + y * sculpt_width) * sculpt_components;
    return index;
}


inline U32 sculpt_st_to_index(S32 s, S32 t, S32 size_s, S32 size_t, U16 sculpt_width, U16 sculpt_height, S8 sculpt_components)
{
    U32 x = (U32) ((F32)s/(size_s) * (F32) sculpt_width);
    U32 y = (U32) ((F32)t/(size_t) * (F32) sculpt_height);

    return sculpt_xy_to_index(x, y, sculpt_width, sculpt_height, sculpt_components);
}


inline LLVector4a sculpt_index_to_vector(U32 index, const U8* sculpt_data)
{
    LLVector4a v = sculpt_rgb_to_vector(sculpt_data[index], sculpt_data[index+1], sculpt_data[index+2]);

    return v;
}

inline LLVector4a sculpt_st_to_vector(S32 s, S32 t, S32 size_s, S32 size_t, U16 sculpt_width, U16 sculpt_height, S8 sculpt_components, const U8* sculpt_data)
{
    U32 index = sculpt_st_to_index(s, t, size_s, size_t, sculpt_width, sculpt_height, sculpt_components);

    return sculpt_index_to_vector(index, sculpt_data);
}

inline LLVector4a sculpt_xy_to_vector(U32 x, U32 y, U16 sculpt_width, U16 sculpt_height, S8 sculpt_components, const U8* sculpt_data)
{
    U32 index = sculpt_xy_to_index(x, y, sculpt_width, sculpt_height, sculpt_components);

    return sculpt_index_to_vector(index, sculpt_data);
}


F32 LLVolume::sculptGetSurfaceArea()
{
    // test to see if image has enough variation to create non-degenerate geometry

    F32 area = 0;

    S32 sizeS = mPathp->mPath.size();
    S32 sizeT = mProfilep->mProfile.size();

    for (S32 s = 0; s < sizeS-1; s++)
    {
        for (S32 t = 0; t < sizeT-1; t++)
        {
            // get four corners of quad
            LLVector4a& p1 = mMesh[(s  )*sizeT + (t  )];
            LLVector4a& p2 = mMesh[(s+1)*sizeT + (t  )];
            LLVector4a& p3 = mMesh[(s  )*sizeT + (t+1)];
            LLVector4a& p4 = mMesh[(s+1)*sizeT + (t+1)];

            // compute the area of the quad by taking the length of the cross product of the two triangles
            LLVector4a v0,v1,v2,v3;
            v0.setSub(p1,p2);
            v1.setSub(p1,p3);
            v2.setSub(p4,p2);
            v3.setSub(p4,p3);

            LLVector4a cross1, cross2;
            cross1.setCross3(v0,v1);
            cross2.setCross3(v2,v3);

            //LLVector3 cross1 = (p1 - p2) % (p1 - p3);
            //LLVector3 cross2 = (p4 - p2) % (p4 - p3);

            area += (cross1.getLength3() + cross2.getLength3()).getF32() / 2.f;
        }
    }

    return area;
}

// create empty placeholder shape
void LLVolume::sculptGenerateEmptyPlaceholder()
{
    S32 sizeS = mPathp->mPath.size();
    S32 sizeT = mProfilep->mProfile.size();

    S32 line = 0;

    for (S32 s = 0; s < sizeS; s++)
    {
        for (S32 t = 0; t < sizeT; t++)
        {
            S32 i = t + line;
            LLVector4a& pt = mMesh[i];

            F32* p = pt.getF32ptr();

            p[0] = 0;
            p[1] = 0;
            p[2] = 0;
            p[3] = 0;

            llassert(pt.isFinite3());
        }
        line += sizeT;
    }
}

// create sphere placeholder shape, laid out as a sphere sculpt's map is
// sampled: the profile runs around each row, and the path from pole to pole,
// so that the sphere stitching's pole and seam normals land on it
void LLVolume::sculptGenerateSpherePlaceholder()
{
    S32 sizeS = mPathp->mPath.size();
    S32 sizeT = mProfilep->mProfile.size();

    S32 line = 0;

    for (S32 s = 0; s < sizeS; s++)
    {
        for (S32 t = 0; t < sizeT; t++)
        {
            S32 i = t + line;
            LLVector4a& pt = mMesh[i];


            F32 u = (F32)t / (sizeT - 1);
            F32 v = (F32)s / (sizeS - 1);

            const F32 RADIUS = (F32) 0.3;

            F32* p = pt.getF32ptr();

            p[0] = (F32)(sin(F_PI * v) * cos(2.0 * F_PI * u) * RADIUS);
            p[1] = (F32)(-sin(F_PI * v) * sin(2.0 * F_PI * u) * RADIUS);
            p[2] = (F32)(cos(F_PI * v) * RADIUS);
            // mMesh's resize leaves what it adds as it found it.
            p[3] = 0;

            llassert(pt.isFinite3());
        }
        line += sizeT;
    }
}

// create the vertices from the map
void LLVolume::sculptGenerateMapVertices(U16 sculpt_width, U16 sculpt_height, S8 sculpt_components, const U8* sculpt_data, U8 sculpt_type)
{
    U8 sculpt_stitching = sculpt_type & LL_SCULPT_TYPE_MASK;
    bool sculpt_invert = sculpt_type & LL_SCULPT_FLAG_INVERT;
    bool sculpt_mirror = sculpt_type & LL_SCULPT_FLAG_MIRROR;
    bool reverse_horizontal = (sculpt_invert ? !sculpt_mirror : sculpt_mirror);  // XOR

    S32 sizeS = mPathp->mPath.size();
    S32 sizeT = mProfilep->mProfile.size();

    S32 line = 0;
    for (S32 s = 0; s < sizeS; s++)
    {
        // Run along the profile.
        for (S32 t = 0; t < sizeT; t++)
        {
            S32 i = t + line;
            LLVector4a& pt = mMesh[i];

            S32 reversed_t = t;

            if (reverse_horizontal)
            {
                reversed_t = sizeT - t - 1;
            }

            U32 x = (U32) ((F32)reversed_t/(sizeT-1) * (F32) sculpt_width);
            U32 y = (U32) ((F32)s/(sizeS-1) * (F32) sculpt_height);


            if (y == 0)  // top row stitching
            {
                // pinch?
                if (sculpt_stitching == LL_SCULPT_TYPE_SPHERE)
                {
                    x = sculpt_width / 2;
                }
            }

            if (y == sculpt_height)  // bottom row stitching
            {
                // wrap?
                if (sculpt_stitching == LL_SCULPT_TYPE_TORUS)
                {
                    y = 0;
                }
                else
                {
                    y = sculpt_height - 1;
                }

                // pinch?
                if (sculpt_stitching == LL_SCULPT_TYPE_SPHERE)
                {
                    x = sculpt_width / 2;
                }
            }

            if (x == sculpt_width)   // side stitching
            {
                // wrap?
                if ((sculpt_stitching == LL_SCULPT_TYPE_SPHERE) ||
                    (sculpt_stitching == LL_SCULPT_TYPE_TORUS) ||
                    (sculpt_stitching == LL_SCULPT_TYPE_CYLINDER))
                {
                    x = 0;
                }

                else
                {
                    x = sculpt_width - 1;
                }
            }

            pt = sculpt_xy_to_vector(x, y, sculpt_width, sculpt_height, sculpt_components, sculpt_data);

            if (sculpt_mirror)
            {
                LLVector4a scale(-1.f,1,1,1);
                pt.mul(scale);
            }

            llassert(pt.isFinite3());
        }

        line += sizeT;
    }
}


constexpr S32 SCULPT_REZ_1 = 6;  // changed from 4 to 6 - 6 looks round whereas 4 looks square
constexpr S32 SCULPT_REZ_2 = 8;
constexpr S32 SCULPT_REZ_3 = 16;
constexpr S32 SCULPT_REZ_4 = 32;

S32 sculpt_sides(F32 detail)
{
    // detail is usually one of: 1, 1.5, 2.5, 4.0.

    if (detail <= 1.0)
    {
        return SCULPT_REZ_1;
    }
    if (detail <= 2.0)
    {
        return SCULPT_REZ_2;
    }
    if (detail <= 3.0)
    {
        return SCULPT_REZ_3;
    }
    else
    {
        return SCULPT_REZ_4;
    }
}



// determine the number of vertices in both s and t direction for this sculpt
void sculpt_calc_mesh_resolution(U16 width, U16 height, U8 type, F32 detail, S32& s, S32& t)
{
    // this code has the following properties:
    // 1) the aspect ratio of the mesh is as close as possible to the ratio of the map
    //    while still using all available verts
    // 2) the mesh cannot have more verts than is allowed by LOD
    // 3) the mesh cannot have more verts than is allowed by the map

    S32 max_vertices_lod = (S32)pow((double)sculpt_sides(detail), 2.0);
    S32 max_vertices_map = width * height / 4;

    S32 vertices;
    if (max_vertices_map > 0)
        vertices = llmin(max_vertices_lod, max_vertices_map);
    else
        vertices = max_vertices_lod;


    F32 ratio;
    if ((width == 0) || (height == 0))
        ratio = 1.f;
    else
        ratio = (F32) width / (F32) height;


    s = (S32)(F32) sqrt(((F32)vertices / ratio));

    s = llmax(s, 4);              // no degenerate sizes, please
    t = vertices / s;

    t = llmax(t, 4);              // no degenerate sizes, please
    s = vertices / t;
}

// A sculpt map averaged down by two along x, y or both: each pixel the rounded mean of the two or four it covers.
static std::vector<U8> sculpt_map_averaged_down(const U8* data, U16 width, U16 height, S8 components, bool along_x, bool along_y)
{
    const S32 step_x = along_x ? 2 : 1;
    const S32 step_y = along_y ? 2 : 1;
    const S32 out_width = width / step_x;
    const S32 out_height = height / step_y;
    std::vector<U8> out((size_t)out_width * out_height * components);
    for (S32 y = 0; y < out_height; ++y)
    {
        for (S32 x = 0; x < out_width; ++x)
        {
            for (S32 c = 0; c < components; ++c)
            {
                S32 sum = 0;
                for (S32 dy = 0; dy < step_y; ++dy)
                {
                    for (S32 dx = 0; dx < step_x; ++dx)
                    {
                        sum += data[((size_t)(y * step_y + dy) * width + (x * step_x + dx)) * components + c];
                    }
                }
                const S32 count = step_x * step_y;
                out[((size_t)y * out_width + x) * components + c] = (U8)((sum + count / 2) / count);
            }
        }
    }
    return out;
}

// sculpt replaces generate() for sculpted surfaces
void LLVolume::sculpt(U16 sculpt_width, U16 sculpt_height, S8 sculpt_components, const U8* sculpt_data, S32 sculpt_level, bool visible_placeholder)
{
    U8 sculpt_type = mParams.getSculptType();

    bool data_is_empty = false;

    if (sculpt_width == 0 || sculpt_height == 0 || sculpt_components < 3 || sculpt_data == NULL)
    {
        sculpt_level = -1;
        data_is_empty = true;
    }
    mSculptPixels = data_is_empty ? 0 : S32(sculpt_width) * S32(sculpt_height);

    // A map sampled more than two pixels apart along an axis can hold its surface between the samples: an oblong map
    // that keeps each vertex on a pair of lines, with a line collapsed to a point every few pairs, gives a coarser
    // detail nothing but the collapsed lines, and no area. Where the samples have no area, the map averaged down by
    // two along each such axis is sampled instead, as the level below it would be, until one has or no axis is left
    // to average; a map whose samples have their area builds as it always has.
    U16 width = sculpt_width;
    U16 height = sculpt_height;
    const U8* data = sculpt_data;
    std::vector<U8> averaged;
    while (true)
    {
        S32 requested_sizeS = 0;
        S32 requested_sizeT = 0;

        sculpt_calc_mesh_resolution(width, height, sculpt_type, mDetail, requested_sizeS, requested_sizeT);

        mPathp->generate(mParams.getPathParams(), mDetail, 0, true, requested_sizeS);
        mProfilep->generate(mParams.getProfileParams(), mPathp->isOpen(), mDetail, 0, true, requested_sizeT);

        S32 sizeS = mPathp->mPath.size();         // we requested a specific size, now see what we really got
        S32 sizeT = mProfilep->mProfile.size();   // we requested a specific size, now see what we really got

        // weird crash bug - DEV-11158 - trying to collect more data:
        if ((sizeS == 0) || (sizeT == 0))
        {
            LL_WARNS() << "sculpt bad mesh size " << sizeS << " " << sizeT << LL_ENDL;
        }

        mMesh.resize(sizeS * sizeT);

        //generate vertex positions
        if (data_is_empty)
        {
            break;
        }

        sculptGenerateMapVertices(width, height, sculpt_components, data, sculpt_type);

        // don't test lowest LOD to support legacy content DEV-33670
        if (mDetail <= SCULPT_MIN_AREA_DETAIL)
        {
            break;
        }

        F32 area = sculptGetSurfaceArea();

        mSurfaceArea = area;

        const F32 SCULPT_MAX_AREA = 384.f;

        if (area >= SCULPT_MIN_AREA && area <= SCULPT_MAX_AREA)
        {
            break;
        }

        // The samples run along x by the profile and along y by the path. Only a surface with too little area is
        // averaged: one with too much, a map of noise, builds the sphere as it always has.
        const bool along_x = area < SCULPT_MIN_AREA && sizeT > 1 && width >= 2 && width > 2 * (sizeT - 1);
        const bool along_y = area < SCULPT_MIN_AREA && sizeS > 1 && height >= 2 && height > 2 * (sizeS - 1);
        if (!along_x && !along_y)
        {
            data_is_empty = true;
            visible_placeholder = true;
            break;
        }
        averaged = sculpt_map_averaged_down(data, width, height, sculpt_components, along_x, along_y);
        data = averaged.data();
        width = along_x ? width / 2 : width;
        height = along_y ? height / 2 : height;
    }

    if (data_is_empty)
    {
        if (visible_placeholder)
        {
            // Object should be visible since there will be nothing else to display
            sculptGenerateSpherePlaceholder();
        }
        else
        {
            sculptGenerateEmptyPlaceholder();
        }
    }

    for (S32 i = 0; i < (S32)mProfilep->mFaces.size(); i++)
    {
        mFaceMask |= mProfilep->mFaces[i].mFaceID;
    }

    mSculptLevel = sculpt_level;
    mSculptSpherePlaceholder = data_is_empty && visible_placeholder;

    // Delete any existing faces so that they get regenerated
    mVolumeFaces.clear();

    createVolumeFaces();
}




bool LLVolume::isCap(S32 face)
{
    return mProfilep->mFaces[face].mCap;
}

bool LLVolume::isFlat(S32 face)
{
    return mProfilep->mFaces[face].mFlat;
}


bool LLVolumeParams::isSculpt() const
{
    return (mSculptType & LL_SCULPT_TYPE_MASK) != LL_SCULPT_TYPE_NONE;
}

bool LLVolumeParams::isMeshSculpt() const
{
    return (mSculptType & LL_SCULPT_TYPE_MASK) == LL_SCULPT_TYPE_MESH;
}

bool LLVolumeParams::operator==(const LLVolumeParams &params) const
{
    return ( (getPathParams() == params.getPathParams()) &&
             (getProfileParams() == params.getProfileParams()) &&
             (mSculptID == params.mSculptID) &&
             (mSculptType == params.mSculptType) );
}

bool LLVolumeParams::operator!=(const LLVolumeParams &params) const
{
    return ( (getPathParams() != params.getPathParams()) ||
             (getProfileParams() != params.getProfileParams()) ||
             (mSculptID != params.mSculptID) ||
             (mSculptType != params.mSculptType) );
}

bool LLVolumeParams::operator<(const LLVolumeParams &params) const
{
    if( getPathParams() != params.getPathParams() )
    {
        return getPathParams() < params.getPathParams();
    }

    if (getProfileParams() != params.getProfileParams())
    {
        return getProfileParams() < params.getProfileParams();
    }

    if (mSculptID != params.mSculptID)
    {
        return mSculptID < params.mSculptID;
    }

    return mSculptType < params.mSculptType;


}

void LLVolumeParams::copyParams(const LLVolumeParams &params)
{
    mProfileParams.copyParams(params.mProfileParams);
    mPathParams.copyParams(params.mPathParams);
    mSculptID = params.getSculptID();
    mSculptType = params.getSculptType();
}

// Less restricitve approx 0 for volumes
constexpr F32 APPROXIMATELY_ZERO = 0.001f;
bool approx_zero( F32 f, F32 tolerance = APPROXIMATELY_ZERO)
{
    return (f >= -tolerance) && (f <= tolerance);
}

// return true if in range (or nearly so)
static bool limit_range(F32& v, F32 min, F32 max, F32 tolerance = APPROXIMATELY_ZERO)
{
    F32 min_delta = v - min;
    if (min_delta < 0.f)
    {
        v = min;
        if (!approx_zero(min_delta, tolerance))
            return false;
    }
    F32 max_delta = max - v;
    if (max_delta < 0.f)
    {
        v = max;
        if (!approx_zero(max_delta, tolerance))
            return false;
    }
    return true;
}

bool LLVolumeParams::setBeginAndEndS(const F32 b, const F32 e)
{
    bool valid = true;

    // First, clamp to valid ranges.
    F32 begin = b;
    valid &= limit_range(begin, 0.f, 1.f - MIN_CUT_DELTA);

    F32 end = e;
    if (end >= .0149f && end < MIN_CUT_DELTA) end = MIN_CUT_DELTA; // eliminate warning for common rounding error
    valid &= limit_range(end, MIN_CUT_DELTA, 1.f);

    valid &= limit_range(begin, 0.f, end - MIN_CUT_DELTA, .01f);

    // Now set them.
    mProfileParams.setBegin(begin);
    mProfileParams.setEnd(end);

    return valid;
}

bool LLVolumeParams::setBeginAndEndT(const F32 b, const F32 e)
{
    bool valid = true;

    // First, clamp to valid ranges.
    F32 begin = b;
    valid &= limit_range(begin, 0.f, 1.f - MIN_CUT_DELTA);

    F32 end = e;
    valid &= limit_range(end, MIN_CUT_DELTA, 1.f);

    valid &= limit_range(begin, 0.f, end - MIN_CUT_DELTA, .01f);

    // Now set them.
    mPathParams.setBegin(begin);
    mPathParams.setEnd(end);

    return valid;
}

bool LLVolumeParams::setHollow(const F32 h)
{
    // Validate the hollow based on path and profile.
    U8 profile  = mProfileParams.getCurveType() & LL_PCODE_PROFILE_MASK;
    U8 hole_type    = mProfileParams.getCurveType() & LL_PCODE_HOLE_MASK;

    F32 max_hollow = HOLLOW_MAX;

#if 0 // Limit removal
    // Only square holes have trouble.
    if (LL_PCODE_HOLE_SQUARE == hole_type)
    {
        switch(profile)
        {
        case LL_PCODE_PROFILE_CIRCLE:
        case LL_PCODE_PROFILE_CIRCLE_HALF:
        case LL_PCODE_PROFILE_EQUALTRI:
            max_hollow = HOLLOW_MAX_SQUARE;
        }
    }
#endif

    F32 hollow = h;
    bool valid = limit_range(hollow, HOLLOW_MIN, max_hollow);
    mProfileParams.setHollow(hollow);

    return valid;
}

bool LLVolumeParams::setTwistBegin(const F32 b)
{
    F32 twist_begin = b;
    bool valid = limit_range(twist_begin, TWIST_MIN, TWIST_MAX);
    mPathParams.setTwistBegin(twist_begin);
    return valid;
}

bool LLVolumeParams::setTwistEnd(const F32 e)
{
    F32 twist_end = e;
    bool valid = limit_range(twist_end, TWIST_MIN, TWIST_MAX);
    mPathParams.setTwistEnd(twist_end);
    return valid;
}

bool LLVolumeParams::setRatio(const F32 x, const F32 y)
{
    F32 min_x = RATIO_MIN;
    F32 max_x = RATIO_MAX;
    F32 min_y = RATIO_MIN;
    F32 max_y = RATIO_MAX;
    // If this is a circular path (and not a sphere) then 'ratio' is actually hole size.
    U8 path_type    = mPathParams.getCurveType();
    U8 profile_type = mProfileParams.getCurveType() & LL_PCODE_PROFILE_MASK;
    if ( LL_PCODE_PATH_CIRCLE == path_type &&
         LL_PCODE_PROFILE_CIRCLE_HALF != profile_type)
    {
        // Holes are more restricted...
        min_x = HOLE_X_MIN;
        max_x = HOLE_X_MAX;
        min_y = HOLE_Y_MIN;
        max_y = HOLE_Y_MAX;
    }

    F32 ratio_x = x;
    bool valid = limit_range(ratio_x, min_x, max_x);
    F32 ratio_y = y;
    valid &= limit_range(ratio_y, min_y, max_y);

    mPathParams.setScale(ratio_x, ratio_y);

    return valid;
}

bool LLVolumeParams::setShear(const F32 x, const F32 y)
{
    F32 shear_x = x;
    bool valid = limit_range(shear_x, SHEAR_MIN, SHEAR_MAX);
    F32 shear_y = y;
    valid &= limit_range(shear_y, SHEAR_MIN, SHEAR_MAX);
    mPathParams.setShear(shear_x, shear_y);
    return valid;
}

bool LLVolumeParams::setTaperX(const F32 v)
{
    F32 taper = v;
    bool valid = limit_range(taper, TAPER_MIN, TAPER_MAX);
    mPathParams.setTaperX(taper);
    return valid;
}

bool LLVolumeParams::setTaperY(const F32 v)
{
    F32 taper = v;
    bool valid = limit_range(taper, TAPER_MIN, TAPER_MAX);
    mPathParams.setTaperY(taper);
    return valid;
}

bool LLVolumeParams::setRevolutions(const F32 r)
{
    F32 revolutions = r;
    bool valid = limit_range(revolutions, REV_MIN, REV_MAX);
    mPathParams.setRevolutions(revolutions);
    return valid;
}

bool LLVolumeParams::setRadiusOffset(const F32 offset)
{
    bool valid = true;

    // If this is a sphere, just set it to 0 and get out.
    U8 path_type    = mPathParams.getCurveType();
    U8 profile_type = mProfileParams.getCurveType() & LL_PCODE_PROFILE_MASK;
    if ( LL_PCODE_PROFILE_CIRCLE_HALF == profile_type ||
        LL_PCODE_PATH_CIRCLE != path_type )
    {
        mPathParams.setRadiusOffset(0.f);
        return true;
    }

    // Limit radius offset, based on taper and hole size y.
    F32 radius_offset   = offset;
    F32 taper_y         = getTaperY();
    F32 radius_mag      = fabs(radius_offset);
    F32 hole_y_mag      = fabs(getRatioY());
    F32 taper_y_mag     = fabs(taper_y);
    // Check to see if the taper effects us.
    if ( (radius_offset > 0.f && taper_y < 0.f) ||
            (radius_offset < 0.f && taper_y > 0.f) )
    {
        // The taper does not help increase the radius offset range.
        taper_y_mag = 0.f;
    }
    F32 max_radius_mag = 1.f - hole_y_mag * (1.f - taper_y_mag) / (1.f - hole_y_mag);

    // Enforce the maximum magnitude.
    F32 delta = max_radius_mag - radius_mag;
    if (delta < 0.f)
    {
        // Check radius offset sign.
        if (radius_offset < 0.f)
        {
            radius_offset = -max_radius_mag;
        }
        else
        {
            radius_offset = max_radius_mag;
        }
        valid = approx_zero(delta, .1f);
    }

    mPathParams.setRadiusOffset(radius_offset);
    return valid;
}

bool LLVolumeParams::setSkew(const F32 skew_value)
{
    bool valid = true;

    // Check the skew value against the revolutions.
    F32 skew        = llclamp(skew_value, SKEW_MIN, SKEW_MAX);
    F32 skew_mag    = fabs(skew);
    F32 revolutions = getRevolutions();
    F32 scale_x     = getRatioX();
    F32 min_skew_mag = 1.0f - 1.0f / (revolutions * scale_x + 1.0f);
    // Discontinuity; A revolution of 1 allows skews below 0.5.
    if ( fabs(revolutions - 1.0f) < 0.001)
        min_skew_mag = 0.0f;

    // Clip skew.
    F32 delta = skew_mag - min_skew_mag;
    if (delta < 0.f)
    {
        // Check skew sign.
        if (skew < 0.0f)
        {
            skew = -min_skew_mag;
        }
        else
        {
            skew = min_skew_mag;
        }
        valid = approx_zero(delta, .01f);
    }

    mPathParams.setSkew(skew);
    return valid;
}

bool LLVolumeParams::setSculptID(const LLUUID& sculpt_id, U8 sculpt_type)
{
    mSculptID = sculpt_id;
    mSculptType = sculpt_type;
    return true;
}

bool LLVolumeParams::setType(U8 profile, U8 path)
{
    bool result = true;
    // First, check profile and path for validity.
    U8 profile_type = profile & LL_PCODE_PROFILE_MASK;
    U8 hole_type    = (profile & LL_PCODE_HOLE_MASK) >> 4;
    U8 path_type    = path >> 4;

    if (profile_type > LL_PCODE_PROFILE_MAX)
    {
        // Bad profile.  Make it square.
        profile = LL_PCODE_PROFILE_SQUARE;
        result = false;
        LL_WARNS() << "LLVolumeParams::setType changing bad profile type (" << profile_type
                << ") to be LL_PCODE_PROFILE_SQUARE" << LL_ENDL;
    }
    else if (hole_type > LL_PCODE_HOLE_MAX)
    {
        // Bad hole.  Make it the same.
        profile = profile_type;
        result = false;
        LL_WARNS() << "LLVolumeParams::setType changing bad hole type (" << hole_type
                << ") to be LL_PCODE_HOLE_SAME" << LL_ENDL;
    }

    if (path_type < LL_PCODE_PATH_MIN ||
        path_type > LL_PCODE_PATH_MAX)
    {
        // Bad path.  Make it linear.
        result = false;
        LL_WARNS() << "LLVolumeParams::setType changing bad path (" << path
                << ") to be LL_PCODE_PATH_LINE" << LL_ENDL;
        path = LL_PCODE_PATH_LINE;
    }

    mProfileParams.setCurveType(profile);
    mPathParams.setCurveType(path);
    return result;
}

// static
bool LLVolumeParams::validate(U8 prof_curve, F32 prof_begin, F32 prof_end, F32 hollow,
        U8 path_curve, F32 path_begin, F32 path_end,
        F32 scx, F32 scy, F32 shx, F32 shy,
        F32 twistend, F32 twistbegin, F32 radiusoffset,
        F32 tx, F32 ty, F32 revolutions, F32 skew)
{
    LLVolumeParams test_params;
    if (!test_params.setType        (prof_curve, path_curve))
    {
            return false;
    }
    if (!test_params.setBeginAndEndS    (prof_begin, prof_end))
    {
            return false;
    }
    if (!test_params.setBeginAndEndT    (path_begin, path_end))
    {
            return false;
    }
    if (!test_params.setHollow      (hollow))
    {
            return false;
    }
    if (!test_params.setTwistBegin      (twistbegin))
    {
            return false;
    }
    if (!test_params.setTwistEnd        (twistend))
    {
            return false;
    }
    if (!test_params.setRatio       (scx, scy))
    {
            return false;
    }
    if (!test_params.setShear       (shx, shy))
    {
            return false;
    }
    if (!test_params.setTaper       (tx, ty))
    {
            return false;
    }
    if (!test_params.setRevolutions     (revolutions))
    {
            return false;
    }
    if (!test_params.setRadiusOffset    (radiusoffset))
    {
            return false;
    }
    if (!test_params.setSkew        (skew))
    {
            return false;
    }
    return true;
}

void LLVolume::getLoDTriangleCounts(const LLVolumeParams& params, S32* counts)
{ //attempt to approximate the number of triangles that will result from generating a volume LoD set for the
    //supplied LLVolumeParams -- inaccurate, but a close enough approximation for determining streaming cost
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;
    F32 detail[] = {1.f, 1.5f, 2.5f, 4.f};
    for (S32 i = 0; i < 4; i++)
    {
        S32 count = 0;
        S32 path_points = LLPath::getNumPoints(params.getPathParams(), detail[i]);
        S32 profile_points = LLProfile::getNumPoints(params.getProfileParams(), false, detail[i]);

        count = (profile_points-1)*2*(path_points-1);
        count += profile_points*2;

        counts[i] = count;
    }
}


S32 LLVolume::getNumTriangles(S32* vcount) const
{
    U32 triangle_count = 0;
    U32 vertex_count = 0;

    for (S32 i = 0; i < getNumVolumeFaces(); ++i)
    {
        const LLVolumeFace& face = getVolumeFace(i);
        triangle_count += face.mNumIndices/3;

        vertex_count += face.mNumVertices;
    }


    if (vcount)
    {
        *vcount = vertex_count;
    }

    return triangle_count;
}

S32 LLVolume::lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end,
                                   S32 face,
                                   LLVector4a* intersection,LLVector2* tex_coord, LLVector4a* normal, LLVector4a* tangent_out)
{
    S32 hit_face = -1;

    S32 start_face;
    S32 end_face;

    if (face == -1) // ALL_SIDES
    {
        start_face = 0;
        end_face = getNumVolumeFaces() - 1;
    }
    else
    {
        start_face = face;
        end_face = face;
    }

    LLVector4a dir;
    dir.setSub(end, start);

    F32 closest_t = 2.f; // must be larger than 1

    end_face = llmin(end_face, getNumVolumeFaces()-1);

    for (S32 i = start_face; i <= end_face; i++)
    {
        LLVolumeFace &face = mVolumeFaces[i];

        LLVector4a box_center;
        box_center.setAdd(face.mExtents[0], face.mExtents[1]);
        box_center.mul(0.5f);

        LLVector4a box_size;
        box_size.setSub(face.mExtents[1], face.mExtents[0]);

        if (LLLineSegmentBoxIntersect(start, end, box_center, box_size))
        {
            if (tangent_out != NULL) // if the caller wants tangents, we may need to generate them
            {
                genTangents(i);
            }

            if (isUnique())
            { //don't bother with an octree for flexi volumes
                U32 tri_count = face.mNumIndices/3;

                for (U32 j = 0; j < tri_count; ++j)
                {
                    U16 idx0 = face.mIndices[j*3+0];
                    U16 idx1 = face.mIndices[j*3+1];
                    U16 idx2 = face.mIndices[j*3+2];

                    const LLVector4a& v0 = face.mPositions[idx0];
                    const LLVector4a& v1 = face.mPositions[idx1];
                    const LLVector4a& v2 = face.mPositions[idx2];

                    F32 a,b,t;

                    if (LLTriangleRayIntersect(v0, v1, v2,
                            start, dir, a, b, t))
                    {
                        if ((t >= 0.f) &&      // if hit is after start
                            (t <= 1.f) &&      // and before end
                            (t < closest_t))   // and this hit is closer
                        {
                            closest_t = t;
                            hit_face = i;

                            if (intersection != NULL)
                            {
                                LLVector4a intersect = dir;
                                intersect.mul(closest_t);
                                intersect.add(start);
                                *intersection = intersect;
                            }


                            if (tex_coord != NULL)
                            {
                                LLVector2* tc = (LLVector2*) face.mTexCoords;
                                *tex_coord = ((1.f - a - b)  * tc[idx0] +
                                    a              * tc[idx1] +
                                    b              * tc[idx2]);

                            }

                            if (normal!= NULL)
                            {
                                LLVector4a* norm = face.mNormals;

                                LLVector4a n1,n2,n3;
                                n1 = norm[idx0];
                                n1.mul(1.f-a-b);

                                n2 = norm[idx1];
                                n2.mul(a);

                                n3 = norm[idx2];
                                n3.mul(b);

                                n1.add(n2);
                                n1.add(n3);

                                *normal     = n1;
                            }

                            if (tangent_out != NULL)
                            {
                                LLVector4a* tangents = face.mTangents;

                                LLVector4a t1,t2,t3;
                                t1 = tangents[idx0];
                                t1.mul(1.f-a-b);

                                t2 = tangents[idx1];
                                t2.mul(a);

                                t3 = tangents[idx2];
                                t3.mul(b);

                                t1.add(t2);
                                t1.add(t3);

                                *tangent_out = t1;
                            }
                        }
                    }
                }
            }
            else
            {
                if (!face.getOctree())
                {
                    face.createOctree();
                }

                LLOctreeTriangleRayIntersect intersect(start, dir, &face, &closest_t, intersection, tex_coord, normal, tangent_out);
                intersect.traverse(face.getOctree());
                if (intersect.mHitFace)
                {
                    hit_face = i;
                }
            }
        }
    }


    return hit_face;
}

class LLVertexIndexPair
{
public:
    LLVertexIndexPair(const LLVector3 &vertex, const S32 index);

    LLVector3 mVertex;
    S32 mIndex;
};

LLVertexIndexPair::LLVertexIndexPair(const LLVector3 &vertex, const S32 index)
{
    mVertex = vertex;
    mIndex = index;
}

constexpr F32 VERTEX_SLOP = 0.00001f;

struct lessVertex
{
    bool operator()(const LLVertexIndexPair *a, const LLVertexIndexPair *b)
    {
        const F32 slop = VERTEX_SLOP;

        if (a->mVertex.mV[0] + slop < b->mVertex.mV[0])
        {
            return true;
        }
        else if (a->mVertex.mV[0] - slop > b->mVertex.mV[0])
        {
            return false;
        }

        if (a->mVertex.mV[1] + slop < b->mVertex.mV[1])
        {
            return true;
        }
        else if (a->mVertex.mV[1] - slop > b->mVertex.mV[1])
        {
            return false;
        }

        if (a->mVertex.mV[2] + slop < b->mVertex.mV[2])
        {
            return true;
        }
        else if (a->mVertex.mV[2] - slop > b->mVertex.mV[2])
        {
            return false;
        }

        return false;
    }
};

struct lessTriangle
{
    bool operator()(const S32 *a, const S32 *b)
    {
        if (*a < *b)
        {
            return true;
        }
        else if (*a > *b)
        {
            return false;
        }

        if (*(a+1) < *(b+1))
        {
            return true;
        }
        else if (*(a+1) > *(b+1))
        {
            return false;
        }

        if (*(a+2) < *(b+2))
        {
            return true;
        }
        else if (*(a+2) > *(b+2))
        {
            return false;
        }

        return false;
    }
};

bool equalTriangle(const S32 *a, const S32 *b)
{
    if ((*a == *b) && (*(a+1) == *(b+1)) && (*(a+2) == *(b+2)))
    {
        return true;
    }
    return false;
}

bool LLVolumeParams::importFile(LLFILE *fp)
{
    //LL_INFOS() << "importing volume" << LL_ENDL;
    const S32 BUFSIZE = 16384;
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    // *NOTE: changing the size or type of this buffer will require
    // changing the sscanf below.
    char keyword[256];  /* Flawfinder: ignore */
    keyword[0] = 0;

    while (!feof(fp))
    {
        if (fgets(buffer, BUFSIZE, fp) == NULL)
        {
            buffer[0] = '\0';
        }

        sscanf(buffer, " %255s", keyword);  /* Flawfinder: ignore */
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("profile", keyword))
        {
            mProfileParams.importFile(fp);
        }
        else if (!strcmp("path",keyword))
        {
            mPathParams.importFile(fp);
        }
        else
        {
            LL_WARNS() << "unknown keyword " << keyword << " in volume import" << LL_ENDL;
        }
    }

    return true;
}

bool LLVolumeParams::exportFile(LLFILE *fp) const
{
    fprintf(fp,"\tshape 0\n");
    fprintf(fp,"\t{\n");
    mPathParams.exportFile(fp);
    mProfileParams.exportFile(fp);
    fprintf(fp, "\t}\n");
    return true;
}


bool LLVolumeParams::importLegacyStream(std::istream& input_stream)
{
    //LL_INFOS() << "importing volume" << LL_ENDL;
    const S32 BUFSIZE = 16384;
    // *NOTE: changing the size or type of this buffer will require
    // changing the sscanf below.
    char buffer[BUFSIZE];       /* Flawfinder: ignore */
    char keyword[256];      /* Flawfinder: ignore */
    keyword[0] = 0;

    while (input_stream.good())
    {
        input_stream.getline(buffer, BUFSIZE);
        sscanf(buffer, " %255s", keyword);
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("profile", keyword))
        {
            mProfileParams.importLegacyStream(input_stream);
        }
        else if (!strcmp("path",keyword))
        {
            mPathParams.importLegacyStream(input_stream);
        }
        else
        {
            LL_WARNS() << "unknown keyword " << keyword << " in volume import" << LL_ENDL;
        }
    }

    return true;
}

bool LLVolumeParams::exportLegacyStream(std::ostream& output_stream) const
{
    output_stream <<"\tshape 0\n";
    output_stream <<"\t{\n";
    mPathParams.exportLegacyStream(output_stream);
    mProfileParams.exportLegacyStream(output_stream);
    output_stream << "\t}\n";
    return true;
}

LLSD LLVolumeParams::sculptAsLLSD() const
{
    LLSD sd = LLSD();
    sd["id"] = getSculptID();
    sd["type"] = getSculptType();

    return sd;
}

bool LLVolumeParams::sculptFromLLSD(LLSD& sd)
{
    setSculptID(sd["id"].asUUID(), (U8)sd["type"].asInteger());
    return true;
}

LLSD LLVolumeParams::asLLSD() const
{
    LLSD sd = LLSD();
    sd["path"] = mPathParams;
    sd["profile"] = mProfileParams;
    sd["sculpt"] = sculptAsLLSD();

    return sd;
}

bool LLVolumeParams::fromLLSD(LLSD& sd)
{
    mPathParams.fromLLSD(sd["path"]);
    mProfileParams.fromLLSD(sd["profile"]);
    sculptFromLLSD(sd["sculpt"]);

    return true;
}

void LLVolumeParams::reduceS(F32 begin, F32 end)
{
    begin = llclampf(begin);
    end = llclampf(end);
    if (begin > end)
    {
        F32 temp = begin;
        begin = end;
        end = temp;
    }
    F32 a = mProfileParams.getBegin();
    F32 b = mProfileParams.getEnd();
    mProfileParams.setBegin(a + begin * (b - a));
    mProfileParams.setEnd(a + end * (b - a));
}

void LLVolumeParams::reduceT(F32 begin, F32 end)
{
    begin = llclampf(begin);
    end = llclampf(end);
    if (begin > end)
    {
        F32 temp = begin;
        begin = end;
        end = temp;
    }
    F32 a = mPathParams.getBegin();
    F32 b = mPathParams.getEnd();
    mPathParams.setBegin(a + begin * (b - a));
    mPathParams.setEnd(a + end * (b - a));
}

const F32 MIN_CONCAVE_PROFILE_WEDGE = 0.125f;   // 1/8 unity
const F32 MIN_CONCAVE_PATH_WEDGE = 0.111111f;   // 1/9 unity

// returns true if the shape can be approximated with a convex shape
// for collison purposes
bool LLVolumeParams::isConvex() const
{
    if (!getSculptID().isNull())
    {
        // can't determine, be safe and say no:
        return false;
    }

    F32 path_length = mPathParams.getEnd() - mPathParams.getBegin();
    F32 hollow = mProfileParams.getHollow();

    U8 path_type = mPathParams.getCurveType();
    if ( path_length > MIN_CONCAVE_PATH_WEDGE
        && ( mPathParams.getTwist() != mPathParams.getTwistBegin()
             || (hollow > 0.f
                 && LL_PCODE_PATH_LINE != path_type) ) )
    {
        // twist along a "not too short" path is concave
        return false;
    }

    F32 profile_length = mProfileParams.getEnd() - mProfileParams.getBegin();
    bool same_hole = hollow == 0.f
                     || (mProfileParams.getCurveType() & LL_PCODE_HOLE_MASK) == LL_PCODE_HOLE_SAME;

    F32 min_profile_wedge = MIN_CONCAVE_PROFILE_WEDGE;
    U8 profile_type = mProfileParams.getCurveType() & LL_PCODE_PROFILE_MASK;
    if ( LL_PCODE_PROFILE_CIRCLE_HALF == profile_type )
    {
        // it is a sphere and spheres get twice the minimum profile wedge
        min_profile_wedge = 2.f * MIN_CONCAVE_PROFILE_WEDGE;
    }

    bool convex_profile = ( ( profile_length == 1.f
                             || profile_length <= 0.5f )
                           && hollow == 0.f )                       // trivially convex
                          || ( profile_length <= min_profile_wedge
                              && same_hole );                       // effectvely convex (even when hollow)

    if (!convex_profile)
    {
        // profile is concave
        return false;
    }

    if ( LL_PCODE_PATH_LINE == path_type )
    {
        // straight paths with convex profile
        return true;
    }

    bool concave_path = (path_length < 1.0f) && (path_length > 0.5f);
    if (concave_path)
    {
        return false;
    }

    // we're left with spheres, toroids and tubes
    if ( LL_PCODE_PROFILE_CIRCLE_HALF == profile_type )
    {
        // at this stage all spheres must be convex
        return true;
    }

    // it's a toroid or tube
    if ( path_length <= MIN_CONCAVE_PATH_WEDGE )
    {
        // effectively convex
        return true;
    }

    return false;
}

// debug
void LLVolumeParams::setCube()
{
    mProfileParams.setCurveType(LL_PCODE_PROFILE_SQUARE);
    mProfileParams.setBegin(0.f);
    mProfileParams.setEnd(1.f);
    mProfileParams.setHollow(0.f);

    mPathParams.setBegin(0.f);
    mPathParams.setEnd(1.f);
    mPathParams.setScale(1.f, 1.f);
    mPathParams.setShear(0.f, 0.f);
    mPathParams.setCurveType(LL_PCODE_PATH_LINE);
    mPathParams.setTwistBegin(0.f);
    mPathParams.setTwistEnd(0.f);
    mPathParams.setRadiusOffset(0.f);
    mPathParams.setTaper(0.f, 0.f);
    mPathParams.setRevolutions(0.f);
    mPathParams.setSkew(0.f);
}

LLFaceID LLVolume::generateFaceMask()
{
    LLFaceID new_mask = 0x0000;

    switch(mParams.getProfileParams().getCurveType() & LL_PCODE_PROFILE_MASK)
    {
    case LL_PCODE_PROFILE_CIRCLE:
    case LL_PCODE_PROFILE_CIRCLE_HALF:
        new_mask |= LL_FACE_OUTER_SIDE_0;
        break;
    case LL_PCODE_PROFILE_SQUARE:
        {
            for(S32 side = (S32)(mParams.getProfileParams().getBegin() * 4.f); side < llceil(mParams.getProfileParams().getEnd() * 4.f); side++)
            {
                new_mask |= LL_FACE_OUTER_SIDE_0 << side;
            }
        }
        break;
    case LL_PCODE_PROFILE_ISOTRI:
    case LL_PCODE_PROFILE_EQUALTRI:
    case LL_PCODE_PROFILE_RIGHTTRI:
        {
            for(S32 side = (S32)(mParams.getProfileParams().getBegin() * 3.f); side < llceil(mParams.getProfileParams().getEnd() * 3.f); side++)
            {
                new_mask |= LL_FACE_OUTER_SIDE_0 << side;
            }
        }
        break;
    default:
        LL_ERRS() << "Unknown profile!" << LL_ENDL;
        break;
    }

    // handle hollow objects
    if (mParams.getProfileParams().getHollow() > 0)
    {
        new_mask |= LL_FACE_INNER_SIDE;
    }

    // handle open profile curves
    if (mProfilep->isOpen())
    {
        new_mask |= LL_FACE_PROFILE_BEGIN | LL_FACE_PROFILE_END;
    }

    // handle open path curves
    if (mPathp->isOpen())
    {
        new_mask |= LL_FACE_PATH_BEGIN | LL_FACE_PATH_END;
    }

    return new_mask;
}

bool LLVolume::isFaceMaskValid(LLFaceID face_mask)
{
    LLFaceID test_mask = 0;
    for(S32 i = 0; i < getNumFaces(); i++)
    {
        test_mask |= mProfilep->mFaces[i].mFaceID;
    }

    return test_mask == face_mask;
}

bool LLVolume::isConvex() const
{
    // mParams.isConvex() may return false even though the final
    // geometry is actually convex due to LOD approximations.
    // TODO -- provide LLPath and LLProfile with isConvex() methods
    // that correctly determine convexity. -- Leviathan
    return mParams.isConvex();
}


std::ostream& operator<<(std::ostream &s, const LLProfileParams &profile_params)
{
    s << "{type=" << (U32) profile_params.mCurveType;
    s << ", begin=" << profile_params.mBegin;
    s << ", end=" << profile_params.mEnd;
    s << ", hollow=" << profile_params.mHollow;
    s << "}";
    return s;
}


std::ostream& operator<<(std::ostream &s, const LLPathParams &path_params)
{
    s << "{type=" << (U32) path_params.mCurveType;
    s << ", begin=" << path_params.mBegin;
    s << ", end=" << path_params.mEnd;
    s << ", twist=" << path_params.mTwistEnd;
    s << ", scale=" << path_params.mScale;
    s << ", shear=" << path_params.mShear;
    s << ", twist_begin=" << path_params.mTwistBegin;
    s << ", radius_offset=" << path_params.mRadiusOffset;
    s << ", taper=" << path_params.mTaper;
    s << ", revolutions=" << path_params.mRevolutions;
    s << ", skew=" << path_params.mSkew;
    s << "}";
    return s;
}


std::ostream& operator<<(std::ostream &s, const LLVolumeParams &volume_params)
{
    s << "{profileparams = " << volume_params.mProfileParams;
    s << ", pathparams = " << volume_params.mPathParams;
    s << "}";
    return s;
}


std::ostream& operator<<(std::ostream &s, const LLProfile &profile)
{
    s << " {open=" << (U32) profile.mOpen;
    s << ", dirty=" << profile.mDirty;
    s << ", totalout=" << profile.mTotalOut;
    s << ", total=" << profile.mTotal;
    s << "}";
    return s;
}


std::ostream& operator<<(std::ostream &s, const LLPath &path)
{
    s << "{open=" << (U32) path.mOpen;
    s << ", dirty=" << path.mDirty;
    s << ", step=" << path.mStep;
    s << ", total=" << path.mTotal;
    s << "}";
    return s;
}

std::ostream& operator<<(std::ostream &s, const LLVolume &volume)
{
    s << "{params = " << volume.getParams();
    s << ", path = " << *volume.mPathp;
    s << ", profile = " << *volume.mProfilep;
    s << "}";
    return s;
}


std::ostream& operator<<(std::ostream &s, const LLVolume *volumep)
{
    s << "{params = " << volumep->getParams();
    s << ", path = " << *(volumep->mPathp);
    s << ", profile = " << *(volumep->mProfilep);
    s << "}";
    return s;
}

LLVolumeFace::LLVolumeFace() :
    mID(0),
    mTypeMask(0),
    mBeginS(0),
    mBeginT(0),
    mNumS(0),
    mNumT(0),
    mNumVertices(0),
    mNumAllocatedVertices(0),
    mNumIndices(0),
    mPositions(NULL),
    mNormals(NULL),
    mTangents(NULL),
    mTexCoords(NULL),
    mIndices(NULL),
    mWeights(NULL),
#if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
    mJustWeights(NULL),
    mJointIndices(NULL),
#endif
    mWeightsScrubbed(false),
    mOctree(NULL),
    mOctreeTriangles(NULL),
    mOptimized(false)
{
    mExtents = (LLVector4a*) ll_aligned_malloc_16(sizeof(LLVector4a)*3);
    mExtents[0].splat(-0.5f);
    mExtents[1].splat(0.5f);
    mCenter = mExtents+2;
}

LLVolumeFace::LLVolumeFace(const LLVolumeFace& src)
:   mID(0),
    mTypeMask(0),
    mBeginS(0),
    mBeginT(0),
    mNumS(0),
    mNumT(0),
    mNumVertices(0),
    mNumAllocatedVertices(0),
    mNumIndices(0),
    mPositions(NULL),
    mNormals(NULL),
    mTangents(NULL),
    mTexCoords(NULL),
    mIndices(NULL),
    mWeights(NULL),
#if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
    mJustWeights(NULL),
    mJointIndices(NULL),
#endif
    mWeightsScrubbed(false),
    mOctree(NULL),
    mOctreeTriangles(NULL)
{
    try
    {
        mExtents = (LLVector4a*)ll_aligned_malloc_16(sizeof(LLVector4a) * 3);
        mCenter = mExtents + 2;
        *this = src;
    }
    catch (std::bad_alloc&)
    {
        LLError::LLUserWarningMsg::showOutOfMemory();
        LL_ERRS("LLVolume") << "Bad memory allocation in LLVolumeFace" << LL_ENDL;
    }
}

LLVolumeFace& LLVolumeFace::operator=(const LLVolumeFace& src)
{
    if (&src == this)
    { //self assignment, do nothing
        return *this;
    }

    mID = src.mID;
    mTypeMask = src.mTypeMask;
    mBeginS = src.mBeginS;
    mBeginT = src.mBeginT;
    mNumS = src.mNumS;
    mNumT = src.mNumT;
    mJoinsS = src.mJoinsS;
    mJoinsT = src.mJoinsT;
    mReversed = src.mReversed;
    mFlexible = src.mFlexible;
    std::copy(std::begin(src.mCapTangent), std::end(src.mCapTangent), mCapTangent);

    mExtents[0] = src.mExtents[0];
    mExtents[1] = src.mExtents[1];
    *mCenter = *src.mCenter;

    mNumVertices = 0;
    mNumIndices = 0;

    freeData();

    resizeVertices(src.mNumVertices);
    resizeIndices(src.mNumIndices);
    // after resizeVertices, which drops the planar normal with the vertices
    std::copy(std::begin(src.mPlanarNormal), std::end(src.mPlanarNormal), mPlanarNormal);

    if (mNumVertices)
    {
        S32 vert_size = mNumVertices*sizeof(LLVector4a);
        S32 tc_size = (mNumVertices*sizeof(LLVector2)+0xF) & ~0xF;

        LLVector4a::memcpyNonAliased16((F32*) mPositions, (F32*) src.mPositions, vert_size);

        if (src.mNormals)
        {
        LLVector4a::memcpyNonAliased16((F32*) mNormals, (F32*) src.mNormals, vert_size);
        }

        if(src.mTexCoords)
        {
            LLVector4a::memcpyNonAliased16((F32*) mTexCoords, (F32*) src.mTexCoords, tc_size);
        }

        // A source without normals or texture coordinates makes a face
        // without them, not one whose are unwritten.
        if (!src.mNormals)
        {
            mNormals = NULL;
        }
        if (!src.mTexCoords)
        {
            mTexCoords = NULL;
        }

        if (src.mTangents)
        {
            allocateTangents(src.mNumVertices);
            LLVector4a::memcpyNonAliased16((F32*) mTangents, (F32*) src.mTangents, vert_size);
            mTangentsStale = src.mTangentsStale;
        }
        else
        {
            ll_aligned_free_16(mTangents);
            mTangents = NULL;
        }

        if (src.mWeights)
        {
            llassert(!mWeights); // don't orphan an old alloc here accidentally
            allocateWeights(src.mNumVertices);
            LLVector4a::memcpyNonAliased16((F32*) mWeights, (F32*) src.mWeights, vert_size);
            mWeightsScrubbed = src.mWeightsScrubbed;
        }
        else
        {
            ll_aligned_free_16(mWeights);
            mWeights = NULL;
            mWeightsScrubbed = false;
        }

    #if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
        if (src.mJointIndices)
        {
            llassert(!mJointIndices); // don't orphan an old alloc here accidentally
            allocateJointIndices(src.mNumVertices);
            LLVector4a::memcpyNonAliased16((F32*) mJointIndices, (F32*) src.mJointIndices, src.mNumVertices * sizeof(U8) * 4);
        }
        else*/
        {
            ll_aligned_free_16(mJointIndices);
            mJointIndices = NULL;
        }
    #endif

    }

    if (mNumIndices)
    {
        S32 idx_size = (mNumIndices*sizeof(U16)+0xF) & ~0xF;

        LLVector4a::memcpyNonAliased16((F32*) mIndices, (F32*) src.mIndices, idx_size);
    }
    else
    {
        ll_aligned_free_16(mIndices);
        mIndices = NULL;
    }

    mOptimized = src.mOptimized;
    mNormalizedScale = src.mNormalizedScale;

    //delete
    return *this;
}

LLVolumeFace::~LLVolumeFace()
{
    ll_aligned_free_16(mExtents);
    mExtents = NULL;
    mCenter = NULL;

    freeData();
}

void LLVolumeFace::freeData()
{
    ll_aligned_free<64>(mPositions);
    mPositions = NULL;

    //normals and texture coordinates are part of the same buffer as mPositions, do not free them separately
    mNormals = NULL;
    mTexCoords = NULL;

    ll_aligned_free_16(mIndices);
    mIndices = NULL;
    ll_aligned_free_16(mTangents);
    mTangents = NULL;
    ll_aligned_free_16(mWeights);
    mWeights = NULL;

#if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
    ll_aligned_free_16(mJointIndices);
    mJointIndices = NULL;
    ll_aligned_free_16(mJustWeights);
    mJustWeights = NULL;
#endif

    destroyOctree();
}

bool LLVolumeFace::create(LLVolume* volume, bool partial_build)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    //tree for this face is no longer valid
    destroyOctree();

    // A partial build moves the positions under the tangents and keeps their
    // buffer; a full build frees it.
    mTangentsStale = partial_build;
    mFlexible = volume->getParams().getPathParams().getCurveType() == LL_PCODE_PATH_FLEXIBLE;
    // A flexible prim's frames, its partial builds, keep the planar normal
    // of its first build.
    if (!partial_build)
    {
        mPlanarNormal[3] = 0.f;
    }

    bool ret = false ;
    if (mTypeMask & CAP_MASK)
    {
        ret = createCap(volume, partial_build);
    }
    else if ((mTypeMask & END_MASK) || (mTypeMask & SIDE_MASK))
    {
        ret = createSide(volume, partial_build);
    }
    else
    {
        LL_ERRS() << "Unknown/uninitialized face type!" << LL_ENDL;
    }

    return ret ;
}

bool LLVolumeFace::VertexMapData::operator==(const LLVolumeFace::VertexData& rhs) const
{
    return getPosition().equals3(rhs.getPosition()) &&
        mTexCoord == rhs.mTexCoord &&
        getNormal().equals3(rhs.getNormal());
}

bool LLVolumeFace::VertexMapData::ComparePosition::operator()(const LLVector3& a, const LLVector3& b) const
{
    if (a.mV[0] != b.mV[0])
    {
        return a.mV[0] < b.mV[0];
    }

    if (a.mV[1] != b.mV[1])
    {
        return a.mV[1] < b.mV[1];
    }

    return a.mV[2] < b.mV[2];
}

// Uploads reach this, so the indices are untrusted: an index past the face's
// vertices is taken as vertex 0, with a warning, before anything reads
// through it.
void LLVolumeFace::remap()
{
    ALScratchArena::Scope scratch;

    // The indices at 32 bits, as meshoptimizer takes them, checked as they
    // go. A face without indices is welded as unindexed geometry.
    U32* indices = scratch.allocate<U32>(mNumIndices);
    const U32* checked = nullptr;
    if (mIndices)
    {
        S32 out_of_range_count = 0;
        for (S32 i = 0; i < mNumIndices; ++i)
        {
            if (mIndices[i] < mNumVertices)
            {
                indices[i] = mIndices[i];
            }
            else
            {
                out_of_range_count++;
                indices[i] = 0;
            }
        }

        if (out_of_range_count)
        {
            LL_WARNS() << out_of_range_count << " indices are out of range." << LL_ENDL;
        }
        checked = indices;
    }

    // Each vertex's place among the vertices that differ in position, normal
    // or texture coordinate, or ~0u where no index uses it. Normals and
    // texture coordinates the face has none of take no part.
    meshopt_Stream streams[3] = { { mPositions, sizeof(F32) * 3, sizeof(LLVector4a) } };
    size_t stream_count = 1;
    if (mNormals)
    {
        streams[stream_count++] = { mNormals, sizeof(F32) * 3, sizeof(LLVector4a) };
    }
    if (mTexCoords)
    {
        streams[stream_count++] = { mTexCoords, sizeof(LLVector2), sizeof(LLVector2) };
    }
    U32* remap = scratch.allocate<U32>(mNumVertices);
    S32 remap_vertices_count = static_cast<S32>(meshopt_generateVertexRemapMulti(remap, checked, checked ? mNumIndices : mNumVertices,
                                                                                 mNumVertices, streams, stream_count));
    meshopt_remapIndexBuffer(indices, checked, mNumIndices, remap);

    // Allocate new buffers
    S32 size = ((mNumIndices * sizeof(U16)) + 0xF) & ~0xF;
    U16* remap_indices = (U16*)ll_aligned_malloc_16(size);

    S32 tc_bytes_size = ((remap_vertices_count * sizeof(LLVector2)) + 0xF) & ~0xF;
    LLVector4a* remap_positions = (LLVector4a*)ll_aligned_malloc<64>(sizeof(LLVector4a) * 2 * remap_vertices_count + tc_bytes_size);
    LLVector4a* remap_normals = remap_positions + remap_vertices_count;
    LLVector2* remap_tex_coords = (LLVector2*)(remap_normals + remap_vertices_count);

    // Fill the buffers, the normals and texture coordinates only where the
    // face has them
    for (S32 i = 0; i < mNumIndices; ++i)
    {
        remap_indices[i] = static_cast<U16>(indices[i]);
    }
    meshopt_remapVertexBuffer(remap_positions, mPositions, mNumVertices, sizeof(LLVector4a), remap);
    if (mNormals)
    {
        meshopt_remapVertexBuffer(remap_normals, mNormals, mNumVertices, sizeof(LLVector4a), remap);
    }
    else
    {
        remap_normals = nullptr;
    }
    if (mTexCoords)
    {
        meshopt_remapVertexBuffer(remap_tex_coords, mTexCoords, mNumVertices, sizeof(LLVector2), remap);
    }
    else
    {
        remap_tex_coords = nullptr;
    }

    // Free unused buffers
    ll_aligned_free_16(mIndices);
    ll_aligned_free<64>(mPositions);

    // Tangets are now invalid
    ll_aligned_free_16(mTangents);
    mTangents = NULL;

    // Assign new values
    mIndices = remap_indices;
    mPositions = remap_positions;
    mNormals = remap_normals;
    mTexCoords = remap_tex_coords;
    mNumVertices = remap_vertices_count;
    mNumAllocatedVertices = remap_vertices_count;
}

namespace
{
    LLVector4a plane_axis(const LLVector4a& normal);

    // v at unit length in x, y and z, or nothing where it has no length.
    LL_FORCE_INLINE void unit3_or_zero(LLVector4a& v)
    {
        LLVector4a length2;
        length2.setAllDot3(v, v);
        LLVector4a unit = v;
        unit.normalize3();
        v.setSelectWithMask(length2.greaterThan(LLVector4a(FLT_MIN)), unit, LLVector4a::getZero());
    }
} // namespace

// A decoded mesh LOD face made ready to draw: MikkTSpace's tangents, as
// meshoptimizer's compatible mode computes them, and the vertices and
// triangles in the order a vertex cache and a fetch take them best.
//
// The tangents are taken in the shape the mesh was authored in, its
// positions scaled back out of the unit cube by mNormalizedScale and its
// normals by the inverse, and they come back as directions in the face's
// own frame, which the vertex matrix takes as it takes the positions, each
// in its normal's plane. A vertex whose corners take different tangents, as
// on a mirrored UV seam, becomes one vertex for each, and copies of a vertex
// the same in every attribute become one. Every attribute is written once a
// vertex, in its final place, from the face's own arrays, and all the
// scratch is the thread's arena. A face that would need more vertices than
// 16-bit indices reach keeps one of each vertex it has, each with its first
// corner's tangent.
bool LLVolumeFace::cacheOptimize(bool gen_tangents)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;
    llassert(!mOptimized);
    mOptimized = true;

    const bool tangents = gen_tangents && mNormals && mTexCoords;
    if (!mPositions || !mIndices || mNumVertices <= 0 || mNumIndices <= 0)
    {
        if (!tangents)
        {
            // Nothing to order.
            return true;
        }
        LL_WARNS_ONCE("LLVolume") << "Invalid volume face data for tangent generation: "
            << "mPositions=" << (void*)mPositions
            << ", mIndices=" << (void*)mIndices
            << ", mNumVertices=" << mNumVertices
            << ", mNumIndices=" << mNumIndices << LL_ENDL;
        return false;
    }

    if (mNumIndices % 3 != 0)
    {
        LL_WARNS_ONCE("LLVolume") << "Non-triangulated mesh, mNumIndices=" << mNumIndices << LL_ENDL;
        return false;
    }

    if (tangents && (mNormalizedScale.mV[0] == 0.0f || mNormalizedScale.mV[1] == 0.0f || mNormalizedScale.mV[2] == 0.0f))
    {
        LL_WARNS_ONCE("LLVolume") << "Invalid normalized scale: " << mNormalizedScale << LL_ENDL;
        return false;
    }

    const size_t index_count = mNumIndices;
    const size_t vertex_count = mNumVertices;

    try
    {
        ALScratchArena::Scope scratch;

        // The indices at 32 bits, as meshoptimizer takes them, checked as
        // they go.
        U32* indices = scratch.allocate<U32>(index_count);
        U32 highest = 0;
        for (size_t i = 0; i < index_count; ++i)
        {
            indices[i] = mIndices[i];
            highest = llmax(highest, indices[i]);
        }
        if (highest >= vertex_count)
        {
            const U16* bad = std::find_if(mIndices, mIndices + index_count, [&](U16 index) { return index >= vertex_count; });
            LL_WARNS_ONCE("LLVolume") << "Out of bounds index detected: mIndices[" << (bad - mIndices) << "]="
                << *bad << " >= mNumVertices=" << mNumVertices << LL_ENDL;
            return false;
        }

        const LLVector4a inv_scale(1.f / mNormalizedScale.mV[0], 1.f / mNormalizedScale.mV[1], 1.f / mNormalizedScale.mV[2], 1.f);

        // Each corner's tangent, four floats a corner.
        F32* corner_tangents = nullptr;
        if (tangents)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("cacheOptimize - tangents");
            corner_tangents = scratch.allocate<F32>(index_count * 4);

            const LLVector4a scale(mNormalizedScale.mV[0], mNormalizedScale.mV[1], mNormalizedScale.mV[2], 1.f);
            LLVector4Logical xyz;
            xyz.clear();
            xyz.setElement<0>();
            xyz.setElement<1>();
            xyz.setElement<2>();

            ALScratchArena::Scope inputs;
            LLVector4a* positions = inputs.allocate<LLVector4a>(vertex_count);
            LLVector4a* normals = inputs.allocate<LLVector4a>(vertex_count);
            for (size_t v = 0; v < vertex_count; ++v)
            {
                positions[v].setMul(mPositions[v], scale);
                // The face keeps its normals at unit length.
                LLVector4a normal;
                normal.setSelectWithMask(xyz, mNormals[v], LLVector4a::getZero());
                unit3_or_zero(normal);
                mNormals[v] = normal;
                normals[v].setMul(normal, inv_scale);
                unit3_or_zero(normals[v]);
            }
            meshopt_generateTangents(corner_tangents, indices, index_count, positions[0].getF32ptr(), vertex_count, sizeof(LLVector4a),
                                     normals[0].getF32ptr(), sizeof(LLVector4a), mTexCoords[0].mV, sizeof(LLVector2),
                                     meshopt_TangentCompatible | meshopt_TangentZeroFallback);
        }

        if (tangents)
        {
            // Each corner's tangent as the face keeps it, before the weld
            // compares them: back in the face's frame, in which a direction
            // along the surface is the authored one over the scale, put in
            // its normal's plane and normalized. Where the plane leaves it
            // nothing, as at a corner of triangles with no area or whose
            // tangents cancel or run along the normal, it is an axis in the
            // plane, as the prims' take it. w stays the corner's.
            for (size_t c = 0; c < index_count; ++c)
            {
                F32* stored = corner_tangents + c * 4;
                const F32 w = stored[3];
                LLVector4a tangent;
                tangent.load4a(stored);
                tangent.mul(inv_scale);
                const LLVector4a& normal = mNormals[indices[c]];
                LLVector4a along;
                along.setAllDot3(normal, tangent);
                along.mul(normal);
                LLVector4a in_plane;
                in_plane.setSub(tangent, along);
                LLVector4a left, whole;
                left.setAllDot3(in_plane, in_plane);
                whole.setAllDot3(tangent, tangent);
                if (left[0] > 1e-6f * whole[0])
                {
                    in_plane.normalize3();
                }
                else
                {
                    in_plane = plane_axis(normal);
                }
                in_plane.getF32ptr()[3] = w;
                in_plane.store4a(stored);
            }
        }

        // The vertices the triangles are to take, and for each the corner
        // its tangent is taken from. With tangents they are welded from the
        // corners, one for each of a vertex's tangents, in the order the
        // corners first use them, or, where that would need more than
        // 16-bit indices reach, one for each vertex; without, they are the
        // face's own.
        const U32* welded_indices = indices;
        size_t welded_count = vertex_count;
        U32* first_corner = nullptr;
        if (tangents)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("cacheOptimize - weld");

            // A vertex the face holds more than once, the same in every
            // attribute, is one vertex: each corner names its vertex by a
            // number all the copies share.
            U32* canonical = scratch.allocate<U32>(vertex_count);
            const meshopt_Stream attributes[] = {
                { mPositions, sizeof(F32) * 3, sizeof(LLVector4a) },
                { mNormals, sizeof(F32) * 3, sizeof(LLVector4a) },
                { mTexCoords, sizeof(LLVector2), sizeof(LLVector2) },
                { mWeights, sizeof(LLVector4a), sizeof(LLVector4a) },
            };
            const size_t distinct = meshopt_generateVertexRemapMulti(canonical, indices, index_count, vertex_count, attributes, mWeights ? 4 : 3);
            U32* corner_vertices = scratch.allocate<U32>(index_count);
            for (size_t c = 0; c < index_count; ++c)
            {
                corner_vertices[c] = canonical[indices[c]];
            }

            U32* remap = scratch.allocate<U32>(index_count);
            const meshopt_Stream streams[] = {
                { corner_vertices, sizeof(U32), sizeof(U32) },
                { corner_tangents, sizeof(F32) * 4, sizeof(F32) * 4 },
            };
            const size_t unique = meshopt_generateVertexRemapMulti(remap, nullptr, index_count, index_count, streams,
                                                                   sizeof(streams) / sizeof(streams[0]));
            if (unique < 65535)
            {
                welded_indices = remap;
                welded_count = unique;
            }
            else
            {
                welded_indices = corner_vertices;
                welded_count = distinct;
            }
            first_corner = scratch.allocate<U32>(welded_count);
            for (size_t c = index_count; c-- > 0;)
            {
                first_corner[welded_indices[c]] = (U32)c;
            }
        }

        U32* ordered = scratch.allocate<U32>(index_count);
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("cacheOptimize - cache order");
            meshopt_optimizeVertexCache(ordered, welded_indices, index_count, welded_count);
        }

        // Each vertex's final place, in the order the reordered triangles
        // first take it; one no triangle takes has none.
        U32* place = scratch.allocate<U32>(welded_count);
        size_t kept = 0;
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("cacheOptimize - fetch order");
            kept = meshopt_optimizeVertexFetchRemap(place, ordered, index_count, welded_count);
        }

        LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("cacheOptimize - write");
        LLVector4a* old_positions = mPositions;
        const LLVector4a* old_normals = mNormals;
        const LLVector2* old_tex_coords = mTexCoords;
        LLVector4a* old_weights = mWeights;
        LLVector4a* old_tangents = tangents ? nullptr : mTangents;
        mPositions = nullptr;
        mWeights = nullptr;
        if (old_tangents)
        {
            mTangents = nullptr;
        }

        resizeVertices((S32)kept);
        if (!mPositions)
        {
            LLError::LLUserWarningMsg::showOutOfMemory();
            LL_ERRS("LLVolume") << "Failed to allocate memory for resizeVertices(" << kept << ")" << LL_ENDL;
        }
        if (old_weights)
        {
            allocateWeights((S32)kept);
        }
        if (tangents || old_tangents)
        {
            allocateTangents((S32)kept);
        }

        for (size_t u = 0; u < welded_count; ++u)
        {
            const U32 to = place[u];
            if (to == U32_MAX)
            {
                continue;
            }
            const U32 corner = first_corner ? first_corner[u] : U32_MAX;
            const U32 from = first_corner ? indices[corner] : (U32)u;

            mPositions[to] = old_positions[from];
            if (old_normals)
            {
                mNormals[to] = old_normals[from];
            }
            if (old_tex_coords)
            {
                mTexCoords[to] = old_tex_coords[from];
            }
            if (old_weights)
            {
                mWeights[to] = old_weights[from];
            }

            if (tangents)
            {
                mTangents[to].load4a(corner_tangents + (size_t)corner * 4);
            }
            else if (old_tangents)
            {
                mTangents[to] = old_tangents[from];
            }
        }

        for (size_t i = 0; i < index_count; ++i)
        {
            mIndices[i] = (U16)place[ordered[i]];
        }

        // A face without normals or texture coordinates, as the DAE loader
        // makes one, keeps none.
        if (!old_normals)
        {
            mNormals = nullptr;
        }
        if (!old_tex_coords)
        {
            mTexCoords = nullptr;
        }

        ll_aligned_free<64>(old_positions);
        ll_aligned_free_16(old_weights);
        ll_aligned_free_16(old_tangents);
    }
    catch (std::bad_alloc&)
    {
        // Only the scratch and meshoptimizer's allocations throw, all made
        // before the face's buffers are replaced, so the face keeps its
        // buffers. This runs on the mesh threads, where a fatal error may
        // not be raised.
        LL_WARNS("LLVolume") << "Bad memory allocation in cacheOptimize, " << vertex_count << " vertices, " << index_count << " indices"
                             << LL_ENDL;
        return false;
    }

    return true;
}

// meshoptimizer's normals, which join two triangles' corners across the edge
// they share where their own normals are within the crease angle, weighting
// each by its angle and the lengths of its two edges. They are made in the
// shape the mesh was authored in, its positions scaled back out of the unit
// cube by mNormalizedScale, so that the crease angle is the one a user sees,
// and come back as the face's normals, the authored ones times the scale.
// A corner of triangles with no area takes the normal of another corner of
// its vertex, or the vertex's own where it has none, and then the axis
// along z. A vertex whose corners take different normals becomes one vertex
// for each, and vertices then the same in every attribute become one; every
// vertex keeps its position, texture coordinate and weights. The tangents go,
// as they no longer fit the normals.
bool LLVolumeFace::generateNormals(F32 crease_angle, F32 smoothing)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if (!mPositions || !mIndices || mNumVertices <= 0 || mNumIndices < 3 || mNumIndices % 3 != 0)
    {
        return false;
    }

    const size_t index_count = mNumIndices;
    const size_t vertex_count = mNumVertices;
    std::vector<U32> indices(index_count);
    for (size_t i = 0; i < index_count; ++i)
    {
        indices[i] = mIndices[i];
        if (indices[i] >= vertex_count)
        {
            LL_WARNS_ONCE("LLVolume") << "Out of bounds index detected: mIndices[" << i << "]=" << indices[i]
                << " >= mNumVertices=" << mNumVertices << LL_ENDL;
            return false;
        }
    }

    const LLVector4a scale(mNormalizedScale.mV[0] != 0.f ? mNormalizedScale.mV[0] : 1.f,
                           mNormalizedScale.mV[1] != 0.f ? mNormalizedScale.mV[1] : 1.f,
                           mNormalizedScale.mV[2] != 0.f ? mNormalizedScale.mV[2] : 1.f, 1.f);
    std::vector<LLVector4a> positions(vertex_count);
    for (size_t v = 0; v < vertex_count; ++v)
    {
        positions[v].setMul(mPositions[v], scale);
    }

    std::vector<F32> generated(index_count * 3);
    meshopt_generateNormals(generated.data(), indices.data(), index_count, positions[0].getF32ptr(), vertex_count, sizeof(LLVector4a),
                            llclamp(crease_angle, 0.f, F_PI), llmax(smoothing, 0.f));

    // Each corner's normal in the face's frame, and for each vertex the
    // first of its corners to have one.
    std::vector<LLVector4a> corner_normals(index_count);
    std::vector<U32> first_corner(vertex_count, U32_MAX);
    for (size_t c = 0; c < index_count; ++c)
    {
        LLVector4a& normal = corner_normals[c];
        normal.set(generated[c * 3 + 0], generated[c * 3 + 1], generated[c * 3 + 2], 0.f);
        normal.mul(scale);
        unit3_or_zero(normal);
        normal.getF32ptr()[3] = 0.f;
        if (first_corner[indices[c]] == U32_MAX && normal.dot3(normal).getF32() > 0.f)
        {
            first_corner[indices[c]] = (U32)c;
        }
    }
    for (size_t c = 0; c < index_count; ++c)
    {
        LLVector4a& normal = corner_normals[c];
        if (normal.dot3(normal).getF32() > 0.f)
        {
            continue;
        }
        const U32 v = indices[c];
        if (first_corner[v] != U32_MAX)
        {
            normal = corner_normals[first_corner[v]];
            continue;
        }
        if (mNormals)
        {
            normal.set(mNormals[v][0], mNormals[v][1], mNormals[v][2], 0.f);
            unit3_or_zero(normal);
        }
        if (normal.dot3(normal).getF32() == 0.f)
        {
            normal.set(0.f, 0.f, 1.f, 0.f);
        }
    }

    // A vertex the face holds more than once, the same in all but its
    // normal, is one vertex; then each corner is that vertex with its own
    // normal, and corners the same in both are one.
    std::vector<U32> canonical(vertex_count);
    const meshopt_Stream attributes[] = {
        { mPositions, sizeof(F32) * 3, sizeof(LLVector4a) },
        { mTexCoords, sizeof(LLVector2), sizeof(LLVector2) },
        { mWeights, sizeof(LLVector4a), sizeof(LLVector4a) },
    };
    meshopt_Stream used[3];
    size_t used_count = 0;
    for (const meshopt_Stream& stream : attributes)
    {
        if (stream.data)
        {
            used[used_count++] = stream;
        }
    }
    meshopt_generateVertexRemapMulti(canonical.data(), indices.data(), index_count, vertex_count, used, used_count);
    std::vector<U32> corner_vertices(index_count);
    for (size_t c = 0; c < index_count; ++c)
    {
        corner_vertices[c] = canonical[indices[c]];
    }
    std::vector<U32> remap(index_count);
    const meshopt_Stream corners[] = {
        { corner_vertices.data(), sizeof(U32), sizeof(U32) },
        { corner_normals.data(), sizeof(F32) * 3, sizeof(LLVector4a) },
    };
    const size_t unique =
        meshopt_generateVertexRemapMulti(remap.data(), nullptr, index_count, index_count, corners, sizeof(corners) / sizeof(corners[0]));
    if (unique > U16_MAX)
    {
        LL_WARNS("LLVolume") << "Normals for a face of " << mNumVertices << " vertices and " << index_count / 3
            << " triangles take more vertices than 16-bit indices reach; keeping its own" << LL_ENDL;
        return false;
    }

    // Each new vertex from the first corner that names it.
    std::vector<U32> from_corner(unique);
    for (size_t c = index_count; c-- > 0;)
    {
        from_corner[remap[c]] = (U32)c;
    }

    LLVector4a* old_positions = mPositions;
    const LLVector2* old_tex_coords = mTexCoords;
    LLVector4a* old_weights = mWeights;
    mPositions = nullptr;
    mWeights = nullptr;
    resizeVertices((S32)unique);
    if (!mPositions)
    {
        LLError::LLUserWarningMsg::showOutOfMemory();
        LL_ERRS("LLVolume") << "Failed to allocate memory for resizeVertices(" << unique << ")" << LL_ENDL;
    }
    if (old_weights)
    {
        allocateWeights((S32)unique);
    }
    for (size_t u = 0; u < unique; ++u)
    {
        const U32 corner = from_corner[u];
        const U32 from = indices[corner];
        mPositions[u] = old_positions[from];
        mNormals[u] = corner_normals[corner];
        if (old_tex_coords)
        {
            mTexCoords[u] = old_tex_coords[from];
        }
        else
        {
            mTexCoords[u].setZero();
        }
        if (old_weights)
        {
            mWeights[u] = old_weights[from];
        }
    }
    for (size_t i = 0; i < index_count; ++i)
    {
        mIndices[i] = (U16)remap[i];
    }
    if (!old_tex_coords)
    {
        mTexCoords = nullptr;
    }
    mOptimized = false;

    ll_aligned_free<64>(old_positions);
    ll_aligned_free_16(old_weights);
    return true;
}

void LLVolumeFace::createOctree(F32 scaler, const LLVector4a& center, const LLVector4a& size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if (getOctree())
    {
        return;
    }

    llassert(mNumIndices % 3 == 0);

    mOctree = new LLVolumeOctree(center, size);
    const U32 num_triangles = mNumIndices / 3;
    // Initialize all the triangles we need
    mOctreeTriangles = new LLVolumeTriangle[num_triangles];

    for (U32 triangle_index = 0; triangle_index < num_triangles; ++triangle_index)
    { //for each triangle
        const U32 index = triangle_index * 3;
        LLVolumeTriangle* tri = &mOctreeTriangles[triangle_index];

        const LLVector4a& v0 = mPositions[mIndices[index]];
        const LLVector4a& v1 = mPositions[mIndices[index + 1]];
        const LLVector4a& v2 = mPositions[mIndices[index + 2]];

        //store pointers to vertex data
        tri->mV[0] = &v0;
        tri->mV[1] = &v1;
        tri->mV[2] = &v2;

        //store indices
        tri->mIndex[0] = mIndices[index];
        tri->mIndex[1] = mIndices[index + 1];
        tri->mIndex[2] = mIndices[index + 2];

        //get minimum point
        LLVector4a min = v0;
        min.setMin(min, v1);
        min.setMin(min, v2);

        //get maximum point
        LLVector4a max = v0;
        max.setMax(max, v1);
        max.setMax(max, v2);

        //compute center
        LLVector4a center;
        center.setAdd(min, max);
        center.mul(0.5f);

        tri->mPositionGroup = center;

        //compute "radius"
        LLVector4a size;
        size.setSub(max,min);

        tri->mRadius = size.getLength3().getF32() * scaler;

        //insert
        mOctree->insert(tri);
    }

    //remove unneeded octree layers
    while (!mOctree->balance()) { }

    //calculate AABB for each node
    LLVolumeOctreeRebound rebound;
    rebound.traverse(mOctree);

    if (gDebugGL)
    {
        LLVolumeOctreeValidate validate;
        validate.traverse(mOctree);
    }
}

void LLVolumeFace::destroyOctree()
{
    delete mOctree;
    mOctree = nullptr;
    delete[] mOctreeTriangles;
    mOctreeTriangles = nullptr;
}

const LLVolumeOctree* LLVolumeFace::getOctree() const
{
    return mOctree;
}

LLVector4a LLVolumeFace::makePlanarNormal() const
{
    LLVector4a planar(0.f, 0.f, 0.f, -1.f);
    bool keep = true;
    if (mPositions && mNormals && mNumVertices > 0)
    {
        // A triangle two of whose corners are one place has no area, which
        // a fused multiply-add would leave as the rounding of its cross
        // product.
        auto same_place = [](const LLVector4a& p, const LLVector4a& q)
        {
            return std::memcmp(p.getF32ptr(), q.getF32ptr(), 3 * sizeof(F32)) == 0;
        };
        LLVector4a sum;
        sum.clear();
        for (S32 i = 0; i + 2 < mNumIndices; i += 3)
        {
            const LLVector4a& a = mPositions[mIndices[i]];
            const LLVector4a& b = mPositions[mIndices[i + 1]];
            const LLVector4a& c = mPositions[mIndices[i + 2]];
            if (same_place(a, b) || same_place(b, c) || same_place(a, c))
            {
                continue;
            }
            LLVector4a ab, ac, area;
            ab.setSub(b, a);
            ac.setSub(c, a);
            area.setCross3(ab, ac);
            sum.add(area);
        }
        const F32 length2 = sum.dot3(sum).getF32();

        // A face with no area keeps nothing, and is made again at the next
        // call: a flexible prim's sides have none before its first pose, and
        // the one normal they have then is no direction of theirs.
        keep = length2 > 0.f && std::isfinite(length2);

        auto same_normal = [&](S32 v) { return std::memcmp(mNormals[v].getF32ptr(), mNormals[0].getF32ptr(), 3 * sizeof(F32)) == 0; };
        bool one_normal = true;
        for (S32 v = 1; v < mNumVertices && one_normal; ++v)
        {
            one_normal = same_normal(v);
        }

        if (one_normal)
        {
            planar = mNormals[0];
            planar.getF32ptr()[3] = 1.f;
        }
        else if (keep)
        {
            sum.mul(1.f / sqrtf(length2));
            constexpr F32 cone = PLANAR_CONE_COS + PLANAR_CONE_TIE;
            bool within = true;
            for (S32 v = 0; v < mNumVertices && within; ++v)
            {
                const F32 along = mNormals[v].dot3(sum).getF32();
                within = along > 0.f && along * along >= cone * cone * mNormals[v].dot3(mNormals[v]).getF32();
            }
            planar = sum;
            planar.getF32ptr()[3] = within ? 1.f : -1.f;
        }
    }

    if (keep)
    {
        std::memcpy(mPlanarNormal, planar.getF32ptr(), sizeof(mPlanarNormal));
    }
    return planar;
}

void LLVolumeFace::getPlanarBasis(const LLVector4a& normal, F32 switch_x, LLVector4a& binormal, LLVector4a& tangent)
{
    const F32 d = normal[0];
    if (d >= switch_x || d <= -switch_x)
    {
        binormal.set(0.f, d < 0.f ? -1.f : 1.f, 0.f);
    }
    else
    {
        binormal.set(normal[1] > 0.f ? -1.f : 1.f, 0.f, 0.f);
    }
    tangent.setCross3(binormal, normal);
}

void LLVolumeFace::getPlanarTexCoords(const LLVector4a& scale, const alsimd::TexTransform* transform, F32* dst) const
{
    const LLVector4a planar = getPlanarNormal();
    if (planar[3] > 0.f)
    {
        LLVector4a binormal, tangent;
        getPlanarBasis(planar, PLANAR_SWITCH_X, binormal, tangent);
        alsimd::planar_texcoords(mPositions, mNumVertices, scale, binormal, tangent, transform, dst);
    }
    else
    {
        alsimd::planar_texcoords(mPositions, mNormals, mNumVertices, scale, PLANAR_SWITCH_X - PLANAR_TIE, transform, dst);
    }
}

LLVector2 LLVolumeFace::getPlanarTexCoord(const LLVector4a& position, const LLVector4a& normal, const alsimd::TexTransform* transform) const
{
    const LLVector4a planar = getPlanarNormal();
    const LLVector4a unscaled(1.f);
    LLVector2 tc;
    if (planar[3] > 0.f)
    {
        LLVector4a binormal, tangent;
        getPlanarBasis(planar, PLANAR_SWITCH_X, binormal, tangent);
        alsimd::planar_texcoords(&position, 1, unscaled, binormal, tangent, transform, tc.mV);
    }
    else
    {
        alsimd::planar_texcoords(&position, &normal, 1, unscaled, PLANAR_SWITCH_X - PLANAR_TIE, transform, tc.mV);
    }
    return tc;
}

void LLVolumeFace::getPlanarTangents(const LLVector4a& scale, LLVector4a* dst) const
{
    const LLVector4a planar = getPlanarNormal();
    if (planar[3] > 0.f)
    {
        LLVector4a binormal, tangent;
        getPlanarBasis(planar, PLANAR_SWITCH_X, binormal, tangent);
        alsimd::planar_tangents(mNormals, mNumVertices, scale, binormal, tangent, dst);
    }
    else
    {
        alsimd::planar_tangents(mNormals, mNumVertices, scale, PLANAR_SWITCH_X - PLANAR_TIE, dst);
    }
}

LLVector4a LLVolumeFace::getPlanarTangent(const LLVector4a& normal, const LLVector4a& scale) const
{
    const LLVector4a planar = getPlanarNormal();
    LLVector4a out;
    if (planar[3] > 0.f)
    {
        LLVector4a binormal, tangent;
        getPlanarBasis(planar, PLANAR_SWITCH_X, binormal, tangent);
        alsimd::planar_tangents(&normal, 1, scale, binormal, tangent, &out);
    }
    else
    {
        alsimd::planar_tangents(&normal, 1, scale, PLANAR_SWITCH_X - PLANAR_TIE, &out);
    }
    return out;
}

namespace
{
    // Points closer than this are one place: far above the rounding that a
    // closed loop carries from its first point around to its last, far below
    // the step between any two points the generator means to be apart.
    constexpr F32 SAME_PLACE = 1e-5f;

    bool same_place(const LLVector4a& a, const LLVector4a& b)
    {
        LLVector4a d;
        d.setSub(a, b);
        return d.dot3(d).getF32() <= SAME_PLACE * SAME_PLACE;
    }

    // Whether every row of a grid has the same place in column a as in
    // column b.
    bool columns_meet(const LLVector4a* pos, S32 num_s, S32 num_t, S32 a, S32 b)
    {
        for (S32 t = 0; t < num_t; ++t)
        {
            if (!same_place(pos[num_s * t + a], pos[num_s * t + b]))
            {
                return false;
            }
        }
        return true;
    }

    // Whether every column of a grid has the same place in row a as in row b.
    bool rows_meet(const LLVector4a* pos, S32 num_s, S32 a, S32 b)
    {
        for (S32 s = 0; s < num_s; ++s)
        {
            if (!same_place(pos[num_s * a + s], pos[num_s * b + s]))
            {
                return false;
            }
        }
        return true;
    }

    // Whether a column of a grid is one place in every row: a pole.
    bool column_is_point(const LLVector4a* pos, S32 num_s, S32 num_t, S32 s)
    {
        for (S32 t = 1; t < num_t; ++t)
        {
            if (!same_place(pos[s], pos[num_s * t + s]))
            {
                return false;
            }
        }
        return true;
    }

    // An edge between two points that are one place but for rounding, as a
    // closed profile's last point and its first, has no length.
    inline void drop_rounding(LLVector4a& edge)
    {
        if (edge.equals3(LLVector4a::getZero(), SAME_PLACE))
        {
            edge.clear();
        }
    }

    // Adds a grid quad, given by its edges, to its corners' normals. Its two
    // triangles, (bl, br, tr) and (bl, tr, tl), share the diagonal from bl to
    // tr: each gives its corner off the diagonal its cross product, twice its
    // area along its normal, and each corner on the diagonal half of it. A
    // parallelogram then gives its four corners one weight, so a vertex has
    // as much from either side of it in every row, and no corner takes a
    // normal from a triangle it is not in. Each cross product is taken at the
    // corner off the diagonal: where a copied or collapsed row or column puts
    // two of a triangle's corners in one place, an edge there has no length
    // and the product is exactly zero, fused multiply-add or not. A quad
    // between a copied row or column adds nothing, so the edge there stays
    // hard. A quad whose diagonal has no length, as a sculpt can fold one,
    // has no area on either side of it and adds nothing either: the edges
    // at its corners off the diagonal are each other's reverse, whose
    // product a fused multiply-add leaves as the rounding of one of its
    // terms.
    inline void add_quad_to_normals(LLVector4a* norm, S32 bl, S32 br, S32 tl, S32 tr, const LLVector4a& bottom, const LLVector4a& right,
                                    const LLVector4a& top, const LLVector4a& left, const LLVector4a& diagonal)
    {
        if (diagonal.equals3(LLVector4a::getZero(), SAME_PLACE))
        {
            return;
        }

        LLVector4a lower, upper;
        lower.setCross3(bottom, right);
        upper.setCross3(top, left);

        LLVector4a shared;
        shared.setAdd(lower, upper);
        shared.mul(0.5f);

        norm[bl].add(shared);
        norm[br].add(lower);
        norm[tr].add(shared);
        norm[tl].add(upper);
    }

    // Gives every vertex of a grid marked as having no normal of its own,
    // (0, 0, 0, 1), a normal from one that has, whose w is 0. It takes the
    // nearest one up or down its column; where its whole column has none,
    // the nearest along its row; where its row has none either, as where a
    // copied column crosses a copied row, one of the nearest around it, in
    // rings of the grid. It takes only a normal a vertex has of its own, so
    // that none is passed on twice. A grid with no normal at all faces +Z.
    // Such a vertex keeps a w of 1 until it has one, and has 2 while it
    // waits for the search around.
    void fill_marked_normals(LLVector4a* norm, S32 num_s, S32 num_t)
    {
        const S32 count = num_s * num_t;
        auto own = [](const LLVector4a& n) { return n[3] == 0.f; };
        if (std::none_of(norm, norm + count, own))
        {
            for (S32 v = 0; v < count; ++v)
            {
                norm[v].set(0.f, 0.f, 1.f, 0.f);
            }
            return;
        }

        bool around = false;
        for (S32 t = 0; t < num_t; ++t)
        {
            for (S32 s = 0; s < num_s; ++s)
            {
                LLVector4a& n = norm[s + num_s * t];
                if (own(n))
                {
                    continue;
                }
                const LLVector4a* donor = nullptr;
                for (S32 d = 1; d < num_t && !donor; ++d)
                {
                    for (S32 row : { t - d, t + d })
                    {
                        if (row >= 0 && row < num_t && own(norm[s + num_s * row]))
                        {
                            donor = &norm[s + num_s * row];
                            break;
                        }
                    }
                }
                for (S32 d = 1; d < num_s && !donor; ++d)
                {
                    for (S32 column : { s - d, s + d })
                    {
                        if (column >= 0 && column < num_s && own(norm[column + num_s * t]))
                        {
                            donor = &norm[column + num_s * t];
                            break;
                        }
                    }
                }
                if (donor)
                {
                    n = *donor;
                    n.getF32ptr()[3] = 1.f;
                }
                else
                {
                    n.getF32ptr()[3] = 2.f;
                    around = true;
                }
            }
        }

        if (around)
        {
            // Every vertex's nearest normal of its own in rings, found
            // outward from all of them at once a ring at a time: a step to
            // any of the eight vertices around one is one ring further.
            ALScratchArena::Scope scratch;
            U32* source = scratch.allocate<U32>(count);
            U32* queue = scratch.allocate<U32>(count);
            size_t head = 0;
            size_t tail = 0;
            for (S32 v = 0; v < count; ++v)
            {
                source[v] = U32_MAX;
                if (own(norm[v]))
                {
                    source[v] = (U32)v;
                    queue[tail++] = (U32)v;
                }
            }
            while (head < tail)
            {
                const U32 v = queue[head++];
                const S32 s = (S32)v % num_s;
                const S32 t = (S32)v / num_s;
                for (S32 row = llmax(t - 1, 0); row <= llmin(t + 1, num_t - 1); ++row)
                {
                    for (S32 column = llmax(s - 1, 0); column <= llmin(s + 1, num_s - 1); ++column)
                    {
                        const S32 u = column + num_s * row;
                        if (source[u] == U32_MAX)
                        {
                            source[u] = source[v];
                            queue[tail++] = (U32)u;
                        }
                    }
                }
            }

            for (S32 v = 0; v < count; ++v)
            {
                if (norm[v][3] == 2.f)
                {
                    norm[v] = norm[source[v]];
                    norm[v].getF32ptr()[3] = 1.f;
                }
            }
        }

        for (S32 v = 0; v < count; ++v)
        {
            norm[v].getF32ptr()[3] = 0.f;
        }
    }

    // Brings every normal of a grid to unit length. A vertex whose quads all
    // have no area, as a copy that closes a cut flat inner side or a row in
    // the middle of a sculpt's pinch, has no normal of its own, and takes a
    // neighbour's.
    void normalize_grid_normals(LLVector4a* norm, S32 num_s, S32 num_t)
    {
        if (alsimd::normalize3_or_mark(norm, num_s * num_t))
        {
            fill_marked_normals(norm, num_s, num_t);
        }
    }

    // A side's column: the profile point it is laid out from, and its s.
    struct SideColumn
    {
        S32 mesh;
        F32 s;
    };

    // How far into its face a side takes a row the path's scale narrows to a
    // line or a point, in the path's t: far enough that the profile's
    // direction it is narrowed across outweighs rounding, near enough to
    // leave every other column's normal as it is to a hundredth of a degree.
    constexpr F32 NARROWED_ROW_STEP = 1e-4f;

    // A profile tangent's component this small a part of the tangent is the
    // rounding of a point on an axis, as sin(2 pi) leaves one: nothing.
    constexpr F32 TANGENT_ROUNDING = 1e-6f;

    // A pole's step along the profile this small a part of what its terms
    // could make is their rounding: the face lies along the axis.
    constexpr F32 POLE_STEP_ROUNDING = 1e-4f;

    // A prim side's normals from the surface it is cut from, its profile
    // swept along its path: at each vertex, the surface's derivative along
    // the profile, M p', crossed with its derivative along the path,
    // M' p + v, where M takes the profile's x and y at the vertex's station,
    // and M' and v are how fast M and the station's position change. A
    // round side's columns take the profile's tangents, a cut point's at the
    // cut, so they do not depend on the LOD; a flat side's take the direction
    // of the straight edge each lies on, a corner's copy the next edge's.
    // Both derivatives run the way the grid's indices do, so each normal
    // faces the way its triangles do, an inner side's into the hole.
    //
    // Where the product vanishes, the side is taken a step into its face, as
    // the limit there. A row the path's scale narrows to a line or a point,
    // at an apex, is taken NARROWED_ROW_STEP along the path. A column whose
    // point the path does not move, a sphere's pole, has no M' p + v, and
    // takes the step along the profile, M' p', one way from its first
    // column and the other from its last. A vertex that is both, one whose
    // edge has no length, and one whose two derivatives lie along one line,
    // as where a twist carries a point on the axis along the profile, takes
    // a neighbour's normal.
    void swept_side_normals(const LLVolume& volume, const SideColumn* columns, S32 num_s, S32 begin_t, S32 num_t, bool flat, const LLVector4a* pos,
                            LLVector4a* norm)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

        const LLProfile& outline = volume.getProfile();
        const LLVector4a* const profile = outline.mProfile.data();
        const LLPath& path = volume.getPath();
        const LLPath::PathPt* const stations = path.mPath.data();

        // The rows' frames, each in its station.
        static_assert(sizeof(LLPath::PathFrame) == 5 * sizeof(LLVector4a), "a frame is five vectors");
        static_assert(sizeof(LLPath::PathPt) % sizeof(LLVector4a) == 0, "stations are a whole number of vectors apart");
        constexpr size_t STRIDE = sizeof(LLPath::PathPt) / sizeof(LLVector4a);
        const LLVector4a* const frames = stations[begin_t].mFrame.mRow;

        // A first or last row the path's scale narrows to a line or a point
        // takes its frame a step into the face.
        LLVector4a stepped[2][5];
        S32 narrowed[2];
        S32 narrowed_count = 0;
        const S32 last_station = (S32)path.mPath.size() - 1;
        for (S32 t = 0; t < num_t; t = t == 0 && num_t > 1 ? num_t - 1 : num_t)
        {
            const S32 station = begin_t + t;
            const LLVector4a& scale = stations[station].mScale;
            if ((station == 0 || station == last_station) && (scale[0] == 0.f || scale[1] == 0.f))
            {
                LLVector4a* frame = stepped[narrowed_count];
                std::copy(frames + STRIDE * t, frames + STRIDE * t + 5, frame);
                const F32 step = station == 0 ? NARROWED_ROW_STEP : -NARROWED_ROW_STEP;
                for (S32 i = 0; i < 2; ++i)
                {
                    LLVector4a along = frame[2 + i];
                    along.mul(step);
                    frame[i].add(along);
                }
                narrowed[narrowed_count++] = t;
            }
        }
        auto narrowed_frame = [&](S32 t) -> const LLVector4a*
        {
            for (S32 r = 0; r < narrowed_count; ++r)
            {
                if (narrowed[r] == t)
                {
                    return stepped[r];
                }
            }
            return nullptr;
        };

        // A tangent's component that is only the rounding of a point on an
        // axis goes; an edge of no length has none.
        auto clean = [](F32& x, F32& y)
        {
            const F32 size = llmax(fabsf(x), fabsf(y));
            x = fabsf(x) <= TANGENT_ROUNDING * size ? 0.f : x;
            y = fabsf(y) <= TANGENT_ROUNDING * size ? 0.f : y;
        };
        // A flat side's run of columns between a corner's copies lies on one
        // edge, from its first column to its last.
        auto edge_of = [&](S32 first, S32 last, F32& x, F32& y)
        {
            x = profile[columns[last].mesh][0] - profile[columns[first].mesh][0];
            y = profile[columns[last].mesh][1] - profile[columns[first].mesh][1];
            if (fabsf(x) <= SAME_PLACE && fabsf(y) <= SAME_PLACE)
            {
                x = y = 0.f;
            }
            clean(x, y);
        };

        // Where every row is the first turned as its station turns from the
        // first's, as along a straight path that neither scales nor shears,
        // so are its normals, n R0^T Rt; where none turns, they are copies.
        const bool repeat = path.framesRepeat();
        const bool turn = path.framesTurn() && narrowed_count == 0;
        auto rows_from_first = [&](bool marked)
        {
            if (repeat)
            {
                for (S32 t = 1; t < num_t; ++t)
                {
                    std::memcpy(norm + num_s * t, norm, num_s * sizeof(LLVector4a));
                }
                return;
            }
            // Each column's first normal in the first station's own frame,
            // then out through each other row's. Their w, as every row's,
            // is 0, and a vertex marked in the first row is marked in all.
            LLMatrix4a back = stations[begin_t].mRot;
            back.transpose();
            for (S32 k = 0; k < num_s; ++k)
            {
                LLVector4a own;
                back.rotate(norm[k], own);
                for (S32 t = 1; t < num_t; ++t)
                {
                    stations[begin_t + t].mRot.rotate(own, norm[k + num_s * t]);
                }
            }
            for (S32 k = 0; k < num_s && marked; ++k)
            {
                if (norm[k][3] != 0.f)
                {
                    for (S32 t = 1; t < num_t; ++t)
                    {
                        norm[k + num_s * t] = norm[k];
                    }
                }
            }
        };

        // A flat side there takes its first row a column at a time: its
        // edges are few, and the columns along each fewer. Where nothing
        // along the path turns or scales, an edge's columns are one normal.
        if (flat && turn)
        {
            const LLVector4a& velocity = frames[4];
            const F32 velocity2 = velocity.dot3(velocity).getF32();
            const bool still = repeat && frames[2].dot3(frames[2]).getF32() == 0.f && frames[3].dot3(frames[3]).getF32() == 0.f;
            bool marked = false;
            S32 first = 0;
            for (S32 k = 1; k <= num_s; ++k)
            {
                if (k < num_s && columns[k].mesh != columns[k - 1].mesh)
                {
                    continue;
                }
                F32 edge_x, edge_y;
                edge_of(first, k - 1, edge_x, edge_y);
                LLVector4a across = frames[0];
                across.mul(edge_x);
                LLVector4a second = frames[1];
                second.mul(edge_y);
                across.add(second);
                const F32 across2 = across.dot3(across).getF32();
                for (S32 j = first; j < (still ? first + 1 : k); ++j)
                {
                    LLVector4a along = velocity;
                    F32 along2 = velocity2;
                    if (!still)
                    {
                        const LLVector4a& point = profile[columns[j].mesh];
                        LLVector4a rate = frames[2];
                        rate.mul(point[0]);
                        along.add(rate);
                        rate = frames[3];
                        rate.mul(point[1]);
                        along.add(rate);
                        along2 = along.dot3(along).getF32();
                    }
                    LLVector4a& n = norm[j];
                    n.setCross3(across, along);
                    const F32 length2 = n.dot3(n).getF32();
                    if (length2 <= llmax(alsimd::SWEPT_PARALLEL * alsimd::SWEPT_PARALLEL * across2 * along2, FLT_MIN))
                    {
                        n.set(0.f, 0.f, 0.f, 1.f);
                        marked = true;
                    }
                    else
                    {
                        n.normalize3();
                    }
                }
                if (still)
                {
                    std::fill(norm + first + 1, norm + k, norm[first]);
                }
                first = k;
            }
            rows_from_first(marked);
            if (marked)
            {
                fill_marked_normals(norm, num_s, num_t);
            }
            return;
        }

        // Each column's point, tangent and share of the station's velocity,
        // as lanes padded to whole registers. A side of the size most are
        // takes them from the stack.
        constexpr size_t STACK_LANES = 64;
        alignas(32) F32 stack_lanes[5 * STACK_LANES];
        S32 stack_poles[STACK_LANES];
        ALScratchArena::Scope scratch;
        const size_t padded = ((size_t)num_s + 7) & ~(size_t)7;
        F32* const lanes = padded <= STACK_LANES ? stack_lanes : scratch.allocate<F32>(5 * padded);
        S32* const poles = padded <= STACK_LANES ? stack_poles : scratch.allocate<S32>(num_s);
        F32* const px = lanes;
        F32* const py = px + padded;
        F32* const tx = py + padded;
        F32* const ty = tx + padded;
        F32* const moves = ty + padded;
        for (S32 k = 0; k < num_s; ++k)
        {
            const LLVector4a& point = profile[columns[k].mesh];
            px[k] = point[0];
            py[k] = point[1];
            moves[k] = 1.f;
        }
        for (F32* lane : { px, py, tx, ty, moves })
        {
            std::fill(lane + num_s, lane + padded, 0.f);
        }
        if (flat)
        {
            S32 first = 0;
            for (S32 k = 1; k <= num_s; ++k)
            {
                if (k < num_s && columns[k].mesh != columns[k - 1].mesh)
                {
                    continue;
                }
                F32 edge_x, edge_y;
                edge_of(first, k - 1, edge_x, edge_y);
                std::fill(tx + first, tx + k, edge_x);
                std::fill(ty + first, ty + k, edge_y);
                first = k;
            }
        }
        else
        {
            for (S32 k = 0; k < num_s; ++k)
            {
                const LLVector4a tangent = outline.getTangent(columns[k].mesh);
                tx[k] = tangent[0];
                ty[k] = tangent[1];
                clean(tx[k], ty[k]);
            }
        }

        // A line moves every point along it, and leaves none in place.
        S32 pole_count = 0;
        if (volume.getPathType() != LL_PCODE_PATH_LINE)
        {
            for (S32 k = 0; k < num_s; ++k)
            {
                if (column_is_point(pos, num_s, num_t, k))
                {
                    const F32 into = k + 1 < num_s ? 1.f : -1.f;
                    px[k] = into * tx[k];
                    py[k] = into * ty[k];
                    moves[k] = 0.f;
                    poles[pole_count++] = k;
                }
            }
        }

        const F32* const column_lanes[5] = { px, py, tx, ty, moves };
        bool marked = false;
        if (turn)
        {
            marked = alsimd::swept_normals(frames, STRIDE, 1, column_lanes, num_s, norm);
            rows_from_first(marked);
        }
        else
        {
            // The rows between the narrowed ones in one run.
            for (S32 t = 0; t < num_t;)
            {
                if (const LLVector4a* frame = narrowed_frame(t))
                {
                    marked = alsimd::swept_normals(frame, 5, 1, column_lanes, num_s, norm + num_s * t) || marked;
                    ++t;
                    continue;
                }
                S32 end = t + 1;
                while (end < num_t && !narrowed_frame(end))
                {
                    ++end;
                }
                marked = alsimd::swept_normals(frames + STRIDE * t, STRIDE, end - t, column_lanes, num_s, norm + num_s * t) || marked;
                t = end;
            }
        }

        auto mark = [&](S32 k, S32 t)
        {
            norm[k + num_s * t].set(0.f, 0.f, 0.f, 1.f);
            marked = true;
        };
        // A pole's step that leaves only the rounding of its terms finds no
        // width there either: a flat face lies along the axis its pole is
        // on. A round face's tangent at a pole is square to its radius, so
        // the turn about the axis always moves it.
        for (S32 t = 0; t < num_t && pole_count > 0 && flat; ++t)
        {
            const LLVector4a& rate0 = frames[STRIDE * t + 2];
            const LLVector4a& rate1 = frames[STRIDE * t + 3];
            const F32 rates2 = rate0.dot3(rate0).getF32() + rate1.dot3(rate1).getF32();
            for (S32 c = 0; c < pole_count; ++c)
            {
                const S32 k = poles[c];
                LLVector4a step = rate0;
                step.mul(px[k]);
                LLVector4a second = rate1;
                second.mul(py[k]);
                step.add(second);
                const F32 terms = (px[k] * px[k] + py[k] * py[k]) * rates2;
                if (step.dot3(step).getF32() <= POLE_STEP_ROUNDING * POLE_STEP_ROUNDING * terms)
                {
                    mark(k, t);
                }
            }
        }
        // Where a pole meets an apex, both derivatives are steps into the
        // face, and the two steps are along one line.
        for (S32 r = 0; r < narrowed_count; ++r)
        {
            for (S32 c = 0; c < pole_count; ++c)
            {
                mark(poles[c], narrowed[r]);
            }
        }
        if (marked)
        {
            fill_marked_normals(norm, num_s, num_t);
        }
    }

    // Twice the area, in profile units, within which a hollow cap's strip
    // takes a triangle for one of no area where its hole touches its wall.
    constexpr F32 CAP_ZERO_AREA = 1e-6f;

    // Whether a hollow cap's strip takes (1, a, 2) next, a step along its
    // outer chain, rather than (2, 1, b), a step along its inner one. A
    // triangle that faces the wrong way will not do, nor one that contains
    // the other chain's next point; of two that will, the one whose new edge
    // is shorter, which outer_shorter says where it is asked. Areas within the
    // tolerance count as none, and so, with a tolerance, a triangle of no
    // area contains nothing.
    template <class OuterShorter>
    bool cap_strip_takes_outer(F32 area_1a2, F32 area_2ab, F32 area_21b, F32 area_1ba, OuterShorter outer_shorter, F32 tolerance)
    {
        bool tri_1a2 = !(area_1a2 < -tolerance);
        bool tri_21b = !(area_21b < -tolerance);
        if (area_2ab < -tolerance && !(tolerance > 0.f && area_1a2 <= tolerance))
        {
            tri_1a2 = false;
        }
        if (area_1ba < -tolerance && !(tolerance > 0.f && area_21b <= tolerance))
        {
            tri_21b = false;
        }
        if (!tri_1a2)
        {
            return false;
        }
        if (!tri_21b)
        {
            return true;
        }
        return outer_shorter();
    }

    // A side's rows and columns that its parameters take to one point at
    // every station: the first or last row where the path tapers to nothing
    // at an end it reaches, and the first or last column where a half circle
    // ends on the axis a circle path turns it about and the path keeps it
    // there, a sphere's poles. A half circle's ends lie on that axis where
    // no cut moves them; a circle of no radius, a hole size of 1 in y, with
    // no skew and no taper along the axis keeps them there, and a twist
    // keeps them only by whole half turns. An inner side runs the other way
    // along its profile.
    struct Collapse
    {
        bool first_row = false;
        bool last_row = false;
        bool first_column = false;
        bool last_column = false;
    };

    Collapse collapse_of(const LLVolume* volume, bool inner)
    {
        Collapse collapse;
        const LLPathParams& path = volume->getParams().getPathParams();
        const U8 curve = path.getCurveType();
        if (curve == LL_PCODE_PATH_LINE)
        {
            // A line's scale of 2 narrows its beginning to nothing, as
            // LLPathParams::getBeginScale has it, and one of 0 its end.
            const F32 scale_x = path.getScaleX();
            const F32 scale_y = path.getScaleY();
            collapse.first_row = scale_x == 2.f && scale_y == 2.f && path.getBegin() == 0.f;
            collapse.last_row = scale_x == 0.f && scale_y == 0.f && path.getEnd() == 1.f;
            return collapse;
        }
        if (curve != LL_PCODE_PATH_CIRCLE && curve != LL_PCODE_PATH_CIRCLE2)
        {
            return collapse;
        }

        // LLPath::genNGon tapers the end by a positive taper and the
        // beginning by a negative one.
        collapse.first_row = path.getTaperX() == -1.f && path.getTaperY() == -1.f && path.getBegin() == 0.f;
        collapse.last_row = path.getTaperX() == 1.f && path.getTaperY() == 1.f && path.getEnd() == 1.f;

        const LLProfileParams& profile = volume->getParams().getProfileParams();
        const F32 half_turns = 2.f * path.getTwist();
        if (curve == LL_PCODE_PATH_CIRCLE && (profile.getCurveType() & LL_PCODE_PROFILE_MASK) == LL_PCODE_PROFILE_CIRCLE_HALF &&
            path.getScaleY() == 1.f && path.getSkew() == 0.f && path.getTaperX() == 0.f && path.getTwistBegin() == path.getTwist() &&
            half_turns == floorf(half_turns))
        {
            const bool begin_pole = profile.getBegin() == 0.f;
            const bool end_pole = profile.getEnd() == 1.f;
            collapse.first_column = inner ? end_pole : begin_pole;
            collapse.last_column = inner ? begin_pole : end_pole;
        }
        return collapse;
    }

    // A side's quads go in an order a 16-entry vertex cache reuses. A grid
    // of up to COLUMN_ROWS rows goes a column at a time, every row of a
    // column before the next: a column's right edge is still cached when the
    // next column takes it as its left. A taller grid of up to LONE_BAND_QUADS
    // quads across goes row by row; a wider one in bands of at most
    // BAND_QUADS quads across, as near equal as the quads divide, each band
    // row by row, every other band from the last row back to the first: a
    // band's row reuses the row before it, and a band starts where the band
    // before it ended. That is on a first or last row, where a collapse or a
    // pinch leaves triangles out and loads the row's vertices in another
    // order; two rows of a band take 14 entries, so the band's next row still
    // finds them. The order depends on the grid's layout alone, never on
    // positions, so the indices a full build writes hold for the partial
    // builds after it.
    constexpr S32 COLUMN_ROWS = 7;
    constexpr S32 LONE_BAND_QUADS = 7;
    constexpr S32 BAND_QUADS = 6;

    // What a side keeps of its quads' triangles: all of them; all but those
    // a collapsed row or column leaves out, a collapsed column being the
    // first quad's left or the last quad's right; or all but those flagged
    // as having two corners in one place. whole(t) is whether every
    // triangle of row t is kept. WHOLE_ROWS is whether rows are whole often
    // enough to write a band's row at once: a sphere's collapsed poles
    // leave a triangle out of every row.
    struct KeepAll
    {
        static constexpr bool ALL = true;
        static constexpr bool WHOLE_ROWS = true;

        bool upper(S32, S32) const { return true; }
        bool lower(S32, S32) const { return true; }
        bool whole(S32) const { return true; }
    };

    struct Kept
    {
        static constexpr bool ALL = false;
        static constexpr bool WHOLE_ROWS = false;

        bool first_row;
        bool last_row;
        bool first_column;
        bool last_column;
        S32 last_quad_column;
        S32 last_quad_row;

        bool upper(S32 s, S32 t) const { return !((last_row && t == last_quad_row) || (first_column && s == 0)); }
        bool lower(S32 s, S32 t) const { return !((first_row && t == 0) || (last_column && s == last_quad_column)); }
        bool whole(S32 t) const { return !first_column && !last_column && !(first_row && t == 0) && !(last_row && t == last_quad_row); }
    };

    struct Unpinched
    {
        static constexpr bool ALL = false;
        static constexpr bool WHOLE_ROWS = true;

        // Bit s % 64 of word t * words + s / 64 for the quad at column s
        // and row t.
        const U64* upper_pinched;
        const U64* lower_pinched;
        S32 words;

        bool upper(S32 s, S32 t) const { return !((upper_pinched[t * words + s / 64] >> (s % 64)) & 1); }
        bool lower(S32 s, S32 t) const { return !((lower_pinched[t * words + s / 64] >> (s % 64)) & 1); }
        bool whole(S32 t) const
        {
            U64 any = 0;
            for (S32 w = 0; w < words; ++w)
            {
                any |= upper_pinched[t * words + w] | lower_pinched[t * words + w];
            }
            return any == 0;
        }
    };

    // The quad at column s and row t of a grid of vertices s + num_s * t, as
    // its two triangles, bottom left, top right, top left and bottom left,
    // bottom right, top right, but for those keep leaves out. Returns the end
    // of what it wrote.
    template <class KEEP>
    LL_FORCE_INLINE U16* quad_indices(U16* out, S32 s, S32 t, S32 num_s, const KEEP& keep)
    {
        const S32 bottom = s + num_s * t;
        const S32 top = bottom + num_s;
        if (KEEP::ALL || keep.upper(s, t))
        {
            out[0] = (U16)bottom;       //bottom left
            out[1] = (U16)(top + 1);    //top right
            out[2] = (U16)top;          //top left
            out += 3;
        }
        if (KEEP::ALL || keep.lower(s, t))
        {
            out[0] = (U16)bottom;       //bottom left
            out[1] = (U16)(bottom + 1); //bottom right
            out[2] = (U16)(top + 1);    //top right
            out += 3;
        }
        return out;
    }

    // Whole quads written together, a band's row or a column: each quad's
    // indices, the quad step vertices on from the one before, as offsets
    // from the first quad's bottom left vertex, padded to whole vectors.
    struct QuadUnit
    {
        static constexpr S32 VECTORS = 6;
        static constexpr S32 MAX_QUADS = VECTORS * 8 / 6;

        alignas(16) U16 offsets[VECTORS * 8];
        S32 quads;
        S32 length;
        S32 vectors;

        // quad_count is from 1 to MAX_QUADS.
        QuadUnit(S32 quad_count, S32 step, S32 num_s)
            : quads(quad_count), length(quad_count * 6), vectors((quad_count * 6 + 7) / 8)
        {
            for (S32 q = 0; q < quads; ++q)
            {
                const S32 bottom = q * step;
                const S32 top = bottom + num_s;
                U16* quad = offsets + 6 * q;
                quad[0] = (U16)bottom;       //bottom left
                quad[1] = (U16)(top + 1);    //top right
                quad[2] = (U16)top;          //top left
                quad[3] = (U16)bottom;       //bottom left
                quad[4] = (U16)(bottom + 1); //bottom right
                quad[5] = (U16)(top + 1);    //top right
            }
            std::fill(offsets + length, offsets + vectors * 8, (U16)0);
        }

    };

    // A unit while it is written, its offsets held as values: a store of an
    // index could otherwise be one to the offsets, and each would be loaded
    // again after it.
    struct UnitWriter
    {
        alsimd::u16x8 v0, v1, v2, v3, v4, v5;
        const U16* offsets;
        S32 length;
        S32 vectors;

        explicit UnitWriter(const QuadUnit& unit)
            : v0(alsimd::loadu_u16(unit.offsets)), v1(alsimd::loadu_u16(unit.offsets + 8)), v2(alsimd::loadu_u16(unit.offsets + 16)),
              v3(alsimd::loadu_u16(unit.offsets + 24)), v4(alsimd::loadu_u16(unit.offsets + 32)), v5(alsimd::loadu_u16(unit.offsets + 40)),
              offsets(unit.offsets), length(unit.length), vectors(unit.vectors)
        {
        }

        // The unit whose first quad's bottom left vertex is base, at out.
        // Its whole vectors run past its length into what is written after
        // it, and are written one index at a time where they would run past
        // limit. Returns the end of the unit.
        LL_FORCE_INLINE U16* write(U16* out, S32 base, const U16* limit) const
        {
            if (out + vectors * 8 > limit)
            {
                for (S32 k = 0; k < length; ++k)
                {
                    out[k] = (U16)(offsets[k] + base);
                }
                return out + length;
            }
            const alsimd::u16x8 b = alsimd::set1_u16((U16)base);
            switch (vectors)
            {
                case 6: alsimd::storeu_u16(out + 40, alsimd::add_u16(v5, b)); [[fallthrough]];
                case 5: alsimd::storeu_u16(out + 32, alsimd::add_u16(v4, b)); [[fallthrough]];
                case 4: alsimd::storeu_u16(out + 24, alsimd::add_u16(v3, b)); [[fallthrough]];
                case 3: alsimd::storeu_u16(out + 16, alsimd::add_u16(v2, b)); [[fallthrough]];
                case 2: alsimd::storeu_u16(out + 8, alsimd::add_u16(v1, b)); [[fallthrough]];
                default: alsimd::storeu_u16(out, alsimd::add_u16(v0, b));
            }
            return out + length;
        }
    };
    static_assert(LONE_BAND_QUADS <= QuadUnit::MAX_QUADS && BAND_QUADS <= QuadUnit::MAX_QUADS && COLUMN_ROWS - 1 <= QuadUnit::MAX_QUADS);

    // A unit pays for its offsets once it has this many quads and is written
    // this many times.
    constexpr S32 UNIT_QUADS = 4;
    constexpr S32 UNIT_USES = 3;

    // The index order is called, not inlined, so that createSide's own loops
    // keep one layout however the order is written.
#if LL_WINDOWS
#define VOLUME_NOINLINE __declspec(noinline)
#else
#define VOLUME_NOINLINE __attribute__((noinline))
#endif

    // count columns of rows quads, the listed ones or the first count, a
    // unit each. Nothing is written past limit. Returns the end of what it
    // wrote.
    template <bool LISTED>
    VOLUME_NOINLINE U16* unit_columns(U16* out, const U16* limit, const S32* lefts, S32 count, S32 num_s, S32 rows)
    {
        const QuadUnit column(rows, num_s, num_s);
        const UnitWriter writer(column);
        for (S32 q = 0; q < count; ++q)
        {
            out = writer.write(out, LISTED ? lefts[q] : q, limit);
        }
        return out;
    }

    // The quads from grid column begin on in bands, the first wide of them
    // a quad wider than narrow, of rows quads up: each band row by row,
    // every other band down, a unit a band's row where keep keeps the whole
    // row and a quad at a time where it does not. Nothing is written past
    // limit. Returns the end of what it wrote.
    template <class KEEP>
    VOLUME_NOINLINE U16* unit_bands(U16* out, const U16* limit, S32 begin, S32 bands, S32 narrow, S32 wide, S32 num_s, S32 rows, const KEEP& keep)
    {
        const QuadUnit narrow_row(narrow, 1, num_s);
        const QuadUnit wide_row(wide > 0 ? narrow + 1 : narrow, 1, num_s);
        S32 first = begin;
        for (S32 band = 0; band < bands; ++band)
        {
            const S32 width = band < wide ? narrow + 1 : narrow;
            const UnitWriter writer(band < wide ? wide_row : narrow_row);
            const auto band_row = [&](U16* at, S32 t)
            {
                if (KEEP::ALL || keep.whole(t))
                {
                    return writer.write(at, first + num_s * t, limit);
                }
                for (S32 s = first; s < first + width; ++s)
                {
                    at = quad_indices<KEEP>(at, s, t, num_s, keep);
                }
                return at;
            };
            if (band & 1)
            {
                for (S32 t = rows - 1; t >= 0; --t)
                {
                    out = band_row(out, t);
                }
            }
            else
            {
                for (S32 t = 0; t < rows; ++t)
                {
                    out = band_row(out, t);
                }
            }
            first += width;
        }
        return out;
    }

    // A grid of up to COLUMN_ROWS rows, a column of quads at a time: the
    // count columns listed, or the first count. Nothing is written past
    // limit. Returns the end of what it wrote.
    template <bool LISTED, class KEEP>
    U16* grid_columns(U16* out, const U16* limit, const S32* lefts, S32 count, S32 num_s, S32 rows, const KEEP& keep)
    {
        if (KEEP::ALL && rows >= UNIT_QUADS && count >= UNIT_USES)
        {
            return unit_columns<LISTED>(out, limit, lefts, count, num_s, rows);
        }
        if (rows == 1)
        {
            for (S32 q = 0; q < count; ++q)
            {
                out = quad_indices<KEEP>(out, LISTED ? lefts[q] : q, 0, num_s, keep);
            }
            return out;
        }
        for (S32 q = 0; q < count; ++q)
        {
            const S32 s = LISTED ? lefts[q] : q;
            for (S32 t = 0; t < rows; ++t)
            {
                out = quad_indices<KEEP>(out, s, t, num_s, keep);
            }
        }
        return out;
    }

    // The count quads from grid column begin on, each sharing its left edge
    // with the one before it, in bands of rows quads up: each band row by
    // row, every other band down. Nothing is written past limit. Returns the
    // end of what it wrote.
    template <class KEEP>
    U16* grid_bands(U16* out, const U16* limit, S32 begin, S32 count, S32 num_s, S32 rows, const KEEP& keep)
    {
        const S32 bands = count <= LONE_BAND_QUADS ? 1 : (count + BAND_QUADS - 1) / BAND_QUADS;
        const S32 narrow = bands == 1 ? count : count / bands;
        const S32 wide = bands == 1 ? 0 : count % bands;
        if constexpr (KEEP::WHOLE_ROWS)
        {
            if (narrow >= UNIT_QUADS)
            {
                return unit_bands<KEEP>(out, limit, begin, bands, narrow, wide, num_s, rows, keep);
            }
        }
        if (bands == 1)
        {
            for (S32 t = 0; t < rows; ++t)
            {
                for (S32 s = begin; s < begin + count; ++s)
                {
                    out = quad_indices<KEEP>(out, s, t, num_s, keep);
                }
            }
            return out;
        }
        S32 first = begin;
        for (S32 band = 0; band < bands; ++band)
        {
            const S32 end = first + narrow + (band < wide ? 1 : 0);
            const bool down = band & 1;
            for (S32 row = 0; row < rows; ++row)
            {
                const S32 t = down ? rows - 1 - row : row;
                for (S32 s = first; s < end; ++s)
                {
                    out = quad_indices<KEEP>(out, s, t, num_s, keep);
                }
            }
            first = end;
        }
        return out;
    }

    // A side's triangles, from the quads of its grid whose left columns are
    // listed, or from every quad, but for those keep leaves out, in the order
    // above. Writes nothing past limit. Returns the end of what it wrote.
    template <bool LISTED, class KEEP>
    VOLUME_NOINLINE U16* grid_indices(U16* out, const U16* limit, S32 num_s, S32 num_t, const S32* lefts, S32 quads, const KEEP& keep)
    {
        const S32 rows = num_t - 1;
        if (num_t <= COLUMN_ROWS)
        {
            return grid_columns<LISTED, KEEP>(out, limit, lefts, quads, num_s, rows, keep);
        }
        if (!LISTED)
        {
            return grid_bands<KEEP>(out, limit, 0, quads, num_s, rows, keep);
        }
        // A flat inner side's quads run between its corners, and two runs
        // share no vertex, so each is a grid of its own.
        S32 begin = 0;
        for (S32 q = 1; q <= quads; ++q)
        {
            if (q == quads || lefts[q] != lefts[q - 1] + 1)
            {
                out = grid_bands<KEEP>(out, limit, lefts[begin], q - begin, num_s, rows, keep);
                begin = q;
            }
        }
        return out;
    }

    bool same_point(const LLVector4a& a, const LLVector4a& b)
    {
        return a.equal(b).areAllSet(LLVector4Logical::MASK_XYZ);
    }

    // Leaves set only the flags of triangles two of whose corners are the
    // same point, of those grid_coincident_triangles found two corners of
    // one code in, and returns how many are left set.
    S32 confirm_pinches(const LLVector4a* pos, S32 num_s, S32 num_t, U64* upper, U64* lower, S32 words)
    {
        S32 pinched = 0;
        // other is the corner off the triangle's diagonal, as a step from
        // its bottom left one.
        const auto confirm = [&](U64& flags, S32 first, S32 other)
        {
            U64 left = flags;
            while (left)
            {
                const S32 bit = std::countr_zero(left);
                left &= left - 1;
                const S32 bl = first + bit;
                const S32 tr = bl + num_s + 1;
                if (same_point(pos[bl], pos[tr]) || same_point(pos[bl], pos[bl + other]) || same_point(pos[bl + other], pos[tr]))
                {
                    ++pinched;
                }
                else
                {
                    flags &= ~(U64(1) << bit);
                }
            }
        };
        for (S32 t = 0; t < num_t - 1; ++t)
        {
            for (S32 w = 0; w < words; ++w)
            {
                confirm(upper[t * words + w], num_s * t + w * 64, num_s);
                confirm(lower[t * words + w], num_s * t + w * 64, 1);
            }
        }
        return pinched;
    }

    // The path station a cap closes: the last for a top cap, the first for a
    // bottom one.
    S32 cap_station(const LLVolume* volume, bool top)
    {
        return top ? (S32)volume->getPath().mPath.size() - 1 : 0;
    }

    // A cap's normal: the normal of its station's profile plane, the third
    // row of the station's rotation, which a taper to a point or a line
    // cannot make degenerate as it does a cross product of the cap's own
    // edges. A top cap's profile winds to face along that row and a bottom
    // cap's against it. Where the ring of the station next to the cap lies
    // on the side the winding faces, the cap faces the other way, and
    // reversed is set: the end of a twisted sphere turns its half disc to
    // the other side of the axis it sweeps about, and the sphere leaves the
    // cap the other way. decide is false on a partial build, which keeps
    // reversed as the full build left it.
    LLVector4a cap_normal(const LLVolume* volume, bool top, bool decide, bool& reversed)
    {
        const LLAlignedArray<LLPath::PathPt, 64>& path = volume->getPath().mPath;
        const S32 own = cap_station(volume, top);
        LLVector4a normal = path[own].mRot.getRow<2>();
        normal.normalize3();
        if (!top)
        {
            normal.negate();
        }

        if (decide)
        {
            reversed = false;
            // A line's or a flexible path's end faces along the path, away
            // from the ring beside it, whatever the shear, taper or twist:
            // only a circle's end can turn past the axis it sweeps about.
            const U8 curve = volume->getParams().getPathParams().getCurveType();
            const S32 count = (S32)path.size();
            if (count > 1 && curve != LL_PCODE_PATH_LINE && curve != LL_PCODE_PATH_FLEXIBLE)
            {
                const S32 next = top ? own - 1 : own + 1;
                const S32 ring = volume->getProfile().getTotal();
                const LLAlignedArray<LLVector4a, 64>& mesh = volume->getMesh();
                LLVector4a toward;
                toward.clear();
                for (S32 i = 0; i < ring; ++i)
                {
                    LLVector4a d;
                    d.setSub(mesh[next * ring + i], mesh[own * ring + i]);
                    toward.add(d);
                }
                reversed = toward.dot3(normal).getF32() > SAME_PLACE * ring;
            }
        }

        if (reversed)
        {
            normal.negate();
        }
        return normal;
    }

    // +x in the plane of a normal, or +y where the normal is within 45
    // degrees of x: a tangent where nothing gives one a direction, unit
    // length and orthogonal to the normal. w is 1.
    LLVector4a plane_axis(const LLVector4a& normal)
    {
        LLVector4a axis;
        if (normal[0] * normal[0] > 0.5f)
        {
            axis.set(0.f, 1.f, 0.f);
        }
        else
        {
            axis.set(1.f, 0.f, 0.f);
        }
        LLVector4a along = normal;
        along.mul(axis.dot3(normal).getF32());
        axis.sub(along);
        axis.normalize3fast();
        axis.getF32ptr()[3] = 1.f;
        return axis;
    }

    // A flexible cap's tangent: dP/du of its end station's frame, u running
    // along the profile's x as the cap's UVs map it, put in the plane of the
    // cap's normal, and w the side of the normal x dP/du that dP/dv lies on,
    // v running along the profile's y on a top cap and against it on a
    // bottom one. Where the station scales x to nothing, an axis in the
    // plane.
    LLVector4a cap_tangent(const LLVolume* volume, bool top, const LLVector4a& normal)
    {
        const LLPath::PathPt& station = volume->getPath().mPath[cap_station(volume, top)];
        const F32* scale = station.mScale.getF32ptr();
        LLVector4a du = station.mRot.getRow<0>();
        du.mul(scale[0]);
        LLVector4a dv = station.mRot.getRow<1>();
        dv.mul(top ? scale[1] : -scale[1]);

        LLVector4a tangent = du;
        LLVector4a along = normal;
        along.mul(du.dot3(normal).getF32());
        tangent.sub(along);
        if (tangent.dot3(tangent).getF32() <= SAME_PLACE * SAME_PLACE)
        {
            return plane_axis(normal);
        }
        tangent.normalize3fast();
        LLVector4a turned;
        turned.setCross3(normal, tangent);
        tangent.getF32ptr()[3] = turned.dot3(dv).getF32() < 0.f ? -1.f : 1.f;
        return tangent;
    }

    // Gives each vertex of a grid whose tangent has no direction, marked by
    // a w of 0, the nearest own tangent up or down its column, else along
    // its row, else around it, put in the plane of its normal, or an axis
    // in that plane where none serves; w becomes w. A donor is a tangent of
    // the grid's own, never one given, so that none is passed on twice.
    void fill_grid_tangents(LLVector4a* tangents, const LLVector4a* normals, S32 num_s, S32 num_t, F32 w)
    {
        const auto own = [](const LLVector4a& t) { return t[3] == 1.f || t[3] == -1.f; };
        for (S32 t = 0; t < num_t; ++t)
        {
            for (S32 s = 0; s < num_s; ++s)
            {
                LLVector4a& tangent = tangents[s + num_s * t];
                if (own(tangent))
                {
                    continue;
                }
                const LLVector4a* donor = nullptr;
                for (S32 d = 1; d < num_t && !donor; ++d)
                {
                    for (S32 row : { t - d, t + d })
                    {
                        if (row >= 0 && row < num_t && own(tangents[s + num_s * row]))
                        {
                            donor = &tangents[s + num_s * row];
                            break;
                        }
                    }
                }
                for (S32 d = 1; d < num_s && !donor; ++d)
                {
                    for (S32 column : { s - d, s + d })
                    {
                        if (column >= 0 && column < num_s && own(tangents[column + num_s * t]))
                        {
                            donor = &tangents[column + num_s * t];
                            break;
                        }
                    }
                }
                for (S32 d = 1; d < llmax(num_s, num_t) && !donor; ++d)
                {
                    for (S32 row = llmax(t - d, 0); row <= llmin(t + d, num_t - 1) && !donor; ++row)
                    {
                        for (S32 column = llmax(s - d, 0); column <= llmin(s + d, num_s - 1); ++column)
                        {
                            if (own(tangents[column + num_s * row]))
                            {
                                donor = &tangents[column + num_s * row];
                                break;
                            }
                        }
                    }
                }
                const LLVector4a& normal = normals[s + num_s * t];
                LLVector4a given = plane_axis(normal);
                if (donor)
                {
                    LLVector4a in_plane = *donor;
                    LLVector4a along = normal;
                    along.mul(in_plane.dot3(normal).getF32());
                    in_plane.sub(along);
                    if (in_plane.dot3(in_plane).getF32() > SAME_PLACE * SAME_PLACE)
                    {
                        in_plane.normalize3fast();
                        given = in_plane;
                    }
                }
                // Marked as given, so that it is no donor.
                given.getF32ptr()[3] = 2.f * w;
                tangent = given;
            }
        }
        for (S32 v = 0; v < num_s * num_t; ++v)
        {
            tangents[v].getF32ptr()[3] = w;
        }
    }

    // Turns every triangle of an index list over.
    void reverse_winding(U16* indices, S32 count)
    {
        for (S32 i = 0; i + 2 < count; i += 3)
        {
            std::swap(indices[i + 1], indices[i + 2]);
        }
    }
}

void    LerpPlanarVertex(LLVolumeFace::VertexData& v0,
                   LLVolumeFace::VertexData& v1,
                   LLVolumeFace::VertexData& v2,
                   LLVolumeFace::VertexData& vout,
                   F32  coef01,
                   F32  coef02)
{

    LLVector4a lhs;
    lhs.setSub(v1.getPosition(), v0.getPosition());
    lhs.mul(coef01);
    LLVector4a rhs;
    rhs.setSub(v2.getPosition(), v0.getPosition());
    rhs.mul(coef02);

    rhs.add(lhs);
    rhs.add(v0.getPosition());

    vout.setPosition(rhs);

    vout.mTexCoord = v0.mTexCoord + ((v1.mTexCoord-v0.mTexCoord)*coef01)+((v2.mTexCoord-v0.mTexCoord)*coef02);
    vout.setNormal(v0.getNormal());
}

bool LLVolumeFace::createUnCutCubeCap(LLVolume* volume, bool partial_build)
{
    const LLAlignedArray<LLVector4a,64>& mesh = volume->getMesh();
    const LLAlignedArray<LLVector4a,64>& profile = volume->getProfile().mProfile;
    S32 max_s = volume->getProfile().getTotal();
    S32 max_t = volume->getPath().mPath.size();

    // S32 i;
    S32 grid_size = (profile.size()-1)/4;
    // VFExtents change
    LLVector4a& min = mExtents[0];
    LLVector4a& max = mExtents[1];

    S32 offset = 0;
    if (mTypeMask & TOP_MASK)
    {
        offset = (max_t-1) * max_s;
    }
    else
    {
        offset = mBeginS;
    }

    {
        VertexData  corners[4];
        VertexData baseVert;
        for(S32 t = 0; t < 4; t++)
        {
            corners[t].getPosition().load4a(mesh[offset + (grid_size*t)].getF32ptr());
            corners[t].mTexCoord.mV[0] = profile[grid_size*t][0]+0.5f;
            corners[t].mTexCoord.mV[1] = 0.5f - profile[grid_size*t][1];
        }

        baseVert.setNormal(cap_normal(volume, mTypeMask & TOP_MASK, !partial_build, mReversed));

        if (mTypeMask & TOP_MASK)
        {
            //Swap the UVs on the U(X) axis for top face
            LLVector2 swap;
            swap = corners[0].mTexCoord;
            corners[0].mTexCoord=corners[3].mTexCoord;
            corners[3].mTexCoord=swap;
            swap = corners[1].mTexCoord;
            corners[1].mTexCoord=corners[2].mTexCoord;
            corners[2].mTexCoord=swap;
        }

        if (!partial_build)
        {
            resizeVertices((grid_size+1)*(grid_size+1));
        }

        LLVector4a* pos = (LLVector4a*) mPositions;
        LLVector4a* norm = (LLVector4a*) mNormals;
        LLVector2* tc = (LLVector2*) mTexCoords;

        for(int gx = 0;gx<grid_size+1;gx++)
        {
            for(int gy = 0;gy<grid_size+1;gy++)
            {
                VertexData newVert;
                LerpPlanarVertex(
                    corners[0],
                    corners[1],
                    corners[3],
                    newVert,
                    (F32)gx/(F32)grid_size,
                    (F32)gy/(F32)grid_size);

                *pos++ = newVert.getPosition();
                *norm++ = baseVert.getNormal();
                *tc++ = newVert.mTexCoord;

                if (gx == 0 && gy == 0)
                {
                    min = newVert.getPosition();
                    max = min;
                }
                else
                {
                    min.setMin(min, newVert.getPosition());
                    max.setMax(max, newVert.getPosition());
                }
            }
        }

        mCenter->setAdd(min, max);
        mCenter->mul(0.5f);
    }

    if (!partial_build)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("llvfcuccm - generate indices");

        resizeIndices(grid_size*grid_size*6);

        U16* out = mIndices;

        S32 idxs[] = {0,1,(grid_size+1)+1,(grid_size+1)+1,(grid_size+1),0};

        for(S32 gx = 0;gx<grid_size;gx++)
        {
            for(S32 gy = 0;gy<grid_size;gy++)
            {
                if (mTypeMask & TOP_MASK)
                {
                    for(S32 i=5;i>=0;i--)
                    {
                        *out++ = ((gy*(grid_size+1))+gx+idxs[i]);
                    }

                }
                else
                {
                    for(S32 i=0;i<6;i++)
                    {
                        *out++ = ((gy*(grid_size+1))+gx+idxs[i]);
                    }
                }
            }
        }

        if (mReversed)
        {
            reverse_winding(mIndices, mNumIndices);
        }
    }

    return true;
}


bool LLVolumeFace::createCap(LLVolume* volume, bool partial_build)
{
    if (!(mTypeMask & HOLLOW_MASK) &&
        !(mTypeMask & OPEN_MASK) &&
        ((volume->getParams().getPathParams().getBegin()==0.0f)&&
        (volume->getParams().getPathParams().getEnd()==1.0f))&&
        (volume->getParams().getProfileParams().getCurveType()==LL_PCODE_PROFILE_SQUARE &&
         volume->getParams().getPathParams().getCurveType()==LL_PCODE_PATH_LINE)
        ){
        return createUnCutCubeCap(volume, partial_build);
    }

    S32 num_vertices = 0, num_indices = 0;

    const LLAlignedArray<LLVector4a,64>& mesh = volume->getMesh();
    const LLAlignedArray<LLVector4a,64>& profile = volume->getProfile().mProfile;

    // All types of caps have the same number of vertices and indices
    num_vertices = profile.size();
    num_indices = (profile.size() - 2)*3;

    if (!partial_build)
    {
        if (!(mTypeMask & HOLLOW_MASK) && !(mTypeMask & OPEN_MASK))
        {
            resizeVertices(num_vertices+1);
            resizeIndices(num_indices+3);
        }
        else
        {
            resizeVertices(num_vertices);
            resizeIndices(num_indices);
        }
    }

    S32 max_s = volume->getProfile().getTotal();
    S32 max_t = volume->getPath().mPath.size();

    mCenter->clear();

    S32 offset = 0;
    if (mTypeMask & TOP_MASK)
    {
        offset = (max_t-1) * max_s;
    }
    else
    {
        offset = mBeginS;
    }

    LLVector2 cuv;
    LLVector2 min_uv, max_uv;
    // VFExtents change
    LLVector4a& min = mExtents[0];
    LLVector4a& max = mExtents[1];

    LLVector2* tc = (LLVector2*) mTexCoords;
    LLVector4a* pos = (LLVector4a*) mPositions;
    LLVector4a* norm = (LLVector4a*) mNormals;

    // Copy the vertices into the array

    const LLVector4a* src = mesh.mArray+offset;
    const LLVector4a* end = src+num_vertices;

    min = *src;
    max = min;


    const LLVector4a* p = profile.mArray;

    if (mTypeMask & TOP_MASK)
    {
        min_uv.set((*p)[0]+0.5f,
                    (*p)[1]+0.5f);

        max_uv = min_uv;

        while(src < end)
        {
            tc->mV[0] = (*p)[0]+0.5f;
            tc->mV[1] = (*p)[1]+0.5f;

            llassert(src->isFinite3()); // MAINT-5660; don't know why this happens, does not affect Release builds
            update_min_max(min,max,*src);
            update_min_max(min_uv, max_uv, *tc);

            *pos = *src;

            llassert(pos->isFinite3());

            ++p;
            ++tc;
            ++src;
            ++pos;
        }
        }
        else
        {

        min_uv.set((*p)[0]+0.5f,
                   0.5f - (*p)[1]);
        max_uv = min_uv;

        while(src < end)
        {
            // Mirror for underside.
            tc->mV[0] = (*p)[0]+0.5f;
            tc->mV[1] = 0.5f - (*p)[1];

            llassert(src->isFinite3());
            update_min_max(min,max,*src);
            update_min_max(min_uv, max_uv, *tc);

            *pos = *src;

            llassert(pos->isFinite3());

            ++p;
            ++tc;
            ++src;
            ++pos;
        }
    }

    mCenter->setAdd(min, max);
    mCenter->mul(0.5f);

    if (!(mTypeMask & HOLLOW_MASK) && !(mTypeMask & OPEN_MASK))
    {
        // The centre vertex takes the UV of its own position: the profile
        // point its station's scale and rotation carry there. A twist turns
        // the cap, and the middle of its positions' box is then not where
        // the middle of its UVs' box maps. Along an axis the station scales
        // to nothing, every point of the cap is one place, and the middle of
        // the UVs serves.
        cuv = (min_uv + max_uv)*0.5f;

        const LLPath::PathPt& station = volume->getPath().mPath[cap_station(volume, mTypeMask & TOP_MASK)];
        LLVector4a origin = station.mPos;
        if (!origin.isFinite3())
        {
            origin.clear();
        }
        LLVector4a from_origin;
        from_origin.setSub(*mCenter, origin);
        const F32* scale = station.mScale.getF32ptr();
        if (scale[0] > SAME_PLACE)
        {
            cuv.mV[0] = from_origin.dot3(station.mRot.getRow<0>()).getF32() / scale[0] + 0.5f;
        }
        if (scale[1] > SAME_PLACE)
        {
            const F32 y = from_origin.dot3(station.mRot.getRow<1>()).getF32() / scale[1];
            cuv.mV[1] = (mTypeMask & TOP_MASK) ? y + 0.5f : 0.5f - y;
        }

        *pos++ = *mCenter;
        *tc++ = cuv;
        num_vertices++;
    }

    const LLVector4a normal = cap_normal(volume, mTypeMask & TOP_MASK, !partial_build, mReversed);
    if (mFlexible)
    {
        // mCapTangent is plain floats, not aligned as a vector stores.
        const LLVector4a tangent = cap_tangent(volume, mTypeMask & TOP_MASK, normal);
        std::copy_n(tangent.getF32ptr(), 4, mCapTangent);
    }

    llassert(llfinite(normal.getF32ptr()[0]));
    llassert(llfinite(normal.getF32ptr()[1]));
    llassert(llfinite(normal.getF32ptr()[2]));

    llassert(!llisnan(normal.getF32ptr()[0]));
    llassert(!llisnan(normal.getF32ptr()[1]));
    llassert(!llisnan(normal.getF32ptr()[2]));

    for (S32 i = 0; i < num_vertices; i++)
    {
        norm[i].load4a(normal.getF32ptr());
    }

    // A partial build keeps the indices it has.
    if (partial_build)
    {
        return true;
    }

    if (mTypeMask & HOLLOW_MASK)
    {
        // Where the hole touches the wall, the strip's triangles have no area
        // but what rounding leaves, of either sign.
        const F32 zero_area = volume->getProfile().holeTouchesWall() ? CAP_ZERO_AREA : 0.f;

        if (mTypeMask & TOP_MASK)
        {
            // HOLLOW TOP
            // Does it matter if it's open or closed? - djs

            S32 pt1 = 0, pt2 = num_vertices - 1;
            S32 i = 0;
            while (pt2 - pt1 > 1)
            {
                // Use the profile points instead of the mesh, since you want
                // the un-transformed profile distances.
                const LLVector4a& p1 = profile[pt1];
                const LLVector4a& p2 = profile[pt2];
                const LLVector4a& pa = profile[pt1+1];
                const LLVector4a& pb = profile[pt2-1];

                const F32* p1V = p1.getF32ptr();
                const F32* p2V = p2.getF32ptr();
                const F32* paV = pa.getF32ptr();
                const F32* pbV = pb.getF32ptr();

                //p1.mV[VZ] = 0.f;
                //p2.mV[VZ] = 0.f;
                //pa.mV[VZ] = 0.f;
                //pb.mV[VZ] = 0.f;

                // Use area of triangle to determine backfacing
                F32 area_1a2, area_1ba, area_21b, area_2ab;
                area_1a2 =  (p1V[0]*paV[1] - paV[0]*p1V[1]) +
                            (paV[0]*p2V[1] - p2V[0]*paV[1]) +
                            (p2V[0]*p1V[1] - p1V[0]*p2V[1]);

                area_1ba =  (p1V[0]*pbV[1] - pbV[0]*p1V[1]) +
                            (pbV[0]*paV[1] - paV[0]*pbV[1]) +
                            (paV[0]*p1V[1] - p1V[0]*paV[1]);

                area_21b =  (p2V[0]*p1V[1] - p1V[0]*p2V[1]) +
                            (p1V[0]*pbV[1] - pbV[0]*p1V[1]) +
                            (pbV[0]*p2V[1] - p2V[0]*pbV[1]);

                area_2ab =  (p2V[0]*paV[1] - paV[0]*p2V[1]) +
                            (paV[0]*pbV[1] - pbV[0]*paV[1]) +
                            (pbV[0]*p2V[1] - p2V[0]*pbV[1]);

                const auto outer_shorter = [&]()
                {
                    LLVector4a d1;
                    d1.setSub(p1, pa);
                    LLVector4a d2;
                    d2.setSub(p2, pb);
                    return d1.dot3(d1) < d2.dot3(d2);
                };
                const bool use_tri1a2 = cap_strip_takes_outer(area_1a2, area_2ab, area_21b, area_1ba, outer_shorter, zero_area);

                if (use_tri1a2)
                {
                    mIndices[i++] = pt1;
                    mIndices[i++] = pt1 + 1;
                    mIndices[i++] = pt2;
                    pt1++;
                }
                else
                {
                    mIndices[i++] = pt1;
                    mIndices[i++] = pt2 - 1;
                    mIndices[i++] = pt2;
                    pt2--;
                }
            }
        }
        else
        {
            // HOLLOW BOTTOM
            // Does it matter if it's open or closed? - djs

            llassert(mTypeMask & BOTTOM_MASK);
            S32 pt1 = 0, pt2 = num_vertices - 1;

            S32 i = 0;
            while (pt2 - pt1 > 1)
            {
                // Use the profile points instead of the mesh, since you want
                // the un-transformed profile distances.
                const LLVector4a& p1 = profile[pt1];
                const LLVector4a& p2 = profile[pt2];
                const LLVector4a& pa = profile[pt1+1];
                const LLVector4a& pb = profile[pt2-1];

                const F32* p1V = p1.getF32ptr();
                const F32* p2V = p2.getF32ptr();
                const F32* paV = pa.getF32ptr();
                const F32* pbV = pb.getF32ptr();

                // Use area of triangle to determine backfacing
                F32 area_1a2, area_1ba, area_21b, area_2ab;
                area_1a2 =  (p1V[0]*paV[1] - paV[0]*p1V[1]) +
                            (paV[0]*p2V[1] - p2V[0]*paV[1]) +
                            (p2V[0]*p1V[1] - p1V[0]*p2V[1]);

                area_1ba =  (p1V[0]*pbV[1] - pbV[0]*p1V[1]) +
                            (pbV[0]*paV[1] - paV[0]*pbV[1]) +
                            (paV[0]*p1V[1] - p1V[0]*paV[1]);

                area_21b =  (p2V[0]*p1V[1] - p1V[0]*p2V[1]) +
                            (p1V[0]*pbV[1] - pbV[0]*p1V[1]) +
                            (pbV[0]*p2V[1] - p2V[0]*pbV[1]);

                area_2ab =  (p2V[0]*paV[1] - paV[0]*p2V[1]) +
                            (paV[0]*pbV[1] - pbV[0]*paV[1]) +
                            (pbV[0]*p2V[1] - p2V[0]*pbV[1]);

                const auto outer_shorter = [&]()
                {
                    LLVector4a d1;
                    d1.setSub(p1, pa);
                    LLVector4a d2;
                    d2.setSub(p2, pb);
                    return d1.dot3(d1) < d2.dot3(d2);
                };
                const bool use_tri1a2 = cap_strip_takes_outer(area_1a2, area_2ab, area_21b, area_1ba, outer_shorter, zero_area);

                // Flipped backfacing from top
                if (use_tri1a2)
                {
                    mIndices[i++] = pt1;
                    mIndices[i++] = pt2;
                    mIndices[i++] = pt1 + 1;
                    pt1++;
                }
                else
                {
                    mIndices[i++] = pt1;
                    mIndices[i++] = pt2;
                    mIndices[i++] = pt2 - 1;
                    pt2--;
                }
            }
        }
    }
    else
    {
        // Not hollow, generate the triangle fan.
        U16 v1 = 2;
        U16 v2 = 1;

        if (mTypeMask & TOP_MASK)
        {
            v1 = 1;
            v2 = 2;
        }

        for (S32 i = 0; i < (num_vertices - 2); i++)
        {
            mIndices[3*i] = num_vertices - 1;
            mIndices[3*i+v1] = i;
            mIndices[3*i+v2] = i + 1;
        }


    }

    if (mReversed)
    {
        reverse_winding(mIndices, mNumIndices);
    }

    return true;
}

void LLVolumeFace::createTangents()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if (!mTangents || mTangentsStale)
    {
        if (!mTangents)
        {
            allocateTangents(mNumVertices);
        }
        mTangentsStale = false;

        if (mFlexible && (mTypeMask & CAP_MASK))
        {
            LLVector4a tangent;
            tangent.loadua(mCapTangent);
            std::fill(mTangents, mTangents + mNumVertices, tangent);
        }
        else if (!(mTypeMask & CAP_MASK) && mNumS > 0 && mNumT > 0 && mNumS * mNumT == mNumVertices &&
                 mNumIndices <= (mNumS - 1) * (mNumT - 1) * 6)
        {
            // A side is createSide's grid. The quads and triangles its indices
            // leave out have no area, and add nothing to a tangent.
            if (mFlexible)
            {
                // Along the rows, the way u runs across the face, and w as
                // the face's UVs wind: u runs along a row and v down a
                // column, each the one way over the grid, but for a closing
                // copy whose u starts the profile over: the way most of a
                // row's steps run.
                S32 rising = 0;
                for (S32 s = 0; s + 1 < mNumS; ++s)
                {
                    const F32 step = mTexCoords[s + 1].mV[0] - mTexCoords[s].mV[0];
                    rising += step > 0.f ? 1 : (step < 0.f ? -1 : 0);
                }
                const F32 dv = mTexCoords[mNumS * (mNumT - 1)].mV[1] - mTexCoords[0].mV[1];
                const F32 sign = rising < 0 ? -1.f : 1.f;
                const F32 w = (rising < 0) == (dv < 0.f) ? 1.f : -1.f;
                if (alsimd::grid_tangents_central(mPositions, mNormals, mNumS, mNumT, mJoinsS, SAME_PLACE, sign, w, mTangents))
                {
                    fill_grid_tangents(mTangents, mNormals, mNumS, mNumT, w);
                }
            }
            else
            {
                alsimd::grid_tangents(mPositions, mNormals, mTexCoords, mNumS, mNumT, mTangents);
            }
        }
        else
        {
            alsimd::indexed_tangents(mPositions, mNormals, mTexCoords, mNumVertices, mIndices, mNumIndices, mTangents);
        }
    }
}

void LLVolumeFace::resizeVertices(S32 num_verts)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    ll_aligned_free<64>(mPositions);
    //DO NOT free mNormals and mTexCoords as they are part of mPositions buffer
    ll_aligned_free_16(mTangents);

    mTangents = NULL;
    mPlanarNormal[3] = 0.f;

    if (num_verts)
    {
        // the texture coordinate block is a whole number of 16-byte vectors:
        // the copies move it that way and the texcoord kernel reads it two
        // coordinates at a time, the last vector included
        S32 tc_size = ((num_verts*sizeof(LLVector2)) + 0xF) & ~0xF;

        mPositions = (LLVector4a*) ll_aligned_malloc<64>(sizeof(LLVector4a)*2*num_verts+tc_size);
        mNormals = mPositions+num_verts;
        mTexCoords = (LLVector2*) (mNormals+num_verts);

        ll_assert_aligned(mPositions, 64);
    }
    else
    {
        mPositions = NULL;
        mNormals = NULL;
        mTexCoords = NULL;
    }


    if (mPositions)
    {
        mNumVertices = num_verts;
        mNumAllocatedVertices = num_verts;
    }
    else
    {
        // Either num_verts is zero or allocation failure
        mNumVertices = 0;
        mNumAllocatedVertices = 0;
    }

    // Force update
    mJointRiggingInfoTab.clear();
}

void LLVolumeFace::allocateTangents(S32 num_verts)
{
    ll_aligned_free_16(mTangents);
    mTangents = (LLVector4a*) ll_aligned_malloc_16(sizeof(LLVector4a)*num_verts);
}

void LLVolumeFace::allocateWeights(S32 num_verts)
{
    ll_aligned_free_16(mWeights);
    mWeights = (LLVector4a*)ll_aligned_malloc_16(sizeof(LLVector4a)*num_verts);

}

void LLVolumeFace::allocateJointIndices(S32 num_verts)
{
#if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
    ll_aligned_free_16(mJointIndices);
    ll_aligned_free_16(mJustWeights);

    mJointIndices = (U8*)ll_aligned_malloc_16(sizeof(U8) * 4 * num_verts);
    mJustWeights = (LLVector4a*)ll_aligned_malloc_16(sizeof(LLVector4a) * num_verts);
#endif
}

void LLVolumeFace::resizeIndices(S32 num_indices)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    ll_aligned_free_16(mIndices);
    llassert(num_indices % 3 == 0);

    if (num_indices)
    {
        // the index block is a whole number of 16-byte vectors, which is
        // how the copies move it
        S32 size = ((num_indices*sizeof(U16)) + 0xF) & ~0xF;

        mIndices = (U16*) ll_aligned_malloc_16(size);
    }
    else
    {
        mIndices = NULL;
    }

    if (mIndices)
    {
        mNumIndices = num_indices;
    }
    else
    {
        // Either num_indices is zero or allocation failure
        mNumIndices = 0;
    }
}

void LLVolumeFace::fillFromLegacyData(std::vector<LLVolumeFace::VertexData>& v, std::vector<U16>& idx)
{
    resizeVertices(static_cast<S32>(v.size()));
    resizeIndices(static_cast<S32>(idx.size()));

    for (U32 i = 0; i < v.size(); ++i)
    {
        mPositions[i] = v[i].getPosition();
        mNormals[i] = v[i].getNormal();
        mTexCoords[i] = v[i].mTexCoord;
    }

    for (U32 i = 0; i < idx.size(); ++i)
    {
        mIndices[i] = idx[i];
    }
}

//static
size_t LLVolumeFace::weldVertices(VertexData* destination_vertices, U32* destination_indices,
                                  const VertexData* vertices, size_t vertex_count,
                                  const U32* indices, size_t index_count)
{
    static_assert(std::is_trivially_copyable_v<VertexData>, "meshopt_remapVertexBuffer copies vertices byte for byte");

    const meshopt_Stream streams[] = {
        { vertices[0].getPosition().getF32ptr(), sizeof(F32) * 3, sizeof(VertexData) },
        { vertices[0].getNormal().getF32ptr(), sizeof(F32) * 3, sizeof(VertexData) },
        { vertices[0].mTexCoord.mV, sizeof(F32) * 2, sizeof(VertexData) },
    };

    // remap[old] is the vertex's new index, or ~0u where no index uses it
    std::vector<unsigned int> remap(vertex_count);
    size_t unique = meshopt_generateVertexRemapMulti(&remap[0], indices, index_count, vertex_count, streams, sizeof(streams) / sizeof(streams[0]));

    meshopt_remapVertexBuffer(destination_vertices, vertices, vertex_count, sizeof(VertexData), &remap[0]);
    meshopt_remapIndexBuffer<unsigned int>(destination_indices, indices, index_count, &remap[0]);

    return unique;
}

bool LLVolumeFace::createSide(LLVolume* volume, bool partial_build)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    bool flat = mTypeMask & FLAT_MASK;

    U8 sculpt_type = volume->getParams().getSculptType();
    U8 sculpt_stitching = sculpt_type & LL_SCULPT_TYPE_MASK;
    bool sculpt_invert = sculpt_type & LL_SCULPT_FLAG_INVERT;
    bool sculpt_mirror = sculpt_type & LL_SCULPT_FLAG_MIRROR;
    bool sculpt_reverse_horizontal = (sculpt_invert ? !sculpt_mirror : sculpt_mirror);  // XOR
    if (volume->isSculptSpherePlaceholder())
    {
        sculpt_stitching = LL_SCULPT_TYPE_SPHERE;
    }

    S32 num_vertices, num_indices;

    const LLAlignedArray<LLVector4a,64>& mesh = volume->getMesh();
    const LLAlignedArray<LLVector4a,64>& profile = volume->getProfile().mProfile;
    const LLAlignedArray<LLPath::PathPt,64>& path_data = volume->getPath().mPath;

    S32 max_s = volume->getProfile().getTotal();

    S32 s, t;
    F32 ss, tt;

    // A flat inner side copies each of its hole's corners, so that its
    // normals turn there: the quad between a corner and its copy has no
    // width, and has no triangles.
    const bool doubled = (mTypeMask & INNER_MASK) && (mTypeMask & FLAT_MASK) && mNumS > 2;
    const LLProfile::Face* outline = doubled ? &volume->getProfile().mFaces[mID] : nullptr;
    const S32 num_s = doubled ? outline->mCount : mNumS;

    F32 begin_stex = floorf(profile[mBeginS][2]);

    // Every row takes the same columns of its mesh row with the same s
    // texture coordinates, and the path's t for its own: the columns and
    // their s are worked out once, so the rows are copies.
    using Column = SideColumn;
    constexpr S32 STACK_COLUMNS = 256;
    Column stack_columns[STACK_COLUMNS];
    std::vector<Column> heap_columns;
    Column* columns = stack_columns;
    // A flat inner side's quads with area, by their left columns.
    S32 stack_lefts[STACK_COLUMNS];
    std::vector<S32> heap_lefts;
    S32* lefts = stack_lefts;
    if (mNumS > STACK_COLUMNS)
    {
        heap_columns.resize(mNumS);
        columns = heap_columns.data();
        if (doubled)
        {
            heap_lefts.resize(mNumS);
            lefts = heap_lefts.data();
        }
    }
    S32 column_count = 0;
    S32 quads = doubled ? 0 : mNumS - 1;
    for (s = 0; s < num_s; s++)
    {
        if (mTypeMask & END_MASK)
        {
            if (s)
            {
                ss = 1.f;
            }
            else
            {
                ss = 0.f;
            }
        }
        else
        {
            // Get s value for tex-coord.
            S32 index = mBeginS + s;
            if (index >= (S32)profile.size())
            {
                // edge?
                ss = flat ? 1.f - begin_stex : 1.f;
            }
            else if (!flat)
            {
                ss = profile[index][2];
            }
            else
            {
                ss = profile[index][2] - begin_stex;
            }
        }

        if (sculpt_reverse_horizontal)
        {
            ss = 1.f - ss;
        }

        // A column past the profile's end wraps to its start.
        const S32 column = mBeginS + s >= max_s ? mBeginS + s - max_s : mBeginS + s;
        if (doubled && s > 0)
        {
            lefts[quads++] = column_count - 1;
        }
        columns[column_count++] = { column, ss };
        if (doubled && outline->isCorner(s))
        {
            columns[column_count++] = { column, ss };
        }
    }
    llassert(column_count == mNumS);

    const S32 rows = mNumT - 1;

    // On a shared volume, a row or column its parameters take to one point
    // keeps its vertices, each with its own UV and normal, and has no
    // triangles: those with two corners there have no area. Of a grid
    // quad's two triangles, bottom left, top right, top left has two corners
    // on its left column and on its top row, and bottom left, bottom right,
    // top right on its right column and its bottom row. A face that would be
    // left with none keeps them all.
    Collapse collapse;
    if (!volume->isUnique() && sculpt_stitching == LL_SCULPT_TYPE_NONE)
    {
        collapse = collapse_of(volume, mTypeMask & INNER_MASK);
        if (mTypeMask & END_MASK)
        {
            collapse.first_column = false;
            collapse.last_column = false;
        }
    }
    const S32 upper_left_out = (collapse.first_column ? rows : 0) + (collapse.last_row ? quads : 0) - (collapse.first_column && collapse.last_row ? 1 : 0);
    const S32 lower_left_out = (collapse.last_column ? rows : 0) + (collapse.first_row ? quads : 0) - (collapse.last_column && collapse.first_row ? 1 : 0);
    S32 triangles = 2 * quads * rows - upper_left_out - lower_left_out;
    if (triangles <= 0)
    {
        collapse = Collapse();
        triangles = 2 * quads * rows;
    }
    const bool collapsed = collapse.first_row || collapse.last_row || collapse.first_column || collapse.last_column;

    num_vertices = mNumS*mNumT;
    num_indices = triangles*3;

    // createVolumeFaces builds partially only where the layout, and so the
    // counts, are the last build's.
    llassert(!partial_build || (num_vertices == mNumVertices && num_indices == mNumIndices));
    if (num_vertices != mNumVertices || num_indices != mNumIndices)
    {
        partial_build = false;
    }

    if (!partial_build)
    {
        resizeVertices(num_vertices);
        resizeIndices(num_indices);
    }

    LLVector4a* pos = (LLVector4a*) mPositions;
    LLVector2* tc = (LLVector2*) mTexCoords;

    S32 cur_vertex = 0;
    S32 end_t = mBeginT+mNumT;

    // Copy the vertices into the array.
    { LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("llvfcs - copy verts");
    const LLVector4a* mesh_points = mesh.data();
    for (t = mBeginT; t < end_t; t++)
    {
        const LLVector4a* row = mesh_points + max_s * t;
        tt = path_data[t].mTexT;
        for (S32 k = 0; k < column_count; ++k)
        {
            pos[cur_vertex] = row[columns[k].mesh];
            tc[cur_vertex].set(columns[k].s, tt);
            cur_vertex++;
        }
    }
    }

    mCenter->clear();

    LLVector4a* cur_pos = pos;
    LLVector4a* end_pos = pos + mNumVertices;

    //get bounding box for this side
    LLVector4a face_min;
    LLVector4a face_max;

    face_min = face_max = *cur_pos;
    alsimd::extents(cur_pos, end_pos - cur_pos, face_min, face_max);
    // VFExtents change
    mExtents[0] = face_min;
    mExtents[1] = face_max;

    U32 tc_count = mNumVertices;
    if (tc_count%2 == 1)
    { //odd number of texture coordinates, duplicate last entry to padded end of array
        tc_count++;
        mTexCoords[mNumVertices] = mTexCoords[mNumVertices-1];
    }

    // mTexCoords is LLVector2* into a 16-byte-aligned slab, two to a
    // vector; the kernel reads it as floats.
    LLVector4a tc_min;
    LLVector4a tc_max;
    alsimd::extents(reinterpret_cast<const LLVector4a*>(mTexCoords), tc_count / 2, tc_min, tc_max);

    F32* minp = tc_min.getF32ptr();
    F32* maxp = tc_max.getF32ptr();

    mTexCoordExtents[0].mV[0] = llmin(minp[0], minp[2]);
    mTexCoordExtents[0].mV[1] = llmin(minp[1], minp[3]);
    mTexCoordExtents[1].mV[0] = llmax(maxp[0], maxp[2]);
    mTexCoordExtents[1].mV[1] = llmax(maxp[1], maxp[3]);

    mCenter->setAdd(face_min, face_max);
    mCenter->mul(0.5f);

    S32 cur_index = 0;

    if (!partial_build)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("llvfcs - generate indices");

        // Now we generate the indices, into resizeIndices' whole 16-byte
        // vectors.
        const U16* limit = mIndices + ((mNumIndices + 7) & ~7);
        U16* end_index = mIndices;
        if (collapsed)
        {
            const Kept kept = { collapse.first_row, collapse.last_row, collapse.first_column, collapse.last_column, mNumS - 2, mNumT - 2 };
            end_index = doubled ? grid_indices<true>(mIndices, limit, mNumS, mNumT, lefts, quads, kept)
                                : grid_indices<false>(mIndices, limit, mNumS, mNumT, lefts, quads, kept);
        }
        else if (sculpt_stitching != LL_SCULPT_TYPE_NONE && !volume->isUnique() && !doubled && quads > 0 && rows > 0)
        {
            // A sculpt's triangles two of whose corners are one point have
            // no area, and are left out: the points' codes find them and the
            // points confirm them. A flexible sculpt's points move after its
            // full build, and it keeps them. A face keeps at least one
            // triangle. The flat inner side of a sculpt whose parameters
            // carry a square or triangle hole has its corners doubled, and
            // keeps all of its quads.
            ALScratchArena::Scope scratch;
            U32* codes = scratch.allocate<U32>(num_vertices + 8);
            alsimd::unit_cube_codes(pos, num_vertices, codes);
            std::fill(codes + num_vertices, codes + num_vertices + 8, 0u);
            const S32 words = (quads + 63) / 64;
            U64* upper = scratch.allocate<U64>((size_t)words * rows);
            U64* lower = scratch.allocate<U64>((size_t)words * rows);
            S32 pinched = 0;
            if (alsimd::grid_coincident_triangles(codes, mNumS, mNumT, upper, lower, words) > 0)
            {
                pinched = confirm_pinches(pos, mNumS, mNumT, upper, lower, words);
            }
            if (pinched > 0 && pinched < triangles)
            {
                end_index = grid_indices<false>(mIndices, limit, mNumS, mNumT, lefts, quads, Unpinched{ upper, lower, words });
            }
            else
            {
                end_index = grid_indices<false>(mIndices, limit, mNumS, mNumT, lefts, quads, KeepAll());
            }
        }
        else
        {
            end_index = doubled ? grid_indices<true>(mIndices, limit, mNumS, mNumT, lefts, quads, KeepAll())
                                : grid_indices<false>(mIndices, limit, mNumS, mNumT, lefts, quads, KeepAll());
        }
        cur_index = (S32)(end_index - mIndices);
        llassert(cur_index <= mNumIndices);
        mNumIndices = cur_index;
    }

    LLVector4a* norm = mNormals;

    // A prim on a path that is a function of t has the surface its side is
    // cut from, and takes that surface's normals. Its seams join nothing:
    // the surface is the same on either side of them.
    const LLPath& path = volume->getPath();
    if (sculpt_stitching == LL_SCULPT_TYPE_NONE && path.hasFrames())
    {
        mJoinsS = false;
        mJoinsT = false;
        swept_side_normals(*volume, columns, mNumS, mBeginT, mNumT, flat, pos, norm);
        return true;
    }

    // A flexible prim's path and a sculpt's map are points, and a circle2
    // path's stations alternate sides. Their normals come from the grid's
    // quads, not from the triangles the indices make of them, so they do not
    // depend on the diagonals or on the order of the triangles.
    //
    // Such a side's normals join across its first and last columns where
    // they are one seam, and across its first and last rows: a prim's where
    // they are one place in every row or column, as its own positions say; a
    // sculpt's where its stitching samples them from one column or row of the
    // map. A flat inner side's seam is one of its corners. A flexible prim's
    // path is open, and its first build, which its partial builds keep the
    // joins of, lays every row at one place before it has a pose.
    if (!partial_build)
    {
        if (sculpt_stitching == LL_SCULPT_TYPE_NONE)
        {
            mJoinsS = !doubled && columns_meet(pos, mNumS, mNumT, 0, mNumS - 1);
            mJoinsT = !mFlexible && rows_meet(pos, mNumS, 0, mNumT - 1);
        }
        else
        {
            mJoinsS = sculpt_stitching == LL_SCULPT_TYPE_SPHERE || sculpt_stitching == LL_SCULPT_TYPE_TORUS ||
                      sculpt_stitching == LL_SCULPT_TYPE_CYLINDER;
            mJoinsT = sculpt_stitching == LL_SCULPT_TYPE_TORUS;
        }
    }

    for (S32 v = 0; v < mNumVertices; ++v)
    {
        norm[v].clear();
    }

    for (t = 0; t < mNumT - 1; ++t)
    {
        const S32 row = mNumS * t;
        LLVector4a left;
        left.setSub(pos[row + mNumS], pos[row]);
        drop_rounding(left);
        for (s = 0; s < mNumS - 1; ++s)
        {
            const S32 bl = row + s;
            const S32 tl = bl + mNumS;
            LLVector4a bottom, right, top, diagonal;
            bottom.setSub(pos[bl + 1], pos[bl]);
            right.setSub(pos[tl + 1], pos[bl + 1]);
            top.setSub(pos[tl + 1], pos[tl]);
            diagonal.setSub(pos[tl + 1], pos[bl]);
            drop_rounding(bottom);
            drop_rounding(right);
            drop_rounding(top);
            add_quad_to_normals(norm, bl, bl + 1, tl, tl + 1, bottom, right, top, left, diagonal);
            left = right;
        }
    }

    if (sculpt_stitching == LL_SCULPT_TYPE_SPHERE)
    {
        // average normals for north pole

        LLVector4a average;
        average.clear();

        for (S32 i = 0; i < mNumS; i++)
        {
            average.add(norm[i]);
        }

        // set average
        for (S32 i = 0; i < mNumS; i++)
        {
            norm[i] = average;
        }

        // average normals for south pole

        average.clear();

        for (S32 i = 0; i < mNumS; i++)
        {
            average.add(norm[i + mNumS * (mNumT - 1)]);
        }

        // set average
        for (S32 i = 0; i < mNumS; i++)
        {
            norm[i + mNumS * (mNumT - 1)] = average;
        }
    }

    if (mJoinsS)
    {
        for (S32 i = 0; i < mNumT; i++)
        {
            LLVector4a n;
            n.setAdd(norm[mNumS*i], norm[mNumS*i+mNumS-1]);
            norm[mNumS * i] = n;
            norm[mNumS * i+mNumS-1] = n;
        }
    }

    if (mJoinsT)
    {
        for (S32 i = 0; i < mNumS; i++)
        {
            LLVector4a n;
            n.setAdd(norm[i], norm[mNumS*(mNumT-1)+i]);
            norm[i] = n;
            norm[mNumS*(mNumT-1)+i] = n;
        }
    }

    normalize_grid_normals(norm, mNumS, mNumT);

    return true;
}


