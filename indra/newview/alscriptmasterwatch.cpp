/**
 * @file alscriptmasterwatch.cpp
 * @brief The files on disk whose saves send linked scripts, watched.
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

#include "alscriptmasterwatch.h"

#include "alwatchedfile.h"
#include "llcallbacklist.h"
#include "lltimer.h"
#include "llviewercontrol.h"

namespace
{
    // How often a file is looked at: a master, the file a scripter saves,
    // soonest; an include a little less often; and everything less often
    // still where there is a great deal of it.
    constexpr F32    MASTER_PERIOD  = 0.5f;
    constexpr F32    INCLUDE_PERIOD = 1.0f;
    constexpr F32    MANY_PERIOD    = 2.0f;
    constexpr size_t MANY           = 256;
    // How long an emptied file is held before it is taken as empty, as an
    // external editor's emptied copy is (ALScriptExternalEditor::changed).
    constexpr F64    EMPTIED_HOLD   = 1.5;
}

ALScriptMasterWatch::ALScriptMasterWatch(released_t released)
: mReleased(std::move(released))
{
}

ALScriptMasterWatch::~ALScriptMasterWatch() = default;

void ALScriptMasterWatch::watch(const std::vector<ALMasterLinks::Watched>& files)
{
    static LLCachedControl<F32> quiet(gSavedSettings, "ALScriptMastersQuiet", 1.f);
    mBurst.setQuiet(llmax(0.f, (F32)quiet));
    const bool many = files.size() > MANY;
    boost::unordered_flat_map<std::string, Watching, ll::string_hash, std::equal_to<>> kept;
    for (const ALMasterLinks::Watched& one : files)
    {
        std::string key = ALMasterLinks::keyOf(one.path);
        if (kept.contains(key))
        {
            continue;
        }
        Watching watching;
        if (const auto found = mFiles.find(key); found != mFiles.end())
        {
            watching = std::move(found->second);
            mFiles.erase(found);
        }
        else
        {
            // Watched from as it is now: what is there already is seen.
            const std::weak_ptr<bool> alive = mAlive;
            watching.file                   = std::make_unique<ALWatchedFile>(one.path, [this, alive](const std::string& path) {
                if (alive.lock())
                {
                    heard(path);
                }
            });
        }
        const F32 period = many ? MANY_PERIOD : one.master ? MASTER_PERIOD : INCLUDE_PERIOD;
        if (watching.period != period)
        {
            watching.file->poll(period);
            watching.period = period;
        }
        watching.master = one.master;
        kept.emplace(std::move(key), std::move(watching));
    }
    // What is left is watched for nothing now, and nothing waits on it.
    for (const auto& [key, gone] : mFiles)
    {
        mBurst.forget(gone.file->path());
    }
    mFiles = std::move(kept);
}

void ALScriptMasterWatch::seen(const std::string& path)
{
    if (const auto found = mFiles.find(ALMasterLinks::keyOf(path)); found != mFiles.end())
    {
        found->second.file->seen();
        mBurst.forget(path);
    }
}

void ALScriptMasterWatch::heard(const std::string& path)
{
    const auto found = mFiles.find(ALMasterLinks::keyOf(path));
    if (found == mFiles.end())
    {
        return;
    }
    // What the look found, which the watcher already has: an emptied file
    // held until it has stayed empty a while.
    const ALWatchedFile& file    = *found->second.file;
    const bool           emptied = file.there() && file.stamp().size == 0;
    const F64            now     = LLTimer::getTotalSeconds();
    mBurst.changed(path, now, emptied ? EMPTIED_HOLD : 0.0);
    schedule();
}

void ALScriptMasterWatch::schedule()
{
    // A later change only puts the burst's time off, never forward: a look
    // already coming finds nothing yet, and comes again.
    const std::optional<F64> due = mBurst.dueAt();
    if (!due || mScheduled)
    {
        return;
    }
    mScheduled                      = true;
    const F64                 now   = LLTimer::getTotalSeconds();
    const std::weak_ptr<bool> alive = mAlive;
    doAfterInterval(
        [this, alive]() {
            if (alive.lock())
            {
                mScheduled = false;
                tick();
            }
        },
        (F32)llmax(0.05, *due - now));
}

void ALScriptMasterWatch::tick()
{
    std::vector<std::string> masters;
    std::vector<std::string> includes;
    const F64                now = LLTimer::getTotalSeconds();
    for (const std::string& path : mBurst.release(now))
    {
        if (const auto found = mFiles.find(ALMasterLinks::keyOf(path)); found != mFiles.end())
        {
            (found->second.master ? masters : includes).push_back(path);
        }
    }
    schedule();
    // Last: what is sent may change the links, and so what is watched.
    if (!masters.empty() || !includes.empty())
    {
        mReleased(masters, includes);
    }
}
