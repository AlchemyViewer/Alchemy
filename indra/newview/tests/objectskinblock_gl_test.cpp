/**
 * @file objectskinblock_gl_test.cpp
 * @brief The rigged programs' joint palette read from the ObjectSkin block, and from uniforms.
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

// objectSkinV.glsl, the shared object every rigged program attaches, compiled with OBJECT_SKIN_BLOCK as the viewer
// compiles it where the context maps buffers persistently, and without, as it does elsewhere. The block must be laid
// out as the viewer packs a palette (LLVOAvatar::MatrixPaletteCache::mGLMp: the origin as a vec4, then each joint's
// three columns), sit at UB_OBJECT_SKIN once the program's uniforms are mapped, and skin a vertex as the uniforms do.

#include "linden_common.h"

#include "aluniformbuffer.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llstrider.h"
#include "llvertexbuffer.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

namespace
{
    constexpr S32 W = 16;
    constexpr S32 H = 16;

    // The palette length the viewer compiles objectSkinV with (LL_MAX_JOINTS_PER_MESH_OBJECT), and the block's size
    // at it: a vec4, then a mat3x4 of three vec4 columns a joint.
    constexpr U32 JOINTS      = 110;
    constexpr U32 BLOCK_BYTES = 16 + 48 * JOINTS;

    const char* kVertex =
        "#version 400\n"
        "in vec3 position;\n"
        "mat3x4 getSkinBlend();\n"
        "vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m);\n"
        "void main() { gl_Position = skinTransformH(getSkinBlend(), position, mat4(1.0)); }\n";

    const char* kFragment =
        "#version 400\n"
        "out vec4 frag_color;\n"
        "void main() { frag_color = vec4(1.0, 0.0, 0.0, 1.0); }\n";

    struct objectskinblock_data
    {
        objectskinblock_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
        }

        ~objectskinblock_data()
        {
            if (mTarget.isBoundInStack())
            {
                mTarget.flush();
            }
            LLGLSLShader::unbind();
            mProgram.unload();
            mShaders.clearShaderObjects();
            mTarget.release();
            mTriangle = nullptr;
            ALUniformBuffer::cleanupClass();
        }

        // The program over objectSkinV compiled with the block or without, and a triangle on the left of the target
        // whose every vertex is weighted wholly to joint 0.
        void setUp(bool block)
        {
            std::map<std::string, std::string> defines = { { "MAX_JOINTS_PER_MESH_OBJECT", std::to_string(JOINTS) } };
            if (block)
            {
                defines["OBJECT_SKIN_BLOCK"] = "1";
            }
            S32 level = 1;
            const GLuint skin = mShaders.loadShaderFile("avatar/objectSkinV.glsl", level, GL_VERTEX_SHADER, &defines);
            {
                INFO("objectSkinV compiles");
                REQUIRE(skin != 0);
            }
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            {
                INFO("test shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mProgram.mProgramObject = glCreateProgram();
            mProgram.mName = "objectSkinV (test)";
            mProgram.attachObject(vs);
            mProgram.attachObject(fs);
            mProgram.attachObject(skin);
            UNSCOPED_INFO("program links");
            REQUIRE(mProgram.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            UNSCOPED_INFO("program maps");
            REQUIRE(mProgram.mapUniforms());

            REQUIRE(mTarget.allocate(W, H, GL_RGBA8));

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_WEIGHT4);
            REQUIRE(mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            REQUIRE(mTriangle->getVertexStrider(vert));
            vert[0].set(-0.9f, -0.5f, 0.f);
            vert[1].set(-0.1f, -0.5f, 0.f);
            vert[2].set(-0.5f, 0.5f, 0.f);
            LLStrider<LLVector4> weight;
            REQUIRE(mTriangle->getWeight4Strider(weight));
            for (S32 i = 0; i < 3; ++i)
            {
                // Joint 0 four times over, a quarter each: the blend is joint 0's.
                weight[i].set(0.5f, 0.5f, 0.5f, 0.5f);
            }
            mTriangle->unmapBuffer();
        }

        // A palette as the viewer packs one: the origin at 0, joint 0 moving a point one unit right, joint 1 leaving
        // it where it is.
        static std::vector<F32> palette()
        {
            std::vector<F32> packed(4 + 2 * 12, 0.f);
            F32* joints = packed.data() + 4;
            for (U32 j = 0; j < 2; ++j)
            {
                F32* m = joints + j * 12;
                m[0]  = 1.f;
                m[5]  = 1.f;
                m[10] = 1.f;
            }
            // Joint 0's translation, in its first column's w.
            joints[3] = 1.f;
            return packed;
        }

        void draw()
        {
            mTarget.bindTarget();
            gGL.setClearColor(LLColor4::black);
            mTarget.clear();
            LLGLDepthTest depth_test(GL_FALSE);
            mTriangle->setBuffer();
            mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            mProgram.unbind();
            mTarget.flush();
        }

        U8 redAt(S32 x)
        {
            mTarget.bindTarget();
            U8 px[4] = {};
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(x, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            mTarget.flush();
            return px[0];
        }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader           mProgram;
        LLRenderTarget         mTarget;
        LLPointer<LLVertexBuffer> mTriangle;
    };

    // Compiled with OBJECT_SKIN_BLOCK, the palette is a block laid out as the viewer packs it, bound at UB_OBJECT_SKIN,
    // and a slice of the scratch ring written with the block's size reserved skins the triangle by joint 0.
    TEST_CASE_METHOD(objectskinblock_data, "objectskinblock_gl: the block takes the palette as the viewer packs it", "[objectskinblock_gl]")
    {
        setUp(true);
        const GLuint program = mProgram.mProgramObject;
        const GLuint index = glGetUniformBlockIndex(program, LLGLSLShader::UNIFORM_BLOCK_NAMES[LLGLSLShader::UB_OBJECT_SKIN]);
        REQUIRE(index != GL_INVALID_INDEX);
        GLint size = 0;
        glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
        CHECK(size == (GLint)BLOCK_BYTES);
        GLint binding = -1;
        glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_BINDING, &binding);
        {
            INFO("mapUniforms put the block at its engine point");
            CHECK(binding == (GLint)LLGLSLShader::UB_OBJECT_SKIN);
        }
        {
            INFO("the palette is no program's uniform now");
            CHECK(glGetUniformLocation(program, "matrixPalette") == -1);
            CHECK(glGetUniformLocation(program, "skin_origin") == -1);
        }

        const std::vector<F32> packed = palette();
        const S64 offset = ALUniformBuffer::scratchWrite(packed.data(), packed.size() * sizeof(F32), BLOCK_BYTES);
        REQUIRE(offset >= 0);
        mProgram.bind();
        ALUniformBuffer::scratchBindRange(LLGLSLShader::UB_OBJECT_SKIN, offset, BLOCK_BYTES);
        draw();

        {
            INFO("the triangle moved from the left to the right");
            CHECK(redAt(W / 4) == 0);
            CHECK(redAt(W * 3 / 4) == 255);
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Without it, the same palette given as uniforms skins the triangle the same way.
    TEST_CASE_METHOD(objectskinblock_data, "objectskinblock_gl: the uniforms skin as the block does", "[objectskinblock_gl]")
    {
        setUp(false);
        CHECK(glGetUniformBlockIndex(mProgram.mProgramObject, LLGLSLShader::UNIFORM_BLOCK_NAMES[LLGLSLShader::UB_OBJECT_SKIN]) ==
              GL_INVALID_INDEX);

        const std::vector<F32> packed = palette();
        mProgram.bind();
        mProgram.uniformMatrix3x4fv(LLShaderMgr::AVATAR_MATRIX, 2, GL_FALSE, packed.data() + 4);
        mProgram.uniform3fv(LLShaderMgr::SKIN_ORIGIN, 1, packed.data());
        draw();

        CHECK(redAt(W / 4) == 0);
        CHECK(redAt(W * 3 / 4) == 255);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }
}
