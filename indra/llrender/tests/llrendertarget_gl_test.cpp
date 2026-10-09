/**
 * @file llrendertarget_gl_test.cpp
 * @brief LLRenderTarget on the hidden window: which fragment output reaches a target's colour attachment.
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
#include "../llrendertarget.h"

#include "llheadlessgl_fixture.h"

#include "../test/lltut.h"

namespace tut
{
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
    }

    struct llrendertarget_data
    {
        static ll_test::HeadlessGL& gl()
        {
            static ll_test::HeadlessGL instance(true, true, true, false);
            return instance;
        }

        llrendertarget_data()
        {
            gl();
        }

        ~llrendertarget_data()
        {
            mTarget.release();
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

        void setUp()
        {
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

            ensure("target allocates", mTarget.allocate(SIZE, SIZE, GL_RGBA8));
        }

        // What the target holds after the triangle draws into it, its draw buffers set to output `output`, or left as
        // bindTarget sets them when it is negative.
        LLColor4U drawn(S32 output)
        {
            mTarget.bindTarget();
            glClearColor(0.f, 0.f, 0.f, 0.f);
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

        LLRenderTarget mTarget;
        GLuint mVAO = 0;
        GLuint mProgram = 0;
    };

    typedef test_group<llrendertarget_data> llrendertarget_t;
    typedef llrendertarget_t::object llrendertarget_object_t;
    tut::llrendertarget_t tut_llrendertarget("llrendertarget_gl");

    // setDrawOutput: a single-attachment target takes the output it names and none of the others, and the next
    // bindTarget gives it output 0 again.
    template<> template<>
    void llrendertarget_object_t::test<1>()
    {
        setUp();
        ensure_equals("bound: output 0", drawn(-1), OUTPUT_COLOURS[0]);
        for (S32 output = 0; output < 4; ++output)
        {
            ensure_equals("output " + std::to_string(output), drawn(output), OUTPUT_COLOURS[output]);
        }
        ensure_equals("rebound: output 0 again", drawn(-1), OUTPUT_COLOURS[0]);
        ensure_equals("no GL error", (U32)glGetError(), (U32)GL_NO_ERROR);
    }
}
