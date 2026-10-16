/**
 * @file aldeferredrebuild.h
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

#pragma once

#include <functional>

// A view that builds its parts again from inside one part's own callback --
// which is what a caller answering a choice by rewriting the choices does --
// would delete the part that is still on the stack. Around each callback
// the rebuild is held; asked for while held, it is done on the next idle,
// once the callback is over. Asked for otherwise, it is done at once.
class ALDeferredRebuild
{
public:
    explicit ALDeferredRebuild(std::function<void()> rebuild);
    ~ALDeferredRebuild();

    ALDeferredRebuild(const ALDeferredRebuild&) = delete;
    ALDeferredRebuild& operator=(const ALDeferredRebuild&) = delete;

    // Now, or once the callback in progress is over.
    void request();

    // A callback, with the rebuild held while it runs.
    template<typename F>
    void around(F&& callback)
    {
        ++mHeld;
        callback();
        --mHeld;
    }

    bool held() const { return mHeld > 0; }

private:
    static void idle(void* self);

    std::function<void()>   mRebuild;
    int                     mHeld = 0;
    bool                    mWaiting = false;
};
