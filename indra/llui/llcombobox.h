/**
 * @file llcombobox.h
 * @brief LLComboBox base class
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

// A control that displays the name of the chosen item, which when clicked
// shows a scrolling box of choices.

#ifndef LL_LLCOMBOBOX_H
#define LL_LLCOMBOBOX_H

#include "llbutton.h"
#include "lluictrl.h"
#include "llctrlselectioninterface.h"
#include "llrect.h"
#include "llscrolllistctrl.h"
#include "lllineeditor.h"

// Classes

class LLFontGL;
class LLViewBorder;

class LLComboBox
: public LLUICtrl
, public LLCtrlListInterface
, public ll::ui::SearchableControl
{
public:
    AL_VIEW_TYPE(LLComboBox, LLUICtrl);

    typedef enum e_preferred_position
    {
        ABOVE,
        BELOW
    } EPreferredPosition;

    struct PreferredPositionValues : public LLInitParam::TypeValuesHelper<EPreferredPosition, PreferredPositionValues>
    {
        static void declareValues();
    };


    struct ItemParams : public LLInitParam::Block<ItemParams, LLScrollListItem::Params>
    {
        Optional<std::string>   label;
        ItemParams();
    };

    struct Params
    :   public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<bool>                      allow_text_entry,
                                            show_text_as_tentative,
                                            allow_new_values;
        Optional<S32>                       max_chars;
        Optional<commit_callback_t>         prearrange_callback,
                                            text_entry_callback,
                                            text_changed_callback;

        Optional<EPreferredPosition, PreferredPositionValues>   list_position;

        // components
        Optional<LLButton::Params>          combo_button;
        Optional<LLScrollListCtrl::Params>  combo_list;
        Optional<LLLineEditor::Params>      combo_editor;

        Optional<LLButton::Params>          drop_down_button;

        Multiple<ItemParams>                items;

        Params();
    };


    virtual ~LLComboBox();

    /*virtual*/ bool postBuild() override;

protected:
    friend class LLUICtrlFactory;
    LLComboBox(const Params&);
    void    initFromParams(const Params&);
    void    prearrangeList(std::string filter = "");

    virtual std::string _getSearchText() const override;
    virtual void onSetHighlight() const override;

    void imageLoaded();

public:
    // LLView interface
    virtual void    onFocusLost() override;

    virtual bool    handleToolTip(S32 x, S32 y, MASK mask) override;
    virtual bool    handleKeyHere(KEY key, MASK mask) override;
    virtual bool    handleUnicodeCharHere(llwchar uni_char) override;
    virtual bool    handleScrollWheel(S32 x, S32 y, LLScrollDelta delta) override;

    // LLUICtrl interface
    virtual void    clear() override;                    // select nothing
    virtual void    onCommit() override;
    virtual bool    acceptsTextInput() const override        { return mAllowTextEntry; }
    virtual bool    isDirty() const override;            // Returns true if the user has modified this control.
    virtual void    resetDirty() override;               // Clear dirty state

    virtual void    setFocus(bool b) override;

    // Selects item by underlying LLSD value, using LLSD::asString() matching.
    // For simple items, this is just the name of the label.
    virtual void    setValue(const LLSD& value ) override;

    // Gets underlying LLSD value for currently selected items.  For simple
    // items, this is just the label.
    virtual LLSD    getValue() const override;

    void            setTextEntry(const LLStringExplicit& text);
    void            setKeystrokeOnEsc(bool enable);

    LLScrollListItem*   add(const std::string& name, EAddPosition pos = ADD_BOTTOM, bool enabled = true);   // add item "name" to menu
    LLScrollListItem*   add(const std::string& name, const LLUUID& id, EAddPosition pos = ADD_BOTTOM, bool enabled = true);
    LLScrollListItem*   add(const std::string& name, void* userdata, EAddPosition pos = ADD_BOTTOM, bool enabled = true);
    LLScrollListItem*   add(const std::string& name, LLSD value, EAddPosition pos = ADD_BOTTOM, bool enabled = true);
    LLScrollListItem*   addSeparator(EAddPosition pos = ADD_BOTTOM);
    bool            remove( S32 index );    // remove item by index, return true if found and removed
    void            removeall() { clearRows(); }
    bool            itemExists(const std::string& name) const;
    bool            valueExists(const std::string& value) const;
    LLScrollListItem* findItemByValue(const std::string& value) const;
    std::vector<LLScrollListItem*> getAllData() const;

    void            sortByName(bool ascending = true); // Sort the entries in the combobox by name

    // Select current item by name using selectItemByLabel.  Returns false if not found.
    bool            setSimple(const LLStringExplicit& name);
    // Get name of current item. Returns an empty string if not found.
    const std::string   getSimple() const;
    // Get contents of column x of selected row
    virtual const std::string getSelectedItemLabel(S32 column = 0) const;

    // Sets the label, which doesn't have to exist in the label.
    // This is probably a UI abuse.
// [SL:KB] - Patch: Control-ComboBox | Checked: Catznip-6.4
    virtual void    setLabel(const LLStringExplicit& name);

    // A value nobody chose here, shown as the value in force: the same
    // distinction a spinner draws for an unset number, in the ink this
    // control already keeps for a value it is not sure of. Set after the
    // value, since setting a value the list has is a choice.
    void            setUnset(bool unset);
    bool            isUnset() const { return mUnset; }
// [/SL:KB]
//  void            setLabel(const LLStringExplicit& name);

    // Updates the combobox label to match the selected list item.
// [SL:KB] - Patch: Control-ComboBox | Checked: Catznip-6.4
    virtual void    updateLabel();
// [/SL:KB]
//  void            updateLabel();

    bool            remove(const std::string& name);    // remove item "name", return true if found and removed

    bool            setCurrentByIndex(S32 index);
    S32             getCurrentIndex() const;

    bool            selectNextItem();
    bool            selectPrevItem();

    void            setEnabledByValue(const LLSD& value, bool enabled);

    void            createLineEditor(const Params&);

    //========================================================================
    LLCtrlSelectionInterface* getSelectionInterface() override   { return (LLCtrlSelectionInterface*)this; };
    LLCtrlListInterface* getListInterface() override             { return (LLCtrlListInterface*)this; };

    // LLCtrlListInterface functions
    // See llscrolllistctrl.h
    virtual S32     getItemCount() const override;
    // Overwrites the default column (See LLScrollListCtrl for format)
    virtual void    addColumn(const LLSD& column, EAddPosition pos = ADD_BOTTOM) override;
    virtual void    clearColumns() override;
    virtual void    setColumnLabel(const std::string& column, const std::string& label) override;
    virtual LLScrollListItem* addElement(const LLSD& value, EAddPosition pos = ADD_BOTTOM, void* userdata = NULL) override;
    virtual LLScrollListItem* addSimpleElement(const std::string& value, EAddPosition pos = ADD_BOTTOM, const LLSD& id = LLSD()) override;
    virtual void    clearRows() override;
    virtual void    sortByColumn(const std::string& name, bool ascending) override;

    // LLCtrlSelectionInterface functions
    virtual bool    getCanSelect() const override                { return true; }
    virtual bool    selectFirstItem() override                   { return setCurrentByIndex(0); }
    virtual bool    selectNthItem( S32 index ) override          { return setCurrentByIndex(index); }
    virtual bool    selectItemRange( S32 first, S32 last ) override;
    virtual S32     getFirstSelectedIndex() const override       { return getCurrentIndex(); }
    virtual bool    setCurrentByID( const LLUUID& id ) override;
    virtual LLUUID  getCurrentID() const override;               // LLUUID::null if no items in menu
    virtual bool    setSelectedByValue(const LLSD& value, bool selected) override;
    virtual LLSD    getSelectedValue() override;
    virtual bool    isSelected(const LLSD& value) const override;
    virtual bool    operateOnSelection(EOperation op) override;
    virtual bool    operateOnAll(EOperation op) override;

    //========================================================================

    void            setLeftTextPadding(S32 pad);

    void*           getCurrentUserdata();

    void            setPrearrangeCallback( commit_callback_t cb ) { mPrearrangeCallback = cb; }
    void            setTextEntryCallback( commit_callback_t cb ) { mTextEntryCallback = cb; }
    void            setTextChangedCallback( commit_callback_t cb ) { mTextChangedCallback = cb; }

    /**
    * Connects callback to signal called when Return key is pressed.
    */
    boost::signals2::connection setReturnCallback( const commit_signal_t::slot_type& cb ) { return mOnReturnSignal.connect(cb); }

    void            setButtonVisible(bool visible);

    // Populates the provided LLSD with combo box-specific information(list of items, item count, current selection label)
    // also includes base LLUICtrl information via parent class
    void addInfo(LLSD & info) override;

    void            onButtonMouseDown();
    void            onListMouseUp();
    void            onItemSelected(const LLSD& data);
    void            onTextCommit(const LLSD& data);

    void            updateSelection();
    virtual void    showList();
    virtual void    hideList();

    virtual void    onTextEntry(LLLineEditor* line_editor);

protected:
    LLButton*           mButton;
    LLLineEditor*       mTextEntry;
    LLScrollListCtrl*   mList;
    EPreferredPosition  mListPosition;
    LLPointer<LLUIImage>    mArrowImage;
    LLUIString          mLabel;
    bool                mHasAutocompletedText;

private:
    bool                mAllowTextEntry;
    bool                mAllowNewValues;
    S32                 mMaxChars;
    bool                mTextEntryTentative;
    bool                mUnset { false };
    commit_callback_t   mPrearrangeCallback;
    commit_callback_t   mTextEntryCallback;
    commit_callback_t   mTextChangedCallback;
    commit_callback_t   mSelectionCallback;
    boost::signals2::connection mTopLostSignalConnection;
    boost::signals2::connection mImageLoadedConnection;
    commit_signal_t     mOnReturnSignal;
    S32                 mLastSelectedIndex;
};

// A combo box with icons for the list of items.
class LLIconsComboBox final
:   public LLComboBox
{
public:
    AL_VIEW_TYPE(LLIconsComboBox, LLComboBox);

    struct Params
    :   public LLInitParam::Block<Params, LLComboBox::Params>
    {
        Optional<S32>       icon_column,
                            label_column;
        Params();
    };

    /*virtual*/ const std::string getSelectedItemLabel(S32 column = 0) const override;

private:
    enum EColumnIndex
    {
        ICON_COLUMN = 0,
        LABEL_COLUMN
    };

    friend class LLUICtrlFactory;
    LLIconsComboBox(const Params&);
    virtual ~LLIconsComboBox() {};

    S32         mIconColumnIndex;
    S32         mLabelColumnIndex;
};

#endif
