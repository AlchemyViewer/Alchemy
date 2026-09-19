/**
 * @file alstudiofloater.h
 * @brief A window laid out as a studio is, and what every such window keeps
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

#include "alhistorylist.h"
#include "alpanefolds.h"
#include "alquickopen.h"
#include "llfloater.h"

#include <functional>
#include <string>
#include <vector>

class LLMenuBarGL;
class LLMenuItemGL;
class LLTextBox;

// A window laid out as a studio is: a menu bar of its own, regions that
// fold away and come out into windows of their own, a status line, and a
// state that remembers all of that and where the window was. XUI Studio
// is one.
//
// The state is kept in the viewer's own settings rather than the
// account's, since a studio is opened from the login screen as often as
// from the world, and there is no account yet to have remembered
// anything -- which is why the rect is in it too, and why the floater's
// own rect bookkeeping is told what the state says.
//
// The menu bar's shortcuts are this window's and not the viewer's: they
// answer while it has the keyboard and are silent everywhere else.
// Without saying so, the viewer's menu answers first and quietly keeps
// every key it also binds -- Control+0 is Zoom In out there, and the pane
// it folds here is never reached.
class ALStudioFloater : public LLFloater
{
public:
    AL_VIEW_TYPE(ALStudioFloater, LLFloater);

    bool hasAccelerators() const override { return true; }
    void onClose(bool app_quitting) override;
    void draw() override;
    bool applyRectControl() override;

    // The status line, in the plain ink or the alarm's.
    virtual void setStatus(const std::string& text, bool failure = false);

    // Undo and redo, which every studio has: a step back and a step
    // forward, however the studio keeps them. False where there was
    // nothing to do.
    virtual bool undo() { return false; }
    virtual bool redo() { return false; }

protected:
    // The setting the state is kept under, which a subclass declares.
    ALStudioFloater(const LLSD& key, std::string state_setting);

    // What the window is built of, told once it is built. The menu bar's
    // undo and redo items are found then, by those names.
    void setMenuBar(LLMenuBarGL* menu_bar);
    void setStatusLine(LLTextBox* status) { mStatus = status; }
    LLMenuBarGL* menuBar() const { return mMenuBar; }

    // The menu bar's own shortcut for a key, taken: what a subclass asks
    // first in handleKeyHere, before its own keys.
    bool handleMenuAccelerator(KEY key, MASK mask);
    // Control and Z, Y, and shift and Z, which the menu bar may not bind:
    // what a subclass asks next. A text control in this window with an
    // edit history of its own keeps them for it -- with hasAccelerators
    // they arrive before the Edit menu's own undo, which is what it would
    // have got from them -- and only a key it declines reaches undo().
    bool handleUndoKeys(KEY key, MASK mask);

    // "Undo" on its own is a promise about nothing in particular. The
    // two menu items say the next step back and the next step forward,
    // in the words the history list uses for the same steps, so the menu
    // and the list agree about what is about to happen; empty says the
    // plain word. The words are the floater's MenuUndo, MenuUndoWhat,
    // MenuRedo and MenuRedoWhat.
    void sayUndoRedo(const std::string& undo_what, const std::string& redo_what);
    // The history list fed, and the menu items said from the steps
    // either side of the present.
    void showHistory(ALHistoryList* list, std::vector<ALHistoryList::Step> steps, size_t in_force);

    // Open quickly: the candidates against a few letters, over the
    // window, gone as soon as one is chosen or the person looks away.
    // Under `anchor` where one is given, else over the window; as wide as
    // `width` where one is given. Asked again while it is up, it keeps
    // what was typed and takes the keyboard back.
    void quickOpen(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder,
                   const std::string& title, std::function<void(const std::string&)> chose,
                   LLView* anchor = nullptr, S32 width = 0);

    // The regions that fold, which the subclass binds.
    ALPaneFolds mFolds;

    // The state as a whole: the regions and the rect, then whatever the
    // subclass writes and reads. Saved on close and whenever the window
    // is dragged to another shape; a subclass saves on its own changes.
    void saveState();
    void loadState();
    virtual void writeState(LLSD& state) const {}
    virtual void readState(const LLSD& state) {}

private:
    // The shape a drag leaves behind -- the window's rect, the size of
    // each region -- noticed once the drag is over, rather than written
    // on every pixel of it.
    void rememberShape();

    std::string         mStateSetting;
    LLMenuBarGL*        mMenuBar = nullptr;
    LLMenuItemGL*       mUndoItem = nullptr;
    LLMenuItemGL*       mRedoItem = nullptr;
    LLTextBox*          mStatus = nullptr;
    // What the state said the window's rect was, applied when it opens;
    // and what was last written, so a frame can tell whether anything
    // moved.
    LLRect              mRestoredRect;
    LLRect              mShapeRect;
    std::vector<S32>    mShapeDims;
    LLHandle<LLView>    mQuickPopover;
};
