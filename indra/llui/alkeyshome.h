/**
 * @file alkeyshome.h
 * @brief Where a key a window of its own did not take goes: to the window it belongs to.
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

#include "llfloater.h"

// A window of its own that belongs to another -- a popover, over the window
// its anchor is in; a pane out of its window -- is where a key pressed over
// it stops: the key never climbs to the window it belongs to, and a Control
// key goes to the viewer's menu bar instead. So every shortcut that window
// has quietly stops working while one of these has the keyboard. Each takes
// a key itself first and sends one it did not take home, this one way.
struct ALKeysHome
{
    static bool handle(LLFloater& window, LLFloater* home, KEY key, MASK mask)
    {
        if (window.LLFloater::handleKeyHere(key, mask))
        {
            return true;
        }
        return home && home->handleKeyHere(key, mask);
    }
};
