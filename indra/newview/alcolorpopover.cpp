/**
 * @file alcolorpopover.cpp
 * @brief The inline colour picker a colour swatch opens
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

#include "llviewerprecompiledheaders.h"
#include "alcolorpopover.h"

#include "alcolorpicker.h"
#include "llbutton.h"
#include "llcolorswatch.h"
#include "lluictrlfactory.h"

// static
LLFloater::Params ALColorPopover::params(const std::string& title)
{
    // Resizable, and opening at the size the last one was left at: the wheel
    // is what grows.
    const S32 header = LLFloater::getDefaultParams().header_height;
    LLFloater::Params p = ALPopover::paramsRemembered(SIZE_KIND,
                                                      widthFor(PICKER_WIDTH),
                                                      heightFor(header, PICKER_HEIGHT),
                                                      title);
    p.min_width = widthFor(MIN_PICKER_WIDTH);
    p.min_height = heightFor(header, MIN_PICKER_HEIGHT);
    return p;
}

ALColorPopover::ALColorPopover(const LLFloater::Params& p, const LLColor4& start, bool show_alpha,
                               std::function<void(const LLColor4&)> on_change,
                               LLColorSwatchCtrl* swatch,
                               const std::string& full_picker_label, const std::string& full_picker_tooltip)
:   ALPopover(p)
{
    const S32 width = getRect().getWidth();
    const S32 top = getRect().getHeight() - getHeaderHeight();

    ALColorPicker::Params pp(LLUICtrlFactory::getDefaultParams<ALColorPicker>());
    pp.name = "picker";
    // Everything between the title and the button row, so a popover
    // dragged bigger is a bigger wheel.
    pp.rect = LLRect(GAP, top - GAP, width - GAP, GAP + BUTTON_HEIGHT + GAP);
    pp.follows.flags = FOLLOWS_ALL;
    pp.show_alpha = show_alpha;
    ALColorPicker* picker = LLUICtrlFactory::create<ALColorPicker>(pp);
    picker->setColor(start);
    // Its value is text, "r, g, b, a"; the colour is what is wanted.
    picker->setCommitCallback([picker, on_change](LLUICtrl*, const LLSD&)
    {
        if (on_change)
        {
            on_change(picker->color());
        }
    });
    addChild(picker);

    LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
    bp.name = "full_picker";
    bp.label = full_picker_label;
    bp.tool_tip = full_picker_tooltip;
    bp.rect = LLRect(width - GAP - BUTTON_WIDTH, GAP + BUTTON_HEIGHT, width - GAP, GAP);
    bp.follows.flags = FOLLOWS_RIGHT | FOLLOWS_BOTTOM;
    LLButton* full = LLUICtrlFactory::create<LLButton>(bp);
    LLHandle<LLView> swatch_handle = swatch ? swatch->getHandle() : LLHandle<LLView>();
    full->setCommitCallback([this, swatch_handle](LLUICtrl*, const LLSD&)
    {
        settle();
        if (LLColorSwatchCtrl* swatch = ALViewType::as<LLColorSwatchCtrl>(swatch_handle.get()))
        {
            swatch->showClassicPicker(true);
        }
    });
    addChild(full);
}
