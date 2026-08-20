/**
 * @file alcolorwheelctrl.h
 * @brief Colourist's wheel: a hue ring with a draggable puck, a master slider
 *        and editable per-channel fields
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

#ifndef AL_COLORWHEELCTRL_H
#define AL_COLORWHEELCTRL_H

#include "alcolorwheelmodel.h"
#include "lluictrl.h"
#include "lluicolor.h"

#include <array>
#include <string>
#include <vector>

class LLButton;
class LLLineEditor;
class LLSliderCtrl;
class LLTextBox;

/**
 * One wheel from a colourist's three-way, registered as @c color_wheel.
 *
 * Drag the puck to bias the colour: angle is hue, distance is how far. Drag
 * the slider under it to move all three channels together. Type into the three
 * fields for an exact value. All of it drives one RGB-triplet setting, and
 * every route round-trips through the same decomposition, so they can never
 * disagree -- see @ref ALColorWheelModel, which holds the maths and the tests.
 *
 * @par Binding
 * Bind it like any other control: @c control_name="RenderColorGradeLift". The
 * widget overrides @c setValue / @c getValue for a three-element LLSD array,
 * which is all @c LLUICtrl::setControlVariable needs to wire the control's
 * signal in both directions -- exactly how @c color_swatch already binds the
 * split-tone tints. No naming contract, no floater-side glue.
 *
 * @par Flavours
 * @c centre, @c min_value and @c max_value describe the setting behind it:
 * lift is 0 over [-0.5, 0.5], gamma and gain are 1 over [0.5, 1.5]. Setting
 * @c lock_master hides the slider, which is right for a split-tone tint
 * because the renderer divides those by @c dot(tint, LUMA) -- their magnitude
 * cancels, so a master there would visibly do nothing.
 *
 * @par Everything is a child
 * The slider, three fields, caption, label and reset glyph are child widgets
 * built here and laid out in @ref updateLayout, following LLColorSwatchCtrl.
 * One XUI tag therefore yields a complete, consistent wheel, and a bank of
 * them cannot drift apart the way six hand-placed widget groups would.
 */
class ALColorWheelCtrl final : public LLUICtrl
{
public:
    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<std::string> label;
        Optional<F32>  centre;
        Optional<F32>  min_value;
        Optional<F32>  max_value;
        Optional<bool> lock_master;
        Optional<S32>  ring_thickness;
        Optional<S32>  ring_steps;
        Optional<S32>  puck_radius;
        Optional<S32>  decimal_digits;
        Optional<LLUIColor> border_color;
        Optional<LLUIColor> face_color;
        Optional<LLUIColor> crosshair_color;

        Params();
    };

    ALColorWheelCtrl(const Params& p);
    ~ALColorWheelCtrl() override = default;

    void draw() override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    /// LLView::setEnabled only sets its own flag, and the slider and fields
    /// draw from their own, so a bare enabled_control on this widget would
    /// leave its children looking live. Propagate.
    void setEnabled(bool enabled) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleDoubleClick(S32 x, S32 y, MASK mask) override;

    /// Three reals, in channel order. Anything else is ignored, so a
    /// mis-typed control_name degrades to an inert widget rather than a crash.
    void setValue(const LLSD& value) override;
    LLSD getValue() const override;

    const ALColorWheelModel& getModel() const { return mModel; }

private:
    /// Position the children and work out where the ring goes. Called on
    /// construction and on every reshape.
    void updateLayout();

    /// Push the model's value out to the slider and the three fields, without
    /// letting their commit handlers fire back.
    void syncChildren();

    /// Write the model's value to the bound control and fire the commit.
    void publish();

    void onMasterCommit();
    void onChannelCommit(S32 index);
    void onReset();

    /// Centre and radius of the hue ring, in local pixels.
    void wheelGeometry(F32& cx, F32& cy, F32& radius) const;

    /// Pointer position to (hue, saturation), clamping to the rim rather than
    /// refusing to move -- LLVirtualTrackball's set-on-click path returns
    /// early outside its circle, which freezes the puck exactly when a
    /// colourist is pushing for maximum.
    bool pointToPolar(S32 x, S32 y, F32& hue_out, F32& sat_out) const;

    ALColorWheelModel mModel;

    LLTextBox*    mLabel = nullptr;
    LLButton*     mReset = nullptr;
    LLSliderCtrl* mMaster = nullptr;
    std::array<LLLineEditor*, 3> mFields{ nullptr, nullptr, nullptr };
    std::array<LLTextBox*, 3>    mCaptions{ nullptr, nullptr, nullptr };

    /// Ring colours, rebuilt only when the step count changes -- they depend
    /// on the hue angle alone, never on the value.
    std::vector<LLColor4> mRingColors;

    LLUIColor mBorderColor;
    LLUIColor mFaceColor;
    LLUIColor mCrosshairColor;

    LLRect mWheelRect;
    S32  mRingThickness;
    S32  mRingSteps;
    S32  mPuckRadius;
    S32  mDecimalDigits;
    bool mDragging = false;
    bool mUpdating = false;
    /// Held so a puck dragged exactly to the centre does not snap its hue to
    /// zero and jump on the way back out.
    F32  mLastHue = 0.f;
};

#endif // AL_COLORWHEELCTRL_H
