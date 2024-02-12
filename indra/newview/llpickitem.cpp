/**
* @file llpickitem.cpp
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

#include "llviewerprecompiledheaders.h"
#include "llpickitem.h"

#include "lltexturectrl.h"

LLPickItem::LLPickItem()
	: LLPanel()
	, mPickID(LLUUID::null)
	, mCreatorID(LLUUID::null)
	, mParcelID(LLUUID::null)
	, mSnapshotID(LLUUID::null)
	, mNeedData(true)
{
	buildFromFile("panel_pick_list_item.xml");
}

LLPickItem::~LLPickItem()
{
	if (mCreatorID.notNull())
	{
		LLAvatarPropertiesProcessor::instance().removeObserver(mCreatorID, this);
	}

}

LLPickItem* LLPickItem::create()
{
	return new LLPickItem();
}

void LLPickItem::init(LLPickData* pick_data)
{
	setPickDesc(pick_data->desc);
	setSnapshotId(pick_data->snapshot_id);
	mPosGlobal = pick_data->pos_global;
	mSimName = pick_data->sim_name;
	mPickDescription = pick_data->desc;
	mUserName = pick_data->user_name;
	mOriginalName = pick_data->original_name;

	LLTextureCtrl* picture = getChild<LLTextureCtrl>("picture");
	picture->setImageAssetID(pick_data->snapshot_id);
}

void LLPickItem::setPickName(const std::string& name)
{
	mPickName = name;
	getChild<LLUICtrl>("picture_name")->setValue(name);

}

const std::string& LLPickItem::getPickName() const
{
	return mPickName;
}

const LLUUID& LLPickItem::getCreatorId() const
{
	return mCreatorID;
}

const LLUUID& LLPickItem::getSnapshotId() const
{
	return mSnapshotID;
}

void LLPickItem::setPickDesc(const std::string& descr)
{
	getChild<LLUICtrl>("picture_descr")->setValue(descr);
}

void LLPickItem::setPickId(const LLUUID& id)
{
	mPickID = id;
}

const LLUUID& LLPickItem::getPickId() const
{
	return mPickID;
}

const LLVector3d& LLPickItem::getPosGlobal() const
{
	return mPosGlobal;
}

const std::string LLPickItem::getDescription() const
{
	return getChild<LLUICtrl>("picture_descr")->getValue().asString();
}

void LLPickItem::update()
{
	setNeedData(true);
	LLAvatarPropertiesProcessor::instance().sendPickInfoRequest(mCreatorID, mPickID);
}

void LLPickItem::processProperties(void *data, EAvatarProcessorType type)
{
	if (APT_PICK_INFO != type)
	{
		return;
	}

	LLPickData* pick_data = static_cast<LLPickData *>(data);
	if (!pick_data || mPickID != pick_data->pick_id)
	{
		return;
	}

	init(pick_data);
	setNeedData(false);
	LLAvatarPropertiesProcessor::instance().removeObserver(mCreatorID, this);
}

BOOL LLPickItem::postBuild()
{
	setMouseEnterCallback(std::bind(&set_child_visible, this, "hovered_icon", true));
	setMouseLeaveCallback(std::bind(&set_child_visible, this, "hovered_icon", false));
	return TRUE;
}

void LLPickItem::setValue(const LLSD& value)
{
	if (!value.isMap()) return;;
	if (!value.has("selected")) return;
	getChildView("selected_icon")->setVisible(value["selected"]);
}

