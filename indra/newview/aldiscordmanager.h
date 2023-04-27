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

#include "llsingleton.h"
#include "llhost.h"

#include "discord.h"

class ALDiscordManager final
:	public LLSingleton<ALDiscordManager>
{
    LLSINGLETON(ALDiscordManager);
    ~ALDiscordManager();
    
public:
    bool initialized() { return mDiscord != nullptr; }
    void init();
    void shutdown();
    bool update(const LLSD&);

private:
    void onLoginCompleted();
    void onRegionChange();
    void updateActivity();

    boost::signals2::connection mIntegrationSettingConnection;
    boost::signals2::connection mRegionChangeConnection;
    std::unique_ptr<discord::Core> mDiscord;
    LLHost mCurrentHost;
    F64 mLoggedInTime = 0.0;
};


#endif // AL_DISCORDMANAGER_H