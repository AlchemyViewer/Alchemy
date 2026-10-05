/**
 * @file alsettingrow.h
 * @brief One setting on one line: its name, a slider, its value, and the way back to its default.
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

#pragma once

#include "lluictrl.h"
#include "llsearchablecontrol.h"

#include <functional>
#include <string>
#include <vector>

class LLButton;
class LLSliderCtrl;
class LLSpinCtrl;
class LLTextBox;

// One setting on one line: a slider with its label and value, and a reset
// button that brings the setting back to its default. It is what a settings
// floater writes as a slider and a separate reset button that repeats the
// slider's setting name -- a pair that has to be laid out against each other
// by hand, kept in step by hand, and greyed together by hand.
//
// The row is the only thing bound: control_name binds the row, and the slider
// inside it is not bound to anything. So a walk looking for what a panel's
// controls are bound to finds one setting per row, the setting is written
// once per commit, and a change to the setting from anywhere reaches the
// slider through the row.
//
// The reset button shows only while the setting differs from its default as
// far as the row can show -- rounded to the decimals the value is shown to, so
// a drag that comes back to 0.7 and lands on 0.69999999 is at the default --
// and its place is kept while it is hidden, so nothing moves when it appears.
// It resets the setting directly and does not fire the row's commit: what
// cares about the setting changing listens to the setting.
//
// A setting that is several numbers -- a vector -- is a row too: given the
// letters its parts go by, the row is its caption and a box per part, each
// named by its letter (ALFieldEditors makes them), and a box typed in writes
// its part with the rest of the setting as it stands. Or one part of one
// is a row of its own: given the part, the row is a slider over it, and
// what it writes, compares and resets is that part alone.
class ALSettingRow final : public LLUICtrl, public ll::ui::SearchableControl
{
public:
    AL_VIEW_TYPE(ALSettingRow, LLUICtrl);

    // The slider's own parameters, by the names a slider writes them, so a
    // slider row becomes a setting row by changing its tag.
    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<S32>   label_width;
        Optional<F32>   min_value;
        Optional<F32>   max_value;
        Optional<F32>   increment;
        Optional<S32>   decimal_digits;
        // Left out, the value box is as wide as the slider would make it --
        // except where the slider's own sizing would cut the number short,
        // where the row works out a width that fits.
        Optional<S32>   text_width;
        Optional<bool>  show_text;
        Optional<bool>  can_edit_text;
        Optional<bool>  show_reset;
        // A vector: the letters its parts go by, with spaces between -- "R
        // G B", "X Y". Or one part of one, by index.
        Optional<std::string> components;
        Optional<S32>         part;
        // How wide each of a vector's letters is: "P1" takes more than "R".
        Optional<S32>         letter_width;
        // The slider's press and release, by the names a slider writes them. A
        // setting that is dear to apply can be told a drag has begun and ended
        // and leave its work for the release; the commits in between still
        // write the setting as any row's do.
        Optional<CommitCallbackParam> mouse_down_callback,
                                      mouse_up_callback;
        Params();
    };

    void setValue(const LLSD& value) override;
    LLSD getValue() const override;
    void setEnabled(bool enabled) override;
    // The keyboard goes to the slider: the row itself has nothing to type
    // into and no key to answer.
    void setFocus(bool focus) override;

    // What the row is called, and the text box that shows it: for a caller
    // that names a setting after its row, or lights the name up.
    const std::string& getLabel() const { return mLabel; }
    LLTextBox* getLabelBox() const;

    // Whether the setting differs from its default as far as the row shows
    // it. False for a row bound to nothing.
    bool isModified();
    // Back to the default, as far as the row shows the setting: the whole
    // of it, or the part it is. What the reset button does, where no
    // handler takes it; a handler that names the step calls it in turn.
    void resetToDefault();

    // Hand the reset to the row's owner: when set, the reset button calls
    // this instead of resetting the setting itself. For an owner that has
    // more to do around a reset than the reset -- naming it as an undo step,
    // say. Unset, the row resets the setting on its own.
    typedef std::function<void(ALSettingRow*)> reset_handler_t;
    void setResetHandler(reset_handler_t handler) { mResetHandler = std::move(handler); }

    // The slider, or null for a vector's row.
    LLSliderCtrl* getSlider() const { return mSlider; }
    LLButton* getResetButton() const { return mReset; }

    // The widths the row lays its parts out by: the reset button's square,
    // and the gap between it and the slider's value box.
    static constexpr S32 RESET_SIZE = 18;
    static constexpr S32 RESET_GAP = 6;

protected:
    friend class LLUICtrlFactory;
    ALSettingRow(const Params& p);

    std::string _getSearchText() const override;
    void onSetHighlight() const override;

private:
    void onSliderCommit();
    void onPartsCommit(const std::string& text);
    void onResetClicked();
    void refreshReset();
    // The setting as it stands with the parts from `first` on replaced.
    LLSD withParts(size_t first, const std::vector<F64>& numbers);
    // What a number of the setting's is shown as, to the row's decimals.
    std::string shown(const LLSD& number) const;

    LLSliderCtrl*            mSlider = nullptr;
    LLButton*                mReset = nullptr;
    // A vector's caption and its boxes, one per letter.
    LLTextBox*               mCaption = nullptr;
    std::vector<LLSpinCtrl*> mBoxes;
    std::vector<std::string> mLetters;
    reset_handler_t          mResetHandler;
    std::string              mLabel;
    S32                      mDecimalDigits = 3;
    // The part of a vector this row is, or none.
    S32                      mPart = -1;
    bool                     mShowReset = true;
    bool                     mResetShown = false;
};
