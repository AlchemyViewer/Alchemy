/**
 * @file skcombatlockout.h
 * @brief Mutes debug rendering that sees through walls while the player is aiming
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Martin "Nexii" Pitt <martin.r.pitt@gmail.com>
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

#ifndef SK_COMBATLOCKOUT_H
#define SK_COMBATLOCKOUT_H

#include "stdtypes.h"

// While the camera is in mouselook (the over-the-shoulder view included) and for a few
// seconds after, every debug view that would let an aiming player see through geometry
// is off. Views read-gated at their draw site need nothing more; views whose live flag
// has to be forced keep the user's intent here and get it back when the lockout lifts.
namespace SKCombatLockout
{
    // Once per frame before any render pass, outside every render type push and pop.
    void update();
    bool active();

    // Render debug bits hidden from LLPipeline::hasRenderDebugMask while active.
    U64 debugMask();

    // The user's intent for the forced toggles: the live flag when free, the stash when locked.
    void setWireframe(bool on);
    bool getWireframe();
    void setHighlightTransparent(bool on);
    bool getHighlightTransparent();
    void setHiddenSelections(bool on);
    bool getHiddenSelections();
    void toggleRenderType(U32 type);
    bool hasRenderType(U32 type);
}

#endif // SK_COMBATLOCKOUT_H
