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

    void checkState();

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

class LLGLSPipelineSkyBox
{
protected:
    LLGLDisable mCullFace;
    LLGLSquashToFarClip mSquashClip;
public:
    LLGLSPipelineSkyBox();
   ~LLGLSPipelineSkyBox();
};

class LLGLSPipelineDepthTestSkyBox : public LLGLSPipelineSkyBox
{
public:
    LLGLSPipelineDepthTestSkyBox(bool depth_test, bool depth_write);

    LLGLDepthTest mDepth;
};

class LLGLSPipelineBlendSkyBox : public LLGLSPipelineDepthTestSkyBox
{
public:
    LLGLSPipelineBlendSkyBox(bool depth_test, bool depth_write);
    LLGLEnable mBlend;
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
