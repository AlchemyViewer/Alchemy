/**
 * @file llbbox.h
 * @brief General purpose bounding box class
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_BBOX_H
#define LL_BBOX_H

#include "v3math.h"
#include "llquaternion.h"

// Note: "local space" for an LLBBox is defined relative to agent space in terms of
// a translation followed by a rotation.  There is no scale term since the LLBBox's min and
// max are not necessarily symetrical and define their own extents.

class LLBBox
{
public:
    constexpr LLBBox() noexcept = default;
    constexpr LLBBox( const LLVector3& pos_agent,
        const LLQuaternion& rot,
        const LLVector3& min_local,
        const LLVector3& max_local ) noexcept
        :
        mMinLocal( min_local ), mMaxLocal( max_local ), mPosAgent(pos_agent), mRotation( rot)
        {}

    // Default copy constructor is OK.

    constexpr const LLVector3&    getPositionAgent() const noexcept     { return mPosAgent; }
    constexpr const LLQuaternion& getRotation() const noexcept          { return mRotation; }

    LLVector3           getMinAgent() const;
    constexpr const LLVector3&    getMinLocal() const noexcept           { return mMinLocal; }
    constexpr void                setMinLocal( const LLVector3& min ) noexcept { mMinLocal = min; }

    LLVector3           getMaxAgent() const;
    constexpr const LLVector3&    getMaxLocal() const noexcept           { return mMaxLocal; }
    constexpr void                setMaxLocal( const LLVector3& max ) noexcept { mMaxLocal = max; }

    constexpr LLVector3           getCenterLocal() const noexcept        { return (mMaxLocal - mMinLocal) * 0.5f + mMinLocal; }
    LLVector3           getCenterAgent() const              { return localToAgent( getCenterLocal() ); }

    constexpr LLVector3           getExtentLocal() const noexcept        { return mMaxLocal - mMinLocal; }

    bool                containsPointLocal(const LLVector3& p) const;
    bool                containsPointAgent(const LLVector3& p) const;

    void                addPointAgent(LLVector3 p);
    void                addBBoxAgent(const LLBBox& b);

    void                addPointLocal(const LLVector3& p);
    void                addBBoxLocal(const LLBBox& b) { addPointLocal( b.mMinLocal ); addPointLocal( b.mMaxLocal ); }

    void                expand( F32 delta );

    LLVector3           localToAgent( const LLVector3& v ) const;
    LLVector3           agentToLocal( const LLVector3& v ) const;

    // Changes rotation but not position
    LLVector3           localToAgentBasis(const LLVector3& v) const;
    LLVector3           agentToLocalBasis(const LLVector3& v) const;

    // Get the smallest possible axis aligned bbox that contains this bbox
    LLBBox              getAxisAligned() const;

//  friend LLBBox operator*(const LLBBox& a, const LLMatrix4& b);

private:
    LLVector3           mMinLocal {};
    LLVector3           mMaxLocal {};
    LLVector3           mPosAgent {};  // Position relative to Agent's Region
    LLQuaternion        mRotation {};
    bool                mEmpty { true };     // Nothing has been added to this bbox yet
};

static_assert(std::is_trivially_copyable<LLBBox>::value, "LLBBox must be trivial copy");
static_assert(std::is_trivially_move_assignable<LLBBox>::value, "LLBBox must be trivial move");
static_assert(std::is_standard_layout<LLBBox>::value, "LLBBox must be a standard layout type");

//LLBBox operator*(const LLBBox &a, const LLMatrix4 &b);


#endif  // LL_BBOX_H
