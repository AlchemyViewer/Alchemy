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
#include "llerror.h"
#include "llevents.h"
#include "stringize.h"

LL::ThreadPool::ThreadPool(const std::string& name, size_t threads, size_t capacity, EThreadPrio prio):
    super(name),
    mQueue(name, capacity),
    mName("ThreadPool:" + name),
    mThreadCount(threads),
    mThreadPriority(prio)
{}

void LL::ThreadPool::start()
{
    for (size_t i = 0; i < mThreadCount; ++i)
    {
		std::string tname{ stringize(mName, ':', (i + 1), '/', mThreadCount) };
        EThreadPrio prio = mThreadPriority;
		mThreads.emplace_back(tname, [this, tname, prio]()
			{
				LL_PROFILER_SET_THREAD_NAME(tname.c_str());
#if LL_WINDOWS
		        int thread_prio = 0;
		        switch (prio)
		        {
		        case E_LOWEST:
			        thread_prio = THREAD_PRIORITY_LOWEST;
			        break;
		        case E_BELOW_NORMAL:
			        thread_prio = THREAD_PRIORITY_BELOW_NORMAL;
			        break;
		        default:
		        case E_NORMAL:
			        thread_prio = THREAD_PRIORITY_NORMAL;
			        break;
		        case E_ABOVE_NORMAL:
			        thread_prio = THREAD_PRIORITY_ABOVE_NORMAL;
			        break;
		        case E_HIGHEST:
			        thread_prio = THREAD_PRIORITY_HIGHEST;
			        break;
		        }
		        SetThreadPriority(GetCurrentThread(), thread_prio);
#endif
                run(tname);
            });
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

LL::ThreadPool::~ThreadPool()
{
    close();
}

void LL::ThreadPool::close()
{
    if (! mQueue.isClosed())
    {
        LL_DEBUGS("ThreadPool") << mName << " closing queue and joining threads" << LL_ENDL;
        mQueue.close();
        for (auto& pair: mThreads)
        {
            LL_DEBUGS("ThreadPool") << mName << " waiting on thread " << pair.first << LL_ENDL;
            pair.second.join();
        }
        LL_DEBUGS("ThreadPool") << mName << " shutdown complete" << LL_ENDL;
    }
}

void LL::ThreadPool::run(const std::string& name)
{
    LL_DEBUGS("ThreadPool") << name << " starting" << LL_ENDL;
    run();
    LL_DEBUGS("ThreadPool") << name << " stopping" << LL_ENDL;
}

void LL::ThreadPool::run()
{
    mQueue.runUntilClose();
}
