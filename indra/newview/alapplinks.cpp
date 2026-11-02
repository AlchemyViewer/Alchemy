/**
 * @file alapplinks.cpp
 * @brief What the viewer's app link handlers read from a link's parts
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#include "linden_common.h"

#include "alapplinks.h"

#include "llstring.h"
#include "lluri.h"

#include <algorithm>

//static
std::string ALAppLinks::floaterName(const LLSD& params)
{
    return LLURI::unescape(params[0].asString());
}

//static
bool ALAppLinks::floaterNameIn(const LLSD& params, const std::list<std::string>& floater_names)
{
    return std::find(floater_names.begin(), floater_names.end(), floaterName(params)) != floater_names.end();
}

//static
std::optional<ALAppLinks::ObjectMute> ALAppLinks::objectMute(const LLSD& params)
{
    const std::string verb = params[1].asString();
    if ((verb != "block" && verb != "unblock") || params.size() <= 2)
    {
        return std::nullopt;
    }
    return ObjectMute{ verb == "block", LLURI::unescape(params[2].asString()) };
}

//static
LLVector3 ALAppLinks::teleportPosition(const LLSD& tokens)
{
    LLVector3 coords(128, 128, 0);
    if (tokens.size() <= 4)
    {
        for (LLSD::Integer axis = 0; axis < 3 && axis + 1 < static_cast<LLSD::Integer>(tokens.size()); ++axis)
        {
            coords.mV[axis] = (F32)tokens[axis + 1].asReal();
        }
    }
    return coords;
}

//static
bool ALAppLinks::queryFlag(const LLSD& value)
{
    bool flag = false;
    if (value.isString() && LLStringUtil::convertToBOOL(value.asString(), flag))
    {
        return flag;
    }
    return value.asBoolean();
}
