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

#include "fsyspath.h"
#include "lleventtimer.h"

class ALWatchedFile::Poll final : public LLEventTimer
{
public:
    Poll(ALWatchedFile& file, F32 period)
        : LLEventTimer(period),
          mFile(file)
    {
    }

    bool tick() override
    {
        // Nothing of this is touched after: the owner may let the file go
        // as it is told of the change, and this with it.
        mFile.check();
        return false;
    }

private:
    ALWatchedFile& mFile;
};

ALWatchedFile::ALWatchedFile(std::string path, changed_t changed)
    : mPath(std::move(path)),
      mChanged(std::move(changed))
{
    mSeen   = stamp();
    mLooked = mSeen;
}

ALWatchedFile::~ALWatchedFile() = default;

void ALWatchedFile::poll(F32 period)
{
    mPoll = std::make_unique<Poll>(*this, period);
}

bool ALWatchedFile::check()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    const Stamp now = stamp();
    const bool  still = now == mLooked;
    mLooked           = now;
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
    mSeen   = stamp();
    mLooked = mSeen;
}

ALWatchedFile::Stamp ALWatchedFile::stamp() const
{
    Stamp                       out;
    std::error_code             ec;
    const std::filesystem::path file = fsyspath(mPath);
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, ec);
    if (ec)
    {
        return out;
    }
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec)
    {
        return out;
    }
    out.exists = true;
    out.time   = time;
    out.size   = size;
    return out;
}
