/**
 * @file llstringtable.h
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

#ifndef LL_STRING_TABLE_H
#define LL_STRING_TABLE_H

#include "lldefs.h"
#include "llformat.h"
#include "llstl.h"

#include <atomic>
#include <memory>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>

#include <boost/unordered/unordered_flat_map.hpp>

class LL_COMMON_API LLStringTableEntry
{
public:
    explicit LLStringTableEntry(std::string_view str);

    // mString points into mText, so this must neither be copied nor moved:
    // either would leave the pointer aimed at the old object's buffer. The
    // table owns entries through unique_ptr and hands out bare pointers, so
    // nothing needs to.
    LLStringTableEntry(const LLStringTableEntry&) = delete;
    LLStringTableEntry& operator=(const LLStringTableEntry&) = delete;
    LLStringTableEntry(LLStringTableEntry&&) = delete;
    LLStringTableEntry& operator=(LLStringTableEntry&&) = delete;

    void incCount() { mCount.fetch_add(1, std::memory_order_relaxed); }
    bool decCount() { return mCount.fetch_sub(1, std::memory_order_acq_rel) != 1; }

    std::string_view view() const { return mText; }

private:
    // Declared first so that mString can be initialized from it.
    std::string         mText;
    std::atomic<S32>    mCount;

public:
    // The whole string, NUL terminated. Callers read this; none writes it.
    char*               mString;
};

// A table of unique strings. Entries are handed out as bare pointers and are
// stable for as long as the table holds them, so callers may compare two names
// by comparing their entry pointers.
//
// Safe to use from more than one thread: lookups of a string already present
// take a shared lock, and only adding a new one is exclusive.
class LL_COMMON_API LLStringTable
{
public:
    // Whether entries are counted. A counted table hands a string back with
    // removeString() once the last holder is done with it. An uncounted one
    // never gives one back -- and so pays no refcount traffic on lookup,
    // which is the whole cost difference on a path parsing hits per tag and
    // per attribute.
    enum ERefCounting
    {
        NOT_COUNTED,
        COUNTED
    };

    // The size argument is a hint about how many distinct strings to expect.
    // It no longer fixes a bucket count, and zero means "no idea".
    explicit LLStringTable(int tablesize = 0, ERefCounting counting = COUNTED);
    ~LLStringTable();

    LLStringTable(const LLStringTable&) = delete;
    LLStringTable& operator=(const LLStringTable&) = delete;

    // Find without adding. Returns null when the string is not present.
    char*               checkString(std::string_view str) const;
    char*               checkString(const char* str) const;
    LLStringTableEntry* checkStringEntry(std::string_view str) const;
    LLStringTableEntry* checkStringEntry(const char* str) const;

    // Find, adding when absent. On a counted table the entry's count goes up
    // by one either way.
    char*               addString(std::string_view str);
    char*               addString(const char* str);
    LLStringTableEntry* addStringEntry(std::string_view str);
    LLStringTableEntry* addStringEntry(const char* str);

    // Drops one count, and the entry with it when that was the last.
    // Does nothing on an uncounted table, which cannot know who still holds
    // the entry; asking is a programming error and asserts.
    void                removeString(std::string_view str);
    void                removeString(const char* str);

    S32                 getUniqueEntries() const;

private:
    // Keyed by a view onto each entry's own text, so a key costs nothing
    // beyond the entry it names, and lookups compare whole strings.
    typedef boost::unordered_flat_map<std::string_view,
                                      std::unique_ptr<LLStringTableEntry>,
                                      ll::string_hash,
                                      std::equal_to<>> table_t;

    mutable std::shared_mutex   mMutex;
    table_t                     mTable;
    const bool                  mCounted;
};

extern LL_COMMON_API LLStringTable gStringTable;

//============================================================================

// A table of unique strings for a caller that only ever adds to it, meant to
// be held locally -- LLXmlTree keeps one for node names and a static one for
// attribute names.
//
// A handle is a bare pointer to the stored string and stays valid until the
// table is cleaned up or destroyed, so callers key containers on the handle
// and compare two names by comparing pointers. LLXmlTreeNode holds its
// attributes that way.
//
// No lock, unlike LLStringTable: everything reaching this goes through an
// LLXmlTree, which is built and read on one thread.

typedef const std::string* LLStdStringHandle;

class LL_COMMON_API LLStdStringTable
{
public:
    // The size argument is a hint about how many distinct strings to expect.
    // It no longer fixes a bucket count, and zero means "no idea".
    explicit LLStdStringTable(S32 tablesize = 0)
    {
        if (tablesize > 0)
        {
            mTable.reserve(tablesize);
        }
    }

    ~LLStdStringTable() = default;

    LLStdStringTable(const LLStdStringTable&) = delete;
    LLStdStringTable& operator=(const LLStdStringTable&) = delete;

    // Drops every string, and with them every handle this table gave out.
    void cleanup() { mTable.clear(); }

    // Find, adding when absent.
    LLStdStringHandle insert(std::string_view str)
    {
        table_t::iterator found = mTable.find(str);
        if (found != mTable.end())
        {
            return found->second.get();
        }

        // The key views the stored string's own characters. Growing the table
        // moves the pointer, never the string it points at, so both the key
        // and the handle survive a rehash.
        std::unique_ptr<std::string> owned = std::make_unique<std::string>(str);
        LLStdStringHandle handle = owned.get();
        mTable.emplace(std::string_view(*handle), std::move(owned));
        return handle;
    }

    LLStdStringHandle addString(std::string_view str) { return insert(str); }

    // Find without adding. Null when the string is not present.
    LLStdStringHandle checkString(std::string_view str) const
    {
        table_t::const_iterator found = mTable.find(str);
        return (found == mTable.end()) ? nullptr : found->second.get();
    }

    LLStdStringHandle lookup(std::string_view str) const { return checkString(str); }

    size_t getUniqueEntries() const { return mTable.size(); }

private:
    typedef boost::unordered_flat_map<std::string_view,
                                      std::unique_ptr<std::string>,
                                      ll::string_hash,
                                      std::equal_to<>> table_t;

    table_t mTable;
};


#endif
