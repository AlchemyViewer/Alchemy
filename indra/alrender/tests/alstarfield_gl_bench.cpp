/**
 * @file alstarfield_gl_bench.cpp
 * @brief Two ways to draw the points of the stars a frame keeps, on the hidden window: a prefix of each cell in view,
 *        one run of glMultiDrawArrays a cell (ALStarField::pointDraws), or every point of the cells in view, the runs
 *        that meet merged, with the vertex stage leaving out the stars under the frame's faintest.
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

// One sample is one frame's point draw at the default count, through a 60-degree 16:9 view 20 degrees up or a
// 100-degree one 60 degrees up, at 1920x1080 into an R11G11B10F attachment, as ALSky::renderStars draws it on the
// emissive path: the CPU's time to work the runs out and to submit them once, and the GPU's to draw them once, from a
// GL_TIME_ELAPSED query around REPEATS draws of them. Each row is the median of 31 samples after a warm-up, with the
// samples' spread.

#include "linden_common.h"

#include "../alstarfield.h"
#include "alskyraster_gl.h"

#include "altestbenchmark.h"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr S32 W        = 1920;
    constexpr S32 H        = 1080;
    constexpr S32 SAMPLES  = 31;
    constexpr S32 WARMUP   = 5;
    constexpr S32 REPEATS  = 8;

    F64 ms_since(Clock::time_point start)
    {
        return std::chrono::duration<F64, std::milli>(Clock::now() - start).count();
    }

    // The median and the samples' spread, in microseconds.
    std::string summary(std::vector<F64> samples)
    {
        std::sort(samples.begin(), samples.end());
        return fmt::format("{:.1f} us ({:.1f}-{:.1f})", samples[samples.size() / 2] * 1000.0,
                           samples.front() * 1000.0, samples.back() * 1000.0);
    }

    // starsV.glsl with every star under the uniform faintest_byte left out first thing, outside the clip volume.
    std::string thresholdVertex(std::string source)
    {
        const size_t main_at = source.find("void main()");
        REQUIRE(main_at != std::string::npos);
        const size_t open = source.find('{', main_at);
        REQUIRE(open != std::string::npos);
        source.insert(open + 1, R"(
    if (float(uint(texture_index) >> 24u) < faintest_byte)
    {
        gl_Position     = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize    = 1.0;
        vary_point      = vec3(0.0, 0.0, 1.0);
        vary_star_color = vec3(0.0);
        vary_spike      = 0.0;
        return;
    }
)");
        source.insert(main_at, "uniform float faintest_byte;\n\n");
        return source;
    }

    struct alstarfield_gl_bench_data
    {
        alstarfield_gl_bench_data()
        {
            mTree.populate();
            std::error_code ec;
            const std::string vertex = LLFile::getContents(std::string(AL_TEST_SHADER_DIR) + "/class1/deferred/starsV.glsl", ec);
            REQUIRE(!ec);
            REQUIRE(mTree.write("deferred/starsThresholdV.glsl", thresholdVertex(vertex)));
            mShaders = std::make_unique<ll_test::TestShaderMgr>(mTree.prefix());
            al_sky_test::loadSharedObjects(*mShaders);

            build(mPrefix, "deferred/starsV.glsl");
            build(mThreshold, "deferred/starsThresholdV.glsl");
            mFaintestLocation = glGetUniformLocation(mThreshold.mProgramObject, "faintest_byte");
            REQUIRE(mFaintestLocation >= 0);

            mStars = ALStarField::generate(ALStarField::DEFAULT_COUNT, al_sky_test::DOME_RADIUS);
            mLayout = ALStarField::layOut(mStars);
            mBuffer = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXTURE_INDEX);
            REQUIRE(mBuffer->allocateBuffer(mLayout.vertices(), 0));
            LLStrider<LLVector4a> vertices;
            REQUIRE(mBuffer->getVertexStrider(vertices));
            ALStarField::fillVertices(mStars, mLayout, vertices);
            mBuffer->unmapBuffer();

            REQUIRE(mTarget.allocate(W, H, GL_RGBA8));
            REQUIRE(mTarget.addColorAttachment(GL_RGBA8));
            REQUIRE(mTarget.addColorAttachment(GL_RGB10_A2));
            REQUIRE(mTarget.addColorAttachment(GL_R11F_G11F_B10F));
            glGenQueries(1, &mQuery);
        }

        ~alstarfield_gl_bench_data()
        {
            LLGLSLShader::unbind();
            mPrefix.unload();
            mThreshold.unload();
            mShaders->clearShaderObjects();
            mTarget.release();
            mBuffer = nullptr;
            glDeleteQueries(1, &mQuery);
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
        }

        void build(LLGLSLShader& program, const char* vertex)
        {
            program.mName = "Deferred Star Program (bench)";
            program.mShaderFiles.clear();
            program.mShaderFiles.push_back(std::make_pair(vertex, GL_VERTEX_SHADER));
            program.mShaderFiles.push_back(std::make_pair("deferred/valueNoiseUtil.glsl", GL_VERTEX_SHADER));
            program.mShaderFiles.push_back(std::make_pair("deferred/starsF.glsl", GL_FRAGMENT_SHADER));
            program.mShaderLevel = 1;
            program.mShaderGroup = LLGLSLShader::SG_SKY;
            program.addPermutation("HAS_EMISSIVE", "1");
            UNSCOPED_INFO("the star program builds from " << vertex);
            REQUIRE(program.createShader());
        }

        // The run's draws, through the bound program: the ranges, REPEATS times.
        void draw(const ALStarField::Draws& draws)
        {
            mBuffer->setBuffer();
            for (S32 r = 0; r < REPEATS; ++r)
            {
                mBuffer->drawMultiArrays(LLRender::POINTS, draws.mFirst.data(), draws.mCount.data(),
                                         (U32)draws.mFirst.size());
            }
        }

        // One way's samples through @a view at @a faintest: by prefix where @a prefix, or whole cells left to the
        // vertex stage.
        void sample(const al_sky_test::View& view, bool prefix, U32 faintest, std::vector<F64>& cpu,
                    std::vector<F64>& gpu, size_t& runs, U32& points)
        {
            mTarget.bindTarget();
            mTarget.clear();
            al_sky_test::loadView(view, W, H);
            LLGLEnable    blend(GL_BLEND);
            LLGLDisable   cull(GL_CULL_FACE);
            LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
            LLGLEnable    point_size(GL_PROGRAM_POINT_SIZE);
            gGL.setSceneBlendType(LLRender::BT_ADD);
            mTarget.setDrawAttachment(3);
            gGL.pushMatrix();
            gGL.translatef(al_sky_test::CAMERA.mV[VX], al_sky_test::CAMERA.mV[VY], al_sky_test::CAMERA.mV[VZ]);
            gGL.rotatef(17.f, 0.f, 0.f, 1.f);

            LLGLSLShader& program = prefix ? mPrefix : mThreshold;
            program.bind();
            program.uniform1f(LLShaderMgr::CUSTOM_ALPHA, 0.5f);
            program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)W, (F32)H);
            program.uniform1f(LLShaderMgr::WATER_TIME, 1234.5f);
            program.uniform3f(LLShaderMgr::DEFERRED_MOON_DIR, 0.f, 0.f, -1.f);
            program.uniform1f(LLShaderMgr::MOON_BRIGHTNESS, 0.f);
            program.uniform3f(LLShaderMgr::MOON_QUAD_CENTER, 0.f, 0.f, 0.f);
            if (!prefix)
            {
                glUniform1f(mFaintestLocation, (F32)faintest);
            }

            LLMatrix4a mvp;
            mvp.setMul(gGL.getModelviewMatrix(), gGL.getProjectionMatrix());
            const ALStarField::Frustum frustum = ALStarField::pointFrustum(mvp, W, H);
            for (S32 s = -WARMUP; s < SAMPLES; ++s)
            {
                glFinish();
                const Clock::time_point start = Clock::now();
                ALStarField::pointDraws(mLayout, frustum, prefix ? faintest : 0, mDraws);
                const F64 cull_ms = ms_since(start);
                const Clock::time_point submit = Clock::now();
                glBeginQuery(GL_TIME_ELAPSED, mQuery);
                draw(mDraws);
                glEndQuery(GL_TIME_ELAPSED);
                const F64 submit_ms = ms_since(submit);
                GLuint64 ns = 0;
                glGetQueryObjectui64v(mQuery, GL_QUERY_RESULT, &ns);
                if (s >= 0)
                {
                    cpu.push_back(cull_ms + submit_ms / REPEATS);
                    gpu.push_back((F64)ns / 1.0e6 / REPEATS);
                }
            }
            runs = mDraws.mFirst.size();
            points = 0;
            for (S32 count : mDraws.mCount)
            {
                points += (U32)count;
            }

            program.unbind();
            gGL.popMatrix();
            mTarget.setDrawBuffers();
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            mTarget.flush();
        }

        void report(const char* name, const al_sky_test::View& view, U32 faintest)
        {
            std::vector<F64> cpu[2];
            std::vector<F64> gpu[2];
            size_t runs[2] = {};
            U32 points[2] = {};
            for (S32 way = 0; way < 2; ++way)
            {
                sample(view, way == 0, faintest, cpu[way], gpu[way], runs[way], points[way]);
            }
            WARN(fmt::format("{}, faintest {}:\n  by prefix:   cpu {}, gpu {}, {} runs, {} points\n"
                             "  whole cells: cpu {}, gpu {}, {} runs, {} points",
                             name, faintest, summary(cpu[0]), summary(gpu[0]), runs[0], points[0], summary(cpu[1]),
                             summary(gpu[1]), runs[1], points[1]));
        }

        ll_test::SharedGLScope                  mGL;
        al_sky_test::SkyShaderTree              mTree;
        std::unique_ptr<ll_test::TestShaderMgr> mShaders;
        LLGLSLShader                            mPrefix;
        LLGLSLShader                            mThreshold;
        GLint                                   mFaintestLocation = -1;
        std::vector<ALStarField::Star>          mStars;
        ALStarField::Layout                     mLayout;
        ALStarField::Draws                      mDraws;
        LLPointer<LLVertexBuffer>               mBuffer;
        LLRenderTarget                          mTarget;
        GLuint                                  mQuery = 0;
    };

    TEST_CASE_METHOD(alstarfield_gl_bench_data, "alstarfield_gl_bench: the points by prefix against whole cells and a threshold",
                     "[alstarfield_gl_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        // Night keeps every star; at dusk a star brightness of 0.03, 0.01 and 0.003 keeps those at about these bytes
        // and brighter on an 8-bit attachment (ALStarField::faintestVisible).
        for (const F32 alpha : { 0.03f, 0.01f, 0.003f })
        {
            WARN(fmt::format("star brightness {}: faintest {}", alpha, ALStarField::faintestVisible(alpha, 0.f, true)));
        }
        for (const U32 faintest : { 0u, 20u, 40u, 80u, 140u })
        {
            report("level, 60 degrees", { 30.f, 20.f, 60.f }, faintest);
            report("up, 100 degrees", { 75.f, 60.f, 100.f }, faintest);
        }
    }
}
