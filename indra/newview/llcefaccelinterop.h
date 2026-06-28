/**
 * @file llcefaccelinterop.h
 * @brief Viewer-side consumer of the CEF accelerated-paint shared texture.
 *
 * Opens the plugin's keyed-mutex shared texture (delivered once per size as a
 * duplicated NT handle), copies each frame into a viewer-owned texture under the
 * mutex, and blits that into a media GL texture - no CPU round-trip. Windows
 * only (D3D11 + WGL_NV_DX_interop2); a stub elsewhere.
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

#ifndef LL_LLCEFACCELINTEROP_H
#define LL_LLCEFACCELINTEROP_H

// Bridges the plugin's shared GPU texture to a viewer media GL texture with no
// CPU copy. All calls must be made on a thread holding the viewer's GL context.
class LLCEFAccelInterop
{
public:
    LLCEFAccelInterop() = default;
    ~LLCEFAccelInterop();

    // Create the D3D11 device + WGL interop device. False if unsupported (caller
    // should fall back to the CPU paint path).
    bool init();
    void shutdown();
    bool valid() const { return mValid; }

    // (Re)bind the plugin's shared frame. `handle` is the platform shared-texture
    // handle on Windows (NT handle) / macOS (IOSurfaceID). On Linux the frame is a
    // dma-buf: `handle` is the fd number in process `src_pid` (opened via
    // /proc/<src_pid>/fd/<handle>) and the dma-buf layout is given by format /
    // stride / offset / modifier. The trailing args are ignored on Windows/macOS.
    bool setStableTexture(unsigned long long handle, int width, int height,
                          int format = 0, unsigned int stride = 0,
                          unsigned long long offset = 0, unsigned long long modifier = 0,
                          int src_pid = 0);

    // Copy the latest plugin frame into dst_tex (a GL_RGBA8 texture). The data is
    // BGRA-ordered, so the caller should sample dst_tex with an R<->B swizzle.
    // Returns true if dst_tex was updated.
    bool blitTo(unsigned int dst_tex, int width, int height);

private:
    bool mValid = false;
    void* mImpl = nullptr;   // platform state (Windows only)
};

#endif // LL_LLCEFACCELINTEROP_H
