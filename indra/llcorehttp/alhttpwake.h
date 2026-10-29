/**
 * @file alhttpwake.h
 * @brief A socket the HTTP worker's wait polls with its transfers', which
 *        another thread makes readable to end the wait.
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

#ifndef AL_HTTP_WAKE_H
#define AL_HTTP_WAKE_H

#include "linden_common.h"      // Modifies curl/curl.h interfaces

#include <curl/curl.h>

#include <atomic>

namespace LLCore
{

// The HTTP worker waits on its transfers' sockets (HttpLibcurl::wait()). A
// request queued meanwhile has to end that wait, and only a socket can end a
// wait on sockets. ALHttpWake is a connected pair: the worker polls the
// reading end, and signal(), from any thread, writes a byte to the other.
// One byte does for any number of signals before the worker drains it, so a
// signal writes only when none is pending.
//
// libcurl 7.54 has neither curl_multi_poll() nor curl_multi_wakeup(), which
// are this, inside libcurl, from 7.68 on.
//
// Windows has no socketpair(): there the pair is two TCP sockets connected
// over the loopback, and each is checked to be the other's peer. Elsewhere
// it is a pipe.
class ALHttpWake
{
public:
    ALHttpWake() = default;
    ~ALHttpWake();

    ALHttpWake(const ALHttpWake&) = delete;
    ALHttpWake& operator=(const ALHttpWake&) = delete;

    // Makes the pair, closing any it had. False where it could not; the
    // worker then polls as it did before the wait.
    bool open();
    void close();
    bool isOpen() const { return mReader != CURL_SOCKET_BAD; }

    // The end to poll for reading.
    curl_socket_t readSocket() const { return mReader; }

    // Makes the reading end readable, unless a signal is already pending.
    // Any thread.
    void signal();

    // Empties the reading end, so that the next poll waits again. The
    // polling thread only, and only before it looks for the work the
    // signals were for: a signal() racing it writes nothing.
    void drain();

private:
    curl_socket_t     mReader = CURL_SOCKET_BAD;
    curl_socket_t     mWriter = CURL_SOCKET_BAD;
    std::atomic<bool> mSignalled{ false };
};

}  // end namespace LLCore

#endif  // AL_HTTP_WAKE_H
