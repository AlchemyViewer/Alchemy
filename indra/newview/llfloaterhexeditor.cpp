/**
 * @file llfloaterhex.h
 * @brief Hex Editor Floater made by Day
 * @author Day Oh, Skills, Cinder
 * 
 * $LicenseInfo:firstyear=2009&license=WTFPLV2$
 *  
 */

#include "llviewerprecompiledheaders.h"

#include "llfloaterhexeditor.h"
#include "llbutton.h"
#include "llagent.h"
#include "lleconomy.h"
#include "llextendedstatus.h"
#include "llfloaterperms.h"
#include "llfloaterreg.h"
#include "llinventorymodel.h"
#include "llassetstorage.h"
#include "llfilesystem.h"
#include "llnotificationsutil.h"
#include "llviewermenufile.h"
#include "llviewerregion.h"
#include "llviewertexturelist.h"


LLFloaterHexEditor::LLFloaterHexEditor(const LLSD& key)
:	LLFloater(key)
,	mItem(nullptr)
,	mAssetType(LLAssetType::AT_UNKNOWN)
,	mEditor(nullptr)
{ }

BOOL LLFloaterHexEditor::postBuild()
{
	mEditor = getChild<LLHexEditor>("hex");
	/*
#ifndef COLUMN_SPAN
	// Set number of columns
	U8 columns = U8(gSavedSettings.getU32("HexEditorColumns"));
	editor->setColumns(columns);
	// Reflect clamped U8ness in settings
	gSavedSettings.setU32("HexEditorColumns", U32(columns));
#endif
	*/
	handleSizing();

	LLButton* upload_btn = getChild<LLButton>("upload_btn");
    upload_btn->setEnabled(false);
    upload_btn->setLabelArg("[UPLOAD]", LLStringExplicit("Upload"));
    upload_btn->setCommitCallback(boost::bind(&LLFloaterHexEditor::onClickUpload, this));

	LLButton* save_btn = getChild<LLButton>("save_btn");
    save_btn->setEnabled(false);
    save_btn->setCommitCallback(boost::bind(&LLFloaterHexEditor::onClickSave, this));

	return TRUE;
}

void LLFloaterHexEditor::onOpen(const LLSD& key)
{
    LLUUID inv_id = key.has("inv_id") ? key["inv_id"].asUUID() : LLUUID::null;

	LL_INFOS("HEX") << "Inventory ID: " << inv_id.asString() << LL_ENDL;
	if (inv_id.isNull()) { return; }
	
	if (key.has("asset_type")) { mAssetType = static_cast<LLAssetType::EType>(key["asset_type"].asInteger()); }
	
    mItem = gInventory.getItem(inv_id);

	if (mItem)
	{
		std::string title = "Hex editor: " + mItem->getName();
		const char* asset_type_name = LLAssetType::lookup(mItem->getType());
		if(asset_type_name)
		{
			title.append(llformat(" (%s)", asset_type_name));
		}
		setTitle(title);
	}

	// Load the asset
	mEditor->setVisible(FALSE);
	childSetTextArg("status_text", "[STATUS]", LLStringExplicit("Loading..."));
	download(mItem, imageCallback, assetCallback);

	refresh();
}

// static
void LLFloaterHexEditor::download(LLInventoryItem* item, loaded_callback_func onImage, LLGetAssetCallback onAsset)
{
    if (item == nullptr)
    {
        LL_WARNS("Hex") << "Could not download null pointer encountered!" << LL_ENDL;
		return;
    }
    switch (item->getType())
    {
        case LLAssetType::AT_TEXTURE:
        {
            LLPointer<LLViewerFetchedTexture> texture = LLViewerTextureManager::getFetchedTexture(
                item->getAssetUUID(), FTT_DEFAULT, MIPMAP_YES, LLViewerTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
            texture->setBoostLevel(LLViewerTexture::BOOST_PREVIEW);
            texture->forceToSaveRawImage(0);
            texture->setLoadedCallbackNoAux(onImage, 0, TRUE, FALSE, item, nullptr);
            break;
        }
        case LLAssetType::AT_NOTECARD:
        case LLAssetType::AT_SCRIPT:
        case LLAssetType::AT_LSL_TEXT:  // normal script download
        case LLAssetType::AT_LSL_BYTECODE:
        {
            gAssetStorage->getInvItemAsset(LLHost(),
                                           gAgent.getID(),
                                           gAgent.getSessionID(),
                                           item->getPermissions().getOwner(),
                                           LLUUID::null,
                                           item->getUUID(),
                                           item->getAssetUUID(),
                                           item->getType(),
                                           onAsset,
                                           item,  // user_data
                                           TRUE);
            break;
        }
        case LLAssetType::AT_SOUND:
        case LLAssetType::AT_CLOTHING:
        case LLAssetType::AT_BODYPART:
        case LLAssetType::AT_ANIMATION:
        case LLAssetType::AT_GESTURE:
        default:
        {
            gAssetStorage->getAssetData(item->getAssetUUID(), item->getType(), onAsset, item, TRUE);
            break;
        }
    }
}

// static
void LLFloaterHexEditor::imageCallback(BOOL success, 
					LLViewerFetchedTexture *src_vi,
					LLImageRaw* src, 
					LLImageRaw* aux_src, 
					S32 discard_level,
					BOOL final,
					void* userdata)
{
	if (final)
	{
        const auto* item = static_cast<LLInventoryItem*>(userdata);
        LLFloaterHexEditor* self = LLFloaterReg::findTypedInstance<LLFloaterHexEditor>("asset_hex_editor", 
				LLSD().with("inv_id", item->getUUID()).with("asset_type", item->getActualType()));
		if (!self) return;

		if(!success)
		{
			self->childSetTextArg("status_text", "[STATUS]", LLStringExplicit("Unable to download asset."));
			return;
		}

		U8* src_data = src->getData();
		S32 src_size = src->getDataSize();
		std::vector<U8> new_data;
		for(S32 i = 0; i < src_size; ++i)
			new_data.push_back(src_data[i]);

		self->mEditor->setValue(new_data);
        self->mEditor->setVisible(TRUE);
        self->childSetTextArg("status_text", "[STATUS]", LLStringExplicit("Note: Image data shown isn't the actual asset data, yet"));

		self->childSetEnabled("save_btn", false);
        self->childSetEnabled("upload_btn", true);
        self->childSetLabelArg("upload_btn", "[UPLOAD]", std::string("Upload (L$10)"));
	}
	else
	{
		src_vi->setBoostLevel(LLViewerTexture::BOOST_UI);
	}
}

// static
void LLFloaterHexEditor::assetCallback(const LLUUID& asset_uuid,
				   LLAssetType::EType type,
				   void* user_data, S32 status, LLExtStat ext_status)
{
    const auto item = static_cast<LLInventoryItem*>(user_data);
    LLFloaterHexEditor* self = LLFloaterReg::findTypedInstance<LLFloaterHexEditor>("asset_hex_editor",
        LLSD().with("inv_id", item->getUUID()).with("asset_type", item->getActualType()));
	if (!self) return;

	if(status != 0 && item->getType() != LLAssetType::AT_NOTECARD)
	{
		self->childSetTextArg("status_text", "[STATUS]", LLStringExplicit("Unable to download asset."));
		return;
	}

	LLFileSystem file(asset_uuid, type, LLFileSystem::READ);
	S32 file_size = file.getSize();

	auto buffer = std::make_unique<U8[]>(file_size);
	if (buffer == nullptr)
	{
        LL_ERRS("Hex") << "Memory Allocation Failed" << LL_ENDL;
		return;
	}

    if (!file.open() || !file.read(buffer.get(), file_size))
    {
        LL_WARNS("Hex") << "Could not read " << asset_uuid.asString() << " into memory" << LL_ENDL;
    }
    file.close();

	std::vector<U8> new_data;
	for(S32 i = 0; i < file_size; ++i)
		new_data.push_back(buffer[i]);

	self->mEditor->setValue(new_data);
    self->mEditor->setVisible(TRUE);
    self->childSetTextArg("status_text", "[STATUS]", LLStringUtil::null);

	self->childSetEnabled("upload_btn", true);
    self->childSetEnabled("save_btn", false);
	if(item->getPermissions().allowModifyBy(gAgent.getID()))
	{
		switch(item->getType())
		{
		case LLAssetType::AT_TEXTURE:
		case LLAssetType::AT_ANIMATION:
		case LLAssetType::AT_SOUND:
            self->childSetLabelArg("upload_btn", "[UPLOAD]", LLStringExplicit("Upload (L$10)"));
			break;
		case LLAssetType::AT_LANDMARK:
		case LLAssetType::AT_CALLINGCARD:
            self->childSetEnabled("upload_btn", false);
            self->childSetEnabled("save_btn", false);
			break;
		default:
            self->childSetEnabled("save_btn", true);
			break;
		}
	}
	else
	{
		switch(item->getType())
		{
		case LLAssetType::AT_TEXTURE:
		case LLAssetType::AT_ANIMATION:
		case LLAssetType::AT_SOUND:
            self->childSetLabelArg("upload_btn", "[UPLOAD]", LLStringExplicit("Upload (L$10)"));
			break;
		default:
			break;
		}
	}

	// Never enable save if it's a local inventory item
	if(gInventory.isObjectDescendentOf(item->getUUID(), gLocalInventory))
	{
        self->childSetEnabled("save_btn", false);
	}
}

void LLFloaterHexEditor::onClickUpload()
{
    const LLInventoryItem* item = mItem;

	LLTransactionID transaction_id;
	transaction_id.generate();
	LLUUID fake_asset_id = transaction_id.makeAssetID(gAgent.getSecureSessionID());

	std::vector<U8> value = mEditor->getValue();
	size_t val_size = value.size();
    auto buffer = std::make_unique<U8[]>(val_size);
	for(size_t i = 0; i < val_size; ++i)
		buffer[i] = value[i];
	value.clear();

	LLFileSystem file(fake_asset_id, item->getType(), LLFileSystem::APPEND);
    if (!file.open() || !file.write(buffer.get(), val_size))
	{
        LLSD args = LLSD().with("MESSAGE", "Couldn't write data to file");
		LLNotificationsUtil::add("GenericAlert", args);
		return;
	}
    file.close();

	LLAssetStorage::LLStoreAssetCallback callback  = nullptr;
	S32 expected_upload_cost = LLGlobalEconomy::getInstance()->getPriceUpload();

	if(item->getType() == LLAssetType::AT_GESTURE ||
	   item->getType() == LLAssetType::AT_LSL_TEXT ||
	   item->getType() == LLAssetType::AT_NOTECARD)
		// gestures, notecards and scripts, create an item first
	{
		create_inventory_item(gAgent.getID(), 
			    gAgent.getSessionID(),
				item->getParentUUID(),
				LLTransactionID::tnull,
				item->getName(),
				fake_asset_id.asString(),
				item->getType(),
				item->getInventoryType(),
				item->getFlags(),
				PERM_ITEM_UNRESTRICTED, 
			    LLPointer<LLInventoryCallback>(nullptr));
	}
	else 
	{
        LLResourceUploadInfo::ptr_t uploadInfo(
			new LLResourceUploadInfo(transaction_id, 
				item->getType(),
				item->getName(),
				item->getDescription(), 
				0, LLFolderType::FT_NONE,
				item->getInventoryType(),
                LLFloaterPerms::getNextOwnerPerms("Uploads"), 
				LLFloaterPerms::getGroupPerms("Uploads"),
				LLFloaterPerms::getEveryonePerms("Uploads"), 
				expected_upload_cost));
		upload_new_resource(uploadInfo);
	}
}

void LLFloaterHexEditor::onSavedAsset(const LLUUID& id, const LLSD& response)
{
    // TODO: Something 
}

void LLFloaterHexEditor::onClickSave()
{
    LLInventoryItem* item = mItem;

	LLTransactionID transaction_id;
	transaction_id.generate();
    const LLUUID fake_asset_id = transaction_id.makeAssetID(gAgent.getSecureSessionID());

	std::vector<U8> value = mEditor->getValue();
	size_t val_size = value.size();
    auto buffer = std::make_unique<U8[]>(val_size);
	for(size_t i = 0; i < val_size; ++i)
		buffer[i] = value[i];
	value.clear();

	LLFileSystem file(fake_asset_id, item->getType(), LLFileSystem::APPEND);
    if (file.open() && file.getMaxSize() > val_size)
    {
        if (!file.write(buffer.get(), val_size))
        {
            LLSD args = LLSD().with("MESSAGE", "Could not write data to file");
            LLNotificationsUtil::add("GenericAlert", args);
            return;
        }
        file.close();
    }


	std::string url;
    LLResourceUploadInfo::ptr_t uploadInfo;

	switch(item->getType())
	{
	case LLAssetType::AT_LSL_TEXT:
	{
		url = gAgent.getRegion()->getCapability("UpdateScriptAgent");
            uploadInfo = std::make_shared<LLScriptAssetUpload>(mItem->getUUID(), 
                                                               std::string(reinterpret_cast<char*>(buffer.get())), 
                                                               boost::bind(&LLFloaterHexEditor::onSavedAsset, this, _1, _4));
		break;
	}
	case LLAssetType::AT_GESTURE:
	{
		url = gAgent.getRegion()->getCapability("UpdateGestureAgentInventory");
        uploadInfo = std::make_shared<LLBufferedAssetUploadInfo>(mItem->getUUID(), LLAssetType::AT_GESTURE,
                                                                 std::string(reinterpret_cast<char*>(buffer.get())),
                                                                 boost::bind(&LLFloaterHexEditor::onSavedAsset, this, _1, _4));
		break;
	}
	case LLAssetType::AT_NOTECARD:
	{
		url = gAgent.getRegion()->getCapability("UpdateNotecardAgentInventory");
        uploadInfo = std::make_shared<LLBufferedAssetUploadInfo>(mItem->getUUID(), LLAssetType::AT_NOTECARD,
                                                                 std::string(reinterpret_cast<char*>(buffer.get())),
                                                                 boost::bind(&LLFloaterHexEditor::onSavedAsset, this, _1, _4));
		break;
	}
	case LLAssetType::AT_SETTINGS:
    {
        url = gAgent.getRegion()->getCapability("UpdateSettingsAgentInventory");
        uploadInfo = std::make_shared<LLBufferedAssetUploadInfo>(mItem->getUUID(), LLAssetType::AT_SETTINGS,
                                                                 std::string(reinterpret_cast<char*>(buffer.get())),
                                                                 boost::bind(&LLFloaterHexEditor::onSavedAsset, this, _1, _4));
        break;
    }
	default:
    {
        childSetTextArg("status_text", "[STATUS]", LLStringExplicit("Saving..."));
        gAssetStorage->storeAssetData(transaction_id, item->getType(), onSaveComplete, item);
        break;
    }
	}

	if(!url.empty() && uploadInfo.get())
	{
        LLViewerAssetUpload::EnqueueInventoryUpload(url, uploadInfo);
	}
}

void LLFloaterHexEditor::onSaveComplete(const LLUUID& asset_uuid, void* user_data, S32 status, LLExtStat ext_status)
{
    const auto item = static_cast<LLInventoryItem*>(user_data); 
	LLFloaterHexEditor* self = LLFloaterReg::findTypedInstance<LLFloaterHexEditor>("asset_hex_editor",
        LLSD().with("inv_id", item->getUUID()).with("asset_type", item->getActualType()));

	self->childSetTextArg("status_text", "[STATUS]", LLStringUtil::null);

	if(item && (status == 0))
	{
		LLPointer<LLViewerInventoryItem> new_item = new LLViewerInventoryItem(item);
		new_item->setDescription(item->getDescription());
		//new_item->setTransactionID(info->mTransactionID);
		new_item->setAssetUUID(asset_uuid);
		new_item->updateServer(FALSE);
		gInventory.updateItem(new_item);
		gInventory.notifyObservers();
	}
	else
	{
        LLSD args = LLSD().with("MESSAGE", llformat("Save failed with status %d, also %d", status, ext_status));
		LLNotificationsUtil::add("GenericAlert", args);
	}
}

void LLFloaterHexEditor::onCommitColumnCount(LLUICtrl *control)
{
	if (control)
	{
		U8 columns = llclamp<U8>((U8)llfloor(control->getValue().asReal()), 0x00, 0xFF);
		mEditor->setColumns(columns);
		handleSizing();
	}
}

void LLFloaterHexEditor::handleSizing()
{
	// Reshape a little based on columns
	S32 min_width = static_cast<S32>(mEditor->getSuggestedWidth(MIN_COLS)) + 20;
	setResizeLimits(min_width, getMinHeight());
	if(getRect().getWidth() < min_width)
	{
		reshape(min_width, getRect().getHeight(), FALSE);
		mEditor->reshape(mEditor->getRect().getWidth(), mEditor->getRect().getHeight(), TRUE);
	}
}
