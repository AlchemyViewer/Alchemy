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

#include <functional>
#include <string>
#include <boost/unordered_map.hpp>
#include <list>

#include "llglheaders.h"

#include "llerror.h"
#include "v4color.h"
#include "llstring.h"
#include "stdtypes.h"
#include "v4math.h"
#include "llplane.h"
#include "llgltypes.h"
#include "llinstancetracker.h"

#include "glm/mat4x4.hpp"

extern bool gDebugGL;
extern bool gDebugSession;
extern bool gDebugGLSession;
extern llofstream gFailLog;

#define LL_GL_ERRS LL_ERRS("RenderState")

void ll_init_fail_log(std::string filename);

void ll_fail(std::string msg);

void ll_close_fail_log();

class LLSD;

// Hard minimum OpenGL version. The renderer assumes this unconditionally rather than
// degrading below it: deprecated formats are re-expressed through GL_TEXTURE_SWIZZLE_RGBA
// instead of being repacked on the CPU, the texture upload thread is always enabled, and
// the shader backend expects GLSL 4.10.
//
// Biased down by 0.01 like the other version checks in this file, because mGLVersion is
// assembled as major + minor * 0.1f and 4.1f is not exactly representable.
const F32 GL_MINIMUM_VERSION = 4.09f;

// Manage GL extensions...
class LLGLManager
{
public:
    LLGLManager();

    bool initGL();
    void shutdownGL();

    void initWGL(); // Initializes WGL extensions
    void initGLX(); // Initializes GLX extensions
    void initEGL(); // Initializes EGL extensions

    std::string getRawGLString(); // For sending to simulator

    bool mInited;
    bool mIsDisabled;

    // OpenGL limits
    S32 mMaxSamples;
    S32 mNumTextureImageUnits;
    S32 mMaxSampleMaskWords;
    S32 mMaxColorTextureSamples;
    S32 mMaxDepthTextureSamples;
    S32 mMaxIntegerSamples;
    S32 mGLMaxVertexRange;
    S32 mGLMaxIndexRange;
    S32 mGLMaxTextureSize;
    F32 mMaxAnisotropy = 0.f;
    S32 mMaxUniformBlockSize = 0;
    S32 mMaxVaryingVectors = 0;
    LLVector2 mAliasedLineRange = LLVector2(1.f, 1.f);

    // GL 4.x capabilities
    bool mHasCubeMapArray = false;
    bool mHasDebugOutput = false;
    bool mHasTransformFeedback = false;
    bool mHasAnisotropic = false;
    // Immutable texture storage (glTexStorage*). Core in 4.2, but also reachable on a
    // 4.1 context via GL_ARB_texture_storage -- which is how macOS gets it. Only true
    // once the entry point has actually resolved, so callers may trust it directly.
    bool mHasTextureStorage = false;
    // GL_EXT_texture_sRGB_decode: sampler/texture control over whether an sRGB-format
    // texture has its transfer function applied on read. Never promoted to core, but
    // universally supported on the hardware this viewer runs on, and REQUIRED here --
    // the renderer decodes explicitly rather than implicitly, so it needs to be able to
    // turn the implicit decode off. See ALSampler::SRGBDecode.
    bool mHasTextureSRGBDecode = false;
    // Direct state access (glBindTextureUnit, glCreateSamplers, glTextureStorage*, ...).
    // Core in 4.5, also reachable as GL_ARB_direct_state_access. Only true once the entry
    // points have actually resolved, so callers may trust it directly.
    //
    // NOTE: DSA entry points that take a texture name require the texture's target to
    // already be established -- glBindTextureUnit on a name straight out of glGenTextures
    // is INVALID_OPERATION. Allocation paths that bind-to-create must keep using
    // glBindTexture until they are ported to glCreateTextures.
    bool mHasDirectStateAccess = false;
    // Clip control (glClipControl for GL_ZERO_TO_ONE depth range). Core in 4.5, also
    // reachable as GL_ARB_clip_control. Gates the reverse-Z depth path; only true once
    // the entry point has actually resolved, so callers may trust it directly. Absent on
    // macOS GL 4.1, which stays forward-Z.
    bool mHasClipControl = false;

    // Vendor-specific extensions
    bool mHasAMDAssociations = false;
    bool mHasNVXGpuMemoryInfo = false;
    bool mHasATIMemInfo = false;
    bool mHasGLXMESAQueryRenderer = false;
    bool mHasEXTMemoryObject           = false;
    bool mHasEXTSemaphore              = false;
    bool mHasEXTMemoryObjectWin32      = false;
    bool mHasEXTSemaphoreWin32         = false;

    bool mIsAMD;
    bool mIsNVIDIA;
    bool mIsIntel;
    bool mIsApple = false;
    // True for any Mesa driver (radeonsi, iris, llvmpipe, zink, ...). Detected
    // from the GL_VERSION string; used to gate Mesa-specific workarounds.
    bool mIsMesa = false;

    // hints to the render pipe
    U32 mDownScaleMethod = 0; // see settings.xml RenderDownScaleMethod

    // Whether this version of GL is good enough for SL to use
    bool mHasRequirements;

    S32 mDriverVersionMajor;
    S32 mDriverVersionMinor;
    S32 mDriverVersionRelease;
    F32 mGLVersion; // e.g = 1.4
    S32 mGLSLVersionMajor;
    S32 mGLSLVersionMinor;
    std::string mDriverVersionVendorString;
    std::string mGLVersionString;

    U32 mVRAM; // VRAM in MB

    std::string getGLInfoString();
    void printGLInfoString();
    void getGLInfo(LLSD& info);

    void asLLSD(LLSD& info);

    // In ALL CAPS
    std::string mGLVendor;
    std::string mGLVendorShort;

    // In ALL CAPS
    std::string mGLRenderer;

    // GL Extension String
    std::set<std::string> mGLExtensions;

#if LL_LINUX
    bool mIsX11 = false;
    bool mIsWayland = false;
#endif

private:
    void reloadExtensionsString();
    void initExtensions();
    void initGLStates();
};

extern LLGLManager gGLManager;

class LLQuaternion;
class LLMatrix4;

void rotate_quat(LLQuaternion& rotation);

void flush_glerror(); // Flush GL errors when we know we're handling them correctly.

void log_glerror();
// Validate every sampler the currently bound program declares against the state actually
// bound to its texture unit: something bound at all, and no depth/compare mismatch in either
// direction (non-shadow sampler over a compare-enabled depth texture, or a shadow sampler
// without comparisons). Returns true when the state is sound.
//
// gDebugGL only -- returns true immediately otherwise. Intended for llassert() at draw
// sites, the way LLVertexBuffer::validateRange is used, so it fails at the draw responsible
// and names the uniform instead of leaving a driver warning to be traced back by hand.
bool validate_bound_samplers();

// Drop the cached sampler enumeration for a program about to be deleted. GL is free to hand
// the name back out, and the cache revalidates only on active-uniform COUNT -- a recreated
// program with the same name and count would be validated against the old program's
// samplers. Call before glDeleteProgram; harmless when the program was never cached.
void forget_program_samplers(U32 program);

void assert_glerror();

void clear_glerror();


# define stop_glerror() assert_glerror()
# define llglassertok() assert_glerror()

// stop_glerror is still needed on OS X but has performance implications
// use macro below to conditionally add stop_glerror to non-release builds
// on OS X
#if LL_DARWIN && !LL_RELEASE_FOR_DOWNLOAD
#define STOP_GLERROR stop_glerror()
#else
#define STOP_GLERROR
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
    LLGLEnable blend(GL_BLEND);
    renderHUD();
    LLGLDisable blend(GL_BLEND);

    //CORRECT USAGE
    {
        LLGLEnable blend(GL_BLEND);
        renderHUD();
    }

    If a state is to be set on a conditional, the following mechanism
    is useful:

    {
        LLGLEnable blend(blend_hud ? GL_GL_BLEND: 0);
        renderHUD();
    }

    A LLGLState initialized with a parameter of 0 does nothing.

    LLGLState works by maintaining a map of the current GL states, and ignoring redundant
    enables/disables.  If a redundant call is attempted, it becomes a noop, otherwise,
    it is set in the constructor and reset in the destructor.

    For debugging GL state corruption, running with debug enabled will trigger asserts
    if the existing GL state does not match the expected GL state.

*/

class LLGLState
{
public:
    static void initClass();
    static void restoreGL();

    static void resetTextureStates();
    static void dumpStates();

    // make sure GL blend function, GL states, and GL color mask match
    // what we expect
    //  writeAlpha - whether or not writing to alpha channel is expected
    static void checkStates(GLboolean writeAlpha = GL_TRUE);

protected:
    static boost::unordered_map<LLGLenum, LLGLboolean> sStateMap;

public:
    enum { CURRENT_STATE = -2, DISABLED_STATE = 0, ENABLED_STATE = 1 };
    LLGLState(LLGLenum state, S32 enabled = CURRENT_STATE);
    ~LLGLState();
    void setEnabled(S32 enabled);
    void enable() { setEnabled(ENABLED_STATE); }
    void disable() { setEnabled(DISABLED_STATE); }
protected:
    LLGLenum mState;
    bool mWasEnabled;
    bool mIsEnabled;
};

// Enable with functor
class LLGLEnableFunc : LLGLState
{
public:
    LLGLEnableFunc(LLGLenum state, bool enable, std::function<void()> func)
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
    LLGLEnable(LLGLenum state) : LLGLState(state, ENABLED_STATE) {}
};

/// TODO: Being deprecated.
class LLGLDisable : public LLGLState
{
public:
    LLGLDisable(LLGLenum state) : LLGLState(state, DISABLED_STATE) {}
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
  Caches inverse of projection matrix used in gGLObliqueProjectionInverse
*/
class LLGLUserClipPlane
{
public:

    LLGLUserClipPlane(const LLPlane& plane, const glm::mat4& modelview, const glm::mat4& projection, bool apply = true);
    ~LLGLUserClipPlane();

    void setPlane(F32 a, F32 b, F32 c, F32 d);
    void disable();

private:
    bool mApply;

    glm::mat4 mProjection;
    glm::mat4 mModelview;
};

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
    LLGLSquashToFarClip(const glm::mat4& projection, U32 layer = 0);

    void setProjectionMatrix(glm::mat4 projection, U32 layer);

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

    bool mInQ;
    LLGLUpdate()
        : mInQ(false)
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

const U32 FENCE_WAIT_TIME_NANOSECONDS = 1000;  //1 microsecond (despite the name's history)

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
    GLsync mSync;

    LLGLSyncFence();
    virtual ~LLGLSyncFence();

    void placeFence();
    bool isCompleted();
    void wait();
};

extern LLMatrix4 gGLObliqueProjectionInverse;

#include "llglstates.h"

void init_glstates();

void parse_gl_version( S32* major, S32* minor, S32* release, std::string* vendor_specific, std::string* version_string );

extern bool gClothRipple;
extern bool gHeadlessClient;
extern bool gNonInteractive;
extern bool gGLActive;

#endif // LL_LLGL_H
