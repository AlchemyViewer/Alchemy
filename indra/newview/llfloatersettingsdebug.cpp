/**
 * @file llfloatersettingsdebug.cpp
 * @brief floater for debugging internal viewer settings
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"
#include "llfloatersettingsdebug.h"

#include "alpropertygrid.h"
#include "llfloater.h"
#include "llfiltereditor.h"
#include "lluictrlfactory.h"
#include "llcombobox.h"
// [RLVa:KB] - Patch: RLVa-2.1.0
#include "llsdserialize.h"
// [/RLVa:KB]
#include "llspinctrl.h"
#include "llcolorswatch.h"
#include "llviewercontrol.h"
#include "lltexteditor.h"


LLFloaterSettingsDebug::LLFloaterSettingsDebug(const LLSD& key)
:   LLFloater(key),
    mSettingList(NULL)
{
}

LLFloaterSettingsDebug::~LLFloaterSettingsDebug()
{}

bool LLFloaterSettingsDebug::postBuild()
{
    enableResizeCtrls(true, false, true);

    mEditor = getChild<ALPropertyGrid>("setting_editor");
    mEditor->setGroups({ getString("SettingGroup") });
    mEditor->onFieldCommit(boost::bind(&LLFloaterSettingsDebug::onEditorCommit, this, _1, _2));
    mEditor->onFieldRemove(boost::bind(&LLFloaterSettingsDebug::onEditorRemove, this, _1));

    ALPropertyGrid::Tips tips;
    tips.field = "[NAME]";
    tips.remove = getString("SettingRemoveTip");
    mEditor->setTips(tips);

    mSettingNameText = getChild<LLTextBox>("setting_name_txt");

    mComment = getChild<LLTextEditor>("comment_text");

    getChild<LLFilterEditor>("filter_input")->setCommitCallback(boost::bind(&LLFloaterSettingsDebug::setSearchFilter, this, _2));

    mSettingList = getChild<LLScrollListCtrl>("setting_list");
    mSettingList->setCommitOnSelectionChange(true);
    mSettingList->setCommitCallback(boost::bind(&LLFloaterSettingsDebug::onSettingSelect, this));

    updateList();

    gSavedSettings.getControl("DebugSettingsHideDefault")->getCommitSignal()->connect(boost::bind(&LLFloaterSettingsDebug::updateList, this, false));

    return true;
}

void LLFloaterSettingsDebug::draw()
{
    LLScrollListItem* first_selected = mSettingList->getFirstSelected();
    if (first_selected)
    {
        LLControlVariable* controlp = (LLControlVariable*)first_selected->getUserdata();
        updateControl(controlp);
    }

    LLFloater::draw();
}
namespace
{
    // A setting's value in the one form the grid reads: text, with the parts
    // of a value that is several numbers separated by spaces, which is how a
    // file writes one.
    std::string settingText(LLControlVariable* control)
    {
        const LLSD value = control->get();
        switch (control->type())
        {
        case TYPE_BOOLEAN:
            return value.asBoolean() ? "true" : "false";

        case TYPE_VEC3:
        case TYPE_VEC3D:
        case TYPE_QUAT:
        case TYPE_RECT:
        case TYPE_COL4:
        case TYPE_COL3:
        {
            std::string joined;
            for (LLSD::array_const_iterator it = value.beginArray(); it != value.endArray(); ++it)
            {
                joined += joined.empty() ? "" : " ";
                joined += it->asString();
            }
            return joined;
        }

        default:
            return value.asString();
        }
    }

    // What the parts of this kind of value are called. Empty for the kinds
    // that are one number or one word, which is most of them.
    std::vector<std::string> settingParts(eControlType type)
    {
        switch (type)
        {
        case TYPE_VEC3:
        case TYPE_VEC3D:    return { "X", "Y", "Z" };
        case TYPE_QUAT:     return { "X", "Y", "Z", "W" };
        case TYPE_COL3:     return { "R", "G", "B" };
        case TYPE_COL4:     return { "R", "G", "B", "A" };
        // In the order a rect is written down, which is the order it is
        // read back in: left, top, right, bottom.
        case TYPE_RECT:     return { "L", "T", "R", "B" };
        default:            break;
        }
        return {};
    }

    // Which editor the grid should reach for. A colour is the one type with a
    // vocabulary worth showing rather than typing, and the grid asks the type
    // name for that rather than the kind.
    ALParamType::EValue settingKind(eControlType type)
    {
        switch (type)
        {
        case TYPE_BOOLEAN:  return ALParamType::BOOLEAN;
        case TYPE_U32:      return ALParamType::UNSIGNED;
        case TYPE_S32:      return ALParamType::INTEGER;
        case TYPE_F32:      return ALParamType::REAL;
        case TYPE_VEC3:
        case TYPE_VEC3D:
        case TYPE_QUAT:
        case TYPE_COL3:
        case TYPE_COL4:     return ALParamType::REAL;
        case TYPE_RECT:     return ALParamType::INTEGER;
        default:            break;
        }
        return ALParamType::STRING;
    }

    const char* settingTypeName(eControlType type)
    {
        switch (type)
        {
        case TYPE_U32:      return "U32";
        case TYPE_S32:      return "S32";
        case TYPE_F32:      return "F32";
        case TYPE_BOOLEAN:  return "Boolean";
        case TYPE_STRING:   return "String";
        case TYPE_VEC3:     return "Vector3";
        case TYPE_VEC3D:    return "Vector3D";
        case TYPE_QUAT:     return "Quaternion";
        case TYPE_RECT:     return "Rect";
        case TYPE_COL4:     return "LLColor4";
        case TYPE_COL3:     return "LLColor3";
        case TYPE_LLSD:     return "LLSD";
        default:            break;
        }
        return "";
    }

    // The parts of a value, read back off the one line the grid commits.
    LLSD partsOf(const std::string& text, S32 count)
    {
        LLSD parts = LLSD::emptyArray();
        std::istringstream reading(text);
        for (S32 i = 0; i < count; ++i)
        {
            F64 number = 0.0;
            reading >> number;
            parts.append(number);
        }
        return parts;
    }
}

// The selected setting, as the one row a grid holds.
//
// This pane used to build the editor for a setting by hand: a boolean combo, a
// text box, four spinners and a colour swatch, all of them made once and shown
// or hidden per type, with three hundred lines deciding which. A property grid
// is what that was, so it is one now -- and a value that is several numbers is
// several boxes rather than four spinners nobody labelled.
//
// Called every frame, because a setting changed anywhere else has to show
// here. So it does nothing unless something it is showing has changed: the
// grid builds its row's editors when it is given fields, and a row rebuilt
// sixty times a second is a row nobody can type into.
void LLFloaterSettingsDebug::updateControl(LLControlVariable* controlp)
{
    if (!controlp || isSettingHidden(controlp))
    {
        if (!mShownName.empty())
        {
            mShownName.clear();
            mShownValue.clear();
            mEditor->setFields({});
            mSettingNameText->setVisible(false);
            mComment->setVisible(false);
        }
        return;
    }

    const std::string name = controlp->getName();
    const std::string value = settingText(controlp);
    // Hidden from the editor can be toggled while this is open, so it is
    // asked every time rather than when the selection changed.
    const bool editable = !controlp->isHiddenFromSettingsEditor();
    const bool written = !controlp->isDefault();
    if (name == mShownName && value == mShownValue
        && written == mShownWritten && editable == mShownEditable)
    {
        return;
    }
    mShownName = name;
    mShownValue = value;
    mShownWritten = written;
    mShownEditable = editable;

    mSettingNameText->setVisible(true);
    mSettingNameText->setText(name);
    mSettingNameText->setToolTip(name);
    mComment->setVisible(true);
    if (const std::string comment = controlp->getComment(); mOldText != comment)
    {
        mComment->setText(comment);
        mOldText = comment;
    }

    ALPropertyGrid::Field field;
    field.name = name;
    field.value = value;
    field.kind = settingKind(controlp->type());
    field.type = settingTypeName(controlp->type());
    field.components = settingParts(controlp->type());
    // What the grid calls authored is what this pane calls changed: a value
    // somebody set, as against the one the file shipped. So a setting at its
    // default is drawn in the quiet ink and has no way back offered, which is
    // what the "changed" column in the list says with an asterisk.
    field.authored = written;
    field.source = written ? getString("SettingChanged") : getString("SettingDefault");
    mEditor->setEnabled(editable);
    mEditor->setFields({ field });
}

// A row committed: the text the grid gives back, read as the type the setting
// is. The grid knows how many numbers it showed and says them in the order it
// showed them, which is the order the type keeps them in.
void LLFloaterSettingsDebug::onEditorCommit(const std::string& name, const std::string& text)
{
    LLControlVariable* controlp = gSavedSettings.getControl(name);
    if (!controlp)
    {
        controlp = gSavedPerAccountSettings.getControl(name);
    }
    if (!controlp || controlp->isHiddenFromSettingsEditor())
    {
        return;
    }

    switch (controlp->type())
    {
    case TYPE_U32:      controlp->set((U32)atoi(text.c_str()));             break;
    case TYPE_S32:      controlp->set((S32)atoi(text.c_str()));            break;
    case TYPE_F32:      controlp->set((F32)atof(text.c_str()));            break;
    case TYPE_BOOLEAN:  controlp->set(text == "true" || text == "1");      break;
    case TYPE_VEC3:
    case TYPE_VEC3D:
    case TYPE_COL3:     controlp->set(partsOf(text, 3));                   break;
    case TYPE_QUAT:
    case TYPE_COL4:
    case TYPE_RECT:     controlp->set(partsOf(text, 4));                   break;
    default:            controlp->set(text);                               break;
    }

    // What was just written is what is shown, so the next frame does not
    // take the row down and build it again under whoever typed into it.
    mShownValue = settingText(controlp);
    mShownWritten = !controlp->isDefault();
    updateDefaultColumn(controlp);
}

// The way back, which this pane has always had as a button and which the grid
// offers on the row itself: what the file shipped is in force again.
void LLFloaterSettingsDebug::onEditorRemove(const std::string& name)
{
    LLControlVariable* controlp = gSavedSettings.getControl(name);
    if (!controlp)
    {
        controlp = gSavedPerAccountSettings.getControl(name);
    }
    if (!controlp || controlp->isHiddenFromSettingsEditor())
    {
        return;
    }
    controlp->resetToDefault(true);
    // The row is a different row now -- nothing writes this setting any more
    // -- so it is built again rather than left saying otherwise.
    mShownName.clear();
    updateDefaultColumn(controlp);
}

void LLFloaterSettingsDebug::updateList(bool skip_selection)
{
    std::string last_selected;
    LLScrollListItem* item = mSettingList->getFirstSelected();
    if (item)
    {
        LLScrollListCell* cell = item->getColumn(1);
        if (cell)
        {
            last_selected = cell->getValue().asString();
         }
    }

    mSettingList->deleteAllItems();
    struct f : public LLControlGroup::ApplyFunctor
    {
        LLScrollListCtrl* setting_list;
        LLFloaterSettingsDebug* floater;
        std::string selected_setting;
        bool skip_selection;
        f(LLScrollListCtrl* list, LLFloaterSettingsDebug* floater, std::string setting, bool skip_selection)
            : setting_list(list), floater(floater), selected_setting(setting), skip_selection(skip_selection) {}
        virtual void apply(const std::string& name, LLControlVariable* control)
        {
            if (!control->isHiddenFromSettingsEditor() && floater->matchesSearchFilter(name) && !floater->isSettingHidden(control))
            {
                LLSD row;

                row["columns"][0]["column"] = "changed_setting";
                row["columns"][0]["value"] = control->isDefault() ? "" : "*";

                row["columns"][1]["column"] = "setting";
                row["columns"][1]["value"] = name;

                LLScrollListItem* item = setting_list->addElement(row, ADD_BOTTOM, (void*)control);
                if (!floater->mSearchFilter.empty() && (selected_setting == name) && !skip_selection)
                {
                    std::string lower_name(name);
                    LLStringUtil::toLower(lower_name);
                    if (LLStringUtil::startsWith(lower_name, floater->mSearchFilter))
                    {
                        item->setSelected(true);
                    }
                }
            }
        }
    } func(mSettingList, this, last_selected, skip_selection);

    std::string key = getKey().asString();
    if (key == "all" || key == "base")
    {
        gSavedSettings.applyToAll(&func);
    }
    if (key == "all" || key == "account")
    {
        gSavedPerAccountSettings.applyToAll(&func);
    }


    if (!mSettingList->isEmpty())
    {
        if (mSettingList->hasSelectedItem())
        {
            mSettingList->scrollToShowSelected();
        }
        else if (!mSettingList->hasSelectedItem() && !mSearchFilter.empty() && !skip_selection)
        {
            if (!mSettingList->selectItemByPrefix(mSearchFilter, false, 1))
            {
                mSettingList->selectFirstItem();
            }
            mSettingList->scrollToShowSelected();
        }
    }
    else
    {
        LLSD row;

        row["columns"][0]["column"] = "changed_setting";
        row["columns"][0]["value"] = "";
        row["columns"][1]["column"] = "setting";
        row["columns"][1]["value"] = "No matching settings.";

        mSettingList->addElement(row);
        updateControl(nullptr);
    }
}

void LLFloaterSettingsDebug::onSettingSelect()
{
    LLScrollListItem* first_selected = mSettingList->getFirstSelected();
    if (first_selected)
    {
        LLControlVariable* controlp = (LLControlVariable*)first_selected->getUserdata();
        if (controlp)
        {
            updateControl(controlp);
        }
    }
}

void LLFloaterSettingsDebug::setSearchFilter(const std::string& filter)
{
    if(mSearchFilter == filter)
        return;
    mSearchFilter = filter;
    LLStringUtil::toLower(mSearchFilter);
    // Split mSearchFilter into tokens on '+' and store in mSearchTokens
    mSearchTokens.clear();
    std::string::size_type start = 0;
    while (true)
    {
        std::string::size_type plus = mSearchFilter.find('+', start);
        std::string token = mSearchFilter.substr(start, plus == std::string::npos ? plus : plus - start);
        LLStringUtil::trim(token);
        if (!token.empty())
        {
            mSearchTokens.push_back(token);
        }
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    updateList();
}

bool LLFloaterSettingsDebug::matchesSearchFilter(std::string setting_name)
{
    // If the search filter is empty, everything passes.
    if (mSearchFilter.empty()) return true;

    LLStringUtil::toLower(setting_name);

    std::string::size_type match_name = std::string::npos;

    for(const std::string& token : mSearchTokens)
    {
        match_name = setting_name.find(token);
        if(std::string::npos == match_name)
        {
            return false;
        }
    }

    return true;
}

bool LLFloaterSettingsDebug::isSettingHidden(LLControlVariable* control)
{
    static LLCachedControl<bool> hide_default(gSavedSettings, "DebugSettingsHideDefault", false);
    return hide_default && control->isDefault();
}

void LLFloaterSettingsDebug::updateDefaultColumn(LLControlVariable* control)
{
    if (isSettingHidden(control))
    {
        updateControl(nullptr);
        updateList(true);
        return;
    }

    LLScrollListItem* item = mSettingList->getFirstSelected();
    if (item)
    {
        LLScrollListCell* cell = item->getColumn(0);
        if (cell)
        {
            std::string is_default = control->isDefault() ? "" : "*";
            cell->setValue(is_default);
        }
    }
}

