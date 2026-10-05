/**
 * @file aldifftokens.h
 * @brief A line cut into the words two lines are compared by.
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

#ifndef AL_ALDIFFTOKENS_H
#define AL_ALDIFFTOKENS_H

#include "altextdiff.h"

#include <string_view>
#include <vector>

// A line cut into the words a comparison weighs and marks. By its bytes
// alone, where nothing says what its stretches are: a run of word bytes
// (identifiers and numbers), a run of blanks, each other byte. By its
// regions, where a grammar has said: code as a language's tokens -- a
// name, a number with its point and its exponent, an operator of more than
// one character, a run of blanks, each other character -- and a string's
// or a comment's text as prose: a word with the apostrophes inside it, a
// run of blanks, each other character.
namespace ALDiffTokens
{
    bool blank(char c);
    // Its words by their bytes alone, each as [begin, end) in it, covering
    // it.
    ALTextDiff::spans_t words(std::string_view line);

    // A word of a line, as [begin, end) in it, and the region it is in.
    struct Token
    {
        S32                begin  = 0;
        S32                end    = 0;
        ALTextDiff::Region region = ALTextDiff::Region::Code;

        bool operator==(const Token& other) const = default;
    };
    typedef std::vector<Token> tokens_t;
    // Its words, covering it: by its regions where it has any, else by its
    // bytes alone.
    void cut(std::string_view line, const ALTextDiff::regions_t* regions, tokens_t& out);
    // Whether a word is a run of blanks; of word bytes.
    bool isBlank(std::string_view line, const Token& token);
    bool isWord(std::string_view line, const Token& token);
}

#endif // AL_ALDIFFTOKENS_H
