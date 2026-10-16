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
#include "llerror.h"
#include "llevents.h"
#include "llsd.h"
#include "stringize.h"

#include <boost/fiber/algo/round_robin.hpp>

/*****************************************************************************
*   Custom fiber scheduler for worker threads
*****************************************************************************/
// As of 2022-12-06, each of our worker threads only runs a single (default)
// fiber: we don't launch explicit fibers within worker threads, nor do we
// anticipate doing so. So a worker thread that's simply waiting for incoming
// tasks should really sleep a little. Override the default fiber scheduler to
// implement that.
struct sleepy_robin: public boost::fibers::algo::round_robin
{
    virtual void suspend_until( std::chrono::steady_clock::time_point const&) noexcept
    {
#if LL_WINDOWS
        // round_robin holds a std::condition_variable, and
        // round_robin::suspend_until() calls
        // std::condition_variable::wait_until(). On Windows, that call seems
        // busier than it ought to be. Try just sleeping.
        Sleep(1);
#else
        // currently unused other than windows, but might as well have something here
        // different units than Sleep(), but we actually just want to sleep for any de-minimis duration
        usleep(1);
#endif
    }

    virtual void notify() noexcept
    {
        // Since our Sleep() call above will wake up on its own, we need not
        // take any special action to wake it.
    }
};

/*****************************************************************************
*   ThreadPoolBase
*****************************************************************************/
LL::ThreadPoolBase::ThreadPoolBase(const std::string& name,
                                   size_t threads,
                                   WorkQueueBase* queue,
                                   bool auto_shutdown,
                                   bool fixed_width):
    super(name),
    mName("ThreadPool:" + name),
    mThreadCount(fixed_width ? threads : getConfiguredWidth(name, threads)),
    mQueue(queue),
    mAutomaticShutdown(auto_shutdown)
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
        mThreads.emplace_back(tname, [this, tname, os_name]()
            {
                // Tracy names the OS thread too, cut to the same fifteen
                // bytes, so it goes first and the short name is the one left
                LL_PROFILER_SET_THREAD_NAME(tname.c_str());
                set_thread_name(os_name.c_str());
                set_thread_fp_mode();
                run(tname);
            });
    }

    if (!mAutomaticShutdown)
    {
        // Some threads, like main window's might need to run a bit longer
        // to wait for a proper shutdown message
        return;
    }

    // Listen on "LLApp", and when the app is shutting down, close the queue
    // and join the workers.
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
    if (!LLEventPumps::wasDeleted())
    {
        LLEventPumps::instance().obtain("LLApp").stopListening(mName);
    }
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
    // Try using sleepy_robin fiber scheduler.
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
