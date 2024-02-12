/**
 * @file llfloaternewlocalinventory.cpp
 * @brief Create local inventory item
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Cinder Roxley
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
#include "llfloaternewlocalinventory.h"

#include "llagent.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llinventorymodel.h"
#include "lllineeditor.h"
#include "lluuid.h"
#include "llviewerinventory.h"
#include "llfloaterreg.h"
#include "llpreviewgesture.h"

LLUUID LLFloaterNewLocalInventory::sLastCreatorId(LLUUID::null);

static void add_item(LLViewerInventoryItem* item);
static void open_asset(LLUUID const& item_id);

LLFloaterNewLocalInventory::LLFloaterNewLocalInventory(const LLSD& key)
:   LLFloater(key)
{ }

BOOL LLFloaterNewLocalInventory::postBuild()
{
    getChild<LLLineEditor>("creator_id_line")->setText(LLUUID::null.asString());
    getChild<LLLineEditor>("owner_id_line")->setText(gAgent.getID().asString());
    getChild<LLLineEditor>("asset_id_line")->setText(LLUUID::null.asString());
    getChild<LLLineEditor>("name_line")->setText(std::string(""));
    getChild<LLLineEditor>("desc_line")->setText(std::string(""));
    getChild<LLButton>("ok_btn")->setCommitCallback(boost::bind(&LLFloaterNewLocalInventory::onClickOK, this));

    LLComboBox* combo = getChild<LLComboBox>("type_combo");
    std::vector<std::string> type_names = LLAssetType::getAssetTypeNames();
    for (auto const& name : type_names)
    {
        combo->add(name);
    }

    return TRUE;
}

void LLFloaterNewLocalInventory::onClickOK()
{
    LLUUID item_id;
    item_id.generate();

    std::string name       = getChild<LLLineEditor>("name_line")->getText();
    std::string desc       = getChild<LLLineEditor>("desc_line")->getText();
    LLUUID      asset_id   = LLUUID(getChild<LLLineEditor>("asset_id_line")->getText());
    LLUUID      creator_id = LLUUID(getChild<LLLineEditor>("creator_id_line")->getText());
    LLUUID      owner_id   = LLUUID(getChild<LLLineEditor>("owner_id_line")->getText());

    LLAssetType::EType     type     = LLAssetType::lookup(getChild<LLUICtrl>("type_combo")->getValue().asString());
    LLInventoryType::EType inv_type = LLInventoryType::IT_UNKNOWN;
    switch (type)
    {
        case LLAssetType::AT_TEXTURE:
        case LLAssetType::AT_TEXTURE_TGA:
        case LLAssetType::AT_IMAGE_TGA:
        case LLAssetType::AT_IMAGE_JPEG:
            inv_type = LLInventoryType::IT_TEXTURE;
            break;
        case LLAssetType::AT_SOUND:
        case LLAssetType::AT_SOUND_WAV:
            inv_type = LLInventoryType::IT_SOUND;
            break;
        case LLAssetType::AT_CALLINGCARD:
            inv_type = LLInventoryType::IT_CALLINGCARD;
            break;
        case LLAssetType::AT_LANDMARK:
            inv_type = LLInventoryType::IT_LANDMARK;
            break;
        case LLAssetType::AT_SCRIPT:
            inv_type = LLInventoryType::IT_LSL;
            break;
        case LLAssetType::AT_CLOTHING:
            inv_type = LLInventoryType::IT_WEARABLE;
            break;
        case LLAssetType::AT_OBJECT:
            inv_type = LLInventoryType::IT_OBJECT;
            break;
        case LLAssetType::AT_NOTECARD:
            inv_type = LLInventoryType::IT_NOTECARD;
            break;
        case LLAssetType::AT_CATEGORY:
            inv_type = LLInventoryType::IT_CATEGORY;
            break;
        case LLAssetType::AT_LSL_TEXT:
        case LLAssetType::AT_LSL_BYTECODE:
            inv_type = LLInventoryType::IT_LSL;
            break;
        case LLAssetType::AT_BODYPART:
            inv_type = LLInventoryType::IT_WEARABLE;
            break;
        case LLAssetType::AT_ANIMATION:
            inv_type = LLInventoryType::IT_ANIMATION;
            break;
        case LLAssetType::AT_GESTURE:
            inv_type = LLInventoryType::IT_GESTURE;
            break;
        case LLAssetType::AT_SETTINGS:
            inv_type = LLInventoryType::IT_SETTINGS;
            break;
        case LLAssetType::AT_MESH:
            inv_type = LLInventoryType::IT_MESH;
            break;
        case LLAssetType::AT_SIMSTATE:
            // *TODO: maybe I missed something? IDK. IDGAF.
        default:
            LL_WARNS("LocalInventory") << "Could not determine LLInventoryType for " << type << "." << LL_ENDL;
            inv_type = LLInventoryType::IT_UNKNOWN;
            break;
    }

    auto* perms = new LLPermissions();
    perms->init(creator_id, owner_id, LLUUID::null, LLUUID::null);

    auto* item =
        new LLViewerInventoryItem(item_id, gLocalInventory, 
            *perms, asset_id, type, inv_type, name, desc, LLSaleInfo::DEFAULT, 
            0, 0);

    add_item(item);
    if (getChild<LLUICtrl>("chk_open")->getValue().asBoolean())
    {
       open_asset(item_id);
    }

    sLastCreatorId = creator_id;
    closeFloater();
}

void add_item(LLViewerInventoryItem* item)
{
    LLInventoryModel::update_map_t update;
    ++update[item->getParentUUID()];
    gInventory.accountForUpdate(update);
    gInventory.updateItem(item);
    gInventory.notifyObservers();
}

void open_asset(LLUUID const& item_id)
{
    LLViewerInventoryItem* item    = gInventory.getItem(item_id);
    LLUUID                 assetid = gInventory.getObject(item_id)->getUUID();
    if (!item)
    {
        LL_WARNS("Local Inv") << "Trying to open non-existent item" << LL_ENDL;
        return;
    }

    LLAssetType::EType type = item->getType();
    switch (type)
    {
        case LLAssetType::AT_SOUND:
        case LLAssetType::AT_SOUND_WAV:
        {
            LLFloaterReg::showInstance("preview_sound", LLSD(assetid), TAKE_FOCUS_NO);
            break;
        }
        case LLAssetType::AT_ANIMATION:
        {
            LLFloaterReg::showInstance("preview_anim", LLSD(assetid), TAKE_FOCUS_NO);
            break;
        }
        case LLAssetType::AT_TEXTURE:
        {
            LLFloaterReg::showInstance("preview_texture", LLSD(assetid), TAKE_FOCUS_NO);
            break;
        }
        case LLAssetType::AT_GESTURE:
        {
            LLPreviewGesture* preview = LLPreviewGesture::show(assetid, LLUUID::null);
            preview->setFocus(TRUE);
            break;
        }
        // *TODO: Handle lsl, notecards, AT_SETTINGS, etc.
        default:
        {
            LL_WARNS("LocalInventory") << "Cannot open type " << type << "." << LL_ENDL;
            break;
        }
    }
}
