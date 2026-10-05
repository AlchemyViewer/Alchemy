/**
 * @file aldifflexer.h
 * @brief A text's lines read by a grammar into the regions a comparison cuts words by.
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

#ifndef AL_ALDIFFLEXER_H
#define AL_ALDIFFLEXER_H

#include "altextdiff.h"
#include "alsyntaxgrammar.h"

#include <memory>
#include <string>
#include <vector>

// A text's lines read by a grammar into what ALTextDiff cuts their words
// by: the stretches that are strings and comments, the rest code -- a
// comparison's lexer (ALTextDiff::lexer_t). Each line read from the state
// the line before it left, so that the lines inside a block comment are
// its.
//
// The last two texts it read are kept, with the state each line starts
// in: a text read again after an edit -- a live comparison's right as it
// is typed in -- is read again only from the edit until a line starts as
// it did, and the rest kept, as the highlighter does.
class ALDiffLexer
{
public:
    explicit ALDiffLexer(std::shared_ptr<const ALSyntaxGrammar> grammar);

    // Each line's regions; held until the text after next is read.
    const std::vector<ALTextDiff::regions_t>& regions(const std::vector<std::string>& lines);
    // A lexer_t over this, kept alive by what holds it.
    static ALTextDiff::lexer_t lexerOf(std::shared_ptr<ALDiffLexer> lexer);

    // How many lines the last text asked for was read again: what a test
    // holds an edit's cost to.
    S32 lastRead() const { return mLastRead; }

private:
    struct Text
    {
        std::vector<std::string>           lines;
        // The state each line starts in, and the one after the last.
        std::vector<ALSyntaxState>         starts;
        std::vector<ALTextDiff::regions_t> regions;
        U64                                used = 0;
    };

    std::shared_ptr<const ALSyntaxGrammar> mGrammar;
    ALSyntaxWords                          mWords;
    std::vector<ALSyntaxToken>             mTokens;
    Text                                   mTexts[2];
    U64                                    mClock    = 0;
    S32                                    mLastRead = 0;
};

#endif // AL_ALDIFFLEXER_H
