/**
 * @file aldeferredrebuild.cpp
 * @brief A rebuild that waits while the thing rebuilt is on the stack.
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

#include "aldeferredrebuild.h"

#include "llcallbacklist.h"

ALDeferredRebuild::ALDeferredRebuild(std::function<void()> rebuild)
:   mRebuild(std::move(rebuild))
{
}

ALDeferredRebuild::~ALDeferredRebuild()
{
    gIdleCallbacks.deleteFunction(idle, this);
}

void ALDeferredRebuild::request()
{
    if (mHeld > 0)
    {
        if (!mWaiting)
        {
            mWaiting = true;
            gIdleCallbacks.addFunction(idle, this);
        }
        return;
    }
    mRebuild();
}

// static
void ALDeferredRebuild::idle(void* self)
{
    ALDeferredRebuild* rebuild = static_cast<ALDeferredRebuild*>(self);
    gIdleCallbacks.deleteFunction(idle, self);
    rebuild->mWaiting = false;
    rebuild->mRebuild();
}
