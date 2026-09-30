/**
 * @file alcolorpopover.h
 * @brief The inline colour picker a colour swatch opens: a ring, a shade
 *        square and channel tracks in a popover
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#ifndef AL_COLORPOPOVER_H
#define AL_COLORPOPOVER_H

#include "alpopover.h"
#include "v4color.h"

#include <functional>
#include <string>

class LLColorSwatchCtrl;

/// The inline picker for a colour: XUI Studio's hue ring, shade square and
/// channel tracks, under a title, with a way on to the viewer's full picker
/// for typed numbers and the eyedropper.
///
/// The picker is a child of the popover itself and not of a panel inside it,
/// and that is load-bearing: a panel takes Escape for itself and drops the
/// keyboard, which closes the popover as *settled*, and Escape here has to
/// mean "put the colour back".
///
/// What a pick means is the owner's: it is told each colour as it is picked
/// and whether the popover was escaped as it closes (ALPopover::onClosed).
class ALColorPopover final : public ALPopover
{
public:
    // The picker gives its channel sliders 218px and the ring the rest of the
    // width, up to its height. So its width is the ring plus 218: 440 by 220
    // is a ring as tall as the picker. Narrower, and the ring is what shrinks
    // -- at 300 it is 74px across.
    static constexpr S32 PICKER_WIDTH = 440;
    static constexpr S32 PICKER_HEIGHT = 220;
    // As small as it may be dragged: the sliders and a ring still worth
    // aiming at.
    static constexpr S32 MIN_PICKER_WIDTH = 330;
    static constexpr S32 MIN_PICKER_HEIGHT = 150;
    static constexpr S32 BUTTON_HEIGHT = 20;
    static constexpr S32 BUTTON_WIDTH = 110;
    static constexpr S32 GAP = 4;

    /// What every colour popover remembers its size as, so a wheel dragged
    /// bigger in one window opens that big in the next.
    static constexpr const char* SIZE_KIND = "color";

    /// Resizable, titled @a title, and at the size the last colour popover
    /// was left at.
    static LLFloater::Params params(const std::string& title);

    /// @a show_alpha adds the alpha track; leave it off for a Color3, where it
    /// would be a control that does nothing. The way on to the full picker
    /// settles this popover -- the full picker starts from what was picked
    /// here, and keeps it if it is closed -- and opens @a swatch's own.
    ALColorPopover(const LLFloater::Params& p, const LLColor4& start, bool show_alpha,
                   std::function<void(const LLColor4&)> on_change,
                   LLColorSwatchCtrl* swatch,
                   const std::string& full_picker_label, const std::string& full_picker_tooltip);

private:
    /// The popover around a picker of @a picker_width by @a picker_height,
    /// laid out under the title, which is drawn across the top of the rect.
    static S32 widthFor(S32 picker_width) { return GAP + picker_width + GAP; }
    static S32 heightFor(S32 header, S32 picker_height)
    {
        return header + GAP + picker_height + GAP + BUTTON_HEIGHT + GAP;
    }
};

#endif // AL_COLORPOPOVER_H
