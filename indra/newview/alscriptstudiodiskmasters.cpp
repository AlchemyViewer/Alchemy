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

#include "alscriptstudiomasters.h"

#include "alscriptdiskmasters.h"

namespace
{
    // The viewer's: each ask passed on to ALScriptDiskMasters, the one the
    // account in hand has, as it is asked. Holds nothing of its own, so
    // that one serves every window.
    class ALScriptStudioDiskMasters final : public ALScriptStudioMasters::DiskMasters
    {
    public:
        std::optional<ALMasterLink> linkOf(const ALScriptRef& ref) override { return ALScriptDiskMasters::instance().linkOf(ref); }
        std::vector<ALMasterLink>   mastering(const std::string& master) override { return ALScriptDiskMasters::instance().mastering(master); }
        bool                        masters(const std::string& master) override { return ALScriptDiskMasters::instance().masters(master); }
        void link(ALMasterLink link) override { ALScriptDiskMasters::instance().link(std::move(link)); }
        void unlink(const ALScriptRef& ref) override { ALScriptDiskMasters::instance().unlink(ref); }
        void send(const ALScriptRef& ref, ALMasterPlan::Send kind) override { ALScriptDiskMasters::instance().send(ref, kind); }
        void wrote(const std::string& path) override { ALScriptDiskMasters::instance().wrote(path); }
        boost::signals2::connection onOutcome(std::function<void(const Outcome& outcome)> heard) override
        {
            return ALScriptDiskMasters::instance().onOutcome(std::move(heard));
        }
        boost::signals2::connection onChanged(std::function<void()> changed) override
        {
            return ALScriptDiskMasters::instance().onChanged(std::move(changed));
        }
        std::vector<Outcome> takeUnheard() override { return ALScriptDiskMasters::instance().takeUnheard(); }
        ALDiskIncludes       blessedFor(const std::string& master, bool lua) override { return ALScriptDiskMasters::blessedFor(master, lua); }
        std::vector<std::pair<std::string, std::string>> aliasesFor(const std::string& master, bool lua) override
        {
            return ALScriptDiskMasters::aliasesFor(master, lua);
        }
    };
}

// static
ALScriptStudioMasters::DiskMasters& ALScriptStudioMasters::viewer()
{
    static ALScriptStudioDiskMasters disk;
    return disk;
}
