/**
 * @file altoolscenepicker.h
 * @brief Transient tool that reports the linear scene colour under a click
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

#ifndef AL_TOOLSCENEPICKER_H
#define AL_TOOLSCENEPICKER_H

#include "lltool.h"
#include "llsingleton.h"
#include "v3color.h"

#include <functional>

/**
 * Click anywhere in the world; get back the linear colour that was rendered
 * there.
 *
 * Deliberately not @c LLToolPipette, which answers a different question:
 * pipette reports the texture entry of the face you hit, so a white wall under
 * a sunset reads as white. A colourist is asking about the light, so this
 * reads the rendered pixel out of the scene buffer instead -- pre-grade and
 * pre-tonemap, via @c LLPipeline::requestScenePixel.
 *
 * Transient, in the pipette's sense: install it with
 * @c LLToolMgr::setTransientTool and it pops itself on mouse-up, so the tool
 * that was in use comes back. It samples on mouse-UP rather than down, so a
 * click can be aimed and then abandoned by dragging off first, and so the
 * sample is never taken from a frame the press itself changed.
 */
class ALToolScenePicker
:   public LLTool, public LLSingleton<ALToolScenePicker>
{
    LLSINGLETON(ALToolScenePicker);
    ~ALToolScenePicker() override;

public:
    typedef std::function<void(const LLColor3&)> pick_cb_t;

    /// Arm the tool. @a callback fires once, on the next frame after the
    /// click, and is dropped if the pick is cancelled.
    void setPickCallback(pick_cb_t callback) { mCallback = std::move(callback); }

    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;

    void handleSelect() override;
    void handleDeselect() override;

private:
    pick_cb_t mCallback;
};

#endif // AL_TOOLSCENEPICKER_H
