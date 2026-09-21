/**
 * @file altextlayout.h
 * @brief Where every glyph of a document's lines sits, laid out once and kept.
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

#include "alfontshaping.h"
#include "altextdocument.h"
#include "llfontgl.h"

#include <boost/signals2.hpp>

#include <functional>
#include <vector>

// The layout of a document's lines: each line shaped once, into glyphs with
// their pen positions, and cut into rows where it wraps; each row a height;
// every line a top, so a view can find the line under a pixel and the pixel
// under a caret. Laid out lazily, line by line, as a view asks; an edit
// throws away the lines it touched and nothing else; a change of font, tab
// width or wrap width throws away everything.
//
// Fonts: one face for the whole document for now. The runs a line is
// shaped in are where a second face would enter, and the glyphs already
// carry the face they came from. A font reload swaps the faces under
// their LLFontGL and bumps LLFontGL::sResolutionGeneration; the layout
// checks it whenever it is asked for anything, and shapes everything
// again, since the glyph ids and the faces it kept are the old font's.
//
// Positions are in the UI's pixels, as every other font measurement the
// widgets see: shaping answers in the screen's, which the UI scale
// divides, and LLFontGL::renderGlyphs multiplies back.
//
// A tab is a gap: a glyph with no face whose advance reaches the next stop.
// An inlay -- a word the view shows beside the text without its being in
// the text, such as a parameter's name before an argument -- is the same
// gap at a column, as wide as whoever provides it says, which the view
// then draws its word into. An atom -- an image or a view in the text --
// will be the same gap with a height of its own, when there is one.
class ALTextLayout
{
public:
    // A glyph as the caret and the hit test see it: the byte of the line it
    // begins at, where its pen sat, and how far the pen moved. Several
    // glyphs may share a cluster; a caret sits only where a cluster begins.
    // An inlay's gap carries the inlay's id, and whether it stands before
    // the text at its column -- so the caret at the column sits after it
    // -- or after the text before the column, with the caret before it.
    struct Glyph
    {
        S32  cluster     = 0;
        F32  pen         = 0.f;
        F32  advance     = 0.f;
        S32  inlay       = -1;
        bool inlayBefore = true;
    };

    // What goes beside a line's text: at a byte column, so wide, before
    // or after the text there, known to the provider by an id.
    struct Inlay
    {
        S32  column = 0;
        F32  width  = 0.f;
        bool before = true;
        S32  id     = -1;
    };
    typedef std::function<void(S32 line, std::vector<Inlay>& out)> inlay_provider_t;

    // A row of a line: the bytes it holds, the glyphs it holds, and where in
    // the unwrapped line it starts, since the glyphs keep their unwrapped pen.
    struct Row
    {
        S32    begin      = 0;
        S32    end        = 0;
        size_t glyphBegin = 0;
        size_t glyphEnd   = 0;
        F32    xStart     = 0.f;
        F32    width      = 0.f;
    };

    struct Line
    {
        std::vector<LLFontGL::Placed> placed;
        std::vector<Glyph>            glyphs;
        std::vector<Row>              rows;
        F32                           width = 0.f;
        bool                          valid = false;
    };

    ALTextLayout();
    ~ALTextLayout();
    ALTextLayout(const ALTextLayout&) = delete;
    ALTextLayout& operator=(const ALTextLayout&) = delete;

    void attach(ALTextDocument* document);

    void            setFont(const LLFontGL* font);
    const LLFontGL* font() const { return mFont; }
    // In pixels; nothing wraps at zero.
    void setWrapWidth(S32 pixels);
    S32  wrapWidth() const { return mWrapWidth; }
    // In spaces.
    void setTabWidth(S32 spaces);
    S32  tabWidth() const { return mTabWidth; }
    // Asked, as each line is laid out, what goes beside it; the widths
    // in the UI's pixels. Whoever provides them lays a line out again,
    // through invalidateLine, when they change.
    void setInlayProvider(inlay_provider_t provider);
    void invalidateLine(S32 index);
    // A space's advance, in the screen's pixels: what a column is, for
    // whoever draws by columns.
    F32  columnWidth() { return spaceAdvance(); }

    // Every row is this tall.
    S32 rowHeight() const;
    S32 lineCount() const { return static_cast<S32>(mLines.size()); }
    // The widest line, in pixels. A line not yet laid out counts its bytes
    // at a space's width, which is near enough for a scrollbar.
    F32 contentWidth();

    // --- hidden lines ----------------------------------------------------------

    // A hidden line is folded away: it keeps its layout and takes no
    // height, so the line after it sits where it would have. Who hides
    // what is the view's business; this only lays out around it.
    void setHidden(S32 first, S32 last, bool hidden);
    bool hidden(S32 index) const { return index >= 0 && index < static_cast<S32>(mHidden.size()) && mHidden[index]; }
    bool anyHidden() const { return mHiddenCount > 0; }
    // The nearest line not hidden, starting at this one and looking in
    // this direction (1 or -1); -1 where there is none.
    S32 visibleFrom(S32 index, S32 direction) const;

    // --- a line ------------------------------------------------------------

    const Line& line(S32 index);
    S32         rowCount(S32 index) { return static_cast<S32>(line(index).rows.size()); }
    S32         lineHeight(S32 index) { return hidden(index) ? 0 : rowCount(index) * rowHeight(); }

    // --- the column ----------------------------------------------------------

    // The top of a line from the top of the document, and the whole. Lines
    // not yet laid out count as one row.
    S32 lineTop(S32 index);
    S32 totalHeight();
    // The line whose rows cover a y, clamped to the first and the last;
    // never a hidden one where any line is not.
    S32 lineAtY(S32 y);

    // --- the caret -------------------------------------------------------------

    // The row a column of a line falls in, and the x of the column within
    // that row. A column at a wrap point is the start of the next row.
    S32 rowOf(S32 index, S32 column);
    F32 xOf(S32 index, S32 column, S32* row = nullptr);
    // The column at an x within a row: the nearest cluster boundary where
    // `round`, else the one at or before.
    S32 columnAt(S32 index, S32 row, F32 x, bool round);

private:
    void onEdit(const ALTextDocument::Edit& edit);
    void invalidateAll();
    void layoutLine(S32 index, Line& out);
    void ensureTops();
    // A space's advance, in the screen's pixels.
    F32  spaceAdvance();
    // Throws everything away when the fonts were reloaded or the UI
    // scale changed since the last layout.
    void refreshIfFontsChanged();

    ALTextDocument*                    mDocument = nullptr;
    boost::signals2::scoped_connection mConnection;
    const LLFontGL*                    mFont      = nullptr;
    S32                                mWrapWidth = 0;
    S32                                mTabWidth  = 4;
    std::vector<Line>                  mLines;
    std::vector<U8>                    mHidden;
    S32                                mHiddenCount = 0;
    // mTops[i] is the top of line i; mTops[count] the whole height.
    std::vector<S32>                   mTops;
    bool                               mTopsDirty    = true;
    F32                                mSpaceAdvance = -1.f;
    // Negative until asked for.
    F32                                mContentWidth = -1.f;
    // What the lines were laid out under.
    S32                                mFontGeneration = -1;
    F32                                mScaleX         = 1.f;
    F32                                mScaleY         = 1.f;
    // Scratch a wrapping loop keeps rather than allocates per line.
    std::vector<size_t>                mBreaks;
    std::vector<ALShapedGlyph>         mShaped;
    inlay_provider_t                   mInlays;
    std::vector<Inlay>                 mInlayScratch;
};
