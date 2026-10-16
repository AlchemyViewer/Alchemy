/**
 * @file llthreadlocalstorage.h
 * @author Richard
 * @brief Class wrappers for thread local storage
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
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

#ifndef LL_LLTHREADLOCALSTORAGE_H
#define LL_LLTHREADLOCALSTORAGE_H

#include "llinstancetracker.h"
#include "lockstatic.h"             // llthread DLL-safe thread-local slots
#include <typeinfo>

// A per-DERIVED_TYPE thread-local pointer. The actual thread_local storage is
// NOT a member here: a plain `thread_local DERIVED_TYPE* sInstance;` would be
// duplicated once per module in a shared-library build, so setInstance() in one
// module and getInstance() inlined into another would see different storage.
// (And a thread_local cannot be dllexport'd to share it -- MSVC C2492.) Instead
// we key into a single canonical thread-local slot array owned by llcommon,
// reached through exported accessors -- one extra (non-inlinable) call per
// access, the price of cross-module correctness.
template<typename DERIVED_TYPE>
class LLThreadLocalSingletonPointer
{
public:
    LL_FORCE_INLINE static DERIVED_TYPE* getInstance()
    {
        return static_cast<DERIVED_TYPE*>(llthread::getThreadLocalSlot(slot()));
    }

    static void setInstance(DERIVED_TYPE* instance)
    {
        llthread::setThreadLocalSlot(slot(), instance);
    }

private:
    // Stable slot index for DERIVED_TYPE, cached per module; the value is
    // canonical (identical) across modules.
    static size_t slot()
    {
        static size_t sSlot = llthread::allocThreadLocalSlot(typeid(DERIVED_TYPE).name());
        return sSlot;
    }
};

#endif // LL_LLTHREADLOCALSTORAGE_H
