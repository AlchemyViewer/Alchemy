/**
 * @file alscriptstudiodiskmasters.cpp
 * @brief The account's scripts mastered on disk, as a Script Studio window's masters ask of them.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptstudiodiskmasters.h"

#include "alscriptdiskmasters.h"

// static
ALScriptStudioDiskMasters& ALScriptStudioDiskMasters::get()
{
    static ALScriptStudioDiskMasters disk;
    return disk;
}

std::optional<ALMasterLink> ALScriptStudioDiskMasters::linkOf(const ALScriptRef& ref)
{
    return ALScriptDiskMasters::instance().linkOf(ref);
}

std::vector<ALMasterLink> ALScriptStudioDiskMasters::mastering(const std::string& master)
{
    return ALScriptDiskMasters::instance().mastering(master);
}

bool ALScriptStudioDiskMasters::masters(const std::string& master)
{
    return ALScriptDiskMasters::instance().masters(master);
}

void ALScriptStudioDiskMasters::link(ALMasterLink link)
{
    ALScriptDiskMasters::instance().link(std::move(link));
}

void ALScriptStudioDiskMasters::unlink(const ALScriptRef& ref)
{
    ALScriptDiskMasters::instance().unlink(ref);
}

void ALScriptStudioDiskMasters::send(const ALScriptRef& ref, ALMasterPlan::Send kind)
{
    ALScriptDiskMasters::instance().send(ref, kind);
}

void ALScriptStudioDiskMasters::wrote(const std::string& path)
{
    ALScriptDiskMasters::instance().wrote(path);
}

boost::signals2::connection ALScriptStudioDiskMasters::onOutcome(std::function<void(const Outcome& outcome)> heard)
{
    return ALScriptDiskMasters::instance().onOutcome(std::move(heard));
}

boost::signals2::connection ALScriptStudioDiskMasters::onChanged(std::function<void()> changed)
{
    return ALScriptDiskMasters::instance().onChanged(std::move(changed));
}

std::vector<ALScriptStudioDiskMasters::Outcome> ALScriptStudioDiskMasters::takeUnheard()
{
    return ALScriptDiskMasters::instance().takeUnheard();
}

ALDiskIncludes ALScriptStudioDiskMasters::blessedFor(const std::string& master, bool lua)
{
    return ALScriptDiskMasters::blessedFor(master, lua);
}

std::vector<std::pair<std::string, std::string>> ALScriptStudioDiskMasters::aliasesFor(const std::string& master, bool lua)
{
    return ALScriptDiskMasters::aliasesFor(master, lua);
}
