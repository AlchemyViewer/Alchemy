/**
 * @file alpostchain_gl_fixture.h
 * @brief The post chain's programs as LLViewerShaderMgr builds them, from the viewer's own shader files, on llrender's
 *        shared hidden window: for the tests that hold a pass against what it replaced, and the benchmarks that time it.
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

#ifndef AL_POSTCHAIN_GL_FIXTURE_H
#define AL_POSTCHAIN_GL_FIXTURE_H

#include "alsamplerstate.h"
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

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ll_test
{
    using ShaderFiles = std::vector<std::pair<std::string, GLenum>>;
    using Permutations = std::map<std::string, std::string>;

    // The viewer's post programs over the shared objects LLViewerShaderMgr::loadBasicShaders compiles for them to
    // attach, the full-screen triangle every post pass draws, and what a test reads back. The first member of a test's
    // fixture after its SharedGLScope.
    class PostChainGL
    {
    public:
        // Over the shader tree at `prefix` ("<app_settings>/shaders/class"), the viewer's own by default.
        explicit PostChainGL(const std::string& prefix = std::string(AL_TEST_SHADER_DIR) + "/class")
        : mShaders(prefix)
        {
            // What a deferred post program and a post-effects one attach (LLShaderMgr::attachShaderFeatures).
            S32 level = 1;
            for (const char* object : { "windlight/atmosphericsVarsV.glsl", "deferred/textureUtilV.glsl", "objects/nonindexedTextureV.glsl" })
            {
                CAPTURE(object);
                level = 1;
                REQUIRE(mShaders.loadShaderFile(object, level, GL_VERTEX_SHADER, &mGlobalDefines));
            }
            for (const char* object : { "deferred/globalF.glsl", "environment/srgbF.glsl", "windlight/atmosphericsVarsF.glsl",
                                        "deferred/deferredUtil.glsl", "windlight/atmosphericsFuncs.glsl",
                                        "windlight/atmosphericsF.glsl", "alchemy/postEffectUtilsF.glsl" })
            {
                CAPTURE(object);
                level = 1;
                REQUIRE(mShaders.loadShaderFile(object, level, GL_FRAGMENT_SHADER, &mGlobalDefines));
            }

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();
        }

        ~PostChainGL()
        {
            LLGLSLShader::unbind();
            mShaders.clearShaderObjects();
            mTriangle = nullptr;
        }

        // Unload what build built, which the fixture holding the programs does before they go.
        void unloadPrograms()
        {
            LLGLSLShader::unbind();
            for (LLGLSLShader* program : mPrograms)
            {
                program->unload();
            }
            mPrograms.clear();
        }

        // Build `program` from `files` with `permutations`, `features` setting what LLViewerShaderMgr sets of its
        // mFeatures; unloaded by unloadPrograms.
        void build(LLGLSLShader& program, const std::string& name, const ShaderFiles& files,
                   const Permutations& permutations = {}, const std::function<void(LLShaderFeatures&)>& features = {})
        {
            program.mName = name;
            program.mShaderFiles = files;
            program.mShaderLevel = 1;
            program.clearPermutations();
            for (const auto& [key, value] : permutations)
            {
                program.addPermutation(key, value);
            }
            if (features)
            {
                features(program.mFeatures);
            }
            CAPTURE(name);
            REQUIRE(program.createShader());
            mPrograms.push_back(&program);
        }

        // The full-screen triangle, into whatever is bound, as LLPipeline's mScreenTriangleVB draws it.
        void drawTriangle()
        {
            mTriangle->setBuffer();
            mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        // Every texel of `target`'s first attachment, RGBA as floats, rows from the bottom.
        static std::vector<F32> read(LLRenderTarget& target)
        {
            std::vector<F32> texels(size_t(target.getWidth()) * target.getHeight() * 4, 0.f);
            target.bindTarget();
            glReadPixels(0, 0, target.getWidth(), target.getHeight(), GL_RGBA, GL_FLOAT, texels.data());
            target.flush();
            return texels;
        }

        // Fill `target`'s first attachment with `texels`, RGBA floats, rows from the bottom.
        static void write(LLRenderTarget& target, const std::vector<F32>& texels)
        {
            REQUIRE(texels.size() == size_t(target.getWidth()) * target.getHeight() * 4);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, target.getTexture());
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, target.getWidth(), target.getHeight(), GL_RGBA, GL_FLOAT, texels.data());
            gGL.getTextureSlot(0)->unbind();
        }

        // A repeatable pattern of texels in [0, 1] with edges in it: a hash of the coordinates over steps, so a
        // sharpening filter has contrast to work on at every scale.
        static std::vector<F32> pattern(U32 width, U32 height, U32 seed = 1)
        {
            std::vector<F32> texels(size_t(width) * height * 4);
            for (U32 y = 0; y < height; ++y)
            {
                for (U32 x = 0; x < width; ++x)
                {
                    for (U32 c = 0; c < 4; ++c)
                    {
                        uint32_t h = (x / ((c % 3) + 1)) * 73856093u ^ (y / ((c % 2) + 1)) * 19349663u ^ (c + seed) * 83492791u;
                        h ^= h >> 13;
                        h *= 0x5bd1e995u;
                        h ^= h >> 15;
                        texels[(size_t(y) * width + x) * 4 + c] = (F32)(h & 0xffff) / 65535.f;
                    }
                }
            }
            return texels;
        }

        std::map<std::string, std::string> mGlobalDefines;
        TestShaderMgr mShaders;

    private:
        LLPointer<LLVertexBuffer> mTriangle;
        std::vector<LLGLSLShader*> mPrograms;
    };

    // The GPU time of what `draw` issues, as GL_TIME_ELAPSED measures it: the median of `runs` runs, each its own
    // query, after `warm` runs unmeasured. Nanoseconds.
    inline U64 gpuMedianNs(const std::function<void()>& draw, U32 runs = 101, U32 warm = 10)
    {
        for (U32 i = 0; i < warm; ++i)
        {
            draw();
        }
        glFinish();

        std::vector<GLuint> queries(runs);
        glGenQueries((GLsizei)runs, queries.data());
        for (U32 i = 0; i < runs; ++i)
        {
            glBeginQuery(GL_TIME_ELAPSED, queries[i]);
            draw();
            glEndQuery(GL_TIME_ELAPSED);
        }
        std::vector<U64> times(runs);
        for (U32 i = 0; i < runs; ++i)
        {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(queries[i], GL_QUERY_RESULT, &ns);
            times[i] = ns;
        }
        glDeleteQueries((GLsizei)runs, queries.data());
        std::nth_element(times.begin(), times.begin() + runs / 2, times.end());
        return times[runs / 2];
    }
}

#endif // AL_POSTCHAIN_GL_FIXTURE_H
