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

#include <set>

#include "SDL3/SDL.h"
#include "SDL3/SDL_endian.h"

#if LL_WINDOWS
#include "llwin32headers.h" // HWND / WNDPROC for the WM_COPYDATA subclass
#endif

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
    // MouseWarpMode == 0 (the default, "trust the backend"). Same semantics
    // as LLWindowWin32::isWrapMouse: false when the active pointing device
    // is absolute-positioned (touchscreen, stylus) so we don't try to warp
    // a finger; true for ordinary mice. The "true" case is also the path
    // that would have warped pre-relative-mode — relative mode covers it
    // now, so isWrapMouse no longer drives camera-control warping, but the
    // touch/pen branch still needs to disable warp.
    bool isWrapMouse() const override { return !mAbsoluteCursorPosition; }

    // On Linux/X11 and Wayland AltGr is delivered as Right-Alt alone (no
    // Ctrl prefix). LLKeyboardSDL folds RALT into MASK_ALT, so a chat-bar
    // user typing AltGr+E (€ on German layout) would otherwise see the
    // in-world "Alt+E" binding fire alongside the € insert. We detect the
    // RAlt-without-LAlt-and-without-Ctrl signature here so handleKey can
    // short-circuit before the binding dispatch — mirrors the AltGr block
    // in LLWindowWin32 / LLViewerWindow::handleKey.
    bool isAltGrPressed() const override
    {
        return (mKeyModifiers & SDL_KMOD_RALT)
            && !(mKeyModifiers & SDL_KMOD_LALT)
            && !(mKeyModifiers & SDL_KMOD_CTRL);
    }

    // Pen / stylus and touchscreen metadata sourced from SDL_EVENT_PEN_AXIS
    // (pressure, tilt) and SDL_EVENT_PEN_PROXIMITY_IN/OUT (active state +
    // eraser-tip flag). For touch input, mPenPressure carries the contact
    // pressure of the most recent SDL_EVENT_FINGER_* sample if the device
    // reports one. Mouse events keep their defaults (pressure=1, no tilt).
    F32 getPointerPressure() const override { return mPenInProximity ? mPenPressure : 1.f; }
    F32 getPointerTiltX() const override { return mPenTiltX; }
    F32 getPointerTiltY() const override { return mPenTiltY; }
    bool isPointerEraserTip() const override { return mPenEraserTip; }
    bool isPointerPenActive() const override { return mPenInProximity; }
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

    // "wayland" / "x11" / "" - reports the SDL video driver this window is on.
    std::string getDisplayServer() const override;

    void bringToFront() override;

    void setLanguageTextInput(const LLCoordGL& pos) override;
    void allowLanguageTextInput(LLPreeditor* preeditor, bool b) override;

    void spawnWebBrowser(const std::string &escaped_url, bool async) override;

    void setTitle(const std::string title) override;

    static std::vector<std::string> getDynamicFallbackFontList();
    static LLFontFallbackMatch findFallbackFontForChar(llwchar wch);

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

    // Bracket an async OS dialog. SDL3's file picker resolves on a later frame,
    // so beforeDialog()/afterDialog() can't wrap it on a single stack frame;
    // enterDialog()/exitDialog() drive the same mDialogDepth machinery from the
    // launch site and the completion callback instead. exitDialog() also restores
    // key-window focus, which SDL leaves dropped after its dialog sheet closes.
    static void enterDialog();
    static void exitDialog();

    // True while any OS dialog is up. The frame loop services modeless pickers,
    // so it must skip BackgroundYieldTime while one is open or it turns laggy.
    static bool dialogOpen();

#if LL_DARWIN
    static U64 getVramSize();
    static void setUseMultGL(bool use_mult_gl);

    static bool sUseMultGL;
#endif

#if LL_WINDOWS
    // DirectInput8 access for llviewerjoystick / SpaceNavigator — same
    // contract as LLWindowWin32 (DI8 needs only the module handle, not the
    // window, so the SDL backend can provide it too).
    void* getDirectInput8() override;
    bool getInputDevices(U32 device_type_filter,
                         std::function<bool(std::string&, LLSD&, void*)> osx_callback,
                         void* di8_devices_callback,
                         void* userdata) override;

    // Forwards WM_COPYDATA payloads (SLURL hand-off from a second viewer
    // instance) from the subclassed WndProc to the window callbacks.
    void handleDataCopy(S32 data_type, void* data);
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

    // Recompute mMinWindowWidthPx/HeightPx from the base-class screen-coord
    // minimum scaled by the current SDL_GetWindowPixelDensity. Called at the
    // end of createContext (after mWindow exists), from setMinSize, and from
    // the display/scale-change event handlers when the active monitor's
    // density might have changed under us.
    void refreshMinSizePixelShadow();

    // Re-cache the window pixel density + pixel height that the per-event input
    // handlers and convertCoords read, so they don't each hit
    // SDL_GetWindowPixelDensity / SDL_GetWindowSizeInPixels (-> GetClientRect, a
    // USER32 syscall) on every mouse event. Run wherever the window's pixel size
    // or density can change (resize / DPI / monitor).
    void refreshPixelMetrics();

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
    // Platform-dependent scancode (SDL_KeyboardEvent.raw) of the last key event.
    // Forwarded through getNativeKeyData() to the CEF media plugin, which needs
    // it as CefKeyEvent.native_key_code for key events to reach the page.
    U32 mKeyRawScanCode = 0;

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
    // Pixel-unit shadow of the screen-coord minimum size stored in the base
    // class. LLWindow::setSize(LLCoordWindow) clamps against
    // mMinWindowWidth/Height as a U32, but LLCoordWindow is in pixels on
    // this backend while the base stores screen-coord (logical) units —
    // direct comparison under-clamps on HiDPI. setSizeImpl(LLCoordWindow)
    // re-clamps against this pixel-unit copy to make the dimensions match.
    U32 mMinWindowWidthPx = 0;
    U32 mMinWindowHeightPx = 0;

    // Cached pixel metrics; see refreshPixelMetrics(). Density is pre-clamped to
    // > 0. Height starts at 0 — convertCoords falls back to a live query until
    // the first refresh (end of createContext).
    F32 mCachedPixelDensity = 1.f;
    S32 mCachedWindowHeightPx = 0;

    F32 mMouseDeltaAccumX = 0.f;
    F32 mMouseDeltaAccumY = 0.f;

    // Count of synthetic SDL_EVENT_MOUSE_MOTION events queued by recent
    // non-relative-mode SDL_WarpMouseInWindow calls. Each warp that moves
    // the cursor produces one synthetic event with xrel/yrel set to the
    // warp distance — feeding that into the camera signal would partially
    // cancel real user motion (visible during alt-cam recenter every
    // frame). Incremented in setCursorPosition / showCursor wherever we
    // warp, decremented by the MOUSE_MOTION handler which drops the
    // synthetic event before it reaches the accumulator. Win32 raw input
    // doesn't have this problem; SDL3 emits the synthetic event by API
    // contract in non-relative mode.
    S32 mPendingWarpSuppressCount = 0;

    // Per-frame scroll-wheel accumulator. SDL_EVENT_MOUSE_WHEEL delivers
    // float-precision deltas (event.wheel.x/y) that we sum here, then
    // emit integer "clicks" through handleScrollWheel/HWheel each time the
    // accumulator crosses ±1. Lets smooth-scroll devices (touchpads,
    // high-resolution wheels) eventually integrate into scroll events
    // instead of having every sub-tick dropped by the integer_x/integer_y
    // truncation the legacy code used.
    F32 mScrollWheelAccumX = 0.f;
    F32 mScrollWheelAccumY = 0.f;

    // Mirrors LLWindowWin32::mAbsoluteCursorPosition. Set true while the
    // most recent mouse motion/button event was synthesised from a touch
    // screen (event.motion.which == SDL_TOUCH_MOUSEID) or a pen / stylus
    // (SDL_PEN_MOUSEID). Drives isWrapMouse() so MouseWarpMode=0 skips
    // the warp path for absolute-positioned devices, and also suppresses
    // relative-mouse-mode entry in hideCursor() since SDL3 doesn't deliver
    // relative deltas for finger drags — relative mode there would freeze
    // mouselook input entirely.
    bool mAbsoluteCursorPosition = false;

    // Pen / stylus state cached from the native SDL3 pen events. SDL3
    // continues to deliver mouse-emulation events alongside these (we
    // don't disable SDL_HINT_PEN_MOUSE_EVENTS), so the viewer's existing
    // input plumbing keeps working unchanged; this state is purely for
    // tools that opt into pressure/tilt/eraser data via the LLWindow
    // virtual getters.
    bool mPenInProximity = false;
    bool mPenEraserTip = false;
    F32 mPenPressure = 1.f;
    F32 mPenTiltX = 0.f;
    F32 mPenTiltY = 0.f;

    // True while SDL_SetWindowRelativeMouseMode is active. We enter relative
    // mode on hideCursor() (the permanent-hide path used by mouselook /
    // camera-grab tools) and leave on showCursor(). In this mode the OS
    // cursor is parked and SDL delivers hardware-level relative motion via
    // SDL_EVENT_MOUSE_MOTION xrel/yrel; the absolute motion.x/y is undefined
    // and we don't forward it to the viewer. The legacy "warp cursor to
    // window center every frame and read the position delta" path is gone.
    bool mRelativeMouseMode = false;

    // Snapshot of mRelativeMouseMode at the OUTERMOST beforeDialog() entry.
    // We drop relative mode while a modal dialog is up (otherwise the user
    // can't see the cursor to click it) and use this to decide whether to
    // re-enter pointer-lock when the outermost afterDialog() balances out.
    // Nested dialogs (e.g. a device-loss notification surfacing while a
    // file picker is open) are gated by mDialogDepth so only the outermost
    // pair saves/restores state.
    bool mDialogSavedRelativeMode = false;
    S32 mDialogDepth = 0;

    // The exit position the viewer asked for via setCursorPosition() while
    // we were in relative mode. SDL3 keeps the cursor parked during relative
    // mode, so an in-flight warp can't be honoured immediately; we cache the
    // requested position and apply it as one real warp on showCursor(),
    // right before the OS cursor becomes visible again. Without this,
    // alt-cam handleMouseUp's "put the cursor back at the click point"
    // call would be silently dropped, leaving the cursor at wherever SDL
    // parked it. Mouselook deselect doesn't set this, so showCursor()
    // defaults to window center in that case.
    LLCoordWindow mDeferredCursorWarp;
    bool mHasDeferredCursorWarp = false;

    // Shared GL contexts for worker threads (texture upload, VBO streaming).
    //
    // Worker threads ask for a shared GL context via createSharedContext() (on
    // the main thread), bind it via makeContextCurrent() and release it with
    // destroySharedContext() (both on the worker thread) so they can stream GL
    // objects without interrupting the main render thread.
    //
    // Rather than SDL3's "one hidden carrier SDL_Window per context" pattern
    // (which forces a main-thread-only deferred-window-destruction dance), we
    // create the contexts with the platform-native GL API behind SDL:
    //   * Windows  — WGL sibling context on the main window's HDC
    //   * macOS    — CGL context sharing the current CGLContextObj (drawable-less)
    //   * X11      — GLX context bound to a 1x1 GLXPbuffer (offscreen, no WM)
    //   * Wayland  — EGL context made current surfaceless (EGL_NO_SURFACE)
    // Each returns an opaque heap handle (LLSDLSharedContext, defined in the
    // .cpp). mSharedContexts tracks the live handles ONLY so destroyContext can
    // warn about and reclaim any a worker failed to release; it is touched only
    // under mSharedCtxMutex.
    LLMutex mSharedCtxMutex;
    std::set<void*> mSharedContexts;

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

#if LL_WINDOWS
    // Install/remove a WndProc subclass on the SDL window's HWND so we can
    // service WM_COPYDATA (SLURL hand-off from a second instance) — SDL3's
    // message hook never sees cross-process SendMessage. Paired around the
    // SDL window lifetime in createContext/destroyContext.
    void installWin32Subclass();
    void removeWin32Subclass();
    static LRESULT CALLBACK win32WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND mWin32Hwnd = nullptr;
    WNDPROC mPrevWndProc = nullptr;
#endif
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

private:
    // Redraw the splash (background + current status text) and pump events so
    // it paints while the main thread is busy loading.
    void render();

    SDL_Window*   mWindow = nullptr;
    SDL_Renderer* mRenderer = nullptr;
    void*         mFont = nullptr;   // TTF_Font* (kept opaque to spare the header SDL_ttf)
    SDL_Texture*  mIcon = nullptr;   // branded app icon, left side
    std::string   mMessage;
    bool          mInitedVideo = false;
    bool          mInitedTTF = false;
};

S32 OSMessageBoxSDL(const std::string& text, const std::string& caption, U32 type);

#endif //LL_LLWINDOWSDL_H
