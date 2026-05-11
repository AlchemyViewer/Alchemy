/**
 * @file llwindowsdl.h
 * @brief SDL implementation of LLWindow class
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

#ifndef LL_LLWINDOWSDL_H
#define LL_LLWINDOWSDL_H

// Simple Directmedia Layer (http://libsdl.org/) implementation of LLWindow class

#include "llwindow.h"
#include "lltimer.h"
#include "llmutex.h"

#include "SDL3/SDL.h"
#include "SDL3/SDL_endian.h"

#ifdef LL_WAYLAND
#include <wayland-client-protocol.h>
#endif

#if LL_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

class LLPreeditor;

class LLWindowSDL final : public LLWindow
{
public:
    void show() override;
    void hide() override;
    void restore() override;

    void close() override;

    bool getVisible() override;

    bool getMinimized() override;

    bool getMaximized() override;

    bool maximize() override;
    void minimize() override;

    bool getPosition(LLCoordScreen *position) override;

    bool getSize(LLCoordScreen *size) override;
    bool getSize(LLCoordWindow *size) override;

    bool setPosition(LLCoordScreen position) override;

    bool setSizeImpl(LLCoordScreen size) override;
    bool setSizeImpl(LLCoordWindow size) override;

    bool switchContext(bool fullscreen, const LLCoordScreen &size, bool enable_vsync,
                                   const LLCoordScreen *const posp = NULL) override;

    bool setCursorPosition(LLCoordWindow position) override;

    bool getCursorPosition(LLCoordWindow *position) override;
    bool getCursorDelta(LLCoordCommon* delta) override;
    // Whether LLViewerWindow::moveCursorToCenter should warp the cursor when
    // MouseWarpMode == 0 (the default, "trust the backend"). SDL3 says no:
    // when the viewer wants pointer-lock for camera control it hides the
    // cursor, which we translate to SDL_SetWindowRelativeMouseMode where
    // motion arrives as hardware deltas. There is no warp-recenter loop on
    // this backend any more. (See hideCursor / setCursorPosition.)
    bool isWrapMouse() const override { return false; }
    void showCursor() override;
    void hideCursor() override;
    bool isCursorHidden() override;

    void showCursorFromMouseMove() override;
    void hideCursorUntilMouseMove() override;

    void updateCursor() override;

    void captureMouse() override;
    void releaseMouse() override;

    void setMouseClipping(bool b) override;

    void setMinSize(U32 min_width, U32 min_height, bool enforce_immediately = true) override;

    bool isClipboardTextAvailable() override;
    bool pasteTextFromClipboard(LLWString &dst) override;
    bool copyTextToClipboard(const LLWString &src) override;

    bool isPrimaryTextAvailable() override;
    bool pasteTextFromPrimary(LLWString &dst) override;
    bool copyTextToPrimary(const LLWString &src) override;

    void flashIcon(F32 seconds) override;
    void maybeStopFlashIcon();

    F32 getGamma() override;
    bool setGamma(const F32 gamma) override; // Set the gamma
    bool restoreGamma() override;            // Restore original gamma table (before updating gamma)

    U32 getFSAASamples() override;
    void setFSAASamples(const U32 samples) override;

    void processMiscNativeEvents() override;

    void gatherInput() override;

    SDL_AppResult handleEvent(const SDL_Event& event);
    static SDL_AppResult handleEvents(const SDL_Event& event);

    void swapBuffers() override;

    void delayInputProcessing()  override {};

    // handy coordinate space conversion routines
    bool convertCoords(LLCoordScreen from, LLCoordWindow *to) override;
    bool convertCoords(LLCoordWindow from, LLCoordScreen *to) override;
    bool convertCoords(LLCoordWindow from, LLCoordGL *to) override;
    bool convertCoords(LLCoordGL from, LLCoordWindow *to) override;
    bool convertCoords(LLCoordScreen from, LLCoordGL *to) override;
    bool convertCoords(LLCoordGL from, LLCoordScreen *to) override;

    LLWindowResolution *getSupportedResolutions(S32 &num_resolutions) override;

    F32 getNativeAspectRatio() override;
    F32 getPixelAspectRatio() override;
    void setNativeAspectRatio(F32 ratio)  override { mOverrideAspectRatio = ratio; }

    void beforeDialog() override;
    void afterDialog() override;

    bool dialogColorPicker(F32 *r, F32 *g, F32 *b) override;

    void *getPlatformWindow() override;

    void bringToFront() override;

    void setLanguageTextInput(const LLCoordGL& pos) override;
    void allowLanguageTextInput(LLPreeditor* preeditor, bool b) override;

    void spawnWebBrowser(const std::string &escaped_url, bool async) override;

    void setTitle(const std::string title) override;

    static std::vector<std::string> getDynamicFallbackFontList();

    void *createSharedContext() override;
    void makeContextCurrent(void *context) override;
    void destroySharedContext(void *context) override;
    void toggleVSync(bool enable_vsync) override;

    F32 getSystemUISize() override;

    static std::vector<std::string> getDisplaysResolutionList();

    // Accessor for the main viewer SDL_Window used by the singleton viewer instance.
    // Code outside llwindowsdl (e.g. file pickers) should prefer this over
    // SDL_GL_GetCurrentWindow() so the parent window is correct even when a
    // worker thread has its own GL context current.
    static SDL_Window* getMainSDLWindow();

#if LL_DARWIN
    static U64 getVramSize();
    static void setUseMultGL(bool use_mult_gl);

    static bool sUseMultGL;
#endif

protected:
    LLWindowSDL(LLWindowCallbacks *callbacks,
                const std::string &title, const std::string& name, int x, int y, int width, int height, U32 flags,
                bool fullscreen, bool clearBg, bool enable_vsync, bool use_gl,
                bool ignore_pixel_depth, U32 fsaa_samples);

    ~LLWindowSDL();

    bool isValid() override;

    LLSD getNativeKeyData() override;

    void initCursors();
    void quitCursors();

protected:
    //
    // Platform specific methods
    //

    // create or re-create the GL context/window.  Called from the constructor and switchContext().
    bool createContext(int x, int y, int width, int height, int bits, bool fullscreen, bool enable_vsync);
    void destroyContext();

    void setupFailure(const std::string &text, const std::string &caption, U32 type);

    bool SDLReallyCaptureInput(bool capture);
    U32 SDLCheckGrabbyKeys(U32 keysym, bool gain);

    //
    // Platform specific variables
    //
    U32 mGrabbyKeyFlags = 0;
    S32 mReallyCapturedCount = 0;
    SDL_Window *mWindow = nullptr;
    SDL_GLContext mContext = nullptr;
    SDL_Cursor *mSDLCursors[UI_CURSOR_COUNT];

    std::string mWindowTitle;
    F32 mNativeAspectRatio = 0.0f;
    F32 mOverrideAspectRatio = 0.0f;
    F32 mGamma = 0.0f;
    U32 mFSAASamples = 0;

    friend class LLWindowManager;

private:
    bool mFlashing = false;
    LLTimer mFlashTimer;
    U32 mKeyVirtualKey = 0;
    U32 mKeyModifiers = SDL_KMOD_NONE;

    // Per-frame mouse-motion accumulator. SDL_EVENT_MOUSE_MOTION delivers
    // event.motion.xrel/yrel (relative motion since the last event in
    // screen-coord units); we scale by pixel density and sum into this
    // member so getCursorDelta() — queried once per frame from
    // LLViewerWindow::updateMouseDelta — sees every motion event, not
    // just the last-position-minus-first-position which truncates to zero
    // at high frame rates.
    //
    // Float storage carries sub-pixel residue across queries so fractional
    // motion eventually integrates into integer deltas instead of being
    // rounded away each frame.
    F32 mMouseDeltaAccumX = 0.f;
    F32 mMouseDeltaAccumY = 0.f;

    // True while SDL_SetWindowRelativeMouseMode is active. We enter relative
    // mode on hideCursor() (the permanent-hide path used by mouselook /
    // camera-grab tools) and leave on showCursor(). In this mode the OS
    // cursor is parked and SDL delivers hardware-level relative motion via
    // SDL_EVENT_MOUSE_MOTION xrel/yrel; the absolute motion.x/y is undefined
    // and we don't forward it to the viewer. The legacy "warp cursor to
    // window center every frame and read the position delta" path is gone.
    bool mRelativeMouseMode = false;

    // Off-screen rendering (OSR) shared GL contexts.
    //
    // Worker threads (texture loader, etc.) ask for a shared GL context via
    // createSharedContext() and bind it via makeContextCurrent() so they can
    // upload textures without interrupting the main render thread. SDL3's
    // OSR pattern is one hidden 1x1 SDL_Window per shared GL context.
    //
    // Threading contract:
    //   * mOSRContexts is touched ONLY under mOSRMutex.
    //   * Worker threads MAY call createSharedContext / destroySharedContext /
    //     makeContextCurrent — those acquire the mutex internally.
    //   * SDL_DestroyWindow is main-thread-only on at least X11 (the X11
    //     backend grabs the global SDL video lock and assumes single-threaded
    //     entry), so destroySharedContext does NOT destroy the carrier window
    //     directly; it queues it onto mDeadOSRWindows for the main thread to
    //     reap in processMiscNativeEvents() / destroyContext().
    //   * destroyContext() also walks any contexts still in mOSRContexts at
    //     shutdown — those represent worker threads that didn't release
    //     their context — and queues their windows for destruction on the
    //     same path.
    LLMutex mOSRMutex;
    std::unordered_map<SDL_GLContext, SDL_Window*> mOSRContexts;
    std::list<SDL_Window*> mDeadOSRWindows;

    // Files accumulated between SDL_EVENT_DROP_BEGIN and SDL_EVENT_DROP_COMPLETE.
    // Each SDL_EVENT_DROP_FILE only carries one path, so we batch them and
    // dispatch a single handleDragNDrop on COMPLETE.
    std::vector<std::string> mPendingDropFiles;

    // Currently-focused preeditor receiving SDL_EVENT_TEXT_EDITING composition
    // updates. Set by LLLineEditor/LLTextEditor via allowLanguageTextInput()
    // on focus-in; cleared on focus-out. Null when no widget owns IME.
    //
    // Note: we do NOT toggle SDL_StartTextInput / SDL_StopTextInput in step
    // with mPreeditor. SDL3 ties both committed-text events
    // (SDL_EVENT_TEXT_INPUT) and composition events (SDL_EVENT_TEXT_EDITING)
    // to the same SDL_StartTextInput flag — turning it off when no text
    // widget is focused would also kill the TEXT_INPUT path that
    // LLMenuGL::handleUnicodeCharHere() needs to drive menu jump keys
    // (see llmenugl.cpp:3135-3142). Unlike Win32's IMM, which keeps WM_CHAR
    // delivery on regardless of composition state, there is no way to get
    // just-the-commits from SDL3.
    LLPreeditor* mPreeditor = nullptr;

    void tryFindFullscreenSize(int &aWidth, int &aHeight);

    enum EServerProtocol{ X11, Wayland, Unknown };
    EServerProtocol mServerProtocol = Unknown;
public:
#if LL_X11
    // X11
    struct X11_DATA
    {
        Display* xdisplay = nullptr;
        Window xwindow = 0;
        int xscreen = -1;
    };
    static X11_DATA sX11Data;
#endif

#if LL_WAYLAND
    // Wayland
    struct WAYLAND_DATA
    {
        struct wl_display* display = nullptr;
        struct wl_surface* surface = nullptr;
    };
    static WAYLAND_DATA sWaylandData;
#endif
};

class LLSplashScreenSDL : public LLSplashScreen
{
public:
    LLSplashScreenSDL();
    virtual ~LLSplashScreenSDL();

    void showImpl() override;
    void updateImpl(const std::string& mesg) override;
    void hideImpl() override;
};

S32 OSMessageBoxSDL(const std::string& text, const std::string& caption, U32 type);

#endif //LL_LLWINDOWSDL_H
