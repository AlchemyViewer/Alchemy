/**
 * @file lleasymessagesender.cpp
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

#ifndef LL_LLFLOATERMESSAGEBUILDER_H
#define LL_LLFLOATERMESSAGEBUILDER_H

#include "lltemplatemessagereader.h"
#include "llmessagelog.h"
#include "lleventtimer.h"
#include "llcircuit.h"
#include "llfloater.h"
#include "lleasymessagesender.h"

class LLUICtrl;

struct LLNetListItem
{
	LLNetListItem(LLUUID id);
	LLUUID mID;
	BOOL mAutoName;
	std::string mName;
	std::string mPreviousRegionName;
	U64 mHandle;
	LLCircuitData* mCircuitData;
};

class LLFloaterMessageBuilder final : public LLFloater, public LLEventTimer
{
public:
	LLFloaterMessageBuilder(const LLSD &);
	~LLFloaterMessageBuilder() = default;
	BOOL postBuild() override;
	void onOpen(const LLSD& key) override;
	static void show(const std::string& initial_text);
	
private:
	static std::list<LLNetListItem*> sNetListItems;
	
	BOOL tick() override;
	void onClickSend();
	void onCommitPacketCombo(LLUICtrl* ctrl);

	static LLFloaterMessageBuilder* sInstance;
	BOOL handleKeyHere(KEY key, MASK mask) override;
	std::string mInitialText;

	static LLNetListItem* findNetListItem(LLHost host);
	static LLNetListItem* findNetListItem(LLUUID id);
	void refreshNetList();
	/*typedef enum e_net_info_mode
	{
		NI_NET,
		NI_LOG
	} ENetInfoMode;
	ENetInfoMode mNetInfoMode;*/

	LLEasyMessageSender mMessageSender;
};

#endif
