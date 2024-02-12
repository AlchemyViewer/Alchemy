/**
 * @file llfloatermessagewriter.cpp
 *
 * $LicenseInfo:firstyear=2015&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "llfloatermessagerewriter.h"
#include "llviewercontrol.h"
#include "llscrolllistctrl.h"
#include "llcombobox.h"
#include "lltexteditor.h"
#include "llviewerwindow.h" // alertXml
#include "llmessagetemplate.h"
#include "llmessageconfig.h"
#include "llmenugl.h"
#include "llmessageconfig.h"

#include <boost/tokenizer.hpp>

LLFloaterMessageRewriter::LLFloaterMessageRewriter(const LLSD& key)
:	LLFloater(key)
{
}

BOOL LLFloaterMessageRewriter::postBuild()
{
	getChild<LLUICtrl>("save_rules")->setCommitCallback(boost::bind(&LLFloaterMessageRewriter::onClickSaveRules, this));
	getChild<LLUICtrl>("new_rule")->setCommitCallback(boost::bind(&LLFloaterMessageRewriter::onClickNewRule, this));
	refreshRuleList();
	return TRUE;
}

void LLFloaterMessageRewriter::refreshRuleList()
{
	LLScrollListCtrl* scrollp = getChild<LLScrollListCtrl>("rule_list");
	
	// Rebuild scroll list from scratch
	LLUUID selected_id = scrollp->getFirstSelected() ? scrollp->getFirstSelected()->getUUID() : LLUUID::null;
	S32 scroll_pos = scrollp->getScrollPos();
	scrollp->clearRows();
	
	LLSD element;
	element["id"] = 9999;

	LLSD& icon_column = element["columns"][0];
	icon_column["column"] = "icon_enabled";
	icon_column["type"] = "icon";
	icon_column["value"] = "";
	
	LLSD& rule_column = element["columns"][1];
	rule_column["column"] = "RuleName";
	rule_column["value"] = "A Cool Rule #1";
//	rule_column["color"] = gColors.getColor("DefaultListText").getValue();
	
	LLSD& direction_column = element["columns"][2];
	direction_column["column"] = "direction";
	direction_column["value"] = "Out";
//	direction_column["color"] = gColors.getColor("DefaultListText").getValue();
		
	LLScrollListItem* scroll_itemp = scrollp->addElement(element);
	
    LLScrollListIcon* icon = (LLScrollListIcon*)scroll_itemp->getColumn(0);
    icon->setValue("account_id_green.tga");
	
	if(selected_id.notNull()) scrollp->selectByID(selected_id);
	if(scroll_pos < scrollp->getItemCount()) scrollp->setScrollPos(scroll_pos);
	
	LLComboBox* messageChooser = getChild<LLComboBox>("message_type");
	
	LLSD& messages = LLMessageConfigFile::instance().mMessages;
	
	for(LLSD::map_iterator map_iter = messages.beginMap(), map_end = messages.endMap(); map_iter != map_end; ++map_iter)
	{
		std::string key((*map_iter).first);
		messageChooser->add(key, ADD_BOTTOM, true);
	}
}

void LLFloaterMessageRewriter::onClickSaveRules()
{
	// *TODO:
}

void LLFloaterMessageRewriter::onClickNewRule()
{
	// *TODO:
}
