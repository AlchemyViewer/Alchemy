/**
 * @file llcefaccelinterop.cpp
 * @brief Viewer-side consumer of the CEF accelerated-paint shared texture.
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

#include "llcefaccelinterop.h"

#if LL_WINDOWS

#include "llgl.h"          // viewer GL entry points + the global wglDX* pointers
#include "llrender.h"
#include "lldxhardware.h"  // shared D3D11 device + GL interop device (gDXHardware)

#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>

namespace
{
    // The D3D11 device + the WGL interop device are process-shared (created once
    // in LLDXHardware), and the wglDX* entry points are loaded by the viewer's
    // WGL loader (LLGLManager::initWGL). Convenience accessors:
    inline ID3D11Device1*       sharedDevice()  { return (ID3D11Device1*)gDXHardware.getD3DDevice(); }
    inline ID3D11DeviceContext* sharedContext() { return (ID3D11DeviceContext*)gDXHardware.getD3DContext(); }
    inline HANDLE               interopDevice() { return (HANDLE)gDXHardware.getGLDXInteropDevice(); }

    struct WinAccel
    {
        // The plugin's shared texture opened in the shared device (cross-device,
        // NT handle - so it can't be GL-registered directly), plus its keyed mutex.
        ID3D11Texture2D* stable = nullptr;
        IDXGIKeyedMutex* mutex = nullptr;

        // An own-(shared-)device intermediate, which CAN be GL-registered; we copy
        // the stable texture into it under the mutex each frame.
        ID3D11Texture2D* local = nullptr;
        GLuint           local_gl = 0;
        HANDLE           local_obj = nullptr;
        int              width = 0;
        int              height = 0;
        bool             logged_register = false;

        // FBOs for the flip+convert blit (created once, reused).
        GLuint           read_fbo = 0;
        GLuint           draw_fbo = 0;

        void releaseStable()
        {
            if (local_obj) { wglDXUnregisterObjectNV(interopDevice(), local_obj); local_obj = nullptr; }
            if (local_gl)  { glDeleteTextures(1, &local_gl); local_gl = 0; }
            if (local)     { local->Release(); local = nullptr; }
            if (mutex)     { mutex->Release(); mutex = nullptr; }
            if (stable)    { stable->Release(); stable = nullptr; }
            width = height = 0;
        }
    };
}

LLCEFAccelInterop::~LLCEFAccelInterop()
{
    shutdown();
}

bool LLCEFAccelInterop::init()
{
    // The shared interop device is brought up once at window init; just verify it
    // (and the WGL entry points) are available.
    if (!gDXHardware.hasGLDXInterop() || !wglDXRegisterObjectNV || !wglDXLockObjectsNV)
    {
        return false;
    }
    mImpl = new WinAccel();
    mValid = true;
    return true;
}

void LLCEFAccelInterop::shutdown()
{
    if (!mImpl)
    {
        return;
    }
    WinAccel* w = (WinAccel*)mImpl;
    w->releaseStable();
    if (w->read_fbo) { glDeleteFramebuffers(1, &w->read_fbo); w->read_fbo = 0; }
    if (w->draw_fbo) { glDeleteFramebuffers(1, &w->draw_fbo); w->draw_fbo = 0; }
    delete w;
    mImpl = nullptr;
    mValid = false;
}

bool LLCEFAccelInterop::setStableTexture(unsigned long long handle, int width, int height,
                                         int format, unsigned int stride,
                                         unsigned long long offset, unsigned long long modifier, int src_pid,
                                         int plane_count, const unsigned long long* plane_fds,
                                         const unsigned int* plane_strides, const unsigned long long* plane_offsets)
{
    (void)format; (void)stride; (void)offset; (void)modifier; (void)src_pid;
    (void)plane_count; (void)plane_fds; (void)plane_strides; (void)plane_offsets;
    if (!mValid || !handle)
    {
        return false;
    }
    WinAccel* w = (WinAccel*)mImpl;
    w->releaseStable();

    // Open the plugin's keyed-mutex shared texture in the shared device.
    if (FAILED(sharedDevice()->OpenSharedResource1((HANDLE)(uintptr_t)handle, __uuidof(ID3D11Texture2D), (void**)&w->stable)) || !w->stable)
    {
        LL_WARNS("Media") << "accelerated paint: OpenSharedResource1 failed" << LL_ENDL;
        return false;
    }
    // The producer/consumer contract relies on keyed-mutex synchronization; an
    // unsynchronized CopyResource could read while the plugin is mid-write. Fail
    // the bind (rather than silently fall back) if the mutex is missing.
    if (FAILED(w->stable->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&w->mutex)) || !w->mutex)
    {
        LL_WARNS("Media") << "accelerated paint: shared texture is missing IDXGIKeyedMutex" << LL_ENDL;
        w->releaseStable();
        return false;
    }

    D3D11_TEXTURE2D_DESC sd = {};
    w->stable->GetDesc(&sd);

    // An own-device intermediate of the same format; this one can be GL-registered
    // (the opened shared texture can't - NT-handle cross-device, same as CEF's).
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = sd.Width; d.Height = sd.Height; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = sd.Format; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(sharedDevice()->CreateTexture2D(&d, nullptr, &w->local)) || !w->local)
    {
        LL_WARNS("Media") << "accelerated paint: CreateTexture2D(local) failed" << LL_ENDL;
        w->releaseStable();
        return false;
    }

    glGenTextures(1, &w->local_gl);
    w->local_obj = wglDXRegisterObjectNV(interopDevice(), w->local, w->local_gl, GL_TEXTURE_2D, WGL_ACCESS_READ_ONLY_NV);
    if (!w->local_obj)
    {
        LL_WARNS("Media") << "accelerated paint: wglDXRegisterObjectNV failed (" << GetLastError() << ")" << LL_ENDL;
        w->releaseStable();
        return false;
    }

    w->width = (int)sd.Width;
    w->height = (int)sd.Height;
    if (!w->logged_register)
    {
        LL_INFOS("Media") << "accelerated paint: stable texture bound " << sd.Width << "x" << sd.Height
                          << " dxfmt=" << sd.Format << LL_ENDL;
        w->logged_register = true;
    }
    return true;
}

bool LLCEFAccelInterop::blitTo(unsigned int dst_tex, int width, int height)
{
    if (!mValid || !dst_tex)
    {
        return false;
    }
    WinAccel* w = (WinAccel*)mImpl;
    if (!w->stable || !w->local || !w->local_obj)
    {
        return false;
    }

    int cw = llmin(width, w->width);
    int ch = llmin(height, w->height);
    if (cw <= 0 || ch <= 0)
    {
        return false;
    }

    ID3D11DeviceContext* ctx = sharedContext();

    // Copy the plugin's latest frame into our intermediate under the keyed mutex
    // (single key 0 = mutual exclusion with the producer + cross-process sync).
    // Acquire with a zero timeout so a busy producer never stalls the viewer GL
    // thread; just skip this frame and retry next tick.
    const HRESULT acq_hr = w->mutex->AcquireSync(0, 0);
    if (acq_hr == (HRESULT)WAIT_TIMEOUT || acq_hr == DXGI_ERROR_WAIT_TIMEOUT)
    {
        return false;   // producer busy this frame; try again next
    }
    if (FAILED(acq_hr))
    {
        LL_WARNS("Media") << "accelerated paint: AcquireSync failed hr=0x"
                          << std::hex << acq_hr << std::dec << LL_ENDL;
        return false;
    }
    ctx->CopyResource(w->local, w->stable);
    ctx->Flush();
    w->mutex->ReleaseSync(0);

    // Lock the GL view of the intermediate and blit it into the media texture
    // via framebuffers. Reading the interop texture through a framebuffer samples
    // it in the correct channel order (same as a normal texture fetch - so no
    // BGRA swizzle is needed), and the inverted destination rectangle flips Y
    // (CEF's accelerated texture is top-down; the viewer expects bottom-up).
    HANDLE gl_dx = interopDevice();
    if (!wglDXLockObjectsNV(gl_dx, 1, &w->local_obj))
    {
        return false;
    }

    if (!w->read_fbo) glGenFramebuffers(1, &w->read_fbo);
    if (!w->draw_fbo) glGenFramebuffers(1, &w->draw_fbo);

    GLint prev_read = 0, prev_draw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, w->read_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, w->local_gl, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, w->draw_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)dst_tex, 0);

    glBlitFramebuffer(0, 0, cw, ch,   // src
                      0, ch, cw, 0,   // dst, Y-flipped
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);

    wglDXUnlockObjectsNV(gl_dx, 1, &w->local_obj);
    return true;
}

#elif LL_DARWIN  // macOS: bind the shared IOSurface to a GL texture, blit to media

#include "llgl.h"
#include <OpenGL/CGLCurrent.h>
#include <OpenGL/CGLIOSurface.h>
#include <IOSurface/IOSurface.h>

#ifndef GL_TEXTURE_RECTANGLE_ARB
#define GL_TEXTURE_RECTANGLE_ARB 0x84F5
#endif

namespace
{
    struct MacAccel
    {
        GLuint      tex = 0;                 // GL_TEXTURE_RECTANGLE bound to the IOSurface
        IOSurfaceRef surface = nullptr;
        int         width = 0;
        int         height = 0;
        GLuint      read_fbo = 0;
        GLuint      draw_fbo = 0;
    };
}

LLCEFAccelInterop::~LLCEFAccelInterop()
{
    shutdown();
}

bool LLCEFAccelInterop::init()
{
    if (!CGLGetCurrentContext())
    {
        return false;
    }
    MacAccel* m = new MacAccel();
    glGenTextures(1, &m->tex);
    mImpl = m;
    mValid = true;
    return true;
}

void LLCEFAccelInterop::shutdown()
{
    if (!mImpl)
    {
        return;
    }
    MacAccel* m = (MacAccel*)mImpl;
    if (m->tex) { glDeleteTextures(1, &m->tex); }
    if (m->read_fbo) { glDeleteFramebuffers(1, &m->read_fbo); }
    if (m->draw_fbo) { glDeleteFramebuffers(1, &m->draw_fbo); }
    if (m->surface) { CFRelease(m->surface); }
    delete m;
    mImpl = nullptr;
    mValid = false;
}

bool LLCEFAccelInterop::setStableTexture(unsigned long long handle, int width, int height,
                                         int format, unsigned int stride,
                                         unsigned long long offset, unsigned long long modifier, int src_pid,
                                         int plane_count, const unsigned long long* plane_fds,
                                         const unsigned int* plane_strides, const unsigned long long* plane_offsets)
{
    (void)format; (void)stride; (void)offset; (void)modifier; (void)src_pid;
    (void)plane_count; (void)plane_fds; (void)plane_strides; (void)plane_offsets;
    if (!mValid || !handle)
    {
        return false;
    }
    MacAccel* m = (MacAccel*)mImpl;

    // The "handle" is an already-resolved IOSurfaceRef (+1 retained) handed over
    // by the mach receiver (LLCEFSurfaceReceiver) - CEF shares its IOSurface via a
    // mach port, which can't be resolved from a cross-process global id, so the
    // surface arrives out-of-band rather than as an IOSurfaceID. We take ownership.
    IOSurfaceRef surf = (IOSurfaceRef)(uintptr_t)handle;
    if (!surf)
    {
        return false;
    }
    // Bind into the GL texture first; only swap in the new surface once the bind
    // succeeds, so a failed import leaves the previous (good) binding intact for
    // blitTo() rather than dropping to a stale/invalid surface.
    const int new_width = (int)IOSurfaceGetWidth(surf);
    const int new_height = (int)IOSurfaceGetHeight(surf);

    CGLContextObj cgl = CGLGetCurrentContext();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, m->tex);
    CGLError err = CGLTexImageIOSurface2D(cgl, GL_TEXTURE_RECTANGLE_ARB, GL_RGBA,
                                          new_width, new_height, GL_BGRA,
                                          GL_UNSIGNED_INT_8_8_8_8_REV, surf, 0);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
    if (err != kCGLNoError)
    {
        CFRelease(surf);
        return false;
    }

    if (m->surface) { CFRelease(m->surface); }
    m->surface = surf;
    m->width = new_width;
    m->height = new_height;
    return true;
}

bool LLCEFAccelInterop::blitTo(unsigned int dst_tex, int width, int height)
{
    if (!mValid || !dst_tex)
    {
        return false;
    }
    MacAccel* m = (MacAccel*)mImpl;
    if (!m->surface || !m->tex)
    {
        return false;
    }
    int cw = llmin(width, m->width);
    int ch = llmin(height, m->height);
    if (cw <= 0 || ch <= 0)
    {
        return false;
    }

    if (!m->read_fbo) glGenFramebuffers(1, &m->read_fbo);
    if (!m->draw_fbo) glGenFramebuffers(1, &m->draw_fbo);

    GLint prev_read = 0, prev_draw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m->read_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE_ARB, m->tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m->draw_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)dst_tex, 0);

    glBlitFramebuffer(0, 0, cw, ch,   // src
                      0, ch, cw, 0,   // dst, Y-flipped
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE_ARB, 0, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    return true;
}

#elif LL_LINUX  // import the plugin's dma-buf via EGL, blit to the media texture

#include "llgl.h"
#include <SDL3/SDL.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

namespace
{
    // fourcc codes (avoid a hard dependency on drm_fourcc.h)
    inline unsigned int dh_fourcc(char a, char b, char c, char d)
    {
        return (unsigned)a | ((unsigned)b << 8) | ((unsigned)c << 16) | ((unsigned)d << 24);
    }

    // DRM "no explicit modifier" sentinel (from drm_fourcc.h; declared here to
    // avoid a hard dependency on it). Must NOT be passed to eglCreateImageKHR as
    // an explicit modifier - doing so fails the import.
    static const unsigned long long DH_DRM_FORMAT_MOD_INVALID = 0x00ffffffffffffffULL;

    struct LinuxAccel
    {
        EGLDisplay display = EGL_NO_DISPLAY;
        bool     has_import_modifiers = false;   // EGL_EXT_image_dma_buf_import_modifiers

        GLuint   tex = 0;          // GL texture the EGLImage is bound to
        EGLImageKHR image = EGL_NO_IMAGE_KHR;
        int      width = 0;
        int      height = 0;
        GLuint   read_fbo = 0;
        GLuint   draw_fbo = 0;

        void releaseImage()
        {
            if (image != EGL_NO_IMAGE_KHR && eglDestroyImageKHR)
            {
                eglDestroyImageKHR(display, image);
                image = EGL_NO_IMAGE_KHR;
            }
        }
    };
}

LLCEFAccelInterop::~LLCEFAccelInterop()
{
    shutdown();
}

bool LLCEFAccelInterop::init()
{
    SDL_EGLDisplay egl_display = SDL_EGL_GetCurrentDisplay();
    if(!egl_display)
    {
        LL_WARNS("Media") << "accelerated paint: EGL display not found (no EGL context?); CPU paint" << LL_ENDL;
        return false;
    }

    LinuxAccel* l = new LinuxAccel();
    l->display = egl_display;   // requires the viewer to use an EGL context

    if (l->display == EGL_NO_DISPLAY || !eglCreateImageKHR || !glEGLImageTargetTexture2DOES)
    {
        LL_WARNS("Media") << "accelerated paint: EGL dma-buf import unavailable (no EGL context?); CPU paint" << LL_ENDL;
        delete l;
        return false;
    }

    // DRM format modifiers (tiling/compression) can only be passed to the import
    // when the driver advertises this extension; otherwise we must let it assume
    // the buffer's default layout. (Mesa supports it; this is a safety net.)
    const char* exts = eglQueryString ? eglQueryString(l->display, EGL_EXTENSIONS) : nullptr;
    l->has_import_modifiers = exts && strstr(exts, "EGL_EXT_image_dma_buf_import_modifiers") != nullptr;

    glGenTextures(1, &l->tex);
    mImpl = l;
    mValid = true;
    return true;
}

void LLCEFAccelInterop::shutdown()
{
    if (!mImpl)
    {
        return;
    }
    LinuxAccel* l = (LinuxAccel*)mImpl;
    l->releaseImage();
    if (l->tex) { glDeleteTextures(1, &l->tex); }
    if (l->read_fbo) { glDeleteFramebuffers(1, &l->read_fbo); }
    if (l->draw_fbo) { glDeleteFramebuffers(1, &l->draw_fbo); }
    delete l;
    mImpl = nullptr;
    mValid = false;
}

bool LLCEFAccelInterop::setStableTexture(unsigned long long handle, int width, int height,
                                         int format, unsigned int stride,
                                         unsigned long long offset, unsigned long long modifier, int src_pid,
                                         int plane_count, const unsigned long long* plane_fds,
                                         const unsigned int* plane_strides, const unsigned long long* plane_offsets)
{
    if (!mValid)
    {
        return false;
    }
    (void)src_pid;   // unused on Linux: the fds arrive already open via SCM_RIGHTS
    LinuxAccel* l = (LinuxAccel*)mImpl;

    // Build a local plane table. The plane fds are already open in THIS (viewer)
    // process - handed over by the plugin as SCM_RIGHTS ancillary data - so we
    // import them directly (a dma-buf fd cannot be reopened via /proc anyway). We
    // do NOT own them: the caller closes them after we return, and EGL keeps its
    // own reference once the image is created. Fall back to the scalar
    // handle/stride/offset for plane 0 if no array was supplied.
    int n = plane_count;
    if (n <= 0) n = 1;
    if (n > 4) n = 4;
    int                fds[4];
    unsigned int       strides[4];
    unsigned long long offsets[4];
    for (int i = 0; i < n; ++i)
    {
        fds[i]     = (int)(plane_fds ? plane_fds[i] : (i == 0 ? handle : 0));
        strides[i] = plane_strides ? plane_strides[i] : (i == 0 ? stride : 0);
        offsets[i] = plane_offsets ? plane_offsets[i] : (i == 0 ? offset : 0);
    }

    // CEF formats: 0 = RGBA_8888, 1 = BGRA_8888 (cef_color_type_t). Map to fourcc.
    unsigned int fourcc = (format == 0) ? dh_fourcc('A','B','2','4')    // DRM_FORMAT_ABGR8888 (RGBA)
                                        : dh_fourcc('A','R','2','4');   // DRM_FORMAT_ARGB8888 (BGRA)

    // Pass the DRM modifier per plane ONLY when it's a real, known value and the
    // driver supports import-with-modifiers. Passing DRM_FORMAT_MOD_INVALID (or
    // any modifier without the extension) makes eglCreateImageKHR fail - the
    // classic "accelerated web page renders grey".
    const bool use_modifier = l->has_import_modifiers && modifier != DH_DRM_FORMAT_MOD_INVALID;

    static const EGLint FD_ATTR[4]  = { EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE3_FD_EXT };
    static const EGLint OFF_ATTR[4] = { EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT };
    static const EGLint PIT_ATTR[4] = { EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT };
    static const EGLint MLO_ATTR[4] = { EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT };
    static const EGLint MHI_ATTR[4] = { EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT };

    EGLint attrs[64];
    int a = 0;
    attrs[a++] = EGL_WIDTH;             attrs[a++] = width;
    attrs[a++] = EGL_HEIGHT;            attrs[a++] = height;
    attrs[a++] = EGL_LINUX_DRM_FOURCC_EXT; attrs[a++] = (EGLint)fourcc;
    for (int i = 0; i < n; ++i)
    {
        attrs[a++] = FD_ATTR[i];  attrs[a++] = fds[i];
        attrs[a++] = OFF_ATTR[i]; attrs[a++] = (EGLint)offsets[i];
        attrs[a++] = PIT_ATTR[i]; attrs[a++] = (EGLint)strides[i];
        if (use_modifier)
        {
            attrs[a++] = MLO_ATTR[i]; attrs[a++] = (EGLint)(modifier & 0xFFFFFFFFu);
            attrs[a++] = MHI_ATTR[i]; attrs[a++] = (EGLint)(modifier >> 32);
        }
    }
    attrs[a++] = EGL_NONE;

    // Import into a temporary handle first; a bad frame must not destroy the
    // current binding before we know the replacement is usable.
    EGLImageKHR new_image = eglCreateImageKHR(l->display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)0, attrs);
    // The fds belong to the caller (received via SCM_RIGHTS); it closes them once
    // we return. EGL has taken its own reference to the buffer by now.

    static bool logged = false;
    if (!logged)
    {
        logged = true;
        LL_INFOS("Media") << "accelerated paint dma-buf import: " << (new_image != EGL_NO_IMAGE_KHR ? "ok" : "FAILED")
                          << " planes=" << n << " fourcc=0x" << std::hex << fourcc
                          << " modifier=0x" << modifier << std::dec
                          << " mod_ext=" << (l->has_import_modifiers ? 1 : 0)
                          << " used_mod=" << (use_modifier ? 1 : 0)
                          << " " << width << "x" << height << LL_ENDL;
    }

    if (new_image == EGL_NO_IMAGE_KHR)
    {
        LL_WARNS("Media") << "accelerated paint: eglCreateImageKHR(dma_buf) failed (planes=" << n
                          << " modifier=0x" << std::hex << modifier << std::dec << ")" << LL_ENDL;
        return false;
    }

    l->releaseImage();
    l->image = new_image;
    l->width = width;
    l->height = height;
    glBindTexture(GL_TEXTURE_2D, l->tex);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, l->image);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool LLCEFAccelInterop::blitTo(unsigned int dst_tex, int width, int height)
{
    if (!mValid || !dst_tex)
    {
        return false;
    }
    LinuxAccel* l = (LinuxAccel*)mImpl;
    if (l->image == EGL_NO_IMAGE_KHR || !l->tex)
    {
        return false;
    }
    int cw = llmin(width, l->width);
    int ch = llmin(height, l->height);
    if (cw <= 0 || ch <= 0)
    {
        return false;
    }

    if (!l->read_fbo) glGenFramebuffers(1, &l->read_fbo);
    if (!l->draw_fbo) glGenFramebuffers(1, &l->draw_fbo);

    GLint prev_read = 0, prev_draw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, l->read_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, l->tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, l->draw_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)dst_tex, 0);

    glBlitFramebuffer(0, 0, cw, ch,   // src
                      0, ch, cw, 0,   // dst, Y-flipped
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
    return true;
}

#else  // other platforms - stub

LLCEFAccelInterop::~LLCEFAccelInterop() { shutdown(); }
bool LLCEFAccelInterop::init() { return false; }
void LLCEFAccelInterop::shutdown() {}
bool LLCEFAccelInterop::setStableTexture(unsigned long long, int, int, int, unsigned int, unsigned long long, unsigned long long, int,
                                         int, const unsigned long long*, const unsigned int*, const unsigned long long*) { return false; }
bool LLCEFAccelInterop::blitTo(unsigned int, int, int) { return false; }

#endif

// static - platform-agnostic capability preflight. Probes the interop once (it
// needs a current GL/EGL context, which exists on the main thread when media
// sources are created) so callers only request accelerated paint from the plugin
// when the consumer can actually bind it - otherwise the plugin would emit GPU
// frames the viewer drops, leaving the media blank with no CPU fallback.
bool LLCEFAccelInterop::isSupported()
{
    static bool s_probed = false;
    static bool s_supported = false;
    if (!s_probed)
    {
        LLCEFAccelInterop probe;
        s_supported = probe.init();
        probe.shutdown();
        s_probed = true;
    }
    return s_supported;
}
