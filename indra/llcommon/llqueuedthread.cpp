/**
 * @file llqueuedthread.cpp
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

#include "linden_common.h"
#include "llqueuedthread.h"

#include <chrono>

#include "llstl.h"
#include "lltimer.h"    // ms_sleep()
#include "llmutex.h"

//============================================================================

// MAIN THREAD
LLQueuedThread::LLQueuedThread(const std::string& name, bool threaded, bool should_pause) :
    LLThread(name),
    mIdleThread(true),
    mNextHandle(0),
    mStarted(false),
    mThreaded(threaded),
    mRequestQueue(name, 1024 * 1024)
{
    llassert(threaded); // not threaded implementation is deprecated

    // The thread starts when the owner calls start(), once the object is made.
    if (mThreaded && should_pause)
    {
        pause();
    }
}

// MAIN THREAD
void LLQueuedThread::start()
{
    if (mThreaded)
    {
        LLThread::start();
    }
}

// MAIN THREAD
LLQueuedThread::~LLQueuedThread()
{
    if (!mThreaded)
    {
        endThread();
    }
    shutdown();
    // ~LLThread() will be called here
}

void LLQueuedThread::shutdown()
{
    setQuitting();

    unpause(); // MAIN THREAD
    if (mThreaded)
    {
        // Closed, the queue still runs what it holds, retries due later
        // included, and the thread stops once it has run the last of it. A
        // retry it can no longer post is left queued, for the loop below.
        mRequestQueue.close();

        S32 timeout = 50;
        for ( ; timeout>0; timeout--)
        {
            if (isStopped())
            {
                break;
            }
            ms_sleep(100);
            LLThread::yield();
        }
        if (timeout == 0)
        {
            LL_WARNS() << "~LLQueuedThread (" << mName << ") timed out!" << LL_ENDL;
        }
    }
    else
    {
        mStatus = STOPPED;
    }

    QueuedRequest* req;
    S32 queued_count = 0;
    bool  has_active = false;
    lockData();
    while ( (req = (QueuedRequest*)mRequestHash.pop_element()) )
    {
        if (req->getStatus() == STATUS_INPROGRESS)
        {
            has_active = true;
            req->setFlags(FLAG_ABORT | FLAG_AUTO_COMPLETE);
            continue;
        }
        if (req->getStatus() == STATUS_QUEUED)
        {
            ++queued_count;
            req->setStatus(STATUS_ABORTED); // avoid assert in deleteRequest
        }
        req->deleteRequest();
    }
    unlockData();
    if (queued_count)
    {
        LL_WARNS() << "~LLQueuedThread() called with unpocessed requests: " << queued_count << LL_ENDL;
    }
    if (has_active)
    {
        LL_WARNS() << "~LLQueuedThread() called with active requests!" << LL_ENDL;
        ms_sleep(100); // last chance for request to finish
        printQueueStats();
    }

    mRequestQueue.close();
}

//----------------------------------------------------------------------------

// MAIN THREAD
// virtual
size_t LLQueuedThread::update(F32 max_time_ms)
{
    LL_PROFILE_ZONE_SCOPED;
    if (!mStarted)
    {
        if (!mThreaded)
        {
            startThread();
            mStarted = true;
        }
    }
    return updateQueue(max_time_ms);
}

size_t LLQueuedThread::updateQueue(F32 max_time_ms)
{
    LL_PROFILE_ZONE_SCOPED;
    // Frame Update
    if (mThreaded)
    {
        // schedule a call to threadedUpdate for every call to updateQueue,
        // unless it has nothing to do: then the post only wakes the thread
        if (!isQuitting() && hasThreadedUpdate())
        {
            mRequestQueue.post([=, this]()
                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_THREAD("qt - update");
                    mIdleThread = false;
                    threadedUpdate();
                    mIdleThread = true;
                }
            );
        }

        if(getPending() > 0)
        {
            unpause();
        }
    }
    else
    {
        mRequestQueue.runFor(std::chrono::microseconds((int) (max_time_ms*1000.f)));
        threadedUpdate();
    }
    return getPending();
}

void LLQueuedThread::incQueue()
{
    // Something has been added to the queue
    if (!isPaused())
    {
        if (mThreaded)
        {
            wake(); // Wake the thread up if necessary.
        }
    }
}

//virtual
// May be called from any thread
size_t LLQueuedThread::getPending()
{
    return mRequestQueue.size();
}

// MAIN thread
void LLQueuedThread::waitOnPending()
{
    while(1)
    {
        update(0);

        if (mIdleThread)
        {
            break;
        }
        if (mThreaded)
        {
            yield();
        }
    }
    return;
}

void LLQueuedThread::waitOnPending(F32 max_time_sec)
{
    LLTimer wait_timer;
    wait_timer.reset();
    while (1)
    {
        update(0);

        if (mIdleThread)
        {
            break;
        }

        if (wait_timer.getElapsedTimeF64() >= max_time_sec)
        {
            LL_WARNS() << "waitOnPending() timed out after " << max_time_sec
                << " seconds in thread: " << mName << LL_ENDL;
            break;
        }

        if (mThreaded)
        {
            yield();
        }
    }
}

// MAIN thread
void LLQueuedThread::printQueueStats()
{
    auto size = mRequestQueue.size();
    if (size > 0)
    {
        LL_INFOS() << llformat("Pending Requests:%d ", mRequestQueue.size()) << LL_ENDL;
    }
    else
    {
        LL_INFOS() << "Queued Thread Idle" << LL_ENDL;
    }
}

// MAIN thread
LLQueuedThread::handle_t LLQueuedThread::generateHandle()
{
    U32 res = ++mNextHandle;
    return res;
}

// MAIN thread
bool LLQueuedThread::addRequest(QueuedRequest* req)
{
    LL_PROFILE_ZONE_SCOPED;
    if (mStatus == QUITTING)
    {
        return false;
    }

    lockData();
    req->setStatus(STATUS_QUEUED);
    mRequestHash.insert(req);
#if _DEBUG
//  LL_INFOS() << llformat("LLQueuedThread::Added req [%08d]",handle) << LL_ENDL;
#endif
    unlockData();

    llassert(!mRunCondition->isSelfLocked());
    mRequestQueue.post([this, req]() { processRequest(req); });

    return true;
}

// MAIN thread
bool LLQueuedThread::waitForResult(LLQueuedThread::handle_t handle, bool auto_complete)
{
    LL_PROFILE_ZONE_SCOPED;
    llassert (handle != nullHandle());
    bool res = false;
    bool waspaused = isPaused();
    bool done = false;
    while(!done)
    {
        update(0); // unpauses
        lockData();
        QueuedRequest* req = (QueuedRequest*)mRequestHash.find(handle);
        if (!req)
        {
            done = true; // request does not exist
        }
        else if (req->getStatus() == STATUS_COMPLETE)
        {
            res = true;
            if (auto_complete)
            {
                mRequestHash.erase(handle);
                req->deleteRequest();
//              check();
            }
            done = true;
        }
        unlockData();

        if (!done && mThreaded)
        {
            yield();
        }
    }
    if (waspaused)
    {
        pause();
    }
    return res;
}

// MAIN thread
LLQueuedThread::QueuedRequest* LLQueuedThread::getRequest(handle_t handle)
{
    if (handle == nullHandle())
    {
        return 0;
    }
    lockData();
    QueuedRequest* res = (QueuedRequest*)mRequestHash.find(handle);
    unlockData();
    return res;
}

LLQueuedThread::status_t LLQueuedThread::getRequestStatus(handle_t handle)
{
    status_t res = STATUS_EXPIRED;
    lockData();
    QueuedRequest* req = (QueuedRequest*)mRequestHash.find(handle);
    if (req)
    {
        res = req->getStatus();
    }
    unlockData();
    return res;
}

// Any thread
void LLQueuedThread::wakeRequest(handle_t handle)
{
    mRequestQueue.post([this, handle]
        {
            LL_PROFILE_ZONE_NAMED("processRequest - wake");
            retryRequest(handle, 0);
        });
}

void LLQueuedThread::abortRequest(handle_t handle, bool autocomplete)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    lockData();
    QueuedRequest* req = (QueuedRequest*)mRequestHash.find(handle);
    const bool found(req != NULL);
    if (req)
    {
        req->setFlags(FLAG_ABORT | (autocomplete ? FLAG_AUTO_COMPLETE : 0));
    }
    unlockData();
    // A request waiting for a wake sees the abort now, not at its fallback
    if (found)
    {
        wakeRequest(handle);
    }
}

// MAIN thread
void LLQueuedThread::setFlags(handle_t handle, U32 flags)
{
    lockData();
    QueuedRequest* req = (QueuedRequest*)mRequestHash.find(handle);
    if (req)
    {
        req->setFlags(flags);
    }
    unlockData();
}

bool LLQueuedThread::completeRequest(handle_t handle)
{
    LL_PROFILE_ZONE_SCOPED;
    bool res = false;
    lockData();
    QueuedRequest* req = (QueuedRequest*)mRequestHash.find(handle);
    if (req)
    {
        llassert_always(req->getStatus() != STATUS_QUEUED);
        llassert_always(req->getStatus() != STATUS_INPROGRESS);
#if _DEBUG
//      LL_INFOS() << llformat("LLQueuedThread::Completed req [%08d]",handle) << LL_ENDL;
#endif
        mRequestHash.erase(handle);
        req->deleteRequest();
//      check();
        res = true;
    }
    unlockData();
    return res;
}

bool LLQueuedThread::check()
{
#if 0 // not a reliable check once mNextHandle wraps, just for quick and dirty debugging
    for (int i=0; i<REQUEST_HASH_SIZE; i++)
    {
        LLSimpleHashEntry<handle_t>* entry = mRequestHash.get_element_at_index(i);
        while (entry)
        {
            if (entry->getHashKey() > mNextHandle)
            {
                LL_ERRS() << "Hash Error" << LL_ENDL;
                return false;
            }
            entry = entry->getNextEntry();
        }
    }
#endif
    return true;
}

//============================================================================
// Runs on its OWN thread

void LLQueuedThread::processRequest(LLQueuedThread::QueuedRequest* req)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;

    mIdleThread = false;
    //threadedUpdate();

    // Get next request from pool
    lockData();

    if ((req->getFlags() & FLAG_ABORT) || (mStatus == QUITTING))
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_THREAD("qtpr - abort");
        req->setStatus(STATUS_ABORTED);
        req->finishRequest(false);
        if (req->getFlags() & FLAG_AUTO_COMPLETE)
        {
            mRequestHash.erase(req);
            req->deleteRequest();
//              check();
        }
        unlockData();
    }
    else
    {
        llassert_always(req->getStatus() == STATUS_QUEUED);

        if (req)
        {
            req->setStatus(STATUS_INPROGRESS);
        }
        unlockData();

        // This is the only place we will call req->setStatus() after
        // it has initially been seet to STATUS_QUEUED, so it is
        // safe to access req.
        if (req)
        {
            // process request
            bool complete = req->processRequest();

            if (complete)
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_THREAD("qtpr - complete");
                lockData();
                req->setStatus(STATUS_COMPLETE);
                req->finishRequest(true);
                if (req->getFlags() & FLAG_AUTO_COMPLETE)
                {
                    mRequestHash.erase(req);
                    req->deleteRequest();
                    //              check();
                }
                unlockData();
            }
            else
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_THREAD("qtpr - retry");
                //put back on queue and try again in 0.1ms
                lockData();
                req->setStatus(STATUS_QUEUED);

                unlockData();

                llassert(!mRunCondition->isSelfLocked());

                // Try again in 16 ms, running whatever else is ready
                // meanwhile, or sooner if wakeRequest() says what it waits
                // on has happened. A request that waits for that wake is
                // tried again only as a fallback, in case it never comes.
                // Posted from this thread between requests, so the wait for
                // the next one already knows this one's time.
                using namespace std::chrono_literals;
                const auto delay(req->waitsForWake() ? 250ms : 16ms);
                const handle_t handle(req->getHashKey());
                const U32 generation(++req->mRetryGeneration);
                req->mRetryPending = true;
                mRequestQueue.post([this, handle, generation]
                    {
                        LL_PROFILE_ZONE_NAMED("processRequest - retry");
                        retryRequest(handle, generation);
                    },
                    LL::WorkSchedule::TimePoint::clock::now() + delay);
            }
        }
    }

    mIdleThread = true;
}

// Runs on its OWN thread. Found by handle, since a retry already run may
// have finished the request and deleted it. While a retry is pending the
// request is queued, which nothing else deletes.
void LLQueuedThread::retryRequest(handle_t handle, U32 generation)
{
    lockData();
    QueuedRequest* req = (QueuedRequest*)mRequestHash.find(handle);
    unlockData();
    if (req && req->mRetryPending && (0 == generation || req->mRetryGeneration == generation))
    {
        req->mRetryPending = false;
        processRequest(req);
    }
}

// virtual
bool LLQueuedThread::runCondition()
{
    // mRunCondition must be locked here
    if (mRequestQueue.size() == 0 && mIdleThread)
        return false;
    else
        return true;
}

// virtual
void LLQueuedThread::run()
{
    // A queue made paused waits here until it is unpaused.
    checkPause();
    startThread();


    /*while (1)
    {
        LL_PROFILE_ZONE_SCOPED;
        // this will block on the condition until runCondition() returns true, the thread is unpaused, or the thread leaves the RUNNING state.
        checkPause();

        mIdleThread = false;

        threadedUpdate();

        auto pending_work = processNextRequest();

        if (pending_work == 0)
        {
            //LL_PROFILE_ZONE_NAMED("LLQueuedThread - sleep");
            mIdleThread = true;
            //ms_sleep(1);
        }
        //LLThread::yield(); // thread should yield after each request
    }*/
    mRequestQueue.runUntilClose();

    endThread();
    LL_INFOS() << "LLQueuedThread " << mName << " EXITING." << LL_ENDL;


}

// virtual
void LLQueuedThread::startThread()
{
}

// virtual
void LLQueuedThread::endThread()
{
}

// virtual
void LLQueuedThread::threadedUpdate()
{
}

//============================================================================

LLQueuedThread::QueuedRequest::QueuedRequest(LLQueuedThread::handle_t handle, U32 flags) :
    LLSimpleHashEntry<LLQueuedThread::handle_t>(handle),
    mStatus(STATUS_UNKNOWN),
    mFlags(flags)
{
}

LLQueuedThread::QueuedRequest::~QueuedRequest()
{
    if (mStatus != STATUS_DELETE)
    {
        // The only method to delete a request is deleteRequest(),
        // it should have set the status to STATUS_DELETE
        LL_ERRS() << "LLQueuedThread::QueuedRequest deleted with status " << mStatus << LL_ENDL;
    }
}

//virtual
void LLQueuedThread::QueuedRequest::finishRequest(bool completed)
{
}

//virtual
void LLQueuedThread::QueuedRequest::deleteRequest()
{
    llassert_always(mStatus != STATUS_INPROGRESS);
    setStatus(STATUS_DELETE);
    delete this;
}
