/**
 * @file altextundo.h
 * @brief The steps back and forward through a document's edits.
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

#include "altextdocument.h"
#include "alundostack.h"
#include "llsd.h"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A document's edits as steps a person can take back: each one the edits
// it was made of, in order, with the selection -- its anchor and its caret
// -- as it stood before and after.
// A run of typing is one step -- characters typed one after the other, each
// where the last ended, within a moment of each other -- and so is a run of
// backspaces or deletes; anything recorded while a group is open is one
// step, which is what a paste of several pieces or a replace-all is to the
// person who did it. The document does the replacing; this only says what
// to replace, and undoes a step's edits in the reverse of the order they
// were made, so each inverse lands on the text it was taken from.
//
// Also where the document knows whether it is the one that was saved: a
// mark on the stack rather than a version number, since undoing past a
// save and redoing back to it is the saved text again. A save ends the
// run, so what is typed after it is always a step of its own.
class ALTextUndo
{
public:
    struct Step
    {
        std::vector<ALTextDocument::Edit> edits;
        ALTextPos                         caretBefore;
        ALTextPos                         caretAfter;
        // Where the selection's other end stood: the caret's own place
        // where nothing was selected.
        ALTextPos                         anchorBefore;
        ALTextPos                         anchorAfter;
        std::string                       mLabel;
        // Which step this is, for a save point to find it by; a run
        // joined to it keeps the number it began with.
        U64                               serial = 0;
        // What it weighs against the budget: its edits' text and what each
        // costs written beside it.
        size_t                            bytes = 0;
        // Made by keys typed (beginTyping), which the next key typed may
        // carry on.
        bool                              typed = false;
    };
    // What the steps back may weigh together, and the history written
    // out: the oldest go first past it, the newest always kept.
    static constexpr size_t BUDGET = 1024 * 1024;
    // A history read and checked against a text, to be put back over it
    // (restore): read and checked once, whoever puts it back.
    struct History
    {
        std::vector<Step> undo;
        std::vector<Step> redo;
        S32               saved = -1;
    };
    // Where the journal stands, taken when a text is sent to be saved and
    // marked saved when the answer comes: the step it stood at, or none,
    // and the era of the bottom of the stack, which forgetting the oldest
    // step and clearing move on.
    struct SavePoint
    {
        U64 serial = 0;
        U32 era    = 0;
    };

    explicit ALTextUndo(ALTextDocument& document);

    // A change made through the document, with where the caret was and
    // is -- or the selection it was made over, anchor to caret. `now` is in
    // seconds from any clock, and the window is how close two changes have
    // to be to join a run.
    void record(const ALTextDocument::Edit& edit, const ALTextPos& before, const ALTextPos& after, F64 now);
    void record(const ALTextDocument::Edit& edit, const ALTextRange& before, const ALTextPos& after, F64 now);
    // Where the selection ends up once the change that was just recorded is
    // done -- a replace-all putting the caret back where it was, a line
    // moved with its selection -- for a redo to put it there. Nothing once
    // a step has been taken back or forward since.
    void settle(const ALTextRange& selection);
    void setRunWindow(F64 seconds) { mWindow = seconds; }

    // What one key typed does -- a character, a pair and its closer, a
    // line broken and indented, an outdent -- is one with the typing run
    // before it where the key was typed, nothing selected, where the run
    // left the caret or its text, within the window; else it starts a
    // run. Given the selection the key was typed at. Scopes nest, as a
    // key's handlers do; a group open takes precedence.
    void beginTyping(const ALTextRange& selection);
    void endTyping();

    // Everything recorded until endGroup() is one step.
    void beginGroup();
    void endGroup();
    // Every group still open closed at once: what is left of whoever opened
    // them -- a modal keymap in its insert mode -- is gone.
    void closeGroups();
    // A run of typing is over: the next change is a step of its own.
    void breakRun() { mSteps.breakRun(); }
    // Whether a group is open.
    bool inGroup() const { return mSteps.inGroup(); }

    // The step back and the step forward, applied. The selection it puts
    // back, anchor to caret, or nothing where there was nothing to do.
    std::optional<ALTextRange> undo();
    std::optional<ALTextRange> redo();
    bool                     canUndo() const { return mSteps.canUndo(); }
    bool                     canRedo() const { return mSteps.canRedo(); }
    // Nothing to step back or forward to, and the text as it stands taken
    // for the saved one -- what a text just loaded is; markNeverSaved()
    // after, where it is not.
    void                     clear();

    // What the next step back and forward are called: the first thing
    // said after a change names it.
    void        label(std::string_view text) { mSteps.label(text); }
    std::string undoLabel() const { return mSteps.undoLabel(); }
    std::string redoLabel() const { return mSteps.redoLabel(); }

    // The text as it stands is the one that was saved.
    void markSaved();
    bool isPristine() const;
    // No text this journal can reach was ever saved: what a tab holds that
    // came from nowhere a save could reach -- work recovered after a crash,
    // a script whose object has gone -- until it is saved somewhere.
    void markNeverSaved();

    // The journal as data -- every step back and forward, its edits, its
    // carets and its name, and where among them the saved text stands --
    // for the same text to be given its history back in another session,
    // as an editor's history outlives its window. A run of typing or of
    // erasing is written as the one edit it amounts to rather than one a
    // character. At most about this many bytes written: the oldest steps
    // back go first past it, and the steps forward all go where they alone
    // would pass a quarter of it.
    LLSD asLLSD(size_t budget = BUDGET) const;
    // That history read for a text it must have been written with: every
    // step tried on copies, back from the text and forward again, each
    // edit's text standing where it says it stood. Nothing where one does
    // not, or it is no history.
    static std::optional<History> historyFrom(const LLSD& sd, std::string_view text);
    // A history read for the text the document holds put back over it, in
    // place of the journal's own.
    void restore(History history);
    // Both at once: false, and the journal as it was, where the history
    // is not of the document as it stands.
    bool fromLLSD(const LLSD& sd);
    // The text the saved mark stands at, stepped to from the document as it
    // stands; nothing where no step reaches a saved text.
    std::optional<std::string> savedText() const;
    // The point the text stands at now, the run ended so that whatever is
    // typed after is a step of its own; and later, the text at that point
    // marked as the saved one, wherever stepping reaches it from here --
    // below, where more was typed meanwhile, above where some was undone,
    // or nowhere, where a change after an undo threw it away.
    SavePoint savePoint();
    void      markSaved(const SavePoint& point);

private:
    // What kind of step an edit makes on its own, and the key a run of
    // that kind is joined by.
    enum class Kind : U8
    {
        Typing,
        Erasing,
        Other
    };
    static Kind             kindOf(const ALTextDocument::Edit& edit);
    static std::string_view keyOf(Kind kind);
    // Whether an edit carries on the run the last step is: the same kind,
    // and in the place the run had reached.
    static bool carriesOn(const Step& last, const ALTextDocument::Edit& next);
    static void join(Step& last, Step&& next);
    // What a step weighs against the budget.
    static size_t weigh(const Step& step) { return step.bytes; }
    // The oldest step forgotten: the saved mark and the era with it.
    void        forgotOldest();
    // The oldest steps forgotten while the steps back weigh more than the
    // budget: what noting a change or closing a group may leave.
    void        forgetOverBudget();
    // What the steps back weigh, summed afresh.
    size_t      undoneBytes() const;

    ALTextDocument&   mDocument;
    // Held by what they weigh, not by how many (forgetOverBudget).
    ALUndoStack<Step> mSteps{ std::numeric_limits<size_t>::max() };
    F64               mWindow = 1.0;
    // The mark: how many steps were in force when the text was saved.
    // Nowhere, once a change has thrown away the redo steps it was among.
    static constexpr size_t NOWHERE = static_cast<size_t>(-1);
    size_t            mSavedInForce = 0;
    U64               mNextSerial   = 0;
    U32               mEra          = 0;
    // Whether the newest step is the change last recorded, which settle()
    // may still say where it left the selection.
    bool              mSettling     = false;
    // How deep the typing scopes open are, and whether the key they are
    // for has recorded an edit yet: the rest of what it does joins that.
    S32               mTypingDepth  = 0;
    bool              mTypingNoted  = false;
    // The selection the key was typed at, anchor to caret.
    ALTextRange       mTypingAt;
    // What the steps back weigh together, kept as they change rather than
    // summed at each.
    size_t            mUndoneBytes  = 0;
};
