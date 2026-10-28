/**
 * @file   threadpool.cpp
 * @author Nat Goodspeed
 * @date   2021-10-21
 * @brief  Implementation for threadpool.
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Copyright (c) 2021, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "threadpool.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include "commoncontrol.h"
#include "llcoros.h"
#include "llerror.h"
#include "llevents.h"
#include "llsd.h"
#include "stringize.h"

#include LLCOROS_MUTEX_HEADER
#include <boost/fiber/algo/round_robin.hpp>

#include <algorithm>
#include <iterator>

/*****************************************************************************
*   Custom fiber scheduler for worker threads
*****************************************************************************/
// As of 2022-12-06, each of our worker threads only runs a single (default)
// fiber: we don't launch explicit fibers within worker threads, nor do we
// anticipate doing so. So a worker thread that's simply waiting for incoming
// tasks should sleep until there is one. Override the default fiber scheduler
// to implement that.
#if LL_WINDOWS
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

struct sleepy_robin: public boost::fibers::algo::round_robin
{
    sleepy_robin():
        mWake(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
        mTimer(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS))
    {
        if (!mWake || !mTimer)
        {
            // Short of either, round_robin's own wait, for the waits and the
            // wakes alike.
            closeHandles();
        }
    }

    ~sleepy_robin() override
    {
        closeHandles();
    }

    // round_robin holds a std::condition_variable, and
    // round_robin::suspend_until() calls
    // std::condition_variable::wait_until(). On Windows, that call seems
    // busier than it ought to be. This waits on an event instead, which a
    // fiber made ready from another thread sets; and, where a fiber's timed
    // wait sets a deadline, on a high resolution waitable timer, which the
    // system timer's tick does not round as it does Sleep() and a wait's
    // timeout -- to a whole 15.6 ms on Windows 11 for a process whose window
    // is hidden, whatever resolution it asked for.
    void suspend_until(std::chrono::steady_clock::time_point const& until) noexcept override
    {
        if (!mWake)
        {
            round_robin::suspend_until(until);
            return;
        }
        if (until == (std::chrono::steady_clock::time_point::max)())
        {
            WaitForSingleObject(mWake, INFINITE);
            return;
        }
        const auto left = until - std::chrono::steady_clock::now();
        if (left <= left.zero())
        {
            return;
        }
        // Relative, in the timer's 100 ns units, rounded up: a wait that
        // ended early would only come back here.
        LARGE_INTEGER due;
        due.QuadPart = -(std::max)(LONGLONG(1),
            LONGLONG(std::chrono::ceil<std::chrono::duration<LONGLONG, std::ratio<1, 10000000>>>(left).count()));
        if (!SetWaitableTimerEx(mTimer, &due, 0, nullptr, nullptr, nullptr, 0))
        {
            // A timer that would not be set: a wait the tick rounds, which a
            // wake still ends at once.
            WaitForSingleObject(mWake, DWORD((std::min)(std::chrono::ceil<std::chrono::milliseconds>(left).count(),
                                                        static_cast<long long>(INFINITE - 1))));
            return;
        }
        const HANDLE waits[] = { mWake, mTimer };
        if (WaitForMultipleObjects(DWORD(std::size(waits)), waits, FALSE, INFINITE) == WAIT_OBJECT_0)
        {
            // Woken before the deadline, which nothing waits for now.
            CancelWaitableTimer(mTimer);
        }
    }

    // Auto-reset: a notify that comes before the wait is kept for it.
    void notify() noexcept override
    {
        if (!mWake)
        {
            round_robin::notify();
            return;
        }
        SetEvent(mWake);
    }

private:
    void closeHandles()
    {
        if (mTimer)
        {
            CloseHandle(mTimer);
            mTimer = nullptr;
        }
        if (mWake)
        {
            CloseHandle(mWake);
            mWake = nullptr;
        }
    }

    HANDLE mWake;
    HANDLE mTimer;
};
#endif // LL_WINDOWS

/*****************************************************************************
*   ThreadPoolBase
*****************************************************************************/
struct LL::ThreadPoolBase::RunState
{
    LLCoros::Mutex mMutex;
    // The threads started and not yet done.
    size_t mRunning = 0;
    // Set by abandon(): the pool, for the last of them to destroy.
    ThreadPoolBase* mOrphan = nullptr;
};

LL::ThreadPoolBase::ThreadPoolBase(const std::string& name,
                                   size_t threads,
                                   WorkQueueBase* queue,
                                   bool auto_shutdown,
                                   bool fixed_width):
    super(name),
    mName("ThreadPool:" + name),
    mThreadCount(fixed_width ? threads : getConfiguredWidth(name, threads)),
    mQueue(queue),
    mAutomaticShutdown(auto_shutdown),
    mRunState(std::make_shared<RunState>())
{}

void LL::ThreadPoolBase::start()
{
    for (size_t i = 0; i < mThreadCount; ++i)
    {
        std::string tname{ stringize(mName, ':', (i+1), '/', mThreadCount) };
        // Linux keeps fifteen bytes of a thread's name, and set_thread_name
        // cuts macOS's to as many, which left every pool thread
        // "ThreadPool:Gene" or the like in top -H, gdb, perf and a crash
        // report. The OS name drops the prefix and shortens the pool's name
        // instead, so the index survives; a pool of one needs none. Windows'
        // SetThreadDescription takes the whole name, and Tracy and the log
        // keep it everywhere.
#if LL_WINDOWS
        std::string os_name{ tname };
#else
        constexpr size_t OS_NAME_MAX = 15;
        std::string os_name{ mThreadCount > 1 ? stringize(':', (i+1)) : std::string() };
        os_name.insert(0, getKey().substr(0, OS_NAME_MAX - os_name.size()));
#endif
        // Counted before it starts, so that it cannot be done before it is
        // counted, and not counted if it never starts.
        {
            LLCoros::LockType lock(mRunState->mMutex);
            ++mRunState->mRunning;
        }
        try
        {
            mThreads.emplace_back(tname, [this, tname, os_name, state = mRunState]()
                {
                    // Tracy names the OS thread too, cut to the same fifteen
                    // bytes, so it goes first and the short name is the one left
                    LL_PROFILER_SET_THREAD_NAME(tname.c_str());
                    set_thread_name(os_name.c_str());
                    set_thread_fp_mode();
                    run(tname);
                    // The pool may be gone by now.
                    threadDone(state);
                });
        }
        catch (...)
        {
            LLCoros::LockType lock(mRunState->mMutex);
            --mRunState->mRunning;
            throw;
        }
    }

    if (!mAutomaticShutdown)
    {
        // Some threads, like main window's might need to run a bit longer
        // to wait for a proper shutdown message
        return;
    }

    // Listen on "LLApp", and when the app is shutting down, close the queue
    // and join the workers.
    mListening = true;
    LLEventPumps::instance().obtain("LLApp").listen(
        mName,
        [this](const LLSD& stat)
        {
            std::string status(stat["status"]);
            if (status != "running")
            {
                // viewer is starting shutdown -- proclaim the end is nigh!
                LL_DEBUGS("ThreadPool") << mName << " saw " << status << LL_ENDL;
                close();
            }
            return false;
        });
}

LL::ThreadPoolBase::~ThreadPoolBase()
{
    close();
    stopListening();
}

void LL::ThreadPoolBase::stopListening()
{
    if (mListening && !LLEventPumps::wasDeleted())
    {
        LLEventPumps::instance().obtain("LLApp").stopListening(mName);
    }
    mListening = false;
}

void LL::ThreadPoolBase::abandon(std::unique_ptr<ThreadPoolBase> pool)
{
    if (!pool)
    {
        return;
    }
    pool->mQueue->close();
    // Here, not on the pool's last thread: the event pumps are the main
    // thread's.
    pool->stopListening();
    {
        // The state outlives the pool, which its last thread may destroy as
        // soon as this lets go of the lock.
        const std::shared_ptr<RunState> state = pool->mRunState;
        LLCoros::LockType               lock(state->mMutex);
        if (state->mRunning > 0)
        {
            LL_DEBUGS("ThreadPool") << pool->mName << " abandoned to its threads" << LL_ENDL;
            for (auto& pair: pool->mThreads)
            {
                pair.second.detach();
            }
            state->mOrphan = pool.release();
            return;
        }
    }
    // Every thread has finished. close() joins only while the queue is
    // open, so they are joined here, and the pool destroyed as it would
    // have been.
    for (auto& pair: pool->mThreads)
    {
        if (pair.second.joinable())
        {
            pair.second.join();
        }
    }
}

//static
void LL::ThreadPoolBase::threadDone(const std::shared_ptr<RunState>& state)
{
    ThreadPoolBase* orphan = nullptr;
    {
        LLCoros::LockType lock(state->mMutex);
        if (--state->mRunning == 0)
        {
            orphan = state->mOrphan;
        }
    }
    // An abandoned pool's threads were let go, so none is joined.
    delete orphan;
}

void LL::ThreadPoolBase::close()
{
    if (! mQueue->isClosed())
    {
        LL_DEBUGS("ThreadPool") << mName << " closing queue and joining threads" << LL_ENDL;
        mQueue->close();
        for (auto& pair: mThreads)
        {
            if (pair.second.joinable())
            {
                LL_DEBUGS("ThreadPool") << mName << " waiting on thread " << pair.first << LL_ENDL;
                pair.second.join();
            }
        }
        LL_DEBUGS("ThreadPool") << mName << " shutdown complete" << LL_ENDL;
    }
}

void LL::ThreadPoolBase::run(const std::string& name)
{
#if LL_WINDOWS
    boost::fibers::use_scheduling_algorithm<sleepy_robin>();
#endif // LL_WINDOWS

    LL_DEBUGS("ThreadPool") << name << " starting" << LL_ENDL;
    run();
    LL_DEBUGS("ThreadPool") << name << " stopping" << LL_ENDL;
}

void LL::ThreadPoolBase::run()
{
    mQueue->runUntilClose();
}

//static
size_t LL::ThreadPoolBase::getConfiguredWidth(const std::string& name, size_t dft)
{
    LLSD poolSizes;
    try
    {
        poolSizes = LL::CommonControl::get("Global", "ThreadPoolSizes");
        // "ThreadPoolSizes" is actually a map containing the sizes of
        // interest -- or should be, if this process has an
        // LLViewerControlListener instance and its settings include
        // "ThreadPoolSizes". If we failed to retrieve it, perhaps we're in a
        // program that doesn't define that, or perhaps there's no such
        // setting, or perhaps we're asking too early, before the LLEventAPI
        // itself has been instantiated. In any of those cases, it seems worth
        // warning.
        if (! poolSizes.isDefined())
        {
            // Note: we don't warn about absence of an override key for a
            // particular ThreadPool name, that's fine. This warning is about
            // complete absence of a ThreadPoolSizes setting, which we expect
            // in a normal viewer session.
            LL_WARNS("ThreadPool") << "No 'ThreadPoolSizes' setting for ThreadPool '"
                                   << name << "'" << LL_ENDL;
        }
    }
    catch (const LL::CommonControl::Error& exc)
    {
        // We don't want ThreadPool to *require* LLViewerControlListener.
        // Just log it and carry on.
        LL_WARNS("ThreadPool") << "Can't check 'ThreadPoolSizes': " << exc.what() << LL_ENDL;
    }

    LL_DEBUGS("ThreadPool") << "ThreadPoolSizes = " << poolSizes << LL_ENDL;
    // LLSD treats an undefined value as an empty map when asked to retrieve a
    // key, so we don't need this to be conditional.
    LLSD sizeSpec{ poolSizes[name] };
    // We retrieve sizeSpec as LLSD, rather than immediately as LLSD::Integer,
    // so we can distinguish the case when it's undefined.
    return sizeSpec.isInteger() ? sizeSpec.asInteger() : dft;
}

//static
size_t LL::ThreadPoolBase::getWidth(const std::string& name, size_t dft)
{
    auto instance{ getInstance(name) };
    if (instance)
    {
        return instance->getWidth();
    }
    else
    {
        return getConfiguredWidth(name, dft);
    }
}
