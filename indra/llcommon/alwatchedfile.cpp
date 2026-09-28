/**
 * @file alwatchedfile.cpp
 * @brief A file watched for whoever changes it, to the nanosecond.
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

#include "alwatchedfile.h"

#include "alserialworker.h"
#include "fsyspath.h"
#include "llerror.h"
#include "lleventtimer.h"
#include "llfile.h"
#include "lltimer.h"
#include "workqueue.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <filesystem>
#include <vector>

#if !LL_WINDOWS
#include <sys/stat.h>
#endif

ALFileStamp ALFileStamp::of(const std::string& path)
{
    ALFileStamp out;
#if LL_WINDOWS
    // One call: the entry keeps what the system said of the file, and its
    // type, time and size are read from that.
    std::error_code                         ec;
    const std::filesystem::directory_entry entry(fsyspath(path), ec);
    if (ec || !entry.is_regular_file(ec) || ec)
    {
        return out;
    }
    const std::filesystem::file_time_type time = entry.last_write_time(ec);
    if (ec)
    {
        return out;
    }
    const std::uintmax_t size = entry.file_size(ec);
    if (ec)
    {
        return out;
    }
    out.time = static_cast<S64>(time.time_since_epoch().count());
    out.size = size;
#else
    llstat status;
    if (LLFile::stat(path, &status) != 0 || !S_ISREG(status.st_mode))
    {
        return out;
    }
#if LL_DARWIN
    const struct timespec& time = status.st_mtimespec;
#else
    const struct timespec& time = status.st_mtim;
#endif
    out.time = static_cast<S64>(time.tv_sec) * 1000000000LL + static_cast<S64>(time.tv_nsec);
    out.size = static_cast<std::uintmax_t>(status.st_size);
#endif
    out.exists = true;
    return out;
}

// The one watcher every polled file shares, for as long as one is polled.
// The main thread's timer picks the files whose look is due; the worker
// makes those looks, a batch at a time; and what they found comes back to
// the main thread, to each file still watched.
class ALWatchedFile::Watcher : public std::enable_shared_from_this<Watcher>
{
public:
    static std::shared_ptr<Watcher> get()
    {
        static std::weak_ptr<Watcher> one;
        std::shared_ptr<Watcher>      out = one.lock();
        if (!out)
        {
            out = std::make_shared<Watcher>();
            one = out;
        }
        return out;
    }

    Watcher() = default;
    ~Watcher()
    {
        delete mTimer;
        if (mWorker)
        {
            mWorker->close();
        }
    }
    Watcher(const Watcher&)            = delete;
    Watcher& operator=(const Watcher&) = delete;

    U64 add(ALWatchedFile& file, F32 period)
    {
        const F64 every = std::max(static_cast<F64>(period), static_cast<F64>(TICK));
        // Spread: files that begin together -- a session's tabs coming
        // back -- are not all looked at in the same tick.
        const F64 spread = every * static_cast<F64>(mAdded++ % SPREAD) / static_cast<F64>(SPREAD);
        const U64 id     = ++mNextId;
        mFiles[id]       = { &file, every, LLTimer::getTotalSeconds() + every + spread };
        if (!mTimer)
        {
            const std::weak_ptr<Watcher> weak = weak_from_this();
            mTimer = LLEventTimer::run_every(TICK, [weak]() {
                if (const std::shared_ptr<Watcher> self = weak.lock())
                {
                    self->tick();
                }
            });
        }
        return id;
    }

    void remove(U64 id) { mFiles.erase(id); }

private:
    // How often the timer asks which looks are due, and how many parts of
    // a period the files that begin together are spread over.
    static constexpr F32 TICK   = 0.1f;
    static constexpr U32 SPREAD = 8;

    struct File
    {
        ALWatchedFile* file = nullptr;
        F64            period = 0.0;
        F64            due    = 0.0;
    };
    struct Look
    {
        U64         id = 0;
        std::string path;
        U32         seenCount = 0;
        ALFileStamp stamp;
    };

    void tick()
    {
        // One batch at a time: a slow disk is not asked again while it has
        // yet to answer.
        if (mInFlight)
        {
            return;
        }
        const F64         now = LLTimer::getTotalSeconds();
        std::vector<Look> batch;
        for (auto& [id, file] : mFiles)
        {
            if (file.due > now)
            {
                continue;
            }
            // On the beat it began on, unless it has fallen a period behind.
            file.due += file.period;
            if (file.due <= now)
            {
                file.due = now + file.period;
            }
            batch.push_back({ id, file.file->mPath, file.file->mSeenCount, {} });
        }
        if (batch.empty())
        {
            return;
        }
        const LL::WorkQueue::ptr_t main = LL::WorkQueue::getInstance("mainloop");
        if (!mWorker)
        {
            mWorker = std::make_unique<ALSerialWorker>("WatchedFiles");
        }
        if (main)
        {
            const std::weak_ptr<Watcher> weak = weak_from_this();
            mInFlight                         = mWorker->post([batch, main, weak]() mutable {
                LL_PROFILE_ZONE_NAMED_CATEGORY_FILE("watched files looked at");
                for (Look& look : batch)
                {
                    look.stamp = ALFileStamp::of(look.path);
                }
                main->post([batch = std::move(batch), weak]() {
                    if (const std::shared_ptr<Watcher> self = weak.lock())
                    {
                        self->looked(batch);
                    }
                });
            });
            if (mInFlight)
            {
                return;
            }
        }
        // No main loop to hand them back to, or the worker is closing: the
        // looks are made here, as check() makes them.
        for (Look& look : batch)
        {
            look.stamp = ALFileStamp::of(look.path);
        }
        looked(batch);
    }

    void looked(const std::vector<Look>& batch)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_FILE;
        mInFlight = false;
        // Each found afresh: an owner told may let go of its file, or of
        // another, or of the last, as it hears.
        const std::shared_ptr<Watcher> keep = shared_from_this();
        for (const Look& look : batch)
        {
            const auto found = mFiles.find(look.id);
            if (found != mFiles.end() && found->second.file->mSeenCount == look.seenCount)
            {
                found->second.file->looked(look.stamp);
            }
        }
    }

    boost::unordered_flat_map<U64, File> mFiles;
    U64                                  mNextId   = 0;
    U32                                  mAdded    = 0;
    bool                                 mInFlight = false;
    LLEventTimer*                        mTimer    = nullptr;
    std::unique_ptr<ALSerialWorker>      mWorker;
};

ALWatchedFile::ALWatchedFile(std::string path, changed_t changed)
    : mPath(std::move(path)),
      mChanged(std::move(changed))
{
    mSeen   = ALFileStamp::of(mPath);
    mLooked = mSeen;
}

ALWatchedFile::~ALWatchedFile()
{
    if (mWatcher)
    {
        mWatcher->remove(mId);
    }
}

void ALWatchedFile::poll(F32 period)
{
    if (mWatcher)
    {
        mWatcher->remove(mId);
    }
    mWatcher = Watcher::get();
    mId      = mWatcher->add(*this, period);
}

bool ALWatchedFile::check()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_FILE;
    return looked(ALFileStamp::of(mPath));
}

bool ALWatchedFile::looked(const ALFileStamp& now)
{
    const bool still = now == mLooked;
    mLooked          = now;
    if (!still || now == mSeen)
    {
        return false;
    }
    mSeen = now;
    // Last, and through copies: the owner may let go of this as it hears.
    if (mChanged)
    {
        const changed_t   changed = mChanged;
        const std::string path    = mPath;
        changed(path);
    }
    return true;
}

void ALWatchedFile::seen()
{
    mSeen   = ALFileStamp::of(mPath);
    mLooked = mSeen;
    ++mSeenCount;
}
