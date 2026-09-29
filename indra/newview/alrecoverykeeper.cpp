/**
 * @file alrecoverykeeper.cpp
 * @brief One window's unsaved text kept in the account's recovery store, as Script Studio keeps its tabs'.
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

#include "alrecoverykeeper.h"

#include "alrecovery.h"

ALRecoveryKeeper::ALRecoveryKeeper(Holder holder) : mHolder(std::move(holder)) {}

void ALRecoveryKeeper::setItem(const LLUUID& object, const LLUUID& item)
{
    const std::string key = item.isNull() ? std::string() : ALRecoveryStore::windowKeyOf(object, item);
    if (key == mKey)
    {
        return;
    }
    // Kept under the old key, where anything was: under the new from here.
    forget();
    mKey = key;
    mSeen.reset();
}

ALRecoveryKeeper::Entry ALRecoveryKeeper::entryNow(Entry::State state) const
{
    Entry entry = mHolder.entry ? mHolder.entry() : Entry();
    entry.key   = mKey;
    entry.state = state;
    return entry;
}

void ALRecoveryKeeper::pump(U64 version, F64 now)
{
    ALRecoveryStore* kept = ALRecovery::store();
    if (!kept || mKey.empty())
    {
        return;
    }
    // A write that failed on the store's thread: said once, and written
    // again with the next change.
    if (mWritten && kept->takeFailure(mKey))
    {
        mWritten.reset();
        if (!mFailed)
        {
            mFailed = true;
            if (mHolder.failed)
            {
                mHolder.failed();
            }
        }
    }
    if (!mHolder.unsaved || !mHolder.unsaved())
    {
        forget();
        return;
    }
    if (!mSeen || *mSeen != version)
    {
        mSeen = version;
        if (mDue <= 0.0)
        {
            mDue = now + DELAY;
        }
    }
    if (mDue > 0.0 && now >= mDue)
    {
        mDue = 0.0;
        if (!mWritten || *mWritten != version)
        {
            kept->writeSoon(entryNow(Entry::State::Unsaved));
            mWritten = version;
        }
    }
}

void ALRecoveryKeeper::forget()
{
    mDue = 0.0;
    if (!mWritten)
    {
        return;
    }
    if (ALRecoveryStore* kept = ALRecovery::store(); kept && !mKey.empty())
    {
        kept->forget(mKey);
    }
    mWritten.reset();
    mFailed = false;
}

bool ALRecoveryKeeper::setAside()
{
    ALRecoveryStore* kept = ALRecovery::store();
    if (!kept || mKey.empty())
    {
        return false;
    }
    const bool aside = kept->setAside(entryNow(Entry::State::Discarded));
    // Its own entry goes as well, whether or not one was written: what is
    // set aside stands for it.
    kept->forget(mKey);
    mWritten.reset();
    mDue = 0.0;
    return aside;
}

std::optional<ALRecoveryKeeper::Entry> ALRecoveryKeeper::left() const
{
    const ALRecoveryStore* kept = ALRecovery::store();
    if (!kept || mKey.empty())
    {
        return std::nullopt;
    }
    // Whole, text and all, as the window takes it up; where it cannot be
    // read, left where it is for Recover Unsaved Changes to show.
    std::optional<Entry> left = kept->leftFor(mKey);
    if (left && !left->whole && !kept->load(*left))
    {
        return std::nullopt;
    }
    return left;
}

void ALRecoveryKeeper::took(const Entry& taken, U64 version)
{
    ALRecoveryStore* kept = ALRecovery::store();
    if (!kept || mKey.empty())
    {
        return;
    }
    // Its own first, forced out to the disk, so that there is never a
    // moment with neither.
    if (kept->write(entryNow(Entry::State::Unsaved)))
    {
        mWritten = version;
        mSeen    = version;
        kept->remove(taken);
    }
}

void ALRecoveryKeeper::turnDown(const Entry& offered)
{
    if (ALRecoveryStore* kept = ALRecovery::store())
    {
        kept->discard(offered);
    }
}
