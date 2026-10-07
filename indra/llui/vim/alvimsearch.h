/**
 * @file alvimsearch.h
 * @brief Vim's searching: the last pattern, its matches kept, the search line lit, and offsets.
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
#include "altextsearch.h"
#include "alvimpattern.h"

#include <optional>
#include <string>
#include <vector>

class ALTextView;
class ALVimHost;
class ALVimKeymap;

// Vim's searching, for the vim keymap (ALVimKeymap): the pattern last
// searched for, which n and N go on with and :s takes up; a pattern in
// vim's spelling as the search engine's; what a search over the whole
// text finds, kept while the text and the pattern stand; the search line
// lit as it is typed; and where an offset leaves the caret by a match.
// What is said, where the caret is and moving it are the keymap's.
class ALVimSearch
{
public:
    explicit ALVimSearch(ALVimKeymap& vim) : mVim(vim) {}

    // Where a search leaves the caret by its match, as /pattern/e+1 says:
    // lines on or back ('l', column 0), or characters on or back from the
    // match's start ('s') or its last character ('e').
    // Zero, as Offset() makes it: none.
    struct Offset
    {
        char kind;
        S32  amount;
    };
    // A search line's pattern and offset: split at the first unescaped
    // `kind` after the pattern.
    static void splitOffset(const std::string& line, llwchar kind, std::string& pattern, std::string& offset_text);
    static bool parseOffset(const std::string& text, Offset& out);
    ALTextPos   offsetFrom(const ALTextDocument& d, const ALTextRange& match, const Offset& offset) const;
    // Searching, with the last pattern kept for n and N, and its offset:
    // the caret to where it goes (target).
    bool search(ALTextView& view, const std::string& pattern, bool forward, S32 count, bool whole_word, const Offset& offset = Offset());
    // Where a search goes from the caret -- the count's match on, round
    // past the ends, the offset taken from it -- its matches lit and the
    // match kept for n, the caret left where it is: an operator's motion.
    // Nothing where there is no match, which is said.
    std::optional<ALTextPos> target(ALTextView& view, const std::string& pattern, bool forward, S32 count, bool whole_word, const Offset& offset = Offset());
    // What is typed on the search line so far, lit and brought into sight;
    // and that let go of, the caret's place in sight again.
    void incrementalSearch(ALTextView& view);
    void endIncremental(ALTextView& view);

    // Vim's spelling of a pattern as the search engine's (ALVimPattern),
    // with the last replacement for ~ and the ignorecase and smartcase
    // settings; and what the places a pattern names are measured against:
    // the caret, and the last visual area.
    typedef ALVimPattern Pattern;
    Pattern              patternOf(const std::string& vim, std::optional<bool> force_case = std::nullopt) const;
    ALVimPattern::Places placesOf(const ALTextView& view) const;
    // The pattern's matches within a scope, the places applied, with
    // where each whole match began.
    std::vector<ALTextRange> matchesOf(ALTextView& view, const Pattern& pattern, ALTextSearchOptions options, const ALTextRange* scope, std::string& error,
                                       std::vector<ALTextPos>& wholes) const;
    // What a search finds over the whole text, kept until the text, the
    // pattern or how it is matched changes: n, N, gn and every key on the
    // search line ask it again of the same text. Found afresh each time
    // where the pattern places its matches by the caret or the last visual
    // area. `lit`: the host its matches were last lit in, by
    // lightFound().
    struct Found
    {
        const ALTextDocument*    doc     = nullptr;
        U32                      version = 0;
        Pattern                  pattern;
        ALTextSearchOptions      options;
        std::vector<ALTextRange> matches;
        std::vector<ALTextPos>   wholes;
        std::string              error;
        const ALVimHost*         lit = nullptr;
    };
    const Found& found(ALTextView& view, const Pattern& pattern, const ALTextSearchOptions& options);
    // Every match found lit, as hlsearch has it: not again where they are
    // lit already.
    void lightFound(ALVimHost& host);
    // The last search's match at the caret or after it, or at it or before
    // it, round past the ends: what gn and gN take.
    std::optional<ALTextRange> matchNear(ALTextView& view, bool forward);

    // The search, for n and N; :s sets it too, and * and # as a whole word.
    // How case is matched is in the keymap's shared state: sensitive
    // unless :set ignorecase says, as vim's own default is.
    std::string pattern;
    Offset      offset{};
    bool        forward   = true;
    bool        wholeWord = false;
    // The last match a search went to, which n from where an offset left
    // the caret goes on from.
    ALTextRange lastMatch;

private:
    ALVimKeymap& mVim;
    Found        mFound;
    // What the search line last lit as it was typed, in which text: a key
    // that leaves it so -- the cursor moved along the line -- lights
    // nothing again.
    bool        mIncrementalShown = false;
    std::string mIncrementalPattern;
    llwchar     mIncrementalKind    = 0;
    U32         mIncrementalVersion = 0;
};
