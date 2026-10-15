/**
 * @file alpaneroom.h
 * @brief A window that makes room for a side pane as it opens, and gives it back as it folds
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

#include <string>
#include <vector>

class LLFloater;
class LLLayoutPanel;

// The panes beside a window's main region -- each a panel of a horizontal
// layout stack, which takes its room from the region that grows -- given
// room of their own instead: as one opens, the window widens by its width
// on its side, as far as the screen goes; as it folds, the window narrows
// by as much. Followed at each frame rather than told, so that a pane
// opened any way -- its button, a key, a command that shows it -- is seen.
class ALPaneRoom
{
public:
    // The window, and its panes by their panels' names; those not found
    // left out.
    void bind(LLFloater* window, const std::vector<std::string>& panels);
    // Each pane that opened or folded since, its room made or given back,
    // where `on`; only noted otherwise, and the first time, which is what
    // the window was restored to. Never while the window is minimized,
    // or in another window's tabs.
    void follow(bool on);

    // The width a window that is `width` wide, `left` from the left of a
    // screen `screen` wide, comes to with `grow` more of it, and where its
    // left goes: on the pane's side, `before` the main region or after
    // it, as far as the screen goes and then on the other side; never
    // narrower than `least`.
    struct Shape
    {
        S32 left  = 0;
        S32 width = 0;
    };
    static Shape grown(S32 left, S32 width, S32 grow, bool before, S32 screen, S32 least);

private:
    struct Watched
    {
        LLLayoutPanel* panel     = nullptr;
        bool           collapsed = false;
    };
    LLHandle<LLFloater>  mWindow;
    std::vector<Watched> mPanes;
    bool                 mStarted = false;
};
