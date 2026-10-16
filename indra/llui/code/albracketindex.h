/**
 * @file albracketindex.h
 * @brief Where a text's brackets pair up, found by the line rather than by the byte.
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

#include "allinetable.h"
#include "altextdocument.h"

#include <limits>
#include <utility>
#include <vector>

class ALSyntaxHighlighter;

// Where the round, square and curly brackets of a text pair up, as a code
// editor draws them, colours them by depth and moves by them, and vim
// jumps and selects by them: one set of rules for all of them. A bracket
// in a string or a comment -- as the highlighter's tokens say, where there
// is one -- is none.
//
// Each line is summed up once, until its text or its tokens change: the
// brackets on it that are code, and for each kind how its brackets nest
// across the line. A search walks the lines by these, passing a whole line
// where what it looks for cannot be on it; and goes only as far as it is
// told, since an unmatched bracket at the caret is otherwise a walk -- and
// a lexing -- of everything to the text's end. How many brackets of any
// kind are open at each line's start is kept too, from the top, until an
// edit above it.
class ALBracketIndex
{
public:
    // How far a search goes, in lines, unless told: a match shown at the
    // caret as it moves. And no bound, for a jump asked for.
    static constexpr S32 NEARBY   = 2000;
    static constexpr S32 ANYWHERE = std::numeric_limits<S32>::max();

    // Over a text, the highlighter's tokens saying what is a string or a
    // comment; without one, every bracket counts.
    explicit ALBracketIndex(ALSyntaxHighlighter* highlighter = nullptr);
    void attach(const ALTextDocument* document);
    // Everything summed up again: the grammar changed.
    void reset();

    // Whether a character is a bracket, which it pairs with, and whether
    // it opens.
    static bool bracketOf(char c, char& partner, bool& opens);

    // The partner of the bracket at a place, only its own kind's nesting
    // counted; false where the place holds no bracket that is code, or its
    // partner is not within `lines` of it.
    bool match(const ALTextPos& at, ALTextPos& out, S32 lines = NEARBY);
    // The `count`th bracket of a kind left open around a place, the place
    // itself not counted: before it for an opener -- `(` for vim's [( --
    // after it for a closer -- `)` for ]). Only that kind's nesting is
    // counted; false where there are not so many within `lines`.
    bool enclosing(const ALTextPos& at, char bracket, S32 count, ALTextPos& out, S32 lines = NEARBY);

    // How many brackets of any kind are open at a line's start, and at a
    // place of a line: a closer past none closes nothing.
    S32 depthBefore(S32 line);
    S32 depthAt(const ALTextPos& at);
    // The brackets of a line that are code, by where each is, in order.
    const std::vector<std::pair<S32, char>>& bracketsOn(S32 line);

private:
    struct Line
    {
        bool valid    = false;
        U32  revision = 0;
        std::vector<std::pair<S32, char>> brackets;
        // For each kind -- round, square, curly -- openers less closers
        // across the line; the lowest that count goes from the line's
        // start; and the lowest closers less openers goes from its end.
        S32 net[3]       = { 0, 0, 0 };
        S32 fromStart[3] = { 0, 0, 0 };
        S32 fromEnd[3]   = { 0, 0, 0 };
        // Every kind at once: openers less closers, and the lowest that
        // goes from the line's start.
        S32 allNet    = 0;
        S32 allLowest = 0;
    };
    const Line& lineAt(S32 line);
    void        edited(const ALTextDocument::Edit& edit);

    ALSyntaxHighlighter*               mHighlighter = nullptr;
    const ALTextDocument*              mDocument    = nullptr;
    boost::signals2::scoped_connection mConnection;
    ALLineTable<Line>                  mLines;
    // The depth entering each line, known for the first mDepthKnown; and
    // the grammar it was known by, since another lexes every line anew.
    std::vector<S32>                   mDepthBefore;
    S32                                mDepthKnown = 0;
    const void*                        mGrammar    = nullptr;
};
