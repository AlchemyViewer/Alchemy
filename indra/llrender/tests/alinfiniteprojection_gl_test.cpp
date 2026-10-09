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
#include "../llrender.h"
#include "llcamera.h"

#include "llheadlessgl_fixture.h"

#include "../test/lltut.h"

#include <cmath>
#include <limits>

// Numbers and bounds from an independent double-precision reference of the projection.
namespace tut
{
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
    }

    struct alinfiniteprojection_data
    {
        static ll_test::HeadlessGL& gl()
        {
            static ll_test::HeadlessGL instance(true, true, true, false);
            return instance;
        }

        alinfiniteprojection_data()
        {
            gl();
            LLRender::sReverseZ = true;
        }

        ~alinfiniteprojection_data()
        {
            LLRender::sReverseZ = false;
            if (mReady)
            {
                glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
                glClearDepth(1.0);
                glDepthFunc(GL_LESS);
                glDisable(GL_DEPTH_TEST);
                glViewport(0, 0, ll_test::HeadlessGL::WIDTH, ll_test::HeadlessGL::HEIGHT);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
                glUseProgram(0);
                glDeleteProgram(mProgram);
            }
        }

        // A D32F depth-only target, reversed clip control, cleared to the far value 0, GREATER.
        void setUp()
        {
            if (!gGLManager.mHasClipControl)
            {
                skip("no clip control: reverse-Z is unavailable on this context");
            }

            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            ensure("shaders compile", vs && fs);
            mProgram = glCreateProgram();
            glAttachShader(mProgram, vs);
            glAttachShader(mProgram, fs);
            glLinkProgram(mProgram);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint linked = GL_FALSE;
            glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
            ensure("program links", linked == GL_TRUE);

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
            ensure_equals("complete", (U32)glCheckFramebufferStatus(GL_FRAMEBUFFER), (U32)GL_FRAMEBUFFER_COMPLETE);

            mReady = true;
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

        GLuint mFBO = 0;
        GLuint mDepth = 0;
        GLuint mVAO = 0;
        GLuint mProgram = 0;
        bool mReady = false;
    };

    typedef test_group<alinfiniteprojection_data> alinfiniteprojection_t;
    typedef alinfiniteprojection_t::object alinfiniteprojection_object_t;
    tut::alinfiniteprojection_t tut_alinfiniteprojection("alinfiniteprojection_gl");

    // The matrix: recognised as infinite, stores near / distance, and its inverse gives w = 0 at the cleared
    // depth, which the reconstruction floor turns into MAX_RECONSTRUCT_DISTANCE. Finite and forward matrices are not.
    template<> template<>
    void alinfiniteprojection_object_t::test<1>()
    {
        const LLMatrix4a proj = infinite();
        ensure("infinite", al_projection_is_infinite(proj));
        ensure("finite reversed is not", !al_projection_is_infinite(al_perspective(FOV, 1.f, NEAR_PLANE, 1024.f)));
        ensure("ortho is not", !al_projection_is_infinite(al_ortho(-1.f, 1.f, -1.f, 1.f, 0.f, 10.f)));

        for (F32 dist : DISTANCES)
        {
            LLVector4a clip;
            proj.transform4(LLVector4a(0.f, 0.f, -dist, 1.f), clip);
            ensure_equals("clip z is the near plane", clip[2], NEAR_PLANE);
            ensure_equals("clip w is the distance", clip[3], dist);
        }

        LLMatrix4a inv = proj;
        inv.invert();
        LLVector4a far_point;
        inv.transform4(LLVector4a(0.f, 0.f, 0.f, 1.f), far_point);
        ensure("cleared depth unprojects to w = 0", fabsf(far_point[3]) < 1e-12f);
        ensure_approximately_equals("floored to MAX_RECONSTRUCT_DISTANCE", reconstruct(inv, 0.f), MAX_RECONSTRUCT_DISTANCE, 8);

        // The floor leaves a finite projection alone: its cleared depth still reconstructs at its far plane.
        LLMatrix4a inv_finite = al_perspective(FOV, 1.f, NEAR_PLANE, 1024.f);
        inv_finite.invert();
        LLVector4a raw;
        inv_finite.transform4(LLVector4a(0.f, 0.f, 0.f, 1.f), raw);
        ensure_equals("finite far untouched by the floor", reconstruct(inv_finite, 0.f), -raw[2] / raw[3]);
        ensure("finite far is about 1024 m", fabsf(-raw[2] / raw[3] - 1024.f) < 1.f);

        LLRender::sReverseZ = false;
        const LLMatrix4a forward = al_perspective(FOV, 1.f, NEAR_PLANE, std::numeric_limits<F32>::infinity());
        ensure("forward falls back to a finite plane", !al_projection_is_infinite(forward) && std::isfinite(forward.getColumn<2>()[2]));
        LLRender::sReverseZ = true;
    }

    // Forward-Z cannot hold an infinite plane in 24-bit depth, so an infinite request falls back to the old
    // fixed plane rather than MAX_FAR_PLANE, and depth beyond it is clipped as before.
    template<> template<>
    void alinfiniteprojection_object_t::test<4>()
    {
        LLRender::sReverseZ = false;
        const LLMatrix4a forward = al_perspective(FOV, 1.f, NEAR_PLANE, std::numeric_limits<F32>::infinity());
        const LLMatrix4a legacy = al_perspective(FOV, 1.f, NEAR_PLANE, FINITE_PROJECTION_FAR);
        LLRender::sReverseZ = true;
        for (S32 i = 0; i < 16; ++i)
        {
            ensure_equals("same as the old fixed plane", forward.getF32ptr()[i], legacy.getF32ptr()[i]);
        }
        LLVector4a clip;
        forward.transform4(LLVector4a(0.f, 0.f, -FINITE_PROJECTION_FAR, 1.f), clip);
        ensure("far plane at the old fixed plane", fabsf(clip[2] / clip[3] - 1.f) < 1e-5f);
        forward.transform4(LLVector4a(0.f, 0.f, -2.f * FINITE_PROJECTION_FAR, 1.f), clip);
        ensure("past it is clipped", clip[2] / clip[3] > 1.f);
    }

    // Draw at each distance: the stored depth is near / distance (oracle), nothing is clipped far, and the
    // reconstructed distance is within the oracle's bound (0.29 mm, 3.9 mm, 9.3 mm, 45 mm).
    template<> template<>
    void alinfiniteprojection_object_t::test<2>()
    {
        setUp();
        const LLMatrix4a proj = infinite();
        LLMatrix4a inv = proj;
        inv.invert();
        const F32 oracle_depth[] = { 1.0e-4f, 1.2207031e-5f, 3.125e-6f, 1.0e-6f };

        for (size_t i = 0; i < std::size(DISTANCES); ++i)
        {
            const F32 dist = DISTANCES[i];
            const std::string at = std::to_string((S32)dist) + " m";
            glClear(GL_DEPTH_BUFFER_BIT);
            draw(proj, dist);
            const F32 d = centreDepth();
            ensure("drawn at " + at, d > 0.f);
            ensure("stored depth at " + at, fabsf(d - oracle_depth[i]) <= oracle_depth[i] * 1e-6f);
            const F64 err = fabs((F64)reconstruct(inv, d) - dist);
            ensure("round trip at " + at + ": " + std::to_string(err * 1000.0) + " mm", err <= bound(dist, d));
        }

        // Past every reach and still not clipped: 1000 km.
        glClear(GL_DEPTH_BUFFER_BIT);
        draw(proj, 1.0e6f);
        ensure("1000 km is not clipped", centreDepth() > 0.f);
        ensure_equals("no GL error", (U32)glGetError(), (U32)GL_NO_ERROR);
    }

    // Two surfaces four depth steps apart at each distance: the nearer one wins whichever is drawn first.
    template<> template<>
    void alinfiniteprojection_object_t::test<3>()
    {
        setUp();
        const LLMatrix4a proj = infinite();

        for (F32 dist : DISTANCES)
        {
            const std::string at = std::to_string((S32)dist) + " m";
            const F32 gap = (F32)(4.0 * depthStep(NEAR_PLANE / dist)) + (std::nextafter(dist, 2.f * dist) - dist);
            const F32 nearer = dist - gap;

            glClear(GL_DEPTH_BUFFER_BIT);
            draw(proj, dist);
            const F32 d_far = centreDepth();
            draw(proj, nearer);
            const F32 d_near = centreDepth();
            ensure("nearer drawn second wins at " + at, d_near > d_far);

            glClear(GL_DEPTH_BUFFER_BIT);
            draw(proj, nearer);
            draw(proj, dist);
            ensure_equals("farther drawn second loses at " + at, centreDepth(), d_near);
        }
        ensure_equals("no GL error", (U32)glGetError(), (U32)GL_NO_ERROR);
    }

    // The sky draws last, on the far plane, writing no depth, and the depth test spares it every pixel the world
    // covered (skyV.glsl). Under the infinite projection the far plane is the cleared depth exactly and geometry as
    // far out as reconstruction reaches still stores more, so the sky passes where nothing was drawn and nowhere else.
    template<> template<>
    void alinfiniteprojection_object_t::test<5>()
    {
        setUp();
        const LLMatrix4a proj = infinite();
        GLuint query = 0;
        glGenQueries(1, &query);
        const auto sky_samples = [&]()
        {
            glDepthFunc(GL_GEQUAL);
            glDepthMask(GL_FALSE);
            glBeginQuery(GL_SAMPLES_PASSED, query);
            draw(proj, 1000.f, true);
            glEndQuery(GL_SAMPLES_PASSED);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_GREATER);
            GLuint samples = 0;
            glGetQueryObjectuiv(query, GL_QUERY_RESULT, &samples);
            return samples;
        };

        glClear(GL_DEPTH_BUFFER_BIT);
        ensure_equals("the sky fills an empty frame", sky_samples(), (GLuint)(SIZE * SIZE));
        ensure_equals("the sky leaves the cleared depth", centreDepth(), 0.f);

        for (F32 dist : { 1000.f, 100000.f, MAX_RECONSTRUCT_DISTANCE })
        {
            const std::string at = std::to_string((S32)dist) + " m";
            glClear(GL_DEPTH_BUFFER_BIT);
            draw(proj, dist);
            ensure("geometry at " + at + " stores more than the far plane", centreDepth() > 0.f);
            ensure_equals("the sky stays behind geometry at " + at, sky_samples(), (GLuint)0);
        }

        glDeleteQueries(1, &query);
        ensure_equals("no GL error", (U32)glGetError(), (U32)GL_NO_ERROR);
    }
}
