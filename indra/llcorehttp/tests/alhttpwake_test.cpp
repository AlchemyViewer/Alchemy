/**
 * @file alhttpwake_test.cpp
 * @brief Tests of the HTTP worker's wake socket and the request queue's use of it.
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

#include "alhttpwake.h"
#include "_httpoperation.h"
#include "_httprequestqueue.h"
#include "lltimer.h"

#include <catch2/catch_test_macros.hpp>

#include <curl/curl.h>

#include <atomic>
#include <future>

#if ! LL_WINDOWS
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#endif

using namespace LLCore;

namespace
{
    // Whether the wake's reading end turns readable within timeout_ms.
    bool readable(const ALHttpWake & wake, int timeout_ms)
    {
        pollfd entry = {};
        entry.fd = wake.readSocket();
        entry.events = POLLIN;
#if LL_WINDOWS
        return WSAPoll(&entry, 1, timeout_ms) > 0;
#else
        return poll(&entry, 1, timeout_ms) > 0;
#endif
    }

    // The bytes waiting on the wake's reading end.
    unsigned long waiting(const ALHttpWake & wake)
    {
#if LL_WINDOWS
        u_long bytes(0);
        ioctlsocket(wake.readSocket(), FIONREAD, &bytes);
        return bytes;
#else
        int bytes(0);
        ioctl(wake.readSocket(), FIONREAD, &bytes);
        return (unsigned long)bytes;
#endif
    }

    struct WakeFixture
    {
        WakeFixture()
        {
            // Windows' sockets want WSAStartup(), which libcurl's global init makes.
            curl_global_init(CURL_GLOBAL_ALL);
            opened = wake.open();
        }

        ~WakeFixture()
        {
            wake.close();
            curl_global_cleanup();
        }

        ALHttpWake wake;
        bool       opened = false;
    };

    TEST_CASE_METHOD(WakeFixture, "alhttpwake: a signal makes the reading end readable until it is drained", "[alhttpwake]")
    {
        REQUIRE(opened);
        CHECK_FALSE(readable(wake, 0));

        wake.signal();
        CHECK(readable(wake, 5000));

        wake.drain();
        CHECK_FALSE(readable(wake, 0));

        // And it signals again after a drain
        wake.signal();
        CHECK(readable(wake, 5000));
    }

    TEST_CASE_METHOD(WakeFixture, "alhttpwake: signals before a drain write one byte between them", "[alhttpwake]")
    {
        REQUIRE(opened);
        wake.signal();
        wake.signal();
        wake.signal();
        REQUIRE(readable(wake, 5000));
        CHECK(waiting(wake) == 1);
    }

    TEST_CASE_METHOD(WakeFixture, "alhttpwake: a signal racing a drain is never lost", "[alhttpwake]")
    {
        // Another thread signals as fast as it can while this one drains
        // whenever the wake is readable. A signal that lands inside a drain
        // must not leave the wake marked signalled with nothing to read:
        // then no later signal would make it readable again.
        REQUIRE(opened);
        std::atomic<bool> stop{ false };
        auto signaller = std::async(std::launch::async, [&]()
            {
                while (! stop.load())
                {
                    wake.signal();
                }
            });
        LLTimer racing;
        while (racing.getElapsedTimeF64() < 1.0)
        {
            if (readable(wake, 0))
            {
                wake.drain();
            }
        }
        stop = true;
        signaller.get();

        if (readable(wake, 100))
        {
            wake.drain();
        }
        wake.signal();
        CHECK(readable(wake, 5000));
    }

    TEST_CASE_METHOD(WakeFixture, "alhttpwake: a child process does not inherit the wake", "[alhttpwake]")
    {
        REQUIRE(opened);
#if LL_WINDOWS
        DWORD flags(0);
        REQUIRE(GetHandleInformation(reinterpret_cast<HANDLE>(wake.readSocket()), &flags));
        CHECK(0 == (flags & HANDLE_FLAG_INHERIT));
#else
        const int flags(fcntl(wake.readSocket(), F_GETFD));
        REQUIRE(-1 != flags);
        CHECK(0 != (flags & FD_CLOEXEC));
#endif
    }

    TEST_CASE_METHOD(WakeFixture, "alhttpwake: a closed wake signals nothing and can be opened again", "[alhttpwake]")
    {
        REQUIRE(opened);
        wake.close();
        CHECK_FALSE(wake.isOpen());
        wake.signal();
        wake.drain();

        REQUIRE(wake.open());
        CHECK_FALSE(readable(wake, 0));
        wake.signal();
        CHECK(readable(wake, 5000));
    }

    TEST_CASE_METHOD(WakeFixture, "alhttpwake: the request queue signals a request on an empty queue and a stop", "[alhttpwake]")
    {
        REQUIRE(opened);
        HttpRequestQueue::init();
        HttpRequestQueue * queue(HttpRequestQueue::instanceOf());
        queue->setWake(&wake);

        // The first request into an empty queue signals...
        queue->addOp(HttpOperation::ptr_t(new HttpOpNull()));
        CHECK(readable(wake, 5000));
        wake.drain();

        // ...and the next, finding one already queued, does not
        queue->addOp(HttpOperation::ptr_t(new HttpOpNull()));
        CHECK_FALSE(readable(wake, 0));

        {
            HttpRequestQueue::OpContainer ops;
            queue->fetchAll(false, ops);
            CHECK(ops.size() == 2);
        }

        // Emptied, it signals for a request again
        queue->addOp(HttpOperation::ptr_t(new HttpOpNull()));
        CHECK(readable(wake, 5000));
        wake.drain();

        // A stop signals however full the queue is
        queue->stopQueue();
        CHECK(readable(wake, 5000));
        wake.drain();

        // Once the wake is taken away, nothing signals it
        queue->setWake(NULL);
        queue->stopQueue();
        CHECK_FALSE(readable(wake, 0));

        {
            HttpRequestQueue::OpContainer ops;
            queue->fetchAll(false, ops);
        }
        HttpRequestQueue::term();
    }
}
