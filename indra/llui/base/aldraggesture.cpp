/**
 * @file aldraggesture.cpp
 * @brief One drag of the pointer, as every control with one has it.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "aldraggesture.h"

#include <cstdlib>

void ALDragGesture::press(S32 x, S32 y)
{
    mPressed  = true;
    mDragging = mDeadZone <= 0;
    mPressX   = x;
    mPressY   = y;
    mLastX    = x;
    mLastY    = y;
}

bool ALDragGesture::past(S32 x, S32 y) const
{
    const S32 dx = x - mPressX;
    const S32 dy = y - mPressY;
    switch (mZone)
    {
        case Zone::Across:
            return std::abs(dx) > mDeadZone;
        case Zone::EitherAxis:
            return std::abs(dx) > mDeadZone || std::abs(dy) > mDeadZone;
        case Zone::Distance:
        default:
            return dx * dx + dy * dy > mDeadZone * mDeadZone;
    }
}

bool ALDragGesture::moved(S32 x, S32 y)
{
    if (!mPressed || (x == mLastX && y == mLastY))
    {
        return false;
    }
    mLastX = x;
    mLastY = y;
    if (!mDragging)
    {
        mDragging = past(x, y);
    }
    return mDragging;
}

bool ALDragGesture::release()
{
    const bool was = mPressed;
    mPressed       = false;
    mDragging      = false;
    return was;
}

bool ALDragGesture::cancel()
{
    return release();
}
