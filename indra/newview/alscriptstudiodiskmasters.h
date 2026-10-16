/**
 * @file alscriptstudiodiskmasters.h
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

#pragma once

#include "alscriptstudiomasters.h"

// What a Script Studio window's masters ask of the account's scripts
// mastered on disk, each ask passed on to ALScriptDiskMasters -- the one
// the account in hand has -- as it is asked. It holds nothing of its own,
// so one serves every window (get()). A test gives the masters its own
// in its place, and so needs none of the viewer behind this.
class ALScriptStudioDiskMasters final : public ALScriptStudioMasters::DiskMasters
{
public:
    static ALScriptStudioDiskMasters& get();

    std::optional<ALMasterLink>                      linkOf(const ALScriptRef& ref) override;
    std::vector<ALMasterLink>                        mastering(const std::string& master) override;
    bool                                             masters(const std::string& master) override;
    void                                             link(ALMasterLink link) override;
    void                                             unlink(const ALScriptRef& ref) override;
    void                                             send(const ALScriptRef& ref, ALMasterPlan::Send kind) override;
    void                                             wrote(const std::string& path) override;
    boost::signals2::connection                      onOutcome(std::function<void(const Outcome& outcome)> heard) override;
    boost::signals2::connection                      onChanged(std::function<void()> changed) override;
    std::vector<Outcome>                             takeUnheard() override;
    ALDiskIncludes                                   blessedFor(const std::string& master, bool lua) override;
    std::vector<std::pair<std::string, std::string>> aliasesFor(const std::string& master, bool lua) override;
};
