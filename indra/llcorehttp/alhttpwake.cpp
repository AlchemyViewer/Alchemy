/**
 * @file alhttpwake.cpp
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

#include "alhttpwake.h"

#if LL_WINDOWS
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace LLCore
{

namespace
{

void close_socket(curl_socket_t & sock)
{
    if (CURL_SOCKET_BAD != sock)
    {
#if LL_WINDOWS
        closesocket(sock);
#else
        ::close(sock);
#endif
        sock = CURL_SOCKET_BAD;
    }
}

bool set_nonblocking(curl_socket_t sock)
{
#if LL_WINDOWS
    u_long on(1);
    return 0 == ioctlsocket(sock, FIONBIO, &on);
#else
    const int flags(fcntl(sock, F_GETFL));
    return -1 != flags
        && -1 != fcntl(sock, F_SETFL, flags | O_NONBLOCK)
        && -1 != fcntl(sock, F_SETFD, FD_CLOEXEC);
#endif
}

#if LL_WINDOWS
// Two TCP sockets connected over the loopback. The listener takes one
// connection and is closed. Another process could connect to it first, so
// the accepted socket has to be the writer's peer.
bool make_loopback_pair(curl_socket_t & reader, curl_socket_t & writer)
{
    SOCKET listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (INVALID_SOCKET == listener)
    {
        return false;
    }
    BOOL exclusive(TRUE);
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int addr_len(sizeof(addr));
    bool ok(0 == bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))
            && 0 == getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &addr_len)
            && 0 == listen(listener, 1));
    if (ok)
    {
        writer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ok = INVALID_SOCKET != writer && 0 == connect(writer, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    }
    if (ok)
    {
        reader = accept(listener, NULL, NULL);
        ok = INVALID_SOCKET != reader;
    }
    closesocket(listener);

    if (ok)
    {
        sockaddr_in writer_name = {};
        sockaddr_in reader_peer = {};
        int writer_len(sizeof(writer_name));
        int reader_len(sizeof(reader_peer));
        ok = 0 == getsockname(writer, reinterpret_cast<sockaddr *>(&writer_name), &writer_len)
            && 0 == getpeername(reader, reinterpret_cast<sockaddr *>(&reader_peer), &reader_len)
            && writer_name.sin_port == reader_peer.sin_port
            && writer_name.sin_addr.s_addr == reader_peer.sin_addr.s_addr;
    }
    if (ok)
    {
        BOOL nodelay(TRUE);
        setsockopt(writer, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));
    }
    return ok;
}
#endif

}  // end anonymous namespace


ALHttpWake::~ALHttpWake()
{
    close();
}


bool ALHttpWake::open()
{
    close();

#if LL_WINDOWS
    bool ok(make_loopback_pair(mReader, mWriter));
#else
    int fds[2];
    bool ok(0 == pipe(fds));
    if (ok)
    {
        mReader = fds[0];
        mWriter = fds[1];
    }
#endif
    ok = ok && set_nonblocking(mReader) && set_nonblocking(mWriter);
    if (! ok)
    {
        close();
    }
    return ok;
}


void ALHttpWake::close()
{
    close_socket(mReader);
    close_socket(mWriter);
    mSignalled = false;
}


void ALHttpWake::signal()
{
    if (CURL_SOCKET_BAD == mWriter || mSignalled.exchange(true))
    {
        return;
    }
    // A full buffer already holds a byte to wake on.
    const char byte(1);
#if LL_WINDOWS
    send(mWriter, &byte, 1, 0);
#else
    while (-1 == write(mWriter, &byte, 1) && EINTR == errno)
    {
    }
#endif
}


void ALHttpWake::drain()
{
    if (CURL_SOCKET_BAD == mReader)
    {
        return;
    }
    // Cleared before reading, so that a signal from here on writes again.
    mSignalled = false;
    char bytes[64];
#if LL_WINDOWS
    while (recv(mReader, bytes, sizeof(bytes), 0) > 0)
    {
    }
#else
    for (;;)
    {
        const ssize_t got(read(mReader, bytes, sizeof(bytes)));
        if (got > 0 || (-1 == got && EINTR == errno))
        {
            continue;
        }
        break;
    }
#endif
}

}  // end namespace LLCore
