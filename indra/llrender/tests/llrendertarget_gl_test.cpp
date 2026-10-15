/**
 * @file llrendertarget_gl_test.cpp
 * @brief LLRenderTarget on the hidden window: which fragment output reaches a target's colour attachment, and integer
 *        attachments allocated, cleared and drawn into, a lent one included.
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

#include "../llgl.h"
#include "../llglheaders.h"
#include "../llglstates.h"
#include "../llimagegl.h"
#include "../llrendertarget.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>
#include <vector>

namespace
{
        constexpr U32 SIZE = 4;

        // A triangle over the viewport.
        const char* kVertex =
            "#version 400\n"
            "void main()\n"
            "{\n"
            "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;\n"
            "    gl_Position = vec4(p, 0.0, 1.0);\n"
            "}\n";

        // Four outputs, as a G-buffer pass writes, each its own colour.
        const char* kFragment =
            "#version 400\n"
            "out vec4 frag_data[4];\n"
            "void main()\n"
            "{\n"
            "    frag_data[0] = vec4(1.0, 0.0, 0.0, 1.0);\n"
            "    frag_data[1] = vec4(0.0, 1.0, 0.0, 1.0);\n"
            "    frag_data[2] = vec4(0.0, 0.0, 1.0, 1.0);\n"
            "    frag_data[3] = vec4(1.0, 1.0, 0.0, 1.0);\n"
            "}\n";

        const LLColor4U OUTPUT_COLOURS[] = { LLColor4U(255, 0, 0, 255), LLColor4U(0, 255, 0, 255),
                                             LLColor4U(0, 0, 255, 255), LLColor4U(255, 255, 0, 255) };

        // The integer targets' size, neither square nor a power of two.
        constexpr U32 INT_W = 6;
        constexpr U32 INT_H = 5;

        // One output of each integer kind, each a function of the pixel: unsigned past 2^24, signed either side of 0,
        // and four wide channels to the top of their range.
        const char* kIntegerFragment =
            "#version 400\n"
            "layout(location = 0) out uvec4 frag_uint;\n"
            "layout(location = 1) out ivec4 frag_int;\n"
            "layout(location = 2) out uvec4 frag_wide;\n"
            "void main()\n"
            "{\n"
            "    uvec2 p = uvec2(gl_FragCoord.xy);\n"
            "    frag_uint = uvec4(0x01000001u + p.y * 1000u + p.x, 0u, 0u, 0u);\n"
            "    frag_int = ivec4(-1 - int(p.x), int(p.y) * 300 - 1000, 0, 0);\n"
            "    frag_wide = uvec4(p.x, p.y, 0xFFFFFFFFu - p.x, 0x80000000u + p.y);\n"
            "}\n";

        // What the bound target's colour attachment holds, read as integers: four channels a texel, those its format
        // lacks read as GL fills them in, 0 and an alpha of 1.
        template <typename T>
        std::vector<T> readInteger(U32 attachment)
        {
            std::vector<T> texels((size_t)INT_W * INT_H * 4, T(0x55));
            glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment);
            glReadPixels(0, 0, INT_W, INT_H, GL_RGBA_INTEGER, std::is_signed_v<T> ? GL_INT : GL_UNSIGNED_INT,
                         texels.data());
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            return texels;
        }

        // Every texel of `texels` is `expected`.
        template <typename T>
        void require_every(const std::string& what, const std::vector<T>& texels, const T (&expected)[4])
        {
            INFO(what);
            for (size_t i = 0; i < texels.size(); ++i)
            {
                INFO("texel " << i / 4 << " channel " << i % 4);
                REQUIRE(texels[i] == expected[i % 4]);
            }
        }

    // The program, vertex array and clear colour the test sets go back with
    // mGL.
    struct llrendertarget_data
    {
        ~llrendertarget_data()
        {
            // A case that failed between a bindTarget and its flush left a target bound, which release refuses.
            while (LLRenderTarget::getCurrentBoundTarget())
            {
                LLRenderTarget::getCurrentBoundTarget()->flush();
            }
            mTarget.release();
            mInteger.release();
            mMixed.release();
            mLent.release();
            mCopy.release();
            mLentImage = nullptr;
            LLRenderTarget::sUseFBO = mUseFBO;
            if (mVAO)
            {
                glDeleteVertexArrays(1, &mVAO);
            }
            glUseProgram(0);
            for (GLuint program : { mProgram, mIntegerProgram })
            {
                if (program)
                {
                    glDeleteProgram(program);
                }
            }
        }

        // The integer program, linked.
        GLuint require_integer_program()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kIntegerFragment);
            {
                INFO("integer shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            const GLuint program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            REQUIRE(linked == GL_TRUE);
            return program;
        }

        void setUp()
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

            REQUIRE(mTarget.allocate(SIZE, SIZE, GL_RGBA8));
        }

        // What the target holds after the triangle draws into it, its draw buffers set to output `output`, or left as
        // bindTarget sets them when it is negative.
        LLColor4U drawn(S32 output)
        {
            mTarget.bindTarget();
            gGL.setClearColor(LLColor4::transparent);
            mTarget.clear();
            if (output >= 0)
            {
                mTarget.setDrawOutput((U32)output);
            }
            glUseProgram(mProgram);
            glBindVertexArray(mVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glUseProgram(0);
            U8 pixel[4] = { 0 };
            glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            mTarget.flush();
            return LLColor4U(pixel[0], pixel[1], pixel[2], pixel[3]);
        }

        ll_test::SharedGLScope mGL;
        LLRenderTarget mTarget;
        LLRenderTarget mInteger;
        LLRenderTarget mMixed;
        LLRenderTarget mLent;
        LLRenderTarget mCopy;
        LLPointer<LLImageGL> mLentImage;
        // setColorAttachment is the FBO path's, which the viewer turns on and a test turns on for itself.
        const bool mUseFBO = LLRenderTarget::sUseFBO;
        GLuint mVAO = 0;
        GLuint mProgram = 0;
        GLuint mIntegerProgram = 0;
    };


    // setDrawOutput: a single-attachment target takes the output it names and none of the others, and the next
    // bindTarget gives it output 0 again.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: setDrawOutput sends the named output to a single-attachment target", "[llrendertarget_gl]")
    {
        setUp();
        UNSCOPED_INFO("bound: output 0");
        CHECK(drawn(-1) == OUTPUT_COLOURS[0]);
        for (S32 output = 0; output < 4; ++output)
        {
            CAPTURE(output);
            REQUIRE(drawn(output) == OUTPUT_COLOURS[output]);
        }
        UNSCOPED_INFO("rebound: output 0 again");
        CHECK(drawn(-1) == OUTPUT_COLOURS[0]);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // Integer attachments: R32UI, RG16I and RGBA32UI allocate as themselves beside a depth buffer. clear takes each to
    // the clear colour rounded to integers -- an unsigned one held at 0 below and at its top above -- and leaves them
    // alone when only the depth is asked for; a program's integer outputs reach them exactly.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: integer attachments keep their formats through clear and draw", "[llrendertarget_gl]")
    {
        mIntegerProgram = require_integer_program();
        glGenVertexArrays(1, &mVAO);
        REQUIRE(mInteger.allocate(INT_W, INT_H, GL_R32UI, /*depth=*/true));
        CHECK(mInteger.addColorAttachment(GL_RG16I));
        CHECK(mInteger.addColorAttachment(GL_RGBA32UI));

        const GLenum formats[] = { GL_R32UI, GL_RG16I, GL_RGBA32UI };
        for (U32 attachment = 0; attachment < 3; ++attachment)
        {
            CAPTURE(attachment);
            UNSCOPED_INFO("the target's record");
            REQUIRE(mInteger.getInternalFormat(attachment) == (U32)formats[attachment]);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mInteger.getTexture(attachment));
            GLint format = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
            UNSCOPED_INFO("the texture's storage");
            REQUIRE((U32)format == (U32)formats[attachment]);
        }
        gGL.getTextureSlot(0)->unbind();

        mInteger.bindTarget();
        CHECK((U32)glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == (U32)GL_FRAMEBUFFER_COMPLETE);

        gGL.setClearColor(LLColor4(7.f, 9.f, 11.f, 13.f));
        mInteger.clear();
        require_every<U32>("R32UI cleared", readInteger<U32>(0), { 7u, 0u, 0u, 1u });
        require_every<S32>("RG16I cleared", readInteger<S32>(1), { 7, 9, 0, 1 });
        require_every<U32>("RGBA32UI cleared", readInteger<U32>(2), { 7u, 9u, 11u, 13u });

        gGL.setClearColor(LLColor4(-5.4f, -6.6f, 1.e10f, 0.4f));
        mInteger.clear(GL_DEPTH_BUFFER_BIT);
        require_every<U32>("R32UI left alone by a depth clear", readInteger<U32>(0), { 7u, 0u, 0u, 1u });
        mInteger.clear();
        require_every<U32>("R32UI held at 0", readInteger<U32>(0), { 0u, 0u, 0u, 1u });
        require_every<S32>("RG16I below 0", readInteger<S32>(1), { -5, -7, 0, 1 });
        require_every<U32>("RGBA32UI held at its top", readInteger<U32>(2), { 0u, 0u, 0xFFFFFFFFu, 0u });

        LLGLDisable depth_test(GL_DEPTH_TEST);
        glUseProgram(mIntegerProgram);
        glBindVertexArray(mVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);
        const std::vector<U32> uints = readInteger<U32>(0);
        const std::vector<S32> ints = readInteger<S32>(1);
        const std::vector<U32> wide = readInteger<U32>(2);
        mInteger.flush();

        for (U32 y = 0; y < INT_H; ++y)
        {
            for (U32 x = 0; x < INT_W; ++x)
            {
                CAPTURE(x, y);
                const size_t i = ((size_t)y * INT_W + x) * 4;
                UNSCOPED_INFO("R32UI drawn");
                REQUIRE(uints[i] == 0x01000001u + y * 1000u + x);
                UNSCOPED_INFO("RG16I r drawn");
                REQUIRE(ints[i] == -1 - (S32)x);
                UNSCOPED_INFO("RG16I g drawn");
                REQUIRE(ints[i + 1] == (S32)y * 300 - 1000);
                UNSCOPED_INFO("RGBA32UI r drawn");
                REQUIRE(wide[i] == x);
                UNSCOPED_INFO("RGBA32UI g drawn");
                REQUIRE(wide[i + 1] == y);
                UNSCOPED_INFO("RGBA32UI b drawn");
                REQUIRE(wide[i + 2] == 0xFFFFFFFFu - x);
                UNSCOPED_INFO("RGBA32UI a drawn");
                REQUIRE(wide[i + 3] == 0x80000000u + y);
            }
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A target of both kinds: clear takes the float attachment to the clear colour and the integer one to its integers,
    // and with the draw buffers narrowed to the first it clears that one alone, as it would a target of one kind.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: a mixed float and integer target clears each attachment as its kind", "[llrendertarget_gl]")
    {
        REQUIRE(mMixed.allocate(INT_W, INT_H, GL_RGBA8));
        CHECK(mMixed.addColorAttachment(GL_R32UI));

        mMixed.bindTarget();
        gGL.setClearColor(LLColor4(1.f, 0.f, 1.f, 1.f));
        mMixed.clear();
        std::vector<U8> rgba((size_t)INT_W * INT_H * 4, 0x55);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, INT_W, INT_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        require_every<U8>("RGBA8 cleared", rgba, { 255, 0, 255, 255 });
        require_every<U32>("R32UI cleared", readInteger<U32>(1), { 1u, 0u, 0u, 1u });

        mMixed.setDrawBuffers(1);
        gGL.setClearColor(LLColor4::transparent);
        mMixed.clear();
        glReadPixels(0, 0, INT_W, INT_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        require_every<U8>("RGBA8 cleared again", rgba, { 0, 0, 0, 0 });
        require_every<U32>("R32UI not drawn to, so left", readInteger<U32>(1), { 1u, 0u, 0u, 1u });
        mMixed.flush();

        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A texture lent by setColorAttachment answers for its format as one the target allocated does, so an integer one
    // clears to its integers; and returning it forgets the format.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: a texture lent by setColorAttachment has its format until returned", "[llrendertarget_gl]")
    {
        const std::vector<U32> zeros((size_t)INT_W * INT_H, 0u);
        mLentImage = new LLImageGL(INT_W, INT_H, 1, /*usemipmaps=*/false);
        mLentImage->setExplicitFormat(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT);
        CHECK(mLentImage->createGLTexture(0, reinterpret_cast<const U8*>(zeros.data())));

        LLRenderTarget::sUseFBO = true;
        mLent.setColorAttachment(mLentImage.get());
        CHECK(mLent.getInternalFormat(0) == (U32)GL_R32UI);

        mLent.bindTarget();
        gGL.setClearColor(LLColor4(5.f, 6.f, 7.f, 8.f));
        mLent.clear();
        require_every<U32>("R32UI cleared to its integers", readInteger<U32>(0), { 5u, 0u, 0u, 1u });
        mLent.flush();

        mLent.releaseColorAttachment();
        CHECK(mLent.getInternalFormat(0) == 0u);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // copyFromBound copies the colour of the framebuffer bound now into a target, scaled with the filter asked for, as
    // the selection outline copies the window behind its lines at half size: a linear halving averages each square of
    // four. The scissor in force does not clip it, and what was bound is bound after.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: copyFromBound copies the bound framebuffer past the scissor", "[llrendertarget_gl]")
    {
        setUp();
        constexpr U32 HALF = SIZE / 2;
        REQUIRE(mCopy.allocate(HALF, HALF, GL_RGBA8));
        mCopy.bindTarget();
        gGL.setClearColor(LLColor4(0.f, 0.f, 1.f, 1.f));
        mCopy.clear();
        mCopy.flush();

        // Black, but for a white first column.
        mTarget.bindTarget();
        gGL.setClearColor(LLColor4::black);
        mTarget.clear();
        {
            LLGLSScissor column(0, 0, 1, SIZE);
            gGL.setClearColor(LLColor4::white);
            mTarget.clear();
        }
        {
            LLGLSScissor corner(0, 0, 1, 1);
            mCopy.copyFromBound(0, 0, SIZE, SIZE, 0, 0, HALF, HALF, GL_LINEAR);
        }
        UNSCOPED_INFO("what was bound is bound after");
        CHECK(LLRenderTarget::getCurrentBoundTarget() == &mTarget);
        mTarget.flush();

        mCopy.bindTarget();
        std::vector<U8> px((size_t)HALF * HALF * 4);
        glReadPixels(0, 0, HALF, HALF, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        mCopy.flush();
        auto red_at = [&px](U32 x, U32 y) { return (U32)px[((size_t)y * HALF + x) * 4]; };
        auto blue_at = [&px](U32 x, U32 y) { return (U32)px[((size_t)y * HALF + x) * 4 + 2]; };
        UNSCOPED_INFO("the white column and the black beside it averaged");
        CHECK(red_at(0, 0) >= 127u);
        CHECK(red_at(0, 0) <= 128u);
        UNSCOPED_INFO("black squares black");
        CHECK(red_at(1, 0) == 0u);
        UNSCOPED_INFO("copied outside the scissor too");
        CHECK(blue_at(HALF - 1, HALF - 1) == 0u);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }
}
