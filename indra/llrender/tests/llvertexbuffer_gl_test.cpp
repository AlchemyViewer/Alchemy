/**
 * @file llvertexbuffer_gl_test.cpp
 * @brief What LLVertexBuffer draws after an upload, a second upload and a new size.
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
#include "../llvertexbuffer.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
{
    constexpr S32 W = 16;
    constexpr S32 H = 16;

    // Pixel columns in the left and right halves of the target.
    constexpr S32 LEFT  = W / 4;
    constexpr S32 RIGHT = W * 3 / 4;

    const char* kColourVertex =
        "#version 330\n"
        "layout (std140) uniform Matrices\n"
        "{\n"
        "    mat4 modelview_matrix;\n"
        "    mat4 projection_matrix;\n"
        "    mat4 modelview_projection_matrix;\n"
        "    mat4 inv_proj;\n"
        "    mat4 texture_matrix0;\n"
        "    mat3 normal_matrix;\n"
        "};\n"
        "in vec3 position;\n"
        "in vec4 diffuse_color;\n"
        "out vec4 vertex_color;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = modelview_projection_matrix * vec4(position, 1.0);\n"
        "    vertex_color = diffuse_color;\n"
        "}\n";

    const char* kColourFragment =
        "#version 330\n"
        "in vec4 vertex_color;\n"
        "out vec4 frag_color;\n"
        "void main() { frag_color = vertex_color; }\n";

    // An opaque colour as glReadPixels packs RGBA8 into four bytes.
    U32 rgba(U8 r, U8 g, U8 b)
    {
        return U32(r) | (U32(g) << 8) | (U32(b) << 16) | (0xFFU << 24);
    }

    U32 packed(const LLColor4U& colour)
    {
        return rgba(colour.mV[VRED], colour.mV[VGREEN], colour.mV[VBLUE]);
    }

    // The program, the matrices and the target go back with mGL.
    struct llvertexbuffer_gl_data
    {
        static constexpr U32 MASK = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_COLOR;

        ~llvertexbuffer_gl_data()
        {
            if (mTarget.isBoundInStack())
            {
                mTarget.flush();
            }
            LLGLSLShader::unbind();
            mProgram.unload();
            mTarget.release();
        }

        // A program of positions and colours, mapped and bound, drawing into a cleared target under identity
        // matrices: a buffer's positions are where it lands in clip space.
        void setUp()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kColourVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kColourFragment);
            {
                INFO("colour shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mProgram.mProgramObject = glCreateProgram();
            mProgram.mName = "position and colour (test)";
            mProgram.attachObject(vs);
            mProgram.attachObject(fs);
            UNSCOPED_INFO("colour program links");
            REQUIRE(mProgram.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            REQUIRE(mProgram.mapUniforms());
            UNSCOPED_INFO("the colour program reads positions and colours");
            REQUIRE(mProgram.mAttributeMask == MASK);

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

        static LLPointer<LLVertexBuffer> makeBuffer(U32 verts, U32 indices)
        {
            LLPointer<LLVertexBuffer> vb = new LLVertexBuffer(MASK);
            REQUIRE(vb->allocateBuffer(verts, indices));
            return vb;
        }

        // The four vertices from `first`: a quad over x in [x0, x1], the target's whole height, in `colour`.
        static void writeQuad(LLVertexBuffer& vb, U32 first, F32 x0, F32 x1, const LLColor4U& colour)
        {
            LLStrider<LLVector3> pos;
            REQUIRE(vb.getVertexStrider(pos, first, 4));
            pos[0].set(x0, -1.f, 0.f);
            pos[1].set(x1, -1.f, 0.f);
            pos[2].set(x0, 1.f, 0.f);
            pos[3].set(x1, 1.f, 0.f);
            writeColours(vb, first, colour);
        }

        static void writeColours(LLVertexBuffer& vb, U32 first, const LLColor4U& colour)
        {
            LLStrider<LLColor4U> col;
            REQUIRE(vb.getColorStrider(col, first, 4));
            for (S32 i = 0; i < 4; ++i)
            {
                col[i] = colour;
            }
        }

        // The six indices from `at`: the two triangles of the quad whose vertices start at `first`.
        static void writeIndices(LLVertexBuffer& vb, U32 at, U16 first)
        {
            LLStrider<U16> idx;
            REQUIRE(vb.getIndexStrider(idx, at, 6));
            const U16 quad[6] = { 0, 1, 2, 2, 1, 3 };
            for (S32 i = 0; i < 6; ++i)
            {
                idx[i] = first + quad[i];
            }
        }

        static void draw(LLVertexBuffer& vb)
        {
            vb.setBuffer();
            vb.draw(LLRender::TRIANGLES, vb.getNumIndices(), 0);
        }

        // Upload `vb` and draw it the way that has always set it up: with nothing bound, so setBuffer points the
        // attributes whatever the upload left behind.
        static void uploadAndDrawFromScratch(LLVertexBuffer& vb)
        {
            vb.unmapBuffer();
            LLVertexBuffer::unbind();
            draw(vb);
        }

        static U32 at(S32 x)
        {
            U8 px[4] = {};
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(x, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            return U32(px[0]) | (U32(px[1]) << 8) | (U32(px[2]) << 16) | (U32(px[3]) << 24);
        }

        ll_test::SharedGLScope mGL;
        // Made before the program and gone after it: mapUniforms reads its reserved names.
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader           mProgram;
        LLRenderTarget         mTarget;
    };


    // An upload binds the buffer it sends, which is not the buffer the attribute pointers were last set against.
    // The draw that follows must still read the buffer it was given, not the one drawn before it.
    TEST_CASE_METHOD(llvertexbuffer_gl_data, "llvertexbuffer_gl: a buffer drawn straight after its upload draws its own vertices", "[llvertexbuffer_gl]")
    {
        setUp();
        const LLColor4U red(255, 0, 0, 255);
        const LLColor4U green(0, 255, 0, 255);

        LLPointer<LLVertexBuffer> left = makeBuffer(4, 6);
        writeQuad(*left, 0, -1.f, 0.f, red);
        writeIndices(*left, 0, 0);
        uploadAndDrawFromScratch(*left);

        LLPointer<LLVertexBuffer> right = makeBuffer(4, 6);
        writeQuad(*right, 0, 0.f, 1.f, green);
        writeIndices(*right, 0, 0);
        right->unmapBuffer();
        draw(*right);

        CHECK(at(LEFT) == packed(red));
        CHECK(at(RIGHT) == packed(green));
    }

    // A buffer drawn, written again at the same size and drawn at once draws what it holds now.
    TEST_CASE_METHOD(llvertexbuffer_gl_data, "llvertexbuffer_gl: a buffer written again draws what was written last", "[llvertexbuffer_gl]")
    {
        setUp();
        const LLColor4U green(0, 255, 0, 255);
        const LLColor4U blue(0, 0, 255, 255);

        LLPointer<LLVertexBuffer> vb = makeBuffer(4, 6);
        writeQuad(*vb, 0, 0.f, 1.f, green);
        writeIndices(*vb, 0, 0);
        uploadAndDrawFromScratch(*vb);
        REQUIRE(at(RIGHT) == packed(green));

        writeColours(*vb, 0, blue);
        vb->unmapBuffer();
        mTarget.clear();
        draw(*vb);

        CHECK(at(RIGHT) == packed(blue));
    }

    // A buffer given a new size has new stores, and draws from them: its indices past the old vertex count reach
    // vertices the old store never had.
    TEST_CASE_METHOD(llvertexbuffer_gl_data, "llvertexbuffer_gl: a buffer given more vertices draws from its new store", "[llvertexbuffer_gl]")
    {
        setUp();
        const LLColor4U red(255, 0, 0, 255);
        const LLColor4U green(0, 255, 0, 255);
        const LLColor4U blue(0, 0, 255, 255);

        LLPointer<LLVertexBuffer> vb = makeBuffer(4, 6);
        writeQuad(*vb, 0, 0.f, 1.f, green);
        writeIndices(*vb, 0, 0);
        uploadAndDrawFromScratch(*vb);
        REQUIRE(at(RIGHT) == packed(green));

        REQUIRE(vb->allocateBuffer(8, 12));
        writeQuad(*vb, 0, -1.f, 0.f, red);
        writeQuad(*vb, 4, 0.f, 1.f, blue);
        writeIndices(*vb, 0, 0);
        writeIndices(*vb, 6, 4);
        vb->unmapBuffer();
        mTarget.clear();
        draw(*vb);

        CHECK(at(LEFT) == packed(red));
        CHECK(at(RIGHT) == packed(blue));
    }

    // Only what a map wrote is sent again. The indices' copy is changed here behind the map's back, which no
    // upload of the vertices may carry to GL: the draw still has the indices GL was last given.
    TEST_CASE_METHOD(llvertexbuffer_gl_data, "llvertexbuffer_gl: writing only the vertices leaves the indices GL holds", "[llvertexbuffer_gl]")
    {
        setUp();
        const LLColor4U green(0, 255, 0, 255);
        const LLColor4U blue(0, 0, 255, 255);

        LLPointer<LLVertexBuffer> vb = makeBuffer(4, 6);
        writeQuad(*vb, 0, 0.f, 1.f, green);
        writeIndices(*vb, 0, 0);
        uploadAndDrawFromScratch(*vb);
        REQUIRE(at(RIGHT) == packed(green));

        U16* indices = reinterpret_cast<U16*>(vb->getMappedIndices());
        REQUIRE(indices);
        std::fill(indices, indices + 6, U16(0)); // every triangle degenerate, were it sent

        writeColours(*vb, 0, blue);
        mTarget.clear();
        uploadAndDrawFromScratch(*vb);

        CHECK(at(RIGHT) == packed(blue));
    }

    // As above, the other way about: the vertices' copy is changed behind the map's back, and an upload of the
    // indices alone leaves GL drawing the vertices it was last given.
    TEST_CASE_METHOD(llvertexbuffer_gl_data, "llvertexbuffer_gl: writing only the indices leaves the vertices GL holds", "[llvertexbuffer_gl]")
    {
        setUp();
        const LLColor4U red(255, 0, 0, 255);
        const LLColor4U green(0, 255, 0, 255);

        LLPointer<LLVertexBuffer> vb = makeBuffer(4, 6);
        writeQuad(*vb, 0, 0.f, 1.f, green);
        writeIndices(*vb, 0, 0);
        uploadAndDrawFromScratch(*vb);
        REQUIRE(at(RIGHT) == packed(green));

        U8* vertices = vb->getMappedData();
        REQUIRE(vertices);
        LLColor4U* colours = reinterpret_cast<LLColor4U*>(vertices + vb->getOffset(LLVertexBuffer::TYPE_COLOR));
        std::fill(colours, colours + 4, red);

        writeIndices(*vb, 0, 0);
        mTarget.clear();
        uploadAndDrawFromScratch(*vb);

        CHECK(at(RIGHT) == packed(green));
    }
}
