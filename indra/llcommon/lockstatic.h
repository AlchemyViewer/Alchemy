/**
 * @file   lockstatic.h
 * @author Nat Goodspeed
 * @date   2019-12-03
 * @brief  LockStatic class provides mutex-guarded access to the specified
 *         static data.
 *
 * $LicenseInfo:firstyear=2019&license=viewerlgpl$
 * Copyright (c) 2019, Linden Research, Inc.
 * $/LicenseInfo$
 */

#if ! defined(LL_LOCKSTATIC_H)
#define LL_LOCKSTATIC_H

#include <mutex> // std::unique_lock
#include <typeinfo> // typeid
#include <cstddef> // size_t
#include "llpreprocessor.h" // LL_COMMON_API

namespace llthread
{

// Return THE canonical instance of the Static identified by 'name'
// (typeid(Static).name()), creating it once via factory() on first request.
// Defined in llthread.cpp -- a single translation unit -- so every module in a
// multi-DLL build shares one instance instead of each getting its own
// function-local static copy. Keyed by the mangled NAME STRING (not type_info
// identity), which is stable across modules even under -fvisibility=hidden.
LL_COMMON_API void* getCanonicalStatic(const char* name, void* (*factory)());

// DLL-safe thread-local slot store. A thread_local variable cannot carry a dll
// interface on MSVC (C2492), so it cannot simply be exported to share one
// instance across modules. Instead the underlying thread_local array lives in a
// single module (llcommon) and is reached through these exported accessors, so
// every module shares one per-thread value per slot. allocThreadLocalSlot()
// assigns a stable slot index per 'name' (pass typeid(T).name()), consistent
// across modules; the index is then used with get/setThreadLocalSlot().
LL_COMMON_API size_t allocThreadLocalSlot(const char* name);
LL_COMMON_API void*  getThreadLocalSlot(size_t slot);
LL_COMMON_API void   setThreadLocalSlot(size_t slot, void* value);

// Instantiate this template to obtain a pointer to the canonical static
// instance of Static while holding a lock on that instance. Use of
// Static::mMutex presumes that Static declares some suitable mMutex.
template <typename Static>
class LockStatic
{
    typedef std::unique_lock<decltype(Static::mMutex)> lock_t;
public:
    LockStatic():
        mData(getStatic()),
        mLock(mData->mMutex)
    {}
    Static* get() const { return mData; }
    operator Static*() const { return get(); }
    Static* operator->() const { return get(); }
    // sometimes we must explicitly unlock...
    void unlock()
    {
        // but once we do, access is no longer permitted
        mData = nullptr;
        mLock.unlock();
    }
protected:
    Static* mData;
    lock_t mLock;
private:
    Static* getStatic()
    {
        // One canonical Static per process, shared across module (DLL)
        // boundaries: getCanonicalStatic() lives in a single translation unit,
        // so every module resolves the SAME instance. This matters because a
        // plain `static Static sData;` is duplicated once per module in a
        // multi-DLL build -- each DLL would get its own copy of the tracked
        // data, which (e.g. for LLInstanceTracker / LLTrace) silently splits
        // registrations across modules.
        //
        // The cached pointer below is itself a function-local static, so it
        // retains the original guarantee that matters: it is constructed
        // exactly once, on first reach -- even when the containing module's
        // other static variables have not yet been runtime-initialized -- and
        // the underlying Static (with its mMutex) is created lazily at that
        // same point. It simply now converges to the one canonical instance.
        static Static* sData = static_cast<Static*>(
            getCanonicalStatic(typeid(Static).name(),
                               []() -> void* { return new Static(); }));
        return sData;
    }
};

} // llthread namespace

#endif /* ! defined(LL_LOCKSTATIC_H) */
