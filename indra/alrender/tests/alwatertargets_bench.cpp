/**
 * @file alwatertargets_bench.cpp
 * @brief What the water's copies of the scene cost the GPU at 2560x1440: the scene's depth copied for a haze pass
 *        against read where it is, the copy of the screen whole against over the field's screen rectangle, and that
 *        copy in RGBA16F over the scene's depth, as it was, against the plan's own, written and read.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

// Each row is the GPU's time for one pass, or the passes named, read from GL_TIME_ELAPSED queries: the median of 31
// samples, each over 20 runs back to back, after a warm-up. The targets are the plan's own descriptions at the 1440p
// defaults, so a row run at two commits measures what each commit's plan builds. The output is a table, read against
// the same row from another build on the same quiet machine; another process drawing on the GPU meanwhile shows as
// noise.

#include "linden_common.h"

#include "../alrenderresourceplan.h"
#include "../alwaterfield.h"

#include "aldepthbounds.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llstrider.h"
#include "llvertexbuffer.h"
#include "v3math.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"
#include "altestbenchmark.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace
{
        using Plan = ALRenderResourcePlan;
        using Id   = Plan::Id;

        constexpr S32 SAMPLES = 31;
        constexpr S32 RUNS    = 20;

        // A triangle over the whole target on the far plane, as the haze passes draw theirs (hazeV.glsl).
        const char* kFarTriangleVertex =
            "#version 400\n"
            "void main()\n"
            "{\n"
            "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
            "    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);\n"
            "}\n";

        // A haze pass's reads: the scene's depth at its own pixel, through the sampler the pipeline binds, worked into
        // a position and a fog term.
        const char* kHazeFragment =
            "#version 400\n"
            "uniform sampler2D depthMap;\n"
            "uniform vec2 screen_res;\n"
            "out vec4 frag_color;\n"
            "void main()\n"
            "{\n"
            "    vec2 tc = gl_FragCoord.xy / screen_res;\n"
            "    float depth = texture(depthMap, tc).r;\n"
            "    vec3 pos = vec3(tc * 2.0 - 1.0, depth) / max(1.0 - depth, 1e-4);\n"
            "    float fog = exp(-length(pos) * 0.01);\n"
            "    frag_color = vec4(vec3(1.0 - fog) * 0.25, fog);\n"
            "}\n";

        // The water's read of the copy of the screen: its own pixel moved by a ripple of up to 3% of the screen, as
        // waterF.glsl moves it by the default scale above the water.
        const char* kRefractionFragment =
            "#version 400\n"
            "uniform sampler2D screenTex;\n"
            "uniform vec2 screen_res;\n"
            "out vec4 frag_color;\n"
            "void main()\n"
            "{\n"
            "    vec2 tc = gl_FragCoord.xy / screen_res;\n"
            "    vec2 ripple = vec2(sin(gl_FragCoord.y * 0.05), cos(gl_FragCoord.x * 0.05)) * 0.03;\n"
            "    vec4 fb = texture(screenTex, clamp(tc + ripple, vec2(0.0), vec2(0.999)));\n"
            "    frag_color = vec4(fb.rgb * 0.9, 0.0);\n"
            "}\n";

        ALRenderResourceInputs defaults()
        {
            ALRenderResourceInputs in;
            in.mWidth   = 2560;
            in.mHeight  = 1440;
            in.mMirrors = true;
            return in;
        }

    struct alwatertargets_bench_data
    {
        alwatertargets_bench_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class"),
          mPlan(Plan::make(defaults()))
        {
            LLRender::sReverseZ = false;
            glClearDepth(1.0);
            LLGLDepthTest::rebase();
        }

        ~alwatertargets_bench_data()
        {
            LLGLSLShader::unbind();
            mCopyDepth.unload();
            mHaze.unload();
            mRefraction.unload();
            mShaders.clearShaderObjects();
            if (mQuery)
            {
                glDeleteQueries(1, &mQuery);
            }
            mTriangle = nullptr;
            for (LLRenderTarget* target : { &mWaterWide, &mWater, &mDepthCopy, &mScreen, &mOut, &mGBuffer })
            {
                target->release();
            }
        }

        // A program of the test's own over the shared objects, mapped as the viewer's are.
        void build(LLGLSLShader& program, const char* name, const char* vertex, const char* fragment)
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, vertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, fragment);
            {
                INFO(std::string(name) + " compiles");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            program.mProgramObject = glCreateProgram();
            program.mName = name;
            program.attachObject(vs);
            program.attachObject(fs);
            {
                INFO(std::string(name) + " links");
                REQUIRE(program.mapAttributes());
            }
            glDeleteShader(vs);
            glDeleteShader(fs);
            INFO(std::string(name) + " maps");
            REQUIRE(program.mapUniforms());
        }

        void setUp()
        {
            std::map<std::string, std::string> defines;
            for (const char* object : { "deferred/textureUtilV.glsl", "objects/nonindexedTextureV.glsl" })
            {
                S32 level = 1;
                INFO(object);
                REQUIRE(mShaders.loadShaderFile(object, level, GL_VERTEX_SHADER, &defines) != 0);
            }
            S32 level = 1;
            REQUIRE(mShaders.loadShaderFile("deferred/globalF.glsl", level, GL_FRAGMENT_SHADER, &defines) != 0);

            // LLViewerShaderMgr's gCopyDepthProgram, which LLPipeline::copySceneDepth draws with.
            mCopyDepth.mName = "Copy Depth Shader";
            mCopyDepth.mShaderFiles.clear();
            mCopyDepth.mShaderFiles.push_back(std::make_pair("interface/copyV.glsl", GL_VERTEX_SHADER));
            mCopyDepth.mShaderFiles.push_back(std::make_pair("interface/copyF.glsl", GL_FRAGMENT_SHADER));
            mCopyDepth.clearPermutations();
            mCopyDepth.addPermutation("COPY_DEPTH", "1");
            mCopyDepth.addPermutation("DEPTH_ONLY", "1");
            mCopyDepth.mShaderLevel = 1;
            REQUIRE(mCopyDepth.createShader());

            build(mHaze, "haze (bench)", kFarTriangleVertex, kHazeFragment);
            build(mRefraction, "refraction (bench)", kFarTriangleVertex, kRefractionFragment);

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();

            // The plan's targets: the G-buffer, the screen over its depth and the water's two copies; and beside them
            // the copy of the screen as it was, the screen's format over the scene's depth.
            REQUIRE(mGBuffer.ensure(*mPlan.desc(Id::MainGBuffer)) != LLRenderTarget::EnsureResult::Failed);
            REQUIRE(mScreen.ensure(*mPlan.desc(Id::MainScreen), mGBuffer) != LLRenderTarget::EnsureResult::Failed);
            REQUIRE(mDepthCopy.ensure(*mPlan.desc(Id::MainDepthCopy)) != LLRenderTarget::EnsureResult::Failed);
            const Plan::Resource& water = mPlan[Id::MainWater];
            REQUIRE((water.mDepthFrom ? mWater.ensure(*water.mDesc, mGBuffer) : mWater.ensure(*water.mDesc)) !=
                    LLRenderTarget::EnsureResult::Failed);
            const ALRenderTargetDesc wide{ .width = (U32)mScreen.getWidth(), .height = (U32)mScreen.getHeight(), .color = { GL_RGBA16F } };
            REQUIRE(mWaterWide.ensure(wide, mGBuffer) != LLRenderTarget::EnsureResult::Failed);
            // Where the water draws, in the water copy's own colour: a target the refraction reads do not feed back into.
            ALRenderTargetDesc out = *mPlan.desc(Id::MainScreen);
            REQUIRE(mOut.ensure(out) != LLRenderTarget::EnsureResult::Failed);

            mWidth  = (S32)mScreen.getWidth();
            mHeight = (S32)mScreen.getHeight();
            drawScene();
            glGenQueries(1, &mQuery);
        }

        // The sky over the top 40%, and the world under it in bands of depth; the screen a colour over all of it.
        void drawScene()
        {
            LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_ALWAYS);
            LLGLSColorMask mask(true, true);
            mGBuffer.bindTarget();
            gGL.setClearColor(LLColor4(0.2f, 0.3f, 0.4f, 1.f));
            glClearDepth(1.0);
            mGBuffer.clear();
            constexpr S32 BANDS = 12;
            const S32 world = mHeight * 6 / 10;
            for (S32 band = 0; band < BANDS; ++band)
            {
                const S32 y0 = world * band / BANDS;
                const S32 y1 = world * (band + 1) / BANDS;
                LLGLSScissor scissor(0, y0, mWidth, y1 - y0);
                glClearDepth(0.2 + 0.7 * (F64)band / BANDS);
                glClear(GL_DEPTH_BUFFER_BIT);
            }
            glClearDepth(1.0);
            mGBuffer.flush();
            mScreen.bindTarget();
            gGL.setClearColor(LLColor4(1.5f, 0.8f, 0.4f, 0.25f));
            mScreen.clear(GL_COLOR_BUFFER_BIT);
            mScreen.flush();
            gGL.setClearColor(LLColor4::black);
        }

        // The GPU's time for `pass`, in milliseconds a run.
        F64 time(const std::function<void()>& pass)
        {
            for (S32 run = 0; run < RUNS; ++run)
            {
                pass();
            }
            glFinish();
            std::vector<F64> samples;
            for (S32 sample = 0; sample < SAMPLES; ++sample)
            {
                glBeginQuery(GL_TIME_ELAPSED, mQuery);
                for (S32 run = 0; run < RUNS; ++run)
                {
                    pass();
                }
                glEndQuery(GL_TIME_ELAPSED);
                GLuint64 ns = 0;
                glGetQueryObjectui64v(mQuery, GL_QUERY_RESULT, &ns);
                samples.push_back((F64)ns / 1.0e6 / RUNS);
            }
            std::nth_element(samples.begin(), samples.begin() + SAMPLES / 2, samples.end());
            return samples[SAMPLES / 2];
        }

        void report(const std::string& row, F64 ms)
        {
            WARN(row << ": " << ll_round((F32)(ms * 1000.0)) << " us");
        }

        // LLPipeline::copySceneDepth: the G-buffer's depth over the whole of `dst`'s, through the copy program.
        void copySceneDepth(LLRenderTarget& dst)
        {
            dst.bindTarget();
            {
                LLGLSColorMask no_color(false, false);
                LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_ALWAYS);
                LLGLDisable no_blend(GL_BLEND);
                LLGLDisable no_cull(GL_CULL_FACE);
                mCopyDepth.bind();
                mCopyDepth.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &mGBuffer, ALSamplers::PointClamp);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
                mCopyDepth.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
                mCopyDepth.unbind();
            }
            dst.flush();
        }

        // A haze pass over the screen, held to what the world drew as LLPipeline::doAtmospherics holds it, reading the
        // scene's depth from `depth`: a copy, or the G-buffer the screen shares it with.
        void haze(LLRenderTarget& depth)
        {
            mScreen.bindTarget();
            {
                LLGLEnable blend(GL_BLEND);
                gGL.blendFunc(LLRender::BF_ONE, LLRender::BF_SOURCE_ALPHA, LLRender::BF_ZERO, LLRender::BF_SOURCE_ALPHA);
                LLGLSColorMask mask(true, true);
                LLGLDisable no_cull(GL_CULL_FACE);
                ALDrawnOnly drawn;
                mHaze.bind();
                const S32 channel = mHaze.enableTexture(LLShaderMgr::DEFERRED_DEPTH);
                REQUIRE(channel > -1);
                gGL.getTextureSlot(channel)->bind(&depth, true);
                mHaze.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)mWidth, (F32)mHeight);
                gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
                mHaze.disableTexture(LLShaderMgr::DEFERRED_DEPTH);
                mHaze.unbind();
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
            }
            mScreen.flush();
        }

        // The water's copy of the screen over `rect`, as LLPipeline::beginWaterTargets blits it.
        void copyScreen(LLRenderTarget& dst, const LLRect& rect)
        {
            dst.copyContents(mScreen, rect.mLeft, rect.mBottom, rect.mRight, rect.mTop, rect.mLeft, rect.mBottom, rect.mRight,
                             rect.mTop, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }

        // The water's reads of the copy, over the whole of a target of the screen's size.
        void refract(LLRenderTarget& copy)
        {
            mOut.bindTarget();
            {
                LLGLDisable no_blend(GL_BLEND);
                LLGLDepthTest no_depth(GL_FALSE);
                LLGLDisable no_cull(GL_CULL_FACE);
                mRefraction.bind();
                mRefraction.bindTexture(LLShaderMgr::WATER_SCREENTEX, &copy);
                mRefraction.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)mWidth, (F32)mHeight);
                gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
                mRefraction.unbindTexture(LLShaderMgr::WATER_SCREENTEX);
                mRefraction.unbind();
            }
            mOut.flush();
        }

        // The field's screen rectangle from a camera 20 m above the sea, `pitch` radians below level, with the margin
        // the default scale above the water (0.03) asks for.
        LLRect fieldRect(F32 pitch)
        {
            const LLVector3 eye(0.f, 0.f, 20.f);
            LLVector3 f(cosf(pitch), 0.f, -sinf(pitch));
            LLVector3 s = f % LLVector3::z_axis;
            s.normalize();
            const LLVector3 u = s % f;
            const F32 m[16] = { s.mV[0], u.mV[0], -f.mV[0], 0.f, s.mV[1], u.mV[1], -f.mV[1], 0.f,
                                s.mV[2], u.mV[2], -f.mV[2], 0.f, -(s * eye), -(u * eye), f * eye, 1.f };
            LLMatrix4a view;
            view.loadu(m);
            const LLMatrix4a proj = LLMatrix4a::perspective(60.f * DEG_TO_RAD, (F32)mWidth / (F32)mHeight, 0.5f, 1000.f);
            return ALWaterField::screenRect(view, proj, LLVector3(-262144.f, -262144.f, -1.f), LLVector3(262144.f, 262144.f, 1.f),
                                            mWidth, mHeight, (S32)std::ceil(0.03f * mWidth) + 1, (S32)std::ceil(0.03f * mHeight) + 1);
        }

        ll_test::SharedGLScope    mGL;
        ll_test::TestShaderMgr    mShaders;
        Plan                      mPlan;
        LLGLSLShader              mCopyDepth;
        LLGLSLShader              mHaze;
        LLGLSLShader              mRefraction;
        LLPointer<LLVertexBuffer> mTriangle;
        LLRenderTarget            mGBuffer;
        LLRenderTarget            mScreen;
        LLRenderTarget            mDepthCopy;
        LLRenderTarget            mWater;
        LLRenderTarget            mWaterWide;
        LLRenderTarget            mOut;
        S32                       mWidth  = 0;
        S32                       mHeight = 0;
        GLuint                    mQuery  = 0;
    };

    TEST_CASE_METHOD(alwatertargets_bench_data, "alwatertargets_bench: the haze passes' depth, copied or read in place", "[alwatertargets_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        setUp();
        const F64 copy     = time([&]() { copySceneDepth(mDepthCopy); });
        const F64 copied   = time([&]() { copySceneDepth(mDepthCopy); haze(mDepthCopy); });
        const F64 in_place = time([&]() { haze(mGBuffer); });
        report("the scene's depth copied, 2560x1440 D32F_S8 to D32F", copy);
        report("a haze pass over a copy of its own, the copy included", copied);
        report("a haze pass reading the scene's depth in place", in_place);
        report("the frame's two haze passes, saved", 2.0 * (copied - in_place));
    }

    TEST_CASE_METHOD(alwatertargets_bench_data, "alwatertargets_bench: the water's copy of the screen, whole or over the field", "[alwatertargets_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        setUp();
        const LLRect whole(0, mHeight, mWidth, 0);
        const LLRect level = fieldRect(0.f);
        const LLRect down  = fieldRect(20.f * DEG_TO_RAD);
        WARN("the field's rectangle, level " << level << ", 20 degrees down " << down);
        report("the copy of the screen, whole", time([&]() { copyScreen(mWater, whole); }));
        report("the copy of the screen over the field, looking level", time([&]() { copyScreen(mWater, level); }));
        report("the copy of the screen over the field, 20 degrees down", time([&]() { copyScreen(mWater, down); }));
    }

    TEST_CASE_METHOD(alwatertargets_bench_data, "alwatertargets_bench: the water's copy of the screen as it was or as the plan has it", "[alwatertargets_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        setUp();
        const LLRect whole(0, mHeight, mWidth, 0);
        const LLRect level = fieldRect(0.f);
        WARN("the plan's copy: colour format " << std::hex << mPlan.desc(Id::MainWater)->color[0] << std::dec
                                               << (mPlan[Id::MainWater].mDepthFrom ? ", over the scene's depth" : ", no depth"));
        report("RGBA16F over the scene's depth, the copy whole", time([&]() { copyScreen(mWaterWide, whole); }));
        report("the plan's, the copy whole", time([&]() { copyScreen(mWater, whole); }));
        report("RGBA16F over the scene's depth, the copy over the field, looking level", time([&]() { copyScreen(mWaterWide, level); }));
        report("the plan's, the copy over the field, looking level", time([&]() { copyScreen(mWater, level); }));
        report("RGBA16F over the scene's depth, the water's reads over the whole screen", time([&]() { refract(mWaterWide); }));
        report("the plan's, the water's reads over the whole screen", time([&]() { refract(mWater); }));
    }
}
