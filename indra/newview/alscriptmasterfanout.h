/**
 * @file alscriptmasterfanout.h
 * @brief The linked scripts an include's save sends again.
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

#include "almasterlinks.h"
#include "alscripttypes.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <deque>
#include <memory>
#include <string>
#include <vector>

// The linked scripts a save of a file they include sends again: every link
// whose last expansion read it, and every one whose last expansion missed
// an include, which it may be (ALMasterLinks::affectedBy) -- persisted, so
// that scripts no tab holds are covered. Each is probed first, expanded as
// it would go up (ALScriptMasterUpload::probe), and those that would go up
// just as they went last are dropped. Then, where more would change than
// the scripter set (`ALScriptMastersAskOver`, 8), or they are in more than
// one object, the scripter is asked once (ALMasterPlan::askFirst): Send All,
// or Don't Send, which leaves them pending. Under that, each is sent as the
// studio's own sends are, which a change in the world holds, and said. A
// file saved while a round is under way is the next round's.
class ALScriptMasterFanOut
{
public:
    // Files saved that links' expansions read; `sent` the masters among
    // what was saved with them, whose scripts went up as a save of their
    // master already, the files beside them read afresh.
    void changed(const std::vector<std::string>& includes, const std::vector<std::string>& sent);

private:
    void start();
    void probeMore();
    void probed(const ALMasterLink& link, bool changing);
    void decide();
    void sendAll(bool send);

    // The round under way: what changed, the links to probe and being
    // probed, and those that would change.
    bool                      mBusy = false;
    std::vector<std::string>  mIncludes;
    std::deque<ALMasterLink>  mToProbe;
    size_t                    mProbing     = 0;
    bool                      mInProbeMore = false;
    std::vector<ALMasterLink> mChanging;
    // What changed while it was: the next round's.
    std::vector<std::string>  mNextIncludes;
    std::vector<std::string>  mNextSent;
    // Held while this is, for a probe or a question to know it still is.
    std::shared_ptr<bool>     mAlive = std::make_shared<bool>(true);
};
