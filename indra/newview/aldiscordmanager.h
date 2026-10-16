/**
* @file aldiscordmanager.h
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

#ifndef AL_DISCORDMANAGER_H
#define AL_DISCORDMANAGER_H

#include "llevents.h"
#include "llframetimer.h"
#include "llhost.h"
#include "llsingleton.h"

#include <boost/signals2/connection.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

// The SDK's header is half a megabyte of declarations; only this class's own
// source needs it.
namespace discordpp
{
    class Activity;
    class Client;
}

// Rich presence through the Discord Social SDK: the grid, and the region and
// avatar name the user chose to share, with the region's crowd as the party.
// There is no Discord login; the SDK reaches the Discord client running on
// this machine, and shows nothing when there is none.
//
// Every setting is the account's, so the client starts at login and stops
// when the account turns it off. LLAppViewer makes the instance at init and
// deletes it at cleanup.
class ALDiscordManager final : public LLSingleton<ALDiscordManager>
{
    LLSINGLETON(ALDiscordManager);
    ~ALDiscordManager() override;

private:
    void start();
    void stop();
    [[nodiscard]] bool started() const noexcept { return mClient != nullptr; }

    void onFrame();
    void onLoginCompleted();
    void onRegionChanged();

    // Sends the activity when it differs from what Discord last accepted.
    void updateActivity();
    [[nodiscard]] discordpp::Activity buildActivity() const;

    std::shared_ptr<discordpp::Client> mClient;
    // What Discord was last sent, so an unchanged activity is not sent
    // again: Discord rate-limits presence updates.
    std::unique_ptr<discordpp::Activity> mLastActivity;

    LLTempBoundListener mFrameListener;
    boost::signals2::scoped_connection mLoginConnection;
    boost::signals2::scoped_connection mRegionConnection;
    boost::signals2::scoped_connection mIntegrationConnection;
    std::array<boost::signals2::scoped_connection, 3> mShareConnections;

    LLFrameTimer mRefreshTimer;
    // The region whose info was asked for, so a crossing asks once.
    LLHost mRegionHost;
    // Milliseconds since the epoch; empty before login.
    std::optional<std::uint64_t> mLoginTime;
};

#endif // AL_DISCORDMANAGER_H
