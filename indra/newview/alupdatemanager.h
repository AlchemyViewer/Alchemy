/**
 * @file alupdatemanager.h
 * @brief Manager for updating!
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2024, Kyler "Felix" Eastridge.
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

#ifndef LL_ALUPDATEMANAGER_H
#define LL_ALUPDATEMANAGER_H

#include "llsingleton.h"
#include "llnotificationptr.h"

class ALUpdateManager final : public LLSingleton<ALUpdateManager>
{
    LLSINGLETON(ALUpdateManager);
public:
    ~ALUpdateManager();
    void showNotification();
    void handleUpdateData(const LLSD& content);
    static void launchRequest();
    void checkNow();
    void tryAutoCheck();

// Portion used for checking updates
    bool            mFirstCheck;
    bool            mSupressAuto;
    bool            mChecking;
    LLTimer         mLastChecked;

// Portion used if we have a update
    bool            mUpdateAvailable;
    bool            getUpdateAvailable(){ return mUpdateAvailable; }
    std::string     mUpdateVersion;
    std::string     mUpdateURL;
    std::string     mAddMessage; // Optional message regarding the update
    LLNotificationPtr mNotification;
};

#endif // LL_ALUPDATEMANAGER_H
