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
// Fonts: one for the document, and any other over a stretch of a line
// where a provider says so -- a heading in a heavier face, a note in the
// reading face inside a code view -- each stretch shaped in its own
// face, the glyphs carrying the face they came from, and every face on a
// row sharing the row's baseline, which is as high as the tallest of
// them asks. A font reload swaps the faces under their LLFontGL and
// bumps LLFontGL::sResolutionGeneration; the layout checks it whenever
// it is asked for anything, and shapes everything again, since the
// glyph ids and the faces it kept are the old font's.
//
// Positions are in the UI's pixels, as every other font measurement the
// widgets see: shaping answers in the screen's, which the UI scale
// divides, and LLFontGL::renderGlyphs multiplies back.
//
// A tab is a gap: a glyph with no face whose advance reaches the next stop.
// An inlay -- a word the view shows beside the text without its being in
// the text, such as a parameter's name before an argument -- is the same
// gap at a column, as wide as whoever provides it says, which the view
// then draws its word into. A substitution shows a stretch of the line
// as other text -- a URL as its label, a key as the name it stands for
// -- or as a box so wide, which is what an atom, an image or a view in
// the text, is to the layout: the stretch's glyphs all share its first
// byte as their cluster, so the caret sits before the stretch or after
// it and nowhere within, and its bytes are never shaped. A row is as
// tall as the font's line, or as the tallest box on it; the text of a
// taller row sits at the bottom, on the baseline the boxes stand on.
class ALTextLayout
{
public:
    // A glyph as the caret and the hit test see it: the byte of the line it
    // begins at, where its pen sat, and how far the pen moved. Several
    // glyphs may share a cluster; a caret sits only where a cluster begins.
    // An inlay's gap carries the inlay's id, and whether it stands before
    // the text at its column -- so the caret at the column sits after it
    // -- or after the text before the column, with the caret before it.
    // A substitution's glyphs carry the substitution's id.
    struct Glyph
    {
        S32  cluster      = 0;
        F32  pen          = 0.f;
        F32  advance      = 0.f;
        S32  inlay        = -1;
        bool inlayBefore  = true;
        S32  substitution = -1;
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

    // What a stretch of a line shows in place of its bytes: other text,
    // or a box so wide, known to the provider by an id. Stretches that
    // overlap are taken first come, the rest dropped.
    struct Substitution
    {
        S32         begin = 0;
        S32         end   = 0;
        // The text shown; empty for a box.
        std::string shown;
        // The box's width and height, in the UI's pixels, where nothing
        // is shown; a height of zero is the row's own.
        F32         width  = 0.f;
        S32         height = 0;
        S32         id     = -1;
    };
    typedef std::function<void(S32 line, std::vector<Substitution>& out)> substitution_provider_t;

    // A stretch of a line shaped in a font of its own.
    struct Run
    {
        S32             begin = 0;
        S32             end   = 0;
        const LLFontGL* font  = nullptr;
    };
    typedef std::function<void(S32 line, std::vector<Run>& out)> run_provider_t;

    // How far in a line's rows start, in the UI's pixels: the first row,
    // and the rows after it where the line wraps -- a log's message
    // carried on under where it began, say. The rows are narrower by as
    // much, and the caret and the hit test count from the line's edge.
    struct Indent
    {
        F32 first = 0.f;
        F32 rest  = 0.f;
    };
    typedef std::function<Indent(S32 line)> indent_provider_t;

    // A row of a line: the bytes it holds, the glyphs it holds, where in
    // the unwrapped line it starts, since the glyphs keep their unwrapped
    // pen; where it sits under the line's top and how tall it is, since
    // a box may make it taller than its text; and its text's own height
    // and ascent, from the tallest font on it, the text sitting at the
    // bottom of the row with its baseline that far under the text's top.
    struct Row
    {
        S32    begin      = 0;
        S32    end        = 0;
        size_t glyphBegin = 0;
        size_t glyphEnd   = 0;
        F32    xStart     = 0.f;
        F32    width      = 0.f;
        S32    top        = 0;
        S32    height     = 0;
        S32    textHeight = 0;
        S32    ascent     = 0;
        // The top of the text within the row, under the row's top.
        S32    textTop() const { return height - textHeight; }
    };

    struct Line
    {
        std::vector<LLFontGL::Placed> placed;
        std::vector<Glyph>            glyphs;
        std::vector<Row>              rows;
        F32                           width  = 0.f;
        S32                           height = 0;
        bool                          valid  = false;
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
    // Asked, as each line is laid out, what stretches of it show as
    // something else; likewise, whoever provides them lays a line out
    // again when they change.
    void setSubstitutionProvider(substitution_provider_t provider);
    // Asked, as each line is laid out, what stretches of it are shaped in
    // a font of their own; likewise.
    void setRunProvider(run_provider_t provider);
    // Asked, as each line is laid out, how far in its rows start;
    // likewise.
    void setIndentProvider(indent_provider_t provider);
    void invalidateLine(S32 index);
    // A space's advance in the UI's pixels, as every other x the layout
    // hands out is: what a column is, for whoever draws by columns.
    F32  columnWidth();

    // The font's line height: what a row is tall unless a box on it is
    // taller, and what a line not yet laid out counts as.
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
    // Moves on whenever which lines are hidden may have changed -- a line
    // hidden or shown, lines made or taken away -- for whoever keeps a list
    // of the lines in sight.
    U32 hiddenRevision() const { return mHiddenRevision; }

    // --- a line ------------------------------------------------------------

    const Line& line(S32 index);
    S32         rowCount(S32 index) { return static_cast<S32>(line(index).rows.size()); }
    S32         lineHeight(S32 index) { return hidden(index) ? 0 : line(index).height; }
    // A row's top under its line's top, and its height; the row a y
    // under the line's top falls in, clamped to the first and the last.
    S32         rowTop(S32 index, S32 row);
    S32         rowHeightOf(S32 index, S32 row);
    S32         rowAtY(S32 index, S32 y);

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

    // How many lines have been laid out, for a test that says a layout is
    // not thrown away for nothing.
    U32 linesLaidOut() const { return mLinesLaidOut; }

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
    // What the lines were laid out under: the fonts' generation and the UI
    // scale as the fonts had it, and the scale as used, which is one where
    // the fonts have none yet.
    S32                                mFontGeneration = -1;
    F32                                mRawScaleX      = -1.f;
    F32                                mRawScaleY      = -1.f;
    F32                                mScaleX         = 1.f;
    F32                                mScaleY         = 1.f;
    U32                                mHiddenRevision = 0;
    U32                                mLinesLaidOut   = 0;
    // Scratch a wrapping loop keeps rather than allocates per line.
    std::vector<size_t>                mBreaks;
    std::vector<ALShapedGlyph>         mShaped;
    inlay_provider_t                   mInlays;
    std::vector<Inlay>                 mInlayScratch;
    substitution_provider_t            mSubstitutions;
    std::vector<Substitution>          mSubstitutionScratch;
    run_provider_t                     mRuns;
    std::vector<Run>                   mRunScratch;
    indent_provider_t                  mIndents;
};
