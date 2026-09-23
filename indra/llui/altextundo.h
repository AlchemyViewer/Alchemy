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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A document's edits as steps a person can take back: each one the edits
// it was made of, in order, with the caret where it stood before and after.
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
        std::string                       mLabel;
        // Which step this is, for a save point to find it by; a run
        // joined to it keeps the number it began with.
        U64                               serial = 0;
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
    // is. `now` is in seconds from any clock, and the window is how close
    // two changes have to be to join a run.
    void record(const ALTextDocument::Edit& edit, const ALTextPos& before, const ALTextPos& after, F64 now);
    void setRunWindow(F64 seconds) { mWindow = seconds; }

    // Everything recorded until endGroup() is one step.
    void beginGroup();
    void endGroup();
    // A run of typing is over: the next change is a step of its own.
    void breakRun() { mSteps.breakRun(); }

    // The step back and the step forward, applied. Where the caret goes,
    // or nothing where there was nothing to do.
    std::optional<ALTextPos> undo();
    std::optional<ALTextPos> redo();
    bool                     canUndo() const { return mSteps.canUndo(); }
    bool                     canRedo() const { return mSteps.canRedo(); }
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
    // as an editor's history outlives its window. At most about this many
    // bytes of edited text: the oldest steps back go first past it, and
    // the steps forward all go where they alone would pass a quarter of it.
    LLSD asLLSD(size_t budget = 1024 * 1024) const;
    // That history put back over the document as it stands, which must be
    // the text it was written with: every step is tried first on copies,
    // back from here and forward again, each edit's text standing where it
    // says it stood. False, and the journal as it was, where one does not.
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
    // What kind of step an edit makes on its own, which is the key a run
    // is joined by.
    static const char* kindOf(const ALTextDocument::Edit& edit);
    // Whether an edit carries on the run the last step is: the same kind,
    // and in the place the run had reached.
    static bool carriesOn(const Step& last, const ALTextDocument::Edit& next);
    static void join(Step& last, Step&& next);

    ALTextDocument&   mDocument;
    ALUndoStack<Step> mSteps;
    F64               mWindow = 1.0;
    S32               mGroupDepth = 0;
    // The mark: how many steps were in force when the text was saved.
    // Nowhere, once a change has thrown away the redo steps it was among.
    static constexpr size_t NOWHERE = static_cast<size_t>(-1);
    size_t            mSavedInForce = 0;
    U64               mNextSerial   = 0;
    U32               mEra          = 0;
};
