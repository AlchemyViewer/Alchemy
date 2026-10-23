/**
 * @file llglstates.h
 * @brief LLGL states definitions
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

//THIS HEADER SHOULD ONLY BE INCLUDED FROM llgl.h
#ifndef LL_LLGLSTATES_H
#define LL_LLGLSTATES_H

#include "llimagegl.h"

//----------------------------------------------------------------------------

class LLGLDepthTest
{
    // Enabled by default
public:
    LLGLDepthTest(GLboolean depth_enabled, GLboolean write_enabled = GL_TRUE, GLenum depth_func = GL_LEQUAL);

    ~LLGLDepthTest();

    // A copy would restore the depth state a second time.
    LLGLDepthTest(const LLGLDepthTest&) = delete;
    LLGLDepthTest& operator=(const LLGLDepthTest&) = delete;

    void checkState();

    // Take the tracked state back to a fresh context's: test off, writes on, GL_LESS, with the
    // func issued through remap so GL agrees under whichever convention is latched. Run per
    // context by LLGLState::initClass.
    static void initClass();

    // Reverse-Z depth-func translation. The tracked state (sDepthFunc) and every call
    // site stay in the forward/semantic convention; the physical glDepthFunc is fed the
    // translated func so a call site asking for "nearer wins" keeps that meaning under
    // reverse-Z. Identity unless LLRender::sReverseZ. LESS<->GREATER, LEQUAL<->GEQUAL;
    // EQUAL/NOTEQUAL/ALWAYS/NEVER pass through unchanged.
    static GLenum remap(GLenum func);
    // Re-issue the physical depth func for the current translation. Call after
    // LLRender::sReverseZ changes so the GL state agrees with the (unchanged) semantic
    // static -- the ambient func was issued under the old translation.
    static void rebase();

    GLboolean mPrevDepthEnabled;
    GLenum mPrevDepthFunc;
    GLboolean mPrevWriteEnabled;
private:
    static GLboolean sDepthEnabled; // defaults to GL_FALSE
    static GLenum sDepthFunc; // defaults to GL_LESS (semantic/forward convention)
    static GLboolean sWriteEnabled; // defaults to GL_TRUE
};

//----------------------------------------------------------------------------

class LLGLSDefault
{
protected:
    LLGLDisable mBlend, mCullFace;
public:
    LLGLSDefault()
        :
        // Disable
        mBlend(GL_BLEND),
        mCullFace(GL_CULL_FACE)
    { }
};

class LLGLSObjectSelect
{
protected:
    LLGLDisable mBlend;
    LLGLEnable mCullFace;
public:
    LLGLSObjectSelect()
        : mBlend(GL_BLEND),
          mCullFace(GL_CULL_FACE)
    { }
};

//----------------------------------------------------------------------------

class LLGLSUIDefault
{
protected:
    LLGLEnable mBlend;
    LLGLDisable mCullFace;
    LLGLDepthTest mDepthTest;
public:
    LLGLSUIDefault()
        : mBlend(GL_BLEND),
          mCullFace(GL_CULL_FACE),
          mDepthTest(GL_FALSE, GL_TRUE, GL_LEQUAL)
    {}
};

//----------------------------------------------------------------------------

class LLGLSPipeline
{
protected:
    LLGLEnable mCullFace;
    LLGLDepthTest mDepthTest;
public:
    LLGLSPipeline()
        : mCullFace(GL_CULL_FACE),
          mDepthTest(GL_TRUE, GL_TRUE, GL_LEQUAL)
    { }
};

class LLGLSPipelineAlpha // : public LLGLSPipeline
{
protected:
    LLGLEnable mBlend;
public:
    LLGLSPipelineAlpha()
        : mBlend(GL_BLEND)
    { }
};

class LLGLSPipelineSelection
{
protected:
    LLGLDisable mCullFace;
public:
    LLGLSPipelineSelection()
        : mCullFace(GL_CULL_FACE)
    {}
};

// Scoped colour write mask: restores whatever was in force, not a hardcoded convention.
//
// The write mask is ambient state with two live conventions in the tree -- the G-buffer
// pools want all four channels, the post-deferred passes want colour-on/alpha-off -- so a
// block that set it and then "restored" a literal was only correct in the pass it was
// written for. Anything that runs in both (the avatar multi-pass hair/skirt path, which an
// impostor bake drives through the G-buffer) has to hand back what it was given.
class LLGLSColorMask
{
public:
    LLGLSColorMask(bool writeColorR, bool writeColorG, bool writeColorB, bool writeAlpha)
    {
        gGL.getColorMask(mPrev);
        gGL.setColorMask(writeColorR, writeColorG, writeColorB, writeAlpha);
    }

    LLGLSColorMask(bool writeColor, bool writeAlpha)
    :   LLGLSColorMask(writeColor, writeColor, writeColor, writeAlpha)
    { }

    ~LLGLSColorMask()
    {
        gGL.setColorMask(mPrev[0], mPrev[1], mPrev[2], mPrev[3]);
    }

    LLGLSColorMask(const LLGLSColorMask&) = delete;
    LLGLSColorMask& operator=(const LLGLSColorMask&) = delete;

private:
    bool mPrev[4];
};

// Scoped scissor: the test enabled and the box set, both handed back on exit as they were
// found. A box never set in this context is left as the scope set it, since GL takes no
// negative size to restore it with.
class LLGLSScissor
{
public:
    LLGLSScissor(S32 x, S32 y, S32 width, S32 height)
    :   mTest(GL_SCISSOR_TEST)
    {
        gGL.getScissor(mPrev);
        gGL.setScissor(x, y, width, height);
    }

    ~LLGLSScissor()
    {
        if (mPrev[2] >= 0 && mPrev[3] >= 0)
        {
            gGL.setScissor(mPrev[0], mPrev[1], mPrev[2], mPrev[3]);
        }
    }

    LLGLSScissor(const LLGLSScissor&) = delete;
    LLGLSScissor& operator=(const LLGLSScissor&) = delete;

private:
    LLGLEnable mTest;
    S32 mPrev[4];
};

// Scoped stencil test: on, with the function, reference and read mask, the operations on a stencil
// fail, a depth fail and a pass, and the write mask given. On exit the test goes back off and the
// rest to GL's defaults, the write mask to every bit above all: a clear writes stencil only through
// it. Scopes do not nest; the inner one's exit would leave the outer's test on at the defaults.
class ALGLStencil
{
public:
    ALGLStencil(GLenum func, GLint ref, GLuint read_mask, GLenum fail, GLenum depth_fail, GLenum pass, GLuint write_mask)
    :   mTest(GL_STENCIL_TEST)
    {
        llassert(sOpen == 0);
        ++sOpen;
        gGL.setStencilFunc(func, ref, read_mask);
        gGL.setStencilOp(fail, depth_fail, pass);
        gGL.setStencilMask(write_mask);
    }

    ~ALGLStencil()
    {
        gGL.setStencilFunc(GL_ALWAYS, 0, ~0u);
        gGL.setStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        gGL.setStencilMask(~0u);
        --sOpen;
    }

    ALGLStencil(const ALGLStencil&) = delete;
    ALGLStencil& operator=(const ALGLStencil&) = delete;

private:
    LLGLEnable mTest;
    // The scopes open, on the thread that draws: one at most.
    static inline S32 sOpen = 0;
};

class LLGLSTracker
{
protected:
    LLGLEnable mCullFace, mBlend;
public:
    LLGLSTracker() :
        mCullFace(GL_CULL_FACE),
        mBlend(GL_BLEND)
    { }
};

//----------------------------------------------------------------------------

#endif
