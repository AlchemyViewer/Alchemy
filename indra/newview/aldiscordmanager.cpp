/**
* @file aldiscordmanager.cpp
* @brief Alchemy Discord Integration
*
* $LicenseInfo:firstyear=2021&license=viewerlgpl$
* Alchemy Viewer Source Code
* Copyright (C) 2022, Alchemy Viewer Project.
* Copyright (C) 2022, Rye Mutt <rye@alchemyviewer.org>
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
* $/LicenseInfo$
*/

#include "llviewerprecompiledheaders.h"

#include "aldiscordmanager.h"

// library
#include "llevents.h"
#include "lltrans.h"

// newview
#include "llagent.h"
#include "llappviewer.h"
#include "llviewernetwork.h"
#include "llregioninfomodel.h"
#include "llviewerbuildconfig.h"
#include "llviewercontrol.h"
#include "llviewerregion.h"
#include "rlvactions.h"

#include "discord.h"

ALDiscordManager::ALDiscordManager()
{
	LLAppViewer::instance()->setOnLoginCompletedCallback(boost::bind(&ALDiscordManager::onLoginCompleted, this));

	gSavedPerAccountSettings.getControl("ALDiscordIntegration")->getSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val)
		{
			bool discord_enabled = new_val;
			if (discord_enabled)
			{
				init();
			}
			else
			{
				shutdown();
			}
		});

	if (gSavedPerAccountSettings.getBool("ALDiscordIntegration"))
	{
		init();
	}
}

ALDiscordManager::~ALDiscordManager()
{
	shutdown();
}

void ALDiscordManager::init()
{
	if (initialized()) return;

	discord::Core* core{};
	auto result = discord::Core::Create(DISCORD_CLIENTID, DiscordCreateFlags_NoRequireDiscord, &core);
	mDiscord.reset(core);
	if (!mDiscord) {
		LL_WARNS() << "Failed to instantiate discord core! (err " << static_cast<int>(result)
			<< ")" << LL_ENDL;
		return;
	}

	mDiscord->SetLogHook(
		discord::LogLevel::Info, [](discord::LogLevel level, const char* message)
		{
			switch (level)
			{
			case discord::LogLevel::Error:
			case discord::LogLevel::Warn:
			{
				LL_WARNS() << "Discord: " << message << LL_ENDL;
				break;
			}
			case discord::LogLevel::Info:
			{
				LL_INFOS() << "Discord: " << message << LL_ENDL;
				break;
			}
			case discord::LogLevel::Debug:
			{
				LL_DEBUGS() << "Discord: " << message << LL_ENDL;
				break;
			}
			}
		});

	discord::Activity activity{};
	activity.GetAssets().SetLargeImage("alchemy_1024");
	activity.GetAssets().SetLargeText("Alchemy Viewer");
	activity.SetType(discord::ActivityType::Playing);
	mDiscord->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
		//LL_INFOS() << ((result == discord::Result::Ok) ? "Succeeded" : "Failed")
		//          << " updating activity!" << LL_ENDL;
		});

	LLEventPumps::instance().obtain("mainloop").listen("ALDiscordManager", boost::bind(&ALDiscordManager::update, this, _1));
}

void ALDiscordManager::shutdown()
{
	LLEventPumps::instance().obtain("mainloop").stopListening("ALDiscordManager");
	mDiscord.reset();
}

bool ALDiscordManager::update(const LLSD&)
{
	if (mDiscord)
	{
		mDiscord->RunCallbacks();
	}
	static LLFrameTimer timer;
	if (timer.checkExpirationAndReset(5.f))
	{
		updateActivity();
	}

	return true;
}

void ALDiscordManager::onLoginCompleted()
{
	mLoggedInTime = LLDate::now().secondsSinceEpoch();
	updateActivity();
}

void ALDiscordManager::updateActivity()
{
	LLViewerRegion* region = gAgent.getRegion();
	if (!mDiscord || !region)
	{
		return;
	}

	discord::Activity activity{};
	activity.SetType(discord::ActivityType::Playing);

	static LLCachedControl<bool> discord_shared_region(gSavedPerAccountSettings, "ALDiscordShareLocationRegion", true);
	static LLCachedControl<U32> discord_shared_region_maturity(gSavedPerAccountSettings, "ALDiscordShareRegionMaxMaturity", true);
	std::string region_name;
	if (RlvActions::canShowLocation() && discord_shared_region && region->getSimAccess() > discord_shared_region_maturity)
	{
		const LLVector3& pos = gAgent.getPositionAgent();
		region_name = fmt::format(FMT_COMPILE("{} ({:.0f}, {:.0f}, {:.0f})"), region->getName(), pos.mV[VX], pos.mV[VY], pos.mV[VZ]);
	}
	else
	{
		region_name = "Hidden Region";
	}
	activity.SetState(region_name.c_str());

	static LLCachedControl<bool> discord_shared_name(gSavedPerAccountSettings, "ALDiscordShareName", true);
	if (RlvActions::canShowName(RlvActions::SNC_DEFAULT, gAgentID) && discord_shared_name)
	{
		std::string name;
		LLAvatarName av_name;
		if (LLAvatarNameCache::get(gAgentID, &av_name))
		{
			name = av_name.getCompleteName(true, true);
		}
		else
		{
			name = gAgentUsername;
		}
		activity.SetDetails(name.c_str());
	}

	if (mLoggedInTime > 0.0)
	{
		activity.GetTimestamps().SetStart(mLoggedInTime);
	}

	if (LLGridManager::getInstance()->isInSecondlife())
	{
		activity.GetAssets().SetLargeImage("secondlife_512");
	}
	else
	{
		activity.GetAssets().SetLargeImage("opensim_512");
	}

	activity.GetAssets().SetLargeText(LLGridManager::getInstance()->getGridLabel().c_str());
	activity.GetAssets().SetSmallImage("alchemy_1024");

	static std::string app_str = fmt::format("via {}", LLTrans::getString("APP_NAME"));
	activity.GetAssets().SetSmallText(app_str.c_str());

	std::string regionId = region->getRegionID().asString();
	activity.GetParty().GetSize().SetCurrentSize(region->mMapAvatars.size());
	S32 max_agents = LLRegionInfoModel::instance().mAgentLimit;
	if (max_agents > 0)
	{
		activity.GetParty().GetSize().SetMaxSize(max_agents);
	}
	else
	{
		LLMessageSystem* msg = gMessageSystem;
		msg->newMessageFast(_PREHASH_RequestRegionInfo);
		msg->nextBlockFast(_PREHASH_AgentData);
		msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
		msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
		gAgent.sendReliableMessage();
	}
	activity.GetParty().SetId(regionId.c_str());
	activity.GetParty().SetPrivacy(discord::ActivityPartyPrivacy::Public);

	mDiscord->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
		//LL_DEBUGS() << ((result == discord::Result::Ok) ? "Succeeded" : "Failed")
		//<< " updating activity!" << LL_ENDL;
		});
}
