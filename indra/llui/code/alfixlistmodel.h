/**
 * @file alfixlistmodel.h
 * @brief What a code editor's quick-fix list offers, in what order, and what each would make of the text.
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
#include "llsd.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// What would put right a problem on a line: what it does, the edits it
// makes -- each a stretch and what goes in its place -- whether it is the
// one to take without asking, whether it only says the problem is wanted,
// and a value for whoever makes it. The editor lists fixes and previews
// them; the one taken is handed back by its value, and whoever supplied it
// makes it, knowing whether the text has moved on since the fixes were
// made.
struct ALCodeFix
{
    std::string                                      title;
    std::vector<std::pair<ALTextRange, std::string>> edits;
    bool                                             preferred = false;
    bool                                             suppress  = false;
    // A change no problem asks for, which the list offers after what puts
    // a problem right and before a suppression.
    bool                                             refactor  = false;
    LLSD                                             value;
    // Said after its title, dim: what it would do beyond its words -- what
    // the script would weigh made, say -- where that is known.
    std::string                                      note;
};

// The quick-fix list's contents: the fixes of a line, which showing of the
// list this is, and the refactors asked for at the caret while they are
// awaited. It says what is listed; the list drawn, its preview and the fix
// taken are the editor's.
class ALFixListModel
{
public:
    // The word that there is nothing to offer, which the list shows as it
    // shows a fix: nothing to make, and nothing to hand back.
    static bool isNothing(const ALCodeFix& fix) { return fix.edits.empty() && fix.value.isUndefined(); }
    // In the order they are offered: the preferred first, then the other
    // fixes, the refactors, and a suppression last, each as given.
    static void rank(std::vector<ALCodeFix>& fixes);
    // The lines a fix touches as they would read with it made, and which
    // lines they are.
    static std::string fixedLines(const ALTextDocument& text, const ALCodeFix& fix, S32& first, S32& last);
    // What the preview shows of a fix: each stretch of lines it changes as
    // they read and as they would, a line `- ` and `+ ` before each, edits
    // a few lines apart or less in one stretch, and an ellipsis between
    // stretches; `kinds` says each line's -- '-', '+' or ' '.
    static std::string previewOf(const ALTextDocument& text, const ALCodeFix& fix, std::vector<char>& kinds);
    // The same with each line as it was made: its kind; the line as it
    // reads, or would read, in the text, before the indentation the lines
    // have in common -- `cut` bytes, off every line long enough -- is taken
    // off; and for the first line a stretch makes, the line of the text the
    // stretch begins at, which what it makes reads on from as the text
    // would read it, -1 for the rest.
    struct PreviewLine
    {
        char        kind = ' ';
        std::string source;
        S32         from = -1;
    };
    static std::string previewOf(const ALTextDocument& text, const ALCodeFix& fix, std::vector<PreviewLine>& lines, size_t& cut);

    // Listed: the fixes of a line, ranked. Which showing of the list this
    // is, counted from one.
    U32  show(S32 line, std::vector<ALCodeFix> fixes);
    // Nothing listed, and no refactors awaited.
    void close();
    const std::vector<ALCodeFix>& fixes() const { return mFixes; }
    S32                           line() const { return mLine; }
    // Notes for the fixes of a showing, put after their fixes in the order
    // listed: false, and nothing changed, where the list has been made
    // again since or they are not one each.
    bool note(U32 shown, const std::vector<std::string>& notes);

    // The refactors asked for, at a stretch with the caret at a place.
    void ask(const ALTextRange& at, const ALTextPos& caret);
    bool awaited() const { return mAwaited; }
    // What the list becomes with the refactors for the place last asked
    // about -- joined to what is listed where the list is `open`, or
    // listed alone, or, where there is nothing at all, a word that says
    // so -- and the row to choose: the one `chosen` was, by its title.
    // Nothing where they are no longer wanted -- the caret moved, or it is
    // another place -- or where they add nothing to the open list.
    struct Joined
    {
        std::vector<ALCodeFix> fixes;
        S32                    chosen = 0;
    };
    std::optional<Joined> join(const ALTextRange& at, const ALTextPos& caret, std::vector<ALCodeFix> actions, bool open, S32 chosen);

private:
    std::vector<ALCodeFix> mFixes;
    S32                    mLine    = -1;
    U32                    mShowing = 0;
    ALTextRange            mAskedAt;
    ALTextPos              mAskedCaret;
    bool                   mAwaited = false;
};
