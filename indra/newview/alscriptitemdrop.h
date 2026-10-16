/**
 * @file alscriptitemdrop.h
 * @brief An inventory item dropped on a script: its name, or its asset's key, put in as a string.
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

#include "llui.h"
#include "lluuid.h"

#include <functional>
#include <string>
#include <string_view>

class LLInventoryItem;
class ALScriptStudioServices;
struct ALScriptStudioDoc;

// An inventory item dropped on a script tab's text: its name goes in where
// it lands, as a string in the script's language -- what a script gives,
// rezzes or reads an item by -- or, with Shift held, its asset's key --
// what it shows a texture, plays a sound or an animation by -- where the
// item's permissions let the key be seen. A folder is none of this, nor a
// notecard, whose own tab (ALNotecardEmbedded) carries the item itself.
namespace ALScriptItemDrop
{
    // The asset's key where it may be seen, else null: the viewer's
    // inventory and permissions, or a test's.
    typedef std::function<LLUUID(const LLInventoryItem& item)> key_of_t;
    bool drop(ALScriptStudioDoc& doc, const ALScriptStudioServices& services, const key_of_t& key_of, S32 x, S32 y, MASK mask, bool dropping,
              EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip);
    // A text as a string in a script: in double quotes, its quotes and
    // backslashes escaped.
    std::string literal(std::string_view text);
}
