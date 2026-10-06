/**
 * @file almenuslot.h
 * @brief A context menu a view shows, and lets go of when it shows another or goes itself.
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

#include "llhandle.h"
#include "stdtypes.h"

#include <string>

class LLContextMenu;
class LLView;

// A context menu a view shows: made from its file, the one it showed before
// let go of first; shown in front, holding the mouse; and let go of with the
// view. A menu lives in the viewer's menu holder, not in the view that
// showed it, and calls back into that view while it is open -- so a view
// going with its menu still open would leave the menu calling into what
// has gone.
//
// The callbacks the menu's items name are registered by whoever makes it,
// with scoped registrars (LLUICtrl::CommitCallbackRegistry and
// EnableCallbackRegistry) held around the call to make().
class ALMenuSlot
{
public:
    ALMenuSlot() = default;
    ~ALMenuSlot() { close(); }
    ALMenuSlot(const ALMenuSlot&)            = delete;
    ALMenuSlot& operator=(const ALMenuSlot&) = delete;

    // The menu made from a file in the menu holder, the one before let go
    // of; null where there is no holder or the file makes none.
    LLContextMenu* make(const std::string& file);
    // The menu made last shown at a point of a view: placed, then put in
    // front with the mouse.
    void           show(LLView* over, S32 x, S32 y);
    // The menu out of sight at once, and gone once the frame is done with it.
    void           close();
    // The menu, while it has not gone.
    LLContextMenu* get() const;

private:
    LLHandle<LLContextMenu> mMenu;
};
