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

#else  // non-macOS: no mach handoff (NT handle / dma-buf paths handle sharing)

void LLCEFSurfaceReceiver::ensureStarted() {}
void* LLCEFSurfaceReceiver::takeLatest(int) { return nullptr; }

#endif
