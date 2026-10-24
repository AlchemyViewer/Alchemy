/**
 * @file alinstanceddraw_gl_test.cpp
 * @brief LLRender::drawArraysInstanced on the hidden window: attribute-less instanced geometry placed by the matrices
 *        the draw syncs.
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
#include "../llglslshader.h"
#include "../llrender.h"
#include "../llrendertarget.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
        constexpr S32 W = 16;
        constexpr S32 H = 16;

        // A unit quad a strip, one a column: instance i covers x in [i, i + 1], y in [0, 1], placed by the matrices
        // the draw syncs. No vertex inputs, so binding the program enables no arrays.
        const char* kGridVertex =
            "#version 330\n"
            "layout (std140) uniform Matrices\n"
            "{\n"
            "    mat4 modelview_matrix;\n"
            "    mat4 modelview_projection_matrix;\n"
            "    mat3 normal_matrix;\n"
            "};\n"
            "layout (std140) uniform ProjectionMatrices\n"
            "{\n"
            "    mat4 projection_matrix;\n"
            "    mat4 inv_proj;\n"
            "    mat4 texture_matrix0;\n"
            "};\n"
            "void main()\n"
            "{\n"
            "    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
            "    gl_Position = modelview_projection_matrix * vec4(corner + vec2(gl_InstanceID, 0.0), 0.0, 1.0);\n"
            "}\n";

        const char* kGridFragment =
            "#version 330\n"
            "out vec4 frag_color;\n"
            "void main() { frag_color = vec4(1.0); }\n";

        // The orthographic projection taking [0, 4] x [0, 4] onto the whole target, raised `rows` quarters.
        LLMatrix4a grid(F32 rows)
        {
            LLMatrix4a m;
            m.setIdentity();
            m.setRow<0>(LLVector4a(0.5f, 0.f, 0.f, 0.f));
            m.setRow<1>(LLVector4a(0.f, 0.5f, 0.f, 0.f));
            m.setRow<3>(LLVector4a(-1.f, -1.f + 0.5f * rows, 0.f, 1.f));
            return m;
        }

    // The program, the matrices, the viewport and the clear colour the tests set go back with mGL.
    struct alinstanceddraw_data
    {
        ~alinstanceddraw_data()
        {
            if (mTarget.isBoundInStack())
            {
                mTarget.flush();
            }
            LLGLSLShader::unbind();
            mProgram.unload();
            mTarget.release();
        }

        // The grid program, mapped and bound, drawing into a cleared target under identity matrices.
        void setUp()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kGridVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kGridFragment);
            {
                INFO("grid shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mProgram.mProgramObject = glCreateProgram();
            mProgram.mName = "attribute-less grid (test)";
            mProgram.attachObject(vs);
            mProgram.attachObject(fs);
            UNSCOPED_INFO("grid program links");
            REQUIRE(mProgram.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            REQUIRE(mProgram.mapUniforms());
            UNSCOPED_INFO("the grid program has no vertex inputs");
            REQUIRE(mProgram.mAttributeMask == 0U);

            REQUIRE(mTarget.allocate(W, H, GL_RGBA8));
            mTarget.bindTarget();
            gGL.setClearColor(LLColor4::black);
            mTarget.clear();

            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            mProgram.bind();
        }

        // The quarter-target cells, column by column from the bottom row, that are white: 4 bits a row.
        U32 covered()
        {
            U8 rgba[W * H * 4] = {};
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            U32 cells = 0;
            for (S32 row = 0; row < 4; ++row)
            {
                for (S32 col = 0; col < 4; ++col)
                {
                    // Each cell's centre pixel.
                    const S32 x = col * (W / 4) + W / 8;
                    const S32 y = row * (H / 4) + H / 8;
                    if (rgba[(y * W + x) * 4] == 255)
                    {
                        cells |= 1U << (row * 4 + col);
                    }
                }
            }
            return cells;
        }

        ll_test::SharedGLScope mGL;
        // Made before the program and gone after it: mapUniforms reads its reserved names.
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader           mProgram;
        LLRenderTarget         mTarget;
    };


    // Three instances of a four-vertex strip cover the first three cells of the bottom row and nothing else.
    TEST_CASE_METHOD(alinstanceddraw_data, "alinstanceddraw_gl: three instanced strips cover the bottom row's first three cells only", "[alinstanceddraw_gl]")
    {
        setUp();
        gGL.loadMatrix(grid(0.f));
        gGL.drawArraysInstanced(LLRender::TRIANGLE_STRIP, 0, 4, 3);
        CHECK(covered() == 0x7U);
    }

    // The draw syncs the matrices: a projection loaded after one draw places the next. The raw draw, which does not
    // sync, still places it by the projection the first draw left in the block.
    TEST_CASE_METHOD(alinstanceddraw_data, "alinstanceddraw_gl: drawArraysInstanced syncs the matrices and a raw draw does not", "[alinstanceddraw_gl]")
    {
        setUp();
        gGL.loadMatrix(grid(0.f));
        gGL.drawArraysInstanced(LLRender::TRIANGLE_STRIP, 0, 4, 1);
        UNSCOPED_INFO("first draw on the bottom row");
        CHECK(covered() == 0x1U);

        gGL.loadMatrix(grid(2.f));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 2);
        UNSCOPED_INFO("the raw draw lands where the last synced projection put it");
        CHECK(covered() == 0x3U);

        gGL.drawArraysInstanced(LLRender::TRIANGLE_STRIP, 0, 4, 2);
        UNSCOPED_INFO("the synced draw lands on the third row");
        CHECK(covered() == (0x3U | 0x300U));
    }
}
