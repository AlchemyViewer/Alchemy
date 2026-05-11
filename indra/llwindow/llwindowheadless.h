/**
 * @file llwindowheadless.h
 * @brief Headless definition of LLWindow class
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

#ifndef LL_LLWINDOWHEADLESS_H
#define LL_LLWINDOWHEADLESS_H

#include "llwindow.h"

// LLWindowHeadless is the no-windowing backend. It's used in two scenarios:
//   1. As a fallback when no display server is available (CLI tools that link
//      the viewer but don't render).
//   2. As a test backend for headless CI — drive the public LLWindow API from
//      test code without a real display, and verify the viewer's UI / event
//      plumbing reacts the way it would on a real window.
//
// Most input/output methods track minimal virtual state so the second case
// works: tests can setSize / setCursorPosition / showCursor / hideCursor and
// read back consistent values. Methods that have nothing meaningful to do
// without a display (clipboard, flashIcon, gamma) stay as no-ops.
class LLWindowHeadless : public LLWindow
{
public:
    /*virtual*/ void show() override { mVisible = true; }
    /*virtual*/ void hide() override { mVisible = false; }
    /*virtual*/ void close() override { mVisible = false; }
    /*virtual*/ bool getVisible() override { return mVisible; }
    /*virtual*/ bool getMinimized() override { return mMinimized; }
    /*virtual*/ bool getMaximized() override { return mMaximized; }
    /*virtual*/ bool maximize() override { mMaximized = true; return true; }
    /*virtual*/ void minimize() override { mMinimized = true; }
    /*virtual*/ void restore() override { mMinimized = false; mMaximized = false; }
    // TODO: LLWindow::getFullscreen() is (intentionally?) NOT virtual.
    // Apparently the coder of LLWindowHeadless didn't realize that. Is it a
    // mistake to shadow the base-class method with an LLWindowHeadless
    // override when called on the subclass, yet call the base-class method
    // when indirecting through a polymorphic pointer or reference?
    bool getFullscreen() {return false;}
    /*virtual*/ bool getPosition(LLCoordScreen *position) override
    {
        if (!position) return false;
        *position = mPosition;
        return true;
    }
    /*virtual*/ bool getSize(LLCoordScreen *size) override
    {
        if (!size) return false;
        size->mX = mSize.mX;
        size->mY = mSize.mY;
        return true;
    }
    /*virtual*/ bool getSize(LLCoordWindow *size) override
    {
        if (!size) return false;
        *size = mSize;
        return true;
    }
    /*virtual*/ bool setPosition(LLCoordScreen position) override
    {
        mPosition = position;
        return true;
    }
    /*virtual*/ bool setSizeImpl(LLCoordScreen size) override
    {
        mSize.mX = size.mX;
        mSize.mY = size.mY;
        return true;
    }
    /*virtual*/ bool setSizeImpl(LLCoordWindow size) override
    {
        mSize = size;
        return true;
    }
    /*virtual*/ bool switchContext(bool fullscreen, const LLCoordScreen &size, bool enable_vsync, const LLCoordScreen * const posp = NULL) override
    {
        mSize.mX = size.mX;
        mSize.mY = size.mY;
        if (posp) { mPosition = *posp; }
        return true;
    }
    void* createSharedContext() override  { return nullptr; }
    void makeContextCurrent(void*) override  {}
    void destroySharedContext(void*) override  {}
    /*virtual*/ void toggleVSync(bool enable_vsync) override { }
    /*virtual*/ bool setCursorPosition(LLCoordWindow position) override
    {
        // Tests calling setCursorPosition expect getCursorPosition to read
        // back the same value, and getCursorDelta to reflect the move.
        // Mirrors the real backends: setCursorPosition is also the "warp"
        // path, which would generate a delta on a real OS.
        mCursorDelta.mX += position.mX - mCursorPos.mX;
        mCursorDelta.mY += position.mY - mCursorPos.mY;
        mCursorPos = position;
        return true;
    }
    /*virtual*/ bool getCursorPosition(LLCoordWindow *position) override
    {
        if (!position) return false;
        *position = mCursorPos;
        return true;
    }
#if (LL_WINDOWS || LL_SDL_WINDOW) && !LL_MESA_HEADLESS
    /*virtual*/ bool getCursorDelta(LLCoordCommon* delta) override
    {
        if (!delta) return false;
        *delta = mCursorDelta;
        // Drain the accumulator on read — matches the contract of the
        // Win32 and SDL3 backends' getCursorDelta.
        mCursorDelta.mX = 0;
        mCursorDelta.mY = 0;
        return true;
    }
#endif
    /*virtual*/ bool isWrapMouse() const override { return false; }
    /*virtual*/ void showCursor() override { mCursorHidden = false; }
    /*virtual*/ void hideCursor() override { mCursorHidden = true; }
    /*virtual*/ void showCursorFromMouseMove() override {}
    /*virtual*/ void hideCursorUntilMouseMove() override {}
    /*virtual*/ bool isCursorHidden() override { return mCursorHidden; }
    /*virtual*/ void updateCursor() override {}
    //virtual ECursorType getCursor() override { return mCurrentCursor; }
    /*virtual*/ void captureMouse() override {}
    /*virtual*/ void releaseMouse() override {}
    /*virtual*/ void setMouseClipping( bool b ) override {}
    /*virtual*/ bool isClipboardTextAvailable() override {return false; }
    /*virtual*/ bool pasteTextFromClipboard(LLWString &dst) override {return false; }
    /*virtual*/ bool copyTextToClipboard(const LLWString &src) override {return false; }
    /*virtual*/ void flashIcon(F32 seconds) override {}
    /*virtual*/ F32 getGamma() override {return 1.0f; }
    /*virtual*/ bool setGamma(const F32 gamma) override {return false; } // Set the gamma
    /*virtual*/ void setFSAASamples(const U32 fsaa_samples) override { mFSAASamples = fsaa_samples; }
    /*virtual*/ U32 getFSAASamples() override { return mFSAASamples; }
    /*virtual*/ bool restoreGamma() override {return false; }   // Restore original gamma table (before updating gamma)
    //virtual ESwapMethod getSwapMethod() override { return mSwapMethod; }
    /*virtual*/ void gatherInput() override {}
    /*virtual*/ void delayInputProcessing() override {}
    /*virtual*/ void swapBuffers() override;


    // Coordinate-space conversion. The headless backend treats screen and
    // window coordinates as the same space (no window origin offset — the
    // virtual "window" fills the virtual "screen"). Window↔GL is the
    // standard Y-flip on a [0, height-1] range, matching what the real
    // Win32 and SDL3 backends do.
    /*virtual*/ bool convertCoords(LLCoordScreen from, LLCoordWindow *to) override
    {
        if (!to) return false;
        to->mX = from.mX;
        to->mY = from.mY;
        return true;
    }
    /*virtual*/ bool convertCoords(LLCoordWindow from, LLCoordScreen *to) override
    {
        if (!to) return false;
        to->mX = from.mX;
        to->mY = from.mY;
        return true;
    }
    /*virtual*/ bool convertCoords(LLCoordWindow from, LLCoordGL *to) override
    {
        if (!to) return false;
        to->mX = from.mX;
        to->mY = mSize.mY - from.mY - 1;
        return true;
    }
    /*virtual*/ bool convertCoords(LLCoordGL from, LLCoordWindow *to) override
    {
        if (!to) return false;
        to->mX = from.mX;
        to->mY = mSize.mY - from.mY - 1;
        return true;
    }
    /*virtual*/ bool convertCoords(LLCoordScreen from, LLCoordGL *to) override
    {
        if (!to) return false;
        to->mX = from.mX;
        to->mY = mSize.mY - from.mY - 1;
        return true;
    }
    /*virtual*/ bool convertCoords(LLCoordGL from, LLCoordScreen *to) override
    {
        if (!to) return false;
        to->mX = from.mX;
        to->mY = mSize.mY - from.mY - 1;
        return true;
    }

    /*virtual*/ LLWindowResolution* getSupportedResolutions(S32 &num_resolutions) override { return NULL; }
    /*virtual*/ F32 getNativeAspectRatio() override
    {
        return mSize.mY > 0 ? (F32)mSize.mX / (F32)mSize.mY : 1.0f;
    }
    /*virtual*/ F32 getPixelAspectRatio() override { return 1.0f; }
    /*virtual*/ void setNativeAspectRatio(F32 ratio) override {}

    /*virtual*/ void *getPlatformWindow() override { return 0; }
    /*virtual*/ void bringToFront() override {}

    LLWindowHeadless(LLWindowCallbacks* callbacks,
        const std::string& title, const std::string& name,
        S32 x, S32 y,
        S32 width, S32 height,
        U32 flags,  bool fullscreen, bool clear_background,
        bool enable_vsync, bool use_gl, bool ignore_pixel_depth);
    virtual ~LLWindowHeadless();

private:
    // Tracked virtual window state. Initialised from the constructor args
    // (x/y/width/height) so tests can configure the virtual display through
    // the normal LLWindowManager::createWindow path.
    LLCoordScreen mPosition { 0, 0 };
    LLCoordWindow mSize { 1024, 768 };
    LLCoordWindow mCursorPos { 0, 0 };
    LLCoordCommon mCursorDelta { 0, 0 };
    bool mVisible = true;
    bool mMinimized = false;
    bool mMaximized = false;
    bool mCursorHidden = false;
    U32 mFSAASamples = 0;
};

class LLSplashScreenHeadless : public LLSplashScreen
{
public:
    LLSplashScreenHeadless() {}
    virtual ~LLSplashScreenHeadless() {}

    /*virtual*/ void showImpl() override {}
    /*virtual*/ void updateImpl(const std::string& mesg) override {}
    /*virtual*/ void hideImpl() override {}

};

#endif //LL_LLWINDOWHEADLESS_H

