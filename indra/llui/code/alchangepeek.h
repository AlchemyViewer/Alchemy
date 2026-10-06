/**
 * @file alchangepeek.h
 * @brief A peek at a change from the code editor: its lines as saved, in a gap under them.
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

#ifndef AL_ALCHANGEPEEK_H
#define AL_ALCHANGEPEEK_H

#include "alchangessincesaved.h"
#include "altextdocument.h"
#include "llpanel.h"

#include <boost/signals2.hpp>

#include <memory>
#include <string>
#include <vector>

class ALCodeEditor;
class ALFlatButton;
class LLTextBox;

// A peek at a change from the code editor: the change since the text was
// last saved that a line is in, its lines as they were shown in a gap
// opened under its lines as they are -- tinted as a comparison tints lines
// taken out, numbered as they were -- with a bar over them saying which
// change of how many, and its steps: the change taken back, one step to
// undo; the change before and the one after, the peek going to it; and
// away. Without leaving the text: the editor stays where it is and keeps
// the keyboard. An edit of the text not made from here closes it, since
// what it shows is of the text before.
//
// The changes are the text's against the one its undo still reaches as
// saved, by lines, as a comparison finds them. The editor holds one, and
// shows it from a bar in its gutter (ALCodeEditor::peekChange).
class ALChangePeek : public LLPanel
{
public:
    // A change of a text since it was saved: its lines now, from `now`, so
    // many; and as saved, from `saved`, so many.
    typedef ALChangesSinceSaved::Change Change;
    // The changes of a text from a saved one, in order.
    static std::vector<Change> changesOf(const std::vector<std::string>& saved, const std::vector<std::string>& now)
    {
        return ALChangesSinceSaved::between(saved, now);
    }
    // The change after the caret's line of an editor's text, or before it:
    // the caret put at its first line, and a peek open there gone with it.
    // What vim's ]c and [c step through outside a comparison. False where
    // there is none that way, or nothing saved to tell one by.
    static bool stepFrom(ALCodeEditor& host, bool forward);
    // The change a line of the text now is in: for lines only taken out,
    // the line they were taken from, or at the text's end the line before
    // it, where the editor bars it. -1 for none.
    static S32 changeAt(const std::vector<Change>& changes, S32 line);

    // The most of a change's lines as saved shown at once; more scroll.
    static constexpr S32 MOST_ROWS = 10;

    explicit ALChangePeek(ALCodeEditor& host);
    ~ALChangePeek() override;

    // Shown at the change a line is in, or nowhere: false where there is
    // none, or nothing saved to tell one by.
    bool showAt(S32 line);
    // The change after the one shown, or before; false where there is none
    // that way.
    bool step(bool forward);
    // The change shown taken back: its lines made as they were saved, one
    // edit and one step to undo; the peek on to the change after where
    // there is one, else away. False where nothing was.
    bool takeBack();
    void close();
    bool isOpen() const { return mShown >= 0; }
    // Which change is shown, counted from nought, of how many; -1 for none.
    S32  changeShown() const { return mShown; }
    S32  changeCount() const { return mKnown ? static_cast<S32>(mKnown->changes.size()) : 0; }
    const Change& change() const { return mKnown->changes[static_cast<size_t>(mShown)]; }
    // The line whose gap it stands in; and what its bar says.
    S32         gapLine() const { return mGapLine; }
    std::string said() const;
    // The lines as saved it shows.
    const ALCodeEditor* savedText() const { return mSaved; }

    // Placed in its gap, as the editor is scrolled: before the editor
    // draws what it holds. Its rectangle is the part of it in the text.
    void place();
    // How far the whole of it goes on below its rectangle, cut off there.
    S32  cutBelow() const { return mCut; }
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;

private:
    // Its lines and its bar for the change shown, and the gap opened for
    // them; the gap shut. The words that changed in each line paired with
    // what it became marked, on the line as saved here and on the line now
    // in the editor; and let go of.
    void fill();
    void markWords();
    void unmarkWords();
    void openGap(S32 line, S32 rows);
    void shutGap();
    // The line its gap is on followed through an edit, as the editor
    // slides the gaps it was told of: heard after the editor has moved
    // them, and before the edit closes it, so that it shuts the gap where
    // the gap now is.
    void slideGap(const ALTextDocument::Edit& edit);
    // How tall it stands: its bar, and the lines it shows.
    S32  height() const;

    ALCodeEditor&            mHost;
    ALCodeEditor*            mSaved    = nullptr;
    LLTextBox*               mSaid     = nullptr;
    ALFlatButton*            mTakeBack = nullptr;
    ALFlatButton*            mPrevious = nullptr;
    ALFlatButton*            mNext     = nullptr;
    ALFlatButton*            mClose    = nullptr;
    // The changes, with the lines of the text as saved and as it was, it
    // last showed one of (ALCodeEditor::changesSinceSaved).
    std::shared_ptr<const ALChangesSinceSaved::Known> mKnown;
    S32                      mShown   = -1;
    // The line whose gap it opened, and the gap it had before; -1 for none.
    S32                      mGapLine = -1;
    S32                      mGapWas  = 0;
    // How far below its rectangle the whole of it goes, as last placed.
    S32                      mCut     = 0;
    // An edit of the text made from here, which does not close it.
    bool                     mEditing = false;
    boost::signals2::scoped_connection mChangedConnection;
    boost::signals2::scoped_connection mEditConnection;
};

#endif // AL_ALCHANGEPEEK_H
