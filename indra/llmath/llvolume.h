/**
 * @file llvolume.h
 * @brief LLVolume base class.
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

#ifndef LL_LLVOLUME_H
#define LL_LLVOLUME_H

#include <iostream>

class LLProfileParams;
class LLPathParams;
class LLVolumeParams;
class LLProfile;
class LLPath;

template<class T> class LLPointer;
template <class T, typename T_PTR> class LLOctreeNode;

class LLVolumeFace;
class LLVolume;
class LLVolumeTriangle;
class LLVolumeOctree;

namespace ll_test
{
    struct VolumeProbe;
}

namespace alsimd
{
    struct TexTransform;
}

#include "lluuid.h"
#include "v4color.h"
#include "v2math.h"
#include "v3math.h"
#include "v3dmath.h"
#include "v4math.h"
#include "llvector4a.h"
#include "llmatrix4a.h"
#include "llquaternion.h"
#include "llstrider.h"
#include "v4coloru.h"
#include "llrefcount.h"
#include "llpointer.h"
#include "llfile.h"
#include "llalignedarray.h"
#include "llrigginginfo.h"

//============================================================================

constexpr S32 MIN_DETAIL_FACES = 6;
constexpr S32 MIN_LOD = 0;
constexpr S32 MAX_LOD = 3;

// These are defined here but are not enforced at this level,
// rather they are here for the convenience of code that uses
// the LLVolume class.
constexpr F32 MIN_VOLUME_PROFILE_WIDTH  = 0.05f;
constexpr F32 MIN_VOLUME_PATH_WIDTH     = 0.05f;

constexpr F32 CUT_QUANTA    = 0.00002f;
constexpr F32 SCALE_QUANTA  = 0.01f;
constexpr F32 SHEAR_QUANTA  = 0.01f;
constexpr F32 TAPER_QUANTA  = 0.01f;
constexpr F32 REV_QUANTA    = 0.015f;
constexpr F32 HOLLOW_QUANTA = 0.00002f;

constexpr S32 MAX_VOLUME_TRIANGLE_INDICES = 10000;

//============================================================================

// useful masks
constexpr LLPCode LL_PCODE_HOLLOW_MASK  = 0x80;     // has a thickness
constexpr LLPCode LL_PCODE_SEGMENT_MASK = 0x40;     // segments (1 angle)
constexpr LLPCode LL_PCODE_PATCH_MASK   = 0x20;     // segmented segments (2 angles)
constexpr LLPCode LL_PCODE_HEMI_MASK    = 0x10;     // half-primitives get their own type per PR's dictum
constexpr LLPCode LL_PCODE_BASE_MASK    = 0x0F;

    // primitive shapes
constexpr LLPCode   LL_PCODE_CUBE           = 1;
constexpr LLPCode   LL_PCODE_PRISM          = 2;
constexpr LLPCode   LL_PCODE_TETRAHEDRON    = 3;
constexpr LLPCode   LL_PCODE_PYRAMID        = 4;
constexpr LLPCode   LL_PCODE_CYLINDER       = 5;
constexpr LLPCode   LL_PCODE_CONE           = 6;
constexpr LLPCode   LL_PCODE_SPHERE         = 7;
constexpr LLPCode   LL_PCODE_TORUS          = 8;
constexpr LLPCode   LL_PCODE_VOLUME         = 9;

    // surfaces
//constexpr LLPCode LL_PCODE_SURFACE_TRIANGLE   = 10;
//constexpr LLPCode LL_PCODE_SURFACE_SQUARE     = 11;
//constexpr LLPCode LL_PCODE_SURFACE_DISC       = 12;

constexpr LLPCode   LL_PCODE_APP                = 14; // App specific pcode (for viewer/sim side only objects)
constexpr LLPCode   LL_PCODE_LEGACY             = 15;

// Pcodes for legacy objects
//constexpr LLPCode LL_PCODE_LEGACY_ATOR =              0x10 | LL_PCODE_LEGACY; // ATOR
constexpr LLPCode   LL_PCODE_LEGACY_AVATAR =            0x20 | LL_PCODE_LEGACY; // PLAYER
//constexpr LLPCode LL_PCODE_LEGACY_BIRD =              0x30 | LL_PCODE_LEGACY; // BIRD
//constexpr LLPCode LL_PCODE_LEGACY_DEMON =             0x40 | LL_PCODE_LEGACY; // DEMON
constexpr LLPCode   LL_PCODE_LEGACY_GRASS =             0x50 | LL_PCODE_LEGACY; // GRASS
constexpr LLPCode   LL_PCODE_TREE_NEW =                 0x60 | LL_PCODE_LEGACY; // new trees
//constexpr LLPCode LL_PCODE_LEGACY_ORACLE =            0x70 | LL_PCODE_LEGACY; // ORACLE
constexpr LLPCode   LL_PCODE_LEGACY_PART_SYS =          0x80 | LL_PCODE_LEGACY; // PART_SYS
constexpr LLPCode   LL_PCODE_LEGACY_ROCK =              0x90 | LL_PCODE_LEGACY; // ROCK
//constexpr LLPCode LL_PCODE_LEGACY_SHOT =              0xA0 | LL_PCODE_LEGACY; // BASIC_SHOT
//constexpr LLPCode LL_PCODE_LEGACY_SHOT_BIG =          0xB0 | LL_PCODE_LEGACY;
//constexpr LLPCode LL_PCODE_LEGACY_SMOKE =             0xC0 | LL_PCODE_LEGACY; // SMOKE
//constexpr LLPCode LL_PCODE_LEGACY_SPARK =             0xD0 | LL_PCODE_LEGACY;// SPARK
constexpr LLPCode   LL_PCODE_LEGACY_TEXT_BUBBLE =       0xE0 | LL_PCODE_LEGACY; // TEXTBUBBLE
constexpr LLPCode   LL_PCODE_LEGACY_TREE =              0xF0 | LL_PCODE_LEGACY; // TREE

    // hemis
constexpr LLPCode   LL_PCODE_CYLINDER_HEMI =        LL_PCODE_CYLINDER   | LL_PCODE_HEMI_MASK;
constexpr LLPCode   LL_PCODE_CONE_HEMI =            LL_PCODE_CONE       | LL_PCODE_HEMI_MASK;
constexpr LLPCode   LL_PCODE_SPHERE_HEMI =          LL_PCODE_SPHERE     | LL_PCODE_HEMI_MASK;
constexpr LLPCode   LL_PCODE_TORUS_HEMI =           LL_PCODE_TORUS      | LL_PCODE_HEMI_MASK;


// Volumes consist of a profile at the base that is swept around
// a path to make a volume.
// The profile code
constexpr U8    LL_PCODE_PROFILE_MASK       = 0x0f;
constexpr U8    LL_PCODE_PROFILE_MIN        = 0x00;
constexpr U8    LL_PCODE_PROFILE_CIRCLE     = 0x00;
constexpr U8    LL_PCODE_PROFILE_SQUARE     = 0x01;
constexpr U8    LL_PCODE_PROFILE_ISOTRI     = 0x02;
constexpr U8    LL_PCODE_PROFILE_EQUALTRI   = 0x03;
constexpr U8    LL_PCODE_PROFILE_RIGHTTRI   = 0x04;
constexpr U8    LL_PCODE_PROFILE_CIRCLE_HALF = 0x05;
constexpr U8    LL_PCODE_PROFILE_MAX        = 0x05;

// Stored in the profile byte
constexpr U8    LL_PCODE_HOLE_MASK      = 0xf0;
constexpr U8    LL_PCODE_HOLE_MIN       = 0x00;
constexpr U8    LL_PCODE_HOLE_SAME      = 0x00;     // same as outside profile
constexpr U8    LL_PCODE_HOLE_CIRCLE    = 0x10;
constexpr U8    LL_PCODE_HOLE_SQUARE    = 0x20;
constexpr U8    LL_PCODE_HOLE_TRIANGLE  = 0x30;
constexpr U8    LL_PCODE_HOLE_MAX       = 0x03;     // min/max needs to be >> 4 of real min/max

constexpr U8    LL_PCODE_PATH_IGNORE    = 0x00;
constexpr U8    LL_PCODE_PATH_MIN       = 0x01;     // min/max needs to be >> 4 of real min/max
constexpr U8    LL_PCODE_PATH_LINE      = 0x10;
constexpr U8    LL_PCODE_PATH_CIRCLE    = 0x20;
constexpr U8    LL_PCODE_PATH_CIRCLE_33 = 0x21;
constexpr U8    LL_PCODE_PATH_CIRCLE2   = 0x30;
constexpr U8    LL_PCODE_PATH_TEST      = 0x40;
constexpr U8    LL_PCODE_PATH_FLEXIBLE  = 0x80;
constexpr U8    LL_PCODE_PATH_MAX       = 0x08;

//============================================================================

// face identifiers
typedef U16 LLFaceID;

constexpr LLFaceID  LL_FACE_PATH_BEGIN      = 0x1 << 0;
constexpr LLFaceID  LL_FACE_PATH_END        = 0x1 << 1;
constexpr LLFaceID  LL_FACE_INNER_SIDE      = 0x1 << 2;
constexpr LLFaceID  LL_FACE_PROFILE_BEGIN   = 0x1 << 3;
constexpr LLFaceID  LL_FACE_PROFILE_END     = 0x1 << 4;
constexpr LLFaceID  LL_FACE_OUTER_SIDE_0    = 0x1 << 5;
constexpr LLFaceID  LL_FACE_OUTER_SIDE_1    = 0x1 << 6;
constexpr LLFaceID  LL_FACE_OUTER_SIDE_2    = 0x1 << 7;
constexpr LLFaceID  LL_FACE_OUTER_SIDE_3    = 0x1 << 8;

//============================================================================

// sculpt types + flags

constexpr U8 LL_SCULPT_TYPE_NONE      = 0;
constexpr U8 LL_SCULPT_TYPE_SPHERE    = 1;
constexpr U8 LL_SCULPT_TYPE_TORUS     = 2;
constexpr U8 LL_SCULPT_TYPE_PLANE     = 3;
constexpr U8 LL_SCULPT_TYPE_CYLINDER  = 4;
constexpr U8 LL_SCULPT_TYPE_MESH      = 5;
constexpr U8 LL_SCULPT_TYPE_GLTF      = 6;
constexpr U8 LL_SCULPT_TYPE_MAX       = LL_SCULPT_TYPE_GLTF;

constexpr U8 LL_SCULPT_TYPE_MASK      = LL_SCULPT_TYPE_SPHERE | LL_SCULPT_TYPE_TORUS | LL_SCULPT_TYPE_PLANE |
    LL_SCULPT_TYPE_CYLINDER | LL_SCULPT_TYPE_MESH | LL_SCULPT_TYPE_GLTF;

// for value checks, assign new value after adding new types

constexpr U8 LL_SCULPT_FLAG_INVERT    = 64;
constexpr U8 LL_SCULPT_FLAG_MIRROR    = 128;
constexpr U8 LL_SCULPT_FLAG_MASK = LL_SCULPT_FLAG_INVERT | LL_SCULPT_FLAG_MIRROR;

// The most faces a mesh LOD may hold: an upload splits a model into
// meshes of as many, and none of 66,000 LODs in a viewer's mesh cache held
// more. A LOD of more does not decode.
constexpr S32 LL_SCULPT_MESH_MAX_FACES = 8;

// The most indices a mesh face may hold: 32 for each of the 65,536 vertices
// its 16-bit indices reach. A large face has about six for each of its
// vertices, and the largest of 136,000 faces in a viewer's mesh cache held
// 360,564. A LOD with a face past this does not decode.
constexpr S32 LL_SCULPT_MESH_MAX_FACE_INDICES = 32 * 65536;

// The most indices a mesh LOD's faces may hold between them: two faces of
// as many as a face may hold. The largest of 66,000 LODs in a viewer's mesh
// cache held 526,422, an eighth of this. A LOD past this does not decode.
constexpr S32 LL_SCULPT_MESH_MAX_LOD_INDICES = 2 * LL_SCULPT_MESH_MAX_FACE_INDICES;

extern bool gDebugGL;

class LLProfileParams
{
public:
    LLProfileParams()
        : mCurveType(LL_PCODE_PROFILE_SQUARE),
          mBegin(0.f),
          mEnd(1.f),
          mHollow(0.f),
          mCRC(0)
    {
    }

    LLProfileParams(U8 curve, F32 begin, F32 end, F32 hollow)
        : mCurveType(curve),
          mBegin(begin),
          mEnd(end),
          mHollow(hollow),
          mCRC(0)
    {
    }

    LLProfileParams(U8 curve, U16 begin, U16 end, U16 hollow)
    {
        mCurveType = curve;
        F32 temp_f32 = begin * CUT_QUANTA;
        if (temp_f32 > 1.f)
        {
            temp_f32 = 1.f;
        }
        mBegin = temp_f32;
        temp_f32 = end * CUT_QUANTA;
        if (temp_f32 > 1.f)
        {
            temp_f32 = 1.f;
        }
        mEnd = 1.f - temp_f32;
        temp_f32 = hollow * HOLLOW_QUANTA;
        if (temp_f32 > 1.f)
        {
            temp_f32 = 1.f;
        }
        mHollow = temp_f32;
        mCRC = 0;
    }

    bool operator==(const LLProfileParams &params) const;
    bool operator!=(const LLProfileParams &params) const;
    bool operator<(const LLProfileParams &params) const;

    void copyParams(const LLProfileParams &params);

    bool importFile(LLFILE *fp);
    bool exportFile(LLFILE *fp) const;

    bool importLegacyStream(std::istream& input_stream);
    bool exportLegacyStream(std::ostream& output_stream) const;

    LLSD asLLSD() const;
    operator LLSD() const { return asLLSD(); }
    bool fromLLSD(LLSD& sd);

    const F32&  getBegin () const               { return mBegin; }
    const F32&  getEnd   () const               { return mEnd;   }
    const F32&  getHollow() const               { return mHollow; }
    const U8&   getCurveType () const           { return mCurveType; }

    void setCurveType(const U32 type)           { mCurveType = type;}
    void setBegin(const F32 begin)              { mBegin = (begin >= 1.0f) ? 0.0f : ((int) (begin * 100000))/100000.0f;}
    void setEnd(const F32 end)                  { mEnd   = (end   <= 0.0f) ? 1.0f : ((int) (end * 100000))/100000.0f;}
    void setHollow(const F32 hollow)            { mHollow = ((int) (hollow * 100000))/100000.0f;}

    friend std::ostream& operator<<(std::ostream &s, const LLProfileParams &profile_params);

protected:
    // Profile params
    U8            mCurveType;
    F32           mBegin;
    F32           mEnd;
    F32           mHollow;

    U32           mCRC;
};

inline bool LLProfileParams::operator==(const LLProfileParams &params) const
{
    return
        (getCurveType() == params.getCurveType()) &&
        (getBegin() == params.getBegin()) &&
        (getEnd() == params.getEnd()) &&
        (getHollow() == params.getHollow());
}

inline bool LLProfileParams::operator!=(const LLProfileParams &params) const
{
    return
        (getCurveType() != params.getCurveType()) ||
        (getBegin() != params.getBegin()) ||
        (getEnd() != params.getEnd()) ||
        (getHollow() != params.getHollow());
}


inline bool LLProfileParams::operator<(const LLProfileParams &params) const
{
    if (getCurveType() != params.getCurveType())
    {
        return getCurveType() < params.getCurveType();
    }
    else
    if (getBegin() != params.getBegin())
    {
        return getBegin() < params.getBegin();
    }
    else
    if (getEnd() != params.getEnd())
    {
        return getEnd() < params.getEnd();
    }
    else
    {
        return getHollow() < params.getHollow();
    }
}

#define U8_TO_F32(x) (F32)(*((S8 *)&x))

class LLPathParams
{
public:
    LLPathParams()
        :
        mCurveType(LL_PCODE_PATH_LINE),
        mBegin(0.f),
        mEnd(1.f),
        mScale(1.f,1.f),
        mShear(0.f,0.f),
        mTwistBegin(0.f),
        mTwistEnd(0.f),
        mRadiusOffset(0.f),
        mTaper(0.f,0.f),
        mRevolutions(1.f),
        mSkew(0.f),
        mCRC(0)
    {
    }

    LLPathParams(U8 curve, F32 begin, F32 end, F32 scx, F32 scy, F32 shx, F32 shy, F32 twistend, F32 twistbegin, F32 radiusoffset, F32 tx, F32 ty, F32 revolutions, F32 skew)
        : mCurveType(curve),
          mBegin(begin),
          mEnd(end),
          mScale(scx,scy),
          mShear(shx,shy),
          mTwistBegin(twistbegin),
          mTwistEnd(twistend),
          mRadiusOffset(radiusoffset),
          mTaper(tx,ty),
          mRevolutions(revolutions),
          mSkew(skew),
          mCRC(0)
    {
    }

    LLPathParams(U8 curve, U16 begin, U16 end, U8 scx, U8 scy, U8 shx, U8 shy, U8 twistend, U8 twistbegin, U8 radiusoffset, U8 tx, U8 ty, U8 revolutions, U8 skew)
    {
        mCurveType = curve;
        mBegin = (F32)(begin * CUT_QUANTA);
        mEnd = (F32)(100.f - end) * CUT_QUANTA;
        if (mEnd > 1.f)
            mEnd = 1.f;
        mScale.setVec((F32) (200 - scx) * SCALE_QUANTA,(F32) (200 - scy) * SCALE_QUANTA);
        mShear.setVec(U8_TO_F32(shx) * SHEAR_QUANTA,U8_TO_F32(shy) * SHEAR_QUANTA);
        mTwistBegin = U8_TO_F32(twistbegin) * SCALE_QUANTA;
        mTwistEnd = U8_TO_F32(twistend) * SCALE_QUANTA;
        mRadiusOffset = U8_TO_F32(radiusoffset) * SCALE_QUANTA;
        mTaper.setVec(U8_TO_F32(tx) * TAPER_QUANTA,U8_TO_F32(ty) * TAPER_QUANTA);
        mRevolutions = ((F32)revolutions) * REV_QUANTA + 1.0f;
        mSkew = U8_TO_F32(skew) * SCALE_QUANTA;

        mCRC = 0;
    }

    bool operator==(const LLPathParams &params) const;
    bool operator!=(const LLPathParams &params) const;
    bool operator<(const LLPathParams &params) const;

    void copyParams(const LLPathParams &params);

    bool importFile(LLFILE *fp);
    bool exportFile(LLFILE *fp) const;

    bool importLegacyStream(std::istream& input_stream);
    bool exportLegacyStream(std::ostream& output_stream) const;

    LLSD asLLSD() const;
    operator LLSD() const { return asLLSD(); }
    bool fromLLSD(LLSD& sd);

    const F32& getBegin() const         { return mBegin; }
    const F32& getEnd() const           { return mEnd; }
    const LLVector2 &getScale() const   { return mScale; }
    const F32& getScaleX() const        { return mScale.mV[0]; }
    const F32& getScaleY() const        { return mScale.mV[1]; }
    const LLVector2 getBeginScale() const;
    const LLVector2 getEndScale() const;
    const LLVector2 &getShear() const   { return mShear; }
    const F32& getShearX() const        { return mShear.mV[0]; }
    const F32& getShearY() const        { return mShear.mV[1]; }
    const U8& getCurveType () const     { return mCurveType; }

    const F32& getTwistBegin() const    { return mTwistBegin;   }
    const F32& getTwistEnd() const      { return mTwistEnd; }
    const F32& getTwist() const         { return mTwistEnd; }   // deprecated
    const F32& getRadiusOffset() const  { return mRadiusOffset; }
    const LLVector2 &getTaper() const   { return mTaper;        }
    const F32& getTaperX() const        { return mTaper.mV[0];  }
    const F32& getTaperY() const        { return mTaper.mV[1];  }
    const F32& getRevolutions() const   { return mRevolutions;  }
    const F32& getSkew() const          { return mSkew;         }

    void setCurveType(const U8 type)    { mCurveType = type;    }
    void setBegin(const F32 begin)      { mBegin     = begin;   }
    void setEnd(const F32 end)          { mEnd       = end; }

    void setScale(const F32 x, const F32 y)     { mScale.setVec(x,y); }
    void setScaleX(const F32 v)                 { mScale.mV[VX] = v; }
    void setScaleY(const F32 v)                 { mScale.mV[VY] = v; }
    void setShear(const F32 x, const F32 y)     { mShear.setVec(x,y); }
    void setShearX(const F32 v)                 { mShear.mV[VX] = v; }
    void setShearY(const F32 v)                 { mShear.mV[VY] = v; }

    void setTwistBegin(const F32 twist_begin)   { mTwistBegin   = twist_begin;  }
    void setTwistEnd(const F32 twist_end)       { mTwistEnd     = twist_end;    }
    void setTwist(const F32 twist)              { setTwistEnd(twist); } // deprecated
    void setRadiusOffset(const F32 radius_offset){ mRadiusOffset    = radius_offset; }
    void setTaper(const F32 x, const F32 y)     { mTaper.setVec(x,y);           }
    void setTaperX(const F32 v)                 { mTaper.mV[VX] = v;            }
    void setTaperY(const F32 v)                 { mTaper.mV[VY] = v;            }
    void setRevolutions(const F32 revolutions)  { mRevolutions  = revolutions;  }
    void setSkew(const F32 skew)                { mSkew         = skew;         }

    friend std::ostream& operator<<(std::ostream &s, const LLPathParams &path_params);

protected:
    // Path params
    U8            mCurveType;
    F32           mBegin;
    F32           mEnd;
    LLVector2     mScale;
    LLVector2     mShear;

    F32           mTwistBegin;
    F32           mTwistEnd;
    F32           mRadiusOffset;
    LLVector2     mTaper;
    F32           mRevolutions;
    F32           mSkew;

    U32           mCRC;
};

inline bool LLPathParams::operator==(const LLPathParams &params) const
{
    return
        (getCurveType() == params.getCurveType()) &&
        (getScale() == params.getScale()) &&
        (getBegin() == params.getBegin()) &&
        (getEnd() == params.getEnd()) &&
        (getShear() == params.getShear()) &&
        (getTwist() == params.getTwist()) &&
        (getTwistBegin() == params.getTwistBegin()) &&
        (getRadiusOffset() == params.getRadiusOffset()) &&
        (getTaper() == params.getTaper()) &&
        (getRevolutions() == params.getRevolutions()) &&
        (getSkew() == params.getSkew());
}

inline bool LLPathParams::operator!=(const LLPathParams &params) const
{
    return
        (getCurveType() != params.getCurveType()) ||
        (getScale() != params.getScale()) ||
        (getBegin() != params.getBegin()) ||
        (getEnd() != params.getEnd()) ||
        (getShear() != params.getShear()) ||
        (getTwist() != params.getTwist()) ||
        (getTwistBegin() !=params.getTwistBegin()) ||
        (getRadiusOffset() != params.getRadiusOffset()) ||
        (getTaper() != params.getTaper()) ||
        (getRevolutions() != params.getRevolutions()) ||
        (getSkew() != params.getSkew());
}


inline bool LLPathParams::operator<(const LLPathParams &params) const
{
    if( getCurveType() != params.getCurveType())
    {
        return getCurveType() < params.getCurveType();
    }
    else
    if( getScale() != params.getScale())
    {
        return getScale() < params.getScale();
    }
    else
    if( getBegin() != params.getBegin())
    {
        return getBegin() < params.getBegin();
    }
    else
    if( getEnd() != params.getEnd())
    {
        return getEnd() < params.getEnd();
    }
    else
    if( getShear() != params.getShear())
    {
        return getShear() < params.getShear();
    }
    else
    if( getTwist() != params.getTwist())
    {
        return getTwist() < params.getTwist();
    }
    else
    if( getTwistBegin() != params.getTwistBegin())
    {
        return getTwistBegin() < params.getTwistBegin();
    }
    else
    if( getRadiusOffset() != params.getRadiusOffset())
    {
        return getRadiusOffset() < params.getRadiusOffset();
    }
    else
    if( getTaper() != params.getTaper())
    {
        return getTaper() < params.getTaper();
    }
    else
    if( getRevolutions() != params.getRevolutions())
    {
        return getRevolutions() < params.getRevolutions();
    }
    else
    {
        return getSkew() < params.getSkew();
    }
}

typedef LLVolumeParams* LLVolumeParamsPtr;
typedef const LLVolumeParams* const_LLVolumeParamsPtr;

class LLVolumeParams
{
public:
    LLVolumeParams()
        : mSculptType(LL_SCULPT_TYPE_NONE)
    {
    }

    LLVolumeParams(LLProfileParams &profile, LLPathParams &path,
                   LLUUID sculpt_id = LLUUID::null, U8 sculpt_type = LL_SCULPT_TYPE_NONE)
        : mProfileParams(profile), mPathParams(path), mSculptID(sculpt_id), mSculptType(sculpt_type)
    {
    }

    bool operator==(const LLVolumeParams &params) const;
    bool operator!=(const LLVolumeParams &params) const;
    bool operator<(const LLVolumeParams &params) const;


    void copyParams(const LLVolumeParams &params);

    const LLProfileParams &getProfileParams() const {return mProfileParams;}
    LLProfileParams &getProfileParams() {return mProfileParams;}
    const LLPathParams &getPathParams() const {return mPathParams;}
    LLPathParams &getPathParams() {return mPathParams;}

    bool importFile(LLFILE *fp);
    bool exportFile(LLFILE *fp) const;

    bool importLegacyStream(std::istream& input_stream);
    bool exportLegacyStream(std::ostream& output_stream) const;

    LLSD sculptAsLLSD() const;
    bool sculptFromLLSD(LLSD& sd);

    LLSD asLLSD() const;
    operator LLSD() const { return asLLSD(); }
    bool fromLLSD(LLSD& sd);

    bool setType(U8 profile, U8 path);

    //void setBeginS(const F32 beginS)          { mProfileParams.setBegin(beginS); }    // range 0 to 1
    //void setBeginT(const F32 beginT)          { mPathParams.setBegin(beginT); }       // range 0 to 1
    //void setEndS(const F32 endS)              { mProfileParams.setEnd(endS); }        // range 0 to 1, must be greater than begin
    //void setEndT(const F32 endT)              { mPathParams.setEnd(endT); }           // range 0 to 1, must be greater than begin

    bool setBeginAndEndS(const F32 begin, const F32 end);           // both range from 0 to 1, begin must be less than end
    bool setBeginAndEndT(const F32 begin, const F32 end);           // both range from 0 to 1, begin must be less than end

    bool setHollow(const F32 hollow);   // range 0 to 1
    bool setRatio(const F32 x)                  { return setRatio(x,x); }           // 0 = point, 1 = same as base
    bool setShear(const F32 x)                  { return setShear(x,x); }           // 0 = no movement,
    bool setRatio(const F32 x, const F32 y);            // 0 = point, 1 = same as base
    bool setShear(const F32 x, const F32 y);            // 0 = no movement

    bool setTwistBegin(const F32 twist_begin);  // range -1 to 1
    bool setTwistEnd(const F32 twist_end);      // range -1 to 1
    bool setTwist(const F32 twist)              { return setTwistEnd(twist); }      // deprecated
    bool setTaper(const F32 x, const F32 y)     { bool pass_x = setTaperX(x); bool pass_y = setTaperY(y); return pass_x && pass_y; }
    bool setTaperX(const F32 v);                // -1 to 1
    bool setTaperY(const F32 v);                // -1 to 1
    bool setRevolutions(const F32 revolutions); // 1 to 4
    bool setRadiusOffset(const F32 radius_offset);
    bool setSkew(const F32 skew);
    bool setSculptID(const LLUUID& sculpt_id, U8 sculpt_type);

    static bool validate(U8 prof_curve, F32 prof_begin, F32 prof_end, F32 hollow,
        U8 path_curve, F32 path_begin, F32 path_end,
        F32 scx, F32 scy, F32 shx, F32 shy,
        F32 twistend, F32 twistbegin, F32 radiusoffset,
        F32 tx, F32 ty, F32 revolutions, F32 skew);

    const F32&  getBeginS()     const   { return mProfileParams.getBegin(); }
    const F32&  getBeginT()     const   { return mPathParams.getBegin(); }
    const F32&  getEndS()       const   { return mProfileParams.getEnd(); }
    const F32&  getEndT()       const   { return mPathParams.getEnd(); }

    const F32&  getHollow()     const   { return mProfileParams.getHollow(); }
    const F32&  getTwist()  const       { return mPathParams.getTwist(); }
    const F32&  getRatio()  const       { return mPathParams.getScaleX(); }
    const F32&  getRatioX()     const   { return mPathParams.getScaleX(); }
    const F32&  getRatioY()     const   { return mPathParams.getScaleY(); }
    const F32&  getShearX()     const   { return mPathParams.getShearX(); }
    const F32&  getShearY()     const   { return mPathParams.getShearY(); }

    const F32&  getTwistBegin()const    { return mPathParams.getTwistBegin();   }
    const F32&  getRadiusOffset() const { return mPathParams.getRadiusOffset(); }
    const F32&  getTaper() const        { return mPathParams.getTaperX();       }
    const F32&  getTaperX() const       { return mPathParams.getTaperX();       }
    const F32&  getTaperY() const       { return mPathParams.getTaperY();       }
    const F32&  getRevolutions() const  { return mPathParams.getRevolutions();  }
    const F32&  getSkew() const         { return mPathParams.getSkew();         }
    const LLUUID& getSculptID() const   { return mSculptID;                     }
    const U8& getSculptType() const     { return mSculptType;                   }
    bool isSculpt() const;
    bool isMeshSculpt() const;
    bool isConvex() const;

    // 'begin' and 'end' should be in range [0, 1] (they will be clamped)
    // (begin, end) = (0, 1) will not change the volume
    // (begin, end) = (0, 0.5) will reduce the volume to the first half of its profile/path (S/T)
    void reduceS(F32 begin, F32 end);
    void reduceT(F32 begin, F32 end);

    struct compare
    {
        bool operator()( const const_LLVolumeParamsPtr& first, const const_LLVolumeParamsPtr& second) const
        {
            return (*first < *second);
        }
    };

    friend std::ostream& operator<<(std::ostream &s, const LLVolumeParams &volume_params);

    // debug helper functions
    void setCube();

protected:
    LLProfileParams mProfileParams;
    LLPathParams    mPathParams;
    LLUUID mSculptID;
    U8 mSculptType;
};


class LLProfile
{
    friend class LLVolume;

public:
    LLProfile()
        : mOpen(false),
          mConcave(false),
          mDirty(true),
          mTotalOut(0),
          mTotal(2)
    {
    }

    S32  getTotal() const                               { return mTotal; }
    S32  getTotalOut() const                            { return mTotalOut; }   // Total number of outside points
    bool isFlat(S32 face) const                         { return (mFaces[face].mCount == 2); }
    bool isOpen() const                                 { return mOpen; }
    void setDirty()                                     { mDirty = true; }

    static S32 getNumPoints(const LLProfileParams& params, bool path_open, F32 detail = 1.0f, S32 split = 0,
                  bool is_sculpted = false, S32 sculpt_size = 0);
    bool generate(const LLProfileParams& params, bool path_open, F32 detail = 1.0f, S32 split = 0,
                  bool is_sculpted = false, S32 sculpt_size = 0);
    bool isConcave() const                              { return mConcave; }
    // Whether the hole was drawn in where it would have gone through the
    // outer wall, and so touches it there.
    bool holeTouchesWall() const                        { return mHoleTouchesWall; }

    // The tangent in the profile's plane at point i, the way the points'
    // indices run, of no length in particular: its circle's at the point's
    // angle, which is the point turned a quarter turn about the centre, the
    // other way round along a hole. A cut point lies on the chord of its
    // step, and genNGon keeps the circle's at the cut's angle for it. The
    // centre of a cut profile has none, and a flat profile's points have
    // the circle's they were laid out on, which only a round side reads.
    LLVector4a getTangent(S32 i) const
    {
        for (S32 c = 0; c < mCutCount; ++c)
        {
            if (mCutPoints[c] == i)
            {
                return mCutTangents[c];
            }
        }
        const LLVector4a& point = mProfile[i];
        return i >= mHoleBegin ? LLVector4a(point[1], -point[0], 0.f) : LLVector4a(-point[1], point[0], 0.f);
    }
public:
    struct Face
    {
        S32       mIndex;
        S32       mCount;
        F32       mScaleU;
        bool      mCap;
        bool      mFlat;
        LLFaceID  mFaceID;
        // A flat hole's corners, where its side folds: of its points between
        // its two ends, mFirstCorner and every mCornerStep-th after it. The
        // rest are split points, on the straight edge between two corners.
        // A step of 0 marks none.
        S32       mFirstCorner = 0;
        S32       mCornerStep = 0;

        bool isCorner(S32 point) const
        {
            return mCornerStep > 0 && point > 0 && point + 1 < mCount && point >= mFirstCorner && (point - mFirstCorner) % mCornerStep == 0;
        }
    };

    LLAlignedArray<LLVector4a, 64> mProfile;
    //LLAlignedArray<LLVector4a, 64> mNormals;
    std::vector<Face>      mFaces;

    //LLAlignedArray<LLVector4a, 64> mEdgeNormals;
    //LLAlignedArray<LLVector4a, 64> mEdgeCenters;

    friend std::ostream& operator<<(std::ostream &s, const LLProfile &profile);

protected:
    ~LLProfile();

    static S32 getNumNGonPoints(const LLProfileParams& params, S32 sides, F32 offset=0.0f, F32 bevel = 0.0f, F32 ang_scale = 1.f, S32 split = 0);
    // Returns whether it began with the point of the begin cut, which it
    // leaves out where the cut falls just short of a step.
    bool genNGon(const LLProfileParams& params, S32 sides, F32 offset=0.0f, F32 bevel = 0.0f, F32 ang_scale = 1.f, S32 split = 0);

    Face* addHole(const LLProfileParams& params, bool flat, F32 sides, F32 offset, F32 box_hollow, F32 ang_scale, S32 split = 0);
    Face* addCap (S16 faceID);
    Face* addFace(S32 index, S32 count, F32 scaleU, S16 faceID, bool flat);

protected:
    bool          mOpen;
    bool          mConcave;
    bool          mDirty;
    bool          mHoleTouchesWall = false;

    S32           mTotalOut;
    S32           mTotal;

    // The cut points that fall between two steps, of the profile and of its
    // hole, and their tangents; and the first of the hole's points, or
    // past the last point where there is none.
    S32           mCutPoints[4] = { -1, -1, -1, -1 };
    LLVector4a    mCutTangents[4];
    S32           mCutCount = 0;
    S32           mHoleBegin = S32_MAX;
};

//-------------------------------------------------------------------
// SWEEP/EXTRUDE PATHS
//-------------------------------------------------------------------

class LLPath
{
public:
    // A station's frame for the surface its profile sweeps out: the rows
    // its profile's x and y are scaled and turned by, M, how fast they
    // change along the path, M', and how fast the station moves, per unit
    // of its t. Five vectors one after another, as alsimd::swept_normals
    // reads a row's.
    struct PathFrame
    {
        LLVector4a mRow[2];
        LLVector4a mRate[2];
        LLVector4a mVelocity;
    };

    class PathPt
    {
    public:
        LLMatrix4a   mRot;
        LLVector4a   mPos;

        LLVector4a   mScale;
        F32          mTexT;
        F32 pad[3]; //for alignment
        // Where the path has frames (see hasFrames).
        PathFrame    mFrame;
        PathPt()
        {
            mPos.clear();
            mTexT = 0;
            mScale.clear();
            mRot.setRows(LLVector4a(1,0,0,0),
                        LLVector4a(0,1,0,0),
                        LLVector4a(0,0,1,0));

            //distinguished data in the pad for debugging
            pad[0] = 3.14159f;
            pad[1] = -3.14159f;
            pad[2] = 0.585f;
        }
    };

public:
    LLPath()
        : mOpen(false),
          mTotal(0),
          mDirty(true),
          mStep(1)
    {
    }

    virtual ~LLPath();

    static S32 getNumPoints(const LLPathParams& params, F32 detail);
    static S32 getNumNGonPoints(const LLPathParams& params, S32 sides, F32 offset=0.0f, F32 end_scale = 1.f, F32 twist_scale = 1.f);

    void genNGon(const LLPathParams& params, S32 sides, F32 offset=0.0f, F32 end_scale = 1.f, F32 twist_scale = 1.f,
                 bool is_sculpted = false);
    virtual bool generate(const LLPathParams& params, F32 detail=1.0f, S32 split = 0,
                          bool is_sculpted = false, S32 sculpt_size = 0);

    bool isOpen() const                     { return mOpen; }
    F32 getStep() const                     { return mStep; }
    void setDirty()                         { mDirty = true; }

    S32 getPathLength() const               { return (S32)mPath.size(); }

    void resizePath(S32 length) { mPath.resize(length); }

    // Whether its stations have frames: a path that is a function of t, a
    // line or a circle. A circle2 path's stations alternate sides, and a
    // flexible or sculpted one's normals come from its points.
    bool hasFrames() const                  { return mHasFrames; }
    // Whether every station's frame is the first's: a line that neither
    // twists nor scales.
    bool framesRepeat() const               { return mFramesRepeat; }
    // Whether every station's frame is the first's turned as the station's
    // rotation turns it from the first's: a line that neither scales nor
    // shears, twisted or not.
    bool framesTurn() const                 { return mFramesTurn; }

    friend std::ostream& operator<<(std::ostream &s, const LLPath &path);

public:
    LLAlignedArray<PathPt, 64> mPath;

protected:
    bool          mOpen;
    S32           mTotal;
    bool          mDirty;
    F32           mStep;
    bool          mHasFrames = false;
    bool          mFramesRepeat = false;
    bool          mFramesTurn = false;
};

class LLDynamicPath : public LLPath
{
public:
    LLDynamicPath() : LLPath() { }
    /*virtual*/ bool generate(const LLPathParams& params, F32 detail=1.0f, S32 split = 0,
                              bool is_sculpted = false, S32 sculpt_size = 0);
};

// Yet another "face" class - caches volume-specific, but not instance-specific data for faces)
class LLVolumeFace
{
public:
    // A vertex as the importers pass it around: position, normal and texture
    // coordinate in one block, so a vector of them is one allocation.
    class VertexData
    {
    public:
        LLVector4a& getPosition() { return mPosition; }
        LLVector4a& getNormal() { return mNormal; }
        const LLVector4a& getPosition() const { return mPosition; }
        const LLVector4a& getNormal() const { return mNormal; }
        void setPosition(const LLVector4a& pos) { mPosition = pos; }
        void setNormal(const LLVector4a& norm) { mNormal = norm; }

        LLVector2 mTexCoord;

        bool operator<(const VertexData& rhs) const;
        bool operator==(const VertexData& rhs) const;

    private:
        LLVector4a mPosition;
        LLVector4a mNormal;
    };

    // Welds vertices equal in position, normal and texture coordinate and
    // drops the ones no index uses. destination_vertices needs room for
    // vertex_count vertices and destination_indices for index_count indices.
    // Returns the number of vertices written.
    static size_t weldVertices(VertexData* destination_vertices, U32* destination_indices,
                               const VertexData* vertices, size_t vertex_count,
                               const U32* indices, size_t index_count);

    LLVolumeFace();
    LLVolumeFace(const LLVolumeFace& src);
    LLVolumeFace& operator=(const LLVolumeFace& rhs);

    ~LLVolumeFace();
private:
    void freeData();
public:

    bool create(LLVolume* volume, bool partial_build = false);
    void createTangents();

    void resizeVertices(S32 num_verts);
    void allocateTangents(S32 num_verts);
    void allocateWeights(S32 num_verts);
    void allocateJointIndices(S32 num_verts);
    void resizeIndices(S32 num_indices);
    void fillFromLegacyData(std::vector<LLVolumeFace::VertexData>& v, std::vector<U16>& idx);

    class VertexMapData : public LLVolumeFace::VertexData
    {
    public:
        U16 mIndex;

        bool operator==(const LLVolumeFace::VertexData& rhs) const;

        struct ComparePosition
        {
            bool operator()(const LLVector3& a, const LLVector3& b) const;
        };

        typedef std::map<LLVector3, std::vector<VertexMapData>, VertexMapData::ComparePosition > PointMap;
    };

    // Eliminates non unique triangles, takes positions,
    // normals and texture coordinates into account.
    void remap();

    bool cacheOptimize(bool gen_tangents = false);

    // Normals made from the face's shape: smooth across an edge whose two
    // triangles meet at less than crease_angle radians and hard across one
    // where they meet at more, evened out over their neighbours by smoothing
    // passes. Returns false, leaving the face as it was, where it has no
    // triangle or would need more vertices than 16-bit indices reach.
    bool generateNormals(F32 crease_angle, F32 smoothing);

    void createOctree(F32 scaler = 0.25f, const LLVector4a& center = LLVector4a(0,0,0), const LLVector4a& size = LLVector4a(0.5f,0.5f,0.5f));
    void destroyOctree();
    // Get a reference to the octree, which may be null
    const LLVolumeOctree* getOctree() const;

    // Planar texture generation projects a point, in the face's volume
    // space at its object's scale, along a normal, by
    // alsimd::planar_texcoords: the binormal is y where the normal's x is
    // at least PLANAR_SWITCH_X in size and x where it is not.
    //
    // A face whose vertex normals all lie within 60 degrees of its planar
    // normal, their cosines past PLANAR_CONE_COS by PLANAR_CONE_TIE, is
    // projected along that normal at every point, as one plane: normals on
    // the cone's edge, as a half cylinder's last ones at LOD 0, are outside
    // it however rounding falls.
    // Any other face is projected along the normal at each point, where an
    // x within PLANAR_TIE below the switch counts as reaching it, so that
    // the normals a round prim puts on the switch go one way on every row
    // and at every LOD.
    static constexpr F32 PLANAR_SWITCH_X = 0.5f;
    static constexpr F32 PLANAR_TIE = 1e-4f;
    static constexpr F32 PLANAR_CONE_COS = 0.5f;
    static constexpr F32 PLANAR_CONE_TIE = 1e-4f;

    // The planar normal: in xyz the vertices' one normal where they all
    // have the same, and otherwise the sum of the triangles' normals
    // weighted by their areas, normalized; in w 1 where the face is
    // projected as one plane and -1 where it is not. Made on the first call
    // and kept until a build other than a flexible prim's partial one: its
    // frames keep their first build's planar texture coordinates. A face
    // with no area keeps nothing, and its planar normal is made again at
    // each call: a flexible prim's sides have none before its first pose.
    // Main thread only.
    LLVector4a getPlanarNormal() const
    {
        LLVector4a planar;
        planar.loadua(mPlanarNormal);
        return planar[3] != 0.f ? planar : makePlanarNormal();
    }

    // Every vertex's planar texture coordinate, the positions taken at
    // scale, through transform where one is given: two floats a vertex.
    void getPlanarTexCoords(const LLVector4a& scale, const alsimd::TexTransform* transform, F32* dst) const;

    // The planar texture coordinate of a point of the face, in volume space
    // at its object's scale, whose normal is normal.
    LLVector2 getPlanarTexCoord(const LLVector4a& position, const LLVector4a& normal, const alsimd::TexTransform* transform = nullptr) const;

    // Every vertex's tangent for those coordinates, in volume space, by
    // alsimd::planar_tangents: what the tangent generators give for UVs that
    // are the planar coordinates, so that a normal map planar texgen lays
    // out shades by its own frame. One vector a vertex, 16-byte aligned.
    void getPlanarTangents(const LLVector4a& scale, LLVector4a* dst) const;

    // The same where normal is, for a point of the face.
    LLVector4a getPlanarTangent(const LLVector4a& normal, const LLVector4a& scale) const;

    // The binormal planar texgen takes along normal with the switch at
    // switch_x, and the tangent, binormal x normal.
    static void getPlanarBasis(const LLVector4a& normal, F32 switch_x, LLVector4a& binormal, LLVector4a& tangent);

    enum
    {
        SINGLE_MASK =   0x0001,
        CAP_MASK =      0x0002,
        END_MASK =      0x0004,
        SIDE_MASK =     0x0008,
        INNER_MASK =    0x0010,
        OUTER_MASK =    0x0020,
        HOLLOW_MASK =   0x0040,
        OPEN_MASK =     0x0080,
        FLAT_MASK =     0x0100,
        TOP_MASK =      0x0200,
        BOTTOM_MASK =   0x0400
    };

public:
    S32 mID;
    U32 mTypeMask;

    // Only used for INNER/OUTER faces
    S32 mBeginS;
    S32 mBeginT;
    S32 mNumS;
    S32 mNumT;

    // Whether a side's first and last columns, and its first and last rows,
    // are one seam whose normals are joined. A full build decides them and a
    // partial build keeps them.
    bool mJoinsS = false;
    bool mJoinsT = false;

    // Whether a cap's triangles wind against the way its profile runs, so
    // that it faces away from the wall it closes where that wall lies on the
    // side its profile's winding faces. A full build decides it and a
    // partial build keeps it.
    bool mReversed = false;

    // Whether the face is a flexible prim's. Its tangents are then the cheap
    // ones, on every build, so that they do not change in kind between its
    // first build and the partial builds of its frames: along a side's rows
    // by central difference, and over a cap the one tangent its end
    // station's frame gives, which every build of the cap sets here.
    bool mFlexible = false;
    F32 mCapTangent[4] = { 1.f, 0.f, 0.f, 1.f };

    LLVector4a* mExtents; //minimum and maximum point of face
    LLVector4a* mCenter;
    LLVector2   mTexCoordExtents[2]; //minimum and maximum of texture coordinates of the face.

    S32 mNumVertices; // num vertices == num normals == num texcoords
    S32 mNumAllocatedVertices;
    S32 mNumIndices;

    LLVector4a* mPositions; // Contains vertices, nortmals and texcoords
    LLVector4a* mNormals; // pointer into mPositions
    LLVector4a* mTangents;
    LLVector2*  mTexCoords; // pointer into mPositions

    // mIndices contains mNumIndices amount of elements.
    // It contains triangles, each 3 indices describe one triangle.
    // If mIndices contains {0, 2, 3, 1, 2, 4}, it means there
    // are two triangles {0, 2, 3} and {1, 2, 4} with values being
    // indexes for mPositions/mNormals/mTexCoords
    U16* mIndices;

    //list of skin weights for rigged volumes
    // format is mWeights[vertex_index].mV[influence] = <joint_index>.<weight>
    // mWeights.size() should be empty or match mVertices.size()
    LLVector4a* mWeights;

#if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
    LLVector4a* mJustWeights;
    U8* mJointIndices;
#endif

    mutable bool mWeightsScrubbed;

    // Which joints are rigged to, and the bounding box of any rigged
    // vertices per joint.
    LLJointRiggingInfoTab mJointRiggingInfoTab;

    //whether or not face has been cache optimized
    bool mOptimized;

    // if this is a mesh asset, scale and translation that were applied
    // when encoding the source mesh into a unit cube
    // used for regenerating tangents
    LLVector3 mNormalizedScale = LLVector3(1,1,1);

private:
    LLVolumeOctree* mOctree;
    LLVolumeTriangle* mOctreeTriangles;

    // The tangents were made for positions a partial build has since moved:
    // createTangents makes them again, in the buffer they have.
    bool mTangentsStale = false;

    // getPlanarNormal's, w 0 until it is made.
    mutable F32 mPlanarNormal[4] = { 0.f, 0.f, 0.f, 0.f };
    LLVector4a makePlanarNormal() const;

    bool createUnCutCubeCap(LLVolume* volume, bool partial_build = false);
    bool createCap(LLVolume* volume, bool partial_build = false);
    bool createSide(LLVolume* volume, bool partial_build = false);
};

class LLVolume : public LLRefCount
{
    friend class LLVolumeLODGroup;
    friend struct ll_test::VolumeProbe;

protected:
    virtual ~LLVolume(); // use unref

public:
    typedef std::vector<LLVolumeFace> face_list_t;

    struct FaceParams
    {
        LLFaceID mFaceID;
        S32 mBeginS;
        S32 mCountS;
        S32 mBeginT;
        S32 mCountT;
    };

    LLVolume(const LLVolumeParams &params, const F32 detail, const bool generate_single_face = false, const bool is_unique = false);

    U8 getProfileType() const                               { return mParams.getProfileParams().getCurveType(); }
    U8 getPathType() const                                  { return mParams.getPathParams().getCurveType(); }
    S32 getNumFaces() const;
    S32 getNumVolumeFaces() const                           { return static_cast<S32>(mVolumeFaces.size()); }
    F32 getDetail() const                                   { return mDetail; }
    F32 getSurfaceArea() const                              { return mSurfaceArea; }
    const LLVolumeParams& getParams() const                 { return mParams; }
    LLVolumeParams getCopyOfParams() const                  { return mParams; }
    const LLProfile& getProfile() const                     { return *mProfilep; }
    LLPath& getPath() const                                 { return *mPathp; }
    void resizePath(S32 length);
    const LLAlignedArray<LLVector4a,64>&    getMesh() const             { return mMesh; }
    const LLVector4a& getMeshPt(const U32 i) const          { return mMesh[i]; }


    void setDirty() { mPathp->setDirty(); mProfilep->setDirty(); }

    void regen();
    void genTangents(S32 face);

    bool isConvex() const;
    bool isCap(S32 face);
    bool isFlat(S32 face);
    bool isUnique() const                                   { return mUnique; }

    // Whether generate() builds the same profile, path and faces from these
    // parameters at every detail, so that one volume serves every LOD.
    static bool isLODInvariant(const LLVolumeParams& params);
    // Whether this volume is shared and its parameters are LOD-invariant.
    bool isLODInvariant() const                             { return mLODInvariant; }

    S32 getSculptLevel() const                              { return mSculptLevel; }
    void setSculptLevel(S32 level)                          { mSculptLevel = level; }

    // Whether the sculpt shows the sphere it shows while its map is missing
    // or has no area, which is laid out and stitched as a sphere sculpt is,
    // whatever its own stitching.
    bool isSculptSpherePlaceholder() const                  { return mSculptSpherePlaceholder; }
    // The pixels of the map the last sculpt() was given: 0 where it had none.
    S32 getSculptPixels() const                             { return mSculptPixels; }


    static void getLoDTriangleCounts(const LLVolumeParams& params, S32* counts);

    S32 getNumTriangles(S32* vcount = nullptr) const;

    //get the face index of the face that intersects with the given line segment at the point
    //closest to start.  Moves end to the point of intersection.  Returns -1 if no intersection.
    //Line segment must be in volume space.
    S32 lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end,
                             S32 face = -1,                          // which face to check, -1 = ALL_SIDES
                             LLVector4a* intersection = nullptr,     // return the intersection point
                             LLVector2* tex_coord = nullptr,         // return the texture coordinates of the intersection point
                             LLVector4a* normal = nullptr,           // return the surface normal at the intersection point
                             LLVector4a* tangent = nullptr           // return the surface tangent at the intersection point
        );

    LLFaceID generateFaceMask();

    bool isFaceMaskValid(LLFaceID face_mask);

    friend std::ostream& operator<<(std::ostream &s, const LLVolume &volume);
    friend std::ostream& operator<<(std::ostream &s, const LLVolume *volumep);      // HACK to bypass Windoze confusion over
                                                                                // conversion if *(LLVolume*) to LLVolume&
    const LLVolumeFace &getVolumeFace(const S32 f) const {return mVolumeFaces[f];} // DO NOT DELETE VOLUME WHILE USING THIS REFERENCE, OR HOLD A POINTER TO THIS VOLUMEFACE

    LLVolumeFace &getVolumeFace(const S32 f) {return mVolumeFaces[f];} // DO NOT DELETE VOLUME WHILE USING THIS REFERENCE, OR HOLD A POINTER TO THIS VOLUMEFACE

    face_list_t& getVolumeFaces() { return mVolumeFaces; }

    U32                 mFaceMask;          // bit array of which faces exist in this volume
    LLVector3           mLODScaleBias;      // vector for biasing LOD based on scale

    void sculpt(U16 sculpt_width, U16 sculpt_height, S8 sculpt_components, const U8* sculpt_data, S32 sculpt_level, bool visible_placeholder);
    void copyVolumeFaces(const LLVolume* volume);
    void copyFacesTo(std::vector<LLVolumeFace> &faces) const;
    void copyFacesFrom(const std::vector<LLVolumeFace> &faces);

    // Each face's vertices and triangles in the order a vertex cache and a
    // fetch take them best, with MikkTSpace tangents where gen_tangents is
    // set; see LLVolumeFace::cacheOptimize.
    bool cacheOptimize(bool gen_tangents = false);

private:
    void sculptGenerateMapVertices(U16 sculpt_width, U16 sculpt_height, S8 sculpt_components, const U8* sculpt_data, U8 sculpt_type);
    F32 sculptGetSurfaceArea();
    void sculptGenerateEmptyPlaceholder();
    void sculptGenerateSpherePlaceholder();

protected:
    bool generate();
    void createVolumeFaces();
public:
    bool unpackVolumeFaces(std::istream& is, S32 size);
    bool unpackVolumeFaces(U8* in_data, S32 size);
private:
    bool unpackVolumeFacesInternal(const LLSD& mdl);
    // A LOD block's faces as it holds them, before cacheOptimize.
    bool decodeVolumeFaces(const LLSD& mdl);

public:
    virtual void setMeshAssetLoaded(bool loaded);
    virtual bool isMeshAssetLoaded() const;
    virtual void setMeshAssetUnavaliable(bool unavaliable);
    virtual bool isMeshAssetUnavaliable() const;

 protected:
    bool mUnique;
    bool mLODInvariant;
    F32 mDetail;
    S32 mSculptLevel;
    bool mSculptSpherePlaceholder = false;
    S32 mSculptPixels = 0;
    F32 mSurfaceArea; //unscaled surface area
    bool mIsMeshAssetLoaded;
    bool mIsMeshAssetUnavaliable;

    const LLVolumeParams mParams;
    LLPath *mPathp;
    LLProfile *mProfilep;
    LLAlignedArray<LLVector4a,64> mMesh;


    bool mGenerateSingleFace;
    face_list_t mVolumeFaces;

public:
    LLVector4a* mHullPoints;
    U16* mHullIndices;
    S32 mNumHullPoints;
    S32 mNumHullIndices;
};

std::ostream& operator<<(std::ostream &s, const LLVolumeParams &volume_params);

bool LLLineSegmentBoxIntersect(const F32* start, const F32* end, const F32* center, const F32* size);
bool LLLineSegmentBoxIntersect(const LLVector3& start, const LLVector3& end, const LLVector3& center, const LLVector3& size);
bool LLLineSegmentBoxIntersect(const LLVector4a& start, const LLVector4a& end, const LLVector4a& center, const LLVector4a& size);

bool LLTriangleRayIntersect(const LLVector4a& vert0, const LLVector4a& vert1, const LLVector4a& vert2, const LLVector4a& orig, const LLVector4a& dir,
                            F32& intersection_a, F32& intersection_b, F32& intersection_t);
bool LLTriangleRayIntersectTwoSided(const LLVector4a& vert0, const LLVector4a& vert1, const LLVector4a& vert2, const LLVector4a& orig, const LLVector4a& dir,
                            F32& intersection_a, F32& intersection_b, F32& intersection_t);

#endif
