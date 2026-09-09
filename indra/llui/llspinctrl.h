/**
 * @file llspinctrl.h
 * @brief Typical spinner with "up" and "down" arrow buttons.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLSPINCTRL_H
#define LL_LLSPINCTRL_H


#include "stdtypes.h"
#include "llbutton.h"
#include "llf32uictrl.h"
#include "v4color.h"
#include "llrect.h"


class LLSpinCtrl final
: public LLF32UICtrl
{
public:
    AL_VIEW_TYPE(LLSpinCtrl, LLF32UICtrl);

    struct Params : public LLInitParam::Block<Params, LLF32UICtrl::Params>
    {
        Optional<S32> label_width;
        Optional<U32> decimal_digits;
        Optional<bool> allow_text_entry;
        Optional<bool> allow_digits_only;
        Optional<bool> label_wrap;

        Optional<bool> scrub;

        Optional<LLUIColor> text_enabled_color;
        Optional<LLUIColor> text_disabled_color;

        Optional<LLButton::Params> up_button;
        Optional<LLButton::Params> down_button;

        Optional<CommitCallbackParam> mouse_down_callback,
                                      mouse_up_callback;

        Params();
    };
protected:
    LLSpinCtrl(const Params&);
    friend class LLUICtrlFactory;
public:
    virtual ~LLSpinCtrl(); // Children all cleaned up by default view destructor.

    // Pixels of travel a scrub spends on one increment.
    static constexpr F32 SCRUB_PIXELS_PER_STEP = 4.f;

    // Where a scrub of this many pixels arrives, so what the gesture does can
    // be asked without a mouse. Travel is rightwards on a labelled spinner and
    // upwards on one whose buttons carry the drag.
    static F32      scrubbedValue(F32 start, S32 travel, F32 increment, MASK mask,
                                  S32 precision, F32 min_value, F32 max_value);

    virtual void    forceSetValue(const LLSD& value ) ;
    virtual void    setValue(const LLSD& value );
            F32     get() const { return getValueF32(); }
            void    set(F32 value) { setValue(value); mInitialValue = value; }

    bool            isMouseHeldDown() const;

    virtual void    setEnabled( bool b );
    virtual void    setFocus( bool b );
    virtual void    clear();
    virtual bool    isDirty() const { return( getValueF32() != mInitialValue ); }
    virtual void    resetDirty() { mInitialValue = getValueF32(); }

    virtual void    setPrecision(S32 precision);

    // A number nobody wrote, shown as the number in force rather than as one
    // this control holds: the box reads empty and the value sits behind it in
    // the ink of a placeholder. Without this an unset number reads exactly
    // like a zero somebody chose. Typing or stepping makes it a value again,
    // which is what choosing it means.
    void            setUnset(bool unset);
    bool            isUnset() const { return mUnset; }

    void            setLabel(const LLStringExplicit& label);
    void            setLabelColor(const LLUIColor& c)            { mTextEnabledColor = c; updateLabelColor(); }
    void            setDisabledLabelColor(const LLUIColor& c)    { mTextDisabledColor = c; updateLabelColor();}
    void            setAllowEdit(bool allow_edit);

    virtual void    onTabInto();

    virtual void    setTentative(bool b);           // marks value as tentative
    virtual void    onCommit();                     // mark not tentative, then commit

    void            forceEditorCommit();            // for commit on external button

    virtual bool    handleScrollWheel(S32 x,S32 y,LLScrollDelta delta);
    virtual bool    handleKeyHere(KEY key, MASK mask);

    // Dragging the label changes the value, and a spinner with no label puts
    // the same gesture on its buttons, vertically. Shift is fine, control is
    // finer still, alt is coarse -- the increments the buttons already use.
    // The value tracks the drag; the ends of it are their own callbacks, so a
    // listener that wants one edit per gesture has somewhere to put it.
    void            setScrub(bool scrub)                        { mScrub = scrub; }
    bool            getScrub() const                            { return mScrub; }
    bool            isScrubbing() const                         { return mScrubbing; }

    boost::signals2::connection setMouseDownCallback(const commit_signal_t::slot_type& cb);
    boost::signals2::connection setMouseUpCallback(const commit_signal_t::slot_type& cb);

    virtual bool    handleMouseDown(S32 x, S32 y, MASK mask);
    virtual bool    handleHover(S32 x, S32 y, MASK mask);
    virtual bool    handleMouseUp(S32 x, S32 y, MASK mask);
    virtual void    onMouseCaptureLost();

    void            onEditorCommit(const LLSD& data);
    static void     onEditorGainFocus(LLFocusableElement* caller, void *userdata);
    static void     onEditorLostFocus(LLFocusableElement* caller, void *userdata);

    void            onUpBtn(const LLSD& data);
    void            onDownBtn(const LLSD& data);

    const LLColor4& getEnabledTextColor() const { return mTextEnabledColor.get(); }
    const LLColor4& getDisabledTextColor() const { return mTextDisabledColor.get(); }

private:
    void            updateLabelColor();
    void            updateEditor();
    bool            mUnset { false };
    void            reportInvalidData();

    // The label carries the drag where there is one; a spinner that draws its
    // own label elsewhere has only its buttons, and they drag vertically.
    bool            scrubVertical() const                       { return mLabelBox == nullptr; }
    bool            inScrubZone(S32 x, S32 y) const;
    void            scrubTo(S32 x, S32 y, MASK mask);
    void            endScrub();

    S32             mPrecision;
    class LLTextBox*    mLabelBox;

    class LLLineEditor* mEditor;
    LLUIColor   mTextEnabledColor;
    LLUIColor   mTextDisabledColor;

    class LLButton*     mUpBtn;
    class LLButton*     mDownBtn;

    bool            mbHasBeenSet;
    bool            mAllowEdit;

    bool            mScrub;
    bool            mScrubbing;
    bool            mScrubMoved;
    S32             mScrubStartX;
    S32             mScrubStartY;
    F32             mScrubStartValue;

    commit_signal_t*    mMouseDownSignal;
    commit_signal_t*    mMouseUpSignal;
};

#endif  // LL_LLSPINCTRL_H
