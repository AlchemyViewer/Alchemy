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

#include "alupdatemanager.h"
#include "llversioninfo.h"
#include "llviewercontrol.h"
#include "llcorehttputil.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llnotificationmanager.h"
#include "llnotifications.h"
#include "llnotificationsutil.h"
#include "llappviewer.h" // For gDisconnected
#include "llweb.h"
#include "llstartup.h"

/*
Current LLSD data template:

<llsd>
  <map>
    <!-- Put all entries in a sub map called "channels", in case we want to
         include something else shared between all channels. -->
    <key>channels</key>
    <map>
      <key>Alchemy Test</key>
      <map>
        <!-- Build ID, viewer checks if this is newer -->
        <key>build</key>
        <integer>123456</integer>

        <!-- Full version number -->
        <key>version</key>
        <string>1.2.3.45678</string>

        <!-- A URL to the download page -->
        <key>download</key>
        <string>Page to open in browser when user clicks "download"</string>

        <!-- A optional message -->
        <key>message</key>
        <string>Coyito was here!</string>
      </map>
    </map>
  </map>
</llsd>
*/

ALUpdateManager::ALUpdateManager() :
    mChecking(false),
    mSupressAuto(false),
    mFirstCheck(true),
    mUpdateAvailable(false),
    mUpdateVersion(""),
    mUpdateURL(""),
    mAddMessage("")
{
}

ALUpdateManager::~ALUpdateManager()
{
}

void ALUpdateManager::showNotification()
{
    // If we already have one, don't grief the user.
    if (mNotification && mNotification->isActive())
        return;

    LLSD args;
    args["CHANNEL"] = LLVersionInfo::instance().getChannel();
    args["VERSION"] = mUpdateVersion;
    args["MYVERSION"] = LLVersionInfo::instance().getVersion();
    args["URL"] = mUpdateURL;
    args["MESSAGE"] = mAddMessage;
    if (LLStartUp::getStartupState() < STATE_LOGIN_CLEANUP)
        mNotification = LLNotificationsUtil::add("AlchemyUpdateAlert", args);
    else
        mNotification = LLNotificationsUtil::add("AlchemyUpdateToast", args);
}

void ALUpdateManager::handleUpdateData(const LLSD& content)
{
    // Check if we have a channel in the list
    std::string channel = LLVersionInfo::instance().getChannel();
    if (!content.has(channel))
        return;

    // Make sure we have a build number
    if (!content[channel].has("build"))
        return;

    // Make sure the build number is newer than the one we have
    int build = LLVersionInfo::instance().getBuild();
    if (content[channel]["build"].asInteger() <= build)
        return;

    //Make sure we have all the remaining fields
    if (!content[channel].has("version"))
        return;

    if (!content[channel].has("download"))
        return;

    LL_INFOS("ALUpdateManager") << "Update detected! 🥳" << LL_ENDL;

    // And now only, and only if we got this far, update the variables
    mUpdateVersion = content[channel]["version"].asString();
    mUpdateURL = content[channel]["download"].asString();

    if (content[channel].has("message"))
        mAddMessage = "\n\n" + content[channel]["message"].asString();
    else
        mAddMessage = "";

    // And mark that we have a update ready!
    mUpdateAvailable = true;

    showNotification();
}

void ALUpdateManager::launchRequest()
{
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter(new LLCoreHttpUtil::HttpCoroutineAdapter("ALUpdateManager", httpPolicy));
    LLCore::HttpRequest::ptr_t httpRequest(new LLCore::HttpRequest);
    LLCore::HttpOptions::ptr_t httpOpts(new LLCore::HttpOptions);

    // If we change the URL
    httpOpts->setFollowRedirects(true);

    // Also try again if we fail the first time, just in case
    httpOpts->setRetries(1);

    static LLCachedControl<std::string> AlchemyUpdateURL(gSavedSettings, "AlchemyUpdateURL");
    LL_INFOS("ALUpdateManager") << "Launching update check to " << AlchemyUpdateURL() << LL_ENDL;
    LLSD result = httpAdapter->getRawAndSuspend(httpRequest, AlchemyUpdateURL);


    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    // Mark we aren't checking anymore first and reset the timer, just in case
    // we end up bailing due to HTTP failure
    ALUpdateManager::getInstance()->mChecking = false;
    ALUpdateManager::getInstance()->mLastChecked.reset();

    if (!status)
    {
        LL_WARNS("ALUpdateManager") << "HTTP status, " << status.toTerseString() <<
            ". Update check failed." << LL_ENDL;
        return;
    }

    const LLSD::Binary &rawBody = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
    std::string body(rawBody.begin(), rawBody.end());
    std::istringstream is(body);

    LLPointer<LLSDParser> parser = new LLSDXMLParser();
    LLSD data;
    if(parser->parse(is, data, body.size()) == LLSDParser::PARSE_FAILURE)
    {
        LL_WARNS("ALUpdateManager") << "Failed to parse update info: " << ll_stream_notation_sd(result) << LL_ENDL;
        return;
    }

    LL_INFOS("ALUpdateManager") << "Got response: " << ll_stream_notation_sd(data) << LL_ENDL;

    if (data.has("channels"))
        ALUpdateManager::getInstance()->handleUpdateData(data["channels"]);
    //Handle anything else here
}

void ALUpdateManager::checkNow()
{
    // No need to check when we are disconnected
    if(gDisconnected)
        return;

    // Bail out if we are already checking
    if (mChecking)
        return;

    mChecking = true;

    LL_INFOS("ALUpdateManager") << "Checking for update..." << LL_ENDL;
    LLCoros::instance().launch("ALUpdateManager::LaunchRequest",
        boost::bind(ALUpdateManager::launchRequest));
}

void ALUpdateManager::tryAutoCheck()
{
    if (mSupressAuto)
        return;

    // Don't check if the user has requested to disable it
    static LLCachedControl<bool> AlchemyUpdateEnableAutoCheck(gSavedSettings, "AlchemyUpdateEnableAutoCheck");
    if (!AlchemyUpdateEnableAutoCheck)
        return;

    static LLCachedControl<F32> AlchemyUpdateCheckFrequency(gSavedSettings, "AlchemyUpdateCheckFrequency");
    if ((mLastChecked.getElapsedTimeF32() < AlchemyUpdateCheckFrequency || AlchemyUpdateCheckFrequency == .0f)
        && !mFirstCheck)
        return;

    mFirstCheck = false;

    LL_INFOS("ALUpdateManager") << "Attempting automatic update check..." << LL_ENDL;
    checkNow();
}

static bool AlchemyUpdateToastlert(const LLSD& notification, const LLSD& response)
{
    std::string option = LLNotification::getSelectedOptionName(response);

    if (option == "DOWNLOAD")
    {
        LLWeb::loadURLExternal(ALUpdateManager::getInstance()->mUpdateURL);
    }
    else if (option == "CLOSE")
    {
        // This is the remind me later option
        // We don't really do anything here, instead we just wait for the next
        // tryAutoCheck.
    }
    else if (option == "SUPPRESS")
    {
        ALUpdateManager::getInstance()->mSupressAuto = true;
    }
    return false;
}
static LLNotificationFunctorRegistration AlchemyUpdateAlert_reg("AlchemyUpdateAlert", AlchemyUpdateToastlert);
static LLNotificationFunctorRegistration AlchemyUpdateToast_reg("AlchemyUpdateToast", AlchemyUpdateToastlert);
