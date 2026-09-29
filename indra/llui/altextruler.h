/**
 * @file altextruler.h
 * @brief The bar down the side of a text view: a ruler of where things are, or a map of the text.
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

#include "alsyntaxgrammar.h"
#include "llview.h"

#include <vector>

class ALTextView;

// The bar down the side of a text view. A ruler at its right while the
// text is taller than the view: the caret, the find bar's matches and the
// marks the view's features give (ALTextFeatures::mapMark) down it, and a
// thumb that fades once the mouse has left and the text has settled. Or,
// where the view asks for one, a map of the text in its place, at the right
// or the left: the lines drawn small, the rows on screen as a window over
// them. A press on either takes the text there, and a drag follows; the
// mouse resting on the map previews the lines under it.
//
// A child of the view it is the bar of, put where it goes by the view
// (ALTextView::placeRuler) and made before anything else the view holds,
// so that the find bar, a list or a card is drawn over it. What it shows --
// the text, its layout, how far it is scrolled, the view's colours -- it
// reads from the view as it draws; what it keeps is only what saves it
// reading the whole text again on a frame where nothing has changed.
class ALTextRuler final : public LLView
{
public:
    AL_VIEW_TYPE(ALTextRuler, LLView);

    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Mandatory<ALTextView*> view;

        Params();
    };

    // The ruler's width, and the least a thumb may be.
    static constexpr S32 WIDTH     = 12;
    static constexpr S32 THUMB_MIN = 24;

    // The lines around the one under the mouse on the map, drawn beside it
    // in the view's own face and colours while the mouse rests there and
    // is not dragging. Over everything the view holds: the view draws it
    // after its children, in the ruler's own coordinates.
    void drawPreview(F32 alpha);
    // Whether a press on it is being dragged.
    bool dragging() const { return mDragging; }

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    void onMouseLeave(S32 x, S32 y, MASK mask) override;
    void onMouseCaptureLost() override;

protected:
    friend class LLUICtrlFactory;
    ALTextRuler(const Params& p);

private:
    // Whether it is the map rather than the ruler, as the view has it now.
    bool isMap() const;

    // The ruler: the thumb on its track, the view scrolled so that the
    // thumb's top is `offset` from a y, and the whole drawn.
    LLRect vThumb(const LLRect& track);
    void   scrollToRulerY(S32 y, S32 offset);
    void   drawRuler(F32 alpha);

    // The map: the lines it shows, hidden ones left out, and how far its
    // window is scrolled; the line at a y of it; the view scrolled so a
    // y of it is in the middle.
    S32  mapScroll(const LLRect& map);
    S32  mapLineAt(S32 y);
    void scrollToMapY(S32 y);
    void drawMap(F32 alpha);
    // The runs of one line for the map, by column, as far as `columns`.
    struct MapRun
    {
        S32          from = 0;
        S32          to   = 0;
        ALSyntaxKind kind = ALSyntaxKind::Text;
    };
    void readMapRuns(S32 line, S32 columns, std::vector<MapRun>& out);

    ALTextView& mView;
    // A press held: where on the thumb it took hold, for the ruler.
    bool mDragging   = false;
    S32  mDragOffset = 0;
    // Where the mouse rests on the map, or -1: what the preview is of.
    S32  mHoverY     = -1;

    // The lines the map shows, as of the layout's hidden revision and the
    // line count.
    std::vector<S32> mMapLines;
    bool             mMapLinesValid    = false;
    U32              mMapLinesRevision = 0;
    S32              mMapLinesCount    = 0;
    // The map's runs of text for the lines in sight, by column, as last
    // read: kept while the text, its grammar and each line's tokens, the
    // tab width, the map's width and the lines in sight hold, so that an
    // idle frame does not read every line again.
    struct MapRuns
    {
        U32                 version  = 0;
        const void*         grammar  = nullptr;
        S32                 tabWidth = -1;
        S32                 columns  = -1;
        S32                 first    = -1;
        S32                 last     = -1;
        U32                 hidden   = 0;
        // For each line in sight, where its runs start, and its tokens'
        // revision; the runs of the last end where the list does.
        std::vector<size_t> starts;
        std::vector<U32>    revisions;
        std::vector<MapRun> runs;
    };
    MapRuns mMapRuns;
    // The ruler's lines with a mark, as of the text's version and the
    // marks' revision.
    std::vector<S32> mMarkLines;
    bool             mMarksValid    = false;
    U32              mMarksVersion  = 0;
    U32              mMarksRevision = 0;
    // Each pixel row of the ruler's track with a match on it, as last found:
    // for which matches, which track and which text's height.
    std::vector<U8> mMatchRows;
    bool            mMatchRowsValid      = false;
    U32             mMatchRowsGeneration = 0;
    S32             mMatchRowsTop        = 0;
    S32             mMatchRowsHeight     = 0;
    S32             mMatchRowsTotal      = 0;
};
