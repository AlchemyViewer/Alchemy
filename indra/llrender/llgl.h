/** 
 * @file llgl.h
 * @brief LLGL definition
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

#ifndef LL_LLGL_H
#define LL_LLGL_H

// This file contains various stuff for handling gl extensions and other gl related stuff.

#include <string>
#include <absl/container/flat_hash_map.h>
#include <list>

#include "llerror.h"
#include "v4color.h"
#include "llstring.h"
#include "stdtypes.h"
#include "v4math.h"
#include "llmatrix4a.h"
#include "llplane.h"
#include "llgltypes.h"
#include "llinstancetracker.h"

#include "llglheaders.h"
#if LL_GNUC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#include "glh/glh_linear.h"
#if LL_GNUC
#pragma GCC diagnostic pop
#endif

extern BOOL gDebugGL;
extern BOOL gDebugSession;
extern llofstream gFailLog;

#define LL_GL_ERRS LL_ERRS("RenderState")

void ll_init_fail_log(std::string filename);

void ll_fail(std::string msg);

void ll_close_fail_log();

class LLSD;

// Manage GL extensions...
class LLGLManager
{
public:
	LLGLManager();

	bool initGL();
	void shutdownGL();

#if LL_WINDOWS
	void initWGL(HDC dc); // Initializes stupid WGL extensions
#endif

	std::string getRawGLString(); // For sending to simulator

	BOOL mInited;
	BOOL mIsDisabled;

	// Extensions used by everyone
	BOOL mHasMultitexture;
	BOOL mHasATIMemInfo;
	BOOL mHasAMDAssociations;
	BOOL mHasNVXMemInfo;
	S32	 mNumTextureUnits;
	BOOL mHasMipMapGeneration;
	BOOL mHasCompressedTextures;
	BOOL mHasFramebufferObject;
	S32 mMaxSamples;
	BOOL mHasBlendFuncSeparate;
		
	// ARB Extensions
	BOOL mHasVertexBufferObject;
	BOOL mHasVertexArrayObject;
	BOOL mHasSync;
	BOOL mHasMapBufferRange;
	BOOL mHasFlushBufferRange;
	BOOL mHasPBuffer;
	BOOL mHasShaderObjects;
	BOOL mHasVertexShader;
	BOOL mHasFragmentShader;
	S32  mNumTextureImageUnits;
	BOOL mHasOcclusionQuery;
	BOOL mHasTimerQuery;
	BOOL mHasOcclusionQuery2;
	BOOL mHasPointParameters;
	BOOL mHasDrawBuffers;
	BOOL mHasDepthClamp;
	BOOL mHasTextureRectangle;
	BOOL mHasTextureMultisample;
	BOOL mHasTransformFeedback;
	S32 mMaxSampleMaskWords;
	S32 mMaxColorTextureSamples;
	S32 mMaxDepthTextureSamples;
	S32 mMaxIntegerSamples;

	// Other extensions.
	BOOL mHasAnisotropic;
	BOOL mHasARBEnvCombine;
	BOOL mHasCubeMap;
	BOOL mHasDebugOutput;
	BOOL mHassRGBTexture;
	BOOL mHassRGBFramebuffer;
    BOOL mHasTexturesRGBDecode;
    bool mHasTextureSwizzle = false;
    bool mHasGPUShader4  = false;
    bool mHasClipControl = false;

	// Vendor-specific extensions
	BOOL mIsATI;
	BOOL mIsNVIDIA;
	BOOL mIsIntel;
	BOOL mIsGF2or4MX;
	BOOL mIsGF3;
	BOOL mIsGFFX;
	BOOL mATIOffsetVerticalLines;
	BOOL mATIOldDriver;

#if LL_DARWIN
	// Needed to distinguish problem cards on older Macs that break with Materials
	BOOL mIsMobileGF;
#endif
	
	// Whether this version of GL is good enough for SL to use
	BOOL mHasRequirements;

	// Misc extensions
	BOOL mHasSeparateSpecularColor;

	S32 mDriverVersionMajor;
	S32 mDriverVersionMinor;
	S32 mDriverVersionRelease;
	F32 mGLVersion; // e.g = 1.4
	S32 mGLSLVersionMajor;
	S32 mGLSLVersionMinor;
	std::string mDriverVersionVendorString;
	std::string mGLVersionString;

	S32 mVRAM; // VRAM in MB
	S32 mGLMaxVertexRange;
	S32 mGLMaxIndexRange;
	S32 mGLMaxTextureSize;
	F32 mGLMaxAnisotropy;
	
	void getPixelFormat(); // Get the best pixel format

	std::string getGLInfoString();
	void printGLInfoString();
	void getGLInfo(LLSD& info);

	void asLLSD(LLSD& info);

	// In ALL CAPS
	std::string mGLVendor;
	std::string mGLVendorShort;

	// In ALL CAPS
	std::string mGLRenderer;

private:
	void initExtensions();
	void initGLStates();
	void initGLImages();
};

extern LLGLManager gGLManager;

class LLQuaternion;
class LLMatrix4;

void rotate_quat(LLQuaternion& rotation);

void flush_glerror(); // Flush GL errors when we know we're handling them correctly.

void log_glerror();
void assert_glerror();

void clear_glerror();

#ifdef SHOW_DEBUG
# define stop_glerror() assert_glerror()
# define llglassertok() assert_glerror()
#else
# define stop_glerror()
# define llglassertok()
#endif

#define llglassertok_always() assert_glerror()

////////////////////////
//
// Note: U32's are GLEnum's...
//

// This is a class for GL state management

/*
	GL STATE MANAGEMENT DESCRIPTION

	LLGLState and its two subclasses, LLGLEnable and LLGLDisable, manage the current 
	enable/disable states of the GL to prevent redundant setting of state within a 
	render path or the accidental corruption of what state the next path expects.

	Essentially, wherever you would call glEnable set a state and then
	subsequently reset it by calling glDisable (or vice versa), make an instance of 
	LLGLEnable with the state you want to set, and assume it will be restored to its
	original state when that instance of LLGLEnable is destroyed.  It is good practice
	to exploit stack frame controls for optimal setting/unsetting and readability of 
	code.  In llglstates.h, there are a collection of helper classes that define groups
	of enables/disables that can cause multiple states to be set with the creation of
	one instance.  

	Sample usage:

	//disable lighting for rendering hud objects
	//INCORRECT USAGE
	LLGLEnable lighting(GL_LIGHTING);
	renderHUD();
	LLGLDisable lighting(GL_LIGHTING);

	//CORRECT USAGE
	{
		LLGLEnable lighting(GL_LIGHTING);
		renderHUD();
	}

	If a state is to be set on a conditional, the following mechanism
	is useful:

	{
		LLGLEnable lighting(light_hud ? GL_LIGHTING : 0);
		renderHUD();
	}

	A LLGLState initialized with a parameter of 0 does nothing.

	LLGLState works by maintaining a map of the current GL states, and ignoring redundant
	enables/disables.  If a redundant call is attempted, it becomes a noop, otherwise,
	it is set in the constructor and reset in the destructor.  

	For debugging GL state corruption, running with debug enabled will trigger asserts
	if the existing GL state does not match the expected GL state.

*/

#include "boost/function.hpp"

class LLGLState
{
public:
	static void initClass();
	static void restoreGL();

	static void resetTextureStates();
	static void dumpStates();
	static void checkStates(const std::string& msg = "");
	static void checkTextureChannels(const std::string& msg = "");
	static void checkClientArrays(const std::string& msg = "", U32 data_mask = 0);
	
protected:
	static absl::flat_hash_map<LLGLenum, LLGLboolean> sStateMap;
	
public:
	enum { CURRENT_STATE = -2 };
	LLGLState(LLGLenum state, S32 enabled = CURRENT_STATE);
	~LLGLState();
	void setEnabled(S32 enabled);
	void enable() { setEnabled(TRUE); }
	void disable() { setEnabled(FALSE); }
protected:
	LLGLenum mState;
	BOOL mWasEnabled;
	BOOL mIsEnabled;
};

// New LLGLState class wrappers that don't depend on actual GL flags.
class LLGLEnableBlending : public LLGLState
{
public:
	LLGLEnableBlending(bool enable);
};

class LLGLEnableAlphaReject : public LLGLState
{
public:
	LLGLEnableAlphaReject(bool enable);
};

// Enable with functor
class LLGLEnableFunc : LLGLState
{
public:
	LLGLEnableFunc(LLGLenum state, bool enable, boost::function<void()> func)
		: LLGLState(state, enable)
	{
		if (enable)
		{
			func();
		}
	}
};

/// TODO: Being deprecated.
class LLGLEnable : public LLGLState
{
public:
	LLGLEnable(LLGLenum state) : LLGLState(state, TRUE) {}
};

/// TODO: Being deprecated.
class LLGLDisable : public LLGLState
{
public:
	LLGLDisable(LLGLenum state) : LLGLState(state, FALSE) {}
};

/*
  Store and modify projection matrix to create an oblique
  projection that clips to the specified plane.  Oblique
  projections alter values in the depth buffer, so this
  class should not be used mid-renderpass.  

  Restores projection matrix on destruction.
  GL_MODELVIEW_MATRIX is active whenever program execution
  leaves this class.
  Does not stack.
*/
LL_ALIGN_PREFIX(16)
class LLGLUserClipPlane 
{
public:
	
	LLGLUserClipPlane(const LLPlane& plane, const LLMatrix4a& modelview, const LLMatrix4a& projection, bool apply = true);
	~LLGLUserClipPlane();

	void setPlane(F32 a, F32 b, F32 c, F32 d);
    void disable();

private:

	LL_ALIGN_16(LLMatrix4a mProjection);
	LL_ALIGN_16(LLMatrix4a mModelview);

	bool mApply;
} LL_ALIGN_POSTFIX(16);

/*
  Modify and load projection matrix to push depth values to far clip plane.

  Restores projection matrix on destruction.
  Saves/restores matrix mode around projection manipulation.
  Does not stack.
*/
class LLGLSquashToFarClip
{
public:
    LLGLSquashToFarClip();
	LLGLSquashToFarClip(const LLMatrix4a& projection, U32 layer = 0);

    void setProjectionMatrix(const LLMatrix4a& P_in, U32 layer);

	~LLGLSquashToFarClip();
};

/*
	Interface for objects that need periodic GL updates applied to them.
	Used to synchronize GL updates with GL thread.
*/
class LLGLUpdate
{
public:

	static std::list<LLGLUpdate*> sGLQ;

	BOOL mInQ;
	LLGLUpdate()
		: mInQ(FALSE)
	{
	}
	virtual ~LLGLUpdate()
	{
		if (mInQ)
		{
			std::list<LLGLUpdate*>::iterator iter = std::find(sGLQ.begin(), sGLQ.end(), this);
			if (iter != sGLQ.end())
			{
				sGLQ.erase(iter);
			}
		}
	}
	virtual void updateGL() = 0;
};

const U32 FENCE_WAIT_TIME_NANOSECONDS = 1000;  //1 ms

class LLGLFence
{
public:
	virtual ~LLGLFence()
	{
	}

	virtual void placeFence() = 0;
	virtual bool isCompleted() = 0;
	virtual void wait() = 0;
};

class LLGLSyncFence : public LLGLFence
{
public:
#ifdef GL_ARB_sync
	GLsync mSync;
#endif
	
	LLGLSyncFence();
	virtual ~LLGLSyncFence();

	void placeFence();
	bool isCompleted();
	void wait();
};

#include "llglstates.h"

void init_glstates();

void parse_gl_version( S32* major, S32* minor, S32* release, std::string* vendor_specific, std::string* version_string );

extern BOOL gHeadlessClient;
extern BOOL gGLActive;

#endif // LL_LLGL_H
