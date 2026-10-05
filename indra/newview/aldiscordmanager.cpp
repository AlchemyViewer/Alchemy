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

// The SDK's C++ wrapper is header-only; this is the one translation unit
// that compiles its definitions.
#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>

// library
#include "indra_constants.h"
#include "llavatarnamecache.h"
#include "llmath.h"
#include "lltrans.h"
#include "message.h"

// newview
#include "alagentlocationformat.h"
#include "llagent.h"
#include "llagentdata.h"
#include "llagentui.h"
#include "llappviewer.h"
#include "llregioninfomodel.h"
#include "llviewercontrol.h"
#include "llviewernetwork.h"
#include "llviewerregion.h"
#include "rlvactions.h"

#include <algorithm>
#include <chrono>
#include <string>

namespace
{
    // Alchemy's application on Discord, which holds the artwork named below.
    constexpr std::uint64_t APPLICATION_ID = 564763931009220608;

    constexpr char VIEWER_IMAGE[] = "alchemy_1024";
    constexpr char SECOND_LIFE_IMAGE[] = "secondlife_512";
    constexpr char OTHER_GRID_IMAGE[] = "opensim_512";
    constexpr char VIEWER_URL[] = "https://www.alchemyviewer.org";
    constexpr char SECOND_LIFE_URL[] = "https://secondlife.com";

    // The account's settings; settings_per_account_alchemy.xml declares them.
    constexpr char SETTING_INTEGRATION[] = "ALDiscordIntegration";
    constexpr char SETTING_SHARE_NAME[] = "ALDiscordShareName";
    constexpr char SETTING_SHARE_REGION[] = "ALDiscordShareLocationRegion";
    constexpr char SETTING_MAX_MATURITY[] = "ALDiscordShareRegionMaxMaturity";

    constexpr char FRAME_LISTENER[] = "ALDiscordManager";

    // How often the activity is rebuilt to follow the avatar about. Discord
    // drops updates sent faster than about one in four seconds.
    constexpr F32 REFRESH_SECONDS = 5.f;

    std::uint64_t milliseconds_since_epoch()
    {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }
}

ALDiscordManager::ALDiscordManager()
{
    mLoginConnection = LLAppViewer::instance()->setOnLoginCompletedCallback([this] { onLoginCompleted(); });

    mIntegrationConnection = gSavedPerAccountSettings.getControl(SETTING_INTEGRATION)->getSignal()->connect(
        [this](LLControlVariable*, const LLSD& enabled, const LLSD&)
        {
            // Before login this is the account's settings loading in; login
            // looks at the setting itself.
            if (!mLoginTime)
            {
                return;
            }
            if (enabled.asBoolean())
            {
                start();
            }
            else
            {
                stop();
            }
        });

    const auto refresh = [this](LLControlVariable*, const LLSD&, const LLSD&) { updateActivity(); };
    std::size_t index = 0;
    for (const char* setting : { SETTING_SHARE_NAME, SETTING_SHARE_REGION, SETTING_MAX_MATURITY })
    {
        mShareConnections.at(index++) = gSavedPerAccountSettings.getControl(setting)->getSignal()->connect(refresh);
    }
}

ALDiscordManager::~ALDiscordManager()
{
    stop();
}

void ALDiscordManager::start()
{
    if (started())
    {
        return;
    }

    // No Discord login: with only the application ID, the SDK sets the
    // presence through the Discord client on this machine.
    mClient = std::make_shared<discordpp::Client>();
    mClient->SetApplicationId(APPLICATION_ID);

    // False: the other mainloop listeners still get the frame.
    mFrameListener = LLEventPumps::instance().obtain("mainloop").listen(FRAME_LISTENER,
        [this](const LLSD&)
        {
            onFrame();
            return false;
        });
    mRegionConnection = gAgent.addRegionChangedCallback([this] { onRegionChanged(); });

    LL_INFOS("Discord") << "Rich presence started" << LL_ENDL;
    onRegionChanged();
    updateActivity();
}

void ALDiscordManager::stop()
{
    if (!started())
    {
        return;
    }

    mFrameListener.disconnect();
    mRegionConnection.disconnect();

    mClient->ClearRichPresence();
    mClient.reset();
    mLastActivity.reset();
    mRegionHost.invalidate();
    LL_INFOS("Discord") << "Rich presence stopped" << LL_ENDL;
}

void ALDiscordManager::onFrame()
{
    LL_PROFILE_ZONE_SCOPED;

    discordpp::RunCallbacks();
    if (mRefreshTimer.checkExpirationAndReset(REFRESH_SECONDS))
    {
        updateActivity();
    }
}

void ALDiscordManager::onLoginCompleted()
{
    mLoginTime = milliseconds_since_epoch();
    if (gSavedPerAccountSettings.getBOOL(SETTING_INTEGRATION))
    {
        start();
    }
}

void ALDiscordManager::onRegionChanged()
{
    if (!started() || !gAgent.getRegion() || gAgent.getRegionHost() == mRegionHost)
    {
        return;
    }
    mRegionHost = gAgent.getRegionHost();

    // The party's size is the region's agent limit, which comes in the
    // region's info; the viewer asks for that only when the region floater
    // opens. Anyone may ask for it.
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_RequestRegionInfo);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    gAgent.sendReliableMessage();

    updateActivity();
}

void ALDiscordManager::updateActivity()
{
    if (!started())
    {
        return;
    }

    discordpp::Activity activity = buildActivity();
    if (mLastActivity && mLastActivity->Equals(activity))
    {
        return;
    }
    mLastActivity = std::make_unique<discordpp::Activity>(activity);

    mClient->UpdateRichPresence(std::move(activity),
        [this](const discordpp::ClientResult& result)
        {
            if (!result.Successful())
            {
                // Most often there is no Discord client running yet: send the
                // activity again next time rather than hold it as sent.
                LL_DEBUGS("Discord") << "Rich presence not updated: " << result.Error() << LL_ENDL;
                mLastActivity.reset();
            }
        });
}

discordpp::Activity ALDiscordManager::buildActivity() const
{
    discordpp::Activity activity;
    activity.SetType(discordpp::ActivityTypes::Playing);

    discordpp::ActivityAssets assets;
    const LLViewerRegion* region = gAgent.getRegion();
    if (!region)
    {
        // Between regions: the viewer alone.
        assets.SetLargeImage(VIEWER_IMAGE);
        assets.SetLargeText(LLTrans::getString("APP_NAME"));
        assets.SetLargeUrl(VIEWER_URL);
        activity.SetAssets(assets);
        return activity;
    }

    // In world: the grid, with the viewer in its corner.
    LLGridManager& grids = LLGridManager::instance();
    if (grids.isSystemGrid())
    {
        assets.SetLargeImage(SECOND_LIFE_IMAGE);
        assets.SetLargeUrl(SECOND_LIFE_URL);
    }
    else
    {
        assets.SetLargeImage(OTHER_GRID_IMAGE);
    }
    assets.SetLargeText(grids.getGridLabel());
    assets.SetSmallImage(VIEWER_IMAGE);
    assets.SetSmallText(LLTrans::getString("DiscordViaViewer"));
    assets.SetSmallUrl(VIEWER_URL);
    activity.SetAssets(assets);

    if (mLoginTime)
    {
        discordpp::ActivityTimestamps timestamps;
        timestamps.SetStart(*mLoginTime);
        activity.SetTimestamps(timestamps);
    }

    static LLCachedControl<bool> share_name(gSavedPerAccountSettings, SETTING_SHARE_NAME, false);
    if (share_name && RlvActions::canShowName(RlvActions::SNC_DEFAULT, gAgentID))
    {
        LLAvatarName av_name;
        activity.SetDetails(LLAvatarNameCache::get(gAgentID, &av_name) ? av_name.getCompleteName(true, true)
                                                                        : gAgentUsername);
    }

    static LLCachedControl<bool> share_region(gSavedPerAccountSettings, SETTING_SHARE_REGION, false);
    static LLCachedControl<U32> max_maturity(gSavedPerAccountSettings, SETTING_MAX_MATURITY, SIM_ACCESS_PG);
    if (!share_region || !RlvActions::canShowLocation() || region->getSimAccess() > max_maturity)
    {
        // A hidden region gives nothing away, the party included: its ID is
        // the region's.
        activity.SetState(LLTrans::getString("DiscordHiddenRegion"));
        return activity;
    }

    // The region and position as the viewer's location readouts write them,
    // leaving out the parcel's name.
    const LLVector3& pos = gAgent.getPositionAgent();
    std::string state;
    ALAgentLocationFormat::format(state, LLAgentUI::LOCATION_FORMAT_NORMAL_COORDS, {}, region->getName(), {},
                                  ll_round(pos.mV[VX]), ll_round(pos.mV[VY]), ll_round(pos.mV[VZ]));
    activity.SetState(std::move(state));

    // The region's crowd out of its agent limit. The limit is the last region
    // info's, so it counts only once that info is this region's. A crowd past
    // the limit, which estate managers can make, raises it: Discord refuses a
    // party larger than its size.
    const auto crowd = std::max<std::int32_t>(1, static_cast<std::int32_t>(region->mMapAvatars.size()));
    const LLRegionInfoModel& info = LLRegionInfoModel::instance();
    const std::int32_t limit = info.mSimName == region->getName() ? info.mAgentLimit : 0;

    discordpp::ActivityParty party;
    party.SetId(region->getRegionID().asString());
    party.SetCurrentSize(crowd);
    party.SetMaxSize(std::max(crowd, limit));
    party.SetPrivacy(discordpp::ActivityPartyPrivacy::Public);
    activity.SetParty(party);

    return activity;
}
