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
#include "alkeychord.h"
#include "alpanefolds.h"
#include "alpopover.h"
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

    // A step chosen in the history gone to: undone or redone one step at a
    // time, each the way the studio takes a step, until `target` are in
    // force -- `in_force()` says how many are -- and stopped where a step
    // will not go or after `bound` steps, for a history that stops moving.
    static void goToStep(size_t target, const std::function<size_t()>& in_force, const std::function<bool()>& back,
                         const std::function<bool()>& forward, size_t bound);

    bool hasAccelerators() const override { return true; }
    // The studio's keys (handleStudioKeys), then the floater's, then the
    // chords it keeps (keepChords).
    bool handleKeyHere(KEY key, MASK mask) override;
    void onClose(bool app_quitting) override;
    void draw() override;
    bool applyRectControl() override;

    // The status line, in the plain ink or the alarm's; either goes quiet
    // after a while, so that what was said long ago does not read as news
    // -- a failure after longer than the rest.
    virtual void setStatus(const std::string& text, bool failure = false);

    // Undo and redo, which every studio has: a step back and a step
    // forward, however the studio keeps them. False where there was
    // nothing to do.
    virtual bool undo() { return false; }
    virtual bool redo() { return false; }

    // A command the studio's keys reach: its name, the key it answers to
    // as standard, and whether a person may give it another -- a Keys tab
    // lists the ones they may. A second key for a command is another entry
    // under the same name, one that may not be given another. A key that
    // is two in turn names the first last: Control-K, then S.
    struct KeyedCommand
    {
        const char* id         = "";
        KEY         key        = KEY_NONE;
        MASK        mask       = MASK_NONE;
        bool        rebindable = true;
        KEY         leadKey    = KEY_NONE;
        MASK        leadMask   = MASK_NONE;

        ALKeyChord chord() const { return { key, mask, leadKey, leadMask }; }
    };

protected:
    // The setting the state is kept under, which a subclass declares;
    // none for a window that keeps no state, such as a second window of
    // the same studio.
    ALStudioFloater(const LLSD& key, std::string state_setting);

    // What the window is built of, told once it is built. The menu bar's
    // undo and redo items are found then, by those names.
    void setMenuBar(LLMenuBarGL* menu_bar);
    void setStatusLine(LLTextBox* status) { mStatus = status; }
    LLMenuBarGL* menuBar() const { return mMenuBar; }

    // The menu bar's own shortcut for a key, taken: what a subclass asks
    // first in handleKeyHere, before its own keys.
    bool handleMenuAccelerator(KEY key, MASK mask);
    // Undo and redo's keys (undoKeyOf), which the menu bar may not bind:
    // what a subclass asks next. A text control in this window with an
    // edit history of its own keeps them for it -- with hasAccelerators
    // they arrive before the Edit menu's own undo, which is what it would
    // have got from them -- and only a key it declines reaches undo().
    bool handleUndoKeys(KEY key, MASK mask);
    // Whether a key is undo's or redo's: Control and Z, and Control and Y
    // or Shift and Z, unless the studio keeps others.
    enum class UndoKey : U8
    {
        None,
        Undo,
        Redo
    };
    virtual UndoKey undoKeyOf(KEY key, MASK mask) const;
    // A command the keys reach, with what it does: false where it cannot
    // be done now, and the key goes on to whatever else would take it.
    void addCommand(const KeyedCommand& command, std::function<bool()> run);
    // The keys a command answers to now: its standard one, unless the
    // studio keeps the ones a person gave it.
    virtual std::vector<ALKeyChord> keysOf(const KeyedCommand& command) const { return { command.chord() }; }
    // The first command a key is the key of that runs: false where none
    // did. A key that is the first of some command's two is taken, and
    // the window waits for the second (ALKeyChords), whatever has the
    // keyboard: the command that is both runs, and the status line says
    // what the window waits for, or that the two are no command.
    bool runCommandKey(KEY key, MASK mask);
    // What every studio's keys are, in order: the menu bar's shortcuts,
    // undo and redo, then its commands. For a studio with keys of its own
    // to try once these have not taken one.
    bool handleStudioKeys(KEY key, MASK mask);
    // A window that keeps its chords takes every Control, Command or Alt
    // chord that nothing in it had a use for, rather than letting it go on
    // to the viewer's menus and the world -- where Control-D duplicates
    // whatever is selected in the world, and Control-Shift-H goes home
    // without asking. Only Quit, and Control-Tab, the viewer's key for
    // moving between windows, go on. Cut, copy, paste and select all,
    // which the viewer's hidden Edit menu took to the text control with
    // the keyboard, are taken to it here instead. In handleKeyHere, last.
    void keepChords(bool keep) { mKeepChords = keep; }

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
    // Centred over the top of `anchor` where one is given, else of the
    // window; as wide and as tall as given, where given. Asked the same
    // question again while it is up -- the same title and placeholder --
    // it keeps what was typed, takes the keyboard back and answers the
    // latest asking; another question puts the one up away, escaped, and
    // is asked afresh. The widget comes back for a caller with more to
    // say to it -- a hint that follows the typing -- or null where it
    // could not be shown. `escaped` is told when it goes by Escape, or
    // put away for another question -- whatever it previewed to be put
    // back -- and `left` when it goes by the person looking away with
    // nothing chosen, where what it previewed stands, the reader having
    // looked at it and moved on; and `hold`, where given, of a choice made
    // with Shift-Return -- the pick to be held rather than taken, for a
    // caller with two things to do with one.
    ALQuickOpen* quickOpen(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder,
                           const std::string& title, std::function<void(const std::string&)> chose,
                           LLView* anchor = nullptr, S32 width = 0, S32 height = 0,
                           std::function<void()> escaped = {}, std::function<void(const std::string&)> hold = {},
                           std::function<void()> left = {});

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
    // The quick open up told whom its answer goes to: the latest asking.
    void answerQuickOpen(ALPopover* popover, ALQuickOpen* quick, std::function<void(const std::string&)> chose,
                         std::function<void()> escaped, std::function<void(const std::string&)> hold,
                         std::function<void()> left);
    // The first command a key, or two, are one of the keys of that runs.
    bool runChord(const ALKeyChord& chord);
    // The second of two keys, the first `lead`: the command they are run,
    // or, held with the first's modifiers and no command, the key alone --
    // Control kept down through both.
    void finishChord(KEY lead_key, MASK lead_mask, KEY key, MASK mask);
    // Cut, copy, paste and select all, to the text control with the
    // keyboard -- in this window, or in one of its own a key comes home
    // from. False where it is not one of them, or no text has the keyboard.
    bool handleEditKeys(KEY key, MASK mask);

    std::string         mStateSetting;
    LLMenuBarGL*        mMenuBar = nullptr;
    LLMenuItemGL*       mUndoItem = nullptr;
    LLMenuItemGL*       mRedoItem = nullptr;
    LLTextBox*          mStatus = nullptr;
    // When the status line was last said, and whether it has gone quiet.
    F64                 mStatusSaidAt  = 0.0;
    bool                mStatusFailure = false;
    bool                mStatusQuiet   = true;
    bool                mKeepChords    = false;
    // What the state said the window's rect was, applied when it opens;
    // and what was last written, so a frame can tell whether anything
    // moved.
    LLRect              mRestoredRect;
    LLRect              mShapeRect;
    std::vector<S32>    mShapeDims;
    ALPopoverSlot       mQuickPopover;
    // The question the quick open up asks, and what carries its answer.
    std::string                        mQuickQuestion;
    boost::signals2::scoped_connection mQuickChose;
    boost::signals2::scoped_connection mQuickHold;
    // The commands the keys reach, in the order they were added.
    struct Registered
    {
        KeyedCommand          command;
        std::function<bool()> run;
    };
    std::vector<Registered> mCommands;
};
