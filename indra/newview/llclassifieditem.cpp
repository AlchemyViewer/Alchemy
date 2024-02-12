/**
* @file llclassifieditem.cpp
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
#include "llclassifieditem.h"

#include "llpanelclassified.h"

LLClassifiedItem::LLClassifiedItem(const LLUUID& avatar_id, const LLUUID& classified_id)
	: LLPanel()
	, mAvatarId(avatar_id)
	, mClassifiedId(classified_id)
{
	buildFromFile("panel_classifieds_list_item.xml");

	LLAvatarPropertiesProcessor::getInstance()->addObserver(getAvatarId(), this);
	LLAvatarPropertiesProcessor::getInstance()->sendClassifiedInfoRequest(getClassifiedId());
}

LLClassifiedItem::~LLClassifiedItem()
{
	LLAvatarPropertiesProcessor::getInstance()->removeObserver(getAvatarId(), this);
}

void LLClassifiedItem::processProperties(void* data, EAvatarProcessorType type)
{
	if (APT_CLASSIFIED_INFO != type)
	{
		return;
	}

	LLAvatarClassifiedInfo* c_info = static_cast<LLAvatarClassifiedInfo*>(data);
	if (!c_info || c_info->classified_id != getClassifiedId())
	{
		return;
	}

	setClassifiedName(c_info->name);
	setDescription(c_info->description);
	setSnapshotId(c_info->snapshot_id);
	setPosGlobal(c_info->pos_global);

	LLAvatarPropertiesProcessor::getInstance()->removeObserver(getAvatarId(), this);
}

BOOL LLClassifiedItem::postBuild()
{
	setMouseEnterCallback(std::bind(&set_child_visible, this, "hovered_icon", true));
	setMouseLeaveCallback(std::bind(&set_child_visible, this, "hovered_icon", false));
	return TRUE;
}

void LLClassifiedItem::setValue(const LLSD& value)
{
	if (!value.isMap()) return;;
	if (!value.has("selected")) return;
	getChildView("selected_icon")->setVisible(value["selected"]);
}

void LLClassifiedItem::fillIn(LLPanelClassifiedEdit* panel)
{
	if (!panel)
	{
		return;
	}

	setClassifiedName(panel->getClassifiedName());
	setDescription(panel->getDescription());
	setSnapshotId(panel->getSnapshotId());
	setCategory(panel->getCategory());
	setContentType(panel->getContentType());
	setAutoRenew(panel->getAutoRenew());
	setPriceForListing(panel->getPriceForListing());
	setPosGlobal(panel->getPosGlobal());
	setLocationText(panel->getClassifiedLocation());
}

void LLClassifiedItem::setClassifiedName(const std::string& name)
{
	getChild<LLUICtrl>("name")->setValue(name);
}

void LLClassifiedItem::setDescription(const std::string& desc)
{
	getChild<LLUICtrl>("description")->setValue(desc);
}

void LLClassifiedItem::setSnapshotId(const LLUUID& snapshot_id)
{
	getChild<LLUICtrl>("picture")->setValue(snapshot_id);
}

LLUUID LLClassifiedItem::getSnapshotId() const
{
	return getChild<LLUICtrl>("picture")->getValue();
}
