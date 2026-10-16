/**
 * @file altextcaret.h
 * @brief Caret policy shared by the text widgets.
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

#ifndef LL_ALTEXTCARET_H
#define LL_ALTEXTCARET_H

#include "stdtypes.h"

// LLLineEditor and LLTextBase are separate implementations on purpose and
// neither includes the other, so anything both of them decide has to be
// decided somewhere else or it gets decided twice. This is that somewhere.
namespace ALTextCaret
{
    // Whether a caret dragged to `mouse_x` should stay where it is rather than
    // move across the cluster it currently sits against.
    //
    // A cluster is one character to the reader however many codepoints it
    // spans, so the caret may only sit at its edges -- and a drag that crosses
    // it must pick an edge without flickering between them. The rule is a
    // 30%/70% deadband about the midpoint rather than a delta gate: the mouse
    // has to move clearly into the far half before the caret follows, so a
    // fast drag still snaps once at the 30% line while jitter inside the
    // deadband never does, whatever the speed.
    //
    // `at_cluster_end` says which edge the caret is on, and so which threshold
    // it has to clear. Both are measured from the cluster's left edge in
    // widget-local pixels.
    inline bool holdsCluster(S32 mouse_x, S32 cluster_left_px,
                             S32 cluster_width_px, bool at_cluster_end)
    {
        if (cluster_width_px <= 0)
        {
            return false;
        }
        const S32 stick_to_left  = cluster_left_px + cluster_width_px * 3 / 10;
        const S32 stick_to_right = cluster_left_px + cluster_width_px * 7 / 10;
        return at_cluster_end ? (mouse_x >= stick_to_left)
                              : (mouse_x <= stick_to_right);
    }
}

#endif // LL_ALTEXTCARET_H
