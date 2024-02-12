/**
* @file llpickitem.h
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

#ifndef LL_PICKITEM_H
#define LL_PICKITEM_H

#include "llavatarpropertiesprocessor.h"
#include "llpanel.h"

struct LLPickData;

static const std::string PICK_ID("pick_id");
static const std::string PICK_CREATOR_ID("pick_creator_id");
static const std::string PICK_NAME("pick_name");

class LLPickItem final : public LLPanel, public LLAvatarPropertiesObserver
{
public:
	LLPickItem();
	~LLPickItem();
	BOOL postBuild() override;

	static LLPickItem* create();
	void init(LLPickData* pick_data);
	void setPickName(const std::string& name);
	void setPickDesc(const std::string& descr);
	void setPickId(const LLUUID& id);
	void setCreatorId(const LLUUID& id) { mCreatorID = id; };
	void setSnapshotId(const LLUUID& id) { mSnapshotID = id; };
	void setNeedData(bool need) { mNeedData = need; };
	const LLUUID& getPickId() const;
	const std::string& getPickName() const;
	const LLUUID& getCreatorId() const;
	const LLUUID& getSnapshotId() const;
	const LLVector3d& getPosGlobal() const;
	const std::string getDescription() const;
	const std::string& getSimName() const { return mSimName; }
	const std::string& getUserName() const { return mUserName; }
	const std::string& getOriginalName() const { return mOriginalName; }
	const std::string& getPickDesc() const { return mPickDescription; }
	void processProperties(void* data, EAvatarProcessorType type) override;
	void update();

	/** setting on/off background icon to indicate selected state */
	void setValue(const LLSD& value) override;

protected:
	LLUUID mPickID;
	LLUUID mCreatorID;
	LLUUID mParcelID;
	LLUUID mSnapshotID;
	LLVector3d mPosGlobal;
	bool mNeedData;

	std::string mPickName;
	std::string mUserName;
	std::string mOriginalName;
	std::string mPickDescription;
	std::string mSimName;
};

#endif // LL_PICKITEM_H
