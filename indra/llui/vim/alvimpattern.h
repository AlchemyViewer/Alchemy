/**
 * @file alvimpattern.h
 * @brief Vim's patterns and replacements as the search engine reads them, and where their matches may stand.
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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A pattern in vim's spelling as the search engine reads it: the regular
// expression; how case is matched; the places its matches must stand
// (\%23l, \%V and the like), which no expression says; the group that is
// the match where \zs split it; the expression without its \K, whose
// matches say where each match's whole begins; whether a match may cross
// a line; and the groups a \ze put in, at which a match is cut, with how
// the engine numbers vim's groups where those come between them. Worked
// out, and the matches found, over a document alone.
struct ALVimPattern
{
    std::string regex;
    bool        caseSensitive = true;
    // A place a match must stand: in the last visual area, at the caret,
    // on a line or in a column -- or before (side -1) or after (+1) one --
    // or at the file's start or end; measured at the whole match's start,
    // or, after a \zs, at the match's.
    struct Where
    {
        enum class Kind : U8
        {
            Visual,
            Caret,
            Line,
            Column,
            FileStart,
            FileEnd
        };
        Kind kind       = Kind::Line;
        S32  side       = 0;
        S32  number     = 0;
        bool afterStart = false;

        bool operator==(const Where&) const = default;
    };
    std::vector<Where> where;
    std::string        wholeRegex;
    bool               acrossLines = false;
    // By bit, as ALTextSearchOptions::cutGroups has them.
    U64                cutGroups = 0;
    // The engine's number for each of vim's groups, in order; empty where
    // the two count alike.
    std::vector<S32>   groupNumbers;

    bool operator==(const ALVimPattern&) const = default;
    // Whether where its matches may stand depends on the caret or the last
    // visual area, not on the text alone.
    bool placed() const;

    // How case is matched where neither the pattern (\c, \C) nor its
    // caller says: vim's `ignorecase` and `smartcase`.
    struct Case
    {
        bool ignore = false;
        bool smart  = false;
    };
    // Vim's magic spelling to the engine's. `~` is the last replacement
    // made, as the text it is.
    static ALVimPattern of(const std::string& vim, const std::string& last_replacement, const Case& case_rules,
                           std::optional<bool> force_case = std::nullopt);
    // A replacement in vim's spelling as the engine's: & and \0 the whole
    // match, \1 to \9 the groups, \r and \n a line break, \t a tab, \u \U
    // \l \L \e \E changing case; the ~ was put in before this.
    static std::string replacementOf(const std::string& with);

    // What the places a pattern names are measured against: the caret, and
    // the last visual area where there was one -- whole lines for one by
    // lines, the character under either end included otherwise, and for a
    // block the columns between its ends as well, the first and the last
    // it covers as the reader counts them with the tab width given
    // (ALVimText::blockColumns), or every line's end for a block taken
    // with $.
    struct Places
    {
        ALTextPos   caret;
        bool        visual = false;
        ALTextRange visualRange;
        S32         blockLeft  = -1;
        S32         blockRight = -1;
        bool        blockToEnd = false;
        S32         tabWidth   = 4;
    };
    // The matches in a document, or the stretch of it `scope` holds, that
    // stand where the pattern says; with where each whole match began,
    // and, where `replaced` is given, what the engine's format `with`
    // makes of each as it was found.
    std::vector<ALTextRange> matchesIn(const ALTextDocument& doc, ALTextSearchOptions options, const ALTextRange* scope, const Places& places,
                                       std::string& error, std::vector<ALTextPos>& wholes, std::string_view with = {},
                                       std::vector<std::string>* replaced = nullptr) const;
    // Of matches found, those that stand where the pattern says; where
    // each began, and what replaces it, kept in step with them.
    void constrain(const ALTextDocument& doc, const Places& places, std::vector<ALTextRange>& matches, std::vector<ALTextPos>& wholes,
                   std::vector<std::string>* replaced = nullptr) const;
};
