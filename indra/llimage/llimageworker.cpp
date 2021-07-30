/** 
 * @file llimageworker.cpp
 * @brief Base class for images.
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

#include "llimageworker.h"
#include "llimagedxt.h"
#include <readerwriterqueue.h>

std::atomic< U32 > sImageThreads = 0;

class PoolWorkerThread : public LLThread
{
public:
	PoolWorkerThread(std::string name) : LLThread(name), mRequestQueue(30)
	{
	}

	virtual void run()
	{
		while (!isQuitting())
		{
            checkPause();

			LLImageDecodeThread::ImageRequest* req  = nullptr;
            while (!isQuitting() && mRequestQueue.try_dequeue(req))
            {
                if (req)
                {
                    req->processRequestIntern();
                }
            }
		}
	}

	bool runCondition()
    {
        return mRequestQueue.size_approx() > 0;
	}

	bool setRequest(LLImageDecodeThread::ImageRequest* req)
	{
        bool bSuccess = mRequestQueue.try_enqueue(req);
		wake();

		return bSuccess;
	}

private:
    moodycamel::ReaderWriterQueue<LLImageDecodeThread::ImageRequest*> mRequestQueue;
};

//----------------------------------------------------------------------------

// MAIN THREAD
LLImageDecodeThread::LLImageDecodeThread(bool threaded, U32 pool_size)
	: LLQueuedThread("imagedecode", threaded)
	, mCreationListSize(0)
	, mLastPoolAllocation(0)
{
	mCreationMutex = new LLMutex();

    if (pool_size == 0)
	{
        pool_size = std::thread::hardware_concurrency();
        if (pool_size == 0)
            pool_size = 4U;  // Use a sane default: 2 cores
        if (pool_size >= 8U)
		{
			// Using number of (virtual) cores minus 3 for:
			// - main image worker
			// - viewer main loop thread
			// - mesh repo thread
			// further bound to a maximum of 16 threads (more than that is totally useless, even
			// when flying over main land with 512m draw distance).
            pool_size = llmin(pool_size - 3U, 16U);
		}
        else if (pool_size > 2U)
		{
			// Using number of (virtual) cores - 1 (for the main image worker
			// thread).
            --pool_size;
		}
	}
    else if (pool_size == 1)  // Disable if only 1
	{
        pool_size = 0;
	}

	sImageThreads = pool_size;
	
	LL_INFOS() << "Initializing with " << sImageThreads << " image decode threads" << LL_ENDL;
	
	for (U32 i = 0; i < pool_size; ++i)
	{
		mThreadPool.push_back(std::make_unique<PoolWorkerThread>(fmt::format("Image Decode Thread {}", i)));
		mThreadPool[i]->start();
	}
}

//virtual 
LLImageDecodeThread::~LLImageDecodeThread()
{
    if (sImageThreads > 0) 
	{
        LL_INFOS() << "Requests failed to queue to pool: " << mFailedRequests << LL_ENDL;
	}
	delete mCreationMutex ;
}

// MAIN THREAD
// virtual
S32 LLImageDecodeThread::update(F32 max_time_ms)
{
	if (mCreationListSize > 0)
	{
		LLMutexLock lock(mCreationMutex);
		for (creation_list_t::iterator iter = mCreationList.begin();
			 iter != mCreationList.end(); ++iter)
		{
			creation_info& info = *iter;
		// ImageRequest* req = new ImageRequest(info.handle, info.image,
		//				     info.priority, info.discard, info.needs_aux,
		//				     info.responder);
			ImageRequest* req = new ImageRequest(info.handle, info.image,
				info.priority, info.discard, info.needs_aux,
				info.responder, this);

			bool res = addRequest(req);
			if (!res)
			{
				LL_WARNS() << "request added after LLLFSThread::cleanupClass()" << LL_ENDL;
				return 0;
			}
		}
		mCreationList.clear();
		mCreationListSize = 0;
	}
	S32 res = LLQueuedThread::update(max_time_ms);
	return res;
}

LLImageDecodeThread::handle_t LLImageDecodeThread::decodeImage(LLImageFormatted* image, 
	U32 priority, S32 discard, BOOL needs_aux, Responder* responder)
{
	handle_t handle = generateHandle();
	// If we have a thread pool dispatch this directly.
	// Note: addRequest could cause the handling to take place on the fetch thread, this is unlikely to be an issue. 
	// if this is an actual problem we move the fallback to here and place the unfulfilled request into the legacy queue
    if (sImageThreads > 0)
	{
		ImageRequest* req = new ImageRequest(handle, image,
			priority, discard, needs_aux,
			responder, this);
		bool res = addRequest(req);
		if (!res)
		{
			LL_WARNS() << "Decode request not added because we are exiting." << LL_ENDL;
			return 0;
		}
	}
	else
	{
		LLMutexLock lock(mCreationMutex);
		mCreationList.push_back(creation_info(handle, image, priority, discard, needs_aux, responder));
		mCreationListSize = mCreationList.size();
	}
	return handle;
}

// Used by unit test only
// Returns the size of the mutex guarded list as an indication of sanity
S32 LLImageDecodeThread::tut_size()
{
	LLMutexLock lock(mCreationMutex);
	S32 res = mCreationList.size();
	return res;
}

//----------------------------------------------------------------------------

LLImageDecodeThread::ImageRequest::ImageRequest(handle_t handle, LLImageFormatted* image, 
												U32 priority, S32 discard, BOOL needs_aux,
												LLImageDecodeThread::Responder* responder,
												LLImageDecodeThread *aQueue)
	: LLQueuedThread::QueuedRequest(handle, priority, FLAG_AUTO_COMPLETE),
	  mFormattedImage(image),
	  mDiscardLevel(discard),
	  mNeedsAux(needs_aux),
	  mDecodedRaw(FALSE),
	  mDecodedAux(FALSE),
	  mResponder(responder),
      mQueue( aQueue )
{
    if (sImageThreads > 0)
		mFlags |= FLAG_ASYNC;
}

LLImageDecodeThread::ImageRequest::~ImageRequest()
{
	mDecodedImageRaw = NULL;
	mDecodedImageAux = NULL;
	mFormattedImage = NULL;
}

//----------------------------------------------------------------------------


// Returns true when done, whether or not decode was successful.
bool LLImageDecodeThread::ImageRequest::processRequest()
{
	// If not async, decode using this thread
	if ((mFlags & FLAG_ASYNC) == 0)
		return processRequestIntern();

	// Try to dispatch to a new thread, if this isn't possible decode on this thread
	if (!mQueue || !mQueue->enqueRequest(this))
		return processRequestIntern();
	return true;
}

bool LLImageDecodeThread::ImageRequest::processRequestIntern()
{
	F32 decode_time_slice = .1f;
	if(mFlags & FLAG_ASYNC)
	{
		decode_time_slice = 10.0f;// long time out as this is not an issue with async
	}

	bool done = true;
	if (!mDecodedRaw && mFormattedImage.notNull())
	{
		// Decode primary channels
		if (mDecodedImageRaw.isNull())
		{
			// parse formatted header
			if (!mFormattedImage->updateData())
			{
				return true; // done (failed)
			}
			if ((mFormattedImage->getWidth() * mFormattedImage->getHeight() * mFormattedImage->getComponents()) <= 0)
			{
				return true; // done (failed)
			}
			if (mDiscardLevel >= 0)
			{
				mFormattedImage->setDiscardLevel(mDiscardLevel);
			}
			mDecodedImageRaw = new LLImageRaw(mFormattedImage->getWidth(),
											  mFormattedImage->getHeight(),
											  mFormattedImage->getComponents());
		}

		if( mDecodedImageRaw->getData() )
			done = mFormattedImage->decode(mDecodedImageRaw, decode_time_slice); // 1ms
		else
		{
			LL_WARNS() << "No memory for LLImageRaw of size " << (U32)mFormattedImage->getWidth() << "x" << (U32)mFormattedImage->getHeight() << "x"
					   << (U32)mFormattedImage->getComponents() << LL_ENDL;
			done = false;
		}
		
		// some decoders are removing data when task is complete and there were errors
		mDecodedRaw = done && mDecodedImageRaw->getData();
	}
	if (done && mNeedsAux && !mDecodedAux && mFormattedImage.notNull())
	{
		// Decode aux channel
		if (!mDecodedImageAux)
		{
			mDecodedImageAux = new LLImageRaw(mFormattedImage->getWidth(),
											  mFormattedImage->getHeight(),
											  1);
		}
		done = mFormattedImage->decodeChannels(mDecodedImageAux, decode_time_slice, 4, 4); // 1ms
		mDecodedAux = done && mDecodedImageAux->getData();
	}

	if(!done)
	{
		LL_WARNS("ImageDecode") << "Image decoding failed to complete with time slice=" << decode_time_slice << LL_ENDL;
	}

	if (mFlags & FLAG_ASYNC)
	{
		setStatus(STATUS_COMPLETE);
		finishRequest(true);
		// always autocomplete
		mQueue->completeRequest(mHashKey);
	}

	return done;
}

void LLImageDecodeThread::ImageRequest::finishRequest(bool completed)
{
	if (mResponder.notNull())
	{
		bool success = completed && mDecodedRaw && (!mNeedsAux || mDecodedAux);
		mResponder->completed(success, mDecodedImageRaw, mDecodedImageAux);
	}
	// Will automatically be deleted
}

// Used by unit test only
// Checks that a responder exists for this instance so that something can happen when completion is reached
bool LLImageDecodeThread::ImageRequest::tut_isOK()
{
	return mResponder.notNull();
}

bool LLImageDecodeThread::enqueRequest(ImageRequest * req)
{
    for (U32 i = 0, count = mThreadPool.size(); i < count; ++i)
    {
        if (mLastPoolAllocation >= count)
        {
            mLastPoolAllocation = 0;
        }
        auto& thread = mThreadPool[mLastPoolAllocation++];
        if (thread->setRequest(req))
        {
            return true;
        }
    }
    ++mFailedRequests;
	return false;
}
