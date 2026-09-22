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

    // The line around it: the ink itself, thinned. A mix would tie the
    // frame to the ground it is drawn against; the ink thinned reads the
    // same over the ground, over the text and over a selection.
    LLColor4 frame(const LLColor4& ink, F32 alpha = 1.f);

    // Both, over a rect: the ground filled, the frame around it. `alpha`
    // is the draw context's, which every caller has and none should have
    // to remember to apply twice.
    void draw(const LLRect& rect, const LLColor4& paper, const LLColor4& ink, F32 alpha);
}
