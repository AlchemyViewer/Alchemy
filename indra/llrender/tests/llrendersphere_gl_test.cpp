/**
 * @file llrendersphere_gl_test.cpp
 * @brief LLRenderSphere's buffer drawn under the debug GL checks.
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
#include "../llrendersphere.h"
#include "../llrendertarget.h"
#include "../llvertexbuffer.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    constexpr S32 W = 16;
    constexpr S32 H = 16;

    // Positions only, so the sphere takes its buffer rather than its immediate-mode path. Halved, so the unit
    // sphere lies wholly inside the clip volume under identity matrices.
    const char* kPositionVertex =
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
        "in vec3 position;\n"
        "void main() { gl_Position = modelview_projection_matrix * vec4(position * 0.5, 1.0); }\n";

    const char* kWhiteFragment =
        "#version 330\n"
        "out vec4 frag_color;\n"
        "void main() { frag_color = vec4(1.0); }\n";

    // gDebugGL raised for a scope: LLVertexBuffer then checks each draw's range against its buffer.
    class DebugGLScope
    {
    public:
        DebugGLScope() : mWas(gDebugGL) { gDebugGL = true; }
        ~DebugGLScope() { gDebugGL = mWas; }

        DebugGLScope(const DebugGLScope&)            = delete;
        DebugGLScope& operator=(const DebugGLScope&) = delete;

    private:
        bool mWas;
    };

    // The program, the matrices and the target go back with mGL.
    struct llrendersphere_gl_data
    {
        ~llrendersphere_gl_data()
        {
            if (mTarget.isBoundInStack())
            {
                mTarget.flush();
            }
            LLGLSLShader::unbind();
            mProgram.unload();
            mTarget.release();
        }

        void setUp()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kPositionVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kWhiteFragment);
            {
                INFO("position shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mProgram.mProgramObject = glCreateProgram();
            mProgram.mName = "positions only (test)";
            mProgram.attachObject(vs);
            mProgram.attachObject(fs);
            UNSCOPED_INFO("position program links");
            REQUIRE(mProgram.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            REQUIRE(mProgram.mapUniforms());
            UNSCOPED_INFO("the position program reads positions only");
            REQUIRE(mProgram.mAttributeMask == LLVertexBuffer::MAP_VERTEX);

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

        ll_test::SharedGLScope mGL;
        // Made before the program and gone after it: mapUniforms reads its reserved names.
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader           mProgram;
        LLRenderTarget         mTarget;
    };


    // The sphere draws its whole buffer, whose last vertex is one less than its count: with the debug checks on, a
    // range one past it is fatal.
    TEST_CASE_METHOD(llrendersphere_gl_data, "llrendersphere_gl: the sphere's buffer draws inside its range under debug GL", "[llrendersphere_gl]")
    {
        setUp();
        // A sphere of the test's own, so its buffer goes with the test rather than outliving the context as gSphere's.
        LLRenderSphere sphere;
        {
            DebugGLScope debug;
            CHECK_NOTHROW(sphere.renderGGL());
        }

        U8 px[4] = {};
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px[0] == 255);
    }
}
