/**
 * @file alsourcemap.h
 * @brief Where every piece of a preprocessed text came from.
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

#include "stdtypes.h"

#include <string>
#include <vector>

// The map between a preprocessed text and the files it was made from: for
// every token of the output, where the token stands in the output and
// where it came from -- its own place in a file where it was copied over,
// or the place of the macro invocation that produced it. The compiler
// speaks of the output's lines; the author reads the source's. The
// analyzers see the output too, and the editor asks about the source's
// positions, so the map goes both ways. Lines and columns are zero-based
// and columns count bytes, as everything in the studio does.
class ALSourceMap
{
public:
    struct Loc
    {
        // An index into files(), or -1 for nowhere: a line the output has
        // no origin for, or a source position that made no output (a
        // directive, or a line a condition skipped).
        S32 file   = -1;
        S32 line   = 0;
        S32 column = 0;
        bool found() const { return file >= 0; }
    };
    struct Segment
    {
        S32  outLine   = 0;
        S32  outColumn = 0;
        // Bytes of output the segment covers.
        S32  length    = 0;
        S32  file      = 0;
        S32  line      = 0;
        S32  column    = 0;
        // The text is the file's own, so a column within it maps exactly;
        // else it is what a macro made, and every column of it maps to
        // the invocation.
        bool verbatim  = true;
    };

    // A file of the map: what it is called, and what tells it from every
    // other -- an inventory path, a disk path, whatever the caller gave.
    struct File
    {
        std::string name;
        std::string path;
    };
    // files()[0] is the script itself; the rest are what it included, in
    // the order they were first opened.
    const std::vector<File>& files() const { return mFiles; }
    S32                      addFile(const std::string& name, const std::string& path);
    // The file with that identity, or -1.
    S32                      fileOf(const std::string& path) const;

    // Segments arrive in output order.
    void add(const Segment& segment);
    // Builds the indexes; nothing answers until it has been called.
    void finish();
    bool empty() const { return mSegments.empty(); }

    // The source position an output position came from: exact within a
    // verbatim segment, the invocation's for a macro's product, and for a
    // column past or between segments, the nearest segment before it on
    // the line -- or nowhere for a line with none.
    Loc toSource(S32 line, S32 column) const;
    // Where a source position went: within the segment that carries it
    // verbatim, or the start of the output the token at that position was
    // consumed into; a position between tokens goes with the next token on
    // its line, and a line that made no output goes nowhere.
    Loc toExpanded(S32 file, S32 line, S32 column) const;
    // The source stretch an output stretch on one line was copied from,
    // where the segments it runs over copied all of it from one line of
    // one file as it stands: what an edit to the output may be made to the
    // source as. False where any of it is a macro's making, or its pieces
    // came from different places.
    bool verbatimSpan(S32 line, S32 column, S32 endColumn, Loc& begin, Loc& end) const;
    // Where an output line begins in the source, where it begins as the
    // source's line does -- its first segment copied from the same column
    // it stands at, so that the blanks before it are the source's own: what
    // an insertion at the line's start may be made to the source as.
    bool lineStart(S32 line, Loc& at) const;

    // This map over another: this one's origins are positions in the text
    // the other maps, so the result maps this one's output straight to
    // the other's files. A segment whose origin the other map has nothing
    // for is dropped.
    ALSourceMap composed(const ALSourceMap& inner) const;

private:
    std::vector<File>        mFiles;
    std::vector<Segment>     mSegments;
    // The first segment of each output line, by line; and every segment
    // ordered by origin, for the way back.
    std::vector<size_t>      mLineStart;
    std::vector<size_t>      mByOrigin;
};
