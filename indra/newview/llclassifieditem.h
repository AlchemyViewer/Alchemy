/**
* @file llclassifieditem.h
* @brief Widget
*
* $LicenseInfo:firstyear=2009&license=viewerlgpl$
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

#ifndef LL_CLASSIFIEDITEM_H
#define LL_CLASSIFIEDITEM_H

#include "llavatarpropertiesprocessor.h"
#include "llpanel.h"

class LLPanelClassifiedEdit;

static const std::string CLASSIFIED_ID("classified_id");
static const std::string CLASSIFIED_NAME("classified_name");

class LLClassifiedItem final : public LLPanel, public LLAvatarPropertiesObserver
{
public:

	LLClassifiedItem(const LLUUID& avatar_id, const LLUUID& classified_id);
	virtual ~LLClassifiedItem();

	void processProperties(void* data, EAvatarProcessorType type) override;
	BOOL postBuild() override;
	void setValue(const LLSD& value) override;
	void fillIn(LLPanelClassifiedEdit* panel);
	LLUUID getAvatarId() const { return mAvatarId; }
	void setAvatarId(const LLUUID& avatar_id) { mAvatarId = avatar_id; }
	LLUUID getClassifiedId() const { return mClassifiedId; }
	void setClassifiedId(const LLUUID& classified_id) { mClassifiedId = classified_id; }
	void setPosGlobal(const LLVector3d& pos) { mPosGlobal = pos; }
	const LLVector3d getPosGlobal() const { return mPosGlobal; }
	void setLocationText(const std::string location) { mLocationText = std::move(location); }
	std::string getLocationText() const { return mLocationText; }
	void setClassifiedName(const std::string& name);
	std::string getClassifiedName() const { return getChild<LLUICtrl>("name")->getValue().asString(); }
	void setDescription(const std::string& desc);
	std::string getDescription() const { return getChild<LLUICtrl>("description")->getValue().asString(); }
	void setSnapshotId(const LLUUID& snapshot_id);
	LLUUID getSnapshotId() const;
	void setCategory(U32 cat) { mCategory = cat; }
	U32 getCategory() const { return mCategory; }
	void setContentType(U32 ct) { mContentType = ct; }
	U32 getContentType() const { return mContentType; }
	void setAutoRenew(U32 renew) { mAutoRenew = renew; }
	bool getAutoRenew() const { return mAutoRenew; }
	void setPriceForListing(S32 price) { mPriceForListing = price; }
	S32 getPriceForListing() const { return mPriceForListing; }

private:
	LLUUID mAvatarId;
	LLUUID mClassifiedId;
	LLVector3d mPosGlobal;
	std::string mLocationText;
	U32 mCategory;
	U32 mContentType;
	bool mAutoRenew;
	S32 mPriceForListing;
};

#endif // LL_CLASSIFIEDITEM_H
