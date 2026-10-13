/**
 * @file alinfiniteprojection_gl_test.cpp
 * @brief The infinite reverse-Z projection on the hidden window: stored depth, the round trip
 *        back to a view distance, and depth order between surfaces a few depth steps apart,
 *        at 1, 8, 32 and 100 km.
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

#include "linden_common.h"

#include "../llgl.h"
#include "../llglheaders.h"
#include "../llglstates.h"
#include "../llrender.h"
#include "llcamera.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

#include <cmath>
#include <limits>

// Numbers and bounds from an independent double-precision reference of the projection.
namespace
{
        constexpr S32 SIZE = 8;
        constexpr F32 NEAR_PLANE = 0.1f;
        constexpr F32 FOV = 1.0f;
        constexpr F32 DISTANCES[] = { 1000.f, 8192.f, 32000.f, 100000.f };

        // A triangle covering the viewport at view distance `dist`, placed in eye space so the projection
        // under test is what puts it on screen; on the reverse-Z far plane instead when `far_plane` is set, as
        // skyV.glsl puts the sky.
        const char* kVertex =
            "#version 400\n"
            "uniform mat4 proj;\n"
            "uniform float dist;\n"
            "uniform vec2 inv_scale;\n"
            "uniform int far_plane;\n"
            "void main()\n"
            "{\n"
            "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;\n"
            "    gl_Position = proj * vec4(p * inv_scale * dist, -dist, 1.0);\n"
            "    if (far_plane != 0) gl_Position.z = 0.0;\n"
            "}\n";

        const char* kFragment =
            "#version 400\n"
            "void main() { }\n";

        LLMatrix4a infinite()
        {
            return al_perspective(FOV, 1.f, NEAR_PLANE, std::numeric_limits<F32>::infinity());
        }

        // What deferredUtil.glsl's getPositionWithNDC does, in float: inv_proj * (0, 0, d, 1), w floored.
        F32 reconstruct(const LLMatrix4a& inv, F32 d)
        {
            LLVector4a pos;
            inv.transform4(LLVector4a(0.f, 0.f, d, 1.f), pos);
            return -pos[2] / llmax(pos[3], 1.f / MAX_RECONSTRUCT_DISTANCE);
        }

        // True metres between the depth stored for `dist` and the next representable (farther) one.
        F64 depthStep(F32 d)
        {
            const F32 next = std::nextafter(d, 0.f);
            return (F64)NEAR_PLANE / next - (F64)NEAR_PLANE / d;
        }

        F64 bound(F32 dist, F32 d)
        {
            return llmax(4.0 * depthStep(d), 4.0 * (F64)(std::nextafter(dist, 2.f * dist) - dist));
        }

    struct alinfiniteprojection_data
    {
        alinfiniteprojection_data()
        {
            LLRender::sReverseZ = true;
        }

        // The convention, clip control, depth state, viewport, target and
        // program the tests change go back with mGL.
        ~alinfiniteprojection_data()
        {
            if (mQuery)
            {
                glDeleteQueries(1, &mQuery);
            }
            if (mFBO)
            {
                glDeleteFramebuffers(1, &mFBO);
            }
            if (mDepth)
            {
                glDeleteTextures(1, &mDepth);
            }
            if (mVAO)
            {
                glDeleteVertexArrays(1, &mVAO);
            }
            if (mProgram)
            {
                glDeleteProgram(mProgram);
            }
        }

        // A D32F depth-only target, reversed clip control, cleared to the far value 0, GREATER.
        void setUp()
        {
            if (!gGLManager.mHasClipControl)
            {
                SKIP("no clip control: reverse-Z is unavailable on this context");
            }

            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            {
                INFO("shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mProgram = glCreateProgram();
            glAttachShader(mProgram, vs);
            glAttachShader(mProgram, fs);
            glLinkProgram(mProgram);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint linked = GL_FALSE;
            glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
            REQUIRE(linked == GL_TRUE);

            glGenVertexArrays(1, &mVAO);
            glGenTextures(1, &mDepth);
            glBindTexture(GL_TEXTURE_2D, mDepth);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SIZE, SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D, 0);
            glGenFramebuffers(1, &mFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepth, 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            REQUIRE((U32)glCheckFramebufferStatus(GL_FRAMEBUFFER) == (U32)GL_FRAMEBUFFER_COMPLETE);

            glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
            glViewport(0, 0, SIZE, SIZE);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_GREATER);
            glClearDepth(0.0);
            glClear(GL_DEPTH_BUFFER_BIT);
        }

        void draw(const LLMatrix4a& proj, F32 dist, bool far_plane = false)
        {
            glUseProgram(mProgram);
            glUniformMatrix4fv(glGetUniformLocation(mProgram, "proj"), 1, GL_FALSE, proj.getF32ptr());
            glUniform1f(glGetUniformLocation(mProgram, "dist"), dist);
            glUniform2f(glGetUniformLocation(mProgram, "inv_scale"), 1.f / proj.getRow<0>()[0], 1.f / proj.getRow<1>()[1]);
            glUniform1i(glGetUniformLocation(mProgram, "far_plane"), far_plane ? 1 : 0);
            glBindVertexArray(mVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glUseProgram(0);
        }

        F32 centreDepth()
        {
            F32 d = -1.f;
            glFinish();
            glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d);
            return d;
        }

        ll_test::SharedGLScope mGL;
        GLuint mFBO = 0;
        GLuint mDepth = 0;
        GLuint mVAO = 0;
        GLuint mProgram = 0;
        // A samples-passed query, for the cases that count what a pass shades.
        GLuint mQuery = 0;
    };


    // The matrix: recognised as infinite, stores near / distance, and its inverse gives w = 0 at the cleared
    // depth, which the reconstruction floor turns into MAX_RECONSTRUCT_DISTANCE. Finite and forward matrices are not.
    TEST_CASE_METHOD(alinfiniteprojection_data, "alinfiniteprojection_gl: the infinite matrix stores near over distance and its far end is floored", "[alinfiniteprojection_gl]")
    {
        const LLMatrix4a proj = infinite();
        CHECK(al_projection_is_infinite(proj));
        UNSCOPED_INFO("finite reversed is not");
        CHECK_FALSE(al_projection_is_infinite(al_perspective(FOV, 1.f, NEAR_PLANE, 1024.f)));
        CHECK_FALSE(al_projection_is_infinite(al_ortho(-1.f, 1.f, -1.f, 1.f, 0.f, 10.f)));

        for (F32 dist : DISTANCES)
        {
            CAPTURE(dist);
            LLVector4a clip;
            proj.transform4(LLVector4a(0.f, 0.f, -dist, 1.f), clip);
            REQUIRE(clip[2] == NEAR_PLANE);
            REQUIRE(clip[3] == dist);
        }

        LLMatrix4a inv = proj;
        inv.invert();
        LLVector4a far_point;
        inv.transform4(LLVector4a(0.f, 0.f, 0.f, 1.f), far_point);
        UNSCOPED_INFO("cleared depth unprojects to w = 0");
        CHECK(fabsf(far_point[3]) < 1e-12f);
        CHECK_THAT(reconstruct(inv, 0.f), ll_test::WithinBits(MAX_RECONSTRUCT_DISTANCE, 8));

        // The floor leaves a finite projection alone: its cleared depth still reconstructs at its far plane.
        LLMatrix4a inv_finite = al_perspective(FOV, 1.f, NEAR_PLANE, 1024.f);
        inv_finite.invert();
        LLVector4a raw;
        inv_finite.transform4(LLVector4a(0.f, 0.f, 0.f, 1.f), raw);
        CHECK(reconstruct(inv_finite, 0.f) == -raw[2] / raw[3]);
        CHECK(fabsf(-raw[2] / raw[3] - 1024.f) < 1.f);

        LLRender::sReverseZ = false;
        const LLMatrix4a forward = al_perspective(FOV, 1.f, NEAR_PLANE, std::numeric_limits<F32>::infinity());
        CHECK_FALSE(al_projection_is_infinite(forward));
        CHECK(std::isfinite(forward.getColumn<2>()[2]));
        LLRender::sReverseZ = true;
    }

    // Draw at each distance: the stored depth is near / distance (oracle), nothing is clipped far, and the
    // reconstructed distance is within the oracle's bound (0.29 mm, 3.9 mm, 9.3 mm, 45 mm).
    TEST_CASE_METHOD(alinfiniteprojection_data, "alinfiniteprojection_gl: drawn depth is near over distance and round-trips within the bound", "[alinfiniteprojection_gl]")
    {
        setUp();
        const LLMatrix4a proj = infinite();
        LLMatrix4a inv = proj;
        inv.invert();
        const F32 oracle_depth[] = { 1.0e-4f, 1.2207031e-5f, 3.125e-6f, 1.0e-6f };

        for (size_t i = 0; i < std::size(DISTANCES); ++i)
        {
            const F32 dist = DISTANCES[i];
            CAPTURE(dist);
            glClear(GL_DEPTH_BUFFER_BIT);
            draw(proj, dist);
            const F32 d = centreDepth();
            UNSCOPED_INFO("drawn");
            REQUIRE(d > 0.f);
            REQUIRE(fabsf(d - oracle_depth[i]) <= oracle_depth[i] * 1e-6f);
            const F64 err = fabs((F64)reconstruct(inv, d) - dist);
            UNSCOPED_INFO("round trip error " << err * 1000.0 << " mm");
            REQUIRE(err <= bound(dist, d));
        }

        // Past every reach and still not clipped: 1000 km.
        glClear(GL_DEPTH_BUFFER_BIT);
        draw(proj, 1.0e6f);
        UNSCOPED_INFO("1000 km is not clipped");
        CHECK(centreDepth() > 0.f);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // Two surfaces four depth steps apart at each distance: the nearer one wins whichever is drawn first.
    TEST_CASE_METHOD(alinfiniteprojection_data, "alinfiniteprojection_gl: surfaces four depth steps apart resolve in either draw order", "[alinfiniteprojection_gl]")
    {
        setUp();
        const LLMatrix4a proj = infinite();

        for (F32 dist : DISTANCES)
        {
            CAPTURE(dist);
            const F32 gap = (F32)(4.0 * depthStep(NEAR_PLANE / dist)) + (std::nextafter(dist, 2.f * dist) - dist);
            const F32 nearer = dist - gap;

            glClear(GL_DEPTH_BUFFER_BIT);
            draw(proj, dist);
            const F32 d_far = centreDepth();
            draw(proj, nearer);
            const F32 d_near = centreDepth();
            UNSCOPED_INFO("nearer drawn second wins");
            REQUIRE(d_near > d_far);

            glClear(GL_DEPTH_BUFFER_BIT);
            draw(proj, nearer);
            draw(proj, dist);
            UNSCOPED_INFO("farther drawn second loses");
            REQUIRE(centreDepth() == d_near);
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // Forward-Z cannot hold an infinite plane in 24-bit depth, so an infinite request falls back to the old
    // fixed plane rather than MAX_FAR_PLANE, and depth beyond it is clipped as before.
    TEST_CASE_METHOD(alinfiniteprojection_data, "alinfiniteprojection_gl: forward-Z falls back to the old fixed plane and clips past it", "[alinfiniteprojection_gl]")
    {
        LLRender::sReverseZ = false;
        const LLMatrix4a forward = al_perspective(FOV, 1.f, NEAR_PLANE, std::numeric_limits<F32>::infinity());
        const LLMatrix4a legacy = al_perspective(FOV, 1.f, NEAR_PLANE, FINITE_PROJECTION_FAR);
        LLRender::sReverseZ = true;
        for (S32 i = 0; i < 16; ++i)
        {
            CAPTURE(i);
            REQUIRE(forward.getF32ptr()[i] == legacy.getF32ptr()[i]);
        }
        LLVector4a clip;
        forward.transform4(LLVector4a(0.f, 0.f, -FINITE_PROJECTION_FAR, 1.f), clip);
        UNSCOPED_INFO("far plane at the old fixed plane");
        CHECK(fabsf(clip[2] / clip[3] - 1.f) < 1e-5f);
        forward.transform4(LLVector4a(0.f, 0.f, -2.f * FINITE_PROJECTION_FAR, 1.f), clip);
        UNSCOPED_INFO("past it is clipped");
        CHECK(clip[2] / clip[3] > 1.f);
    }

    // The sky draws last, on the far plane, writing no depth, and the depth test spares it every pixel the world
    // covered (skyV.glsl). Under the infinite projection the far plane is the cleared depth exactly and geometry as
    // far out as reconstruction reaches still stores more, so the sky passes where nothing was drawn and nowhere else.
    TEST_CASE_METHOD(alinfiniteprojection_data, "alinfiniteprojection_gl: the sky on the far plane passes only where nothing was drawn", "[alinfiniteprojection_gl]")
    {
        setUp();
        const LLMatrix4a proj = infinite();
        glGenQueries(1, &mQuery);
        const auto sky_samples = [&]()
        {
            glDepthFunc(GL_GEQUAL);
            glDepthMask(GL_FALSE);
            glBeginQuery(GL_SAMPLES_PASSED, mQuery);
            draw(proj, 1000.f, true);
            glEndQuery(GL_SAMPLES_PASSED);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_GREATER);
            GLuint samples = 0;
            glGetQueryObjectuiv(mQuery, GL_QUERY_RESULT, &samples);
            return samples;
        };

        glClear(GL_DEPTH_BUFFER_BIT);
        UNSCOPED_INFO("the sky fills an empty frame");
        CHECK(sky_samples() == (GLuint)(SIZE * SIZE));
        UNSCOPED_INFO("the sky leaves the cleared depth");
        CHECK(centreDepth() == 0.f);

        for (F32 dist : { 1000.f, 100000.f, MAX_RECONSTRUCT_DISTANCE })
        {
            CAPTURE(dist);
            glClear(GL_DEPTH_BUFFER_BIT);
            draw(proj, dist);
            UNSCOPED_INFO("geometry stores more than the far plane");
            REQUIRE(centreDepth() > 0.f);
            UNSCOPED_INFO("the sky stays behind geometry");
            REQUIRE(sky_samples() == (GLuint)0);
        }

        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // The haze pass draws on the far plane too (hazeV.glsl), testing the other way (LLPipeline::doAtmospherics): only
    // in front of the far plane, so it shades every pixel geometry covers, however far out, and no pixel of sky.
    TEST_CASE_METHOD(alinfiniteprojection_data, "alinfiniteprojection_gl: the haze pass on the far plane shades all geometry and no sky", "[alinfiniteprojection_gl]")
    {
        setUp();
        const LLMatrix4a proj = infinite();
        UNSCOPED_INFO("in front of the far plane is LESS under reverse-Z");
        CHECK((U32)LLGLDepthTest::remap(GL_GREATER) == (U32)GL_LESS);
        glGenQueries(1, &mQuery);
        const auto haze_samples = [&]()
        {
            glDepthFunc(LLGLDepthTest::remap(GL_GREATER));
            glDepthMask(GL_FALSE);
            glBeginQuery(GL_SAMPLES_PASSED, mQuery);
            draw(proj, 1000.f, true);
            glEndQuery(GL_SAMPLES_PASSED);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_GREATER);
            GLuint samples = 0;
            glGetQueryObjectuiv(mQuery, GL_QUERY_RESULT, &samples);
            return samples;
        };

        glClear(GL_DEPTH_BUFFER_BIT);
        UNSCOPED_INFO("no haze on an empty frame");
        CHECK(haze_samples() == (GLuint)0);

        for (F32 dist : { 1.f, 1000.f, 100000.f, MAX_RECONSTRUCT_DISTANCE })
        {
            CAPTURE(dist);
            glClear(GL_DEPTH_BUFFER_BIT);
            draw(proj, dist);
            UNSCOPED_INFO("haze over all of the geometry");
            REQUIRE(haze_samples() == (GLuint)(SIZE * SIZE));
        }

        // Pixel by pixel: geometry over the left half only, sky over the right.
        glClear(GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, SIZE / 2, SIZE);
        draw(proj, MAX_RECONSTRUCT_DISTANCE);
        glViewport(0, 0, SIZE, SIZE);
        UNSCOPED_INFO("haze over the geometry's half alone");
        CHECK(haze_samples() == (GLuint)(SIZE * SIZE / 2));

        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // The soften pass draws on the far plane twice (softenLightV.glsl, LLPipeline::renderDeferredLighting): its lit
    // program in front of the far plane, as the haze does, and its sky program on it. Between them every pixel is
    // shaded once, by the program for what it is, however far out the geometry lies.
    TEST_CASE_METHOD(alinfiniteprojection_data, "alinfiniteprojection_gl: the soften pass shades every pixel once as lit or as sky", "[alinfiniteprojection_gl]")
    {
        setUp();
        const LLMatrix4a proj = infinite();
        glGenQueries(1, &mQuery);
        const auto samples = [&](GLenum func)
        {
            glDepthFunc(LLGLDepthTest::remap(func));
            glDepthMask(GL_FALSE);
            glBeginQuery(GL_SAMPLES_PASSED, mQuery);
            draw(proj, 1000.f, true);
            glEndQuery(GL_SAMPLES_PASSED);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_GREATER);
            GLuint passed = 0;
            glGetQueryObjectuiv(mQuery, GL_QUERY_RESULT, &passed);
            return passed;
        };

        glClear(GL_DEPTH_BUFFER_BIT);
        UNSCOPED_INFO("nothing lit on an empty frame");
        CHECK(samples(GL_GREATER) == (GLuint)0);
        UNSCOPED_INFO("all sky on an empty frame");
        CHECK(samples(GL_EQUAL) == (GLuint)(SIZE * SIZE));

        for (F32 dist : { 1.f, 1000.f, 100000.f, MAX_RECONSTRUCT_DISTANCE })
        {
            CAPTURE(dist);
            glClear(GL_DEPTH_BUFFER_BIT);
            draw(proj, dist);
            UNSCOPED_INFO("all lit over geometry");
            REQUIRE(samples(GL_GREATER) == (GLuint)(SIZE * SIZE));
            UNSCOPED_INFO("no sky over geometry");
            REQUIRE(samples(GL_EQUAL) == (GLuint)0);
        }

        // Pixel by pixel: geometry at the reconstruction floor over the left half only, sky over the right.
        glClear(GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, SIZE / 2, SIZE);
        draw(proj, MAX_RECONSTRUCT_DISTANCE);
        glViewport(0, 0, SIZE, SIZE);
        const GLuint lit = samples(GL_GREATER);
        const GLuint sky = samples(GL_EQUAL);
        UNSCOPED_INFO("lit over the geometry's half");
        CHECK(lit == (GLuint)(SIZE * SIZE / 2));
        UNSCOPED_INFO("every pixel shaded once");
        CHECK(lit + sky == (GLuint)(SIZE * SIZE));

        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }
}
