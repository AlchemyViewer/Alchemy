/**
 * @file algamemode.cpp
 * @brief Support for FeralInteractive's GameMode
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Copyright (C) 2021, XenHat <me@xenh.at>
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
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "algamemode.h"

#include "llerror.h"
#include "llviewercontrol.h"

#include "gamemode_client.h"

#include <boost/signals2.hpp>

// static 
ALGameMode& ALGameMode::instance()
{ 
	static ALGameMode inst; 
	return inst;
}

void ALGameMode::init()
{
		enable(gSavedSettings.getBool("AlchemyGameModeEnable"));
		gSavedSettings.getControl("AlchemyGameModeEnable")->getCommitSignal()->connect(boost::bind(&ALGameMode::onToggleGameModeControl, this));
}

// static
void ALGameMode::shutdown()
{
	gamemode_request_end();
}

void ALGameMode::onToggleGameModeControl()
{
	enable(gSavedSettings.getBool("AlchemyGameModeEnable"));
}

void ALGameMode::enable(const bool enable)
{
	if (enable && getenv("DISABLE_GAMEMODE") != NULL)
	{
		LL_WARNS() << "The DISABLE_GAMEMODE environment variable has been set and therefore GameMode will not run." << LL_ENDL;
	}
	else if (mEnabled != enable)
	{
		mEnabled = enable;
		enable ? gamemode_request_start() : gamemode_request_end();
		if (gamemode_query_status() > 0)
		{
			LL_INFOS() << "GameMode enabled successfully" << LL_ENDL;
		}
		else
		{
			LL_INFOS() << "GameMode disabled." << LL_ENDL;
			std::string errstr = gamemode_error_string();
			if (errstr.length() > 0)
			{
				LL_WARNS() << "Gamemode returned the following error: '" << gamemode_error_string() << "'" << LL_ENDL;
			}
		}
	}
}
