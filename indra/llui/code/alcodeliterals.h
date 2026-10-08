/**
 * @file alcodeliterals.h
 * @brief A text's string and path literals, as its grammar's tokens have them.
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

#include <optional>
#include <string>
#include <string_view>

class ALSyntaxHighlighter;

// The string and path literals of a text, as its highlighter's tokens have
// them: the literal a place is in, what one holds, where a string names a
// file, and what a string comes to. Over a text and its highlighter, which
// it reads and never changes, and free of any view: what the code editor
// completes in a string by, and says of one on its card.
class ALCodeLiterals
{
public:
    ALCodeLiterals(const ALTextDocument& document, ALSyntaxHighlighter& highlighter);

    // The whole string literal a position is in, quotes and all: the run
    // of string and escape tokens around it, carried across lines while
    // one begins or ends inside a string, so that a long string is one
    // literal rather than a line of one. Empty where the position is not
    // in a string.
    ALTextRange stringAt(const ALTextPos& pos) const;
    // What a string that names a file holds -- between its quotes, or to
    // the line's end where it is not closed; empty where it holds nothing
    // yet -- where a position is in it or at its end: a string the grammar
    // says names one, by what comes before its opening quote
    // (ALSyntaxGrammar::pathString). None anywhere else.
    std::optional<ALTextRange> pathAt(const ALTextPos& pos) const;
    // The same of any string or path on its line, the grammar not asked:
    // what it holds, the byte that opens it -- a quote, or an include's
    // `<` -- and whether it is closed on its line.
    std::optional<ALTextRange> quotedAt(const ALTextPos& pos, char* opener = nullptr, bool* closed = nullptr) const;
    // What to say about one: its size, which is what a scripter wants of
    // a string and what the type alone never says -- the bytes it comes
    // to, the characters where they are not the same number, and what it
    // is written as where the escapes make that longer.
    std::string stringSize(const ALTextRange& literal) const;

    // What a string's text reads as, for the names a host offers for it to
    // be matched against: a quote or a backslash escaped is itself -- the
    // escapes a host writes its names with -- and an escape only begun at
    // the end is nothing yet. Any other is left as written, which no name
    // holds.
    static std::string unescaped(std::string_view written);

private:
    const ALTextDocument& mDocument;
    ALSyntaxHighlighter&  mHighlighter;
};
