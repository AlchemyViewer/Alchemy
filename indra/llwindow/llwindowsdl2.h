/**
 * @file llwindowsdl2.h
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

 // Simple Directmedia Layer 2.0 (http://libsdl.org/) implementation of LLWindow class

#include "llwindow.h"

#include "llwin32headers.h"

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED 1
#endif

#include "SDL2/SDL.h"

class LLWindowSDL2 : public LLWindow
{
public:
	/*virtual*/ void show();
	/*virtual*/ void hide();
	/*virtual*/ void close();
	/*virtual*/ BOOL getVisible();
	/*virtual*/ BOOL getMinimized();
	/*virtual*/ BOOL getMaximized();
	/*virtual*/ BOOL maximize();
	/*virtual*/ void minimize();
	/*virtual*/ void restore();
	/*virtual*/ BOOL getFullscreen();
	/*virtual*/ BOOL getPosition(LLCoordScreen *position);
	/*virtual*/ BOOL getSize(LLCoordScreen *size);
	/*virtual*/ BOOL getSize(LLCoordWindow *size);
	/*virtual*/ BOOL setPosition(LLCoordScreen position);
	/*virtual*/ BOOL setSizeImpl(LLCoordScreen size);
	/*virtual*/ BOOL setSizeImpl(LLCoordWindow size);
	/*virtual*/ BOOL switchContext(U32 window_mode, const LLCoordScreen &size, U32 vsync_setting, const LLCoordScreen * const posp = NULL) override;
	/*virtual*/ BOOL setCursorPosition(LLCoordWindow position);
	/*virtual*/ BOOL getCursorPosition(LLCoordWindow *position);
	/*virtual*/ void showCursor();
	/*virtual*/ void hideCursor();
	/*virtual*/ void showCursorFromMouseMove();
	/*virtual*/ void hideCursorUntilMouseMove();
	/*virtual*/ BOOL isCursorHidden();
	/*virtual*/ void updateCursor();
	/*virtual*/ void captureMouse();
	/*virtual*/ void releaseMouse();
	/*virtual*/ void setMouseClipping(BOOL b);
	/*virtual*/	void setMinSize(U32 min_width, U32 min_height, bool enforce_immediately = true);

	/*virtual*/ BOOL isClipboardTextAvailable();
	/*virtual*/ BOOL pasteTextFromClipboard(LLWString &dst);
	/*virtual*/ BOOL copyTextToClipboard(const LLWString & src);

	/*virtual*/ void setWindowTitle(const std::string& title);
	/*virtual*/ void flashIcon(F32 seconds);
	/*virtual*/ F32 getGamma();
	/*virtual*/ BOOL setGamma(const F32 gamma); // Set the gamma
	/*virtual*/ U32 getFSAASamples();
	/*virtual*/ void setFSAASamples(const U32 samples);
	/*virtual*/ BOOL restoreGamma();			// Restore original gamma table (before updating gamma)
	/*virtual*/ ESwapMethod getSwapMethod() { return mSwapMethod; }
	/*virtual*/ void processMiscNativeEvents();
	/*virtual*/ void gatherInput();
	/*virtual*/ void swapBuffers();
	/*virtual*/ void restoreGLContext() {};

	/*virtual*/ void delayInputProcessing() { };

	// handy coordinate space conversion routines
	/*virtual*/ BOOL convertCoords(LLCoordScreen from, LLCoordWindow *to);
	/*virtual*/ BOOL convertCoords(LLCoordWindow from, LLCoordScreen *to);
	/*virtual*/ BOOL convertCoords(LLCoordWindow from, LLCoordGL *to);
	/*virtual*/ BOOL convertCoords(LLCoordGL from, LLCoordWindow *to);
	/*virtual*/ BOOL convertCoords(LLCoordScreen from, LLCoordGL *to);
	/*virtual*/ BOOL convertCoords(LLCoordGL from, LLCoordScreen *to);

	/*virtual*/ LLWindowResolution* getSupportedResolutions(S32 &num_resolutions);
	/*virtual*/ F32	getNativeAspectRatio();
	/*virtual*/ F32 getPixelAspectRatio();
	/*virtual*/ void setNativeAspectRatio(F32 ratio) { mOverrideAspectRatio = ratio; }

	/*virtual*/ void beforeDialog();
	/*virtual*/ void afterDialog();

	/*virtual*/ BOOL dialogColorPicker(F32 *r, F32 *g, F32 *b);

	/*virtual*/ void *getPlatformWindow();
	/*virtual*/ void bringToFront();

	/*virtual*/ void spawnWebBrowser(const std::string& escaped_url, bool async);

	static std::vector<std::string> getDynamicFallbackFontList();

	SDL_Window* getSDLWindow()
	{
		return mWindow;
	}

protected:
	LLWindowSDL2(LLWindowCallbacks* callbacks,
		const std::string& title, const std::string& name, int x, int y, int width, int height, U32 flags,
		U32 window_mode, BOOL clearBg, U32 vsync_setting, BOOL use_gl,
		BOOL ignore_pixel_depth, U32 fsaa_samples);
	~LLWindowSDL2();

	/*virtual*/ BOOL	isValid();
	/*virtual*/ LLSD    getNativeKeyData();

	void	initCursors();
	void	quitCursors();

	BOOL	shouldPostQuit() { return mPostQuit; }

protected:
	//
	// Platform specific methods
	//

	// create or re-create the GL context/window.  Called from the constructor and switchContext().
	BOOL createContext(int x, int y, int width, int height, int bits, U32 window_mode, U32 vsync_setting);
	void destroyContext();
	void setupFailure(const std::string& text, const std::string& caption, U32 type);
	U32 SDLCheckGrabbyKeys(SDL_Keycode keysym, BOOL gain);
	BOOL SDLReallyCaptureInput(BOOL capture);
#if LL_WINDOWS
	void setIconWindows();
#endif

	//
	// Platform specific variables
	//
	U32             mGrabbyKeyFlags;
	int				mReallyCapturedCount;
	SDL_Window*		mWindow;
	SDL_GLContext   mGLContext;
	std::string		mWindowName;
	std::string		mWindowTitle;
	double			mOriginalAspectRatio;
	BOOL			mNeedsResize;		// Constructor figured out the window is too big, it needs a resize.
	LLCoordScreen   mNeedsResizeSize;
	F32				mOverrideAspectRatio;
	F32				mGamma;
	U32				mFSAASamples;

	SDL_Cursor*		mSDLCursors[UI_CURSOR_COUNT];

	friend class LLWindowManager;

#if LL_WINDOWS
	HICON mWinWindowIcon;
#endif

private:
	U32 mKeyScanCode;
	U32 mKeyVirtualKey;
	SDL_Keycode mKeyModifiers;
#if LL_WINDOWS
	U32				mRawMsg;
	U32				mRawWParam;
	U32				mRawLParam;
#endif
};


class LLSplashScreenSDL2 : public LLSplashScreen
{
public:
	LLSplashScreenSDL2();
	virtual ~LLSplashScreenSDL2();

	/*virtual*/ void showImpl();
	/*virtual*/ void updateImpl(const std::string& mesg);
	/*virtual*/ void hideImpl();
private:
#if LL_WINDOWS
		HWND mWindow;
#endif
};

S32 OSMessageBoxSDL2(const std::string& text, const std::string& caption, U32 type);

#endif //LL_LLWINDOWSDL_H
