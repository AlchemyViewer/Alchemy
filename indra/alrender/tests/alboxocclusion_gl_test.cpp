/**
 * @file alboxocclusion_gl_test.cpp
 * @brief Boxes tested against a depth pyramid all at once, and their answers read back frames later.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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
 * $/LicenseInfo$
 */

// ALBoxOcclusion as the main view's occlusion pass uses it: the depth drawn so far built into a pyramid, a frame's
// boxes added and dispatched once, their answers collected once the frame fences show the frame finished. A box is
// hidden only where every point of it lies behind the depth under it; one in front of it, beside it or straddling its
// edge is seen.

#include "linden_common.h"

#include "../alboxocclusion.h"
#include "../aldepthpyramid.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llmatrix4a.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llstrider.h"
#include "llvertexbuffer.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    constexpr S32 W = 64;
    constexpr S32 H = 64;

    const char* kVertex =
        "#version 400\n"
        "in vec3 position;\n"
        "void main() { gl_Position = vec4(position, 1.0); }\n";

    const char* kFragment =
        "#version 400\n"
        "out vec4 frag_color;\n"
        "void main() { frag_color = vec4(1.0); }\n";

    LLVector4a vec(F32 x, F32 y, F32 z)
    {
        return LLVector4a(x, y, z, 0.f);
    }

    // Under identity matrices, so a box's coordinates are its clip space, forward-Z: an occluder over the left half
    // of the target at depth 0.5, the right half left at the far plane.
    struct alboxocclusion_data
    {
        alboxocclusion_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
        }

        ~alboxocclusion_data()
        {
            if (mScene.isBoundInStack())
            {
                mScene.flush();
            }
            LLGLSLShader::unbind();
            mBoxes.release();
            mPyramid.release();
            mOccluder.unload();
            mPyramidProgram.unload();
            mGroupProgram.unload();
            mShaders.clearShaderObjects();
            mScene.release();
            mQuad = nullptr;
            LLRender::sCompute  = mCompute;
            LLRender::sReverseZ = mReverseZ;
        }

        void setUp()
        {
            if (!ll_test::contextHasCompute())
            {
                SKIP("the context has no compute");
            }
            LLRender::sCompute  = true;
            LLRender::sReverseZ = false;
            if (gGLManager.mHasClipControl)
            {
                glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
            }
            glClearDepth(1.0);
            LLGLDepthTest::rebase();

            mPyramidProgram.mName = "Depth Pyramid Shader";
            mPyramidProgram.mShaderFiles.clear();
            mPyramidProgram.mShaderFiles.push_back(std::make_pair("deferred/depthPyramidC.glsl", GL_COMPUTE_SHADER));
            mPyramidProgram.mShaderLevel = 1;
            UNSCOPED_INFO("the pyramid's program builds");
            REQUIRE(mPyramidProgram.createShader());

            // As LLViewerShaderMgr builds gGroupOcclusionProgram.
            mGroupProgram.mName = "Group Occlusion Shader";
            mGroupProgram.mShaderFiles.clear();
            mGroupProgram.mShaderFiles.push_back(std::make_pair("deferred/groupOcclusionC.glsl", GL_COMPUTE_SHADER));
            mGroupProgram.mShaderFiles.push_back(std::make_pair("deferred/depthPyramidUtil.glsl", GL_COMPUTE_SHADER));
            mGroupProgram.mShaderLevel = 1;
            UNSCOPED_INFO("the boxes' program builds");
            REQUIRE(mGroupProgram.createShader());
            REQUIRE(mGroupProgram.isCompute());
            {
                INFO("its storage block is at the engine's point");
                CHECK(mGroupProgram.getStorageBinding(LLShaderMgr::OCCLUSION_BOXES) ==
                      mShaders.getStorageBlockBinding(LLShaderMgr::OCCLUSION_BOXES));
            }

            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            REQUIRE(vs);
            REQUIRE(fs);
            mOccluder.mProgramObject = glCreateProgram();
            mOccluder.mName = "occluder (test)";
            mOccluder.attachObject(vs);
            mOccluder.attachObject(fs);
            REQUIRE(mOccluder.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            REQUIRE(mOccluder.mapUniforms());

            mQuad = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(mQuad->allocateBuffer(4, 6));
            LLStrider<LLVector3> pos;
            REQUIRE(mQuad->getVertexStrider(pos));
            pos[0].set(-1.f, -1.f, 0.f);
            pos[1].set(0.f, -1.f, 0.f);
            pos[2].set(-1.f, 1.f, 0.f);
            pos[3].set(0.f, 1.f, 0.f);
            LLStrider<U16> idx;
            REQUIRE(mQuad->getIndexStrider(idx));
            const U16 quad[6] = { 0, 1, 2, 2, 1, 3 };
            for (S32 i = 0; i < 6; ++i)
            {
                idx[i] = quad[i];
            }
            mQuad->unmapBuffer();

            REQUIRE(mScene.allocate(W, H, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                                    LLRenderTarget::DEPTH_FMT_32F));
            mIdentity.setIdentity();
        }

        // The occluder drawn into the scene, and the pyramid built from its depth, the scene left bound.
        void drawAndBuild()
        {
            mScene.bindTarget();
            glClearColor(0.f, 0.f, 0.f, 0.f);
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            {
                LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LEQUAL);
                mOccluder.bind();
                mQuad->setBuffer();
                mQuad->draw(LLRender::TRIANGLES, 6, 0);
                mOccluder.unbind();
            }
            UNSCOPED_INFO("the pyramid builds from the scene's depth");
            REQUIRE(mPyramid.build(mPyramidProgram, mScene));
        }

        // This frame ended, finished on the GPU and shown finished, and the answers taken in.
        void finishAndCollect()
        {
            if (mScene.isBoundInStack())
            {
                mScene.flush();
            }
            ALFrameFences& frames = gGL.frameFences();
            gGL.endFrame();
            glFinish();
            frames.waitFinished(frames.frame() - 1);
            mBoxes.collect();
        }

        ll_test::SharedGLScope    mGL;
        ll_test::TestShaderMgr    mShaders;
        LLGLSLShader              mPyramidProgram;
        LLGLSLShader              mGroupProgram;
        LLGLSLShader              mOccluder;
        LLPointer<LLVertexBuffer> mQuad;
        LLRenderTarget            mScene;
        ALDepthPyramid            mPyramid;
        ALBoxOcclusion            mBoxes;
        LLMatrix4a                mIdentity;
        bool                      mCompute  = LLRender::sCompute;
        bool                      mReverseZ = LLRender::sReverseZ;
    };

    // No ticket is no answer, and the boxes take boxes before anything is set up.
    TEST_CASE("alboxocclusion: no ticket has no answer, and a fresh one takes boxes", "[alboxocclusion]")
    {
        ALBoxOcclusion boxes;
        CHECK(boxes.result(ALBoxOcclusion::Ticket()) == ALBoxOcclusion::Status::Expired);
        CHECK(boxes.canAdd());
        const ALBoxOcclusion::Ticket ticket = boxes.add(vec(0.f, 0.f, 0.f), vec(1.f, 1.f, 1.f));
        REQUIRE(ticket.isValid());
        CHECK(boxes.result(ticket) == ALBoxOcclusion::Status::Pending);
        CHECK(boxes.getOpenCount() == 1);
        CHECK(ALBoxOcclusion::screenReach() == 768.f);
    }

    // Behind the occluder, hidden; in front of it, beside it and across its edge, seen: answered once the frame is
    // shown finished, and pending until then.
    TEST_CASE_METHOD(alboxocclusion_data, "alboxocclusion_gl: a box is hidden only where the depth hides all of it", "[alboxocclusion_gl]")
    {
        setUp();
        drawAndBuild();

        const ALBoxOcclusion::Ticket behind   = mBoxes.add(vec(-0.5f, 0.f, 0.6f), vec(0.2f, 0.2f, 0.1f));
        const ALBoxOcclusion::Ticket in_front = mBoxes.add(vec(-0.5f, 0.f, -0.5f), vec(0.2f, 0.2f, 0.1f));
        const ALBoxOcclusion::Ticket beside   = mBoxes.add(vec(0.5f, 0.f, 0.6f), vec(0.2f, 0.2f, 0.1f));
        const ALBoxOcclusion::Ticket across   = mBoxes.add(vec(0.f, 0.f, 0.6f), vec(0.2f, 0.2f, 0.1f));
        REQUIRE(behind.isValid());
        REQUIRE(across.isValid());
        REQUIRE(mBoxes.dispatch(mGroupProgram, mPyramid, mIdentity));
        {
            INFO("pending until the frame is shown finished");
            CHECK(mBoxes.result(behind) == ALBoxOcclusion::Status::Pending);
        }
        finishAndCollect();

        CHECK(mBoxes.result(behind) == ALBoxOcclusion::Status::Hidden);
        CHECK(mBoxes.result(in_front) == ALBoxOcclusion::Status::Seen);
        CHECK(mBoxes.result(beside) == ALBoxOcclusion::Status::Seen);
        CHECK(mBoxes.result(across) == ALBoxOcclusion::Status::Seen);
        CHECK(mBoxes.getDispatched() == 4);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // While every readback slot waits on the GPU, no box is taken, so that its group is asked by query instead of
    // waiting on an answer no dispatch will give; once the frames are shown finished, boxes are taken again.
    TEST_CASE_METHOD(alboxocclusion_data, "alboxocclusion_gl: no box is taken while every answer is on its way", "[alboxocclusion_gl]")
    {
        setUp();
        drawAndBuild();
        for (U32 i = 0; i < ALBoxOcclusion::IN_FLIGHT; ++i)
        {
            CAPTURE(i);
            REQUIRE(mBoxes.add(vec(-0.5f, 0.f, 0.6f), vec(0.2f, 0.2f, 0.1f)).isValid());
            REQUIRE(mBoxes.dispatch(mGroupProgram, mPyramid, mIdentity));
        }
        CHECK_FALSE(mBoxes.canAdd());
        CHECK_FALSE(mBoxes.add(vec(-0.5f, 0.f, 0.6f), vec(0.2f, 0.2f, 0.1f)).isValid());

        finishAndCollect();
        CHECK(mBoxes.canAdd());
        CHECK(mBoxes.getRefused() == 0);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Released, every answer goes with the stores, and every ticket expires: its group is taken as seen.
    TEST_CASE_METHOD(alboxocclusion_data, "alboxocclusion_gl: release expires every ticket", "[alboxocclusion_gl]")
    {
        setUp();
        drawAndBuild();
        const ALBoxOcclusion::Ticket answered = mBoxes.add(vec(-0.5f, 0.f, 0.6f), vec(0.2f, 0.2f, 0.1f));
        REQUIRE(mBoxes.dispatch(mGroupProgram, mPyramid, mIdentity));
        finishAndCollect();
        REQUIRE(mBoxes.result(answered) == ALBoxOcclusion::Status::Hidden);
        const ALBoxOcclusion::Ticket open = mBoxes.add(vec(0.5f, 0.f, 0.6f), vec(0.2f, 0.2f, 0.1f));
        REQUIRE(open.isValid());

        mBoxes.release();
        CHECK(mBoxes.result(answered) == ALBoxOcclusion::Status::Expired);
        CHECK(mBoxes.result(open) == ALBoxOcclusion::Status::Expired);
        CHECK(mBoxes.getOpenCount() == 0);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }
}
