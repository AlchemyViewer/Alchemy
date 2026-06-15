/**
 * @file llwindowsdl.cpp
 * @brief SDL implementation of LLWindow class
 * @author This module has many fathers, and it shows.
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

#include "linden_common.h"

#include "llwindowsdl.h"

#include "llwindowcallbacks.h"
#include "llkeyboardsdl.h"

#include "llerror.h"
#include "llgl.h"
#include "llstring.h"
#include "lldir.h"
#include "llfindlocale.h"
#include "llpreeditor.h"
#include "llsdl.h"

#include "SDL3_ttf/SDL_ttf.h"     // LLSplashScreenSDL status text
#include "SDL3_image/SDL_image.h" // LLSplashScreenSDL branded icon (PNG)

#if LL_LINUX
extern "C" {
# include "fontconfig/fontconfig.h"
}

// not necessarily available on random SDL platforms, so #if LL_LINUX
// for execv(), waitpid(), fork()
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>

#if LL_X11
LLWindowSDL::X11_DATA LLWindowSDL::sX11Data = {};
#endif
#if LL_WAYLAND
LLWindowSDL::WAYLAND_DATA LLWindowSDL::sWaylandData = {};
#endif
#endif // LL_LINUX

#if LL_DARWIN
#include <OpenGL/OpenGL.h>
#include <Carbon/Carbon.h>
#include <CoreServices/CoreServices.h>
#include <CoreGraphics/CGDisplayConfiguration.h>
#include <SDL3_image/SDL_image.h>

bool LLWindowSDL::sUseMultGL = false;
#endif

#if LL_WINDOWS
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "lldxhardware.h"

// DirectInput8 interface for llviewerjoystick / SpaceNavigator. Created once
// in the LLWindowSDL constructor; only needs the process module handle, not
// the window, so the SDL backend can provide the same access LLWindowWin32 does.
static LPDIRECTINPUT8 gSDLDirectInput8 = nullptr;
#endif

// Native shared-GL-context creation (see createSharedContext). The GLX/EGL
// entry points are resolved at runtime via SDL_GL_GetProcAddress /
// SDL_EGL_GetProcAddress (the viewer doesn't link libGL/libEGL directly under
// SDL), so we only need the platform types and tokens here.
#if LL_X11
#include <GL/glx.h>
#endif
#if LL_WAYLAND
#include <EGL/egl.h>
#endif

bool gHiDPISupport = true;

const S32 MAX_NUM_RESOLUTIONS = 200;
const S32 DEFAULT_REFRESH_RATE = 60;

//
// LLWindowSDL
//

// TOFU HACK -- (*exactly* the same hack as LLWindowMacOSX for a similar
// set of reasons): Stash a pointer to the LLWindowSDL object here and
// maintain in the constructor and destructor.  This assumes that there will
// be only one object of this class at any time.  Currently this is true.
static LLWindowSDL *gWindowImplementation = nullptr;

LLWindowSDL::LLWindowSDL(LLWindowCallbacks* callbacks,
                         const std::string& title, const std::string& name, S32 x, S32 y, S32 width,
                         S32 height, U32 flags,
                         bool fullscreen, bool clearBg,
                         bool enable_vsync, bool use_gl,
                         bool ignore_pixel_depth, U32 fsaa_samples)
        : LLWindow(callbacks, fullscreen, flags),
        mGamma(1.0f), mFlashing(false)
{
    if (!SDL_GL_LoadLibrary(nullptr))
    {
        // SDL3 returns bool; on failure GL functions won't be available.
        // The caller will hit a setupFailure when context creation fails,
        // but log the underlying cause here so the chain is visible.
        LL_WARNS() << "SDL_GL_LoadLibrary failed: " << SDL_GetError() << LL_ENDL;
    }

    // Initialize the keyboard
    gKeyboard = new LLKeyboardSDL();
    gKeyboard->setCallbacks(callbacks);

#if LL_WINDOWS
    // Init Direct Input - needed for joystick / Spacemouse (see llviewerjoystick).
    if (!gSDLDirectInput8)
    {
        LPDIRECTINPUT8 di8_interface = nullptr;
        if (DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
                               IID_IDirectInput8, (LPVOID*)&di8_interface, nullptr) == DI_OK)
        {
            gSDLDirectInput8 = di8_interface;
        }
    }
#endif

    // Assume 4:3 aspect ratio until we know better
    mNativeAspectRatio = 1024.f / 768.f;

    // Record the requested MSAA sample count so createContext can ask SDL for it.
    // Without this, the constructor argument was being dropped on the floor and
    // the Windows-backend setFSAASamples() pattern (which only stores) didn't
    // actually take effect because the GL attribute was never set.
    mFSAASamples = fsaa_samples;

    if (title.empty())
        mWindowTitle = "Alchemy";
    else
        mWindowTitle = title;

    // Zero the cursor pointer array before any path that might destroy them.
    // initCursors() does this too, but createContext() can fail before
    // initCursors runs, and the destructor's quitCursors() walks this array
    // unconditionally — uninitialised garbage there would crash on
    // SDL_DestroyCursor.
    for (int i = 0; i < UI_CURSOR_COUNT; ++i)
        mSDLCursors[i] = nullptr;

    // Stash the object pointer for OSMessageBox() BEFORE createContext().
    // A setupFailure() path inside createContext routes through OSMessageBox
    // → OSMessageBoxSDL → LLWindowSDL::getMainSDLWindow(), which reads
    // gWindowImplementation. If we set this after createContext, the error
    // dialog runs unparented.
    gWindowImplementation = this;

    // Create the GL context and set it up for windowed or fullscreen, as appropriate.
    if(createContext(x, y, width, height, 32, fullscreen, enable_vsync))
    {
        gGLManager.initWGL();
        gGLManager.initGL();
#if LL_WINDOWS
        // GL didn't always report a VRAM budget (notably Intel iGPUs); ask DXGI.
        LLDXHardware::updateVRAMBudgetFromDXGI();
#endif

        //start with arrow cursor
        initCursors();
        setCursor( UI_CURSOR_ARROW );
    }

    stop_glerror();
}

#if LL_LINUX
// The BMP cursor/icon tree (res-sdl/) is only shipped in the Linux bundle.
// macOS loads cursors from cursors_mac/*.tif (makeSDLCursorFromMacTIF) and
// Windows from the exe's embedded .cur resources (makeSDLCursorFromWin32), so
// this helper would be unused on those platforms — and the build is -Werror
// on unused static functions.
static SDL_Surface *Load_BMP_Resource(const char *basename)
{
    const int PATH_BUFFER_SIZE=1000;
    char path_buffer[PATH_BUFFER_SIZE]; /* Flawfinder: ignore */

    // Figure out where our BMP is living on the disk
    snprintf(path_buffer, PATH_BUFFER_SIZE-1, "%s%sres-sdl%s%s",
             gDirUtilp->getAppRODataDir().c_str(),
             gDirUtilp->getDirDelimiter().c_str(),
             gDirUtilp->getDirDelimiter().c_str(),
             basename);
    path_buffer[PATH_BUFFER_SIZE-1] = '\0';

    return SDL_LoadBMP(path_buffer);
}
#endif // LL_LINUX

void LLWindowSDL::setTitle(const std::string title)
{
    SDL_SetWindowTitle( mWindow, title.c_str() );
}

void LLWindowSDL::tryFindFullscreenSize( int &width, int &height )
{
    LL_INFOS() << "createContext: setting up fullscreen " << width << "x" << height << LL_ENDL;

    // If the requested width or height is 0, find the best default for the monitor.
    if(width == 0 || height == 0)
    {
        // Scan through the list of modes, looking for one which has:
        //      height between 700 and 800
        //      aspect ratio closest to the user's original mode
        S32 resolutionCount = 0;
        LLWindowResolution *resolutionList = getSupportedResolutions(resolutionCount);

        if(resolutionList != nullptr)
        {
            F32 closestAspect = 0;
            U32 closestHeight = 0;
            U32 closestWidth = 0;

            LL_INFOS() << "createContext: searching for a display mode, original aspect is " << mNativeAspectRatio << LL_ENDL;

            for(S32 i=0; i < resolutionCount; i++)
            {
                F32 aspect = (F32)resolutionList[i].mWidth / (F32)resolutionList[i].mHeight;

                LL_INFOS() << "createContext: width " << resolutionList[i].mWidth << " height " << resolutionList[i].mHeight << " aspect " << aspect << LL_ENDL;

                if( (resolutionList[i].mHeight >= 700) && (resolutionList[i].mHeight <= 800) &&
                    (fabs(aspect - mNativeAspectRatio) < fabs(closestAspect - mNativeAspectRatio)))
                {
                    LL_INFOS() << " (new closest mode) " << LL_ENDL;

                    // This is the closest mode we've seen yet.
                    closestWidth = resolutionList[i].mWidth;
                    closestHeight = resolutionList[i].mHeight;
                    closestAspect = aspect;
                }
            }

            width = closestWidth;
            height = closestHeight;
        }
    }

    if(width == 0 || height == 0)
    {
        // Mode search failed for some reason.  Use the old-school default.
        width = 1024;
        height = 768;
    }
}

bool LLWindowSDL::createContext(int x, int y, int width, int height, int bits, bool fullscreen, bool enable_vsync)
{
    LL_INFOS() << "createContext, fullscreen=" << fullscreen << " size=" << width << "x" << height << LL_ENDL;

    // captures don't survive contexts
    mGrabbyKeyFlags = 0;

    if (width == 0)
        width = 1024;
    if (height == 0)
        height = 768;
    if (x == 0)
        x = SDL_WINDOWPOS_UNDEFINED;
    if (y == 0)
        y = SDL_WINDOWPOS_UNDEFINED;

    mFullscreen = fullscreen;

    // Setup default backing colors
    GLint redBits{8}, greenBits{8}, blueBits{8}, alphaBits{8};
    GLint depthBits{ 24 };

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,   redBits);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, greenBits);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,  blueBits);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, alphaBits);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depthBits);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Multi-sample anti-aliasing. Driver may quietly downgrade to 0/2/4/8
    // if the requested sample count isn't supported.
    if (mFSAASamples > 0)
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, mFSAASamples);
    }
    else
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    }

    SDL_GLContextFlag context_flags{};
    if(LLRender::sGLCoreProfile)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#if LL_DARWIN
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        context_flags |= SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
#endif
    }

    if (gDebugGL)
    {
        context_flags |= SDL_GL_CONTEXT_DEBUG_FLAG;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, context_flags);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

    if(mFullscreen)
    {
        tryFindFullscreenSize(width, height);
    }

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, mWindowTitle.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, mFullscreen);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, gHiDPISupport);

    mWindow = SDL_CreateWindowWithProperties(props);
    if (mWindow == nullptr)
    {
        LL_WARNS() << "Window creation failure. SDL: " << SDL_GetError() << LL_ENDL;
        setupFailure("Window creation error", "Error", OSMB_OK);
        SDL_DestroyProperties(props);
        return false;
    }
    SDL_DestroyProperties(props); // Free properties once window is created

    // Create the context
    mContext = SDL_GL_CreateContext(mWindow);
    if(!mContext)
    {
        LL_WARNS() << "Cannot create GL context " << SDL_GetError() << LL_ENDL;
        setupFailure("GL Context creation error", "Error", OSMB_OK);
        return false;
    }

    if (!SDL_GL_MakeCurrent(mWindow, mContext))
    {
        LL_WARNS() << "Failed to make context current. SDL: " << SDL_GetError() << LL_ENDL;
        setupFailure("GL Context failed to set current failure", "Error", OSMB_OK);
        return false;
    }

    // If the caller requested fullscreen at a specific resolution, switch
    // from SDL3's default borderless-desktop-fullscreen mode to an
    // exclusive fullscreen mode at that resolution. width/height come from
    // either the user's saved-window-size setting or from tryFindFullscreenSize
    // above (which scans the display's supported modes); either way they are
    // a deliberate resolution request, not "whatever the desktop is."
    //
    // On Wayland compositors that don't allow client-driven mode changes
    // (most of them), SDL_SetWindowFullscreenMode quietly fails and we keep
    // the borderless default. On X11 + KMS this actually changes the
    // physical display mode.
    if (mFullscreen && width > 0 && height > 0)
    {
        SDL_DisplayMode mode = {};
        const SDL_DisplayID display = SDL_GetDisplayForWindow(mWindow);
        if (SDL_GetClosestFullscreenDisplayMode(display, width, height, 0.f,
                                                /*include_high_density_modes=*/false,
                                                &mode))
        {
            if (SDL_SetWindowFullscreenMode(mWindow, &mode))
            {
                LL_INFOS() << "Exclusive fullscreen mode: "
                           << mode.w << "x" << mode.h
                           << " @ " << mode.refresh_rate << "Hz" << LL_ENDL;
            }
            else
            {
                LL_WARNS() << "SDL_SetWindowFullscreenMode " << mode.w << "x" << mode.h
                           << " @ " << mode.refresh_rate << "Hz failed: "
                           << SDL_GetError() << " — staying at borderless desktop." << LL_ENDL;
            }
        }
        else
        {
            LL_INFOS() << "No fullscreen mode matches " << width << "x" << height
                       << " on display " << display
                       << " — using borderless desktop fullscreen." << LL_ENDL;
        }
    }

    // Prefer the window's actually-applied fullscreen mode (which reflects
    // any exclusive mode we just set) over the desktop's current mode. When
    // the window is borderless-desktop or windowed, SDL_GetWindowFullscreenMode
    // returns nullptr and we fall back to SDL_GetCurrentDisplayMode — the
    // pre-exclusive-mode-support behaviour.
    const SDL_DisplayMode* displayMode = SDL_GetWindowFullscreenMode(mWindow);
    if (!displayMode)
    {
        displayMode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(mWindow));
    }
    if(displayMode)
    {
        mRefreshRate = ll_round(displayMode->refresh_rate);
        mNativeAspectRatio = ((F32)displayMode->w) / ((F32)displayMode->h);
        if(mFullscreen)
        {
            mFullscreenWidth = displayMode->w;
            mFullscreenHeight = displayMode->h;
            mFullscreenRefresh = ll_round(displayMode->refresh_rate);

            LL_INFOS() << "Running at " << mFullscreenWidth
            << "x"   << mFullscreenHeight
            << " @ " << mFullscreenRefresh
            << LL_ENDL;
        }
    }
    else
    {
        LL_WARNS() << "Failed to get current display mode and refresh rate" << LL_ENDL;
        mRefreshRate = 0;
        mNativeAspectRatio = 1024.f / 768.f;
        if(mFullscreen) // Fallback to window size
        {
            SDL_GetWindowSize(mWindow, &mFullscreenWidth, &mFullscreenHeight);
            mFullscreenRefresh = -1;
        }
    }

    if(mRefreshRate == 0) // We failed to get a valid refresh rate above
    {
        mRefreshRate = DEFAULT_REFRESH_RATE;
    }

    /* Grab the window manager specific information */
#if LL_LINUX
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0)
    {
        LL_INFOS() << "Running under X11" << LL_ENDL;
        mServerProtocol = X11;

        gGLManager.mIsX11 = true;

#if LL_X11
        sX11Data.xdisplay = (Display *)SDL_GetPointerProperty(SDL_GetWindowProperties(mWindow), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        sX11Data.xwindow = (Window)SDL_GetNumberProperty(SDL_GetWindowProperties(mWindow), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        sX11Data.xscreen = (int)SDL_GetNumberProperty(SDL_GetWindowProperties(mWindow), SDL_PROP_WINDOW_X11_SCREEN_NUMBER, -1);
        if (sX11Data.xdisplay && sX11Data.xwindow)
        {

        }
#endif

        gGLManager.initGLX();
    }
    else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
    {
        LL_INFOS() << "Running under Wayland" << LL_ENDL;
        mServerProtocol = Wayland;

        gGLManager.mIsWayland = true;

#if LL_WAYLAND
        sWaylandData.display = (struct wl_display *)SDL_GetPointerProperty(SDL_GetWindowProperties(mWindow), SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        sWaylandData.surface = (struct wl_surface *)SDL_GetPointerProperty(SDL_GetWindowProperties(mWindow), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if (sWaylandData.display && sWaylandData.surface)
        {
        }
#endif

        gGLManager.initEGL();

        // NOTE: the Wayland init path used to unsetenv("DISPLAY") here to
        // coax dullahan/CEF onto the native Wayland path. That global env
        // mutation affected every other subprocess the viewer spawns, so
        // we dropped it. Routing CEF to Wayland needs a per-spawn fix on
        // the dullahan side (e.g. scrubbing DISPLAY only when launching
        // dullahan_host, or passing OZONE_PLATFORM=wayland via the launch
        // env). Until that lands, CEF will continue to use XWayland when
        // DISPLAY is set, which is the SDL2-era behaviour.
    }
#endif

    SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &redBits);
    SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &greenBits);
    SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &blueBits);
    SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &alphaBits);
    SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depthBits);

    LL_INFOS() << "GL buffer:" << LL_ENDL;
    LL_INFOS() << "  Red Bits " << S32(redBits) << LL_ENDL;
    LL_INFOS() << "  Green Bits " << S32(greenBits) << LL_ENDL;
    LL_INFOS() << "  Blue Bits " << S32(blueBits) << LL_ENDL;
    LL_INFOS() << "  Alpha Bits " << S32(alphaBits) << LL_ENDL;
    LL_INFOS() << "  Depth Bits " << S32(depthBits) << LL_ENDL;

    GLint colorBits = redBits + greenBits + blueBits + alphaBits;
    if (colorBits < 32)
    {
        setupFailure(
                "Alchemy Viewer requires True Color (32-bit) to run in a window.\n"
                "Please go to Control Panels -> Display -> Settings and\n"
                "set the screen to 32-bit color.\n"
                "Alternately, if you choose to run fullscreen, Alchemy Viewer\n"
                "will automatically adjust the screen each time it runs.",
                "Error",
                OSMB_OK);
        return false;
    }

    LL_PROFILER_GPU_CONTEXT;

    // Enable vertical sync
    toggleVSync(enable_vsync);

#if LL_DARWIN
    setUseMultGL(sUseMultGL);

    // Get vram via CGL on macos
    gGLManager.mVRAM = getVramSize();
#endif

#if LL_LINUX
    // Set the application icon.
    SDL_Surface* bmpsurface = Load_BMP_Resource("ll_icon.BMP");
    if (bmpsurface)
    {
        SDL_SetWindowIcon(mWindow, bmpsurface);
        SDL_DestroySurface(bmpsurface);
        bmpsurface = nullptr;
    }
#endif

    // SDL3 ties both committed-text events (SDL_EVENT_TEXT_INPUT) and
    // composition events (SDL_EVENT_TEXT_EDITING) to the same
    // SDL_StartTextInput flag. The viewer routes plain unicode chars
    // through TEXT_INPUT for things like menu jump keys
    // (LLMenuGL::handleUnicodeCharHere -> handleJumpKey), so we have to
    // leave text input on whenever the window is alive — gating it on
    // text-widget focus would kill the menu accelerator path.
    //
    // Composition routing IS per-widget: see allowLanguageTextInput() /
    // mPreeditor, which decides whether TEXT_EDITING events get dispatched
    // to a preeditor or dropped.
    if (!SDL_StartTextInput(mWindow))
    {
        LL_WARNS() << "SDL_StartTextInput failed: " << SDL_GetError() << LL_ENDL;
    }

    // Refresh the pixel-unit minimum-size shadow against this window's
    // density. setMinSize may have run before the window existed (then
    // density=1 was used as a fallback) or against a prior window with a
    // different density (after switchContext). Recompute now that mWindow
    // reflects the active display.
    refreshMinSizePixelShadow();

#if LL_WINDOWS
    // Hook WM_COPYDATA on the native HWND for second-instance SLURL hand-off.
    installWin32Subclass();
#endif

    return true;
}

void LLWindowSDL::refreshPixelMetrics()
{
    if (!mWindow)
    {
        return;
    }
    const float density = SDL_GetWindowPixelDensity(mWindow);
    mCachedPixelDensity = density > 0.f ? density : 1.f;
    S32 height_pixels = 0;
    SDL_GetWindowSizeInPixels(mWindow, nullptr, &height_pixels);
    mCachedWindowHeightPx = height_pixels;
}

void LLWindowSDL::refreshMinSizePixelShadow()
{
    refreshPixelMetrics();
    if (!mWindow || mMinWindowWidth <= 0 || mMinWindowHeight <= 0)
    {
        return;
    }
    mMinWindowWidthPx = (U32)(mMinWindowWidth * mCachedPixelDensity);
    mMinWindowHeightPx = (U32)(mMinWindowHeight * mCachedPixelDensity);
}

// Opaque handle returned from createSharedContext() and passed back to
// makeContextCurrent()/destroySharedContext(). Carries whatever the platform
// GL API needs to bind and tear down the context.
namespace
{
    struct LLSDLSharedContext
    {
#if LL_WINDOWS
        HGLRC rc = nullptr;
        HDC   dc = nullptr;        // main window DC the sibling context binds to
#elif LL_DARWIN
        CGLContextObj ctx = nullptr;
#else // LL_LINUX
#if LL_X11
        // X11 / GLX
        Display*   glx_dpy  = nullptr;
        GLXContext glx_ctx  = nullptr;
        GLXPbuffer glx_pbuf = 0;
#endif
#if LL_WAYLAND
        // Wayland / EGL — kept as void* so EGL types stay out of the header
        void* egl_dpy = nullptr;   // EGLDisplay
        void* egl_ctx = nullptr;   // EGLContext
#endif
#endif
    };

    // Platform GL teardown for one shared context. No bookkeeping — the caller
    // owns mSharedContexts and the LLSDLSharedContext allocation.
    void tearDownNativeSharedContext(LLSDLSharedContext* s)
    {
        if (!s) return;
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
#if LL_X11
        if (s->glx_ctx)
        {
            typedef void (*fn_destroyctx)(Display*, GLXContext);
            typedef void (*fn_destroypb)(Display*, GLXPbuffer);
            auto glx_destroyctx = (fn_destroyctx)SDL_GL_GetProcAddress("glXDestroyContext");
            auto glx_destroypb  = (fn_destroypb)SDL_GL_GetProcAddress("glXDestroyPbuffer");
            if (glx_destroyctx) glx_destroyctx(s->glx_dpy, s->glx_ctx);
            if (glx_destroypb && s->glx_pbuf) glx_destroypb(s->glx_dpy, s->glx_pbuf);
        }
#endif
#if LL_WAYLAND
        if (s->egl_ctx)
        {
            typedef unsigned int (*fn_destroyctx)(void*, void*);
            auto egl_destroyctx = (fn_destroyctx)SDL_EGL_GetProcAddress("eglDestroyContext");
            if (egl_destroyctx) egl_destroyctx(s->egl_dpy, s->egl_ctx);
        }
#endif
#endif
    }
}

// Create a GL context that shares object namespace with the main context, for
// a worker thread (texture upload, VBO streaming). Uses the platform-native GL
// API behind SDL instead of a hidden carrier SDL_Window — see the header note.
// Runs on the main thread (the thread that constructs the GL worker pool).
void* LLWindowSDL::createSharedContext()
{
    // Bind the main context so the platform "get current" queries below return
    // the main display / config / share-context, and so the new context shares
    // object namespace with the right context.
    if (!SDL_GL_MakeCurrent(mWindow, mContext))
    {
        LL_WARNS() << "SDL_GL_MakeCurrent(main) failed in createSharedContext: "
                   << SDL_GetError() << LL_ENDL;
    }

    // A version request derived from the live main context, clamped to the
    // range the viewer supports. WGL/EGL need this explicitly (mirroring
    // LLWindowWin32::createSharedContext); GLX/CGL inherit it from the share
    // context, hence the guard against an unused-variable warning there.
#if LL_WINDOWS || LL_WAYLAND
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
#if LL_X11
    if (mServerProtocol == X11)
    {
        // GLX: bind the shared context to a 1x1 offscreen GLXPbuffer (no window
        // manager interaction, destroyable from the worker thread). Each worker
        // needs its own drawable — reusing the main window drawable would
        // BadAccess (it's already current on the main thread).
        typedef Display*     (*fn_getdpy)(void);
        typedef GLXContext   (*fn_getctx)(void);
        typedef GLXFBConfig* (*fn_choose)(Display*, int, const int*, int*);
        typedef GLXContext   (*fn_newctx)(Display*, GLXFBConfig, int, GLXContext, Bool);
        typedef GLXPbuffer   (*fn_pbuffer)(Display*, GLXFBConfig, const int*);

        auto glx_getdpy  = (fn_getdpy)SDL_GL_GetProcAddress("glXGetCurrentDisplay");
        auto glx_getctx  = (fn_getctx)SDL_GL_GetProcAddress("glXGetCurrentContext");
        auto glx_choose  = (fn_choose)SDL_GL_GetProcAddress("glXChooseFBConfig");
        auto glx_newctx  = (fn_newctx)SDL_GL_GetProcAddress("glXCreateNewContext");
        auto glx_pbuffer = (fn_pbuffer)SDL_GL_GetProcAddress("glXCreatePbuffer");

        if (glx_getdpy && glx_getctx && glx_choose && glx_newctx && glx_pbuffer)
        {
            Display*   dpy   = glx_getdpy();
            GLXContext share = glx_getctx();
            int screen = (sX11Data.xscreen >= 0) ? sX11Data.xscreen : (dpy ? DefaultScreen(dpy) : 0);
            const int cfg_attribs[] =
            {
                GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
                GLX_RENDER_TYPE,   GLX_RGBA_BIT,
                GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
                None
            };
            int n = 0;
            GLXFBConfig* fbc = (dpy ? glx_choose(dpy, screen, cfg_attribs, &n) : nullptr);
            if (fbc && n > 0)
            {
                const int pb_attribs[] = { GLX_PBUFFER_WIDTH, 1, GLX_PBUFFER_HEIGHT, 1, None };
                GLXPbuffer pbuf = glx_pbuffer(dpy, fbc[0], pb_attribs);
                GLXContext ctx  = glx_newctx(dpy, fbc[0], GLX_RGBA_TYPE, share, True);
                // glXChooseFBConfig returns an Xlib-allocated array that must be
                // released with XFree. The SDL backend doesn't link libX11 (GLX
                // is reached through SDL_GL_GetProcAddress), so resolve XFree from
                // the already-resident libX11 at runtime via SDL's loader instead
                // of taking an X11 link dependency. On the X11 server path libX11
                // is always loaded; if XFree can't be found, skip the free (a
                // tiny, bounded, once-per-worker-context leak) rather than crash.
                if (SDL_SharedObject* x11lib = SDL_LoadObject("libX11.so.6"))
                {
                    if (SDL_FunctionPointer x_free = SDL_LoadFunction(x11lib, "XFree"))
                    {
                        ((int (*)(void*))x_free)(fbc);
                    }
                    SDL_UnloadObject(x11lib);
                }
                if (pbuf && ctx)
                {
                    shared->glx_dpy  = dpy;
                    shared->glx_pbuf = pbuf;
                    shared->glx_ctx  = ctx;
                    ok = true;
                }
                else
                {
                    LL_WARNS() << "GLX shared pbuffer/context creation failed" << LL_ENDL;
                    LLSDLSharedContext tmp; tmp.glx_dpy = dpy; tmp.glx_ctx = ctx; tmp.glx_pbuf = pbuf;
                    tearDownNativeSharedContext(&tmp);
                }
            }
            else
            {
                LL_WARNS() << "glXChooseFBConfig found no pbuffer-capable config" << LL_ENDL;
            }
        }
        else
        {
            LL_WARNS() << "Could not resolve GLX entry points for shared context" << LL_ENDL;
        }
    }
#endif // LL_X11
#if LL_WAYLAND
    if (mServerProtocol == Wayland)
    {
        // Wayland / EGL: a surfaceless context (EGL_NO_SURFACE) avoids needing a
        // per-worker drawable. Requires EGL_KHR_surfaceless_context (Mesa has
        // it). SDL exposes the display/config it created the main context with.
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
#endif // LL_WAYLAND
#endif // LL_LINUX

    if (!ok)
    {
        delete shared;
        return nullptr;
    }

    {
        LLMutexLock lk(&mSharedCtxMutex);
        mSharedContexts.insert(shared);
    }
    LL_DEBUGS() << "Created native shared GL context." << LL_ENDL;
    return shared;
}

void LLWindowSDL::makeContextCurrent(void* contextPtr)
{
    if (!contextPtr) return;
    auto* s = (LLSDLSharedContext*)contextPtr;
#if LL_WINDOWS
    if (!wglMakeCurrent(s->dc, s->rc))
    {
        LL_WARNS("Window") << "wglMakeCurrent(shared) failed: " << GetLastError() << LL_ENDL;
    }
#elif LL_DARWIN
    CGLSetCurrentContext(s->ctx);
#else // LL_LINUX
#if LL_X11
    if (s->glx_ctx)
    {
        typedef Bool (*fn_makecur)(Display*, GLXDrawable, GLXContext);
        auto glx_makecur = (fn_makecur)SDL_GL_GetProcAddress("glXMakeCurrent");
        if (glx_makecur && !glx_makecur(s->glx_dpy, s->glx_pbuf, s->glx_ctx))
        {
            LL_WARNS("Window") << "glXMakeCurrent(shared) failed" << LL_ENDL;
        }
    }
#endif
#if LL_WAYLAND
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
#endif
#endif // LL_LINUX
    LL_PROFILER_GPU_CONTEXT;
}

void LLWindowSDL::destroySharedContext(void* contextPtr)
{
    if (!contextPtr) return;
    auto* s = (LLSDLSharedContext*)contextPtr;
    tearDownNativeSharedContext(s);
    {
        LLMutexLock lk(&mSharedCtxMutex);
        mSharedContexts.erase(s);
    }
    delete s;
}

void LLWindowSDL::toggleVSync(bool enable_vsync)
{
    const int interval = enable_vsync ? 1 : 0;
    LL_INFOS("Window") << (enable_vsync ? "Enabling" : "Disabling") << " vertical sync" << LL_ENDL;
    if (!SDL_GL_SetSwapInterval(interval))
    {
        // Some compositors (notably Wayland) refuse interval=0; the driver may also reject it.
        LL_WARNS("Window") << "SDL_GL_SetSwapInterval(" << interval << ") failed: "
                           << SDL_GetError() << LL_ENDL;
    }
}

// changing fullscreen resolution, or switching between windowed and fullscreen mode.
bool LLWindowSDL::switchContext(bool fullscreen, const LLCoordScreen &size, bool enable_vsync, const LLCoordScreen * const posp)
{
    const bool needsRebuild = true;  // Just nuke the context and start over.
    bool result = true;

    LL_INFOS() << "switchContext, fullscreen=" << fullscreen << LL_ENDL;
    stop_glerror();
    if(needsRebuild)
    {
        destroyContext();
        result = createContext(0, 0, size.mX, size.mY, 32, fullscreen, enable_vsync);
        if (result)
        {
            gGLManager.initWGL();
            gGLManager.initGL();
#if LL_WINDOWS
            LLDXHardware::updateVRAMBudgetFromDXGI();
#endif

            //start with arrow cursor
            initCursors();
            setCursor( UI_CURSOR_ARROW );
        }
    }

    stop_glerror();

    return result;
}

void LLWindowSDL::destroyContext()
{
    LL_INFOS() << "destroyContext begins" << LL_ENDL;

    {
        // Reclaim any shared GL contexts a worker thread failed to release.
        // Normal shutdown joins the GL worker threads before destroyContext, so
        // this is defensive — a leftover here means a worker didn't call
        // destroySharedContext(). The native contexts can be torn down from the
        // main thread (unlike the old carrier-window path, no deferral needed).
        LLMutexLock lk(&mSharedCtxMutex);
        if (!mSharedContexts.empty())
        {
            LL_WARNS() << "destroyContext: " << mSharedContexts.size()
                       << " shared GL context(s) still alive at shutdown — releasing." << LL_ENDL;
            for (void* handle : mSharedContexts)
            {
                auto* s = (LLSDLSharedContext*)handle;
                tearDownNativeSharedContext(s);
                delete s;
            }
            mSharedContexts.clear();
        }
    }

#if LL_WINDOWS
    // Unhook WM_COPYDATA before SDL tears the HWND down.
    removeWin32Subclass();
#endif

    // Stop unicode input — paired with the SDL_StartTextInput in createContext().
    // Guard against the early-failure path where SDL_CreateWindowWithProperties
    // never gave us a window (setupFailure → close → destroyContext): mWindow
    // is null and SDL_StopTextInput would dereference it.
    if (mWindow)
    {
        SDL_StopTextInput(mWindow);
    }
    mPreeditor = nullptr;

    // Balance the SDL_DisableScreenSaver we may have issued on FOCUS_GAINED.
    // Normally SDL_Quit cleans this up, but quit_sdl() skips SDL_Quit when
    // the framework owns the lifecycle (SDL_MAIN_USE_CALLBACKS) — make sure
    // the screensaver inhibit doesn't outlive the viewer's window.
    SDL_EnableScreenSaver();

    // Clean up remaining GL state before blowing away window
    LL_INFOS() << "shutdownGL begins" << LL_ENDL;
    gGLManager.shutdownGL();

    LL_INFOS() << "Destroying SDL cursors" << LL_ENDL;
    quitCursors();

    if (mContext)
    {
        LL_INFOS() << "Destroying SDL GL Context" << LL_ENDL;
        SDL_GL_DestroyContext(mContext);
        mContext = nullptr;
    }
    else
    {
        LL_INFOS() << "SDL GL Context already destroyed" << LL_ENDL;
    }

    if (mWindow)
    {
        LL_INFOS() << "Destroying SDL Window" << LL_ENDL;
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
    }
    else
    {
        LL_INFOS() << "SDL Window already destroyed" << LL_ENDL;
    }

    // Reset per-window state so switchContext (which calls destroyContext +
    // createContext) doesn't inherit accumulators / pending warp suppression /
    // device classification from the prior window. Stale values would survive
    // a fullscreen toggle or display change and bias the first few frames of
    // the new context.
    mPendingWarpSuppressCount = 0;
    mMouseDeltaAccumX = 0.f;
    mMouseDeltaAccumY = 0.f;
    mScrollWheelAccumX = 0.f;
    mScrollWheelAccumY = 0.f;
    mHasDeferredCursorWarp = false;
    mAbsoluteCursorPosition = false;
    mRelativeMouseMode = false;
    mDialogDepth = 0;
    mDialogSavedRelativeMode = false;
    mPendingDropFiles.clear();
    mKeyVirtualKey = 0;
    mKeyModifiers = SDL_KMOD_NONE;
    mPenInProximity = false;
    mPenEraserTip = false;
    mPenPressure = 1.f;
    mPenTiltX = 0.f;
    mPenTiltY = 0.f;

    LL_INFOS() << "destroyContext end" << LL_ENDL;
}

LLWindowSDL::~LLWindowSDL()
{
    destroyContext();

    delete[] mSupportedResolutions;

#if LL_WINDOWS
    if (gSDLDirectInput8)
    {
        gSDLDirectInput8->Release();
        gSDLDirectInput8 = nullptr;
    }
#endif

    gWindowImplementation = nullptr;
}


void LLWindowSDL::show()
{
    if (mWindow)
    {
        SDL_ShowWindow(mWindow);
        SDL_RaiseWindow(mWindow);
    }
}

void LLWindowSDL::hide()
{
    if (mWindow)
    {
        SDL_HideWindow(mWindow);
    }
}

void LLWindowSDL::minimize()
{
    if (mWindow)
    {
        SDL_MinimizeWindow(mWindow);
    }
}

void LLWindowSDL::restore()
{
    if (mWindow)
    {
        SDL_RestoreWindow(mWindow);
    }
}

// close() destroys all OS-specific code associated with a window.
// Usually called from LLWindowManager::destroyWindow()
void LLWindowSDL::close()
{
    // Make sure cursor is visible and we haven't mangled the clipping state.
    setMouseClipping(false);
    showCursor();

    destroyContext();
}

bool LLWindowSDL::isValid()
{
    return mWindow != nullptr;
}

bool LLWindowSDL::getVisible()
{
    bool result = true;
    if (mWindow)
    {
        SDL_WindowFlags flags = SDL_GetWindowFlags(mWindow);
        if (flags & SDL_WINDOW_HIDDEN)
        {
            result = false;
        }
    }
    return result;
}

bool LLWindowSDL::getMinimized()
{
    bool result = false;
    if (mWindow)
    {
        SDL_WindowFlags flags = SDL_GetWindowFlags(mWindow);
        if (flags & SDL_WINDOW_MINIMIZED)
        {
            result = true;
        }
    }
    return result;
}

bool LLWindowSDL::getMaximized()
{
    bool result = false;
    if (mWindow)
    {
        SDL_WindowFlags flags = SDL_GetWindowFlags(mWindow);
        if (flags & SDL_WINDOW_MAXIMIZED)
        {
            result = true;
        }
    }

    return result;
}

bool LLWindowSDL::maximize()
{
    if (mWindow)
    {
        SDL_MaximizeWindow(mWindow);
        return true;
    }
    return false;
}

bool LLWindowSDL::getPosition(LLCoordScreen *position)
{
    if (mWindow)
    {
        SDL_GetWindowPosition(mWindow, &position->mX, &position->mY);
        return true;
    }
    return false;
}

bool LLWindowSDL::getSize(LLCoordScreen *size)
{
    if (mWindow)
    {
        SDL_GetWindowSize(mWindow, &size->mX, &size->mY);
        return true;
    }

    return false;
}

bool LLWindowSDL::getSize(LLCoordWindow *size)
{
    if (mWindow)
    {
        SDL_GetWindowSizeInPixels(mWindow, &size->mX, &size->mY);
        return true;
    }

    return false;
}

bool LLWindowSDL::setPosition(const LLCoordScreen position)
{
    if (mWindow)
    {
        SDL_SetWindowPosition(mWindow, position.mX, position.mY);
        return true;
    }

    return false;
}

template< typename T > bool setSizeImpl( const T& newSize, SDL_Window *pWin )
{
    if( !pWin )
        return false;

    SDL_WindowFlags winFlags = SDL_GetWindowFlags(pWin);

    if( winFlags & SDL_WINDOW_MAXIMIZED)
        SDL_RestoreWindow(pWin);

    SDL_SetWindowSize(pWin, newSize.mX, newSize.mY);
    return true;
}

bool LLWindowSDL::setSizeImpl(const LLCoordScreen size)
{
    return ::setSizeImpl( size, mWindow );
}

bool LLWindowSDL::setSizeImpl(const LLCoordWindow size)
{
    if (!mWindow) return false;

    // Re-clamp against the pixel-unit minimum. The base setSize(LLCoordWindow)
    // already ran its clamp, but against mMinWindowWidth/Height which is in
    // screen-coord units on this backend — that under-clamps on HiDPI, so a
    // request for a window smaller than the configured minimum would slip
    // through to SDL which then enforces its own (logical) minimum, blowing
    // the actual window much larger than the user asked for.
    LLCoordWindow clamped = size;
    clamped.mX = llmax(clamped.mX, (S32)mMinWindowWidthPx);
    clamped.mY = llmax(clamped.mY, (S32)mMinWindowHeightPx);

    // LLCoordWindow is in pixel units on this backend (see the coord-space
    // contract below convertCoords). SDL_SetWindowSize expects screen-coord
    // (logical) units — without scaling, asking for 1920x1080 client pixels
    // on a 2x display would create a 960x540 window. Convert by inverse
    // pixel density so a pixel request produces that many pixels on screen.
    const float density = SDL_GetWindowPixelDensity(mWindow);
    const float div = density > 0.f ? density : 1.f;

    LLCoordScreen logical_size;
    logical_size.mX = (S32)((F32)clamped.mX / div);
    logical_size.mY = (S32)((F32)clamped.mY / div);
    return ::setSizeImpl(logical_size, mWindow);
}


void LLWindowSDL::swapBuffers()
{
    if (mWindow)
    {
        SDL_GL_SwapWindow(mWindow);
    }
    LL_PROFILER_GPU_COLLECT;
}

U32 LLWindowSDL::getFSAASamples()
{
    return mFSAASamples;
}

void LLWindowSDL::setFSAASamples(const U32 samples)
{
    mFSAASamples = samples;
}

F32 LLWindowSDL::getGamma()
{
    return 1.f / mGamma;
}

bool LLWindowSDL::restoreGamma()
{
    // SDL3 removed SDL_SetWindowGammaRamp; nothing to restore at the OS level.
    return true;
}

bool LLWindowSDL::setGamma(const F32 gamma)
{
    if (mWindow)
    {
        mGamma = gamma;
        if (mGamma == 0)
            mGamma = 0.1f;
        mGamma = 1.f / mGamma;

        // SDL3 dropped hardware gamma-ramp support. We still record the value so
        // callers see a coherent get/set, but it doesn't change pixels until the
        // shader pipeline grows a software gamma stage.
        static bool warned = false;
        if (!warned)
        {
            LL_WARNS("Window") << "Hardware gamma is unavailable on SDL3; "
                               << "the brightness preference is currently a no-op." << LL_ENDL;
            warned = true;
        }
    }
    return true;
}

bool LLWindowSDL::isCursorHidden()
{
    return mCursorHidden;
}

// Constrains the mouse to the window.
void LLWindowSDL::setMouseClipping(bool b)
{
    if (mWindow)
    {
        SDL_SetWindowMouseGrab(mWindow, b);
    }
}

// virtual
void LLWindowSDL::setMinSize(U32 min_width, U32 min_height, bool enforce_immediately)
{
    // Callers pass min sizes in screen-coord (logical) units — the historical
    // Win32 convention and what SDL_SetWindowMinimumSize expects directly.
    // The base class stores those values as-is and uses them to clamp
    // setSize(LLCoordScreen) (correct) AND setSize(LLCoordWindow) (wrong on
    // this backend, since LLCoordWindow is in pixels). Maintain a pixel-unit
    // shadow that setSizeImpl(LLCoordWindow) can re-clamp against.
    LLWindow::setMinSize(min_width, min_height, enforce_immediately);

    refreshMinSizePixelShadow();

    if (mWindow && min_width > 0 && min_height > 0)
    {
        SDL_SetWindowMinimumSize(mWindow, min_width, min_height);
    }
}

bool LLWindowSDL::setCursorPosition(const LLCoordWindow position)
{
    if (!mWindow) return false;

    // In relative mouse mode the cursor is parked by SDL and warping is both
    // pointless and harmful — some SDL backends emit a synthetic xrel for
    // the warp distance which would slam the camera. The viewer's caller
    // (LLViewerWindow::moveCursorToCenter) zeroes its own delta state for
    // the same reason; we just zero ours and skip the warp.
    //
    // Stash the requested position so showCursor() can apply it as a real
    // warp on exit. The viewer's tool exit code (LLToolCamera::handleMouseUp
    // → setMousePositionScreen(mMouseDownX, mMouseDownY)) calls us while
    // still in relative mode; without deferring, the cursor would reappear
    // wherever SDL parked it instead of at the requested point.
    if (mRelativeMouseMode)
    {
        mDeferredCursorWarp = position;
        mHasDeferredCursorWarp = true;
        mMouseDeltaAccumX = 0.f;
        mMouseDeltaAccumY = 0.f;
        return true;
    }

    // LLCoordWindow is in window-relative PIXEL units, but
    // SDL_WarpMouseInWindow expects screen-coord (logical) units —
    // divide by pixel density to convert. See the coord-space contract
    // comment below convertCoords for the unit conventions.
    const float density = SDL_GetWindowPixelDensity(mWindow);
    const float div = density > 0.f ? density : 1.f;
    const float target_logical_x = (F32)position.mX / div;
    const float target_logical_y = (F32)position.mY / div;

    // Tell the next MOUSE_MOTION handler to drop the synthetic motion event
    // SDL3 queues for this warp — its xrel/yrel carry the warp distance,
    // not user input, and feeding them into the accumulator would partially
    // cancel real motion every alt-cam recenter frame. Only mark "pending"
    // if the warp will actually move the cursor; otherwise SDL3 won't emit
    // a synthetic event and we'd eat a real motion event by accident.
    float curr_x = 0.f, curr_y = 0.f;
    SDL_GetMouseState(&curr_x, &curr_y);
    if (fabsf(target_logical_x - curr_x) > 0.5f ||
        fabsf(target_logical_y - curr_y) > 0.5f)
    {
        ++mPendingWarpSuppressCount;
    }

    SDL_WarpMouseInWindow(mWindow, target_logical_x, target_logical_y);

    // Drop whatever was queued before the warp — the cursor has just
    // teleported, so any pre-warp motion no longer reflects "where the
    // user is relative to where they were."
    mMouseDeltaAccumX = 0.f;
    mMouseDeltaAccumY = 0.f;
    return true;
}

bool LLWindowSDL::getCursorPosition(LLCoordWindow *position)
{
    if (!position || !mWindow) return false;

    // In relative mouse mode the OS cursor is parked and SDL_GetMouseState
    // returns either the lock position or stale pre-lock coordinates. Report
    // the window center so LLViewerWindow::updateMouseDelta's
    // "is the cursor inside the window?" test keeps mMouseInWindow=true,
    // matching the actual pointer-lock semantics.
    if (mRelativeMouseMode)
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(mWindow, &w, &h);
        position->mX = w / 2;
        position->mY = h / 2;
        return true;
    }

    // SDL3 SDL_GetMouseState returns the cursor position relative to the
    // focus window in screen-coord (logical) units. Multiply by pixel
    // density so the result is in window-relative PIXEL units, matching
    // the unit convention LLCoordWindow holds on this backend (see the
    // coord-space contract comment below convertCoords). Without the
    // density scale, comparing the result against mWindowRectRaw (which
    // the viewer stores in pixels) silently mis-tests window bounds on
    // HiDPI displays.
    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    const float density = SDL_GetWindowPixelDensity(mWindow);
    const float scale = density > 0.f ? density : 1.f;
    position->mX = llfloor(x * scale);
    position->mY = llfloor(y * scale);
    return true;
}

bool LLWindowSDL::getCursorDelta(LLCoordCommon* delta)
{
    if (!delta) return false;

    // Return the integer part of the accumulated motion and carry the
    // sub-pixel residue forward so fractional deltas at very high frame
    // rates (where mouse motion is < 1 pixel/frame) integrate over time
    // instead of being silently rounded to zero each frame. The
    // accumulator is fed by SDL_EVENT_MOUSE_MOTION below.
    //
    // Use lltrunc (toward-zero) rather than llfloor: llfloor(-0.1) = -1
    // would amplify sub-pixel negative noise into a -1 pixel per several
    // frames of constant drift. Toward-zero truncation only emits a
    // non-zero delta once the accumulator's magnitude actually reaches 1.
    const S32 ix = lltrunc(mMouseDeltaAccumX);
    const S32 iy = lltrunc(mMouseDeltaAccumY);
    mMouseDeltaAccumX -= (F32)ix;
    mMouseDeltaAccumY -= (F32)iy;
    delta->mX = ix;
    delta->mY = iy;
    return true;
}

F32 LLWindowSDL::getNativeAspectRatio()
{
    if (mOverrideAspectRatio > 0.f)
    {
        return mOverrideAspectRatio;
    }
    else if (mNativeAspectRatio > 0.f)
    {
        // we grabbed this value at startup, based on the user's desktop settings
        return mNativeAspectRatio;
    }

    // RN: this hack presumes that the largest supported resolution is monitor-limited
    // and that pixels in that mode are square, therefore defining the native aspect ratio
    // of the monitor...this seems to work to a close approximation for most CRTs/LCDs
    S32 num_resolutions;
    LLWindowResolution* resolutions = getSupportedResolutions(num_resolutions);
    if (!resolutions || num_resolutions <= 0)
    {
        // No usable display modes (e.g. Wayland exposes none, or nothing met the
        // 800x600 floor). Fall back to the 4:3 assumption rather than indexing
        // resolutions[-1] and dividing by a garbage height.
        return 1024.f / 768.f;
    }

    return ((F32)resolutions[num_resolutions - 1].mWidth / (F32)resolutions[num_resolutions - 1].mHeight);
}

F32 LLWindowSDL::getPixelAspectRatio()
{
    F32 pixel_aspect = 1.f;
    if (getFullscreen())
    {
        LLCoordScreen screen_size;
        if (getSize(&screen_size))
        {
            pixel_aspect = getNativeAspectRatio() * (F32)screen_size.mY / (F32)screen_size.mX;
        }
    }

    return pixel_aspect;
}


// This is to support 'temporarily windowed' mode so that
// dialogs are still usable in fullscreen.
void LLWindowSDL::beforeDialog()
{
    LL_INFOS() << "LLWindowSDL::beforeDialog() depth=" << mDialogDepth << LL_ENDL;

    // Only the outermost beforeDialog/afterDialog pair captures and
    // restores window state. Nested dialogs (e.g. a device-loss notice
    // popping up while a file picker is open) MUST NOT re-snapshot
    // mDialogSavedRelativeMode — the outer call already dropped relative
    // mode, so the nested call would read `false` and clobber the true
    // value the outer pair needs to know to restore.
    if (mDialogDepth == 0)
    {
        // If the user is in mouselook / a tool with pointer-lock, the
        // cursor is parked and invisible. A modal dialog popping up in
        // that state is a dead-end — the user can't see their cursor to
        // click it. Drop relative mode for the dialog's duration;
        // afterDialog re-enters if the viewer still wants pointer-lock
        // (mHideCursorPermanent stays set throughout because we don't
        // call showCursor here).
        mDialogSavedRelativeMode = mRelativeMouseMode;
        if (mRelativeMouseMode && mWindow)
        {
            SDL_SetWindowRelativeMouseMode(mWindow, false);
            mRelativeMouseMode = false;
            SDL_ShowCursor();
        }

        if (SDLReallyCaptureInput(false)) // must ungrab input so popup works!
        {
            if (mFullscreen && mWindow )
                SDL_SetWindowFullscreen( mWindow, 0 );
        }
    }
    ++mDialogDepth;
}

void LLWindowSDL::afterDialog()
{
    if (mDialogDepth > 0)
    {
        --mDialogDepth;
    }
    LL_INFOS() << "LLWindowSDL::afterDialog() depth=" << mDialogDepth << LL_ENDL;

    if (mDialogDepth > 0)
    {
        // Nested afterDialog: still inside an outer dialog scope, nothing
        // to restore yet.
        return;
    }

    if (mFullscreen && mWindow)
    {
        // Restore fullscreen state that beforeDialog() left so dialogs could draw above us.
        SDL_SetWindowFullscreen(mWindow, true);
    }

    // Restore pointer-lock if we dropped it for the dialog AND the viewer is
    // still in a mouselook/grab session (mHideCursorPermanent is the signal
    // for that — beforeDialog doesn't touch it, so it's a reliable indicator).
    if (mDialogSavedRelativeMode && mHideCursorPermanent && mWindow
        && !mAbsoluteCursorPosition)
    {
        if (SDL_SetWindowRelativeMouseMode(mWindow, true))
        {
            mRelativeMouseMode = true;
            // Don't drag stale motion into the resumed session.
            mMouseDeltaAccumX = 0.f;
            mMouseDeltaAccumY = 0.f;
            mPendingWarpSuppressCount = 0;
        }
    }
    mDialogSavedRelativeMode = false;
}

void LLWindowSDL::flashIcon(F32 seconds)
{
    LL_INFOS() << "LLWindowSDL::flashIcon(" << seconds << ")" << LL_ENDL;

    F32 remaining_time = mFlashTimer.getRemainingTimeF32();
    if (remaining_time < seconds)
        remaining_time = seconds;
    mFlashTimer.reset();
    mFlashTimer.setTimerExpirySec(remaining_time);

    if (!mWindow)
        return;
    SDL_FlashWindow(mWindow, SDL_FLASH_UNTIL_FOCUSED);
    mFlashing = true;
}

void LLWindowSDL::maybeStopFlashIcon()
{
    if (mFlashing && mFlashTimer.hasExpired())
    {
        mFlashing = false;
        if (mWindow)
            SDL_FlashWindow( mWindow, SDL_FLASH_CANCEL );
    }
}

bool LLWindowSDL::isClipboardTextAvailable()
{
    return SDL_HasClipboardText();
}

bool LLWindowSDL::pasteTextFromClipboard(LLWString &dst)
{
    if (isClipboardTextAvailable())
    {
        char* data = SDL_GetClipboardText();
        if (data)
        {
            dst = LLWString(utf8str_to_wstring(data));
            SDL_free(data);
            return true;
        }
    }
    return false;
}

bool LLWindowSDL::copyTextToClipboard(const LLWString& text)
{
    const std::string utf8 = wstring_to_utf8str(text);
    return SDL_SetClipboardText(utf8.c_str());
}

bool LLWindowSDL::isPrimaryTextAvailable()
{
    return SDL_HasPrimarySelectionText();
}

bool LLWindowSDL::pasteTextFromPrimary(LLWString &dst)
{
    if (isPrimaryTextAvailable())
    {
        char* data = SDL_GetPrimarySelectionText();
        if (data)
        {
            dst = LLWString(utf8str_to_wstring(data));
            SDL_free(data);
            return true;
        }
    }
    return false;
}

bool LLWindowSDL::copyTextToPrimary(const LLWString& text)
{
    const std::string utf8 = wstring_to_utf8str(text);
    return SDL_SetPrimarySelectionText(utf8.c_str());
}

LLWindow::LLWindowResolution* LLWindowSDL::getSupportedResolutions(S32 &num_resolutions)
{
    if (!mSupportedResolutions)
    {
        mSupportedResolutions = new LLWindowResolution[MAX_NUM_RESOLUTIONS];
        mNumSupportedResolutions = 0;

        SDL_DisplayID display = SDL_GetPrimaryDisplay();
        int num_modes = 0;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &num_modes);
        num_modes = llclamp(num_modes, 0, MAX_NUM_RESOLUTIONS);
        if (modes) {
            for (int i = 0; i < num_modes; ++i) {
                SDL_DisplayMode *mode = modes[i];
                int w = mode->w;
                int h = mode->h;
                if ((w >= 800) && (h >= 600))
                {
                    // make sure we don't add the same resolution multiple times!
                    // A row is "the same" only when both width AND height match the previous row;
                    // if either dimension differs we keep it.
                    if ( (mNumSupportedResolutions == 0) ||
                        ((mSupportedResolutions[mNumSupportedResolutions-1].mWidth != w) ||
                         (mSupportedResolutions[mNumSupportedResolutions-1].mHeight != h)) )
                    {
                        mSupportedResolutions[mNumSupportedResolutions].mWidth = w;
                        mSupportedResolutions[mNumSupportedResolutions].mHeight = h;
                        mNumSupportedResolutions++;
                    }
                }
            }
            SDL_free(modes);
        }
    }

    num_resolutions = mNumSupportedResolutions;
    return mSupportedResolutions;
}

//static
SDL_Window* LLWindowSDL::getMainSDLWindow()
{
    return gWindowImplementation ? gWindowImplementation->mWindow : nullptr;
}

//static
std::vector<std::string> LLWindowSDL::getDisplaysResolutionList()
{
    std::vector<std::string> ret;
    if (gWindowImplementation)
    {
        S32 resolutionCount = 0;
        LLWindowResolution* resolutionList = gWindowImplementation->getSupportedResolutions(resolutionCount);
        if (resolutionList != nullptr)
        {
            for (S32 i = 0; i < resolutionCount; i++)
            {
                const LLWindowResolution& resolution = resolutionList[i];
                ret.push_back(std::to_string(resolution.mWidth) + "x" + std::to_string(resolution.mHeight));
            }
        }
    }
    return ret;
}


// =====================================================================
// Coordinate-space contract on the SDL3 backend
// =====================================================================
//
// SDL3 has two distinct coordinate units that this code shuttles between:
//
//   * "screen coordinates" — the logical-pixel space SDL3 uses internally
//     for mouse-event delivery, SDL_GetWindowSize, SDL_GetWindowPosition,
//     SDL_GetMouseState, SDL_WarpMouseInWindow, SDL_SetTextInputArea, etc.
//     On a 1.0x display this equals pixels; on HiDPI (Retina, fractional
//     Wayland scaling) one screen-coord unit covers `pixel_density`
//     physical pixels.
//
//   * "pixels" — the actual back-buffer / GL viewport units, reported by
//     SDL_GetWindowSizeInPixels and SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED.
//
// The ratio `pixel_density = pixel_w / screen_coord_w` is queried with
// SDL_GetWindowPixelDensity().
//
// LLWindowSDL converts at the SDL boundary so the LL coord types speak
// consistent units regardless of what unit SDL handed them:
//
//   LLCoordWindow — window-relative PIXEL units (Y-down). This matches
//                   the Win32 / macOS backends, and matches the unit the
//                   viewer's mWindowRectRaw and pixel-coordinate hit
//                   testing already use. Mouse-event handlers above scale
//                   event.motion.x/y by pixel_density before storing into
//                   LLCoordWindow; getCursorPosition does the same.
//   LLCoordGL     — window-relative PIXEL units (Y-up, GL clip-space
//                   convention). Window<->GL conversion is a Y-flip,
//                   nothing else; both spaces share the unit.
//   LLCoordScreen — DESKTOP-relative SCREEN-COORD (logical) units. This
//                   is what SDL_GetWindowPosition reports, so we keep
//                   LLCoordScreen in the same unit. On Wayland
//                   SDL_GetWindowPosition returns (0, 0) (Wayland
//                   intentionally hides absolute window position), so
//                   convertCoords involving LLCoordScreen degenerates to
//                   a density-only conversion there.
//
// Boundary conversions:
//   * Window<->GL: pure Y-flip using SDL_GetWindowSizeInPixels for the
//     height. Both sides are pixels.
//   * Window<->Screen: cross-unit. Window->Screen divides pixels by
//     density and adds the window's screen-coord desktop position.
//     Screen->Window subtracts the desktop position and multiplies by
//     density.
// =====================================================================

bool LLWindowSDL::convertCoords(LLCoordGL from, LLCoordWindow *to)
{
    if (!to || !mWindow)
        return false;

    // Both spaces are window-relative PIXEL units. Y-flip using the pixel
    // window height; the X axis is identity. Y-down LLCoordWindow has
    // mY = 0 at the top, Y-up LLCoordGL has mY = 0 at the bottom.
    // Use the cached pixel height (refreshed on every resize/DPI change) to skip
    // a GetClientRect syscall on the per-event conversion path; fall back to a
    // live query before the first refresh (height still 0).
    S32 height_pixels = mCachedWindowHeightPx;
    if (height_pixels <= 0)
    {
        SDL_GetWindowSizeInPixels(mWindow, nullptr, &height_pixels);
    }

    to->mX = from.mX;
    to->mY = height_pixels - from.mY - 1;

    return true;
}

bool LLWindowSDL::convertCoords(LLCoordWindow from, LLCoordGL* to)
{
    if (!to || !mWindow)
        return false;

    // Inverse of the above — Y-flip in pixels, X is identity. Cached height (see
    // the forward conversion above) with a live fallback before the first refresh.
    S32 height_pixels = mCachedWindowHeightPx;
    if (height_pixels <= 0)
    {
        SDL_GetWindowSizeInPixels(mWindow, nullptr, &height_pixels);
    }

    to->mX = from.mX;
    to->mY = height_pixels - from.mY - 1;

    return true;
}

bool LLWindowSDL::convertCoords(LLCoordScreen from, LLCoordWindow* to)
{
    if (!to || !mWindow)
        return false;

    // LLCoordScreen is desktop-relative screen-coord (logical) units;
    // LLCoordWindow is window-relative pixels. Subtract the window's
    // desktop position (which SDL_GetWindowPosition reports in screen-
    // coord units), then scale the resulting window-relative screen-
    // coord into pixels.
    //
    // SDL_GetWindowPosition returns (0, 0) on Wayland by design (no
    // absolute window position is exposed to clients); this degenerates
    // to a pure density scale there — the best we can do.
    int win_x = 0, win_y = 0;
    SDL_GetWindowPosition(mWindow, &win_x, &win_y);
    const float density = SDL_GetWindowPixelDensity(mWindow);
    const float scale = density > 0.f ? density : 1.f;

    to->mX = llfloor((from.mX - win_x) * scale);
    to->mY = llfloor((from.mY - win_y) * scale);
    return true;
}

bool LLWindowSDL::convertCoords(LLCoordWindow from, LLCoordScreen *to)
{
    if (!to || !mWindow)
        return false;

    // Inverse: pixel window-relative -> desktop-relative screen-coord.
    // Divide pixels by density to recover screen-coord, then add the
    // window's desktop position.
    int win_x = 0, win_y = 0;
    SDL_GetWindowPosition(mWindow, &win_x, &win_y);
    const float density = SDL_GetWindowPixelDensity(mWindow);
    const float div = density > 0.f ? density : 1.f;

    to->mX = llfloor(from.mX / div) + win_x;
    to->mY = llfloor(from.mY / div) + win_y;
    return true;
}

bool LLWindowSDL::convertCoords(LLCoordScreen from, LLCoordGL *to)
{
    LLCoordWindow window_coord;
    return convertCoords(from, &window_coord) && convertCoords(window_coord, to);
}

bool LLWindowSDL::convertCoords(LLCoordGL from, LLCoordScreen *to)
{
    LLCoordWindow window_coord;
    return convertCoords(from, &window_coord) && convertCoords(window_coord, to);
}

void LLWindowSDL::setupFailure(const std::string& text, const std::string& caption, U32 type)
{
    close();

    OSMessageBox(text, caption, type);
}

bool LLWindowSDL::SDLReallyCaptureInput(bool capture)
{
    // note: this used to be safe to call nestedly, but in the
    // end that's not really a wise usage pattern, so don't.

    if (capture)
        mReallyCapturedCount = 1;
    else
        mReallyCapturedCount = 0;

    bool wantGrab;
    if (mReallyCapturedCount <= 0) // uncapture
    {
        wantGrab = false;
    } else // capture
    {
        wantGrab = true;
    }

    if (mReallyCapturedCount < 0) // yuck, imbalance.
    {
        mReallyCapturedCount = 0;
        LL_WARNS() << "ReallyCapture count was < 0" << LL_ENDL;
    }

    bool newGrab = wantGrab;
    if (!mFullscreen) /* only bother if we're windowed anyway */
    {
        if (wantGrab)
        {
            newGrab = SDL_SetWindowMouseGrab(mWindow, true);
            if (!newGrab)
            {
                LL_WARNS() << "SDL_SetWindowMouseGrab(true) failed: " << SDL_GetError() << LL_ENDL;
            }
        }
        else
        {
            newGrab = false;
            if (!SDL_SetWindowMouseGrab(mWindow, false))
            {
                LL_WARNS() << "SDL_SetWindowMouseGrab(false) failed: " << SDL_GetError() << LL_ENDL;
            }
        }
    }

    // return boolean success for whether we ended up in the desired state
    return capture == newGrab;
}

U32 LLWindowSDL::SDLCheckGrabbyKeys(U32 keysym, bool gain)
{
    /* part of the fix for SL-13243: Some popular window managers like
       to totally eat alt-drag for the purposes of moving windows.  We
       spoil their day by acquiring the exclusive X11 mouse lock for as
       long as ALT is held down, so the window manager can't easily
       see what's happening.  Tested successfully with Metacity.
       And... do the same with CTRL, for other darn WMs.  We don't
       care about other metakeys as SL doesn't use them with dragging
       (for now). */

    /* We maintain a bitmap of critical keys which are up and down
       instead of simply key-counting, because SDL sometimes reports
       misbalanced keyup/keydown event pairs to us for whatever reason. */

    U32 mask = 0;
    switch (keysym)
    {
        case SDLK_LALT:
            mask = 1U << 0; break;
        case SDLK_RALT:
            mask = 1U << 1; break;
        case SDLK_LCTRL:
            mask = 1U << 2; break;
        case SDLK_RCTRL:
            mask = 1U << 3; break;
        default:
            break;
    }

    if (gain)
        mGrabbyKeyFlags |= mask;
    else
        mGrabbyKeyFlags &= ~mask;

    //LL_INFOS() << "mGrabbyKeyFlags=" << mGrabbyKeyFlags << LL_ENDL;

    /* 0 means we don't need to mousegrab, otherwise grab. */
    return mGrabbyKeyFlags;
}

// virtual
void LLWindowSDL::processMiscNativeEvents()
{
    // Native shared GL contexts (createSharedContext) are torn down directly by
    // the worker thread in destroySharedContext() — there is no deferred
    // main-thread window-destruction queue to drain here anymore.
}

void LLWindowSDL::gatherInput()
{
    // This is for the case where SDL is not driving the main event loop
    if(!gSDLMainHandled)
    {
        SDL_Event event;

        // Handle all outstanding SDL events
        while (SDL_PollEvent(&event))
        {
            handleEvent(event);
        }
    }

    updateCursor();

    // This is a good time to stop flashing the icon if our mFlashTimer has
    // expired.
    if (mFlashing && mFlashTimer.hasExpired())
    {
        if (mWindow)
            SDL_FlashWindow(mWindow, SDL_FLASH_CANCEL);
        mFlashing = false;
    }
}

SDL_AppResult LLWindowSDL::handleEvent(const SDL_Event& event)
{
    switch(event.type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            // SDL3 synthesises mouse-motion events for touchscreens / stylus
            // with event.motion.which set to SDL_TOUCH_MOUSEID /
            // SDL_PEN_MOUSEID — matches LLWindowWin32's mAbsoluteCursorPosition.
            const bool from_absolute_device =
                    (event.motion.which == SDL_TOUCH_MOUSEID
                     || event.motion.which == SDL_PEN_MOUSEID);

            // Drop synthetic motion events generated by SDL_WarpMouseInWindow.
            // Without this, every alt-cam recenter-warp (or any other warp
            // we issue in non-relative mode) feeds its warp distance into
            // the accumulator on the next frame, partially cancelling real
            // user motion. The viewer-side caller has already set its
            // notion of the cursor position to the warp target, so dropping
            // the event entirely leaves consistent state.
            //
            // Only mouse events can be synthetic warps — touch/pen emulated
            // motion never is — so gate the suppress consumption on the
            // source. Otherwise a touch sample that arrives between our
            // warp call and SDL's synthetic event would consume the
            // suppress and let the synthetic warp slip through.
            if (!from_absolute_device && mPendingWarpSuppressCount > 0)
            {
                --mPendingWarpSuppressCount;
                break;
            }

            // Defer the device-class update until after the suppress check
            // so a suppressed real-mouse warp event doesn't override a
            // previous touch/pen classification with the warp's mouse-source
            // ID (the user is still on touch; the synthetic was just SDL
            // bookkeeping for our warp call).
            mAbsoluteCursorPosition = from_absolute_device;

            // event.motion.x/y are SDL3 screen-coord (logical) units. Scale to
            // PIXEL units so LLCoordWindow stays in the same unit as
            // mWindowRectRaw and the rest of the viewer's pixel-based hit
            // testing — see the coord-space contract below convertCoords.
            // Cached density (refreshed on resize/DPI change) — avoids a
            // GetClientRect syscall per motion event (up to the mouse poll rate).
            const float scale = mCachedPixelDensity;

            // Always accumulate the per-event relative motion (xrel/yrel) so
            // getCursorDelta() — drained once per frame from
            // LLViewerWindow::updateMouseDelta — sees every sample even at
            // very high frame rates where the per-frame absolute-position
            // diff would truncate to zero. In relative mouse mode these
            // values come directly from the OS pointer driver (libinput
            // rel-pointer / XInput2 raw / raw input); otherwise SDL emulates
            // them from absolute-position diffs.
            //
            // Sign convention: getCursorDelta is consumed by viewer code
            // (alt-cam, mouselook, focus tool) that was written against
            // Win32's raw-input convention where mY is Y-UP — see
            // LLWindowWin32::WM_INPUT, "mRawMouseDelta.mY -= ...". SDL3's
            // event.motion.yrel is Y-DOWN (positive when the cursor moves
            // down); negate it on the way into the accumulator so the
            // viewer's "-dy pitches the camera up" math behaves the same
            // on both backends. X is Y-RIGHT-positive on both, no flip.
            mMouseDeltaAccumX += event.motion.xrel * scale;
            mMouseDeltaAccumY -= event.motion.yrel * scale;

            // When relative mode is on, motion.x/y is undefined (SDL parks
            // the cursor) and we're in mouselook — UI hover/hit-testing
            // isn't running, and camera input is driven via getCursorDelta.
            // Don't forward a fake absolute position to the viewer.
            if (!mRelativeMouseMode)
            {
                LLCoordWindow winCoord(llfloor(event.motion.x * scale),
                                       llfloor(event.motion.y * scale));
                LLCoordGL openGlCoord;
                convertCoords(winCoord, &openGlCoord);
                mCallbacks->handleMouseMove(this, openGlCoord, gKeyboard->currentMask(true));
            }
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL:
        {
            // Use the float-precision deltas (event.wheel.x/y) rather than
            // the truncated event.wheel.integer_x/integer_y so touchpads and
            // high-resolution wheels deliver smooth scroll instead of being
            // quantised to integer notches and dropping sub-tick motion.
            //
            // event.wheel.direction is informational — SDL3 has already
            // applied the OS natural-scroll preference to x/y, so using the
            // values as-is respects the user's system setting. Linux users
            // who enabled natural scroll in GNOME/KDE/Wayland get natural
            // scroll inside the viewer too; we intentionally don't
            // normalise FLIPPED back to "Win32 direction".
            mScrollWheelAccumX += event.wheel.x;
            mScrollWheelAccumY += event.wheel.y;

            // handleScrollWheel/HWheel take S32 "clicks". Emit integer ticks
            // when the accumulator crosses ±1 and carry the sub-tick residue
            // forward so smooth-scroll gestures eventually integrate. lltrunc
            // (toward zero) matches the mouse-delta accumulator and avoids
            // amplifying sub-tick noise into spurious scroll events the way
            // llfloor would for negative residue.
            const S32 iy = lltrunc(mScrollWheelAccumY);
            mScrollWheelAccumY -= (F32)iy;
            if (iy != 0 || event.wheel.y != 0.f)
            {
                mCallbacks->handleScrollWheel(this, LLScrollDelta(-iy, -event.wheel.y));
            }
            const S32 ix = lltrunc(mScrollWheelAccumX);
            mScrollWheelAccumX -= (F32)ix;
            if (ix != 0 || event.wheel.x != 0.f)
            {
                // Win32 sends WM_MOUSEHWHEEL's HIWORD (+=right) directly with
                // no sign flip (llwindowwin32.cpp:2929: `h_delta / WHEEL_DELTA`).
                // SDL3 `event.wheel.x` follows the same +=right convention, so
                // forward unmodified. Only the Y axis negates to match
                // Win32's vertical convention ("+ clicks = scroll content
                // down" — see the line just above).
                mCallbacks->handleScrollHWheel(this, LLScrollDelta(ix, event.wheel.x));
            }
            break;
        }

        // Pen / stylus native events. SDL3 also emits mouse-emulation events
        // for these (with event.motion.which == SDL_PEN_MOUSEID) which our
        // mouse handlers already route through the usual input plumbing. The
        // native channel is consumed here purely to harvest the rich
        // metadata — pressure, tilt, eraser-tip flag — that mouse emulation
        // throws away. Tools query the cached values via
        // LLWindow::getPointerPressure / getPointerTiltX/Y /
        // isPointerEraserTip / isPointerPenActive.
        case SDL_EVENT_PEN_PROXIMITY_IN:
        {
            mPenInProximity = true;
            // Eraser-tip flag isn't on the proximity event itself; it shows
            // up on subsequent PEN_DOWN/UP via event.ptouch.eraser, and on
            // PEN_MOTION / PEN_AXIS via the pen_state bitfield. Start
            // assuming writing tip.
            mPenEraserTip = false;
            LL_DEBUGS("Window") << "Pen entered proximity" << LL_ENDL;
            break;
        }
        case SDL_EVENT_PEN_PROXIMITY_OUT:
        {
            mPenInProximity = false;
            mPenEraserTip = false;
            mPenPressure = 1.f;
            mPenTiltX = 0.f;
            mPenTiltY = 0.f;
            LL_DEBUGS("Window") << "Pen left proximity" << LL_ENDL;
            break;
        }
        case SDL_EVENT_PEN_AXIS:
        {
            mPenEraserTip = (event.paxis.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0;
            switch (event.paxis.axis)
            {
                case SDL_PEN_AXIS_PRESSURE:
                    mPenPressure = event.paxis.value;
                    break;
                case SDL_PEN_AXIS_XTILT:
                    mPenTiltX = event.paxis.value;
                    break;
                case SDL_PEN_AXIS_YTILT:
                    mPenTiltY = event.paxis.value;
                    break;
                default:
                    // Distance / rotation / slider / tangential-pressure are
                    // not currently surfaced through LLWindow. The values
                    // are still available via direct SDL polling if a tool
                    // needs them.
                    break;
            }
            break;
        }
        case SDL_EVENT_PEN_DOWN:
        case SDL_EVENT_PEN_UP:
            // ptouch carries the eraser-tip flag directly; keep it in sync.
            mPenEraserTip = event.ptouch.eraser;
            // Mouse-emulation events handle the actual click routing.
            break;
        case SDL_EVENT_PEN_MOTION:
            mPenEraserTip = (event.pmotion.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0;
            // Mouse-emulation events handle motion routing.
            break;
        case SDL_EVENT_PEN_BUTTON_DOWN:
        case SDL_EVENT_PEN_BUTTON_UP:
            // Mouse-emulation events handle button routing. Logging only.
            LL_DEBUGS("Window") << "Pen button event "
                                << std::hex << event.type << std::dec << LL_ENDL;
            break;

        // Touchscreen finger events. As with pen, SDL3 emits mouse-emulation
        // events alongside these and the viewer's mouse plumbing handles
        // routing. We track pressure on single-finger contacts; multi-touch
        // gesture recognition would require a dedicated callback layer that
        // doesn't exist yet.
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
            mPenPressure = event.tfinger.pressure;
            mPenInProximity = true;
            break;
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            mPenPressure = 1.f;
            mPenInProximity = false;
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            // Touch / pen also fire mouse-button events; track which source
            // they came from so the next hideCursor() can pick the right
            // pointer-lock strategy. See the MOUSE_MOTION case above.
            mAbsoluteCursorPosition = (event.button.which == SDL_TOUCH_MOUSEID
                                       || event.button.which == SDL_PEN_MOUSEID);

            // Scale screen-coord event coords up to PIXEL units for LLCoordWindow.
            const float scale = mCachedPixelDensity; // cached; see refreshPixelMetrics
            LLCoordWindow winCoord(llfloor(event.button.x * scale),
                                   llfloor(event.button.y * scale));
            LLCoordGL openGlCoord;
            convertCoords(winCoord, &openGlCoord);
            MASK mask = gKeyboard->currentMask(true);

            if (event.button.button == SDL_BUTTON_LEFT)  // left
            {
                if (event.button.clicks == 2)
                    mCallbacks->handleDoubleClick(this, openGlCoord, mask);
                else
                    mCallbacks->handleMouseDown(this, openGlCoord, mask);
            }
            else if (event.button.button == SDL_BUTTON_RIGHT)
            {
                mCallbacks->handleRightMouseDown(this, openGlCoord, mask);
            }
            else if (event.button.button == SDL_BUTTON_MIDDLE)  // middle
            {
                mCallbacks->handleMiddleMouseDown(this, openGlCoord, mask);
            }
            else
            {
                mCallbacks->handleOtherMouseDown(this, openGlCoord, mask, event.button.button);
            }

            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            // Mirror the touch/pen tracking from MOUSE_MOTION / BUTTON_DOWN
            // so a button-up arriving without a preceding same-frame motion
            // event still updates mAbsoluteCursorPosition. Without this, a
            // device-class transition that happened to land on a button-up
            // boundary would leave the previous device's classification
            // stuck until the next motion/down.
            mAbsoluteCursorPosition = (event.button.which == SDL_TOUCH_MOUSEID
                                       || event.button.which == SDL_PEN_MOUSEID);

            const float scale = mCachedPixelDensity; // cached; see refreshPixelMetrics
            LLCoordWindow winCoord(llfloor(event.button.x * scale),
                                   llfloor(event.button.y * scale));
            LLCoordGL openGlCoord;
            convertCoords(winCoord, &openGlCoord);
            MASK mask = gKeyboard->currentMask(true);

            if (event.button.button == SDL_BUTTON_LEFT)  // left
            {
                mCallbacks->handleMouseUp(this, openGlCoord, mask);
            }
            else if (event.button.button == SDL_BUTTON_RIGHT)  // right
            {
                mCallbacks->handleRightMouseUp(this, openGlCoord, mask);
            }
            else if (event.button.button == SDL_BUTTON_MIDDLE)  // middle
            {
                mCallbacks->handleMiddleMouseUp(this, openGlCoord, mask);
            }
            else
            {
                mCallbacks->handleOtherMouseUp(this, openGlCoord, mask, event.button.button);
            }

            break;
        }

        case SDL_EVENT_KEY_DOWN:
        {
            mKeyVirtualKey = event.key.key;
            mKeyModifiers = event.key.mod;
            mKeyRawScanCode = event.key.raw;

            // Collapse the alternate Return keysyms (USB-HID secondary Return
            // and numpad Enter) onto the canonical SDLK_RETURN so downstream
            // bindings only have to look for one value.
            if (mKeyVirtualKey == SDLK_RETURN2 || mKeyVirtualKey == SDLK_KP_ENTER)
            {
                mKeyVirtualKey = SDLK_RETURN;
            }

            gKeyboard->handleKeyDown(mKeyVirtualKey, mKeyModifiers);

            if (mKeyVirtualKey == SDLK_RETURN)
            {
                // Synthesise a Unicode-char event for Return. SDL3 only fires
                // SDL_EVENT_TEXT_INPUT for printable characters the platform
                // IME composed, never for control keys like Return, Tab, or
                // Backspace — so we deliver Return to
                // LLViewerWindow::handleUnicodeChar ourselves, which then
                // funnels uni_char == '\r' into gViewerInput.handleKey(KEY_RETURN, …)
                // for chat-send and similar (see llviewerwindow.cpp:3406).
                //
                // The mask we pass is the live MASK from currentMask(false),
                // which only reflects SHIFT/CTRL/ALT — NumLock/CapsLock/etc.
                // don't translate into the MASK type at all, so lock-key state
                // has no effect on the chat-send path here.
                mCallbacks->handleUnicodeChar(mKeyVirtualKey, gKeyboard->currentMask(false));
            }

            // part of the fix for SL-13243
            if (SDLCheckGrabbyKeys(mKeyVirtualKey, true) != 0)
                SDLReallyCaptureInput(true);
            break;
        }

        case SDL_EVENT_KEY_UP:
        {
            mKeyVirtualKey = event.key.key;
            mKeyModifiers = event.key.mod;
            mKeyRawScanCode = event.key.raw;

            if (mKeyVirtualKey == SDLK_RETURN2 || mKeyVirtualKey == SDLK_KP_ENTER)
            {
                mKeyVirtualKey = SDLK_RETURN;
            }

            gKeyboard->handleKeyUp(mKeyVirtualKey, mKeyModifiers);
            if (SDLCheckGrabbyKeys(mKeyVirtualKey, false) == 0)
                SDLReallyCaptureInput(false); // part of the fix for SL-13243
            break;
        }

        case SDL_EVENT_TEXT_INPUT:
        {
            // event.text.text is "UTF-8 encoded" per SDL3 SDL_TextInputEvent
            // docs; nothing in the spec actually rules out a NULL pointer for
            // a malformed event, so guard. Empty string is similarly skipped.
            if (!event.text.text || event.text.text[0] == '\0')
            {
                break;
            }

            // Skip TEXT_INPUT events generated by Ctrl-modified keystrokes:
            // those are accelerators (Ctrl-A select-all, Ctrl-C copy, etc.)
            // that the preceding SDL_EVENT_KEY_DOWN has already routed
            // through the binding system. Win32 doesn't have this problem
            // because WM_CHAR for Ctrl-A delivers 0x01 (the SOH control
            // character) which the text editor naturally ignores; SDL3
            // delivers the literal "a" instead, which a focused text field
            // would happily insert in place of the just-made selection
            // ("Ctrl-A selects then immediately overwrites with a"). The
            // KEY_DOWN path handled the accelerator, so dropping the
            // TEXT_INPUT is correct.
            //
            // We do NOT filter on MASK_ALT alone — Right-Alt (AltGr) on
            // European keyboards composes characters like Å/Ø/€ and those
            // legitimately arrive via TEXT_INPUT.
            // Use the modifier state captured WITH the keystroke (mKeyModifiers,
            // set from event.key.mod on the KEY_DOWN that produced this text),
            // not the live SDL_GetModState(): gatherInput drains the whole event
            // batch at once, so a fast Ctrl+key whose Ctrl-release lands in the
            // same pump would read CTRL as already up here and fail to drop the
            // accelerator's literal character (Ctrl-A then "a" overwriting the
            // selection). mKeyModifiers reflects the producing keystroke, and an
            // IME commit refreshes it via its own non-Ctrl key-downs.
            if (mKeyModifiers & SDL_KMOD_CTRL)
            {
                break;
            }

            // SDL3 fires TEXT_INPUT to deliver committed text from the platform
            // IME (or from direct keyboard typing if no IME is active). If a
            // preeditor was mid-composition, reset its preedit state first so
            // the in-flight preedit string is cleared from the widget's mText
            // before the commit chars are inserted — otherwise the preedit
            // accumulates alongside the commit and corrupts the field. (See
            // LLLineEditor::updatePreedit at lllineeditor.cpp:2754, which
            // explicitly notes its calls "must be preceded by resetPreedit".)
            if (mPreeditor)
            {
                mPreeditor->resetPreedit();
            }
            auto string = utf8str_to_wstring(event.text.text);
            MASK current_mask = gKeyboard->currentMask(false);
            for (auto key : string)
            {
                // Deliberately do NOT overwrite mKeyVirtualKey here.
                // mKeyVirtualKey is read by getNativeKeyData() and forwarded
                // to Dullahan/CEF as the virtual-key code; Win32 keeps it as
                // the most recent WM_KEYDOWN VK (mKeyCharCode is the
                // character-code field on that backend). Writing the Unicode
                // codepoint here corrupts the value CEF sees — multi-codepoint
                // commits (combined emoji, ZWJ sequences) would leave the
                // final character cached, and the CEF keysym layer at
                // media_plugin_cef.cpp:1108-1164 treats anything >= 0x7f as
                // a special-case sysmod path with that bogus value.
                mCallbacks->handleUnicodeChar(key, current_mask);
            }
            break;
        }

        case SDL_EVENT_TEXT_EDITING:
        {
            // IME composition update (the in-progress preedit string the user is
            // composing before commit). Without an active LLPreeditor the viewer
            // has no widget that wants to display preedit feedback — drop it; the
            // eventual SDL_EVENT_TEXT_INPUT delivers the committed text.
            if (!mPreeditor)
            {
                break;
            }

            const bool empty = !event.edit.text || event.edit.text[0] == '\0';

            // Skip the entire handler when both the incoming composition is
            // empty AND there's no active preedit to clear. Some Linux IMEs
            // (ibus, fcitx) emit empty TEXT_EDITING events on every
            // modifier-only keypress to "cancel composition mode" — Ctrl-A
            // is a common trigger. Calling resetPreedit() in that state has
            // a nasty side effect: LLLineEditor::resetPreedit treats
            // "selection-without-preedit" as the IME-overwrites-selection
            // pattern and calls deleteSelection(), which wipes the text the
            // user just selected with the menu accelerator.
            S32 preedit_pos = 0, preedit_len = 0;
            mPreeditor->getPreeditRange(&preedit_pos, &preedit_len);
            if (empty && preedit_len == 0)
            {
                break;
            }

            // resetPreedit must be called before every updatePreedit (see
            // lllineeditor.cpp:2763). Each TEXT_EDITING delivers the FULL
            // current composition string, so we drop the previous preedit
            // before inserting the new one — otherwise the field accumulates
            // every intermediate composition state.
            mPreeditor->resetPreedit();

            // An empty composition string means the IME cancelled or cleared
            // the preedit; the reset above is the entire job.
            if (empty)
            {
                break;
            }

            const LLWString preedit = utf8str_to_wstring(event.edit.text);

            // event.edit.start is a UTF-8 byte offset within event.edit.text
            // (the IME's caret position inside the composition), or -1 when
            // the IME hasn't reported a position. start == 0 is a legitimate
            // "caret at the beginning" value and must NOT be confused with
            // the -1 default (the earlier `> 0` check did exactly that and
            // pinned the caret to the end of the preedit whenever the IME
            // started the cursor at byte 0).
            S32 caret = static_cast<S32>(preedit.length());
            if (event.edit.start >= 0)
            {
                // Convert from UTF-8 byte offset to llwchar (UTF-32) offset
                // by re-encoding the prefix and taking its length.
                const std::string prefix(event.edit.text, event.edit.start);
                caret = static_cast<S32>(utf8str_to_wstring(prefix).length());
            }
            const LLPreeditor::segment_lengths_t lengths { static_cast<S32>(preedit.length()) };
            const LLPreeditor::standouts_t standouts { false };
            mPreeditor->updatePreedit(preedit, lengths, standouts, caret);
            break;
        }

        case SDL_EVENT_TEXT_EDITING_CANDIDATES:
        {
            // SDL3 IMEs surface a candidate list (Japanese/Chinese 候補 selection).
            // The viewer has no candidate-list UI, so the platform IME renders its
            // own popup. Logged at debug for diagnostic correlation.
            LL_DEBUGS("SDL") << "TEXT_EDITING_CANDIDATES: "
                             << event.edit_candidates.num_candidates << " candidate(s)" << LL_ENDL;
            break;
        }

        case SDL_EVENT_WINDOW_EXPOSED:
        {
            mCallbacks->handlePaint(this, 0, 0, 0, 0);
            break;
        }

        case SDL_EVENT_WINDOW_RESIZED:
        {
            LL_INFOS() << "Handling a resize event: " << event.window.data1 << "x" << event.window.data2 << LL_ENDL;
            // Window size changed: refresh the cached pixel metrics the input
            // handlers and convertCoords read, and reuse the density here.
            // refreshPixelMetrics clamps it > 0, covering the 0.f the SDL backend
            // can briefly return during display-change races (which would
            // otherwise zero width/height into a degenerate handleResize).
            refreshPixelMetrics();
            const F32 pixel_density = mCachedPixelDensity;
            // mMinWindowWidth/Height are 0 until setMinSize runs, so llmax can't
            // keep these positive this early. Floor to 1px so a 0-dimension
            // resize (some WMs emit one mid-drag / on un-maximize) never reaches
            // handleResize as a degenerate 0x0 viewport / divide-by-zero aspect.
            S32 width = llmax(1, (S32)(llmax(event.window.data1, (S32)mMinWindowWidth) * pixel_density));
            S32 height = llmax(1, (S32)(llmax(event.window.data2, (S32)mMinWindowHeight) * pixel_density));

            mCallbacks->handleResize(this, width, height);
            break;
        }
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        {
            // Distinct from RESIZED: PIXEL_SIZE_CHANGED fires when the back-buffer
            // dimensions change even if the window's logical size stayed the same
            // (e.g. moving to a monitor with a different scale factor under
            // fractional Wayland scaling). data1/data2 are already in pixels here,
            // so the clamp has to use the PIXEL-unit minimum shadow — the
            // screen-coord mMinWindowWidth/Height would under-clamp on HiDPI.
            LL_INFOS() << "Handling a pixel-size event: " << event.window.data1 << "x" << event.window.data2 << LL_ENDL;
            // Back-buffer dimensions changed; refresh the cached pixel height
            // convertCoords reads.
            refreshPixelMetrics();
            // Floor to 1px: mMinWindowWidthPx/HeightPx are 0 until setMinSize
            // runs, so a 0-dimension pixel-size event would otherwise pass
            // through as a degenerate 0x0 resize.
            S32 width  = llmax(1, llmax(event.window.data1, (S32)mMinWindowWidthPx));
            S32 height = llmax(1, llmax(event.window.data2, (S32)mMinWindowHeightPx));
            mCallbacks->handleResize(this, width, height);
            break;
        }
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            mCallbacks->handleMouseLeave(this);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            //SDL_SetWindowKeyboardGrab(mWindow, true);
            // Inhibit the screensaver while the viewer is the focused window.
            // On Wayland this routes through the org.freedesktop.ScreenSaver
            // Inhibit portal; on X11 it suppresses the XScreenSaver timer.
            // We pair this with EnableScreenSaver on focus-loss so the
            // screensaver can run normally while the user is in another app.
            SDL_DisableScreenSaver();
            // Re-engage relative mouse mode if the viewer still expects
            // pointer-lock (the user was in mouselook / a tool-grab session
            // when focus was lost, and the next-best signal "we have a
            // permanently hidden cursor" is still set). SDL3 silently drops
            // relative mode on focus loss, so without this the camera
            // signal would be dead until the next showCursor/hideCursor
            // cycle. mHideCursorPermanent is the right indicator since
            // beforeDialog/auto-idle paths don't set it.
            if (mWindow && mHideCursorPermanent && !mAbsoluteCursorPosition
                && !mRelativeMouseMode)
            {
                if (SDL_SetWindowRelativeMouseMode(mWindow, true))
                {
                    mRelativeMouseMode = true;
                    mMouseDeltaAccumX = 0.f;
                    mMouseDeltaAccumY = 0.f;
                    mPendingWarpSuppressCount = 0;
                }
            }
            mCallbacks->handleFocus(this);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            mCallbacks->handleFocusLost(this);
            // Drop the IME preeditor reference defensively. The viewer's
            // focus manager normally clears keyboard focus on app focus
            // loss, which triggers allowLanguageTextInput(nullptr) — but
            // if any path leaves mPreeditor pointing at a widget that
            // thinks it's defocused, a stray TEXT_EDITING event would
            // update a non-active widget.
            mPreeditor = nullptr;
            // Sync our relative-mode state to SDL3's. SDL3's keyboard
            // layer auto-disables relative mode on focus loss; if we don't
            // mirror that, our member stays true and setCursorPosition
            // defers warps + getCursorPosition reports center while real
            // motion is now actually being delivered, manifesting as a
            // camera jolt or a stuck-centered cursor on focus return.
            // Zero accumulators and suppress count so no stale motion
            // bleeds into the next session.
            if (mRelativeMouseMode)
            {
                mRelativeMouseMode = false;
                mMouseDeltaAccumX = 0.f;
                mMouseDeltaAccumY = 0.f;
                mPendingWarpSuppressCount = 0;
            }
            SDL_EnableScreenSaver();
            //SDL_SetWindowKeyboardGrab(mWindow, false);
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            mCallbacks->handleActivate(this, true);
            break;
        case SDL_EVENT_WINDOW_MAXIMIZED:
            mCallbacks->handleActivate(this, true);
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            mCallbacks->handleActivate(this, false);
            break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        {
            // Update refresh rate when changing monitors
            const SDL_DisplayMode* displayMode = SDL_GetCurrentDisplayMode(event.window.data1);
            if(displayMode)
            {
                mRefreshRate = ll_round(displayMode->refresh_rate);
                mNativeAspectRatio = ((F32)displayMode->w) / ((F32)displayMode->h);
            }
            // Pixel density may have changed; refresh the pixel-unit min-size
            // shadow so setSizeImpl(LLCoordWindow)'s re-clamp stays unit-correct.
            refreshMinSizePixelShadow();
            mCallbacks->handleDisplayChanged();
            break;
        }
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        {
            S32 w = 0, h = 0;
            SDL_GetWindowSizeInPixels(mWindow, &w, &h);
            // Same as DISPLAY_CHANGED: the pixel-unit shadow tracks the
            // window's current density, which just changed.
            refreshMinSizePixelShadow();
            mCallbacks->handleDPIChanged(this, getSystemUISize(), w, h);
            break;
        }
        case SDL_EVENT_DISPLAY_ADDED:
        case SDL_EVENT_DISPLAY_REMOVED:
        case SDL_EVENT_DISPLAY_ORIENTATION:
        case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
        {
            // Display set changed (monitor plugged/unplugged, rotated, or scale changed).
            // Invalidate the cached resolution list so the next getSupportedResolutions()
            // rebuilds it from the current display set.
            LL_INFOS() << "Display event 0x" << std::hex << event.type << std::dec
                       << " for display " << event.display.displayID
                       << " — invalidating supported-resolution cache." << LL_ENDL;
            delete[] mSupportedResolutions;
            mSupportedResolutions = nullptr;
            mNumSupportedResolutions = 0;
            mCallbacks->handleDisplayChanged();
            break;
        }
        case SDL_EVENT_LOCALE_CHANGED:
        {
            // The viewer reads the system locale once at startup via FL_FindLocale;
            // we don't currently re-localise live. Log for support visibility.
            LL_INFOS() << "System locale changed — viewer localisation honors only the value at startup." << LL_ENDL;
            break;
        }
        case SDL_EVENT_DROP_BEGIN:
        {
            // Drag-and-drop session starting. SDL3 doesn't populate event.drop.x/y
            // or deliver file paths until later (DROP_FILE between BEGIN and
            // COMPLETE), so we can't START_TRACKING here — just reset the buffer.
            mPendingDropFiles.clear();
            break;
        }
        case SDL_EVENT_DROP_FILE:
        {
            // SDL3 sends one DROP_FILE per dropped file — accumulate, then dispatch
            // the full sequence on DROP_COMPLETE.
            if (event.drop.data)
            {
                mPendingDropFiles.emplace_back(event.drop.data);
            }
            break;
        }
        case SDL_EVENT_DROP_TEXT:
        {
            // The current handleDragNDrop pipeline is file-only (DNDT_FILE); SDL3
            // also delivers dropped text but we have nowhere to route it. Drop silently.
            break;
        }
        case SDL_EVENT_DROP_COMPLETE:
        {
            // End of the drag-and-drop session.
            //
            // LLViewerWindow::handleDragNDropFile populates its mDragItems cache
            // *only* on DNDA_START_TRACKING (it iterates the file list and builds
            // LLViewerInventoryItem objects), then consumes the cache on
            // DNDA_DROPPED — a DROPPED event with no preceding START_TRACKING is
            // a no-op (see indra/newview/llviewerwindow.cpp:1455+).
            //
            // SDL3's drop API only surfaces file paths between DROP_BEGIN and
            // DROP_COMPLETE, so we can't drive START/TRACK live the way the
            // Win32 OLE backend does (DragEnter/DragOver/Drop). Instead we
            // synthesise the same three-step sequence here so the receiver's
            // state machine sees a well-formed transaction:
            //
            //   1) DNDA_START_TRACKING with the file list -> mDragItems populated
            //   2) DNDA_DROPPED                           -> upload / apply
            //   3) DNDA_STOP_TRACKING                     -> mDragItems cleared
            // event.drop.x/y are screen-coord units; scale to PIXEL units.
            const float scale = mCachedPixelDensity; // cached; see refreshPixelMetrics
            LLCoordWindow winCoord(llfloor(event.drop.x * scale),
                                   llfloor(event.drop.y * scale));
            LLCoordGL openGlCoord;
            convertCoords(winCoord, &openGlCoord);
            const MASK mask = gKeyboard->currentMask(true);
            if (!mPendingDropFiles.empty())
            {
                mCallbacks->handleDragNDrop(this, openGlCoord, mask,
                                            LLWindowCallbacks::DNDA_START_TRACKING,
                                            LLWindowCallbacks::DNDT_FILE,
                                            mPendingDropFiles);
                mCallbacks->handleDragNDrop(this, openGlCoord, mask,
                                            LLWindowCallbacks::DNDA_DROPPED,
                                            LLWindowCallbacks::DNDT_FILE,
                                            mPendingDropFiles);
            }
            // Always send STOP_TRACKING so any cached state (mDragItems,
            // hover highlight) is cleared, whether or not files were dropped
            // (text-only drops and cancelled drags both arrive here too).
            mCallbacks->handleDragNDrop(this, openGlCoord, mask,
                                        LLWindowCallbacks::DNDA_STOP_TRACKING,
                                        LLWindowCallbacks::DNDT_FILE,
                                        {});
            mPendingDropFiles.clear();
            break;
        }
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        {
            if(mCallbacks->handleCloseRequest(this, true))
            {
                // Get the app to initiate cleanup.
                mCallbacks->handleQuit(this);
                // The app is responsible for calling destroyWindow when done with GL
            }
            break;
        }
        case SDL_EVENT_RENDER_DEVICE_RESET:
        case SDL_EVENT_RENDER_DEVICE_LOST:
        case SDL_EVENT_RENDER_TARGETS_RESET:
        {
            // The GL device underneath us has been reset, lost, or had its
            // render targets invalidated — display sleep/wake, GPU driver
            // restart, VT switch on some Mesa stacks. Every texture, VBO,
            // shader program, framebuffer object, and reflection probe is
            // now in an undefined state and subsequent GL calls will either
            // silently no-op or crash. The pre-relative-mode viewer simply
            // crashed; this gives the user a visible cause-of-death and a
            // proper log line for support before we tear down.
            //
            // True recovery — destroying every GL resource and rebuilding
            // it from scratch — is a substantial cross-subsystem effort
            // (LLPipeline, LLViewerTextureList, LLReflectionMapManager,
            // every loaded shader, the UI atlas) and is deliberately not
            // attempted here. The viewer exits cleanly so the user knows
            // to restart instead of staring at a black or corrupted window.
            const char* kind = (event.type == SDL_EVENT_RENDER_DEVICE_RESET)
                                   ? "device reset"
                                   : (event.type == SDL_EVENT_RENDER_DEVICE_LOST)
                                         ? "device lost"
                                         : "render targets reset";
            LL_WARNS("Window") << "GL " << kind
                               << " detected via SDL3 event. SDL error: "
                               << SDL_GetError() << LL_ENDL;

            OSMessageBoxSDL(
                "The graphics driver reported an OpenGL device "
                + std::string(kind)
                + ". Alchemy can't continue rendering and will now exit.\n\n"
                  "This usually follows display sleep/wake, a GPU driver "
                  "restart, or a virtual-terminal switch. Please relaunch "
                  "the viewer to resume.",
                "OpenGL device lost",
                OSMB_OK);

            if (mCallbacks)
            {
                mCallbacks->handleQuit(this);
            }
            break;
        }
        default:
            break;
    }

    return SDL_APP_CONTINUE;
}

// static
SDL_AppResult LLWindowSDL::handleEvents(const SDL_Event& event)
{
    // Drop events once the window is gone. During teardown destroyContext()
    // nulls mWindow before ~LLWindowSDL clears gWindowImplementation, and the
    // per-event handlers below deref mWindow (SDL_GetWindowSizeInPixels, relative
    // mouse mode, DPI) without their own guards.
    if (!gWindowImplementation || !gWindowImplementation->mWindow) return SDL_APP_CONTINUE;

    return gWindowImplementation->handleEvent(event);
}

#if LL_DARWIN
// On macOS the viewer ships TIFF cursor art in <Bundle>/Contents/Resources/cursors_mac/
// (see viewer_manifest.py). The legacy res-sdl/*.BMP tree is Linux-only, so on the
// SDL build for Mac we load the native TIFFs via SDL3_image. The TIFFs already carry
// proper alpha, so unlike the BMP path we skip the color-key step.
static SDL_Cursor *makeSDLCursorFromMacTIF(const char *basename, int hotx, int hoty)
{
    std::string fullpath = gDirUtilp->add(
        gDirUtilp->getAppRODataDir(),
        "cursors_mac",
        basename);

    SDL_Surface *surface = IMG_Load(fullpath.c_str());
    if (!surface)
    {
        LL_WARNS() << "Cursor TIFF failed to load: " << fullpath
                   << ": " << SDL_GetError() << LL_ENDL;
        return nullptr;
    }

    LL_DEBUGS() << "Loaded cursor file " << fullpath << " "
                << surface->w << "x" << surface->h << LL_ENDL;

    SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!rgba)
    {
        LL_WARNS() << "Cursor RGBA conversion failed for " << fullpath
                   << ": " << SDL_GetError() << LL_ENDL;
        return nullptr;
    }

    if (hotx < 0 || hotx >= rgba->w || hoty < 0 || hoty >= rgba->h)
    {
        LL_WARNS() << "Cursor " << fullpath << " hot-spot ("
                   << hotx << "," << hoty << ") is outside "
                   << rgba->w << "x" << rgba->h << "; clamping." << LL_ENDL;
        hotx = llclamp(hotx, 0, rgba->w - 1);
        hoty = llclamp(hoty, 0, rgba->h - 1);
    }

    SDL_Cursor *sdlcursor = SDL_CreateColorCursor(rgba, hotx, hoty);
    SDL_DestroySurface(rgba);
    if (!sdlcursor)
    {
        LL_WARNS() << "SDL_CreateColorCursor failed for " << fullpath
                   << ": " << SDL_GetError() << LL_ENDL;
    }
    return sdlcursor;
}
#endif // LL_DARWIN

#if LL_LINUX
static SDL_Cursor *makeSDLCursorFromBMP(const char *filename, int hotx, int hoty)
{
    SDL_Surface *bmpsurface = Load_BMP_Resource(filename);
    if (!bmpsurface)
    {
        LL_WARNS() << "Cursor BMP failed to load: " << filename << LL_ENDL;
        return nullptr;
    }

    LL_DEBUGS() << "Loaded cursor file " << filename << " "
                << bmpsurface->w << "x" << bmpsurface->h << LL_ENDL;

    // Normalise to RGBA32 (byte order R,G,B,A in memory regardless of
    // host endianness) so we have a predictable layout to color-key
    // against, and so SDL_CreateColorCursor gets a surface with alpha.
    SDL_Surface *rgba = SDL_ConvertSurface(bmpsurface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(bmpsurface);
    if (!rgba)
    {
        LL_WARNS() << "Cursor RGBA conversion failed for " << filename
                   << ": " << SDL_GetError() << LL_ENDL;
        return nullptr;
    }

    // Color-key the legacy (200,200,200) "background" pixels to fully
    // transparent. The viewer's cursor BMPs were authored against this
    // exact gray as the transparency key (same convention the old SDL2
    // path used), so the assets stay drop-in compatible. Unlike the
    // legacy path we keep the rest of the pixel data intact — the old
    // code quantised everything to 1-bit black/white, which is why
    // multi-colour cursors looked degraded vs Win32.
    //
    // A converted SDL3 RGBA32 surface shouldn't need locking, but the
    // pair is cheap and defends against future SDL changes that flag the
    // surface as RLE / GPU-backed.
    const bool must_lock = SDL_MUSTLOCK(rgba);
    if (must_lock && !SDL_LockSurface(rgba))
    {
        LL_WARNS() << "SDL_LockSurface failed for cursor " << filename
                   << ": " << SDL_GetError() << LL_ENDL;
        SDL_DestroySurface(rgba);
        return nullptr;
    }
    for (int y = 0; y < rgba->h; ++y)
    {
        U8 *row = (U8*)rgba->pixels + (size_t)y * rgba->pitch;
        for (int x = 0; x < rgba->w; ++x)
        {
            U8 *px = row + (size_t)x * 4;
            if (px[0] == 200 && px[1] == 200 && px[2] == 200)
            {
                px[3] = 0;
            }
        }
    }
    if (must_lock)
    {
        SDL_UnlockSurface(rgba);
    }

    // Clamp the hot-spot — out-of-range coordinates are undefined behaviour
    // on most platforms and at minimum produce a cursor that "clicks"
    // nowhere near the visible tip.
    if (hotx < 0 || hotx >= rgba->w || hoty < 0 || hoty >= rgba->h)
    {
        LL_WARNS() << "Cursor " << filename << " hot-spot ("
                   << hotx << "," << hoty << ") is outside "
                   << rgba->w << "x" << rgba->h << "; clamping." << LL_ENDL;
        hotx = llclamp(hotx, 0, rgba->w - 1);
        hoty = llclamp(hoty, 0, rgba->h - 1);
    }

    SDL_Cursor *sdlcursor = SDL_CreateColorCursor(rgba, hotx, hoty);
    SDL_DestroySurface(rgba);
    if (!sdlcursor)
    {
        LL_WARNS() << "SDL_CreateColorCursor failed for " << filename
                   << ": " << SDL_GetError() << LL_ENDL;
    }
    return sdlcursor;
}
#endif // LL_LINUX

#if LL_WINDOWS
// Convert one of the viewer's embedded Win32 cursor resources (the same
// branded .cur/.ani assets the native backend loads in
// LLWindowWin32::initCursors) into an SDL color cursor. The hot-spot is taken
// from the resource itself via GetIconInfo, so — unlike the BMP path — there
// is no hand-maintained hot-spot table to keep in sync. Returns nullptr if the
// resource is missing or can't be converted, letting initCursors fall back to
// the SDL system arrow.
// Read a GDI bitmap as `rows` of top-down 32bpp BGRA into `out`. Used for both
// the colour bitmap and the (1bpp or 8bpp) mask — GetDIBits converts whatever
// the source depth is to 32bpp, so monochrome masks come back as black(0)/
// white(0xFFFFFF) pixels we can test a single byte of.
static bool win32ReadDIB32(HBITMAP hbm, int width, int rows, std::vector<U8>& out)
{
    out.assign((size_t)width * rows * 4, 0);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = width;
    bi.bmiHeader.biHeight      = -rows; // negative => top-down rows
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC hdc = GetDC(nullptr);
    const bool ok = GetDIBits(hdc, hbm, 0, rows, out.data(), &bi, DIB_RGB_COLORS) != 0;
    ReleaseDC(nullptr, hdc);
    return ok;
}

// Convert one of the viewer's embedded Win32 cursor resources (the same
// branded .cur/.ani assets the native backend loads in
// LLWindowWin32::initCursors) into an SDL color cursor. The hot-spot is taken
// from the resource itself via GetIconInfo, so — unlike the BMP path — there
// is no hand-maintained hot-spot table to keep in sync. Returns nullptr if the
// resource is missing or can't be converted, letting initCursors fall back to
// the SDL system arrow.
static SDL_Cursor *makeSDLCursorFromWin32(const char *resource_name)
{
    HMODULE module = GetModuleHandle(nullptr);
    // LR_DEFAULTCOLOR matches LLWindowWin32::loadColorCursor and also works for
    // the monochrome resources LoadCursor would otherwise handle. LR_SHARED so
    // we don't have to DestroyCursor an exe-embedded resource.
    HCURSOR hcur = (HCURSOR)LoadImageA(module, resource_name, IMAGE_CURSOR,
                                       0, 0, LR_DEFAULTCOLOR | LR_SHARED);
    if (!hcur)
    {
        LL_WARNS() << "Cursor resource not found: " << resource_name << LL_ENDL;
        return nullptr;
    }

    ICONINFO ii = {};
    if (!GetIconInfo(hcur, &ii))
    {
        LL_WARNS() << "GetIconInfo failed for cursor " << resource_name
                   << " (err " << GetLastError() << ")" << LL_ENDL;
        return nullptr;
    }
    // GetIconInfo hands back *copies* of the bitmaps that we own and must free.
    // For a 1bpp monochrome cursor hbmColor is NULL and hbmMask is double-height
    // (AND mask stacked over XOR mask); colour cursors (8/32bpp) carry hbmColor.
    HBITMAP hbmColor = ii.hbmColor;
    HBITMAP hbmMask  = ii.hbmMask;
    const bool monochrome = (hbmColor == nullptr);

    SDL_Cursor *result = nullptr;
    BITMAP bm = {};
    HBITMAP dim_src = monochrome ? hbmMask : hbmColor;
    if (dim_src && GetObject(dim_src, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0)
    {
        const int width  = bm.bmWidth;
        const int height = monochrome ? bm.bmHeight / 2 : bm.bmHeight;
        const size_t count = (size_t)width * height;

        std::vector<U8> rgba(count * 4, 0); // final R,G,B,A handed to SDL
        bool built = false;

        if (!monochrome)
        {
            std::vector<U8> color, mask;
            const bool got_color = win32ReadDIB32(hbmColor, width, height, color);
            const bool got_mask  = hbmMask && win32ReadDIB32(hbmMask, width, height, mask);
            if (got_color)
            {
                // 32bpp .cur files carry a real per-pixel alpha channel; 8bpp
                // ones leave it zero and express transparency via the AND mask.
                bool has_alpha = false;
                for (size_t i = 0; i < count; ++i)
                {
                    if (color[i * 4 + 3] != 0) { has_alpha = true; break; }
                }
                for (size_t i = 0; i < count; ++i)
                {
                    const U8 b = color[i * 4 + 0];
                    const U8 g = color[i * 4 + 1];
                    const U8 r = color[i * 4 + 2];
                    U8 a = color[i * 4 + 3];
                    if (!has_alpha)
                    {
                        // AND mask: white (set) => transparent, black => opaque.
                        a = (got_mask && mask[i * 4 + 0]) ? 0 : 255;
                    }
                    rgba[i * 4 + 0] = r;
                    rgba[i * 4 + 1] = g;
                    rgba[i * 4 + 2] = b;
                    rgba[i * 4 + 3] = a;
                }
                built = true;
            }
            else
            {
                LL_WARNS() << "GetDIBits failed for cursor " << resource_name << LL_ENDL;
            }
        }
        else
        {
            // 1bpp: hbmMask is AND mask (top half) over XOR mask (bottom half).
            //   AND=1,XOR=0 => transparent;  AND=0,XOR=1 => white;
            //   AND=0,XOR=0 => black;  AND=1,XOR=1 => invert screen (approximated
            //   as opaque black — none of the viewer's cursors rely on invert).
            std::vector<U8> mask;
            if (win32ReadDIB32(hbmMask, width, height * 2, mask))
            {
                for (size_t i = 0; i < count; ++i)
                {
                    const bool and_bit = mask[i * 4] != 0;            // top half
                    const bool xor_bit = mask[(count + i) * 4] != 0;  // bottom half
                    U8 r, g, b, a;
                    if (and_bit && !xor_bit)      { r = g = b = 0;   a = 0;   }
                    else if (!and_bit && xor_bit) { r = g = b = 255; a = 255; }
                    else                          { r = g = b = 0;   a = 255; }
                    rgba[i * 4 + 0] = r;
                    rgba[i * 4 + 1] = g;
                    rgba[i * 4 + 2] = b;
                    rgba[i * 4 + 3] = a;
                }
                built = true;
            }
            else
            {
                LL_WARNS() << "GetDIBits failed for monochrome cursor " << resource_name << LL_ENDL;
            }
        }

        if (built)
        {
            SDL_Surface *surf = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
            if (surf)
            {
                const bool must_lock = SDL_MUSTLOCK(surf);
                if (!must_lock || SDL_LockSurface(surf))
                {
                    for (int y = 0; y < height; ++y)
                    {
                        memcpy((U8*)surf->pixels + (size_t)y * surf->pitch,
                               rgba.data() + (size_t)y * width * 4,
                               (size_t)width * 4);
                    }
                    if (must_lock)
                    {
                        SDL_UnlockSurface(surf);
                    }

                    int hotx = llclamp((int)ii.xHotspot, 0, width - 1);
                    int hoty = llclamp((int)ii.yHotspot, 0, height - 1);
                    result = SDL_CreateColorCursor(surf, hotx, hoty);
                    if (!result)
                    {
                        LL_WARNS() << "SDL_CreateColorCursor failed for " << resource_name
                                   << ": " << SDL_GetError() << LL_ENDL;
                    }
                }
                SDL_DestroySurface(surf);
            }
        }
    }
    else
    {
        LL_WARNS() << "Cursor " << resource_name << " has no usable bitmap." << LL_ENDL;
    }

    if (hbmColor) DeleteObject(hbmColor);
    if (hbmMask)  DeleteObject(hbmMask);
    return result;
}
#endif // LL_WINDOWS

void LLWindowSDL::updateCursor()
{
    if (mCurrentCursor != mNextCursor)
    {
        if (mNextCursor < UI_CURSOR_COUNT)
        {
            SDL_Cursor *sdlcursor = mSDLCursors[mNextCursor];
            // Try to default to the arrow for any cursors that
            // did not load correctly.
            if (!sdlcursor && mSDLCursors[UI_CURSOR_ARROW])
                sdlcursor = mSDLCursors[UI_CURSOR_ARROW];
            if (sdlcursor)
                SDL_SetCursor(sdlcursor);

            mCurrentCursor = mNextCursor;
        }
        else
        {
            LL_WARNS() << "Tried to set invalid cursor number " << mNextCursor << LL_ENDL;
        }
    }
}

void LLWindowSDL::initCursors()
{
    // Blank the cursor pointer array for those we may miss.
    for (int i=0; i<UI_CURSOR_COUNT; ++i)
    {
        mSDLCursors[i] = nullptr;
    }

    // Pre-make an SDL cursor for each of the known cursor types.
    // We hardcode the hotspots - to avoid that we'd have to write
    // a .cur file loader.
    // NOTE: SDL doesn't load RLE-compressed BMP files.
    mSDLCursors[UI_CURSOR_ARROW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    mSDLCursors[UI_CURSOR_WAIT] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    mSDLCursors[UI_CURSOR_HAND] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    mSDLCursors[UI_CURSOR_IBEAM] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    mSDLCursors[UI_CURSOR_CROSS] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    mSDLCursors[UI_CURSOR_SIZENWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
    mSDLCursors[UI_CURSOR_SIZENESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE);
    mSDLCursors[UI_CURSOR_SIZEWE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    mSDLCursors[UI_CURSOR_SIZENS] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    mSDLCursors[UI_CURSOR_SIZEALL] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
    mSDLCursors[UI_CURSOR_NO] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED);
    mSDLCursors[UI_CURSOR_WORKING] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_PROGRESS);
#if LL_DARWIN
    // The macOS bundle ships TIFF cursors under cursors_mac/ (not the res-sdl BMP
    // tree), and uses Mac-specific hot-spots — keep these in sync with
    // LLWindowMacOSX::initCursors() so the SDL build matches the native client.
    // ARROWCOPY/ARROWCOPYMULTI/ARROWDRAGMULTI have no TIF counterpart (the native
    // path handles them via NSCursor.dragCopy / remapping in updateCursor), so we
    // leave them null and let updateCursor's nullptr→arrow fallback take over.
    mSDLCursors[UI_CURSOR_TOOLGRAB] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLGRAB.tif", 2, 14);
    mSDLCursors[UI_CURSOR_TOOLLAND] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLLAND.tif", 13, 8);
    mSDLCursors[UI_CURSOR_TOOLFOCUS] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLFOCUS.tif", 7, 6);
    mSDLCursors[UI_CURSOR_TOOLCREATE] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLCREATE.tif", 7, 7);
    mSDLCursors[UI_CURSOR_ARROWDRAG] = makeSDLCursorFromMacTIF("UI_CURSOR_ARROWDRAG.tif", 1, 1);
    mSDLCursors[UI_CURSOR_NOLOCKED] = makeSDLCursorFromMacTIF("UI_CURSOR_NOLOCKED.tif", 8, 8);
    mSDLCursors[UI_CURSOR_ARROWLOCKED] = makeSDLCursorFromMacTIF("UI_CURSOR_ARROWLOCKED.tif", 1, 1);
    mSDLCursors[UI_CURSOR_GRABLOCKED] = makeSDLCursorFromMacTIF("UI_CURSOR_GRABLOCKED.tif", 2, 14);
    mSDLCursors[UI_CURSOR_TOOLTRANSLATE] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLTRANSLATE.tif", 1, 1);
    mSDLCursors[UI_CURSOR_TOOLROTATE] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLROTATE.tif", 1, 1);
    mSDLCursors[UI_CURSOR_TOOLSCALE] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLSCALE.tif", 1, 1);
    mSDLCursors[UI_CURSOR_TOOLCAMERA] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLCAMERA.tif", 7, 6);
    mSDLCursors[UI_CURSOR_TOOLPAN] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLPAN.tif", 7, 6);
    mSDLCursors[UI_CURSOR_TOOLZOOMIN] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLZOOMIN.tif", 7, 6);
    mSDLCursors[UI_CURSOR_TOOLZOOMOUT] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLZOOMOUT.tif", 7, 6);
    mSDLCursors[UI_CURSOR_TOOLPICKOBJECT3] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLPICKOBJECT3.tif", 1, 1);
    mSDLCursors[UI_CURSOR_TOOLPLAY] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLPLAY.tif", 1, 1);
    mSDLCursors[UI_CURSOR_TOOLPAUSE] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLPAUSE.tif", 1, 1);
    mSDLCursors[UI_CURSOR_TOOLMEDIAOPEN] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLMEDIAOPEN.tif", 1, 1);
    mSDLCursors[UI_CURSOR_PIPETTE] = makeSDLCursorFromMacTIF("UI_CURSOR_PIPETTE.tif", 3, 29);
    mSDLCursors[UI_CURSOR_TOOLSIT] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLSIT.tif", 20, 15);
    mSDLCursors[UI_CURSOR_TOOLBUY] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLBUY.tif", 20, 15);
    mSDLCursors[UI_CURSOR_TOOLOPEN] = makeSDLCursorFromMacTIF("UI_CURSOR_TOOLOPEN.tif", 20, 15);
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING] = makeSDLCursorFromMacTIF("UI_CURSOR_PATHFINDING.tif", 16, 16);
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_START] = makeSDLCursorFromMacTIF("UI_CURSOR_PATHFINDING_START.tif", 16, 16);
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_START_ADD] = makeSDLCursorFromMacTIF("UI_CURSOR_PATHFINDING_START_ADD.tif", 16, 16);
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_END] = makeSDLCursorFromMacTIF("UI_CURSOR_PATHFINDING_END.tif", 16, 16);
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_END_ADD] = makeSDLCursorFromMacTIF("UI_CURSOR_PATHFINDING_END_ADD.tif", 16, 16);
    mSDLCursors[UI_CURSOR_TOOLNO] = makeSDLCursorFromMacTIF("UI_CURSOR_NO.tif", 8, 8);
#elif LL_WINDOWS
    // Load the branded cursors from the exe's embedded .cur resources — the
    // same resource names LLWindowWin32::initCursors uses. Hot-spots come from
    // the resources themselves (GetIconInfo), so there's no hand-tuned table.
    mSDLCursors[UI_CURSOR_TOOLGRAB] = makeSDLCursorFromWin32("TOOLGRAB");
    mSDLCursors[UI_CURSOR_TOOLLAND] = makeSDLCursorFromWin32("TOOLLAND");
    mSDLCursors[UI_CURSOR_TOOLFOCUS] = makeSDLCursorFromWin32("TOOLFOCUS");
    mSDLCursors[UI_CURSOR_TOOLCREATE] = makeSDLCursorFromWin32("TOOLCREATE");
    mSDLCursors[UI_CURSOR_ARROWDRAG] = makeSDLCursorFromWin32("ARROWDRAG");
    mSDLCursors[UI_CURSOR_ARROWCOPY] = makeSDLCursorFromWin32("ARROWCOPY");
    mSDLCursors[UI_CURSOR_ARROWDRAGMULTI] = makeSDLCursorFromWin32("ARROWDRAGMULTI");
    mSDLCursors[UI_CURSOR_ARROWCOPYMULTI] = makeSDLCursorFromWin32("ARROWCOPYMULTI");
    mSDLCursors[UI_CURSOR_NOLOCKED] = makeSDLCursorFromWin32("NOLOCKED");
    mSDLCursors[UI_CURSOR_ARROWLOCKED] = makeSDLCursorFromWin32("ARROWLOCKED");
    mSDLCursors[UI_CURSOR_GRABLOCKED] = makeSDLCursorFromWin32("GRABLOCKED");
    mSDLCursors[UI_CURSOR_TOOLTRANSLATE] = makeSDLCursorFromWin32("TOOLTRANSLATE");
    mSDLCursors[UI_CURSOR_TOOLROTATE] = makeSDLCursorFromWin32("TOOLROTATE");
    mSDLCursors[UI_CURSOR_TOOLSCALE] = makeSDLCursorFromWin32("TOOLSCALE");
    mSDLCursors[UI_CURSOR_TOOLCAMERA] = makeSDLCursorFromWin32("TOOLCAMERA");
    mSDLCursors[UI_CURSOR_TOOLPAN] = makeSDLCursorFromWin32("TOOLPAN");
    mSDLCursors[UI_CURSOR_TOOLZOOMIN] = makeSDLCursorFromWin32("TOOLZOOMIN");
    mSDLCursors[UI_CURSOR_TOOLZOOMOUT] = makeSDLCursorFromWin32("TOOLZOOMOUT");
    mSDLCursors[UI_CURSOR_TOOLPICKOBJECT3] = makeSDLCursorFromWin32("TOOLPICKOBJECT3");
    mSDLCursors[UI_CURSOR_TOOLPLAY] = makeSDLCursorFromWin32("TOOLPLAY");
    mSDLCursors[UI_CURSOR_TOOLPAUSE] = makeSDLCursorFromWin32("TOOLPAUSE");
    mSDLCursors[UI_CURSOR_TOOLMEDIAOPEN] = makeSDLCursorFromWin32("TOOLMEDIAOPEN");
    mSDLCursors[UI_CURSOR_PIPETTE] = makeSDLCursorFromWin32("TOOLPIPETTE");
    mSDLCursors[UI_CURSOR_TOOLSIT] = makeSDLCursorFromWin32("TOOLSIT");
    mSDLCursors[UI_CURSOR_TOOLBUY] = makeSDLCursorFromWin32("TOOLBUY");
    mSDLCursors[UI_CURSOR_TOOLOPEN] = makeSDLCursorFromWin32("TOOLOPEN");
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING] = makeSDLCursorFromWin32("TOOLPATHFINDING");
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_START] = makeSDLCursorFromWin32("TOOLPATHFINDINGPATHSTART");
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_START_ADD] = makeSDLCursorFromWin32("TOOLPATHFINDINGPATHSTARTADD");
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_END] = makeSDLCursorFromWin32("TOOLPATHFINDINGPATHEND");
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_END_ADD] = makeSDLCursorFromWin32("TOOLPATHFINDINGPATHENDADD");
    mSDLCursors[UI_CURSOR_TOOLNO] = makeSDLCursorFromWin32("TOOLNO");
#else
    mSDLCursors[UI_CURSOR_TOOLGRAB] = makeSDLCursorFromBMP("lltoolgrab.BMP",2,13);
    mSDLCursors[UI_CURSOR_TOOLLAND] = makeSDLCursorFromBMP("lltoolland.BMP",1,6);
    mSDLCursors[UI_CURSOR_TOOLFOCUS] = makeSDLCursorFromBMP("lltoolfocus.BMP",8,5);
    mSDLCursors[UI_CURSOR_TOOLCREATE] = makeSDLCursorFromBMP("lltoolcreate.BMP",7,7);
    mSDLCursors[UI_CURSOR_ARROWDRAG] = makeSDLCursorFromBMP("arrowdrag.BMP",0,0);
    mSDLCursors[UI_CURSOR_ARROWCOPY] = makeSDLCursorFromBMP("arrowcop.BMP",0,0);
    mSDLCursors[UI_CURSOR_ARROWDRAGMULTI] = makeSDLCursorFromBMP("llarrowdragmulti.BMP",0,0);
    mSDLCursors[UI_CURSOR_ARROWCOPYMULTI] = makeSDLCursorFromBMP("arrowcopmulti.BMP",0,0);
    mSDLCursors[UI_CURSOR_NOLOCKED] = makeSDLCursorFromBMP("llnolocked.BMP",8,8);
    mSDLCursors[UI_CURSOR_ARROWLOCKED] = makeSDLCursorFromBMP("llarrowlocked.BMP",0,0);
    mSDLCursors[UI_CURSOR_GRABLOCKED] = makeSDLCursorFromBMP("llgrablocked.BMP",2,13);
    mSDLCursors[UI_CURSOR_TOOLTRANSLATE] = makeSDLCursorFromBMP("lltooltranslate.BMP",0,0);
    mSDLCursors[UI_CURSOR_TOOLROTATE] = makeSDLCursorFromBMP("lltoolrotate.BMP",0,0);
    mSDLCursors[UI_CURSOR_TOOLSCALE] = makeSDLCursorFromBMP("lltoolscale.BMP",0,0);
    mSDLCursors[UI_CURSOR_TOOLCAMERA] = makeSDLCursorFromBMP("lltoolcamera.BMP",7,5);
    mSDLCursors[UI_CURSOR_TOOLPAN] = makeSDLCursorFromBMP("lltoolpan.BMP",7,5);
    mSDLCursors[UI_CURSOR_TOOLZOOMIN] = makeSDLCursorFromBMP("lltoolzoomin.BMP",7,5);
    mSDLCursors[UI_CURSOR_TOOLZOOMOUT] = makeSDLCursorFromBMP("lltoolzoomout.BMP", 7, 5);
    mSDLCursors[UI_CURSOR_TOOLPICKOBJECT3] = makeSDLCursorFromBMP("toolpickobject3.BMP",0,0);
    mSDLCursors[UI_CURSOR_TOOLPLAY] = makeSDLCursorFromBMP("toolplay.BMP",0,0);
    mSDLCursors[UI_CURSOR_TOOLPAUSE] = makeSDLCursorFromBMP("toolpause.BMP",0,0);
    mSDLCursors[UI_CURSOR_TOOLMEDIAOPEN] = makeSDLCursorFromBMP("toolmediaopen.BMP",0,0);
    mSDLCursors[UI_CURSOR_PIPETTE] = makeSDLCursorFromBMP("lltoolpipette.BMP",2,28);
    mSDLCursors[UI_CURSOR_TOOLSIT] = makeSDLCursorFromBMP("toolsit.BMP",20,15);
    mSDLCursors[UI_CURSOR_TOOLBUY] = makeSDLCursorFromBMP("toolbuy.BMP",20,15);
    mSDLCursors[UI_CURSOR_TOOLOPEN] = makeSDLCursorFromBMP("toolopen.BMP",20,15);
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING] = makeSDLCursorFromBMP("lltoolpathfinding.BMP", 16, 16);
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_START] = makeSDLCursorFromBMP("lltoolpathfindingpathstart.BMP", 16, 16);
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_START_ADD] = makeSDLCursorFromBMP("lltoolpathfindingpathstartadd.BMP", 16, 16);
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_END] = makeSDLCursorFromBMP("lltoolpathfindingpathend.BMP", 16, 16);
    mSDLCursors[UI_CURSOR_TOOLPATHFINDING_PATH_END_ADD] = makeSDLCursorFromBMP("lltoolpathfindingpathendadd.BMP", 16, 16);
    mSDLCursors[UI_CURSOR_TOOLNO] = makeSDLCursorFromBMP("llno.BMP",8,8);
#endif // LL_DARWIN
}

void LLWindowSDL::quitCursors()
{
    // SDL3 cursors are owned by the SDL library, not by any window —
    // SDL_DestroyCursor must be called regardless of whether mWindow is
    // still alive. The previous mWindow-guard skipped destruction when
    // the window was nulled (e.g. failed switchContext), leaking cursors.
    for (int i=0; i<UI_CURSOR_COUNT; ++i)
    {
        if (mSDLCursors[i])
        {
            SDL_DestroyCursor(mSDLCursors[i]);
            mSDLCursors[i] = nullptr;
        }
    }
}

void LLWindowSDL::captureMouse()
{
    // SDL already enforces the semantics that captureMouse is
    // used for, i.e. that we continue to get mouse events as long
    // as a button is down regardless of whether we left the
    // window, and in a less obnoxious way than SDL_WM_GrabInput
    // which would confine the cursor to the window too.

    LL_DEBUGS() << "LLWindowSDL::captureMouse" << LL_ENDL;
}

void LLWindowSDL::releaseMouse()
{
    // see LWindowSDL::captureMouse()

    LL_DEBUGS() << "LLWindowSDL::releaseMouse" << LL_ENDL;
}

void LLWindowSDL::hideCursor()
{
    if (mCursorHidden) return;

    mCursorHidden = true;
    mHideCursorPermanent = true;

    // The "permanent hide" path is what mouselook / camera-grab tools take
    // when they want pointer-lock semantics. SDL3 expresses this as relative
    // mouse mode: the OS cursor is hidden and parked, and motion arrives as
    // hardware-level xrel/yrel via SDL_EVENT_MOUSE_MOTION. This is the modern
    // replacement for the warp-cursor-to-center loop — warps go through the
    // compositor (Wayland in particular coalesces and rate-limits them) and
    // the resulting position-diff motion truncates at high frame rates.
    //
    // Skip relative mode when the active pointer is touch/pen: SDL3 doesn't
    // deliver relative deltas from finger drags or stylus motion (the OS
    // model is absolute positions, not relative motion), so relative mode
    // would just freeze the mouselook camera. Visibility-only hide on those
    // devices lets the position-diff path (xrel/yrel synthesised by SDL
    // from consecutive touch positions) drive the camera signal instead.
    if (mWindow && !mAbsoluteCursorPosition)
    {
        if (SDL_SetWindowRelativeMouseMode(mWindow, true))
        {
            mRelativeMouseMode = true;
            // Drop any motion that was queued before entering pointer-lock —
            // it isn't relevant to the new camera-control session.
            mMouseDeltaAccumX = 0.f;
            mMouseDeltaAccumY = 0.f;
            // Fresh session: forget any deferred warp left over from a
            // prior aborted exit so it doesn't surface on next showCursor().
            mHasDeferredCursorWarp = false;
            // Intentionally NOT zeroing mPendingWarpSuppressCount: any
            // pending synthetic motion events from non-relative-mode warps
            // queued just before mouselook entry are still sitting in SDL's
            // event queue, and the motion handler needs them suppressed
            // before they reach the accumulator (where, in relative mode,
            // they'd be sampled as camera input). The count drains
            // naturally as those events are processed.
        }
        else
        {
            LL_WARNS("Window") << "SDL_SetWindowRelativeMouseMode(true) failed: "
                               << SDL_GetError()
                               << " — falling back to visibility-only hide." << LL_ENDL;
            SDL_HideCursor();
        }
    }
    else if (mWindow)
    {
        SDL_HideCursor();
    }
}

void LLWindowSDL::showCursor()
{
    if (!mCursorHidden) return;

    mCursorHidden = false;
    mHideCursorPermanent = false;

    if (mWindow)
    {
        if (mRelativeMouseMode)
        {
            // Pre-increment the suppress count on X11: when XInput2 raw
            // input is deactivated, SDL emits a synthetic motion event as
            // the cursor un-parks back to the lock position. Without the
            // bump that motion would land in the accumulator at the moment
            // the user is exiting mouselook — visible as a one-frame camera
            // jolt. Wayland's relative-pointer protocol
            // (Wayland_input_disable_relative_pointer) does not synthesise
            // a motion event on disable, so the bump would eat the user's
            // first real motion sample after exit — visible as a one-frame
            // cursor stutter. Gate on the protocol we detected at
            // createContext.
            if (mServerProtocol == X11)
            {
                ++mPendingWarpSuppressCount;
            }
            SDL_SetWindowRelativeMouseMode(mWindow, false);
            mRelativeMouseMode = false;
            mMouseDeltaAccumX = 0.f;
            mMouseDeltaAccumY = 0.f;

            // Place the visible cursor where the viewer wants it before it
            // becomes visible again. Two cases:
            //   * alt-cam / focus-tool handleMouseUp called setCursorPosition
            //     during relative mode → mHasDeferredCursorWarp carries the
            //     click-point or screen-target it asked for.
            //   * mouselook deselect doesn't call setCursorPosition at all
            //     (moveCursorToCenter is a no-op on SDL3) → default to the
            //     window center, which matches the "you exit mouselook
            //     looking at the middle of the world view" expectation.
            LLCoordWindow warp_to;
            if (mHasDeferredCursorWarp)
            {
                warp_to = mDeferredCursorWarp;
                mHasDeferredCursorWarp = false;
            }
            else
            {
                int w = 0, h = 0;
                SDL_GetWindowSizeInPixels(mWindow, &w, &h);
                warp_to.mX = w / 2;
                warp_to.mY = h / 2;
            }
            const float density = SDL_GetWindowPixelDensity(mWindow);
            const float div = density > 0.f ? density : 1.f;
            const float target_logical_x = (F32)warp_to.mX / div;
            const float target_logical_y = (F32)warp_to.mY / div;

            // Suppress the synthetic motion event SDL queues for this warp —
            // same reason as setCursorPosition. Skip if the warp won't move
            // the cursor (no synthetic event would be emitted in that case).
            float curr_x = 0.f, curr_y = 0.f;
            SDL_GetMouseState(&curr_x, &curr_y);
            if (fabsf(target_logical_x - curr_x) > 0.5f ||
                fabsf(target_logical_y - curr_y) > 0.5f)
            {
                ++mPendingWarpSuppressCount;
            }
            SDL_WarpMouseInWindow(mWindow, target_logical_x, target_logical_y);
            mMouseDeltaAccumX = 0.f;
            mMouseDeltaAccumY = 0.f;
        }
        // Safe to call unconditionally — SDL_ShowCursor is a no-op when the
        // cursor is already visible, and we need it for the fallback path
        // where relative mode failed and we used SDL_HideCursor instead.
        SDL_ShowCursor();
    }
}

void LLWindowSDL::hideCursorUntilMouseMove()
{
    // Auto-hide after the cursor sits idle — visibility-only. Must NOT enter
    // relative mode here: the cursor is supposed to reappear at its current
    // screen position on the next motion event, not get teleported back to
    // wherever pointer-lock parked it.
    if (mHideCursorPermanent || mCursorHidden) return;
    mCursorHidden = true;
    SDL_HideCursor();
}

void LLWindowSDL::showCursorFromMouseMove()
{
    if (mHideCursorPermanent || !mCursorHidden) return;
    mCursorHidden = false;
    SDL_ShowCursor();
}

//
// LLSplashScreenSDL — a small borderless status window shown while the viewer
// loads, mirroring LLSplashScreenWin32 (the app name plus an updatable status
// line). Drawn with SDL_Renderer; text rendered with SDL_ttf from one of the
// viewer's bundled fonts.
//
namespace
{
    constexpr int   SPLASH_W        = 480;
    constexpr int   SPLASH_H        = 120;
    constexpr float SPLASH_FONT_PT  = 18.0f;
    // Inter (variable WOFF2) — FreeType decompresses WOFF2 via brotli, which the
    // viewer's freetype build (shared with SDL3_ttf) enables.
    const char*     SPLASH_FONT     = "InterVariable.woff2";
    // Branded icon, vertically centered in a square box on the left.
    constexpr int   SPLASH_ICON     = 80;
    constexpr int   SPLASH_ICON_X   = 20;
    constexpr int   SPLASH_ICON_Y   = (SPLASH_H - SPLASH_ICON) / 2;
    const char*     SPLASH_ICON_PNG = "alchemy_logo.png";
    // Status text is centered in the area to the right of the icon.
    constexpr int   SPLASH_TEXT_X0  = SPLASH_ICON_X + SPLASH_ICON + 16;
    constexpr int   SPLASH_TEXT_X1  = SPLASH_W - 16;
}

LLSplashScreenSDL::LLSplashScreenSDL()
{
}

LLSplashScreenSDL::~LLSplashScreenSDL()
{
}

void LLSplashScreenSDL::showImpl()
{
    // The splash is shown before createWindow()/init_sdl(), so the video
    // subsystem may not be up yet. SDL_InitSubSystem is reference-counted, so
    // initialising it here is safe; hideImpl() releases exactly this reference,
    // leaving the count the main window's own init_sdl() established.
    //
    // This is the process's *first* video init, so the SDL hints must already
    // be set: the Cocoa backend's registerUserDefaults reads them once here and
    // never again (e.g. SDL_HINT_MAC_SCROLL_MOMENTUM → AppleMomentumScrollSupported).
    // Setting them only in init_sdl() would be too late and silently disable
    // macOS momentum scrolling. set_sdl_hints() is idempotent.
    set_sdl_hints();

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        LL_WARNS() << "Splash: SDL_InitSubSystem(VIDEO) failed: " << SDL_GetError() << LL_ENDL;
        return;
    }
    mInitedVideo = true;

    if (TTF_Init())
    {
        mInitedTTF = true;
    }
    else
    {
        LL_WARNS() << "Splash: TTF_Init failed: " << SDL_GetError() << LL_ENDL;
    }

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Alchemy");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, SPLASH_W);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, SPLASH_H);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_UTILITY_BOOLEAN, true);
    mWindow = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);

    if (!mWindow)
    {
        LL_WARNS() << "Splash: window creation failed: " << SDL_GetError() << LL_ENDL;
        return;
    }

    // Use a software renderer that draws into the window's surface rather than
    // an accelerated one. SDL's accelerated renderer would create its own
    // GL/D3D device on the splash window, which can collide with the main
    // window's OpenGL context initialisation that follows — keep the splash
    // entirely off the GPU.
    SDL_Surface* winsurf = SDL_GetWindowSurface(mWindow);
    if (winsurf)
    {
        mRenderer = SDL_CreateSoftwareRenderer(winsurf);
    }
    if (!mRenderer)
    {
        LL_WARNS() << "Splash: software renderer creation failed: " << SDL_GetError() << LL_ENDL;
        // Don't leave a blank borderless always-on-top window up for the whole
        // load. Tear the partial splash window down now; hideImpl() still
        // balances the TTF/VIDEO subsystem refcounts taken above.
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
        return;
    }

    if (mInitedTTF)
    {
        const std::string font_path = gDirUtilp->add(gDirUtilp->getAppRODataDir(), "fonts", SPLASH_FONT);
        mFont = TTF_OpenFont(font_path.c_str(), SPLASH_FONT_PT);
        if (!mFont)
        {
            LL_WARNS() << "Splash: TTF_OpenFont(" << font_path << ") failed: " << SDL_GetError() << LL_ENDL;
        }
    }

    // Branded app icon (PNG), staged into app_settings by CMake.
    if (mRenderer)
    {
        const std::string icon_path = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, SPLASH_ICON_PNG);
        SDL_Surface* icon_surf = IMG_Load(icon_path.c_str());
        if (icon_surf)
        {
            mIcon = SDL_CreateTextureFromSurface(mRenderer, icon_surf);
            // Smooth the downscale from the source (256px) to the splash box.
            if (mIcon)
            {
                SDL_SetTextureScaleMode(mIcon, SDL_SCALEMODE_LINEAR);
            }
            SDL_DestroySurface(icon_surf);
        }
        else
        {
            LL_WARNS() << "Splash: IMG_Load(" << icon_path << ") failed: " << SDL_GetError() << LL_ENDL;
        }
    }

    render();
}

void LLSplashScreenSDL::updateImpl(const std::string& mesg)
{
    mMessage = mesg;
    render();
}

void LLSplashScreenSDL::render()
{
    if (!mRenderer)
    {
        return;
    }

    SDL_SetRenderDrawColor(mRenderer, 28, 28, 34, 255);
    SDL_RenderClear(mRenderer);

    // Thin accent border around the edge.
    SDL_SetRenderDrawColor(mRenderer, 90, 110, 160, 255);
    SDL_FRect border = { 0.5f, 0.5f, (float)SPLASH_W - 1.f, (float)SPLASH_H - 1.f };
    SDL_RenderRect(mRenderer, &border);

    // Branded icon, vertically centered on the left.
    if (mIcon)
    {
        SDL_FRect dst = { (float)SPLASH_ICON_X, (float)SPLASH_ICON_Y, (float)SPLASH_ICON, (float)SPLASH_ICON };
        SDL_RenderTexture(mRenderer, mIcon, nullptr, &dst);
    }

    if (mFont)
    {
        TTF_Font* font = (TTF_Font*)mFont;
        const SDL_Color fg = { 235, 235, 240, 255 };
        const std::string& text = mMessage.empty() ? std::string("Loading Alchemy...") : mMessage;
        SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), text.length(), fg);
        if (surf)
        {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(mRenderer, surf);
            if (tex)
            {
                // Center the text in the area to the right of the icon.
                const int region = SPLASH_TEXT_X1 - SPLASH_TEXT_X0;
                SDL_FRect dst = {
                    (float)(SPLASH_TEXT_X0 + (region - surf->w) / 2),
                    (float)((SPLASH_H - surf->h) / 2),
                    (float)surf->w,
                    (float)surf->h
                };
                SDL_RenderTexture(mRenderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(surf);
        }
    }

    // Flush the queued render commands into the window surface, then blit that
    // surface to the screen (the software renderer targets the surface, not the
    // window directly, so SDL_RenderPresent alone wouldn't show anything).
    SDL_RenderPresent(mRenderer);
    SDL_UpdateWindowSurface(mWindow);

    // No SDL event loop is running yet (the splash precedes the main window and
    // SDL_AppIterate), so pump once here to let the window actually composite.
    SDL_PumpEvents();
}

void LLSplashScreenSDL::hideImpl()
{
    if (mIcon)
    {
        SDL_DestroyTexture(mIcon);
        mIcon = nullptr;
    }
    if (mFont)
    {
        TTF_CloseFont((TTF_Font*)mFont);
        mFont = nullptr;
    }
    if (mRenderer)
    {
        SDL_DestroyRenderer(mRenderer);
        mRenderer = nullptr;
    }
    if (mWindow)
    {
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
    }
    if (mInitedTTF)
    {
        TTF_Quit();
        mInitedTTF = false;
    }
    if (mInitedVideo)
    {
        // Release the reference showImpl() took. By now createWindow()'s
        // init_sdl() has taken its own, so VIDEO stays up for the main window.
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        mInitedVideo = false;
    }
}

S32 OSMessageBoxSDL(const std::string& text, const std::string& caption, U32 type)
{
    // A fatal-error dialog can fire before the splash or main window has brought
    // video up. On macOS SDL_ShowMessageBox routes through Cocoa_RegisterApp →
    // registerUserDefaults, which reads our hints exactly once. Make sure they're
    // set first so an early dialog doesn't lock in the wrong defaults (e.g.
    // momentum scrolling). Idempotent if hints are already applied.
    set_sdl_hints();

    // Use the main viewer window as the message box's parent so it is modal
    // to the viewer and stacks correctly above fullscreen on compositors that
    // place dialogs relative to their parent window.
    SDL_Window* const parent = LLWindowSDL::getMainSDLWindow();
    SDL_MessageBoxData oData = { SDL_MESSAGEBOX_INFORMATION, parent, caption.c_str(), text.c_str(), 0, nullptr, nullptr };
    SDL_MessageBoxButtonData btnOk[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, OSBTN_OK, "OK" }};
    SDL_MessageBoxButtonData btnOkCancel [] =  {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, OSBTN_OK, "OK" }, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, OSBTN_CANCEL, "Cancel"} };
    SDL_MessageBoxButtonData btnYesNo[] = { {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, OSBTN_YES, "Yes" }, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, OSBTN_NO, "No"} };

    switch (type)
    {
        default:
        case OSMB_OK:
            oData.flags = SDL_MESSAGEBOX_WARNING;
            oData.buttons = btnOk;
            oData.numbuttons = 1;
            break;
        case OSMB_OKCANCEL:
            oData.flags = SDL_MESSAGEBOX_INFORMATION;
            oData.buttons = btnOkCancel;
            oData.numbuttons = 2;
            break;
        case OSMB_YESNO:
            oData.flags = SDL_MESSAGEBOX_INFORMATION;
            oData.buttons = btnYesNo;
            oData.numbuttons = 2;
            break;
    }

    if(gWindowImplementation != nullptr)
        gWindowImplementation->beforeDialog();

    int btn{0};
    if(SDL_ShowMessageBox( &oData, &btn ))
    {
        if(gWindowImplementation != nullptr)
            gWindowImplementation->afterDialog();
        return btn;
    }

    if(gWindowImplementation != nullptr)
        gWindowImplementation->afterDialog();

    return OSBTN_CANCEL;
}

bool LLWindowSDL::dialogColorPicker( F32 *r, F32 *g, F32 *b)
{
    return false;
}

/*
        Make the raw keyboard data available - used to poke through to LLQtWebKit so
        that Qt/Webkit has access to the virtual keycodes etc. that it needs
*/
LLSD LLWindowSDL::getNativeKeyData()
{
    LLSD result = LLSD::emptyMap();

    // Pass the raw SDL modifier mask (SDL_Keymod) straight through. The media
    // plugin's only consumer is dullahan/CEF, which translates SDL_Keymod into
    // CEF's EVENTFLAG_* itself, so there's no need to approximate a GDK-style
    // mask here (the historical convention this used to emulate).
    U32 modifiers = (U32)mKeyModifiers;

    result["virtual_key"] = (S32)mKeyVirtualKey;
    result["virtual_key_win"] = (S32)LLKeyboardSDL::mapSDLtoWin( mKeyVirtualKey );
    result["modifiers"] = (S32)modifiers;
    // Platform-dependent scancode (SDL_KeyboardEvent.raw): the Mac virtual
    // keycode / X11-evdev keycode CEF needs as native_key_code so the page
    // actually receives the key event (see media_plugin_cef keyEvent()).
    result["sdl_scancode"] = (S32)mKeyRawScanCode;
    return result;
}

// Open a URL with the user's default web browser.
// Must begin with protocol identifier.
void LLWindowSDL::spawnWebBrowser(const std::string& escaped_url, bool async)
{
    bool found = false;
    S32 i;
    for (i = 0; i < gURLProtocolWhitelistCount; i++)
    {
        if (escaped_url.find(gURLProtocolWhitelist[i]) != std::string::npos)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        LL_WARNS() << "spawn_web_browser called for url with protocol not on whitelist: " << escaped_url << LL_ENDL;
        return;
    }

    LL_INFOS() << "spawn_web_browser: " << escaped_url << LL_ENDL;

    if (!SDL_OpenURL(escaped_url.c_str()))
    {
        LL_WARNS() << "spawn_web_browser failed with error: " << SDL_GetError() << LL_ENDL;
    }

    LL_INFOS() << "spawn_web_browser returning." << LL_ENDL;
}

void* LLWindowSDL::getPlatformWindow()
{
    // Note: on Linux this returns nullptr by design. The X11 Window handle
    // (typedef Window = XID = unsigned long, not a pointer) and the Wayland
    // wl_surface* don't share a single native-handle type, and current
    // callers all cast directly to HWND. Linux code that needs the native
    // handle should reach into LLWindowSDL::sX11Data or sWaylandData, which
    // are populated in createContext() with the correct typed pointers.
    void* ret = nullptr;
    if (mWindow)
    {
#if LL_WINDOWS
        ret = SDL_GetPointerProperty(SDL_GetWindowProperties(mWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif LL_DARWIN
        ret = SDL_GetPointerProperty(SDL_GetWindowProperties(mWindow), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#endif
    }
    return ret;
}

#if LL_WINDOWS
void LLWindowSDL::installWin32Subclass()
{
    if (mPrevWndProc) // already installed
        return;

    HWND hwnd = (HWND)getPlatformWindow();
    if (!hwnd)
        return;

    mWin32Hwnd = hwnd;
    mPrevWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                              (LONG_PTR)&LLWindowSDL::win32WndProc);
}

void LLWindowSDL::removeWin32Subclass()
{
    if (mWin32Hwnd && mPrevWndProc)
    {
        SetWindowLongPtrW(mWin32Hwnd, GWLP_WNDPROC, (LONG_PTR)mPrevWndProc);
    }
    mWin32Hwnd = nullptr;
    mPrevWndProc = nullptr;
}

// SDL pumps the Win32 message queue on the main thread, so a cross-process
// SendMessage(WM_COPYDATA) is dispatched here synchronously on the main
// thread — no cross-thread post needed (unlike LLWindowWin32, whose WndProc
// runs on its own window thread). Everything else chains to SDL's WndProc so
// the window keeps working normally.
LRESULT CALLBACK LLWindowSDL::win32WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    LLWindowSDL* self = gWindowImplementation;

    if (msg == WM_COPYDATA && self)
    {
        // received a URL from a second instance (see sendURLToOtherInstance).
        PCOPYDATASTRUCT cds = (PCOPYDATASTRUCT)lparam;
        // The only downstream consumer (LLViewerWindow::handleDataCopy) treats
        // the payload as a C string. Reject oversized or empty messages and
        // guarantee NUL-termination so a misbehaving sender can't force a huge
        // alloc or trigger an OOB read.
        constexpr DWORD MAX_WM_COPYDATA_BYTES = 64 * 1024;
        if (cds && cds->lpData && cds->cbData != 0 && cds->cbData <= MAX_WM_COPYDATA_BYTES)
        {
            const DWORD cb = cds->cbData;
            U8* data = new U8[cb + 1];
            memcpy(data, cds->lpData, cb);
            data[cb] = 0;
            self->handleDataCopy((S32)cds->dwData, data);
            delete[] data;
        }
        return TRUE;
    }

    WNDPROC prev = (self && self->mWin32Hwnd == hwnd) ? self->mPrevWndProc : nullptr;
    if (prev)
        return CallWindowProcW(prev, hwnd, msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void LLWindowSDL::handleDataCopy(S32 data_type, void* data)
{
    if (mCallbacks)
    {
        mCallbacks->handleDataCopy(this, data_type, data);
    }
}

void* LLWindowSDL::getDirectInput8()
{
    return &gSDLDirectInput8;
}

bool LLWindowSDL::getInputDevices(U32 device_type_filter,
                                  std::function<bool(std::string&, LLSD&, void*)> osx_callback,
                                  void* di8_devices_callback,
                                  void* userdata)
{
    if (gSDLDirectInput8 != nullptr)
    {
        // Enumerate devices
        HRESULT status = gSDLDirectInput8->EnumDevices(
            (DWORD)device_type_filter,
            (LPDIENUMDEVICESCALLBACK)di8_devices_callback,
            (LPVOID*)userdata,
            DIEDFL_ATTACHEDONLY);

        return status == DI_OK;
    }
    return false;
}
#endif // LL_WINDOWS

void LLWindowSDL::bringToFront()
{
    // This is currently used when we are 'launched' to a specific
    // map position externally.
    LL_INFOS() << "bringToFront" << LL_ENDL;
    if (mWindow && !mFullscreen)
    {
        SDL_RaiseWindow(mWindow);
    }
}

//static
std::vector<std::string> LLWindowSDL::getDynamicFallbackFontList()
{
    std::vector<std::string> rtns;
#if LL_LINUX
    // Use libfontconfig to find us a nice ordered list of fallback fonts
    // specific to this system.
    std::string final_fallback("/usr/share/fonts/truetype/kochi/kochi-gothic.ttf");
    const int max_font_count_cutoff = 40; // fonts are expensive in the current system, don't enumerate an arbitrary number of them
    // Our 'ideal' font properties which define the sorting results.
    // slant=0 means Roman, index=0 means the first face in a font file
    // (the one we actually use), weight=80 means medium weight,
    // spacing=0 means proportional spacing.
    std::string sort_order("slant=0:index=0:weight=80:spacing=0");
    // elide_unicode_coverage removes fonts from the list whose unicode
    // range is covered by fonts earlier in the list.  This usually
    // removes ~90% of the fonts as redundant (which is great because
    // the font list can be huge), but might unnecessarily reduce the
    // renderable range if for some reason our FreeType actually fails
    // to use some of the fonts we want it to.
    const bool elide_unicode_coverage = true;

    FcFontSet *fs = nullptr;
    FcPattern *sortpat = nullptr;

    LL_INFOS() << "Getting system font list from FontConfig..." << LL_ENDL;

    // If the user has a system-wide language preference, then favor
    // fonts from that language group.  This doesn't affect the types
    // of languages that can be displayed, but ensures that their
    // preferred language is rendered from a single consistent font where
    // possible.
    FL_Locale *locale = nullptr;
    FL_Success success = FL_FindLocale(&locale, FL_MESSAGES);
    if (success != 0)
    {
        if (success >= 2 && locale->lang) // confident!
        {
            LL_INFOS("AppInit") << "Language " << locale->lang << LL_ENDL;
            LL_INFOS("AppInit") << "Location " << locale->country << LL_ENDL;
            LL_INFOS("AppInit") << "Variant " << locale->variant << LL_ENDL;

            LL_INFOS() << "Preferring fonts of language: "
                       << locale->lang
                       << LL_ENDL;
            sort_order = "lang=" + std::string(locale->lang) + ":"
                         + sort_order;
        }
    }
    FL_FreeLocale(&locale);

    if (!FcInit())
    {
        LL_WARNS() << "FontConfig failed to initialize." << LL_ENDL;
        rtns.push_back(final_fallback);
        return rtns;
    }

    sortpat = FcNameParse((FcChar8*) sort_order.c_str());
    if (sortpat)
    {
        // Sort the list of system fonts from most-to-least-desirable.
        FcResult result;
        fs = FcFontSort(nullptr, sortpat, elide_unicode_coverage, nullptr, &result);
        FcPatternDestroy(sortpat);
    }

    int found_font_count = 0;
    if (fs)
    {
        // Get the full pathnames to the fonts, where available,
        // which is what we really want.
        found_font_count = fs->nfont;
        for (int i=0; i<fs->nfont; ++i)
        {
            FcChar8 *filename;
            if (FcResultMatch == FcPatternGetString(fs->fonts[i], FC_FILE, 0, &filename) && filename)
            {
                rtns.push_back(std::string((const char*)filename));
                if (rtns.size() >= max_font_count_cutoff)
                    break; // hit limit
            }
        }
        FcFontSetDestroy (fs);
    }

    LL_DEBUGS() << "Using font list: " << LL_ENDL;
    for (auto it = rtns.begin(); it != rtns.end(); ++it)
    {
        LL_DEBUGS() << "  file: " << *it << LL_ENDL;
    }

    LL_INFOS() << "Using " << rtns.size() << "/" << found_font_count << " system fonts." << LL_ENDL;

    rtns.push_back(final_fallback);
#endif
    return rtns;
}

void LLWindowSDL::setLanguageTextInput(const LLCoordGL& position)
{
    if (!mWindow)
    {
        return;
    }

    // SDL_SetTextInputArea wants the rect and cursor offset in screen-coord
    // (logical) units, not pixels — that's the unit the IME backends
    // (ibus / fcitx / IMM via SDL) speak. LLCoordWindow on this backend is
    // in PIXELS (see the coord-space contract above), so we divide by
    // pixel density at the boundary.
    const float density = SDL_GetWindowPixelDensity(mWindow);
    const float div = density > 0.f ? density : 1.f;

    // If the active preeditor can give us its actual bounds + caret, feed
    // those to SDL so the platform IME anchors its candidate-list popup to
    // the real text input area (rather than a magic 500x16 box guessed at
    // the caret). Mirrors how the Win32 backend builds CANDIDATEFORM from
    // the same LLPreeditor::getPreeditLocation query — see
    // llwindowwin32.cpp:4164.
    if (mPreeditor)
    {
        LLCoordGL caret_gl;
        LLRect bounds_gl;
        if (mPreeditor->getPreeditLocation(-1, &caret_gl, &bounds_gl, nullptr))
        {
            // LLRect uses GL convention (Y up, mTop > mBottom). convertCoords
            // flips Y so the GL-top-left maps to the window-top-left (the
            // corner with the smaller window-Y), and likewise GL-bottom-right
            // maps to window-bottom-right.
            LLCoordWindow top_left;
            LLCoordWindow bottom_right;
            convertCoords(LLCoordGL(bounds_gl.mLeft, bounds_gl.mTop), &top_left);
            convertCoords(LLCoordGL(bounds_gl.mRight, bounds_gl.mBottom), &bottom_right);

            LLCoordWindow caret_win;
            convertCoords(caret_gl, &caret_win);

            SDL_Rect rect;
            rect.x = llfloor(top_left.mX / div);
            rect.y = llfloor(top_left.mY / div);
            rect.w = llfloor((bottom_right.mX - top_left.mX) / div);
            rect.h = llfloor((bottom_right.mY - top_left.mY) / div);

            // A widget that's still being laid out can return a default-
            // constructed LLRect (all zeros); SDL_SetTextInputArea with a
            // zero-sized rect would anchor the IME popup to the window
            // origin. Fall through to the caret-based fallback instead.
            if (rect.w > 0 && rect.h > 0)
            {
                const int cursor_x = llfloor((caret_win.mX - top_left.mX) / div);
                SDL_SetTextInputArea(mWindow, &rect, cursor_x);
                return;
            }
        }
    }

    // Fallback: no preeditor (or it couldn't compute its bounds). Use a
    // single-line guess centred at the supplied caret position, in line with
    // the pre-improvement behaviour — enough for the IME to place its popup
    // roughly under the cursor.
    LLCoordWindow caret_win;
    convertCoords(position, &caret_win);

    SDL_Rect rect;
    rect.x = llfloor(caret_win.mX / div);
    rect.y = llfloor(caret_win.mY / div);
    rect.w = 500;
    rect.h = 16;
    SDL_SetTextInputArea(mWindow, &rect, 0);
}

void LLWindowSDL::allowLanguageTextInput(LLPreeditor* preeditor, bool b)
{
    // Track which widget (if any) currently wants to receive IME composition
    // updates. SDL3's SDL_EVENT_TEXT_EDITING events are dispatched to
    // mPreeditor in handleEvent(); if no widget owns IME they get dropped.
    //
    // We deliberately do NOT toggle SDL_StartTextInput / SDL_StopTextInput
    // here: SDL3 binds both committed-text events (TEXT_INPUT) and
    // composition events (TEXT_EDITING) to the same flag, and the viewer
    // routes plain unicode chars through TEXT_INPUT for menu jump keys
    // (LLMenuGL::handleUnicodeCharHere -> handleJumpKey, llmenugl.cpp:3139)
    // and similar non-editor-focused unicode consumers. Gating SDL text
    // input on widget focus the way Win32's allowLanguageTextInput gates
    // IMM would kill those paths.
    //
    // Mirror the Win32 backend's "only the owning widget releases IME" rule
    // so a non-focused widget's setEnabled(false) can't stomp on the
    // focused widget's preedit state.
    if (b)
    {
        mPreeditor = preeditor;
        return;
    }

    if (mPreeditor != preeditor)
    {
        return;
    }

    // Owning widget is releasing IME. Two cleanups before we forget about it:
    //
    //   1) resetPreedit on the widget — the in-flight composition string
    //      lives inside the widget's mText (as a marked-up range) and
    //      LLPreeditor::resetPreedit is the documented way to remove it.
    //      Without this, focusing away mid-composition leaves the partial
    //      preedit text visible as if it had been committed, and a future
    //      re-focus would render it as a stale "preedit" highlight.
    //
    //   2) SDL_ClearComposition on the window — tells the platform IME
    //      (ibus/fcitx/IMM) to dismiss its composition popup. Win32's
    //      interruptLanguageTextInput uses NI_COMPOSITIONSTR/CPS_COMPLETE
    //      to *commit* instead, but SDL3 only exposes the cancel path;
    //      losing in-flight composition on focus-away is the tradeoff.
    //
    // Gate resetPreedit on an active composition for the same reason as the
    // SDL_EVENT_TEXT_EDITING handler above: LLLineEditor::resetPreedit treats
    // "selection-without-preedit" as the IME-overwrites-selection pattern and
    // calls deleteSelection(). Focus loss via Tab on a line editor with
    // SelectAllOnFocusReceived (the viewer default for most fields) lands
    // here with the whole text selected and no preedit — and would wipe the
    // field's contents on every tab-out.
    if (mPreeditor)
    {
        S32 preedit_pos = 0, preedit_len = 0;
        mPreeditor->getPreeditRange(&preedit_pos, &preedit_len);
        if (preedit_len > 0)
        {
            mPreeditor->resetPreedit();
        }
    }
    mPreeditor = nullptr;
    if (mWindow)
    {
        SDL_ClearComposition(mWindow);
    }
}

F32 LLWindowSDL::getSystemUISize()
{
    if(mWindow)
    {
        F32 scale = SDL_GetWindowDisplayScale(mWindow);
        if (scale > 0.0f)
        {
            return scale;
        }
    }
    return 1.f;
}

#if LL_DARWIN
// static
U64 LLWindowSDL::getVramSize()
{
    CGLRendererInfoObj info = 0;
    GLint vram_megabytes = 0;
    int num_renderers = 0;
    CGLError the_err = CGLQueryRendererInfo (CGDisplayIDToOpenGLDisplayMask(kCGDirectMainDisplay), &info, &num_renderers);
    if(0 == the_err)
    {
        // The name, uses, and other platform definitions of gGLManager.mVRAM suggest that this is supposed to be total vram in MB,
        // rather than, say, just the texture memory. The two exceptions are:
        // 1. LLAppViewer::getViewerInfo() puts the value in a field labeled "TEXTURE_MEMORY"
        // 2. For years, this present function used kCGLRPTextureMemoryMegabytes
        // Now we use kCGLRPVideoMemoryMegabytes to bring it in line with everything else (except thatone label).
        CGLDescribeRenderer (info, 0, kCGLRPVideoMemoryMegabytes, &vram_megabytes);
        CGLDestroyRendererInfo (info);
    }
    else
    {
        vram_megabytes = 256;
    }

    return (U64)vram_megabytes; // return value is in megabytes.
}

//static
void LLWindowSDL::setUseMultGL(bool use_mult_gl)
{
    bool was_enabled = sUseMultGL;

    sUseMultGL = use_mult_gl;

    if (gGLManager.mInited)
    {
        CGLContextObj ctx = CGLGetCurrentContext();
        //enable multi-threaded OpenGL (whether or not sUseMultGL actually changed)
        if (sUseMultGL)
        {
            CGLError cgl_err =  CGLEnable( ctx, kCGLCEMPEngine);
            if (cgl_err != kCGLNoError )
            {
                LL_INFOS("GLInit") << "Multi-threaded OpenGL not available." << LL_ENDL;
                sUseMultGL = false;
            }
            else
            {
                LL_INFOS("GLInit") << "Multi-threaded OpenGL enabled." << LL_ENDL;
            }
        }
        else if (was_enabled)
        {
            CGLDisable( ctx, kCGLCEMPEngine);
            LL_INFOS("GLInit") << "Multi-threaded OpenGL disabled." << LL_ENDL;
        }
    }
}
#endif

