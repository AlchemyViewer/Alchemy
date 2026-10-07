/**
 * @file alscriptmasteradopt.cpp
 * @brief Links just made, probed for what a send through them would know.
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

#include "alscriptmasteradopt.h"

#include "alscriptdiskmasters.h"
#include "alscriptmasterupload.h"

namespace
{
    // How many probes at a time: each reads its master, expands it, and
    // reads the world's text.
    constexpr size_t PROBES = 4;
}

// static
bool ALScriptMasterAdopt::knowsNothing(const ALMasterLink& link)
{
    return link.hash.empty() && link.uses.empty() && !link.missed;
}

void ALScriptMasterAdopt::adopt(const std::vector<ALMasterLink>& made)
{
    for (const ALMasterLink& link : made)
    {
        mToProbe.push_back(link);
    }
    probeMore();
}

void ALScriptMasterAdopt::probeMore()
{
    // A probe may answer as it is asked, its master gone say: the next taken
    // by the loop, not by a call inside it.
    if (mInProbeMore)
    {
        return;
    }
    mInProbeMore = true;
    while (mProbing < PROBES && !mToProbe.empty())
    {
        ALMasterLink link = std::move(mToProbe.front());
        mToProbe.pop_front();
        ++mProbing;
        const std::weak_ptr<bool> alive = mAlive;
        ALScriptMasterUpload::probe(
            link,
            [this, alive, link](const ALScriptMasterUpload::Probe& found) {
                if (!alive.lock())
                {
                    return;
                }
                --mProbing;
                // What went up last only where the world holds what the
                // file makes now: otherwise the link's first send is a real
                // one, and its hash what that send makes.
                const bool        held  = found.what == ALScriptDiskMasters::Outcome::What::Sent && found.worldSame;
                const std::string hash  = held ? found.ours : std::string();
                const S64         stamp = held ? found.stamp : 0;
                ALScriptDiskMasters::instance().adopted(ALScriptRef(link.object, link.item), link.master, found.uses, found.missed, hash, stamp);
                probeMore();
            },
            /*world*/ true);
    }
    mInProbeMore = false;
}
