/**
 * @file llcefsurfacereceiver.cpp
 * @brief Viewer-side mach-port receiver for CEF accelerated-paint IOSurfaces.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "llviewerprecompiledheaders.h"

#include "llcefsurfacereceiver.h"

// static
LLCEFSurfaceReceiver& LLCEFSurfaceReceiver::instance()
{
    static LLCEFSurfaceReceiver sInstance;
    return sInstance;
}

#if LL_DARWIN

#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <IOSurface/IOSurface.h>
#include <CoreFoundation/CoreFoundation.h>

#include <map>
#include <unistd.h>

// Shared wire format with the producer (media_plugin_cef.cpp). MUST stay in sync.
// A complex message carrying one IOSurface mach port plus inline frame metadata.
typedef struct
{
    mach_msg_header_t          header;
    mach_msg_body_t            body;       // descriptor count = 1
    mach_msg_port_descriptor_t surface;    // the IOSurfaceCreateMachPort() right
    int32_t                    accel_id;
    int32_t                    width;
    int32_t                    height;
    int32_t                    format;
} CefSurfaceSendMsg;

// Receive needs room for the kernel-appended trailer.
typedef struct
{
    CefSurfaceSendMsg     msg;
    mach_msg_trailer_t    trailer;
} CefSurfaceRecvMsg;

namespace
{
    struct Receiver
    {
        mach_port_t port = MACH_PORT_NULL;   // our receive right (also the service)
        bool        started = false;
        bool        failed = false;          // bootstrap registration refused; give up
        // Newest pending IOSurface mach port per accel id (a send right we own
        // until it is looked up or superseded).
        std::map<int, mach_port_t> latest;

        // Allocate the receive right and register it with the bootstrap server
        // under the per-viewer name the plugin derives from host_pid.
        bool ensureStarted()
        {
            if (started) return true;
            if (failed)  return false;

            kern_return_t kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
            if (kr != KERN_SUCCESS)
            {
                LL_WARNS("Media") << "accel surface receiver: mach_port_allocate failed (" << kr << ")" << LL_ENDL;
                failed = true;
                return false;
            }
            // A send right (same name) for bootstrap to hand to look_up callers.
            mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND);

            char name[128];
            snprintf(name, sizeof(name), "org.alchemyviewer.cefsurface.%d", (int)getpid());

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            kr = bootstrap_register(bootstrap_port, name, port);
#pragma clang diagnostic pop
            if (kr != KERN_SUCCESS)
            {
                LL_WARNS("Media") << "accel surface receiver: bootstrap_register(" << name
                                  << ") failed (" << kr << "); accelerated paint cannot share the surface" << LL_ENDL;
                mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
                port = MACH_PORT_NULL;
                failed = true;
                return false;
            }

            LL_INFOS("Media") << "accel surface receiver: registered " << name << LL_ENDL;
            started = true;
            return true;
        }

        // Non-blocking drain: pull every queued message, keeping only the newest
        // surface port per accel id (deallocating superseded ones).
        void drain()
        {
            while (true)
            {
                CefSurfaceRecvMsg rcv;
                kern_return_t kr = mach_msg(&rcv.msg.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                                            0, sizeof(rcv), port, 0, MACH_PORT_NULL);
                if (kr != KERN_SUCCESS)
                {
                    break;   // MACH_RCV_TIMED_OUT (empty) or error - stop
                }
                if (!(rcv.msg.header.msgh_bits & MACH_MSGH_BITS_COMPLEX) ||
                    rcv.msg.body.msgh_descriptor_count < 1)
                {
                    continue;   // malformed; nothing to release
                }

                mach_port_t surf_port = rcv.msg.surface.name;
                int id = rcv.msg.accel_id;

                auto it = latest.find(id);
                if (it != latest.end() && it->second != MACH_PORT_NULL)
                {
                    mach_port_deallocate(mach_task_self(), it->second);   // drop superseded
                }
                latest[id] = surf_port;
            }
        }
    };

    Receiver& rcv()
    {
        static Receiver r;
        return r;
    }
}

void LLCEFSurfaceReceiver::ensureStarted()
{
    rcv().ensureStarted();
}

void* LLCEFSurfaceReceiver::takeLatest(int accel_id)
{
    Receiver& r = rcv();
    if (!r.ensureStarted())
    {
        return nullptr;
    }
    r.drain();

    auto it = r.latest.find(accel_id);
    if (it == r.latest.end() || it->second == MACH_PORT_NULL)
    {
        return nullptr;   // no new frame for this media
    }

    mach_port_t surf_port = it->second;
    r.latest.erase(it);

    IOSurfaceRef surf = IOSurfaceLookupFromMachPort(surf_port);
    mach_port_deallocate(mach_task_self(), surf_port);   // release our send right
    return surf;   // +1 retained (or null); ownership transferred to caller
}

// macOS shares the frame as an IOSurface (above), not as dma-buf fds.
void LLCEFSurfaceReceiver::DmabufFrame::closeFds() {}
bool LLCEFSurfaceReceiver::takeLatestDmabuf(int, DmabufFrame&) { return false; }

#elif LL_LINUX  // dma-buf fds handed over via an AF_UNIX datagram + SCM_RIGHTS

#include <map>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace
{
    // Wire header shared with the producer (media_plugin_cef.cpp). MUST stay in
    // sync. The dma-buf plane fds ride alongside as SCM_RIGHTS ancillary data.
    struct CefDmabufMsg
    {
        int32_t  accel_id;
        int32_t  plane_count;
        int32_t  width;
        int32_t  height;
        int32_t  format;
        uint32_t stride[4];
        uint64_t offset[4];
        uint64_t modifier;
    };

    // Abstract-namespace socket address from the viewer pid (the Linux analog of
    // the macOS bootstrap name org.alchemyviewer.cefsurface.<pid>). Abstract
    // sockets need no filesystem entry and vanish when the socket closes.
    socklen_t makeAddr(struct sockaddr_un& addr, int pid)
    {
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        int n = snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1,
                         "org.alchemyviewer.cefsurface.%d", pid);   // [0] stays NUL = abstract
        return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + n);
    }

    struct Receiver
    {
        int  sock = -1;
        bool started = false;
        bool failed = false;
        // Newest pending frame per accel id (its fds are open + owned by us until
        // taken or superseded).
        std::map<int, LLCEFSurfaceReceiver::DmabufFrame> latest;

        bool ensureStarted()
        {
            if (started) return true;
            if (failed)  return false;

            sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
            if (sock < 0) { failed = true; return false; }

            struct sockaddr_un addr;
            socklen_t len = makeAddr(addr, (int)getpid());
            if (bind(sock, (struct sockaddr*)&addr, len) != 0)
            {
                LL_WARNS("Media") << "accel surface receiver: bind failed errno=" << errno
                                  << "; accelerated paint cannot share the frame" << LL_ENDL;
                ::close(sock); sock = -1; failed = true; return false;
            }
            int rcvbuf = 4 * 1024 * 1024;   // hold a few queued frames across a slow tick
            setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

            LL_INFOS("Media") << "accel surface receiver: listening (abstract org.alchemyviewer.cefsurface."
                              << (int)getpid() << ")" << LL_ENDL;
            started = true;
            return true;
        }

        // Non-blocking drain: pull every queued datagram, keeping only the newest
        // frame per accel id (closing the fds of superseded / malformed ones).
        void drain()
        {
            if (sock < 0) return;
            while (true)
            {
                CefDmabufMsg hdr;
                struct iovec iov = { &hdr, sizeof(hdr) };
                char cbuf[CMSG_SPACE(sizeof(int) * 4)];
                struct msghdr msg;
                memset(&msg, 0, sizeof(msg));
                msg.msg_iov = &iov;
                msg.msg_iovlen = 1;
                msg.msg_control = cbuf;
                msg.msg_controllen = sizeof(cbuf);

                ssize_t r = recvmsg(sock, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
                if (r < 0) break;   // EAGAIN (empty) or error - stop

                // Collect any fds first so a malformed frame never leaks them.
                int fds[4]; int nfds = 0;
                for (struct cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
                {
                    if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
                    {
                        int cnt = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
                        int* p = (int*)CMSG_DATA(c);
                        for (int i = 0; i < cnt; ++i)
                            (nfds < 4) ? (void)(fds[nfds++] = p[i]) : (void)::close(p[i]);
                    }
                }

                bool bad = (r != (ssize_t)sizeof(hdr)) ||
                           (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
                           nfds <= 0 || nfds != hdr.plane_count;
                if (bad)
                {
                    for (int i = 0; i < nfds; ++i) ::close(fds[i]);
                    continue;
                }

                LLCEFSurfaceReceiver::DmabufFrame f;
                f.plane_count = nfds;
                f.width = hdr.width; f.height = hdr.height; f.format = hdr.format;
                f.modifier = hdr.modifier;
                for (int i = 0; i < nfds; ++i)
                {
                    f.fd[i] = fds[i];
                    f.stride[i] = hdr.stride[i];
                    f.offset[i] = hdr.offset[i];
                }
                auto it = latest.find(hdr.accel_id);
                if (it != latest.end()) it->second.closeFds();   // drop superseded
                latest[hdr.accel_id] = f;
            }
        }
    };

    Receiver& rcv() { static Receiver r; return r; }
}

void LLCEFSurfaceReceiver::DmabufFrame::closeFds()
{
    for (int i = 0; i < plane_count; ++i)
        if (fd[i] >= 0) { ::close(fd[i]); fd[i] = -1; }
}

void LLCEFSurfaceReceiver::ensureStarted() { rcv().ensureStarted(); }

void* LLCEFSurfaceReceiver::takeLatest(int) { return nullptr; }   // macOS-only path

bool LLCEFSurfaceReceiver::takeLatestDmabuf(int accel_id, DmabufFrame& out)
{
    Receiver& r = rcv();
    if (!r.ensureStarted())
    {
        return false;
    }
    r.drain();

    auto it = r.latest.find(accel_id);
    if (it == r.latest.end() || it->second.plane_count <= 0)
    {
        return false;   // no new frame for this media
    }
    out = it->second;       // transfer fd ownership to the caller
    r.latest.erase(it);
    return true;
}

#else  // other platforms: no side-channel handoff (Windows shares via NT handle)

void LLCEFSurfaceReceiver::ensureStarted() {}
void* LLCEFSurfaceReceiver::takeLatest(int) { return nullptr; }
void LLCEFSurfaceReceiver::DmabufFrame::closeFds() {}
bool LLCEFSurfaceReceiver::takeLatestDmabuf(int, DmabufFrame&) { return false; }

#endif
