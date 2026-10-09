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

#include "aldiffedit.h"
#include "altextdiff.h"
#include "alsyntaxgrammar.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <memory>
#include <string>
#include <string_view>
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
// it did, and the rest kept, as the highlighter does. What it read again
// is said of each (ALTextDiff::Reread): how far after the edit the lines'
// regions changed, which a comparison compares again, and no further.
// Where the edit is said (ALTextDiff::Known), the texts it holds are not
// compared with the one asked for to find it.
class ALDiffLexer
{
public:
    explicit ALDiffLexer(std::shared_ptr<const ALSyntaxGrammar> grammar);

    // Each line's regions; held until the text after next is read.
    const std::vector<ALTextDiff::regions_t>& regions(const std::vector<std::string>& lines);
    // As regions(), of a text known to be one it holds edited: that one as
    // it is, or read again in its place from the edit said where that one
    // is the text it would read in the place of; as regions(), where it
    // holds no text so numbered or would read in the place of the other.
    const std::vector<ALTextDiff::regions_t>& regions(const std::vector<std::string>& lines, const ALTextDiff::Known& known);
    // A lexer_t over this, kept alive by what holds it.
    static ALTextDiff::lexer_t lexerOf(std::shared_ptr<ALDiffLexer> lexer);
    // A told_t over this, kept alive by what holds it: given beside a
    // lexer_t over the same.
    static ALTextDiff::told_t  toldOf(std::shared_ptr<ALDiffLexer> lexer);

    // What it says of the text it holds whose regions it answered as
    // `regions` (ALTextDiff::Reread); nothing of one it does not hold.
    ALTextDiff::Reread          reread(const std::vector<ALTextDiff::regions_t>& regions) const;
    // A reread_t over this, kept alive by what holds it: given beside a
    // lexer_t over the same.
    static ALTextDiff::reread_t rereadOf(std::shared_ptr<ALDiffLexer> lexer);

    // How many lines the last text asked for was read again; and how many
    // lines of the texts asked for it has compared with those of a text it
    // held, to find where the two differ: what a test holds an edit's cost
    // to.
    S32 lastRead() const { return mLastRead; }
    U64 linesCompared() const { return mCompared; }

private:
    struct Text
    {
        std::vector<std::string>           lines;
        // The state each line starts in, and the one after the last.
        std::vector<ALSyntaxState>         starts;
        std::vector<ALTextDiff::regions_t> regions;
        U64                                used = 0;
        // Its number, and where it was read again in place of the text
        // held before, that one's and the first line from which every line
        // reads as it did there (ALTextDiff::Reread).
        U64                                number = 0;
        U64                                was    = 0;
        S32                                same   = 0;
    };

    // Lines read already -- a text held, or what one held between where a
    // text read in its place first and last differs from it -- of which a
    // line of a text being read that is one, and starts in the state it
    // did, takes what it was read as: two versions of a script differ in
    // lines here and there all through, and the rest need not be read.
    class Source
    {
    public:
        explicit Source(const Text& text) : mText(text) {}
        // Which of its lines a line is, starting in `state`: the next
        // looked for, past the last taken, else the nearest place past it
        // by its text; -1 for none. Each taken once, in order.
        S32         find(const std::string& line, const ALSyntaxState& state);
        const Text& text() const { return mText; }

    private:
        const Text& mText;
        size_t      mNext    = 0;
        bool        mIndexed = false;
        boost::unordered_flat_map<std::string_view, std::vector<S32>, ll::string_hash, std::equal_to<>> mPlaces;
    };

    // Where a text held and one asked for differ, each pair of lines
    // compared counted.
    ALDiffEdit::Edges edgesOf(const Text& text, const std::vector<std::string>& lines);
    // A text read in the place of the one a slot holds: again in place,
    // where that is mostly this text by where the two differ -- found,
    // where `edges` does not say -- else whole.
    const std::vector<ALTextDiff::regions_t>& readIn(size_t slot, const std::vector<std::string>& lines, const ALDiffEdit::Edges* edges);
    // A text mostly the one a slot held -- a keystroke's -- read again in
    // place: only from where the two first differ until a line after the
    // last starts as it did, what lies between taken from what it held
    // where it can; each line after it read again compared with the
    // regions it had.
    void readAgain(Text& text, const std::vector<std::string>& lines, const ALDiffEdit::Edges& edges);
    // A text read whole, each line taken from what either text held where
    // it can: the second of a comparison's texts read after the first.
    void readWhole(Text& text, const std::vector<std::string>& lines, const Text& other);
    // A line read into its regions from `state`, which it leaves as the
    // line after starts; or taken from the first of the sources that has
    // it.
    void read(const std::string& line, ALSyntaxState& state, ALTextDiff::regions_t& out);
    void readOrTake(const std::string& line, ALSyntaxState& state, ALTextDiff::regions_t& out, Source* sources, size_t count);

    std::shared_ptr<const ALSyntaxGrammar> mGrammar;
    ALSyntaxWords                          mWords;
    std::vector<ALSyntaxToken>             mTokens;
    // A line after an edit as it is read again, beside the regions it had.
    ALTextDiff::regions_t                  mAgain;
    Text                                   mTexts[2];
    U64                                    mClock    = 0;
    // How many texts it has read, which numbers each.
    U64                                    mNumbered = 0;
    S32                                    mLastRead = 0;
    U64                                    mCompared = 0;
};

#endif // AL_ALDIFFLEXER_H
