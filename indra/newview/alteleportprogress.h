/**
 * @file alteleportprogress.h
 * @brief ALTeleportProgress: the text the teleport screen shows for a TeleportProgress
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

#pragma once

#include <map>
#include <string>

struct ALTeleportProgress
{
    // The simulator's TeleportProgress names a step, such as "sending_dest", which the viewer words for itself in
    // teleport_strings.xml (LLAgent::sTeleportProgressMessages). The text shown is that wording, or the simulator's
    // own text where the viewer has none for it.
    static std::string text(const std::map<std::string, std::string>& messages, const std::string& sent)
    {
        const auto found = messages.find(sent);
        return found != messages.end() ? found->second : sent;
    }
};
