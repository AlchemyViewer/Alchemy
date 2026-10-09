/**
 * @file alfoldmodel.h
 * @brief The blocks of a code editor's text that fold, and which of them are folded.
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

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The blocks of a code editor's text that fold, and which are folded.
//
// Where whoever holds the text knows its syntax -- the editor, from its
// bracket index and its grammar -- a block is what the syntax opens and
// closes: a bracket and its partner, `function` and its `end`, across
// lines, from the line it opens on -- or, where it opens a line of its
// own, as a brace under its header does, from the header -- through the
// line it closes on, unless something opens again after the close there
// (`} else {`), which that line then starts. Elsewhere a block is a line
// and the deeper lines after it, by indentation alone -- any language, a
// line of nothing going with whichever side keeps the block whole -- with
// the closer on the line after it taken as part of it, and a brace on a
// line of its own folding with the header above it. Either way a comment
// `#region` and its `#endregion` fold what is between them too.
//
// Each line is read once, until its text changes or, with syntax, its
// tokens do, which it is told of rather than asks every line; the blocks
// are found again from what was read when the text changes -- by syntax,
// walked again only from before the lines that changed until the walk is
// where it was, and put together again from what that walk changed; by
// indentation in one pass. The folds slide with its
// edits, and one whose block is gone goes.
//
// Worked out over a document alone -- one document's, whose version says
// when its blocks are found again -- the editor hides the lines a fold
// takes from its layout, and moves the caret out of what is folded away.
class ALFoldModel
{
public:
    // A block: the line it starts on stays in sight, the lines through
    // `end` go when it is folded.
    struct Region
    {
        S32 start = 0;
        S32 end   = 0;
    };

    // What a line holds of the blocks its syntax makes, in the order it
    // holds them: what opens one, what closes one, and what closes one and
    // opens the next -- an `else`.
    enum class Event : U8
    {
        Open,
        Close,
        Middle,
    };
    struct Block
    {
        S32   column = 0;
        Event event  = Event::Open;
        // The first thing on its line, as the model reads it.
        bool  first  = false;
    };
    typedef std::function<void(S32 line, std::vector<Block>& out)> blocks_t;
    typedef std::function<void(S32 line)>                           lex_t;
    // The syntax, where it is known: each line's blocks, and what lexes the
    // text through a line, after which relexed() has been told of every
    // line through it whose blocks may have changed -- whose tokens did --
    // so that a line is asked again only then. None: blocks by
    // indentation.
    void setSyntax(blocks_t blocks, lex_t lex);
    bool hasSyntax() const { return static_cast<bool>(mBlocks); }
    // The lines `first` through `last` lexed anew, their tokens changed:
    // their blocks read again when next they are wanted.
    void relexed(S32 first, S32 last);
    // The line comment the text is written with, whose `#region` and
    // `#endregion` fold what is between them.
    void setLineComment(std::string token);

    // Every block in the text, by start line, found again where the
    // document has changed since, or `invalidate` said so. Tabs are as
    // wide as `tab_width` says.
    const std::vector<Region>& regions(const ALTextDocument& doc, S32 tab_width);
    void                       invalidate()
    {
        mValid    = false;
        mWalkKept = false;
    }
    // How many lines the last finding of the blocks by syntax walked, and
    // whether it put them together from the walk's changes alone: for a
    // test that says an edit walks and puts together only what it must.
    S32                        lastWalked() const { return mLastWalked; }
    bool                       lastSpliced() const { return mLastSpliced; }
    // The blocks as last found, not found again: what a view draws while
    // the syntax they are found by is still being worked out down the
    // text, which finding them now would work out all at once.
    const std::vector<Region>& lastFound() const { return mRegions; }
    // The block that starts at a line; and the innermost one around it --
    // of those that hold it, the one that starts last.
    const Region* startingAt(const ALTextDocument& doc, S32 tab_width, S32 line);
    const Region* around(const ALTextDocument& doc, S32 tab_width, S32 line);

    // The first lines of the blocks open at a line that start above it,
    // outermost first, the innermost `most` of them -- a sticky header's
    // -- looked for no more than `reach` lines up: by the syntax, walked
    // back from the line, so that nothing below it is read; by
    // indentation, from the blocks.
    std::vector<S32> openAt(const ALTextDocument& doc, S32 tab_width, S32 line, size_t most, S32 reach = 2000);

    // How far in a line's text begins, tabs to their stops; a line of
    // nothing takes the next line's with anything on it, within `reach`,
    // else 0 -- as an indent guide draws it.
    S32 indentOf(const ALTextDocument& doc, S32 tab_width, S32 line, S32 reach = 200);

    // Whether a folded block starts at the line; and the start lines of
    // those that are, in order.
    bool                    isFolded(S32 line) const;
    const std::vector<S32>& folded() const { return mFolded; }

    // The block that starts at the line, else the innermost one around it,
    // folded: the block, or nothing where there is none, or it is folded.
    std::optional<Region> fold(const ALTextDocument& doc, S32 tab_width, S32 line);
    // The folded block that starts at the line, else the innermost folded
    // one around it, opened; false where there is none.
    bool unfold(const ALTextDocument& doc, S32 tab_width, S32 line);
    void foldAll(const ALTextDocument& doc, S32 tab_width);
    void unfoldAll() { mFolded.clear(); }
    // Every folded block a line is inside opened, and every fold whose
    // block is gone; false where there was none of either.
    bool reveal(const ALTextDocument& doc, S32 tab_width, S32 line);
    // The lines hidden, each folded block's after its first; a fold whose
    // block is gone let go of first.
    std::vector<std::pair<S32, S32>> hidden(const ALTextDocument& doc, S32 tab_width);

    // An edit of the text, over lines `first` through `last` of it as it
    // was, which the edit made into `made` lines. A fold that starts on
    // the edit's first line stays where the edit began inside that line,
    // or only typed at its start: typing on a block's first line is not
    // opening the block. From the line's start, whole lines put in above
    // it take it down with them, and anything more takes it. One on the
    // edit's last line stays too where the edit ended at that line's
    // start and left it a line of its own -- whole lines taken from above
    // a folded block -- and moves with it; the rest inside go, and those
    // after move along, two that land on one line becoming one. The
    // blocks are found again. A batch's runs of lines each so, one after
    // another. `lines`, the text's lines after it, where known.
    void edited(const ALTextDocument::Edit& edit, S32 lines = -1);

private:
    // What a line was read as: how far in its text begins, or -1 for none;
    // whether it is a closer alone, a brace alone; a region's marker, +1
    // or -1; and its syntax's blocks.
    struct Line
    {
        bool               valid    = false;
        S32                indent   = -1;
        bool               closes   = false;
        bool               brace    = false;
        S8                 marker   = 0;
        std::vector<Block> blocks;
    };
    const Line& lineAt(const ALTextDocument& doc, S32 line);
    // Tabs as wide as this from now, every line read again where they were
    // another width.
    void        setTabWidth(S32 tab_width);
    // Each block's last line by its first, into `end_of`, one a line, -1
    // where none starts.
    void        bySyntax(std::vector<S32>& end_of) const;
    void        byIndent(const ALTextDocument& doc, std::vector<S32>& end_of);
    // A `#region` or an `#endregion` met as the lines are walked in order:
    // one opened, or the innermost still open ended there.
    static void pairMarker(S8 marker, S32 line, std::vector<S32>& open, std::vector<S32>& end_of);

    std::vector<Region> mRegions;
    // What finding the blocks works with, kept from one finding to the
    // next rather than made again after every edit: each line's block end,
    // what is open as the syntax is walked -- where each opened, whether it
    // opened its line, and the line it folds from -- the region markers
    // open, and each line's indentation.
    struct Opened
    {
        S32  line   = 0;
        bool first  = false;
        S32  target = 0;

        bool operator==(const Opened& other) const { return line == other.line && first == other.first && target == other.target; }
    };
    std::vector<S32>    mEndOf;
    std::vector<Opened> mOpen;
    std::vector<S32>    mMarked;
    std::vector<S32>    mIndents;

    // The walk by syntax kept from one finding to the next, so that after
    // an edit it is walked again only from before the lines that changed
    // until it is where it was before them: each block it closed, with the
    // line that closed it and the line it folds from -- its start, or for
    // one opened on a line of its own its header, as it was told when it
    // opened -- in the order it closed them; and every
    // so many lines, what was open there, how many it had closed before,
    // and how far down what was open the lines from there to the next such
    // place reached -- what they closed of it -- so that where only what
    // is under that differs, as under a brace put in above, those lines
    // close what they closed before and are not walked again.
    struct Closed
    {
        S32  line   = 0;
        S32  start  = 0;
        S32  end    = 0;
        bool alone  = false;
        S32  target = 0;
    };
    struct Walked
    {
        S32                 line   = 0;
        size_t              closed = 0;
        std::vector<Opened> open;
        std::vector<S32>    marked;
        size_t              openLow   = 0;
        size_t              markedLow = 0;
    };
    static constexpr S32 WALK_STEP = 128;
    // The walk, again from before the lines changed or whole; and one line
    // of it, which closes what it closes of what was open before it, and
    // takes the low marks down to as little as it leaves open.
    void walkSyntax(const ALTextDocument& doc);
    void walkLine(const ALTextDocument& doc, const Line& line, S32 at, std::vector<Opened>& open, std::vector<S32>& marked, std::vector<Closed>& closed,
                  size_t& open_low, size_t& marked_low);
    // The line a block opened on a line of its own at `at` folds from: its
    // header, the line with anything on it above, where that opens nothing
    // of its own still open -- a brace under `default` folds with it -- and
    // else its own.
    S32  aloneTarget(const ALTextDocument& doc, S32 at, const std::vector<Opened>& open, const std::vector<S32>& marked);
    // What the last walk changed, where the blocks may be put together from
    // it alone: the lines it walked again, `from` up to `to`, where it
    // rejoined the walk kept; the lines what it took out and put in fold
    // from; the lines what was open where it rejoined folds from; and where
    // what it put in is among those closed.
    struct Splice
    {
        bool             ok         = false;
        S32              from       = 0;
        S32              to         = 0;
        std::vector<S32> touched;
        std::vector<S32> kept;
        size_t           freshBegin = 0;
        size_t           freshEnd   = 0;
    };
    // The blocks found last, moved along with the edits since, put together
    // again from the walk's changes alone.
    void spliceRegions();
    Splice               mSplice;
    bool                 mRegionsKept = false;
    bool                 mLastSpliced = false;
    std::vector<Closed> mClosed;
    std::vector<Walked> mWalked;
    // The lines changed since the walk -- edited, or lexed anew -- none
    // where the first is past the last; the lines the text had, as the
    // edits said, which a count that differs makes walked whole; whether
    // what is kept is any use; and how many lines the last finding walked.
    S32  mDirtyFirst = 0;
    S32  mDirtyLast  = -1;
    S32  mWalkLines  = 0;
    bool mWalkKept   = false;
    S32  mLastWalked = 0;
    U32                 mVersion  = 0;
    // Tabs are measured by it where a line mixes them with spaces.
    S32                 mTabWidth = 0;
    bool                mValid    = false;
    std::vector<S32>    mFolded;
    ALLineTable<Line>   mLines;
    blocks_t            mBlocks;
    lex_t               mLex;
    std::string         mLineComment;
};
