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

    // True if this platform can actually consume the plugin's GPU shared texture.
    // Probed once (requires a current GL context). Callers should only request
    // accelerated paint from the plugin when this is true; otherwise the plugin
    // would produce GPU frames the viewer cannot bind, leaving the media blank.
    static bool isSupported();

    // (Re)bind the plugin's shared frame. `handle` is the platform shared-texture
    // handle on Windows (NT handle) / macOS (IOSurfaceID). On Linux the frame is a
    // dma-buf: `handle` is plane 0's fd number in process `src_pid` (opened via
    // /proc/<src_pid>/fd/<fd>) and the layout is given by format / modifier plus
    // the per-plane table. A tiled/compressed (CCS) buffer has more than one plane;
    // pass them all via plane_fds / plane_strides / plane_offsets (each of length
    // plane_count) or the import fails and the surface renders grey. When the plane
    // arrays are null, the single `stride`/`offset` describe plane 0. Everything
    // past `format` is ignored on Windows/macOS.
    bool setStableTexture(unsigned long long handle, int width, int height,
                          int format = 0, unsigned int stride = 0,
                          unsigned long long offset = 0, unsigned long long modifier = 0,
                          int src_pid = 0, int plane_count = 1,
                          const unsigned long long* plane_fds = nullptr,
                          const unsigned int* plane_strides = nullptr,
                          const unsigned long long* plane_offsets = nullptr);

    // Copy the latest plugin frame into dst_tex (a GL_RGBA8 texture). The data is
    // BGRA-ordered, so the caller should sample dst_tex with an R<->B swizzle.
    // Returns true if dst_tex was updated.
    bool blitTo(unsigned int dst_tex, int width, int height);

private:
    bool mValid = false;
    void* mImpl = nullptr;   // platform state (Windows only)
};

#endif // LL_LLCEFACCELINTEROP_H
