/**
 * @file llradiogroup.h
 * @brief LLRadioGroup base class
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_LLRADIOGROUP_H
#define LL_LLRADIOGROUP_H

#include "lluictrl.h"
#include "llcheckboxctrl.h"
#include "llctrlselectioninterface.h"

/*
 * An invisible view containing multiple mutually exclusive toggling
 * buttons (usually radio buttons).  Automatically handles the mutex
 * condition by highlighting only one button at a time.
 */
class LLRadioGroup final
:   public LLUICtrl, public LLCtrlSelectionInterface
{
public:
    AL_VIEW_TYPE(LLRadioGroup, LLUICtrl);

    struct ItemParams : public LLInitParam::Block<ItemParams, LLCheckBoxCtrl::Params>
    {
        Optional<LLSD>  value;
        ItemParams();
    };

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<bool>                      allow_deselect;
        Multiple<ItemParams, AtLeast<1> >   items;
        Params();
    };

protected:
    LLRadioGroup(const Params&);
    friend class LLUICtrlFactory;

public:

    /*virtual*/ void initFromParams(const Params&);

    virtual ~LLRadioGroup();

    virtual bool postBuild() override;

    virtual bool handleKeyHere(KEY key, MASK mask) override;

    void setIndexEnabled(S32 index, bool enabled);
    // return the index value of the selected item
    S32 getSelectedIndex() const { return mSelectedIndex; }
    // set the index value programatically
    bool setSelectedIndex(S32 index, bool from_event = false);
    // foxus child by index if it can get focus
    void focusSelectedRadioBtn();

    // Accept and retrieve strings of the radio group control names
    virtual void    setValue(const LLSD& value ) override;
    virtual LLSD    getValue() const override;

    // Update the control as needed.  Userdata must be a pointer to the button.
    void onClickButton(LLUICtrl* clicked_radio);

    //========================================================================
    LLCtrlSelectionInterface* getSelectionInterface() override   { return (LLCtrlSelectionInterface*)this; };

    // LLCtrlSelectionInterface functions
    /*virtual*/ S32     getItemCount() const override                { return static_cast<S32>(mRadioButtons.size()); }
    /*virtual*/ bool    getCanSelect() const override                { return true; }
    /*virtual*/ bool    selectFirstItem() override                   { return setSelectedIndex(0); }
    /*virtual*/ bool    selectNthItem( S32 index ) override          { return setSelectedIndex(index); }
    /*virtual*/ bool    selectItemRange( S32 first, S32 last ) override { return setSelectedIndex(first); }
    /*virtual*/ S32     getFirstSelectedIndex() const override       { return getSelectedIndex(); }
    /*virtual*/ bool    setCurrentByID( const LLUUID& id ) override;
    /*virtual*/ LLUUID  getCurrentID() const override;               // LLUUID::null if no items in menu
    /*virtual*/ bool    setSelectedByValue(const LLSD& value, bool selected) override;
    /*virtual*/ LLSD    getSelectedValue() override;
    /*virtual*/ bool    isSelected(const LLSD& value) const override;
    /*virtual*/ bool    operateOnSelection(EOperation op) override;
    /*virtual*/ bool    operateOnAll(EOperation op) override;

private:
    const LLFontGL*     mFont;
    S32                 mSelectedIndex;

    typedef std::vector<class LLRadioCtrl*> button_list_t;
    button_list_t       mRadioButtons;

    bool                mAllowDeselect; // user can click on an already selected option to deselect it
};

#endif
