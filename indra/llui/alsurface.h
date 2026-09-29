/**
 * @file alsurface.h
 * @brief The one look every small thing floating over the editor's text wears.
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

#include "llrect.h"
#include "lluicolor.h"
#include "v4color.h"

// A hover card, a completion list, a signature, the quick-open window, the
// find bar, the map's preview: all of them are the same thing, a small
// surface held over the text for a moment. They looked like six, because
// each was mixed where it was drawn and the numbers drifted -- a ground at
// 0.06 here and 0.08 there, a frame in a quarter of the ink here and in the
// fold colour there.
//
// So the recipe lives in one place and every surface asks for it. Two
// colours, both made from the two the view already has: the paper it draws
// its text on and the ink it draws the text in. Nothing here reads the
// colour table, so a surface follows whatever theme its view is wearing
// rather than whatever the skin was built with.
namespace ALSurface
{
    // How far the ground is carried from the paper towards the ink, and
    // how much of the ink the frame is. Held apart from the functions so
    // that a caller wanting the shade without the surface -- a gutter, a
    // sticky header -- can say which one it means.
    inline constexpr F32 GROUND = 0.06f;
    inline constexpr F32 FRAME  = 0.25f;
    // The row a list is pointing at, which is a small ground held over the
    // surface's own and drifted the same way -- 0.12 in one list, 0.25 in
    // another -- until it was written down here.
    inline constexpr F32 CHOSEN = 0.18f;

    // So far from the paper towards the ink, opaque. The mix is in the
    // colours as they are: these are the view's own, already resolved.
    LLColor4 shade(const LLColor4& paper, const LLColor4& ink, F32 amount);

    // What a surface sits on: far enough off the paper to be seen against
    // it, near enough that text stays as readable as it was.
    inline LLColor4 ground(const LLColor4& paper, const LLColor4& ink) { return shade(paper, ink, GROUND); }

    // The band under the row a list is pointing at.
    inline LLColor4 chosen(const LLColor4& paper, const LLColor4& ink) { return shade(paper, ink, CHOSEN); }
    // The same band for a list with no paper of its own, drawn over
    // whatever it sits on: the ink at the same weight, which over the paper
    // comes to the same mix.
    inline LLColor4 chosenOver(const LLColor4& ink) { return LLColor4(ink.mV[VRED], ink.mV[VGREEN], ink.mV[VBLUE], CHOSEN); }

    // How far apart two colours read, as WCAG 2 measures it: 1 for a
    // colour against itself, 21 for black against white. The first is
    // taken as drawn over the second, where it is not opaque. Text wants
    // LEGIBLE at the least.
    inline constexpr F32 LEGIBLE = 4.5f;
    F32 contrast(const LLColor4& ink, const LLColor4& paper);
    // What a word quietened from `ink` towards `wanted` may come to and
    // still read at `least` against `paper`: `wanted` itself where it
    // does, else the nearest to it that does on the way from the ink.
    LLColor4 legible(const LLColor4& ink, const LLColor4& wanted, const LLColor4& paper, F32 least = LEGIBLE);

    // The line around it: the ink itself, thinned. A mix would tie the
    // frame to the ground it is drawn against; the ink thinned reads the
    // same over the ground, over the text and over a selection.
    LLColor4 frame(const LLColor4& ink, F32 alpha = 1.f);

    // Both, over a rect: the ground filled, the frame around it. `alpha`
    // is the draw context's, which every caller has and none should have
    // to remember to apply twice.
    void draw(const LLRect& rect, const LLColor4& paper, const LLColor4& ink, F32 alpha);

    // The skin's colours, by what each is in a control -- the other half of
    // how our controls are drawn. Unlike the recipe above these read the
    // colour table: a dial or a swatch is drawn in the viewer's theme,
    // whatever view it sits in. Each control draws them at the alpha it
    // always has; what is one place is which colour plays which part.
    //
    // The ground a picture sits in -- a dial's face, a pad -- and the line
    // round a swatch or a strip, drawn in the same shade.
    const LLUIColor& well();
    // A line round or across a picture: a dial's rim, a pad's grid, the
    // frame a rule is drawn in.
    const LLUIColor& rim();
    // What is moved, marked or set: a dial's handle, a pad's dot, an edge
    // that is tied.
    const LLUIColor& handle();
    // A control's own words, and words that are there to be passed over:
    // a caption, a value in force that nobody wrote.
    const LLUIColor& text();
    const LLUIColor& quiet();
}
