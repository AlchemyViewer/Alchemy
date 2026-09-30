/**
 * @file alscriptregionusage.cpp
 * @brief What the region reserves for the scripts of the objects in hand.
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

#include "alscriptregionusage.h"

namespace
{
    // The objects of a list of groups -- an answer's attachment points, or
    // its parcels -- each with the resources the region counts for it.
    void readObjects(const LLSD& groups, const LLDate& when, ALScriptRegionUsage::usages_t& out)
    {
        for (LLSD::array_const_iterator group = groups.beginArray(); group != groups.endArray(); ++group)
        {
            const LLSD& objects = (*group)["objects"];
            for (LLSD::array_const_iterator object = objects.beginArray(); object != objects.endArray(); ++object)
            {
                const LLUUID id = (*object)["id"].asUUID();
                if (id.isNull())
                {
                    continue;
                }
                const LLSD&                resources = (*object)["resources"];
                ALScriptRegionUsage::Usage usage;
                usage.memory = static_cast<S64>(resources["memory"].asReal());
                usage.urls   = resources["urls"].asInteger();
                usage.when   = when;
                out[id]      = usage;
            }
        }
    }
}

ALScriptRegionUsage::ALScriptRegionUsage(World world) : mWorld(std::move(world))
{
}

const ALScriptRegionUsage::Usage* ALScriptRegionUsage::usageOf(const LLUUID& root) const
{
    const auto found = mUsages.find(root);
    return found != mUsages.end() ? &found->second : nullptr;
}

void ALScriptRegionUsage::ask(const std::vector<LLUUID>& roots, F64 now)
{
    mNow                            = now;
    bool                attachments = false;
    std::vector<LLUUID> on_land;
    for (const LLUUID& root : roots)
    {
        const auto asked = mAsked.find(root);
        if (asked != mAsked.end() && now - asked->second < ASK_EVERY)
        {
            continue;
        }
        switch (mWorld.kindOf ? mWorld.kindOf(root) : World::Kind::None)
        {
            case World::Kind::Attachment:
                mAsked[root] = now;
                attachments  = true;
                break;
            case World::Kind::Land:
                mAsked[root] = now;
                on_land.push_back(root);
                break;
            default:
                break;
        }
    }
    const std::weak_ptr<bool> alive = mAlive;
    // One answer is every attachment's.
    if (attachments && now - mAttachmentsAsked >= ASK_EVERY && mWorld.askAttachments)
    {
        mAttachmentsAsked = now;
        mWorld.askAttachments([this, alive](const LLSD& answer) {
            if (alive.lock())
            {
                usages_t read;
                readAttachments(answer, LLDate::now(), read);
                heard(read);
            }
        });
    }
    for (const LLUUID& root : on_land)
    {
        if (!mWorld.askLand)
        {
            break;
        }
        mWorld.askLand(root, [this, alive](const LLSD& answer) {
            if (alive.lock())
            {
                usages_t read;
                readDetails(answer, LLDate::now(), read);
                heard(read);
            }
        });
    }
}

// static
void ALScriptRegionUsage::readAttachments(const LLSD& answer, const LLDate& when, usages_t& out)
{
    readObjects(answer["attachments"], when, out);
}

// static
void ALScriptRegionUsage::readDetails(const LLSD& answer, const LLDate& when, usages_t& out)
{
    readObjects(answer["parcels"], when, out);
}

void ALScriptRegionUsage::heard(const usages_t& usages)
{
    if (usages.empty())
    {
        return;
    }
    // Each object named is answered for as of the last asking, asked with
    // the others or not: a parcel's answer is every object on it.
    for (const auto& [id, usage] : usages)
    {
        mUsages[id]      = usage;
        const auto asked = mAsked.find(id);
        mAsked[id]       = asked != mAsked.end() ? std::max(asked->second, mNow) : mNow;
    }
    mHeard();
}
