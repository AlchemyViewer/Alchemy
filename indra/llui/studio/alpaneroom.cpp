/**
 * @file alpaneroom.cpp
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

#include "linden_common.h"

#include "alpaneroom.h"

#include "llfloater.h"
#include "lllayoutstack.h"

void ALPaneRoom::bind(LLFloater* window, const std::vector<std::string>& panels)
{
    mWindow  = window ? window->getHandle() : LLHandle<LLFloater>();
    mStarted = false;
    mPanes.clear();
    for (const std::string& name : panels)
    {
        LLLayoutPanel* panel = window ? window->findChild<LLLayoutPanel>(name, true) : nullptr;
        if (panel)
        {
            mPanes.push_back({ panel, panel->isCollapsed() });
        }
    }
}

void ALPaneRoom::follow(bool on)
{
    LLFloater* window = mWindow.get();
    if (!window)
    {
        return;
    }
    const bool started = mStarted;
    mStarted           = true;
    for (Watched& pane : mPanes)
    {
        const bool collapsed = pane.panel->isCollapsed();
        if (collapsed == pane.collapsed)
        {
            continue;
        }
        pane.collapsed = collapsed;
        LLView* stack  = pane.panel->getParent();
        LLView* screen = window->getParent();
        if (!on || !started || !stack || !screen || window->isMinimized() || window->getHost())
        {
            continue;
        }
        // Its width as the stack lays it out open, which it may not be yet
        // as it opens or still as it folds.
        const S32    width  = llmax(pane.panel->getTargetDim(), pane.panel->getExpandedMinDim());
        const LLRect rect   = window->getRect();
        const bool   before = pane.panel->getRect().getCenterX() < stack->getRect().getWidth() / 2;
        const Shape  shape  = grown(rect.mLeft, rect.getWidth(), collapsed ? -width : width, before, screen->getRect().getWidth(),
                                    window->getMinWidth());
        window->setShape(LLRect(shape.left, rect.mTop, shape.left + shape.width, rect.mBottom));
    }
}

// static
ALPaneRoom::Shape ALPaneRoom::grown(S32 left, S32 width, S32 grow, bool before, S32 screen, S32 least)
{
    Shape shape;
    // As wide as asked, but no wider than the screen where it widens, nor
    // narrower than the least it may be.
    shape.width = llmax(least, grow > 0 ? llmin(width + grow, llmax(width, screen)) : width + grow);
    const S32 change = shape.width - width;
    // On the pane's side; where that runs off the screen, the rest on the
    // other side. Narrowed, the other side stays put.
    shape.left = before ? left - change : left;
    if (change > 0)
    {
        shape.left = llclamp(shape.left, 0, llmax(0, screen - shape.width));
        shape.left = llmin(shape.left, left);
    }
    return shape;
}
