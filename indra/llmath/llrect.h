/**
 * @file llrect.h
 * @brief A rectangle in GL coordinates, with bottom,left = 0,0
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


#ifndef LL_LLRECT_H
#define LL_LLRECT_H

#include <iostream>
#include "llmath.h"
#include "llsd.h"

// Top > Bottom due to GL coords
template <class Type> class LLRectBase
{
public:
    typedef Type tCoordType;
    Type        mLeft   { 0 };
    Type        mTop    { 0 };
    Type        mRight  { 0 };
    Type        mBottom { 0 };

    // Note: follows GL_QUAD conventions: the top and right edges are not considered part of the rect
    constexpr Type getWidth()   const noexcept { return mRight - mLeft; }
    constexpr Type getHeight()  const noexcept { return mTop - mBottom; }
    constexpr Type getCenterX() const noexcept { return (mLeft + mRight) / 2; }
    constexpr Type getCenterY() const noexcept { return (mTop + mBottom) / 2; }

    constexpr LLRectBase() noexcept = default;

    constexpr LLRectBase(Type left, Type top, Type right, Type bottom) noexcept:
    mLeft(left), mTop(top), mRight(right), mBottom(bottom)
    {}

    explicit LLRectBase(const LLSD& sd)
    {
        setValue(sd);
    }

    void setValue(const LLSD& sd)
    {
        mLeft = (Type)sd[0].asInteger();
        mTop = (Type)sd[1].asInteger();
        mRight = (Type)sd[2].asInteger();
        mBottom = (Type)sd[3].asInteger();
    }

    LLSD getValue() const
    {
        LLSD ret;
        ret[0] = mLeft;
        ret[1] = mTop;
        ret[2] = mRight;
        ret[3] = mBottom;
        return ret;
    }

    // Note: follows GL_QUAD conventions: the top and right edges are not considered part of the rect
    constexpr bool pointInRect(const Type x, const Type y) const noexcept
    {
        return  mLeft <= x && x < mRight &&
                mBottom <= y && y < mTop;
    }

    //// Note: follows GL_QUAD conventions: the top and right edges are not considered part of the rect
    constexpr bool localPointInRect(const Type x, const Type y) const noexcept
    {
        return  0 <= x && x < getWidth() &&
                0 <= y && y < getHeight();
    }

    constexpr void clampPointToRect(Type& x, Type& y) noexcept
    {
        x = llclamp(x, mLeft, mRight);
        y = llclamp(y, mBottom, mTop);
    }

    constexpr void clipPointToRect(const Type start_x, const Type start_y, Type& end_x, Type& end_y) noexcept
    {
        if (!pointInRect(start_x, start_y))
        {
            return;
        }
        Type clip_x = 0;
        Type clip_y = 0;
        Type delta_x = end_x - start_x;
        Type delta_y = end_y - start_y;
        if (end_x > mRight) clip_x = end_x - mRight;
        if (end_x < mLeft) clip_x = end_x - mLeft;
        if (end_y > mTop) clip_y = end_y - mTop;
        if (end_y < mBottom) clip_y = end_y - mBottom;
        // clip_? and delta_? should have same sign, since starting point is in rect
        // so ratios will be positive
        F32 ratio_x = 0;
        F32 ratio_y = 0;
        if (delta_x != 0) ratio_x = ((F32)clip_x / (F32)delta_x);
        if (delta_y != 0) ratio_y = ((F32)clip_y / (F32)delta_y);
        if (ratio_x > ratio_y)
        {
            // clip along x direction
            end_x -= (Type)(clip_x);
            end_y -= (Type)(delta_y * ratio_x);
        }
        else
        {
            // clip along y direction
            end_x -= (Type)(delta_x * ratio_y);
            end_y -= (Type)clip_y;
        }
    }

    // Note: Does NOT follow GL_QUAD conventions: the top and right edges ARE considered part of the rect
    // returns true if any part of rect is is inside this LLRect
    constexpr bool overlaps(const LLRectBase& rect) const noexcept
    {
        return !(mLeft > rect.mRight
            || mRight < rect.mLeft
            || mBottom > rect.mTop
            || mTop < rect.mBottom);
    }

    constexpr bool contains(const LLRectBase& rect) const noexcept
    {
        return mLeft <= rect.mLeft
            && mRight >= rect.mRight
            && mBottom <= rect.mBottom
            && mTop >= rect.mTop;
    }

    constexpr LLRectBase& set(Type left, Type top, Type right, Type bottom) noexcept
    {
        mLeft = left;
        mTop = top;
        mRight = right;
        mBottom = bottom;
        return *this;
    }

    // Note: follows GL_QUAD conventions: the top and right edges are not considered part of the rect
    constexpr LLRectBase& setOriginAndSize( Type left, Type bottom, Type width, Type height) noexcept
    {
        mLeft = left;
        mTop = bottom + height;
        mRight = left + width;
        mBottom = bottom;
        return *this;
    }

    // Note: follows GL_QUAD conventions: the top and right edges are not considered part of the rect
    constexpr LLRectBase& setLeftTopAndSize( Type left, Type top, Type width, Type height) noexcept
    {
        mLeft = left;
        mTop = top;
        mRight = left + width;
        mBottom = top - height;
        return *this;
    }

    constexpr LLRectBase& setCenterAndSize(Type x, Type y, Type width, Type height) noexcept
    {
        // width and height could be odd, so favor top, right with extra pixel
        mLeft = x - width/2;
        mBottom = y - height/2;
        mTop = mBottom + height;
        mRight = mLeft + width;
        return *this;
    }


    constexpr LLRectBase& translate(Type horiz, Type vertical) noexcept
    {
        mLeft += horiz;
        mRight += horiz;
        mTop += vertical;
        mBottom += vertical;
        return *this;
    }

    constexpr LLRectBase& stretch( Type dx, Type dy) noexcept
    {
        mLeft -= dx;
        mRight += dx;
        mTop += dy;
        mBottom -= dy;
        return makeValid();
    }

    constexpr LLRectBase& stretch( Type delta ) noexcept
    {
        stretch(delta, delta);
        return *this;
    }

    constexpr LLRectBase& makeValid() noexcept
    {
        mLeft = llmin(mLeft, mRight);
        mBottom = llmin(mBottom, mTop);
        return *this;
    }

    constexpr bool isValid() const noexcept
    {
        return mLeft <= mRight && mBottom <= mTop;
    }

    constexpr bool isEmpty() const noexcept
    {
        return mLeft == mRight || mBottom == mTop;
    }

    constexpr bool notEmpty() const noexcept
    {
        return !isEmpty();
    }

    constexpr void unionWith(const LLRectBase &other) noexcept
    {
        mLeft = llmin(mLeft, other.mLeft);
        mRight = llmax(mRight, other.mRight);
        mBottom = llmin(mBottom, other.mBottom);
        mTop = llmax(mTop, other.mTop);
    }

    constexpr void intersectWith(const LLRectBase &other) noexcept
    {
        mLeft = llmax(mLeft, other.mLeft);
        mRight = llmin(mRight, other.mRight);
        mBottom = llmax(mBottom, other.mBottom);
        mTop = llmin(mTop, other.mTop);
        if (mLeft > mRight)
        {
            mLeft = mRight;
        }
        if (mBottom > mTop)
        {
            mBottom = mTop;
        }
    }

    friend std::ostream &operator<<(std::ostream &s, const LLRectBase &rect)
    {
        s << "{ L " << rect.mLeft << " B " << rect.mBottom
            << " W " << rect.getWidth() << " H " << rect.getHeight() << " }";
        return s;
    }

    constexpr bool operator==(const LLRectBase &b) const noexcept
    {
        return ((mLeft == b.mLeft) &&
                (mTop == b.mTop) &&
                (mRight == b.mRight) &&
                (mBottom == b.mBottom));
    }

    constexpr bool operator!=(const LLRectBase &b) const noexcept
    {
        return ((mLeft != b.mLeft) ||
                (mTop != b.mTop) ||
                (mRight != b.mRight) ||
                (mBottom != b.mBottom));
    }

    static const LLRectBase<Type> null;
};

template <class Type> inline constexpr LLRectBase<Type> LLRectBase<Type>::null{};

typedef LLRectBase<S32> LLRect;
typedef LLRectBase<F32> LLRectf;

static_assert(std::is_trivially_copyable<LLRect>::value, "LLRect must be trivial copy");
static_assert(std::is_trivially_move_assignable<LLRect>::value, "LLRect must be trivial move");
static_assert(std::is_standard_layout<LLRect>::value, "LLRect must be a standard layout type");

#endif
