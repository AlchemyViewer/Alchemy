/**
 * @file lldxhardware.h
 * @brief LLDXHardware definition
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

#ifndef LL_LLDXHARDWARE_H
#define LL_LLDXHARDWARE_H

#include <map>

#include "stdtypes.h"
#include "llsd.h"


class LLDXHardware
{
public:
    LLDXHardware();

    void cleanup();

    // WMI can return multiple GPU drivers
    // specify which one to output
    typedef enum {
        GPU_INTEL,
        GPU_NVIDIA,
        GPU_AMD,
        GPU_ANY
    } EGPUVendor;
    std::string getDriverVersionWMI(EGPUVendor vendor);

    LLSD getDisplayInfo();

    // Raise gGLManager.mVRAM to the DXGI video-memory budget when the GL
    // vendor paths (AMD associations / NVX memory info) didn't report one —
    // matters mainly for Intel iGPUs. Must run with GL initialized. Shared
    // by LLWindowWin32's window thread (checkDXMem) and the SDL backend.
    static void updateVRAMBudgetFromDXGI();

    // --- Shared D3D11 <-> OpenGL interop (WGL_NV_DX_interop2) ---
    // One D3D11 device + one wglDXOpenDeviceNV interop device for the whole
    // process, brought up once after the GL context and WGL extensions exist.
    // The zero-copy CEF media surfaces share these instead of each creating their
    // own. Must be called with the GL context current. Returns true if available.
    // No-op / false on platforms without WGL_NV_DX_interop2.
    bool initGLDXInterop();
    void cleanupGLDXInterop();
    bool hasGLDXInterop() const { return mGLDXInteropDevice != nullptr; }
    void* getD3DDevice() const { return mD3DDevice; }            // ID3D11Device1*
    void* getD3DContext() const { return mD3DContext; }          // ID3D11DeviceContext*
    void* getGLDXInteropDevice() const { return mGLDXInteropDevice; } // wglDXOpenDeviceNV handle

private:
    void* mD3DDevice = nullptr;
    void* mD3DContext = nullptr;
    void* mGLDXInteropDevice = nullptr;
};

extern LLDXHardware gDXHardware;

#endif // LL_LLDXHARDWARE_H
