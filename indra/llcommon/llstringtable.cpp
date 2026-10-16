/**
 * @file llstringtable.cpp
 * @brief The LLStringTable class provides a _fast_ method for finding
 * unique copies of strings.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llstringtable.h"

#include <mutex>

// Add-only: nothing calls removeString on it, so it skips the refcount
// traffic that would otherwise land on every tag and attribute parsed.
LLStringTable gStringTable(32768, LLStringTable::NOT_COUNTED);

LLStringTableEntry::LLStringTableEntry(std::string_view str)
:   mText(str),
    mCount(1),
    mString(mText.data())
{
}

LLStringTable::LLStringTable(int tablesize, ERefCounting counting)
:   mCounted(counting == COUNTED)
{
    if (tablesize > 0)
    {
        mTable.reserve(static_cast<std::size_t>(tablesize));
    }
}

LLStringTable::~LLStringTable() = default;

S32 LLStringTable::getUniqueEntries() const
{
    std::shared_lock<std::shared_mutex> lock(mMutex);
    return static_cast<S32>(mTable.size());
}

LLStringTableEntry* LLStringTable::checkStringEntry(std::string_view str) const
{
    std::shared_lock<std::shared_mutex> lock(mMutex);
    table_t::const_iterator found = mTable.find(str);
    return found == mTable.end() ? nullptr : found->second.get();
}

LLStringTableEntry* LLStringTable::checkStringEntry(const char* str) const
{
    return str ? checkStringEntry(std::string_view(str)) : nullptr;
}

char* LLStringTable::checkString(std::string_view str) const
{
    LLStringTableEntry* entry = checkStringEntry(str);
    return entry ? entry->mString : nullptr;
}

char* LLStringTable::checkString(const char* str) const
{
    return str ? checkString(std::string_view(str)) : nullptr;
}

LLStringTableEntry* LLStringTable::addStringEntry(std::string_view str)
{
    // Already present is the common case by a wide margin -- the same handful
    // of tag and attribute names recur across every file parsed -- so look
    // first under a shared lock and only take the table exclusively to insert.
    {
        std::shared_lock<std::shared_mutex> lock(mMutex);
        table_t::const_iterator found = mTable.find(str);
        if (found != mTable.end())
        {
            if (mCounted)
            {
                found->second->incCount();
            }
            return found->second.get();
        }
    }

    std::unique_lock<std::shared_mutex> lock(mMutex);

    // Another thread may have inserted it while the lock was not held.
    table_t::const_iterator found = mTable.find(str);
    if (found != mTable.end())
    {
        if (mCounted)
        {
            found->second->incCount();
        }
        return found->second.get();
    }

    std::unique_ptr<LLStringTableEntry> owned = std::make_unique<LLStringTableEntry>(str);
    LLStringTableEntry* entry = owned.get();
    // Key on the entry's own text, not on the caller's buffer, which it does
    // not own and which may be gone before the next lookup.
    mTable.emplace(entry->view(), std::move(owned));
    return entry;
}

LLStringTableEntry* LLStringTable::addStringEntry(const char* str)
{
    return str ? addStringEntry(std::string_view(str)) : nullptr;
}

char* LLStringTable::addString(std::string_view str)
{
    LLStringTableEntry* entry = addStringEntry(str);
    return entry ? entry->mString : nullptr;
}

char* LLStringTable::addString(const char* str)
{
    return str ? addString(std::string_view(str)) : nullptr;
}

void LLStringTable::removeString(std::string_view str)
{
    // An uncounted table never incremented, so it has no idea whether anyone
    // else still holds this entry and must not drop it.
    llassert(mCounted);
    if (!mCounted)
    {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(mMutex);
    table_t::iterator found = mTable.find(str);
    if (found != mTable.end() && !found->second->decCount())
    {
        mTable.erase(found);
    }
}

void LLStringTable::removeString(const char* str)
{
    if (str)
    {
        removeString(std::string_view(str));
    }
}
