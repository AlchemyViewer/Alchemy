/**
* @file alpickitem.cpp
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
#include "alpickitem.h"

#include "lltexturectrl.h"

ALPickItem::ALPickItem()
    : LLPanel()
    , mPickID(LLUUID::null)
    , mCreatorID(LLUUID::null)
    , mParcelID(LLUUID::null)
    , mSnapshotID(LLUUID::null)
    , mNeedData(true)
{
    buildFromFile("panel_al_pick_list_item.xml");
}

ALPickItem::~ALPickItem()
{
    if (mCreatorID.notNull())
    {
        LLAvatarPropertiesProcessor::instance().removeObserver(mCreatorID, this);
    }

}

ALPickItem* ALPickItem::create()
{
    return new ALPickItem();
}

void ALPickItem::init(LLPickData* pick_data)
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

void ALPickItem::setPickName(const std::string& name)
{
    mPickName = name;
    getChild<LLUICtrl>("picture_name")->setValue(name);

}

const std::string& ALPickItem::getPickName() const
{
    return mPickName;
}

const LLUUID& ALPickItem::getCreatorId() const
{
    return mCreatorID;
}

const LLUUID& ALPickItem::getSnapshotId() const
{
    return mSnapshotID;
}

void ALPickItem::setPickDesc(const std::string& descr)
{
    getChild<LLUICtrl>("picture_descr")->setValue(descr);
}

void ALPickItem::setPickId(const LLUUID& id)
{
    mPickID = id;
}

const LLUUID& ALPickItem::getPickId() const
{
    return mPickID;
}

const LLVector3d& ALPickItem::getPosGlobal() const
{
    return mPosGlobal;
}

const std::string ALPickItem::getDescription() const
{
    return getChild<LLUICtrl>("picture_descr")->getValue().asString();
}

void ALPickItem::update()
{
    setNeedData(true);
    LLAvatarPropertiesProcessor::instance().sendPickInfoRequest(mCreatorID, mPickID);
}

void ALPickItem::processProperties(void *data, EAvatarProcessorType type)
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

bool ALPickItem::postBuild()
{
    setMouseEnterCallback(std::bind(&set_child_visible, this, "hovered_icon", true));
    setMouseLeaveCallback(std::bind(&set_child_visible, this, "hovered_icon", false));
    return true;
}

void ALPickItem::setValue(const LLSD& value)
{
    if (!value.isMap()) return;;
    if (!value.has("selected")) return;
    getChildView("selected_icon")->setVisible(value["selected"]);
}

