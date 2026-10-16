/**
 * @file llcoord.h
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

#ifndef LL_LLCOORD_H
#define LL_LLCOORD_H

template<typename> class LLCoord;
struct LL_COORD_TYPE_GL;
struct LL_COORD_TYPE_WINDOW;
struct LL_COORD_TYPE_SCREEN;

typedef LLCoord<LL_COORD_TYPE_GL> LLCoordGL;
typedef LLCoord<LL_COORD_TYPE_WINDOW> LLCoordWindow;
typedef LLCoord<LL_COORD_TYPE_SCREEN> LLCoordScreen;

struct LLCoordCommon
{
    constexpr LLCoordCommon(S32 x, S32 y) noexcept : mX(x), mY(y) {}
    constexpr LLCoordCommon() noexcept = default;
    S32 mX { 0 };
    S32 mY { 0 };
};

// A two-dimensional pixel value
template<typename COORD_FRAME>
class LLCoord : protected COORD_FRAME
{
public:
    typedef LLCoord<COORD_FRAME> self_t;
    typename COORD_FRAME::value_t   mX { 0 };
    typename COORD_FRAME::value_t   mY { 0 };

    constexpr LLCoord() noexcept = default;
    constexpr LLCoord(typename COORD_FRAME::value_t x, typename COORD_FRAME::value_t y) noexcept : mX(x), mY(y)
    {}

    LLCoord(const LLCoordCommon& other)
    {
        COORD_FRAME::convertFromCommon(other);
    }

    LLCoordCommon convert() const
    {
        return COORD_FRAME::convertToCommon();
    }

    constexpr void set(typename COORD_FRAME::value_t x, typename COORD_FRAME::value_t y) noexcept { mX = x; mY = y;}
    constexpr bool operator==(const self_t& other) const noexcept { return mX == other.mX && mY == other.mY; }
    constexpr bool operator!=(const self_t& other) const noexcept { return !(*this == other); }

    static constexpr const self_t& getTypedCoords(const COORD_FRAME& self) noexcept { return static_cast<const self_t&>(self); }
    static constexpr self_t& getTypedCoords(COORD_FRAME& self) noexcept { return static_cast<self_t&>(self); }
};

struct LL_COORD_TYPE_GL
{
    typedef S32 value_t;

    LLCoordCommon convertToCommon() const
    {
        const LLCoordGL& self = LLCoordGL::getTypedCoords(*this);
        return LLCoordCommon(self.mX, self.mY);
    }

    void convertFromCommon(const LLCoordCommon& from)
    {
        LLCoordGL& self = LLCoordGL::getTypedCoords(*this);
        self.mX = from.mX;
        self.mY = from.mY;
    }
};

struct LL_COORD_TYPE_WINDOW
{
    typedef S32 value_t;

    LLCoordCommon convertToCommon() const;
    void convertFromCommon(const LLCoordCommon& from);
};

struct LL_COORD_TYPE_SCREEN
{
    typedef S32 value_t;

    LLCoordCommon convertToCommon() const;
    void convertFromCommon(const LLCoordCommon& from);
};

#endif
