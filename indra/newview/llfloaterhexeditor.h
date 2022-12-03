/**
 * @file llfloaterhex.h
 * @brief Hex Editor Floater made by Day
 * @author Day Oh, Skills, Cinder
 * 
 * $LicenseInfo:firstyear=2009&license=WTFPLV2$
 *  
 */

#ifndef LL_FLOATERHEX_H
#define LL_FLOATERHEX_H

#include "llassetstorage.h"
#include "llassettype.h"
#include "llextendedstatus.h"
#include "llfloater.h"
#include "llhexeditor.h"
#include "llinventory.h"
#include "llviewertexture.h"

class LLFloaterHexEditor : public LLFloater
{
public:
	LLFloaterHexEditor(const LLSD& key);

	void onOpen(const LLSD& key) override;
	BOOL postBuild();

	LLInventoryItem* mItem;
	LLAssetType::EType mAssetType;
	LLHexEditor* mEditor;

	static void imageCallback(BOOL success, 
					LLViewerFetchedTexture *src_vi,
					LLImageRaw* src, 
					LLImageRaw* aux_src, 
					S32 discard_level,
					BOOL final,
					void* userdata);
	static void assetCallback(const LLUUID& asset_uuid,
		            LLAssetType::EType type,
				    void* user_data, S32 status, LLExtStat ext_status);



	static void download(LLInventoryItem* item, loaded_callback_func onImage, LLGetAssetCallback onAsset);
	static void onSaveComplete(const LLUUID& asset_uuid, void* user_data, S32 status, LLExtStat ext_status);

private:
    ~LLFloaterHexEditor() override = default;

    void onClickSave();
    void onClickUpload();

	void onSavedAsset(const LLUUID& id, const LLSD& response);
	void onCommitColumnCount(LLUICtrl *control);
	void handleSizing();
};

#endif
