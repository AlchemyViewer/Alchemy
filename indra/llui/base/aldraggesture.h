/**
 * @file aldraggesture.h
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

#pragma once

#include "stdtypes.h"

// One drag of the pointer, as every control that has one has it: pressed at
// a point; a drag once the pointer is past a dead zone, and a drag until it
// is let go, however near the press it comes back; told of a move only when
// the pointer has moved -- the viewer hovers whatever holds the capture on
// every frame, moved or not, and a control that commits as it goes would
// commit the same value sixty times a second -- and let go by the button
// coming up or by the capture being taken away, which a control that does
// not listen for is left thinking it is still being dragged.
//
// No widget here: the control keeps the capture and draws what it draws,
// and asks this what the pointer is doing. It says too when the control
// commits what the drag does: as it goes, on every move -- a curve whose
// handle is watched as it moves -- or once, as it is let go -- a dial whose
// value is taken when the hand comes away.
class ALDragGesture
{
public:
    // How the dead zone is measured from the press: by the straight
    // distance; across alone, for what moves along a row; or by the
    // farther of the two axes.
    enum class Zone : U8
    {
        Distance,
        Across,
        EitherAxis
    };
    enum class Commit : U8
    {
        AsItGoes,
        OnRelease
    };

    // A drag once the pointer is more than `dead_zone` pixels from the
    // press; none, and every press is a drag from the start.
    explicit ALDragGesture(S32 dead_zone = 0, Zone zone = Zone::Distance, Commit commit = Commit::OnRelease)
        : mDeadZone(dead_zone), mZone(zone), mCommit(commit)
    {
    }

    // Pressed at (x, y): begun, and a drag already where there is no dead
    // zone. The control takes the capture.
    void press(S32 x, S32 y);
    // The pointer at (x, y): true where the control should follow it --
    // it has moved since it was last told, and the press is a drag -- and
    // false for a frame where it did not move, or while it is still inside
    // the dead zone.
    bool moved(S32 x, S32 y);
    // Let go by the button: true where it had been pressed, and so has a
    // release to answer, a click as well as a drag.
    bool release();
    // Let go by the capture being taken away: what was under way ends
    // here, and the control decides what that keeps. True where there was
    // anything under way.
    bool cancel();

    bool pressed() const { return mPressed; }
    // Past the dead zone, since the press.
    bool dragging() const { return mDragging; }
    bool commitsAsItGoes() const { return mCommit == Commit::AsItGoes; }

    S32 pressX() const { return mPressX; }
    S32 pressY() const { return mPressY; }
    // Where the pointer was last told to be.
    S32 lastX() const { return mLastX; }
    S32 lastY() const { return mLastY; }
    // How far it is from the press.
    S32 dx() const { return mLastX - mPressX; }
    S32 dy() const { return mLastY - mPressY; }

private:
    bool past(S32 x, S32 y) const;

    S32    mDeadZone = 0;
    Zone   mZone     = Zone::Distance;
    Commit mCommit   = Commit::OnRelease;
    bool   mPressed  = false;
    bool   mDragging = false;
    S32    mPressX   = 0;
    S32    mPressY   = 0;
    S32    mLastX    = 0;
    S32    mLastY    = 0;
};
