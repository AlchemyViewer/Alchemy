/**
 * @file llsdl.cpp
 * @brief SDL initialization
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include <initializer_list>
#include <list>

#include "llsdl.h"

#include "llerror.h"
#include "llgl.h"
#include "llrender.h"
#include "llwindow.h"

#if LL_DARWIN
#include <OpenGL/OpenGL.h>
#endif

// The EGL entry points are resolved at runtime through SDL_EGL_GetProcAddress
// (the viewer does not link libEGL directly under SDL), so only the tokens
// are wanted here.
#if LL_LINUX
#include <EGL/egl.h>
#endif

bool gSDLMainHandled = false;

// SDL_main.h declares SDL_RegisterApp/SDL_UnregisterApp. SDL_MAIN_HANDLED
// keeps it declaration-only: the entry-point implementation lives in
// llappviewersdl.cpp (SDL_MAIN_USE_CALLBACKS) on SDL-window builds.
#if !LL_SDL_WINDOW || LL_WINDOWS
#define SDL_MAIN_HANDLED 1
#include "SDL3/SDL_main.h"
#endif

#if LL_WINDOWS
#include "llwin32headers.h" // CS_BYTEALIGNCLIENT / CS_OWNDC
#endif

#if LL_DARWIN
#include "llsdl_macos.h"
#endif

void sdl_logger(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    switch (priority)
    {
        case SDL_LOG_PRIORITY_TRACE:
        case SDL_LOG_PRIORITY_VERBOSE:
        case SDL_LOG_PRIORITY_DEBUG:
            LL_DEBUGS("SDL") << "log='" << message << "'" << LL_ENDL;
            break;
        case SDL_LOG_PRIORITY_INFO:
            LL_INFOS("SDL") << "log='" << message << "'" << LL_ENDL;
            break;
        case SDL_LOG_PRIORITY_WARN:
        case SDL_LOG_PRIORITY_ERROR:
        case SDL_LOG_PRIORITY_CRITICAL:
            LL_WARNS("SDL") << "log='" << message << "'" << LL_ENDL;
            break;
        case SDL_LOG_PRIORITY_INVALID:
        default:
            break;
    }
}

void set_sdl_hints()
{
#if LL_SDL_WINDOW
    // Run once. SDL_SetHint is itself idempotent, but the registerUserDefaults
    // hints only take effect on the *first* video init, so re-setting them after
    // that point would be pointless — bail early to make the intent explicit.
    static bool sHintsSet = false;
    if (sHintsSet)
        return;
    sHintsSet = true;

    std::initializer_list<std::tuple< char const*, char const * > > hintList =
            {
#if LL_LINUX
                    // The viewer's GL layer on Linux is EGL and nothing else:
                    // the worker threads' shared contexts are surfaceless EGL
                    // contexts and EGL_KHR_image is resolved through it. SDL
                    // would otherwise create the main context with GLX under
                    // its x11 driver; with this it uses EGL on X11 too, so one
                    // code path serves both and no X11 header enters the tree.
                    {SDL_HINT_VIDEO_FORCE_EGL,"1"},
#endif

                    // Don't ask the compositor to bypass us in fullscreen —
                    // keeps screen recorders, alt-tab thumbnails, picom-style
                    // effects, etc. working. Slight latency cost in exclusive
                    // fullscreen vs. taking the bypass.
                    {SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR,"0"},

                    // Process clicks immediately on focus instead of swallowing
                    // the first click to focus the window.
                    {SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH,"1"},

                    // Per SDL3's hint docs: when an app uses relative mouse
                    // mode directly (as we do — see LLWindowSDL::hideCursor)
                    // this hint is automatically *disabled* by SDL, so our
                    // explicit "0" matches the current default. Setting it
                    // explicitly is defensive: if a future SDL3 release ever
                    // changed the auto-default, we want plain warps to stay
                    // plain warps (never silently flip the cursor into
                    // relative mode underneath us).
                    {SDL_HINT_MOUSE_EMULATE_WARP_WITH_RELATIVE,"0"},

                    // SDL3 default ("0"): a SDL_WarpMouseInWindow while in
                    // relative mode does NOT generate a synthetic motion event
                    // — apps don't have to filter the warp distance out of
                    // xrel/yrel. We rely on that: getCursorDelta accumulates
                    // every motion event, and we never want a warp-distance
                    // to land in the camera signal. Don't override.

                    // LOAD-BEARING for numpad support: this is SDL3's default
                    // value, and it deliberately omits "hide_numpad". With
                    // "hide_numpad" SDL would pre-fold SDLK_KP_* onto the digit
                    // row / nav cluster based on NumLock *before* delivering the
                    // event, which would make NumLock-off numpad keys
                    // indistinguishable from the real arrow keys and break
                    // LLKeyboardSDL's numpad-distinct logic (it relies on
                    // receiving the raw SDLK_KP_* keysym). Do not add
                    // "hide_numpad" here.
                    {SDL_HINT_KEYCODE_OPTIONS,"french_numbers,latin_letters"},
                    // The viewer renders the IME composition string itself
                    // via LLPreeditor::updatePreedit (see the SDL_EVENT_TEXT_EDITING
                    // handler in llwindowsdl.cpp). Tell SDL/the platform IME
                    // to suppress its own preedit popup so we don't render
                    // the composition twice. We do NOT advertise "candidates"
                    // — the viewer has no candidate-list UI, so the IME
                    // should keep drawing that natively.
                    {SDL_HINT_IME_IMPLEMENTED_UI,"composition"},

                    // Prevent popup of text overlay when holding movement keys on macos
                    {SDL_HINT_MAC_PRESS_AND_HOLD, "0"},

                    // Momentum scrolling on macos is desirable for mac touchpads
                    {SDL_HINT_MAC_SCROLL_MOMENTUM, "1"},

                    // Don't let SDL post a synthetic SDL_EVENT_QUIT when the
                    // last window closes. The viewer owns its own shutdown
                    // sequence (LLAppViewer), and transient teardown of the
                    // main window must never be read as a request to quit.
                    {SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0"},
            };

    for (auto hint: hintList)
    {
        SDL_SetHint(std::get<0>(hint), std::get<1>(hint));
    }
#endif
}

void init_sdl(const std::string& app_name, bool hidden_window)
{
#if LL_LINUX
    // No X or Wayland socket -- CI, a container, WSL without one -- and the
    // default driver has nowhere to put even a hidden window. The offscreen
    // driver gives a context over EGL with no surface at all. With a display,
    // the default driver's hidden window is closer to what ships, and a
    // driver named in the environment is left alone.
    if (hidden_window
        && !SDL_getenv("SDL_VIDEO_DRIVER")
        && !SDL_getenv("DISPLAY")
        && !SDL_getenv("WAYLAND_DISPLAY"))
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    }
#else
    (void)hidden_window;
#endif

#ifndef LL_SDL_WINDOW
    if (!gSDLMainHandled)
    {
        SDL_SetMainReady();
    }

    SDL_SetLogOutputFunction(&sdl_logger, nullptr);

    const int c_sdl_version = SDL_VERSION;
    LL_INFOS() << "Compiled against SDL "
               << SDL_VERSIONNUM_MAJOR(c_sdl_version) << "."
               << SDL_VERSIONNUM_MINOR(c_sdl_version) << "."
               << SDL_VERSIONNUM_MICRO(c_sdl_version) << LL_ENDL;
    const int r_sdl_version = SDL_GetVersion();
    LL_INFOS() << "Running with SDL "
               << SDL_VERSIONNUM_MAJOR(r_sdl_version) << "."
               << SDL_VERSIONNUM_MINOR(r_sdl_version) << "."
               << SDL_VERSIONNUM_MICRO(r_sdl_version) << LL_ENDL;
#endif

#if LL_WINDOWS && defined(LL_SDL_WINDOW)
    Uint32 style = 0;
#if defined(CS_BYTEALIGNCLIENT) && defined(CS_OWNDC)
    style = (CS_BYTEALIGNCLIENT | CS_OWNDC);
#endif
    SDL_RegisterApp(app_name.c_str(), style, nullptr);
#endif

    // Hints must be in place before the first video init. The splash screen
    // also calls this before its own SDL_InitSubSystem(SDL_INIT_VIDEO); the
    // once-guard inside makes a second call here a no-op.
    set_sdl_hints();

    // SDL_INIT_VIDEO is the only subsystem the viewer actually uses through
    // SDL3. Joystick / gamepad input goes through libndof (llviewerjoystick),
    // not SDL3 — initialising those subsystems would only add surface area
    // (hotplug events, device enumeration, shutdown ordering) for no caller.
    // If/when an SDL3 gamepad code path is added it can re-init at that time.
    std::initializer_list<std::tuple<uint32_t, char const*, bool>> initList=
            {
                {SDL_INIT_VIDEO,"SDL_INIT_VIDEO", true},
            };

    for (auto subSystem : initList)
    {
        if (!SDL_InitSubSystem(std::get<0>(subSystem)))
        {
            LL_WARNS() << "SDL_InitSubSystem for " << std::get<1>(subSystem) << " failed " << SDL_GetError() << LL_ENDL;

            if (std::get<2>(subSystem))
            {
                OSMessageBox("SDL_Init() failure", "error", OSMB_OK);
                return;
            }
        }
    }

#if LL_DARWIN
    // SDL has now registered the app and built its default Cocoa menu bar. Drop
    // the Cmd+W shortcut off its auto-created Window > Close item so the
    // shortcut reaches the viewer's own "Close Window" handler instead of
    // tearing down the window (which the SDL backend reads as a quit request).
    ll_sdl_macos_strip_default_close_shortcut();
#endif
}

void quit_sdl()
{
#if LL_WINDOWS && defined(LL_SDL_WINDOW)
    SDL_UnregisterApp();
#endif

    // When SDL_MAIN_USE_CALLBACKS is in effect (gSDLMainHandled=true), the
    // SDL framework owns SDL_Init/SDL_Quit around the SDL_App* callbacks.
    // Calling SDL_Quit() here would run before SDL's own SDL_Quit at process
    // exit — SDL3 makes this idempotent, but it's still the wrong owner.
    // Only quit ourselves when we initialised SDL ourselves.
    if (!gSDLMainHandled)
    {
        SDL_Quit();
    }
}

// The handle behind sdl_create_shared_context: whatever the platform GL API
// needs to bind and tear the context down.
namespace
{
    struct LLSDLSharedContext
    {
#if LL_WINDOWS
        HGLRC rc = nullptr;
        HDC   dc = nullptr;        // the DC the sibling context binds to
#elif LL_DARWIN
        CGLContextObj ctx = nullptr;
#else // LL_LINUX
        // EGL, as void* so the EGL types stay out of the header
        void* egl_dpy = nullptr;   // EGLDisplay
        void* egl_ctx = nullptr;   // EGLContext
#endif
    };
}

void* sdl_create_shared_context()
{
    // A version request derived from the live main context, clamped to the
    // range the viewer supports. WGL/EGL need this explicitly (mirroring
    // LLWindowWin32::createSharedContext); CGL inherits it from the share
    // context, hence the guard against an unused-variable warning there.
#if LL_WINDOWS || LL_LINUX
    const F32 gl_ver = llclamp(gGLManager.mGLVersion, 3.0f, 4.6f);
    const S32 ver_major = (S32)gl_ver;
    const S32 ver_minor = (S32)ll_round((gl_ver - ver_major) * 10.f);
#endif

    auto* shared = new LLSDLSharedContext();
    bool ok = false;

#if LL_WINDOWS
    HDC   dc    = wglGetCurrentDC();
    HGLRC share = wglGetCurrentContext();
    if (dc && share && wglCreateContextAttribsARB)
    {
        S32 attribs[] =
        {
            WGL_CONTEXT_MAJOR_VERSION_ARB, ver_major,
            WGL_CONTEXT_MINOR_VERSION_ARB, ver_minor,
            WGL_CONTEXT_PROFILE_MASK_ARB,  LLRender::sGLCoreProfile ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
            WGL_CONTEXT_FLAGS_ARB, gDebugGL ? WGL_CONTEXT_DEBUG_BIT_ARB : 0,
            0
        };
        HGLRC rc = nullptr;
        for (;;)
        {
            rc = wglCreateContextAttribsARB(dc, share, attribs);
            if (rc) break;
            if (attribs[3] > 0)      { attribs[3]--; }                   // step minor down
            else if (attribs[1] > 3) { attribs[1]--; attribs[3] = 3; }   // step major down
            else                     { break; }                         // gave up at 3.0
        }
        if (rc)
        {
            shared->rc = rc;
            shared->dc = dc;
            ok = true;
        }
        else
        {
            LL_WARNS() << "wglCreateContextAttribsARB (shared) failed" << LL_ENDL;
        }
    }
#elif LL_DARWIN
    CGLContextObj share = CGLGetCurrentContext();
    if (share)
    {
        CGLPixelFormatObj pf = CGLGetPixelFormat(share);
        CGLContextObj ctx = nullptr;
        CGLError err = CGLCreateContext(pf, share, &ctx);
        if (err == kCGLNoError && ctx)
        {
            shared->ctx = ctx;
            ok = true;
        }
        else
        {
            LL_WARNS() << "CGLCreateContext (shared) failed: " << CGLErrorString(err) << LL_ENDL;
        }
    }
#else // LL_LINUX
    {
        // EGL: a surfaceless context (EGL_NO_SURFACE) avoids needing a
        // per-worker drawable. Requires EGL_KHR_surfaceless_context (Mesa and
        // NVIDIA have it). SDL exposes the display/config it created the main
        // context with -- with EGL on X11 too, see SDL_HINT_VIDEO_FORCE_EGL.
        typedef void* (*fn_getctx)(void);
        typedef void* (*fn_createctx)(void*, void*, void*, const int*);
        typedef unsigned int (*fn_bindapi)(unsigned int);

        auto egl_getctx    = (fn_getctx)SDL_EGL_GetProcAddress("eglGetCurrentContext");
        auto egl_createctx = (fn_createctx)SDL_EGL_GetProcAddress("eglCreateContext");
        auto egl_bindapi   = (fn_bindapi)SDL_EGL_GetProcAddress("eglBindAPI");

        void* dpy   = (void*)SDL_EGL_GetCurrentDisplay();
        void* cfg   = (void*)SDL_EGL_GetCurrentConfig();
        void* share = egl_getctx ? egl_getctx() : nullptr;

        if (dpy && egl_createctx && share)
        {
            if (egl_bindapi) egl_bindapi(EGL_OPENGL_API);
            // Must request the version explicitly — an empty attrib list defaults
            // to GL 1.0, which can't drive the modern texture/VBO uploads the
            // worker shares with the main context. (EGL 1.5 tokens.)
            const int ctx_attribs[] =
            {
                EGL_CONTEXT_MAJOR_VERSION, ver_major,
                EGL_CONTEXT_MINOR_VERSION, ver_minor,
                EGL_NONE
            };
            void* ctx = egl_createctx(dpy, cfg, share, ctx_attribs);
            if (ctx && ctx != EGL_NO_CONTEXT)
            {
                shared->egl_dpy = dpy;
                shared->egl_ctx = ctx;
                ok = true;
            }
            else
            {
                LL_WARNS() << "eglCreateContext (shared) failed" << LL_ENDL;
            }
        }
        else
        {
            LL_WARNS() << "Could not resolve EGL state/entry points for shared context" << LL_ENDL;
        }
    }
#endif // LL_LINUX

    if (!ok)
    {
        delete shared;
        return nullptr;
    }

    LL_DEBUGS() << "Created native shared GL context." << LL_ENDL;
    return shared;
}

void sdl_make_shared_context_current(void* handle)
{
    if (!handle) return;
    auto* s = (LLSDLSharedContext*)handle;
#if LL_WINDOWS
    if (!wglMakeCurrent(s->dc, s->rc))
    {
        LL_WARNS("Window") << "wglMakeCurrent(shared) failed: " << GetLastError() << LL_ENDL;
    }
#elif LL_DARWIN
    CGLSetCurrentContext(s->ctx);
#else // LL_LINUX
    if (s->egl_ctx)
    {
        // eglBindAPI is per-thread, so re-assert OpenGL on the worker before
        // binding the context surfaceless.
        typedef unsigned int (*fn_bindapi)(unsigned int);
        typedef unsigned int (*fn_makecur)(void*, void*, void*, void*);
        auto egl_bindapi = (fn_bindapi)SDL_EGL_GetProcAddress("eglBindAPI");
        auto egl_makecur = (fn_makecur)SDL_EGL_GetProcAddress("eglMakeCurrent");
        if (egl_bindapi) egl_bindapi(EGL_OPENGL_API);
        if (egl_makecur && !egl_makecur(s->egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, s->egl_ctx))
        {
            LL_WARNS("Window") << "eglMakeCurrent(shared, surfaceless) failed" << LL_ENDL;
        }
    }
#endif // LL_LINUX
    LL_PROFILER_GPU_CONTEXT;
}

void sdl_destroy_shared_context(void* handle)
{
    if (!handle) return;
    auto* s = (LLSDLSharedContext*)handle;
#if LL_WINDOWS
    if (s->rc && !wglDeleteContext(s->rc))
    {
        LL_WARNS("Window") << "wglDeleteContext(shared) failed: " << GetLastError() << LL_ENDL;
    }
#elif LL_DARWIN
    if (s->ctx)
    {
        CGLDestroyContext(s->ctx);
    }
#else // LL_LINUX
    if (s->egl_ctx)
    {
        typedef unsigned int (*fn_destroyctx)(void*, void*);
        auto egl_destroyctx = (fn_destroyctx)SDL_EGL_GetProcAddress("eglDestroyContext");
        if (egl_destroyctx) egl_destroyctx(s->egl_dpy, s->egl_ctx);
    }
#endif
    delete s;
}
