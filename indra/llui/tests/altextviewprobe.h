/**
 * @file altextviewprobe.h
 * @brief What the text view's tests reach inside it for
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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

#ifndef AL_ALTEXTVIEWPROBE_H
#define AL_ALTEXTVIEWPROBE_H

#include "altextview.h"

#include "llrect.h"

#include <functional>
#include <vector>

namespace ll_test
{
    // ALTextView befriends this one name, so every test that reaches
    // inside the view shares this one definition: two of them, different,
    // in one binary would leave one test running the other's.
    struct TextViewProbe
    {
        // How long Next Misspelling may check lines for, and whether it is
        // still going.
        static void misspellingBudget(ALTextView& view, F32 seconds) { view.mMisspellingBudget = seconds; }
        static bool seeking(const ALTextView& view) { return view.mMisspellingSought.has_value(); }
        static void trimLayout(ALTextView& view) { view.trimLayout(); }
        // Every selection, the main one among them, in the order they begin.
        static std::vector<ALTextRange> selections(const ALTextView& view) { return view.selectionsInOrder(); }
        static S32  heldMost() { return ALTextView::LAYOUT_HELD_MOST; }
        // The rows and the gaps in sight, as the view walks them to draw.
        static void visibleRows(ALTextView& view, const LLRect& text, const std::function<void(S32, S32, S32)>& visit) { view.forEachVisibleRow(text, visit); }
        static void visibleGaps(ALTextView& view, const LLRect& text, const std::function<void(S32, S32, S32)>& visit) { view.forEachVisibleGap(text, visit); }
        // What a frame does before it draws, of what a test reaches:
        // Next Misspelling gone on with, and the primary selection offered.
        static void nextFrame(ALTextView& view)
        {
            if (view.mMisspellingSought)
            {
                view.seekMisspelling();
            }
            view.publishPrimary();
        }
        // The colours the view works out for a row's glyphs, as it draws them.
        static std::vector<LLColor4U> colours(ALTextView& view, S32 line, const ALTextLayout::Row& row)
        {
            const ALTextLayout::Line& laid = view.layout().line(line);
            view.colorRow(line, laid, row, 1.f);
            view.tintRow(line, laid, row, 1.f, view.mColorScratch);
            return view.mColorScratch;
        }
    };
}

#endif // AL_ALTEXTVIEWPROBE_H
