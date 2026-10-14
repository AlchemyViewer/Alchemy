/**
 * @file aldepthbounds_gl_test.cpp
 * @brief The depth bounds test on the hidden window: the full-screen passes it holds to what the world drew, against
 *        the depth test they used before, and the depths a light's sphere spans, under both depth conventions.
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

#include "linden_common.h"

#include "../aldepthbounds.h"
#include "../llgl.h"
#include "../llglheaders.h"
#include "../llglstates.h"
#include "../llrender.h"
#include "llcamera.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

// Each case lays a row of pixels down with a depth apiece, written into the depth texture as stored, so every depth is
// exactly what the case asked for, then draws one triangle over the row and reads back which pixels it reached. The
// stored depths are worked out here from the projection's own formula, not by the code under test.
namespace
{
    constexpr F32 NEAR_PLANE = 0.1f;
    constexpr F32 FOV = 1.0f;
    constexpr F32 FORWARD_FAR = 512.f;

    const char* kVertex =
        "#version 400\n"
        "uniform float far_ndc;\n"
        "void main()\n"
        "{\n"
        "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;\n"
        "    gl_Position = vec4(p, far_ndc, 1.0);\n"
        "}\n";

    const char* kFragment =
        "#version 400\n"
        "out vec4 frag_color;\n"
        "void main() { frag_color = vec4(1.0); }\n";

    // What the main view stores at view distance `dist`: near / dist under the infinite reverse-Z projection,
    // the forward finite projection's window depth otherwise.
    F64 stored_depth(bool reverse, F64 dist)
    {
        if (reverse)
        {
            return NEAR_PLANE / dist;
        }
        const F64 n = NEAR_PLANE;
        const F64 f = FORWARD_FAR;
        const F64 ndc = (f + n) / (f - n) - 2.0 * f * n / ((f - n) * dist);
        return ndc * 0.5 + 0.5;
    }

    struct aldepthbounds_data
    {
        ll_test::SharedGLScope mGL;
        GLuint mFBO = 0;
        GLuint mColor = 0;
        GLuint mDepth = 0;
        GLuint mVAO = 0;
        GLuint mProgram = 0;
        S32 mWidth = 0;
        bool mReverse = false;

        ~aldepthbounds_data()
        {
            if (mFBO)
            {
                glDeleteFramebuffers(1, &mFBO);
            }
            if (mColor)
            {
                glDeleteTextures(1, &mColor);
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

        void requireBounds()
        {
            if (!ALDepthBounds::available())
            {
                SKIP("no GL_EXT_depth_bounds_test on this context");
            }
        }

        // A row of `width` pixels under the viewer's depth for the convention: D32F with stencil under reverse-Z,
        // cleared to 0, D24 with stencil forward, cleared to 1. The tracked depth state is put on the convention.
        void setUp(bool reverse, S32 width)
        {
            if (reverse && !gGLManager.mHasClipControl)
            {
                SKIP("no clip control: reverse-Z is unavailable on this context");
            }
            mReverse = reverse;
            mWidth = width;

            if (!mProgram)
            {
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
            }

            if (mFBO)
            {
                glDeleteFramebuffers(1, &mFBO);
                glDeleteTextures(1, &mColor);
                glDeleteTextures(1, &mDepth);
            }
            glGenTextures(1, &mColor);
            glBindTexture(GL_TEXTURE_2D, mColor);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, 1, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
            glGenTextures(1, &mDepth);
            glBindTexture(GL_TEXTURE_2D, mDepth);
            if (reverse)
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH32F_STENCIL8, width, 1, 0, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, nullptr);
            }
            else
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, 1, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            glGenFramebuffers(1, &mFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mColor, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, mDepth, 0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            REQUIRE((U32)glCheckFramebufferStatus(GL_FRAMEBUFFER) == (U32)GL_FRAMEBUFFER_COMPLETE);
            glViewport(0, 0, width, 1);

            LLRender::sReverseZ = reverse;
            if (gGLManager.mHasClipControl)
            {
                glClipControl(GL_LOWER_LEFT, reverse ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
            }
            LLGLDepthTest::rebase();
        }

        // Pixel i holds depths[i], uploaded as the texture stores it. Not by scissored clears: a driver that cannot
        // clear a rectangle draws a quad at the clear depth instead (Mesa's llvmpipe), whose transform to window depth,
        // worked in float through ndc, rounds a reverse-Z depth of 1e-9 to the clear's 0.
        void lay(const std::vector<F64>& depths)
        {
            REQUIRE((S32)depths.size() == mWidth);
            glBindTexture(GL_TEXTURE_2D, mDepth);
            if (mReverse)
            {
                struct Texel
                {
                    F32 mDepth;
                    U32 mStencil;
                };
                std::vector<Texel> texels((size_t)mWidth);
                for (S32 i = 0; i < mWidth; ++i)
                {
                    texels[i] = { (F32)depths[i], 0 };
                }
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mWidth, 1, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, texels.data());
            }
            else
            {
                // Depth in the top 24 bits, stencil in the low 8.
                std::vector<U32> texels((size_t)mWidth);
                for (S32 i = 0; i < mWidth; ++i)
                {
                    texels[i] = (U32)(depths[i] * 16777215.0 + 0.5) << 8;
                }
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mWidth, 1, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, texels.data());
            }
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // The far-plane triangle over the row, under whatever depth state the caller holds; which pixels it reached.
        std::vector<bool> draw()
        {
            gGL.setClearColor(LLColor4::transparent);
            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(mProgram);
            glUniform1f(glGetUniformLocation(mProgram, "far_ndc"), mReverse ? 0.f : 1.f);
            glBindVertexArray(mVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glUseProgram(0);

            std::vector<U8> texels(mWidth);
            glFinish();
            glReadPixels(0, 0, mWidth, 1, GL_RED, GL_UNSIGNED_BYTE, texels.data());
            std::vector<bool> reached(mWidth);
            for (S32 i = 0; i < mWidth; ++i)
            {
                reached[i] = texels[i] != 0;
            }
            return reached;
        }

        // Under the bounds `proj` gives a sphere of `radius` at view distance `dist`, with the depth test off.
        std::vector<bool> drawSphere(const LLMatrix4a& proj, F32 dist, F32 radius)
        {
            F64 zmin = 0.0, zmax = 0.0;
            {
                INFO("the projection's depth is view z's alone");
                REQUIRE(ALDepthBounds::viewSpan(proj, -dist + radius, -dist - radius, zmin, zmax));
            }
            LLGLDepthTest depth(GL_FALSE, GL_FALSE);
            ALDepthBounds bounds(zmin, zmax);
            return draw();
        }

        LLMatrix4a projection() const
        {
            return al_perspective(FOV, 1.f, NEAR_PLANE, mReverse ? std::numeric_limits<F32>::infinity() : FORWARD_FAR);
        }
    };

    // ALDrawnOnly reaches what the depth test on the far plane reached before it, under reverse-Z: every pixel the
    // world drew, out as far as reconstruction reaches and past it, and none the sky holds.
    TEST_CASE_METHOD(aldepthbounds_data, "aldepthbounds_gl: under reverse-Z the drawn pixels are the depth test's, and no sky", "[aldepthbounds_gl]")
    {
        requireBounds();
        setUp(true, 8);
        const std::vector<F64> depths = {
            0.0,                                                    // sky
            stored_depth(true, 1.0e8),                              // 100,000 km out, 1e-9
            stored_depth(true, MAX_RECONSTRUCT_DISTANCE),           // as far as reconstruction reaches
            stored_depth(true, 256000.0),                           // the water's rim
            stored_depth(true, 8192.0),                             // terrain's reach
            0.5,
            1.0,                                                    // on the near plane
            0.0,                                                    // sky
        };
        lay(depths);

        std::vector<bool> reference;
        {
            LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_GREATER);
            reference = draw();
        }
        std::vector<bool> drawn;
        {
            ALDrawnOnly scope;
            {
                INFO("the scope holds the bounds");
                CHECK(scope.bounds() != nullptr);
            }
            drawn = draw();
        }

        for (size_t i = 0; i < depths.size(); ++i)
        {
            INFO("pixel " << i);
            CHECK((bool)drawn[i] == (depths[i] != 0.0));
            CHECK((bool)drawn[i] == (bool)reference[i]);
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // The same forward, where the 24-bit depth's last step short of the clear is geometry and the clear is sky.
    TEST_CASE_METHOD(aldepthbounds_data, "aldepthbounds_gl: forward the drawn pixels are the depth test's, and no sky", "[aldepthbounds_gl]")
    {
        requireBounds();
        setUp(false, 6);
        const F64 last_step = 16777214.0 / 16777215.0;
        const std::vector<F64> depths = {
            1.0,                                    // sky
            last_step,                              // the farthest depth geometry can store
            stored_depth(false, FORWARD_FAR - 1.0), // a metre short of the far plane
            0.5,
            0.0,                                    // on the near plane
            1.0,                                    // sky
        };
        lay(depths);

        std::vector<bool> reference;
        {
            LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_GREATER);
            reference = draw();
        }
        std::vector<bool> drawn;
        {
            ALDrawnOnly scope;
            drawn = draw();
        }

        for (size_t i = 0; i < depths.size(); ++i)
        {
            INFO("pixel " << i);
            CHECK((bool)drawn[i] == (depths[i] != 1.0));
            CHECK((bool)drawn[i] == (bool)reference[i]);
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A light's sphere 3 m around a point 20 m out: the bounds pass what lies within its span of view depth and
    // nothing in front of it, behind it or of the sky, under either convention.
    TEST_CASE_METHOD(aldepthbounds_data, "aldepthbounds_gl: a light's sphere keeps to its span of depth", "[aldepthbounds_gl]")
    {
        requireBounds();
        for (bool reverse : { true, false })
        {
            setUp(reverse, 7);
            const F64 sky = reverse ? 0.0 : 1.0;
            const F64 distances[] = { 16.9, 17.1, 20.0, 22.9, 23.1, 100.0 };
            std::vector<F64> depths;
            for (F64 dist : distances)
            {
                depths.push_back(stored_depth(reverse, dist));
            }
            depths.push_back(sky);
            lay(depths);

            const std::vector<bool> reached = drawSphere(projection(), 20.f, 3.f);
            const bool expected[] = { false, true, true, true, false, false, false };
            for (size_t i = 0; i < depths.size(); ++i)
            {
                INFO((reverse ? "reverse-Z" : "forward") << ", pixel " << i);
                CHECK((bool)reached[i] == expected[i]);
            }
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A sphere around the eye spans from the near plane out to its far side.
    TEST_CASE_METHOD(aldepthbounds_data, "aldepthbounds_gl: a sphere around the eye spans from the near plane out", "[aldepthbounds_gl]")
    {
        requireBounds();
        for (bool reverse : { true, false })
        {
            setUp(reverse, 5);
            const F64 sky = reverse ? 0.0 : 1.0;
            const std::vector<F64> depths = {
                reverse ? 1.0 : 0.0,           // on the near plane
                stored_depth(reverse, 0.5),
                stored_depth(reverse, 3.9),
                stored_depth(reverse, 4.1),
                sky,
            };
            lay(depths);

            // centred a metre ahead, 3 m across: from behind the eye out to 4 m
            const std::vector<bool> reached = drawSphere(projection(), 1.f, 3.f);
            const bool expected[] = { true, true, true, false, false };
            for (size_t i = 0; i < depths.size(); ++i)
            {
                INFO((reverse ? "reverse-Z" : "forward") << ", pixel " << i);
                CHECK((bool)reached[i] == expected[i]);
            }
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A span past the forward projection's far plane still leaves the sky out, and an oblique projection, whose depth
    // moves across the view, gives no span at all.
    TEST_CASE_METHOD(aldepthbounds_data, "aldepthbounds_gl: past the far plane the sky stays out, and an oblique projection has no span", "[aldepthbounds_gl]")
    {
        requireBounds();
        setUp(false, 3);
        lay({ stored_depth(false, 505.0), stored_depth(false, FORWARD_FAR - 0.5), 1.0 });
        const std::vector<bool> reached = drawSphere(projection(), FORWARD_FAR, 10.f);
        {
            INFO("geometry inside the span");
            CHECK(reached[0]);
            CHECK(reached[1]);
        }
        {
            INFO("not the sky beyond the far plane");
            CHECK(!reached[2]);
        }

        LLMatrix4a oblique = projection();
        oblique.getF32ptr()[2] = 0.25f;
        F64 zmin = -1.0, zmax = -1.0;
        CHECK(!ALDepthBounds::viewSpan(oblique, -10.f, -20.f, zmin, zmax));
        {
            INFO("nothing set");
            CHECK(zmin == -1.0);
            CHECK(zmax == -1.0);
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }
}
