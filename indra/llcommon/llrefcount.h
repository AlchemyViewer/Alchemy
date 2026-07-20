/**
 * @file llrefcount.h
 * @brief Base class for reference counted objects for use with LLPointer
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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
#ifndef LLREFCOUNT_H
#define LLREFCOUNT_H

#include <boost/intrusive_ptr.hpp>
#include "llatomic.h"

#include <atomic>

class LLMutex;

//----------------------------------------------------------------------------
// RefCount objects should generally only be accessed by way of LLPointer<>'s
// see llthread.h for LLThreadSafeRefCount
//----------------------------------------------------------------------------

//nonsense but recognizable value for freed LLRefCount (aids in debugging)
#define LL_REFCOUNT_FREE 1234567890
extern const S32 gMaxRefCount;

class LL_COMMON_API LLRefCount
{
protected:
    LLRefCount(const LLRefCount& other);
    LLRefCount& operator=(const LLRefCount&);
    virtual ~LLRefCount(); // use unref()

public:
    LLRefCount();

    inline void ref() const
    {
        llassert(mRef != LL_REFCOUNT_FREE); // object is deleted
        mRef++;
        llassert(mRef < gMaxRefCount); // ref count excessive, likely memory leak
    }

    inline S32 unref() const
    {
        llassert(mRef != LL_REFCOUNT_FREE); // object is deleted
        llassert(mRef > 0); // ref count below 1, likely corrupted
        if (0 == --mRef)
        {
            mRef = LL_REFCOUNT_FREE; // set to nonsense yet recognizable value to aid in debugging
            delete this;
            return 0;
        }
        return mRef;
    }

    //NOTE: when passing around a const LLRefCount object, this can return different results
    // at different types, since mRef is mutable
    S32 getNumRefs() const
    {
        return mRef;
    }

private:
    mutable S32 mRef;
};


//============================================================================

// see llmemory.h for LLPointer<> definition

class LL_COMMON_API LLThreadSafeRefCount
{
protected:
    virtual ~LLThreadSafeRefCount(); // use unref()

public:
    LLThreadSafeRefCount();
    LLThreadSafeRefCount(const LLThreadSafeRefCount&);
    LLThreadSafeRefCount& operator=(const LLThreadSafeRefCount& ref)
    {
        mRef.store(0, std::memory_order_relaxed);
        return *this;
    }

    void ref()
    {
        // Relaxed is sufficient for the increment: it only has to be atomic. The caller
        // necessarily already holds a reference, so this cannot race the count to zero,
        // and the increment itself publishes nothing that another thread must observe.
        mRef.fetch_add(1, std::memory_order_relaxed);
    }

    void unref()
    {
        llassert(mRef.load(std::memory_order_relaxed) >= 1);

        // Release, so that every write made through this reference happens-before the
        // destructor. The acquire fence on the final decrement then pairs with the
        // releases from *all* threads' decrements, so ~T() observes their writes too.
        // This is the standard shared_ptr refcount idiom -- do not weaken the decrement
        // to relaxed, or the destructor can race the last writer's stores.
        if (mRef.fetch_sub(1, std::memory_order_release) == 1)
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            // If we hit zero, the caller should be the only smart pointer owning the object and we can delete it.
            // It is technically possible for a vanilla pointer to mess this up, or another thread to
            // jump in, find this object, create another smart pointer and end up dangling, but if
            // the code is that bad and not thread-safe, it's trouble already.
            delete this;
        }
    }

    S32 getNumRefs() const
    {
        return mRef.load(std::memory_order_relaxed);
    }

private:
    // Deliberately a bare std::atomic rather than LLAtomicS32: the orderings above are
    // chosen per-operation for refcounting. LLAtomicBase is seq_cst on every operation
    // and is used elsewhere for cross-thread flags that depend on that -- see the note
    // in llatomic.h.
    std::atomic<S32> mRef;
};

/**
 * intrusive pointer support for LLThreadSafeRefCount
 * this allows you to use boost::intrusive_ptr with any LLThreadSafeRefCount-derived type
 */
inline void intrusive_ptr_add_ref(LLThreadSafeRefCount* p)
{
    p->ref();
}

inline void intrusive_ptr_release(LLThreadSafeRefCount* p)
{
    p->unref();
}

/**
 * intrusive pointer support
 * this allows you to use boost::intrusive_ptr with any LLRefCount-derived type
 */
inline void intrusive_ptr_add_ref(LLRefCount* p)
{
    p->ref();
}

inline void intrusive_ptr_release(LLRefCount* p)
{
    p->unref();
}

#endif
