/**
 * @file alpanefolds.cpp
 * @brief The regions of a window that fold away, and the buttons that fold them
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
#include "alpanefolds.h"

#include "aldockpanel.h"
#include "llbutton.h"
#include "llfloater.h"
#include "lllayoutstack.h"

void ALPaneFolds::bind(LLView* window, std::vector<Pane> panes)
{
    mPanes.clear();

    for (Pane& pane : panes)
    {
        Bound bound;

        bound.mRegion = window->findChildView(pane.mPanel, true);
        bound.mPanel = ALViewType::as<LLLayoutPanel>(bound.mRegion);
        bound.mButton = pane.mButton.empty() ? nullptr : window->findChild<LLButton>(pane.mButton, true);
        // Everything the region holds, moved into a pane of its own that
        // fills it: the tree gains a level and every name in it is where it
        // was.
        bound.mDock = bound.mRegion && !pane.mTitle.empty() ? ALDockPanel::wrap(bound.mRegion, pane.mTitle) : nullptr;
        bound.mPane = std::move(pane);

        if (bound.mButton)
        {
            const std::string key = bound.mPane.mKey;

            bound.mButton->setCommitCallback([this, key](LLUICtrl*, const LLSD&) { toggle(key); });
        }

        mPanes.push_back(std::move(bound));
    }

    refreshButtons();
}

const ALPaneFolds::Bound* ALPaneFolds::find(std::string_view pane) const
{
    for (const Bound& bound : mPanes)
    {
        if (bound.mPane.mKey == pane || bound.mPane.mPanel == pane)
        {
            return &bound;
        }
    }

    return nullptr;
}

ALPaneFolds::Bound* ALPaneFolds::find(std::string_view pane)
{
    return const_cast<Bound*>(std::as_const(*this).find(pane));
}

// A fold somebody asked for, which is what a window writes down; a fold
// read back from what was written is not.
void ALPaneFolds::toggle(std::string_view pane)
{
    setCollapsed(pane, !collapsed(pane));
    mChanged();
}

// A region the person is not using gives its room to the one that grows.
// A folded region keeps everything it holds: what goes is the room, so
// coming back costs nothing and loses no selection.
void ALPaneFolds::setCollapsed(std::string_view pane, bool collapsed)
{
    const Bound* bound = find(pane);
    LLLayoutStack* stack = bound && bound->mPanel ? bound->mPanel->getParentAs<LLLayoutStack>() : nullptr;

    if (!stack)
    {
        return;
    }

    stack->collapsePanel(bound->mPanel, collapsed);
    refreshButtons();
}

bool ALPaneFolds::collapsed(std::string_view pane) const
{
    const Bound* bound = find(pane);

    return bound && bound->mPanel && bound->mPanel->isCollapsed();
}

S32 ALPaneFolds::dim(std::string_view pane) const
{
    const Bound* bound = find(pane);

    return bound && bound->mPanel ? bound->mPanel->getTargetDim() : 0;
}

// Asked for the way a drag on the bar asks, so the stack takes it from
// its neighbours the way it would have then.
void ALPaneFolds::setDim(std::string_view pane, S32 dim)
{
    const Bound* bound = find(pane);

    if (bound && bound->mPanel && dim > 0)
    {
        bound->mPanel->setTargetDim(dim);
    }
}

std::vector<S32> ALPaneFolds::dims() const
{
    std::vector<S32> result;

    for (const Bound& bound : mPanes)
    {
        result.push_back(bound.mPanel ? bound.mPanel->getTargetDim() : 0);
    }

    return result;
}

bool ALPaneFolds::out(std::string_view pane) const
{
    const Bound* bound = find(pane);

    return bound && bound->mDock && bound->mDock->poppedOut();
}

void ALPaneFolds::toggleOut(std::string_view pane)
{
    const Bound* bound = find(pane);

    if (!bound || !bound->mDock)
    {
        return;
    }

    if (bound->mDock->poppedOut())
    {
        bound->mDock->dock();
    }
    else
    {
        bound->mDock->popOut();
    }

    refreshButtons();
    mChanged();
}

LLFloater* ALPaneFolds::window(std::string_view pane) const
{
    const Bound* bound = find(pane);

    return bound && bound->mDock && bound->mDock->poppedOut() ? bound->mDock->getParentByType<LLFloater>() : nullptr;
}

void ALPaneFolds::dockAll()
{
    for (const Bound& bound : mPanes)
    {
        if (bound.mDock && bound.mDock->poppedOut())
        {
            bound.mDock->dock();
        }
    }
}

// Pressed in while the region is showing, so a row of them reads as a
// list of what is on screen rather than a list of what is hidden.
void ALPaneFolds::refreshButtons()
{
    for (const Bound& bound : mPanes)
    {
        if (bound.mButton)
        {
            bound.mButton->setToggleState(!(bound.mPanel && bound.mPanel->isCollapsed()));
        }
    }
}

// Folded, how wide, and whether it is in a window of its own and where
// that window is: a person who put the inspectors on the other monitor
// finds them there next time.
void ALPaneFolds::save(LLSD& state) const
{
    for (const Bound& bound : mPanes)
    {
        if (bound.mPanel)
        {
            state["fold_" + bound.mPane.mKey] = collapsed(bound.mPane.mKey);
            state["dim_" + bound.mPane.mKey] = dim(bound.mPane.mKey);
        }

        if (!bound.mDock)
        {
            continue;
        }

        state["out_" + bound.mPane.mKey] = bound.mDock->poppedOut();

        const LLRect r = bound.mDock->floatingRect();

        if (!r.isEmpty())
        {
            LLSD rect = LLSD::emptyArray();

            rect.append(r.mLeft);
            rect.append(r.mBottom);
            rect.append(r.mRight);
            rect.append(r.mTop);
            state["rect_" + bound.mPane.mKey] = rect;
        }
    }
}

// The sizes first, then the folds: a fold takes the size the region had
// and gives it back on unfolding, so a size set after a fold is lost.
void ALPaneFolds::load(const LLSD& state)
{
    for (const Bound& bound : mPanes)
    {
        const std::string key = "dim_" + bound.mPane.mKey;

        if (state.has(key))
        {
            setDim(bound.mPane.mKey, state[key].asInteger());
        }
    }

    for (const Bound& bound : mPanes)
    {
        const std::string key = "fold_" + bound.mPane.mKey;

        if (state.has(key))
        {
            setCollapsed(bound.mPane.mKey, state[key].asBoolean());
        }
    }

    for (const Bound& bound : mPanes)
    {
        const std::string rect = "rect_" + bound.mPane.mKey;
        const std::string out = "out_" + bound.mPane.mKey;

        if (!bound.mDock)
        {
            continue;
        }

        if (state.has(rect) && state[rect].size() == 4)
        {
            bound.mDock->setFloatingRect(LLRect(state[rect][0].asInteger(), state[rect][3].asInteger(),
                                                state[rect][2].asInteger(), state[rect][1].asInteger()));
        }

        if (state[out].asBoolean())
        {
            bound.mDock->popOut();
        }
    }

    refreshButtons();
}
