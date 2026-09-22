/**
 * @file altextsearch.h
 * @brief Finding in a document: every place a query stands, the next of them, and what replaces one.
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

#include <string>
#include <string_view>
#include <vector>

struct ALTextSearchOptions
{
    bool caseSensitive = false;
    bool wholeWord     = false;
    bool regex         = false;
    // What replaces a match takes the match's case: upper for HELLO,
    // capitalised for Hello, lower for hello, as typed for anything else.
    bool preserveCase  = false;
    // The match reported is this group of the pattern rather than the
    // whole, where it is not zero: what vim's \zs asks for. A match in
    // which the group took no part is reported whole.
    S32  matchGroup    = 0;
    // A match may cross a line's end: the lines are searched as one
    // text with a line break between them, so that \n in a pattern is
    // the end of a line and a class with a line break in it -- vim's
    // \_s -- reaches the next line; ^ and $ still stand at every line's
    // ends, and . stays within a line. Otherwise each line is searched
    // on its own and no match crosses one. A query that is not a
    // pattern crosses lines where it has a line break in it.
    bool acrossLines   = false;
};

// Finding in a document: every place a query stands, plain or as a
// regular expression, within the whole text or a stretch of it; the next
// or the previous of them from a position, going round the ends; and
// what a match is replaced by, a pattern's groups filled in. Pure
// functions over the document, so the bar and the view share one answer.
class ALTextSearch
{
public:
    // Every match in order, within `scope` where one is given. A query
    // that is empty finds nothing; a pattern that does not compile finds
    // nothing and says why in `error`. Matches stay within a line unless
    // the options let them cross; `whole_begins`, where asked for, says
    // where the whole of each match began, which differs from the match
    // only where a group is what is reported.
    static std::vector<ALTextRange> matches(const ALTextDocument& doc, std::string_view query, const ALTextSearchOptions& options,
                                            const ALTextRange* scope = nullptr, std::string* error = nullptr,
                                            std::vector<ALTextPos>* whole_begins = nullptr);

    // Going forward, the first match starting at or after `from`; going
    // back, the last starting before it; round the ends either way. -1
    // with none.
    static S32 nearest(const std::vector<ALTextRange>& matches, const ALTextPos& from, bool forward);

    // What replaces a match: the text itself, or with $1 and \1 filled
    // from the pattern's groups; in the match's case where that is asked.
    static std::string replacement(const ALTextDocument& doc, const ALTextRange& match, std::string_view query,
                                   const ALTextSearchOptions& options, std::string_view with);
};
