/**
 * @file altessellation_gl_test.cpp
 * @brief Tessellation stages on the hidden window: a four-stage program
 *        links, GL_PATCHES draws, and the quad conventions the terrain
 *        relies on -- winding, outer-level indexing, crack-free spacing.
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
#include "../llrender.h"
#include "../llvertexbuffer.h"

#include "llheadlessgl_fixture.h"

#include "../test/lltut.h"

#include <algorithm>
#include <array>
#include <vector>

namespace tut
{
    namespace
    {
        // The stages the terrain program has: a pass-through vertex shader, a
        // control shader that takes its levels from uniforms, an evaluation
        // shader that bilinearly places the quad, and a flat fragment shader.
        // The same u->x, v->y mapping and `ccw` the terrain uses.
        const char* kVertex =
            "#version 400\n"
            "layout(location = 0) in vec2 position;\n"
            "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";

        const char* kControl =
            "#version 400\n"
            "layout(vertices = 4) out;\n"
            "uniform vec4 outer;\n"
            "uniform vec2 inner;\n"
            "void main()\n"
            "{\n"
            "    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n"
            "    if (gl_InvocationID == 0)\n"
            "    {\n"
            "        gl_TessLevelOuter[0] = outer.x;\n"
            "        gl_TessLevelOuter[1] = outer.y;\n"
            "        gl_TessLevelOuter[2] = outer.z;\n"
            "        gl_TessLevelOuter[3] = outer.w;\n"
            "        gl_TessLevelInner[0] = inner.x;\n"
            "        gl_TessLevelInner[1] = inner.y;\n"
            "    }\n"
            "}\n";

        const char* kEvaluation =
            "#version 400\n"
            "layout(quads, fractional_odd_spacing, ccw) in;\n"
            "void main()\n"
            "{\n"
            "    vec2 t = gl_TessCoord.xy;\n"
            "    vec4 bottom = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, t.x);\n"
            "    vec4 top    = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, t.x);\n"
            "    gl_Position = mix(bottom, top, t.y);\n"
            "}\n";

        const char* kFragment =
            "#version 400\n"
            "out vec4 frag_color;\n"
            "void main() { frag_color = vec4(1.0); }\n";

        // One patch covering the whole framebuffer, corners in the order the
        // terrain emits them: (x0,y0) (x1,y0) (x1,y1) (x0,y1), counter-clockwise
        // seen from +Z.
        const std::array<F32, 8> kCCW = { -1.f, -1.f,  1.f, -1.f,  1.f, 1.f, -1.f, 1.f };
        const std::array<F32, 8> kCW  = { -1.f, -1.f, -1.f,  1.f,  1.f, 1.f,  1.f, -1.f };

        struct Vertex { F32 x, y, z, w; };
    }

    struct altessellation_data
    {
        static ll_test::HeadlessGL& gl()
        {
            static ll_test::HeadlessGL instance(/*needs_vbos=*/true,
                                                /*needs_imagegl=*/false,
                                                /*needs_llrender=*/true,
                                                /*needs_render=*/false);
            return instance;
        }

        altessellation_data()
        {
            gl();
            mProgram = glCreateProgram();
            const std::array<std::pair<GLenum, const char*>, 4> stages = {{
                { GL_VERTEX_SHADER,          kVertex },
                { GL_TESS_CONTROL_SHADER,    kControl },
                { GL_TESS_EVALUATION_SHADER, kEvaluation },
                { GL_FRAGMENT_SHADER,        kFragment },
            }};
            for (const auto& [stage, source] : stages)
            {
                const GLuint shader = ll_test::compileTestShader(stage, source);
                if (shader)
                {
                    glAttachShader(mProgram, shader);
                    glDeleteShader(shader);
                }
                else
                {
                    mCompiled = false;
                }
            }
            const char* captured = "gl_Position";
            glTransformFeedbackVaryings(mProgram, 1, &captured, GL_INTERLEAVED_ATTRIBS);
            glLinkProgram(mProgram);
            GLint linked = GL_FALSE;
            glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
            mLinked = linked == GL_TRUE;
            if (!mLinked)
            {
                GLchar log[2048] = {0};
                glGetProgramInfoLog(mProgram, sizeof(log) - 1, nullptr, log);
                LL_WARNS("HeadlessGL") << "Tessellation program link failed: " << log << LL_ENDL;
            }
            mOuter = glGetUniformLocation(mProgram, "outer");
            mInner = glGetUniformLocation(mProgram, "inner");

            glGenBuffers(1, &mCorners);
            glGenBuffers(1, &mCapture);
            glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, mCapture);
            glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, CAPTURE_BYTES, nullptr, GL_STREAM_READ);
            glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);
            glGenQueries(1, &mQuery);
        }

        ~altessellation_data()
        {
            glUseProgram(0);
            glDisableVertexAttribArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glDeleteQueries(1, &mQuery);
            glDeleteBuffers(1, &mCapture);
            glDeleteBuffers(1, &mCorners);
            glDeleteProgram(mProgram);
        }

        void setLevels(F32 o0, F32 o1, F32 o2, F32 o3, F32 i0, F32 i1)
        {
            glUseProgram(mProgram);
            glUniform4f(mOuter, o0, o1, o2, o3);
            glUniform2f(mInner, i0, i1);
        }

        void bindCorners(const std::array<F32, 8>& corners)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mCorners);
            glBufferData(GL_ARRAY_BUFFER, sizeof(F32) * corners.size(), corners.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(0);
        }

        // Rasterise one patch into the hidden window's framebuffer, back-face
        // culled, and return the pixels.
        std::vector<U8> draw(const std::array<F32, 8>& corners)
        {
            LLGLEnable  cull(GL_CULL_FACE);
            LLGLDisable depth(GL_DEPTH_TEST);
            glFrontFace(GL_CCW);
            glViewport(0, 0, W, H);
            gl().clearFramebuffer();
            glUseProgram(mProgram);
            bindCorners(corners);
            glPatchParameteri(GL_PATCH_VERTICES, 4);
            glDrawArrays(GL_PATCHES, 0, 4);
            glFinish();
            return ll_test::readFramebufferRGBA(W, H);
        }

        // Run one patch through the evaluation shader with rasterisation off,
        // capturing every emitted vertex and counting the triangles.
        std::vector<Vertex> capture(const std::array<F32, 8>& corners, GLuint& triangles)
        {
            LLGLEnable discard(GL_RASTERIZER_DISCARD);
            glUseProgram(mProgram);
            bindCorners(corners);
            glPatchParameteri(GL_PATCH_VERTICES, 4);
            glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, mCapture);
            glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, mQuery);
            glBeginTransformFeedback(GL_TRIANGLES);
            glDrawArrays(GL_PATCHES, 0, 4);
            glEndTransformFeedback();
            glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
            glGetQueryObjectuiv(mQuery, GL_QUERY_RESULT, &triangles);
            std::vector<Vertex> out(triangles * 3);
            if (!out.empty())
            {
                glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, sizeof(Vertex) * out.size(), out.data());
            }
            glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
            return out;
        }

        // Distinct emitted vertices lying on the line where `axis` (0 = x, 1 = y)
        // equals `value`. The tessellator evaluates each vertex once, so exact
        // comparison dedupes the copies the triangle list carries.
        static size_t distinctOn(const std::vector<Vertex>& verts, int axis, F32 value)
        {
            std::vector<std::pair<F32, F32>> seen;
            for (const Vertex& v : verts)
            {
                const F32 along = axis == 0 ? v.x : v.y;
                if (along != value) continue;
                const std::pair<F32, F32> key(v.x, v.y);
                if (std::find(seen.begin(), seen.end(), key) == seen.end())
                {
                    seen.push_back(key);
                }
            }
            return seen.size();
        }

        static bool white(const std::vector<U8>& px, S32 x, S32 y)
        {
            const size_t i = (static_cast<size_t>(y) * W + x) * 4;
            return px[i] == 255 && px[i + 1] == 255 && px[i + 2] == 255;
        }

        static bool black(const std::vector<U8>& px, S32 x, S32 y)
        {
            const size_t i = (static_cast<size_t>(y) * W + x) * 4;
            return px[i] == 0 && px[i + 1] == 0 && px[i + 2] == 0;
        }

        static constexpr S32 W = ll_test::HeadlessGL::WIDTH;
        static constexpr S32 H = ll_test::HeadlessGL::HEIGHT;
        static constexpr GLsizeiptr CAPTURE_BYTES = 64 * 1024;

        GLuint mProgram = 0;
        GLuint mCorners = 0;
        GLuint mCapture = 0;
        GLuint mQuery = 0;
        GLint  mOuter = -1;
        GLint  mInner = -1;
        bool   mCompiled = true;
        bool   mLinked = false;
    };

    typedef test_group<altessellation_data> altessellation_test;
    typedef altessellation_test::object     altessellation_object;
    tut::altessellation_test altessellation_testcase("ALTessellation");

    // The context compiles both tessellation stages and links them with a
    // vertex and fragment shader. Everything below assumes this.
    template<> template<>
    void altessellation_object::test<1>()
    {
        ensure("all four stages compiled", mCompiled);
        ensure("four-stage program linked", mLinked);
        ensure("outer levels uniform found", mOuter >= 0);
        ensure("inner levels uniform found", mInner >= 0);
    }

    // At level 1 everywhere a counter-clockwise patch is two triangles over
    // the whole framebuffer, and back-face culling keeps them.
    template<> template<>
    void altessellation_object::test<2>()
    {
        ensure("program linked", mLinked);
        setLevels(1, 1, 1, 1, 1, 1);
        const std::vector<U8> px = draw(kCCW);
        ensure("bottom-left covered",  white(px, 1, 1));
        ensure("bottom-right covered", white(px, W - 2, 1));
        ensure("top-right covered",    white(px, W - 2, H - 2));
        ensure("top-left covered",     white(px, 1, H - 2));
        ensure("centre covered",       white(px, W / 2, H / 2));
    }

    // The same corners clockwise are back faces: `layout(ccw)` with u along
    // x and v along y is the winding the pipeline's cull expects.
    template<> template<>
    void altessellation_object::test<3>()
    {
        ensure("program linked", mLinked);
        setLevels(1, 1, 1, 1, 1, 1);
        const std::vector<U8> px = draw(kCW);
        ensure("bottom-left culled",  black(px, 1, 1));
        ensure("top-right culled",    black(px, W - 2, H - 2));
        ensure("centre culled",       black(px, W / 2, H / 2));
    }

    // Subdivided with fractional spacing the patch still covers every pixel:
    // no cracks between the pieces, at integer and at fractional levels.
    template<> template<>
    void altessellation_object::test<4>()
    {
        ensure("program linked", mLinked);
        for (const F32 level : { 4.f, 2.5f, 7.25f })
        {
            setLevels(level, level, level, level, level, level);
            const std::vector<U8> px = draw(kCCW);
            size_t uncovered = 0;
            for (S32 y = 0; y < H; ++y)
                for (S32 x = 0; x < W; ++x)
                    if (!white(px, x, y)) ++uncovered;
            ensure_equals("every pixel covered at level " + std::to_string(level), uncovered, size_t(0));
        }
    }

    // Two triangles at level 1, and 2 * 3 * 3 at level 3: the tessellator
    // honours the levels and transform feedback sees what it emits. Odd
    // spacing rounds a level up to the next odd integer, so 3 is the smallest
    // level above 1 whose segments are all equal and whose count is exact.
    template<> template<>
    void altessellation_object::test<5>()
    {
        ensure("program linked", mLinked);
        GLuint triangles = 0;
        setLevels(1, 1, 1, 1, 1, 1);
        capture(kCCW, triangles);
        ensure_equals("level 1 is two triangles", triangles, GLuint(2));
        setLevels(3, 3, 3, 3, 3, 3);
        capture(kCCW, triangles);
        ensure_equals("level 3 is eighteen triangles", triangles, GLuint(18));
    }

    // Which outer level drives which edge. Raising one outer level to 3 puts
    // four distinct vertices on exactly one side of the patch and leaves two
    // on every other. Outer[0] is the u = 0 edge (x = -1), [1] the v = 0 edge
    // (y = -1), [2] u = 1 (x = +1), [3] v = 1 (y = +1).
    template<> template<>
    void altessellation_object::test<6>()
    {
        ensure("program linked", mLinked);
        // Edge i of the patch, as outer[i] should see it.
        struct Edge { int axis; F32 value; const char* name; };
        const Edge edges[4] = {
            { 0, -1.f, "outer[0] is the x = -1 edge" },
            { 1, -1.f, "outer[1] is the y = -1 edge" },
            { 0,  1.f, "outer[2] is the x = +1 edge" },
            { 1,  1.f, "outer[3] is the y = +1 edge" },
        };
        for (int raised = 0; raised < 4; ++raised)
        {
            F32 outer[4] = { 1.f, 1.f, 1.f, 1.f };
            outer[raised] = 3.f;
            setLevels(outer[0], outer[1], outer[2], outer[3], 1.f, 1.f);
            GLuint triangles = 0;
            const std::vector<Vertex> verts = capture(kCCW, triangles);
            for (int i = 0; i < 4; ++i)
            {
                const size_t expected = i == raised ? 4 : 2;
                ensure_equals(std::string(edges[raised].name) + (i == raised ? "" : ": other side unsplit"),
                              distinctOn(verts, edges[i].axis, edges[i].value), expected);
            }
        }
    }
}
