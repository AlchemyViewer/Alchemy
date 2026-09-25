/**
 * @file altextspelling.h
 * @brief A text view's spell check: the words a line lacks, and what the one at the caret might have been.
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

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class ALSyntaxHighlighter;

// A text view's spell check over its document. The words each line lacks
// from the dictionary -- all of a line of prose, only the comments and
// strings of code, and never what reads as code, a name or a number -- are
// found when the line is asked about, and kept until the line changes, its
// tokens do (a comment opened above it), or the dictionary does. Beside
// them, the misspelling at the caret with what it might have been, and the
// word taken into the dictionary or let pass.
//
// Who says whether a word is spelled right is the viewer's dictionary
// unless told otherwise, which a test is; only the dictionary takes a word
// in or lets one pass. Whether the check is wanted at all is the caller's,
// which says so with each question (`on`).
class ALTextSpelling
{
public:
    typedef std::function<bool(const std::string& word)>                                 checker_t;
    typedef std::function<void(const std::string& word, std::vector<std::string>& out)> suggester_t;
    // A line's misspellings, each the stretch of it from one column to
    // another.
    typedef std::vector<std::pair<S32, S32>> words_t;

    void setChecker(checker_t checker, suggester_t suggester);
    // Whether anyone is there to ask: a checker given, or the viewer's
    // spell check on.
    bool available() const;

    // The words a line lacks, checked now where they were not, or the
    // line's tokens changed since; none for a line not in the document,
    // and none found while the check is not `on`.
    const words_t& misspellings(const ALTextDocument& doc, ALSyntaxHighlighter& highlighter, S32 line, bool on);
    // Whether a place is in one, and the word there.
    bool misspelledAt(const ALTextDocument& doc, ALSyntaxHighlighter& highlighter, const ALTextPos& pos, bool on, ALTextRange* word = nullptr);
    // An edit of the document: the lines it touched checked again when
    // they are next asked about, the ones below sliding with the text;
    // and the suggestions let go of.
    void edited(const ALTextDocument::Edit& edit, S32 line_count);
    // Everything checked again, and the suggestions let go of: the
    // dictionary changed, or who checks.
    void recheck();

    // The misspelling at a place, and what the dictionary would put in its
    // place; nothing where the word there is spelled right, or the check
    // is not `on`.
    void                            suggestAt(const ALTextDocument& doc, ALSyntaxHighlighter& highlighter, const ALTextPos& pos, bool on);
    const std::vector<std::string>& suggestions() const { return mSuggestions; }
    const ALTextRange&              suggestedFor() const { return mSuggestedFor; }
    // A suggestion taken: the word, and what goes in its place; nothing
    // where there is none such. The suggestions are let go of either way.
    std::optional<std::pair<ALTextRange, std::string>> take(U32 index);
    // Whether the word suggested for may go into the viewer's dictionary,
    // or its ignore list -- the dictionary is who checks, and the check is
    // `on` -- and it put there, everything checked again.
    bool canTeach(bool on) const;
    void addToDictionary(const ALTextDocument& doc);
    void addToIgnore(const ALTextDocument& doc);

private:
    void checkLine(const ALTextDocument& doc, ALSyntaxHighlighter& highlighter, S32 line, bool on);

    checker_t   mChecker;
    suggester_t mSuggester;
    struct Line
    {
        bool    valid = false;
        // The line's tokens as they were checked by.
        U32     revision = 0;
        words_t words;
    };
    std::vector<Line>        mLines;
    std::vector<std::string> mSuggestions;
    ALTextRange              mSuggestedFor;
};
