/**
 * @file aldockpanel.h
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

#pragma once

#include "llfloater.h"
#include "llpanel.h"

#include <string>

class ALDockPanel;

// The window a pane sits in while it is out of its own. Thin on purpose: it
// carries the pane's title, gives the pane the whole of itself, hands its
// keys back to the window the pane came from, and puts the pane back when it
// is closed rather than taking it down with it.
class ALPanelFloater final : public LLFloater
{
public:
    AL_VIEW_TYPE(ALPanelFloater, LLFloater);

    ALPanelFloater(const LLFloater::Params& p, ALDockPanel* pane, LLFloater* home);

    // Closing a window that is only borrowing a pane puts the pane back.
    void onClose(bool app_quitting) override;

    // A pane out of its window is a different focused floater, and the
    // window it came from never hears the keys pressed over it -- so every
    // accelerator the developer had quietly stops working, which is the kind
    // of thing nobody reports and everybody stops using. Unhandled keys go
    // home.
    bool handleKeyHere(KEY key, MASK mask) override;
    bool hasAccelerators() const override { return true; }

    LLFloater* home() const { return mHome.get(); }

private:
    LLHandle<LLView>    mPane;
    LLHandle<LLFloater> mHome;
};

// A pane that knows it can be somewhere else.
//
// A developer with two monitors wants the canvas on one and the inspectors
// on the other; a developer chasing one finding wants the findings large and
// alone. Both are the same gesture: take this pane out of the window and
// give it one of its own, and put it back afterwards.
//
// **This is view surgery, not a second copy.** The pane is moved in the
// tree, so every pointer to it and to anything inside it keeps working. What
// stops working is every `getChild` that goes *through* it from the window
// it left, because it is no longer under that window at all. The rule to
// hold, and to grep for in review: nothing may `getChild` through a pane
// that can come out.
//
// Where it came from is remembered here and nowhere else: the parent, the
// rect and the follows flags. Not the size its layout panel wanted -- a
// stack guards that through its own layout, so folding the panel away while
// the pane is out does not lose it.
class ALDockPanel : public LLPanel
{
public:
    AL_VIEW_TYPE(ALDockPanel, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        // What the window is called while the pane is in it.
        Optional<std::string> title;
        Params();
    };

    // Everything a parent already holds, moved into a pane of its own that
    // fills it. The way to give an existing window panes that come out
    // without rewriting the file it was declared in: the tree gains a level
    // and every name in it is still where it was.
    static ALDockPanel* wrap(LLView* parent, const std::string& title);

    void setTitle(const std::string& title) { mTitle = title; }
    const std::string& title() const { return mTitle; }

    // Out into a window of its own, and back where it came from. Both are
    // safe to call when it is already there.
    void popOut();
    void dock();
    bool poppedOut() const { return !mFloater.isDead(); }

    // What is left of a window once its title bar has had its band. A pane
    // given the whole of a floater covers the title, the buttons and the
    // handle it is dragged by.
    static LLRect contentRect(const LLFloater* floater);

    // Where it is while it is out, for whoever is saving that.
    LLRect floatingRect() const;
    void setFloatingRect(const LLRect& rect) { mFloatingRect = rect; }

protected:
    friend class LLUICtrlFactory;
    ALDockPanel(const Params& p);

private:
    std::string         mTitle;
    LLHandle<LLView>    mHome;          // the parent it was declared in
    LLHandle<LLFloater> mFloater;       // the window it is in, while it is out
    LLRect              mHomeRect;
    LLRect              mFloatingRect;
    U32                 mHomeFollows = FOLLOWS_ALL;
};
