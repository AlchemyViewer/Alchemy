/**
 * @file llcefsurfacereceiver.h
 * @brief Viewer-side mach-port receiver for CEF accelerated-paint IOSurfaces (macOS).
 *
 * CEF's accelerated-paint IOSurface is shared between its GPU and browser
 * processes via mach ports, NOT a global IOSurfaceID, so the browser-side plugin
 * cannot hand it to the viewer through the socket/LLSD channel (which carries no
 * mach rights) and a cross-process IOSurfaceLookup(id) fails. Instead the plugin
 * sends an IOSurfaceCreateMachPort() right over a mach channel rendezvous'd
 * through the bootstrap server under a per-viewer name; this singleton owns the
 * receive end and demuxes incoming surfaces by the per-media accel id.
 *
 * Non-macOS builds get a stub (those platforms share via NT handle / dma-buf).
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

#ifndef LL_LLCEFSURFACERECEIVER_H
#define LL_LLCEFSURFACERECEIVER_H

// Process-global receiver for accelerated-paint frames handed over by the CEF
// media plugin(s) over a side channel separate from the (TCP) LLSD message pipe.
// The rendezvous name is derived from this (viewer) process id, which the plugin
// already learns from the "init" message's host_pid, so no extra handshake field
// is needed. macOS uses a mach port carrying an IOSurface; Linux uses an AF_UNIX
// datagram socket carrying the dma-buf fds as SCM_RIGHTS ancillary data (a TCP
// socket cannot carry fds, and a dma-buf fd cannot be reopened via /proc).
class LLCEFSurfaceReceiver
{
public:
    static LLCEFSurfaceReceiver& instance();

    // Register the receive endpoint if not already (idempotent). MUST be called
    // independently of frame delivery: the plugin only starts producing once the
    // endpoint exists, so waiting for the first frame to register would deadlock
    // (no endpoint -> no frames -> never registered). No-op on non-mac/non-Linux.
    void ensureStarted();

    // macOS: drain all pending surface messages (non-blocking), keep only the
    // newest surface per accel id, then hand off the newest for `accel_id`.
    // Returns a +1-retained IOSurfaceRef (opaque pointer; caller takes ownership
    // and must CFRelease / hand to code that does) or nullptr if no new frame.
    void* takeLatest(int accel_id);

    // Linux: a dma-buf frame received via SCM_RIGHTS. The fds are open in THIS
    // (viewer) process and owned by whoever takes the frame, which must close them
    // (closeFds()) once imported. Pure data so it can cross to the interop without
    // pulling unistd.h into this header.
    struct DmabufFrame
    {
        int                plane_count = 0;
        int                fd[4]       = { -1, -1, -1, -1 };
        unsigned int       stride[4]   = { 0, 0, 0, 0 };
        unsigned long long offset[4]   = { 0, 0, 0, 0 };
        unsigned long long modifier    = 0;
        int                width       = 0;
        int                height      = 0;
        int                format      = 0;
        void closeFds();   // defined in the .cpp (Linux closes; elsewhere a no-op)
    };

    // Linux: drain pending dma-buf frames (non-blocking), keep only the newest per
    // accel id, then hand off the newest for `accel_id`. Returns true and fills
    // `out` (caller owns out.fd[] and must closeFds()), or false if no new frame.
    // Always false on non-Linux.
    bool takeLatestDmabuf(int accel_id, DmabufFrame& out);

private:
    LLCEFSurfaceReceiver() = default;
};

#endif // LL_LLCEFSURFACERECEIVER_H
