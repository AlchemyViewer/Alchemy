/** 
 * @file llmessageconfig.h
 * @brief Live file handling for messaging
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#ifndef LL_MESSAGECONFIG_H
#define LL_MESSAGECONFIG_H

#include <string>
#include "llsd.h"
#include "lllivefile.h"

static constexpr char messageConfigFileName[] = "message.xml";
static constexpr F32 messageConfigRefreshRate = 5.f; // seconds

static std::string sServerName;
static std::string sConfigDir;

static std::string sServerDefault;
static LLSD sMessages;

class LLSD;

class LLMessageConfig
{
public:
	enum Flavor { NO_FLAVOR=0, LLSD_FLAVOR=1, TEMPLATE_FLAVOR=2 };
	enum SenderTrust { NOT_SET=0, UNTRUSTED=1, TRUSTED=2 };
		
	static void initClass(const std::string& server_name,
						  const std::string& config_dir);
	static void useConfig(const LLSD& config);

	static Flavor getServerDefaultFlavor();
	 static S32 getMaxQueuedEvents();

	// For individual messages
	static Flavor getMessageFlavor(const std::string& msg_name);
	static SenderTrust getSenderTrustedness(const std::string& msg_name);
	static bool isValidMessage(const std::string& msg_name);
	static bool onlySendLatest(const std::string& msg_name);
	static bool isCapBanned(const std::string& cap_name);
	static LLSD getConfigForMessage(const std::string& msg_name);
};

class LLMessageConfigFile : public LLLiveFile
{
public:
	LLMessageConfigFile() :
		LLLiveFile(filename(), messageConfigRefreshRate),
		mMaxQueuedEvents(0)
            { }

	static std::string filename();

	LLSD mMessages;
	std::string mServerDefault;
	
	static LLMessageConfigFile& instance();
		// return the singleton configuration file

	/* virtual */ bool loadFile() override;
	void loadServerDefaults(const LLSD& data);
	void loadMaxQueuedEvents(const LLSD& data);
	void loadMessages(const LLSD& data);
	void loadCapBans(const LLSD& data);
	void loadMessageBans(const LLSD& data);
	bool isCapBanned(const std::string& cap_name) const;

public:
	LLSD mCapBans;
	S32 mMaxQueuedEvents;

private:
	static constexpr S32 DEFAULT_MAX_QUEUED_EVENTS = 100;
};
#endif // LL_MESSAGECONFIG_H
