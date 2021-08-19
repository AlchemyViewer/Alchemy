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
#include "llpreeditor.h"

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED 1
#endif
#include <SDL.h>
#include <SDL_endian.h>
#include <SDL_video.h>

#if LL_X11
// get X11-specific headers for use in low-level stuff like copy-and-paste support
#include <SDL_syswm.h>
#endif

// AssertMacros.h does bad things.
#include "fix_macros.h"
#undef verify
#undef require


class LLWindowSDL final : public LLWindow
{
public:
	/*virtual*/ void show() override;
	/*virtual*/ void hide() override;
	/*virtual*/ void close() override;
	/*virtual*/ BOOL getVisible() override;
	/*virtual*/ BOOL getMinimized() override;
	/*virtual*/ BOOL getMaximized() override;
	/*virtual*/ BOOL maximize() override;
	/*virtual*/ void minimize() override;
	/*virtual*/ void restore() override;
	BOOL getFullscreen();
	/*virtual*/ BOOL getPosition(LLCoordScreen *position) override;
	/*virtual*/ BOOL getSize(LLCoordScreen *size) override;
	/*virtual*/ BOOL getSize(LLCoordWindow *size) override;
	/*virtual*/ BOOL setPosition(LLCoordScreen position) override;
	/*virtual*/ BOOL setSizeImpl(LLCoordScreen size) override;
	/*virtual*/ BOOL setSizeImpl(LLCoordWindow size) override;
	/*virtual*/ BOOL switchContext(BOOL fullscreen, const LLCoordScreen &size, BOOL disable_vsync, const LLCoordScreen * const posp = NULL) override;
	/*virtual*/ BOOL setCursorPosition(LLCoordWindow position) override;
	/*virtual*/ BOOL getCursorPosition(LLCoordWindow *position) override;
	/*virtual*/ void showCursor() override;
	/*virtual*/ void hideCursor() override;
	/*virtual*/ void showCursorFromMouseMove() override;
	/*virtual*/ void hideCursorUntilMouseMove() override;
	/*virtual*/ BOOL isCursorHidden() override;
	/*virtual*/ void updateCursor() override;
	/*virtual*/ void captureMouse() override;
	/*virtual*/ void releaseMouse() override;
	/*virtual*/ void setMouseClipping( BOOL b ) override;
	/*virtual*/	void setMinSize(U32 min_width, U32 min_height, bool enforce_immediately = true) override;

	/*virtual*/ BOOL isClipboardTextAvailable() override;
	/*virtual*/ BOOL pasteTextFromClipboard(LLWString &dst) override;
	/*virtual*/ BOOL copyTextToClipboard(const LLWString & src) override;

	/*virtual*/ BOOL isPrimaryTextAvailable() override;
	/*virtual*/ BOOL pasteTextFromPrimary(LLWString &dst) override;
	/*virtual*/ BOOL copyTextToPrimary(const LLWString & src) override;
	/*virtual*/ void setWindowTitle(const std::string& title) override;
	/*virtual*/ void flashIcon(F32 seconds) override;
	/*virtual*/ F32 getGamma() override;
	/*virtual*/ BOOL setGamma(const F32 gamma) override; // Set the gamma
	/*virtual*/ U32 getFSAASamples() override;
	/*virtual*/ void setFSAASamples(const U32 samples) override;
	/*virtual*/ BOOL restoreGamma() override;			// Restore original gamma table (before updating gamma)
	/*virtual*/ ESwapMethod getSwapMethod() override { return mSwapMethod; }
	/*virtual*/ void processMiscNativeEvents() override;
	/*virtual*/ void gatherInput() override;
	/*virtual*/ void swapBuffers() override;

	/*virtual*/ void delayInputProcessing() override { };

	// handy coordinate space conversion routines
	/*virtual*/ BOOL convertCoords(LLCoordScreen from, LLCoordWindow *to) override;
	/*virtual*/ BOOL convertCoords(LLCoordWindow from, LLCoordScreen *to) override;
	/*virtual*/ BOOL convertCoords(LLCoordWindow from, LLCoordGL *to) override;
	/*virtual*/ BOOL convertCoords(LLCoordGL from, LLCoordWindow *to) override;
	/*virtual*/ BOOL convertCoords(LLCoordScreen from, LLCoordGL *to) override;
	/*virtual*/ BOOL convertCoords(LLCoordGL from, LLCoordScreen *to) override;

	/*virtual*/ LLWindowResolution* getSupportedResolutions(S32 &num_resolutions) override;
	/*virtual*/ F32	getNativeAspectRatio() override;
	/*virtual*/ F32 getPixelAspectRatio() override;
	/*virtual*/ void setNativeAspectRatio(F32 ratio) override { mOverrideAspectRatio = ratio; }

	/*virtual*/ void beforeDialog() override;
	/*virtual*/ void afterDialog() override;

	/*virtual*/ BOOL dialogColorPicker(F32 *r, F32 *g, F32 *b) override;

	/*virtual*/ void *getPlatformWindow() override;
	/*virtual*/ void bringToFront() override;

	void allowLanguageTextInput(LLPreeditor *preeditor, BOOL b) override;
	void updateLanguageTextInputArea() override;
	void setLanguageTextInput( const LLCoordGL & pos ) override;

	void spawnWebBrowser(const std::string& escaped_url, bool async) override;
	
	static std::vector<std::string> getDynamicFallbackFontList();

	// Not great that these are public, but they have to be accessible
	// by non-class code and it's better than making them global.
#if LL_X11
	Window mSDL_XWindowID;
	Display *mSDL_Display;
#endif

#if LL_GTK
	// Lazily initialize and check the runtime GTK version for goodness.
	static bool ll_try_gtk_init(void);
#endif // LL_GTK

#if LL_X11
	static Window get_SDL_XWindowID(void);
	static Display* get_SDL_Display(void);
#endif // LL_X11	

protected:
	LLWindowSDL(LLWindowCallbacks* callbacks,
		const std::string& title, int x, int y, int width, int height, U32 flags,
		BOOL fullscreen, BOOL clearBg, BOOL disable_vsync, BOOL use_gl,
		BOOL ignore_pixel_depth, U32 fsaa_samples);
	~LLWindowSDL();

	/*virtual*/ BOOL	isValid() override;
	/*virtual*/ LLSD    getNativeKeyData() override;

	void	initCursors();
	void	quitCursors();

	// Changes display resolution. Returns true if successful
	BOOL	setDisplayResolution(S32 width, S32 height, S32 bits, S32 refresh);

	// Go back to last fullscreen display resolution.
	BOOL	setFullscreenResolution();

	BOOL	shouldPostQuit() { return mPostQuit; }

protected:
	//
	// Platform specific methods
	//

	// create or re-create the GL context/window.  Called from the constructor and switchContext().
	BOOL createContext(int x, int y, int width, int height, int bits, BOOL fullscreen, BOOL disable_vsync);
	void destroyContext();
	void setupFailure(const std::string& text, const std::string& caption, U32 type);
	U32 SDLCheckGrabbyKeys(SDL_Keycode keysym, BOOL gain);
	BOOL SDLReallyCaptureInput(BOOL capture);

	//
	// Platform specific variables
	//
	U32             mGrabbyKeyFlags;
	int				mReallyCapturedCount;
	SDL_Window*		mWindow;
	SDL_GLContext   mGLContext;
	std::string		mWindowName;
	std::string 	mWindowTitle;
	double			mOriginalAspectRatio;
	BOOL			mNeedsResize;		// Constructor figured out the window is too big, it needs a resize.
	LLCoordScreen   mNeedsResizeSize;
	F32				mOverrideAspectRatio;
	F32				mGamma;
	U32				mFSAASamples;

	int				mSDLFlags;

	SDL_Cursor*		mSDLCursors[UI_CURSOR_COUNT];

	friend class LLWindowManager;

private:
	BOOL mFlashing;
	LLTimer mFlashTimer;
	
	U32 mKeyScanCode;
	U32 mKeyVirtualKey;
	SDL_Keymod mKeyModifiers;

	bool			mLanguageTextInputAllowed;
	LLPreeditor*	mPreeditor;
};


class LLSplashScreenSDL : public LLSplashScreen
{
public:
	LLSplashScreenSDL();
	virtual ~LLSplashScreenSDL();

	/*virtual*/ void showImpl() override;
	/*virtual*/ void updateImpl(const std::string& mesg) override;
	/*virtual*/ void hideImpl() override;
};

S32 OSMessageBoxSDL(const std::string& text, const std::string& caption, U32 type);

#endif //LL_LLWINDOWSDL_H
