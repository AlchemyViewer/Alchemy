/**
 * @file llimage_test.cpp
 * @brief Unit tests for the locks images are read and written under
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llimage.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <type_traits>

namespace
{
    using namespace std::chrono_literals;

    // A copied guard would unlock its images once per copy.
    static_assert(!std::is_copy_constructible_v<LLImageDataCopyLock> && !std::is_copy_assignable_v<LLImageDataCopyLock>);
    static_assert(!std::is_copy_constructible_v<LLImageDataLockPair> && !std::is_copy_assignable_v<LLImageDataLockPair>);

    // Runs a body on a thread of its own and says whether it has returned. A body
    // still blocked when the worker goes out of scope is left to finish without it:
    // a deadlock the test has already reported, or a probe kept out of an image the
    // test still holds. The thread owns the flag it sets, and a probe owns the image
    // it locks, so both outlive the worker.
    class Worker
    {
    public:
        template <typename FUNC>
        explicit Worker(FUNC&& body)
        : mDone(std::make_shared<std::atomic<bool>>(false))
        , mThread([done = mDone, body = std::forward<FUNC>(body)]() mutable
            {
                body();
                *done = true;
            })
        {
        }

        ~Worker()
        {
            if (*mDone)
            {
                mThread.join();
            }
            else
            {
                mThread.detach();
            }
        }

        bool finishesWithin(std::chrono::milliseconds limit)
        {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (!*mDone)
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    return false;
                }
                std::this_thread::sleep_for(1ms);
            }
            return true;
        }

    private:
        std::shared_ptr<std::atomic<bool>> mDone;
        std::thread mThread;
    };

    // Long enough that a thread which could run has run; a body that has not
    // returned by then is blocked.
    constexpr auto BLOCKED = 50ms;
    // Long enough that a thread which was released has finished.
    constexpr auto RELEASED = 5s;

    // Whether another thread is kept from reading the image right now. The reader
    // holds its own reference, since a blocked one outlives the call.
    bool readersExcluded(LLPointer<LLImageRaw> image)
    {
        Worker reader([image]()
            {
                LLImageDataSharedLock lock(image);
            });
        return !reader.finishesWithin(BLOCKED);
    }

    // Whether another thread is kept from writing the image right now.
    bool writersExcluded(LLPointer<LLImageRaw> image)
    {
        Worker writer([image]()
            {
                LLImageDataLock lock(image);
            });
        return !writer.finishesWithin(BLOCKED);
    }

    TEST_CASE("llimage: copies each way between two images do not deadlock", "[llimage]")
    {
        // A copy reads its source and writes its destination. Locked in argument order,
        // a thread copying a into b and another copying b into a would each hold the
        // image the other waits for.
        LLPointer<LLImageRaw> a = new LLImageRaw(8, 8, 4);
        LLPointer<LLImageRaw> b = new LLImageRaw(8, 8, 4);
        constexpr int COPIES = 20000;
        Worker forward([a, b]()
            {
                for (int i = 0; i < COPIES; ++i)
                {
                    b->copy(a);
                }
            });
        Worker backward([a, b]()
            {
                for (int i = 0; i < COPIES; ++i)
                {
                    a->copy(b);
                }
            });
        UNSCOPED_INFO("copies the opposite way round deadlocked");
        CHECK(forward.finishesWithin(RELEASED));
        CHECK(backward.finishesWithin(RELEASED));
    }

    TEST_CASE("llimage: a copy lock reads one image and writes the other in either address order", "[llimage]")
    {
        LLPointer<LLImageRaw> a = new LLImageRaw(8, 8, 4);
        LLPointer<LLImageRaw> b = new LLImageRaw(8, 8, 4);
        auto check = [](const LLPointer<LLImageRaw>& from, const LLPointer<LLImageRaw>& to)
        {
            {
                LLImageDataCopyLock lock(from, to);
                CHECK_FALSE(readersExcluded(from));
                CHECK(writersExcluded(from));
                CHECK(readersExcluded(to));
            }
            CHECK_FALSE(writersExcluded(from));
            CHECK_FALSE(writersExcluded(to));
        };
        // The lower address is locked first, so one of the two calls swaps the sides.
        check(a, b);
        check(b, a);
    }

    TEST_CASE("llimage: an image passed as both sides is locked once and exclusively", "[llimage]")
    {
        // Locked once a side, the write would wait on this thread's own read.
        LLPointer<LLImageRaw> image = new LLImageRaw(8, 8, 4);
        {
            LLImageDataCopyLock lock(image, image);
            CHECK(readersExcluded(image));
        }
        UNSCOPED_INFO("the image stayed locked");
        CHECK_FALSE(writersExcluded(image));
    }

    TEST_CASE("llimage: a null side leaves the other locked alone", "[llimage]")
    {
        LLPointer<LLImageRaw> image = new LLImageRaw(8, 8, 4);
        {
            LLImageDataCopyLock lock(nullptr, image);
            CHECK(readersExcluded(image));
        }
        {
            LLImageDataCopyLock lock(image, nullptr);
            CHECK_FALSE(readersExcluded(image));
            CHECK(writersExcluded(image));
        }
        CHECK_FALSE(writersExcluded(image));
    }
}
