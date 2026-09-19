/**
 * @file aldockpanel.cpp
 * @brief A pane that can come out of the window it was declared in.
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

#include "aldockpanel.h"

#include "llfocusmgr.h"
#include "lllayoutstack.h"
#include "lluictrlfactory.h"

static LLDefaultChildRegistry::Register<ALDockPanel> r("dock_panel");

namespace
{
    // What a pane's own window is, if nobody has said where to put it: as
    // big as the pane was, where the pane was, and a little down and right
    // so it is not exactly over the hole it left.
    constexpr S32 OFFSET = 24;
    // The least a window holding a pane may be dragged down to, below its
    // own title bar.
    constexpr S32 LEAST_WIDTH = 200;
    constexpr S32 LEAST_HEIGHT = 80;
}

ALPanelFloater::ALPanelFloater(const LLFloater::Params& p, ALDockPanel* pane, LLFloater* home)
:   LLFloater(LLSD(), p),
    mPane(pane ? pane->getHandle() : LLHandle<LLView>()),
    mHome(home ? home->getHandle() : LLHandle<LLFloater>())
{
}

void ALPanelFloater::onClose(bool app_quitting)
{
    // The pane is only borrowed. A window closing gives it back, unless the
    // application is going away, in which case there is nothing to give it
    // back to.
    if (!app_quitting)
    {
        if (ALDockPanel* pane = mPane.get() ? mPane.get()->as<ALDockPanel>() : nullptr)
        {
            pane->dock();
        }
    }
    LLFloater::onClose(app_quitting);
}

bool ALPanelFloater::handleKeyHere(KEY key, MASK mask)
{
    if (LLFloater::handleKeyHere(key, mask))
    {
        return true;
    }
    if (LLFloater* home = mHome.get())
    {
        return home->handleKeyHere(key, mask);
    }
    return false;
}

ALDockPanel::Params::Params()
:   title("title")
{
}

ALDockPanel::ALDockPanel(const Params& p)
:   LLPanel(p),
    mTitle(p.title)
{
}

// static
ALDockPanel* ALDockPanel::wrap(LLView* parent, const std::string& title)
{
    if (!parent)
    {
        return nullptr;
    }
    Params p;
    p.name = parent->getName() + "_pane";
    p.title = title;
    p.rect = parent->getLocalRect();
    p.follows.flags = FOLLOWS_ALL;
    p.background_visible = false;
    p.mouse_opaque = false;
    ALDockPanel* pane = LLUICtrlFactory::create<ALDockPanel>(p);

    // Everything the parent held, in the order it held it: a view list is
    // filled from the front, so it is read backwards to be added forwards.
    const std::vector<LLView*> held(parent->getChildList()->begin(), parent->getChildList()->end());
    for (auto it = held.rbegin(); it != held.rend(); ++it)
    {
        parent->removeChild(*it);
        pane->addChild(*it);
    }
    parent->addChild(pane);
    return pane;
}

void ALDockPanel::popOut()
{
    if (poppedOut())
    {
        return;
    }
    LLView* home = getParent();
    if (!home)
    {
        return;
    }
    LLFloater* was_in = getParentByType<LLFloater>();

    mHome = home->getHandle();
    mHomeFollows = getFollows();

    // The window is the pane AND a title bar: a window made the size of the
    // pane has to put one of them over the other, and what the pane draws
    // along its own top -- a row of tabs, most of the time -- then shares a
    // band with the title and the window's own buttons.
    const S32 header = LLFloater::getDefaultParams().header_height;
    LLRect where = mFloatingRect;
    const bool remembered = !where.isEmpty();
    if (!remembered)
    {
        where = calcScreenRect();
        where.mTop += header;
        where.translate(OFFSET, -OFFSET);
    }

    LLFloater::Params fp(LLFloater::getDefaultParams());
    fp.name = getName() + "_window";
    fp.title = mTitle;
    fp.rect = where;
    fp.can_resize = true;
    fp.can_minimize = true;
    fp.can_close = true;
    fp.save_rect = false;
    fp.save_visibility = false;
    fp.min_width = LEAST_WIDTH;
    fp.min_height = header + LEAST_HEIGHT;
    ALPanelFloater* floater = new ALPanelFloater(fp, this, was_in);
    mFloater = floater->getHandle();

    // The pane is moved, not copied: every pointer into it goes on working,
    // and every getChild through it from the window it left stops.
    home->removeChild(this);
    floater->addChild(this);
    setFollows(FOLLOWS_ALL);
    setShape(contentRect(floater));
    setVisible(true);

    // What is left in the window takes the room. The size the panel wants
    // survives this on its own: a stack sets `mIgnoreReshape` around its own
    // layout, so laying a collapsed panel out at nothing does not tell the
    // panel that nothing is the size it wants.
    if (LLLayoutPanel* holder = home->as<LLLayoutPanel>())
    {
        if (LLLayoutStack* stack = holder->getParentAs<LLLayoutStack>())
        {
            stack->collapsePanel(holder, true);
        }
    }

    // The keyboard is not taken along, and the window it came from forgets
    // it was there: a focused control moving to another window, with this
    // one still remembering it as where its focus was, is how a keystroke
    // ends up somewhere nobody is looking.
    if (gFocusMgr.childHasKeyboardFocus(this))
    {
        gFocusMgr.setKeyboardFocus(nullptr);
    }
    if (was_in)
    {
        gFocusMgr.clearLastFocusForGroup(was_in);
    }

    floater->openFloater();
    // Where it was remembered may be off a screen that has since gone.
    if (remembered && gFloaterView && floater->getParent() == gFloaterView)
    {
        gFloaterView->adjustToFitScreen(floater, false);
    }
    floater->setFocus(true);
}

void ALDockPanel::dock()
{
    LLFloater* floater = mFloater.get();
    // Cleared first: the window's own close puts the pane back, so this must
    // not come round again through it.
    mFloater.markDead();

    // And home forgotten once it is back: the window's close comes round
    // through here as well, and a pane put back twice is taken out of its
    // home and put in again for nothing.
    LLView* home = mHome.get();
    mHome.markDead();
    if (home)
    {
        if (floater)
        {
            mFloatingRect = floater->getRect();
            floater->removeChild(this);
        }
        // The whole of home as it is now, not as it was: the window may
        // have been resized while the pane was out, and a pane put back at
        // its old size follows every later resize from the wrong one.
        home->addChild(this);
        setFollows(mHomeFollows);
        setShape(home->getLocalRect());
        setVisible(true);

        // The room it gave up, given back at the size it wanted before.
        if (LLLayoutPanel* holder = home->as<LLLayoutPanel>())
        {
            if (LLLayoutStack* stack = holder->getParentAs<LLLayoutStack>())
            {
                stack->collapsePanel(holder, false);
            }
        }
    }
    if (floater)
    {
        floater->closeFloater();
    }
}

// What is left of a window once its title bar has had its band: everything
// a floater built from a file gets, and everything a pane should take.
// static
LLRect ALDockPanel::contentRect(const LLFloater* floater)
{
    LLRect r = floater->getLocalRect();
    r.mTop -= floater->getHeaderHeight();
    return r;
}

LLRect ALDockPanel::floatingRect() const
{
    if (const LLFloater* floater = mFloater.get())
    {
        return floater->getRect();
    }
    return mFloatingRect;
}
