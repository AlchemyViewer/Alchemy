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
                                         unsigned long long offset, unsigned long long modifier, int src_pid)
{
    (void)format; (void)stride; (void)offset; (void)modifier; (void)src_pid;
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
    w->stable->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&w->mutex);

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
    if (w->mutex)
    {
        if (FAILED(w->mutex->AcquireSync(0, 1000)))
        {
            return false;   // producer busy this frame; try again next
        }
        ctx->CopyResource(w->local, w->stable);
        ctx->Flush();
        w->mutex->ReleaseSync(0);
    }
    else
    {
        ctx->CopyResource(w->local, w->stable);
        ctx->Flush();
    }

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
                                         unsigned long long offset, unsigned long long modifier, int src_pid)
{
    (void)format; (void)stride; (void)offset; (void)modifier; (void)src_pid;
    if (!mValid || !handle)
    {
        return false;
    }
    MacAccel* m = (MacAccel*)mImpl;

    // The "handle" is an IOSurfaceID (the surface changes each frame in CEF's
    // pool); look it up and (re)bind it to our rectangle texture.
    IOSurfaceRef surf = IOSurfaceLookup((IOSurfaceID)handle);
    if (!surf)
    {
        return false;
    }
    if (m->surface) { CFRelease(m->surface); }
    m->surface = surf;
    m->width = (int)IOSurfaceGetWidth(surf);
    m->height = (int)IOSurfaceGetHeight(surf);

    CGLContextObj cgl = CGLGetCurrentContext();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, m->tex);
    CGLError err = CGLTexImageIOSurface2D(cgl, GL_TEXTURE_RECTANGLE_ARB, GL_RGBA,
                                          m->width, m->height, GL_BGRA,
                                          GL_UNSIGNED_INT_8_8_8_8_REV, surf, 0);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
    return err == kCGLNoError;
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
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>   // glEGLImageTargetTexture2DOES
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

namespace
{
    // fourcc codes (avoid a hard dependency on drm_fourcc.h)
    inline unsigned int dh_fourcc(char a, char b, char c, char d)
    {
        return (unsigned)a | ((unsigned)b << 8) | ((unsigned)c << 16) | ((unsigned)d << 24);
    }

    struct LinuxAccel
    {
        EGLDisplay display = EGL_NO_DISPLAY;
        PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = nullptr;
        PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = nullptr;
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;

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
    LinuxAccel* l = new LinuxAccel();
    l->display = eglGetCurrentDisplay();   // requires the viewer to use an EGL context
    l->eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    l->eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    l->glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if (l->display == EGL_NO_DISPLAY || !l->eglCreateImageKHR || !l->glEGLImageTargetTexture2DOES)
    {
        LL_WARNS("Media") << "accelerated paint: EGL dma-buf import unavailable (no EGL context?); CPU paint" << LL_ENDL;
        delete l;
        return false;
    }
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
                                         unsigned long long offset, unsigned long long modifier, int src_pid)
{
    if (!mValid || !src_pid)
    {
        return false;
    }
    LinuxAccel* l = (LinuxAccel*)mImpl;

    // `handle` is the dma-buf fd number in the plugin process; re-open it here via
    // /proc (the plugin keeps it alive briefly).
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/fd/%llu", src_pid, handle);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        return false;
    }

    // CEF formats: 0 = RGBA_8888, 1 = BGRA_8888 (cef_color_type_t). Map to fourcc.
    unsigned int fourcc = (format == 0) ? dh_fourcc('A','B','2','4')    // DRM_FORMAT_ABGR8888 (RGBA)
                                        : dh_fourcc('A','R','2','4');   // DRM_FORMAT_ARGB8888 (BGRA)

    EGLint attrs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_LINUX_DRM_FOURCC_EXT, (EGLint)fourcc,
        EGL_DMA_BUF_PLANE0_FD_EXT, fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)offset,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)stride,
        EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, (EGLint)(modifier & 0xFFFFFFFFu),
        EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (EGLint)(modifier >> 32),
        EGL_NONE
    };

    l->releaseImage();
    l->image = l->eglCreateImageKHR(l->display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)0, attrs);
    close(fd);   // EGL keeps its own reference once the image is created
    if (l->image == EGL_NO_IMAGE_KHR)
    {
        LL_WARNS("Media") << "accelerated paint: eglCreateImageKHR(dma_buf) failed" << LL_ENDL;
        return false;
    }

    l->width = width;
    l->height = height;
    glBindTexture(GL_TEXTURE_2D, l->tex);
    l->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, l->image);
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
bool LLCEFAccelInterop::setStableTexture(unsigned long long, int, int, int, unsigned int, unsigned long long, unsigned long long, int) { return false; }
bool LLCEFAccelInterop::blitTo(unsigned int, int, int) { return false; }

#endif
