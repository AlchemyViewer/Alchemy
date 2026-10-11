/**
 * @file llglslshader_gl_test.cpp
 * @brief LLGLSLShader on the hidden window. Its uniform value cache: what a program holds, read back from GL, after
 *        each kind of set, across a relink, and where two setters write one location. And the compute path,
 *        through the shader manager as the viewer builds programs: the #version a stage gets, a compute program's
 *        work group size, its dispatches direct, by thread count and indirect, the image units its reserved names
 *        get, arrays included, the fixed storage binding points a buffer bound once serves every program at, and a
 *        compaction drawn indirectly. The units every kind of sampler gets, integer textures read through reserved
 *        samplers, and what the debug validator says of images and stencil reads. And who owns a shader object: a program its own, deleted once it links,
 *        the manager the shared ones.
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

#include "../algpubuffer.h"
#include "../alimageslot.h"
#include "../llgl.h"
#include "../llglheaders.h"
#include "../llglslshader.h"
#include "../llimagegl.h"
#include "../llrendertarget.h"
#include "../llshadermgr.h"

#include "llheadlessgl_fixture.h"

#include "../test/lltut.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace tut
{
    namespace
    {
        const char* kVertex =
            "#version 330\n"
            "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";

        // One uniform of each type a setter writes, under reserved names, all folded into the output so each stays
        // active. uGrainAnimate is a bool, which takes the int setters and the float ones alike. The types are this
        // program's own, as the shader manager matches a reserved uniform by name alone: sun_up_factor, an int in the
        // viewer's shaders, is the ivec4 here.
        const char* kFragment =
            "#version 330\n"
            "uniform int light_count;\n"
            "uniform ivec4 sun_up_factor;\n"
            "uniform uint uFrameId;\n"
            "uniform uvec4 cas_param_0;\n"
            "uniform float env_intensity;\n"
            "uniform float minimum_alpha;\n"
            "uniform vec2 screen_res;\n"
            "uniform vec3 sun_dir;\n"
            "uniform vec4 color;\n"
            "uniform vec4 specular_color;\n"
            "uniform vec4 light_diffuse[4];\n"
            "uniform bool uGrainAnimate;\n"
            "out vec4 frag_color;\n"
            "void main()\n"
            "{\n"
            "    vec4 sum = vec4(float(light_count)) + vec4(sun_up_factor) + vec4(float(uFrameId)) + vec4(cas_param_0);\n"
            "    sum += vec4(env_intensity, minimum_alpha, screen_res) + vec4(sun_dir, 0.0) + color + specular_color;\n"
            "    for (int i = 0; i < 4; ++i)\n"
            "    {\n"
            "        sum += light_diffuse[i];\n"
            "    }\n"
            "    frag_color = uGrainAnimate ? sum : -sum;\n"
            "}\n";

        // 2^24: past it a float cannot hold every int, and BIG + 1 rounds to BIG.
        constexpr GLint BIG = 1 << 24;

        // ---- The compute cases ----------------------------------------------------------------------------------

        // Reserved uniform names only the tests use, appended past the product's, in this order: the images take
        // their units in it.
        enum ETestReserved : S32
        {
            TEST_COUNT = LLShaderMgr::END_RESERVED_UNIFORMS,
            TEST_WIDTH,
            TEST_IMAGES,
            TEST_IMAGE_A,
            TEST_IMAGE_B,
            TEST_IMAGE_R32F,
            TEST_IMAGE_RGBA16F,
            TEST_IMAGE_INT,
            TEST_SAMPLERS,
            TEST_SOURCE_MAP,
            TEST_UINT_MAP,
            TEST_INT_MAP,
            TEST_RESERVED_END
        };

        const char* const kTestReservedNames[] =
        {
            "testCount",
            "testWidth",
            "testImages",
            "testImageA",
            "testImageB",
            "testImageR32F",
            "testImageRGBA16F",
            "testImageInt",
            "testSamplers",
            "testSourceMap",
            "testUintMap",
            "testIntMap",
        };
        static_assert(std::size(kTestReservedNames) == TEST_RESERVED_END - TEST_COUNT, "one name per test entry");

        // Storage blocks only the tests use, appended past the product's with their element counts: their fixed
        // binding points follow in this order.
        enum ETestStorage : S32
        {
            TEST_COUNTS = LLShaderMgr::END_RESERVED_STORAGE_BLOCKS,
            TEST_DRAW_LIST,
            TEST_DISPATCH,
            TEST_BLOCK_FIRST,
            TEST_BLOCK_SECOND,
            TEST_BLOCK_ARRAY,
            TEST_STORAGE_END
        };

        const std::pair<const char*, U32> kTestStorageBlocks[] =
        {
            { "TestCounts",      1 },
            { "TestDrawList",    1 },
            { "TestDispatch",    1 },
            { "TestBlockFirst",  1 },
            { "TestBlockSecond", 1 },
            { "TestBlockArray",  2 },
        };
        static_assert(std::size(kTestStorageBlocks) == TEST_STORAGE_END - TEST_COUNTS, "one block per test entry");

        // The shared objects every graphics program attaches, as the viewer's would be, each a function nothing
        // calls: what the compute branch must keep away from a compute program.
        const std::pair<const char*, GLenum> kStubObjects[] =
        {
            { "deferred/globalF.glsl",           GL_FRAGMENT_SHADER },
            { "deferred/textureUtilV.glsl",      GL_VERTEX_SHADER },
            { "objects/nonindexedTextureV.glsl", GL_VERTEX_SHADER },
        };

        // Every file the cases build programs from. No #version: loadShaderFile writes it.
        const std::pair<const char*, const char*> kShaderFiles[] =
        {
            { "deferred/globalF.glsl",
              "vec4 test_global_stub(vec4 c) { return c; }\n" },
            { "deferred/textureUtilV.glsl",
              "vec2 test_texture_util_stub(vec2 t) { return t; }\n" },
            { "objects/nonindexedTextureV.glsl",
              "vec2 test_nonindexed_stub(vec2 t) { return t; }\n" },

            // Each index below the count once, from a dispatch rounded up to whole groups of 64.
            { "test/countC.glsl",
              "layout(local_size_x = 64) in;\n"
              "uniform int testCount;\n"
              "layout(std430) buffer TestCounts\n"
              "{\n"
              "    uint hits[];\n"
              "};\n"
              "void main()\n"
              "{\n"
              "    uint i = gl_GlobalInvocationID.x;\n"
              "    if (i < uint(testCount))\n"
              "    {\n"
              "        atomicAdd(hits[i], 1u);\n"
              "    }\n"
              "}\n" },

            // A pattern into two images of different formats.
            { "test/imageStoreC.glsl",
              "layout(local_size_x = 8, local_size_y = 8) in;\n"
              "layout(r32f) writeonly uniform image2D testImageR32F;\n"
              "layout(rgba16f) writeonly uniform image2D testImageRGBA16F;\n"
              "void main()\n"
              "{\n"
              "    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);\n"
              "    if (any(greaterThanEqual(texel, imageSize(testImageR32F))))\n"
              "    {\n"
              "        return;\n"
              "    }\n"
              "    imageStore(testImageR32F, texel, vec4(float(texel.x * 1000 + texel.y)));\n"
              "    imageStore(testImageRGBA16F, texel, vec4(float(texel.x) * 0.125, float(texel.y) * 0.125, 0.25, 1.0));\n"
              "}\n" },

            // A triangle over the viewport, and a fragment that copies its texel of a texture out.
            { "test/fullscreenV.glsl",
              "void main()\n"
              "{\n"
              "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;\n"
              "    gl_Position = vec4(p, 0.0, 1.0);\n"
              "}\n" },
            { "test/sampleF.glsl",
              "uniform sampler2D testSourceMap;\n"
              "out vec4 frag_color;\n"
              "void main()\n"
              "{\n"
              "    frag_color = texelFetch(testSourceMap, ivec2(gl_FragCoord.xy), 0);\n"
              "}\n" },
            { "test/solidF.glsl",
              "out vec4 frag_color;\n"
              "void main()\n"
              "{\n"
              "    frag_color = vec4(1.0);\n"
              "}\n" },

            // The ids below the count that are multiples of 3, appended to the list after a draw command whose
            // instance count is their count.
            { "test/compactC.glsl",
              "layout(local_size_x = 32) in;\n"
              "uniform int testCount;\n"
              "layout(std430) buffer TestDrawList\n"
              "{\n"
              "    uint vertex_count;\n"
              "    uint instance_count;\n"
              "    uint first_vertex;\n"
              "    uint base_instance;\n"
              "    uint ids[];\n"
              "};\n"
              "void main()\n"
              "{\n"
              "    uint id = gl_GlobalInvocationID.x;\n"
              "    if (id >= uint(testCount) || id % 3u != 0u)\n"
              "    {\n"
              "        return;\n"
              "    }\n"
              "    uint slot = atomicAdd(instance_count, 1u);\n"
              "    ids[slot] = id;\n"
              "}\n" },
            // One point per instance, at the pixel its id names, reading the list the compaction wrote.
            { "test/compactV.glsl",
              "uniform int testWidth;\n"
              "layout(std430) readonly buffer TestDrawList\n"
              "{\n"
              "    uint vertex_count;\n"
              "    uint instance_count;\n"
              "    uint first_vertex;\n"
              "    uint base_instance;\n"
              "    uint ids[];\n"
              "};\n"
              "flat out uint vary_id;\n"
              "void main()\n"
              "{\n"
              "    uint id = ids[gl_InstanceID];\n"
              "    vary_id = id;\n"
              "    uint width = uint(testWidth);\n"
              "    vec2 pixel = vec2(float(id % width), float(id / width)) + 0.5;\n"
              "    gl_Position = vec4(pixel / float(testWidth) * 2.0 - 1.0, 0.0, 1.0);\n"
              "}\n" },
            { "test/compactF.glsl",
              "flat in uint vary_id;\n"
              "out vec4 frag_color;\n"
              "void main()\n"
              "{\n"
              "    frag_color = vec4(float(vary_id + 1u), 0.0, 0.0, 1.0);\n"
              "}\n" },

            // A dispatch command written on the GPU, 16 bytes into its buffer, and a kernel that counts what it runs.
            { "test/indirectWriteC.glsl",
              "layout(local_size_x = 1) in;\n"
              "uniform int testCount;\n"
              "layout(std430) buffer TestDispatch\n"
              "{\n"
              "    uvec4 header;\n"
              "    uint groups_x;\n"
              "    uint groups_y;\n"
              "    uint groups_z;\n"
              "};\n"
              "void main()\n"
              "{\n"
              "    header = uvec4(0u);\n"
              "    groups_x = uint(testCount);\n"
              "    groups_y = 2u;\n"
              "    groups_z = 1u;\n"
              "}\n" },
            { "test/indirectCountC.glsl",
              "layout(local_size_x = 4) in;\n"
              "layout(std430) buffer TestCounts\n"
              "{\n"
              "    uint hits[];\n"
              "};\n"
              "void main()\n"
              "{\n"
              "    atomicAdd(hits[0], 1u);\n"
              "    atomicMax(hits[1], gl_WorkGroupID.x + 1u);\n"
              "    atomicMax(hits[2], gl_WorkGroupID.y + 1u);\n"
              "    atomicMax(hits[3], gl_WorkGroupID.z + 1u);\n"
              "}\n" },

            // Two blocks, declared in the order opposite to the reserved one.
            { "test/remapC.glsl",
              "layout(local_size_x = 1) in;\n"
              "layout(std430) buffer TestBlockSecond\n"
              "{\n"
              "    uint second[];\n"
              "};\n"
              "layout(std430) buffer TestBlockFirst\n"
              "{\n"
              "    uint first[];\n"
              "};\n"
              "void main()\n"
              "{\n"
              "    first[0] = 111u;\n"
              "    second[0] = 222u;\n"
              "}\n" },

            // Two images, the same way round.
            { "test/imageUnitsC.glsl",
              "layout(local_size_x = 8, local_size_y = 8) in;\n"
              "layout(r32f) writeonly uniform image2D testImageB;\n"
              "layout(r32f) writeonly uniform image2D testImageA;\n"
              "void main()\n"
              "{\n"
              "    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);\n"
              "    if (any(greaterThanEqual(texel, imageSize(testImageA))))\n"
              "    {\n"
              "        return;\n"
              "    }\n"
              "    imageStore(testImageA, texel, vec4(1.0));\n"
              "    imageStore(testImageB, texel, vec4(2.0));\n"
              "}\n" },

            // An unsigned texture read through a usampler2D, beside a float sampler that takes the unit before it.
            { "test/uintSampleC.glsl",
              "layout(local_size_x = 8, local_size_y = 8) in;\n"
              "uniform sampler2D diffuseMap;\n"
              "uniform usampler2D testUintMap;\n"
              "uniform int testCount;\n"
              "layout(std430) buffer TestCounts\n"
              "{\n"
              "    uint hits[];\n"
              "};\n"
              "void main()\n"
              "{\n"
              "    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);\n"
              "    ivec2 size = textureSize(testUintMap, 0);\n"
              "    if (any(greaterThanEqual(texel, size)))\n"
              "    {\n"
              "        return;\n"
              "    }\n"
              "    uint value = texelFetch(testUintMap, texel, 0).r;\n"
              "    if (testCount < 0)\n"
              "    {\n"
              "        value += uint(texelFetch(diffuseMap, texel, 0).r);\n"
              "    }\n"
              "    hits[texel.y * size.x + texel.x] = value;\n"
              "}\n" },

            // An integer output of each kind, a function of the pixel: unsigned past 2^24, signed either side of 0.
            { "test/uintWriteF.glsl",
              "out uvec4 frag_uint;\n"
              "void main()\n"
              "{\n"
              "    uvec2 p = uvec2(gl_FragCoord.xy);\n"
              "    frag_uint = uvec4(0x01000001u + p.y * 1000u + p.x, 0u, 0u, 0u);\n"
              "}\n" },
            { "test/intWriteF.glsl",
              "out ivec4 frag_int;\n"
              "void main()\n"
              "{\n"
              "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
              "    frag_int = ivec4(-1 - p.x, p.y * 300 - 1000, 0, 0);\n"
              "}\n" },
            // Both read back at the pixel's own texel through texture(), which the bound sampler governs.
            { "test/intSampleF.glsl",
              "uniform usampler2D testUintMap;\n"
              "uniform isampler2D testIntMap;\n"
              "layout(location = 0) out uvec4 frag_uint;\n"
              "layout(location = 1) out ivec4 frag_int;\n"
              "void main()\n"
              "{\n"
              "    vec2 uv = gl_FragCoord.xy / vec2(textureSize(testUintMap, 0));\n"
              "    frag_uint = texture(testUintMap, uv);\n"
              "    frag_int = texture(testIntMap, uv);\n"
              "}\n" },

            // One reserved block alone, and the same block beside another that comes before it in the table: in a
            // numbering by program each would put the shared block at a different point.
            { "test/firstOnlyC.glsl",
              "layout(local_size_x = 1) in;\n"
              "layout(std430) buffer TestBlockFirst\n"
              "{\n"
              "    uint first[];\n"
              "};\n"
              "void main()\n"
              "{\n"
              "    first[0] = 7u;\n"
              "}\n" },
            { "test/firstAndCountsC.glsl",
              "layout(local_size_x = 1) in;\n"
              "layout(std430) buffer TestCounts\n"
              "{\n"
              "    uint hits[];\n"
              "};\n"
              "layout(std430) buffer TestBlockFirst\n"
              "{\n"
              "    uint first[];\n"
              "};\n"
              "void main()\n"
              "{\n"
              "    first[1] = 9u;\n"
              "    hits[0] = 5u;\n"
              "}\n" },
            // An array of two blocks, each element written; and one that reads only its second.
            { "test/blockArrayC.glsl",
              "layout(local_size_x = 1) in;\n"
              "layout(std430) buffer TestBlockArray\n"
              "{\n"
              "    uint v[];\n"
              "} arrays[2];\n"
              "void main()\n"
              "{\n"
              "    arrays[0].v[0] = 11u;\n"
              "    arrays[1].v[0] = 22u;\n"
              "}\n" },
            { "test/blockArrayOneC.glsl",
              "layout(local_size_x = 1) in;\n"
              "layout(std430) buffer TestBlockArray\n"
              "{\n"
              "    uint v[];\n"
              "} arrays[2];\n"
              "void main()\n"
              "{\n"
              "    arrays[1].v[0] = 33u;\n"
              "}\n" },
            // An image array, before a single image in the reserved table though GL may report it after.
            { "test/imageArrayC.glsl",
              "layout(local_size_x = 8, local_size_y = 8) in;\n"
              "layout(r32f) writeonly uniform image2D testImageA;\n"
              "layout(r32f) writeonly uniform image2D testImages[2];\n"
              "void main()\n"
              "{\n"
              "    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);\n"
              "    if (any(greaterThanEqual(texel, imageSize(testImageA))))\n"
              "    {\n"
              "        return;\n"
              "    }\n"
              "    imageStore(testImageA, texel, vec4(1.0));\n"
              "    imageStore(testImages[0], texel, vec4(2.0));\n"
              "    imageStore(testImages[1], texel, vec4(3.0));\n"
              "}\n" },
            // A signed integer image.
            { "test/imageIntC.glsl",
              "layout(local_size_x = 8, local_size_y = 8) in;\n"
              "layout(r32i) writeonly uniform iimage2D testImageInt;\n"
              "void main()\n"
              "{\n"
              "    imageStore(testImageInt, ivec2(gl_GlobalInvocationID.xy), ivec4(-3));\n"
              "}\n" },

            // A sampler of each kind under a reserved name: an array of two, an array texture, an unsigned one and an
            // integer buffer, each read so it stays active.
            { "test/samplerTypesF.glsl",
              "uniform sampler2D testSamplers[2];\n"
              "uniform sampler2DArray testSourceMap;\n"
              "uniform usampler2D testUintMap;\n"
              "uniform isamplerBuffer testIntMap;\n"
              "out vec4 frag_color;\n"
              "void main()\n"
              "{\n"
              "    vec4 sum = texture(testSamplers[0], vec2(0.5)) + texture(testSamplers[1], vec2(0.5));\n"
              "    sum += texture(testSourceMap, vec3(0.5, 0.5, 0.0));\n"
              "    sum += vec4(texelFetch(testUintMap, ivec2(0), 0));\n"
              "    sum += vec4(texelFetch(testIntMap, 0));\n"
              "    frag_color = sum;\n"
              "}\n" },

            // A call into the stub deferred/globalF.glsl defines, which links only with that object attached.
            { "test/globalCallF.glsl",
              "vec4 test_global_stub(vec4 c);\n"
              "out vec4 frag_color;\n"
              "void main()\n"
              "{\n"
              "    frag_color = test_global_stub(vec4(0.25, 0.5, 0.75, 1.0));\n"
              "}\n" },
        };

        bool writeShaderFiles(const ll_test::TestShaderDir& dir)
        {
            bool ok = true;
            for (const auto& file : kShaderFiles)
            {
                ok = dir.write(file.first, file.second) && ok;
            }
            return ok;
        }

        // The files above on disk, once for the binary.
        const ll_test::TestShaderDir& testShaderDir()
        {
            static const ll_test::TestShaderDir dir;
            static const bool written = writeShaderFiles(dir);
            llassert_always(written);
            return dir;
        }

        // Configure `shader` from `files` and build it through createShader, as the viewer builds its programs: the
        // files at class 1, the default features unless `attach_nothing`.
        bool buildProgram(LLGLSLShader& shader, const std::string& name,
                          std::initializer_list<std::pair<std::string, GLenum>> files, U32 min_glsl_version = 0,
                          bool attach_nothing = false)
        {
            shader.unload();
            shader.mName = name;
            shader.mShaderLevel = 1;
            for (const auto& file : files)
            {
                shader.mShaderFiles.push_back(file);
            }
            shader.mFeatures.mMinGLSLVersion = min_glsl_version;
            shader.mFeatures.attachNothing = attach_nothing;
            return shader.createShader();
        }

        // The first line of a shader object's source: its #version.
        std::string firstSourceLine(GLuint object)
        {
            GLint length = 0;
            glGetShaderiv(object, GL_SHADER_SOURCE_LENGTH, &length);
            std::vector<GLchar> source((size_t)llmax(length, 1), 0);
            glGetShaderSource(object, (GLsizei)source.size(), nullptr, source.data());
            const std::string text(source.data());
            return text.substr(0, text.find('\n'));
        }

        // The #version `file` compiles at as a program's own stage, asking for `min_version`: compiled as
        // createShader compiles one, read, and deleted. Empty if it does not compile.
        std::string compiledVersion(LLShaderMgr& shaders, const char* file, GLenum type, U32 min_version)
        {
            S32 level = 1;
            const GLuint object = shaders.compileShaderFile(file, level, type, nullptr, -1, min_version);
            if (!object)
            {
                return std::string();
            }
            const std::string line = firstSourceLine(object);
            LLShaderMgr::deleteShaderObject(object);
            return line;
        }

        // The shader objects attached to `program`.
        std::vector<GLuint> attachedNames(GLuint program)
        {
            GLuint shaders[16] = { 0 };
            GLsizei count = 0;
            glGetAttachedShaders(program, 16, &count, shaders);
            return std::vector<GLuint>(shaders, shaders + llmax(count, 0));
        }

        // Whether a live shader object has been deleted and waits only for the last program holding it to let go.
        bool flaggedForDeletion(GLuint object)
        {
            GLint status = GL_FALSE;
            glGetShaderiv(object, GL_DELETE_STATUS, &status);
            return status == GL_TRUE;
        }

        // A colour attachment's level 0 as GL holds it.
        template <typename T>
        std::vector<T> readTexture(U32 texture, U32 width, U32 height, U32 channels, GLenum format, GLenum type)
        {
            std::vector<T> texels((size_t)width * height * channels, T(0));
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, texture);
            glGetTexImage(GL_TEXTURE_2D, 0, format, type, texels.data());
            gGL.getTextureSlot(0)->unbind();
            return texels;
        }

        // What the bound target holds, as floats.
        std::vector<F32> readBoundTarget(U32 width, U32 height, U32 channels, GLenum format)
        {
            std::vector<F32> pixels((size_t)width * height * channels, 0.f);
            glReadPixels(0, 0, width, height, format, GL_FLOAT, pixels.data());
            return pixels;
        }

        std::vector<U32> readWords(const ALGPUBuffer& buffer, size_t count)
        {
            std::vector<U32> words(count, 0xFFFFFFFFu);
            buffer.read(0, words.data(), count * sizeof(U32));
            return words;
        }

    // The program the test binds goes back with mGL, and the test's shader manager hands back the shared context's.
    struct llglslshader_data
    {
        ~llglslshader_data()
        {
            LLGLSLShader::unbind();
            for (U32 unit = 0; unit < AL_NUM_IMAGE_SLOTS; ++unit)
            {
                gGL.getImageSlot(unit)->unbind();
            }
            mShader.unload();
            mCompute.unload();
            mCompute2.unload();
            mDraw.unload();
            mDraw2.unload();
            mDraw3.unload();
            mUintImage = nullptr;
            // A case that failed between a bindTarget and its flush left a target bound, which release refuses.
            while (LLRenderTarget::getCurrentBoundTarget())
            {
                LLRenderTarget::getCurrentBoundTarget()->flush();
            }
            for (LLRenderTarget& target : mTargets)
            {
                target.release();
            }
            for (ALGPUBuffer& buffer : mBuffers)
            {
                buffer.release();
            }
            mShaders.clearShaderObjects();
        }

        // The test's names in the reserved list and the shared objects compiled, as the viewer has them before it
        // builds a program.
        void setUpShaders()
        {
            ensure_equals("the test's names follow the product's", mShaders.mReservedUniforms.size(),
                          size_t(LLShaderMgr::END_RESERVED_UNIFORMS));
            for (const char* name : kTestReservedNames)
            {
                mShaders.mReservedUniforms.push_back(name);
            }
            ensure_equals("the test's blocks follow the product's", mShaders.mReservedStorageBlocks.size(),
                          size_t(LLShaderMgr::END_RESERVED_STORAGE_BLOCKS));
            for (const auto& block : kTestStorageBlocks)
            {
                mShaders.mReservedStorageBlocks.push_back({ block.first, block.second });
            }
            for (const auto& object : kStubObjects)
            {
                S32 level = 1;
                ensure(std::string("compiled ") + object.first,
                       mShaders.loadShaderFile(object.first, level, object.second) != 0);
            }
        }

        // setUpShaders, and compute in use, or the case skipped where the context has none.
        void setUpCompute()
        {
            if (!ll_test::contextHasCompute())
            {
                skip("the context has no compute");
            }
            LLRender::sCompute = true;
            setUpShaders();
        }

        // The test program, compiled and linked through LLGLSLShader's own path, mapped and bound.
        void setUp()
        {
            // An error an earlier test left on the shared context is its own: each test ends asserting none of its
            // calls raised one.
            while (glGetError() != GL_NO_ERROR)
            {
            }

            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            ensure("shaders compile", vs && fs);
            mShader.mProgramObject = glCreateProgram();
            mShader.mName = "uniform cache (test)";
            mShader.attachObject(vs);
            mShader.attachObject(fs);
            const bool linked = mShader.mapAttributes();
            glDeleteShader(vs);
            glDeleteShader(fs);
            ensure("program links", linked);
            ensure("program maps", mShader.mapUniforms());
            mShader.bind();
        }

        // The same program linked by GL alone, never through LLGLSLShader::link. 0 if it does not link.
        static GLuint linkRaw()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            if (!vs || !fs)
            {
                if (vs) glDeleteShader(vs);
                if (fs) glDeleteShader(fs);
                return 0;
            }
            const GLuint program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked != GL_TRUE)
            {
                glDeleteProgram(program);
                return 0;
            }
            return program;
        }

        GLint location(const char* name) const
        {
            return glGetUniformLocation(mShader.mProgramObject, name);
        }

        // What the program holds at a uniform, as GL reads it back, against what a check expects of its first
        // components. Floats are compared by their bits, so a check is exact.
        void ensureInts(const std::string& what, const char* name, std::initializer_list<GLint> expected) const
        {
            std::array<GLint, 4> held = {};
            glGetUniformiv(mShader.mProgramObject, location(name), held.data());
            size_t i = 0;
            for (const GLint value : expected)
            {
                ensure_equals(what + ", component " + std::to_string(i), held[i], value);
                ++i;
            }
        }

        void ensureUints(const std::string& what, const char* name, std::initializer_list<GLuint> expected) const
        {
            std::array<GLuint, 4> held = {};
            glGetUniformuiv(mShader.mProgramObject, location(name), held.data());
            size_t i = 0;
            for (const GLuint value : expected)
            {
                ensure_equals(what + ", component " + std::to_string(i), held[i], value);
                ++i;
            }
        }

        void ensureFloats(const std::string& what, const char* name, std::initializer_list<F32> expected) const
        {
            std::array<F32, 4> held = {};
            glGetUniformfv(mShader.mProgramObject, location(name), held.data());
            size_t i = 0;
            for (const F32 value : expected)
            {
                ensure_equals(what + ", component " + std::to_string(i) + " bits",
                              std::bit_cast<U32>(held[i]), std::bit_cast<U32>(value));
                ++i;
            }
        }

        ll_test::SharedGLScope mGL;
        // Made before the programs and gone after them: mapUniforms reads its reserved names. Its files are the
        // compute cases'.
        ll_test::TestShaderMgr mShaders{ testShaderDir().prefix() };
        LLGLSLShader mShader;
        LLGLSLShader mCompute;
        LLGLSLShader mCompute2;
        LLGLSLShader mDraw;
        LLGLSLShader mDraw2;
        LLGLSLShader mDraw3;
        LLRenderTarget mTargets[3];
        ALGPUBuffer mBuffers[2];
        LLPointer<LLImageGL> mUintImage;
    };

    typedef test_group<llglslshader_data> llglslshader_t;
    typedef llglslshader_t::object llglslshader_object_t;
    tut::llglslshader_t tut_llglslshader("llglslshader_gl");
    }

    // Ints and uints arrive exactly past 2^24: each set below differs from the one before only in values that
    // convert to the same float, and only in some of its components.
    template<> template<>
    void llglslshader_object_t::test<1>()
    {
        setUp();

        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, BIG);
        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, BIG + 1);
        ensureInts("int", "light_count", { BIG + 1 });

        const GLint first[4]  = { BIG,     -BIG,     BIG, BIG };
        const GLint second[4] = { BIG,     -BIG,     BIG, BIG + 1 };
        const GLint third[4]  = { BIG + 1, -BIG - 1, BIG, BIG + 1 };
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, first);
        ensureInts("ivec4", "sun_up_factor", { BIG, -BIG, BIG, BIG });
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, second);
        ensureInts("ivec4, w alone changed", "sun_up_factor", { BIG, -BIG, BIG, BIG + 1 });
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, third);
        ensureInts("ivec4, x and y changed", "sun_up_factor", { BIG + 1, -BIG - 1, BIG, BIG + 1 });

        mShader.uniform1ui(LLShaderMgr::FRAME_ID, (GLuint)BIG);
        mShader.uniform1ui(LLShaderMgr::FRAME_ID, (GLuint)BIG + 1);
        ensureUints("uint", "uFrameId", { (GLuint)BIG + 1 });

        // The sharpening constants are floats' bits. 1.0's and the next float up's are both 1065353216 as floats.
        const GLuint one[4]  = { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u };
        const GLuint next[4] = { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800001u };
        mShader.uniform4uiv(LLShaderMgr::CAS_PARAM_0, 1, one);
        mShader.uniform4uiv(LLShaderMgr::CAS_PARAM_0, 1, next);
        ensureUints("uvec4, w alone changed", "cas_param_0", { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800001u });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A, then B, then A again holds A, for every kind of set: each upload replaces what its location's slot holds,
    // so the third set is compared with B. The pairs mix a setter's value and array forms, which share a kind.
    template<> template<>
    void llglslshader_object_t::test<2>()
    {
        setUp();

        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 0.5f);
        ensureFloats("vec4 B", "color", { 0.25f, 0.5f, 0.75f, 0.5f });
        const F32 color[4] = { 0.25f, 0.5f, 0.75f, 1.f };
        mShader.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color);
        ensureFloats("vec4 A again", "color", { 0.25f, 0.5f, 0.75f, 1.f });

        mShader.uniform3f(LLShaderMgr::DEFERRED_SUN_DIR, 0.f, 0.5f, 1.f);
        const F32 sun[3] = { 0.f, 0.5f, 0.25f };
        mShader.uniform3fv(LLShaderMgr::DEFERRED_SUN_DIR, 1, sun);
        ensureFloats("vec3 B", "sun_dir", { 0.f, 0.5f, 0.25f });
        mShader.uniform3f(LLShaderMgr::DEFERRED_SUN_DIR, 0.f, 0.5f, 1.f);
        ensureFloats("vec3 A again", "sun_dir", { 0.f, 0.5f, 1.f });

        mShader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, 640.f, 480.f);
        const F32 res[2] = { 640.f, 360.f };
        mShader.uniform2fv(LLShaderMgr::DEFERRED_SCREEN_RES, 1, res);
        ensureFloats("vec2 B", "screen_res", { 640.f, 360.f });
        mShader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, 640.f, 480.f);
        ensureFloats("vec2 A again", "screen_res", { 640.f, 480.f });

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.25f);
        const F32 intensity = 0.5f;
        mShader.uniform1fv(LLShaderMgr::ENVIRONMENT_INTENSITY, 1, &intensity);
        ensureFloats("float B", "env_intensity", { 0.5f });
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.25f);
        ensureFloats("float A again", "env_intensity", { 0.25f });

        const GLint ivec_a[4] = { 1, 2, 3, 4 };
        const GLint ivec_b[4] = { 1, 2, 3, 5 };
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, ivec_a);
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, ivec_b);
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, ivec_a);
        ensureInts("ivec4 A again", "sun_up_factor", { 1, 2, 3, 4 });

        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        const GLint count = 4;
        mShader.uniform1iv(LLShaderMgr::MULTI_LIGHT_COUNT, 1, &count);
        ensureInts("int B", "light_count", { 4 });
        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        ensureInts("int A again", "light_count", { 3 });

        mShader.uniform1ui(LLShaderMgr::FRAME_ID, 7u);
        mShader.uniform1ui(LLShaderMgr::FRAME_ID, 8u);
        mShader.uniform1ui(LLShaderMgr::FRAME_ID, 7u);
        ensureUints("uint A again", "uFrameId", { 7u });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A float set twice holds its value, and a new value replaces it. Raw uploads behind the cache's back show
    // which sets reached GL: just after a link the cache holds nothing, not a zero, so a set of 0 uploads; and a set
    // of the value last uploaded does not.
    template<> template<>
    void llglslshader_object_t::test<3>()
    {
        setUp();
        const GLint loc = location("env_intensity");
        ensure("env_intensity is active", loc >= 0);

        glUniform1f(loc, 5.f);
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.f);
        ensureFloats("the first set uploads, a zero too", "env_intensity", { 0.f });

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        ensureFloats("set twice", "env_intensity", { 0.5f });

        glUniform1f(loc, 2.f);
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        ensureFloats("the value last uploaded is not uploaded again", "env_intensity", { 2.f });

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.75f);
        ensureFloats("a new value replaces it", "env_intensity", { 0.75f });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // LLGLSLShader::link forgets the cache: the relink resets every uniform to 0, and the value set before it,
    // set again, arrives.
    template<> template<>
    void llglslshader_object_t::test<4>()
    {
        setUp();
        const GLint before = location("color");
        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        ensureFloats("set before the relink", "color", { 0.25f, 0.5f, 0.75f, 1.f });

        ensure("program relinks", mShader.link());
        if (location("color") != before)
        {
            skip("the relink moved the uniform, so a stale slot would not be consulted");
        }
        ensureFloats("the relink reset it", "color", { 0.f, 0.f, 0.f, 0.f });

        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        ensureFloats("set again after the relink", "color", { 0.25f, 0.5f, 0.75f, 1.f });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // mapUniforms forgets the cache, and setMinimumAlpha's own: a new program, linked by GL alone and mapped, holds
    // the values set again after it, though they are the ones set on the program it replaced.
    template<> template<>
    void llglslshader_object_t::test<5>()
    {
        setUp();
        const GLint before = location("color");
        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        mShader.setMinimumAlpha(0.5f);
        ensureFloats("colour on the old program", "color", { 0.25f, 0.5f, 0.75f, 1.f });
        ensureFloats("minimum alpha on the old program", "minimum_alpha", { 0.5f });

        const GLuint fresh = linkRaw();
        ensure("the new program links", fresh != 0);
        LLGLSLShader::unbind();
        const GLuint old = mShader.mProgramObject;
        mShader.mProgramObject = fresh;
        forget_program_samplers(old);
        glDeleteProgram(old);
        ensure("the new program maps", mShader.mapUniforms());
        mShader.bind();
        if (location("color") != before)
        {
            skip("the new program put the uniform elsewhere, so a stale slot would not be consulted");
        }
        ensureFloats("the new program starts at 0", "color", { 0.f, 0.f, 0.f, 0.f });

        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        ensureFloats("colour set again", "color", { 0.25f, 0.5f, 0.75f, 1.f });
        mShader.setMinimumAlpha(0.5f);
        ensureFloats("minimum alpha set again", "minimum_alpha", { 0.5f });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A bool takes the int setters and the float ones, and holds whichever set it last, the int set after a float
    // one included. INT_MIN and -0.0 share their bits: true as an int, false as a float, so only the kind a slot
    // records tells the float set from the int one before it.
    template<> template<>
    void llglslshader_object_t::test<6>()
    {
        setUp();

        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 1);
        ensureInts("true by int", "uGrainAnimate", { 1 });
        mShader.uniform1f(LLShaderMgr::GRAIN_ANIMATE, 0.f);
        ensureInts("false by float", "uGrainAnimate", { 0 });
        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 1);
        ensureInts("true by int again", "uGrainAnimate", { 1 });

        // Read at run time, so nothing folds the sign of the zero away.
        volatile U32 sign_bit = 0x80000000u;
        const F32 negative_zero = std::bit_cast<F32>(static_cast<U32>(sign_bit));
        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, INT_MIN);
        ensureInts("true by INT_MIN", "uGrainAnimate", { 1 });
        mShader.uniform1f(LLShaderMgr::GRAIN_ANIMATE, negative_zero);
        ensureInts("false by -0.0, the same bits", "uGrainAnimate", { 0 });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // The reserved index and the name of one uniform share its slot: A by index, B by name, then A by index holds A.
    template<> template<>
    void llglslshader_object_t::test<7>()
    {
        setUp();
        const LLStaticHashedString light_count("light_count");

        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        mShader.uniform1i(light_count, 4);
        ensureInts("by name", "light_count", { 4 });
        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        ensureInts("by index after the name", "light_count", { 3 });
        mShader.uniform1i(light_count, 4);
        ensureInts("by name after the index", "light_count", { 4 });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // The fast setters upload without comparing and leave the cache describing what they uploaded: a cached set of
    // the value before a fast one arrives.
    template<> template<>
    void llglslshader_object_t::test<8>()
    {
        setUp();

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        mShader.fastUniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.25f);
        ensureFloats("fast float", "env_intensity", { 0.25f });
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        ensureFloats("cached float after the fast one", "env_intensity", { 0.5f });

        const F32 a[4] = { 0.25f, 0.5f, 0.75f, 1.f };
        const F32 b[4] = { 1.f, 0.75f, 0.5f, 0.25f };
        mShader.uniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, a);
        mShader.fastUniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, b);
        ensureFloats("fast vec4", "specular_color", { 1.f, 0.75f, 0.5f, 0.25f });
        mShader.uniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, a);
        ensureFloats("cached vec4 after the fast one", "specular_color", { 0.25f, 0.5f, 0.75f, 1.f });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // An array set uploads every time and forgets what its location held: element 0 set alone, then the array over
    // it, then element 0 alone again holds element 0's value; and an array whose element 0 is unchanged still
    // reaches GL.
    template<> template<>
    void llglslshader_object_t::test<9>()
    {
        setUp();

        const F32 single[4]    = { 0.25f, 0.5f, 0.75f, 1.f };
        const F32 two[8]       = { 1.f, 1.f, 1.f, 1.f,   0.5f, 0.5f, 0.5f, 0.5f };
        const F32 two_again[8] = { 1.f, 1.f, 1.f, 1.f,   0.25f, 0.25f, 0.25f, 0.25f };

        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 1, single);
        ensureFloats("element 0 alone", "light_diffuse[0]", { 0.25f, 0.5f, 0.75f, 1.f });
        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 2, two);
        ensureFloats("element 0 by the array", "light_diffuse[0]", { 1.f, 1.f, 1.f, 1.f });
        ensureFloats("element 1 by the array", "light_diffuse[1]", { 0.5f, 0.5f, 0.5f, 0.5f });
        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 1, single);
        ensureFloats("element 0 alone again", "light_diffuse[0]", { 0.25f, 0.5f, 0.75f, 1.f });

        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 2, two);
        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 2, two_again);
        ensureFloats("element 1 by an array whose element 0 did not change", "light_diffuse[1]",
                     { 0.25f, 0.25f, 0.25f, 0.25f });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // Where the context has compute, its limits are at least what GL 4.3 promises, which is what the features size
    // themselves by.
    template<> template<>
    void llglslshader_object_t::test<10>()
    {
        if (!gGLManager.mHasCompute)
        {
            skip("the context has no compute");
        }
        for (U32 i = 0; i < 3; ++i)
        {
            ensure("work group count " + std::to_string(i), gGLManager.mMaxComputeWorkGroupCount[i] >= 65535);
        }
        ensure("work group size x", gGLManager.mMaxComputeWorkGroupSize[0] >= 1024);
        ensure("work group size y", gGLManager.mMaxComputeWorkGroupSize[1] >= 1024);
        ensure("work group size z", gGLManager.mMaxComputeWorkGroupSize[2] >= 64);
        ensure("invocations", gGLManager.mMaxComputeWorkGroupInvocations >= 1024);
        ensure("shared memory", gGLManager.mMaxComputeSharedMemorySize >= 32768);
        ensure("image units", gGLManager.mMaxImageUnits >= 8);
        ensure("compute storage blocks", gGLManager.mMaxComputeShaderStorageBlocks >= 8);
        ensure("storage bindings", gGLManager.mMaxShaderStorageBufferBindings >= 8);
        ensure("storage block size", gGLManager.mMaxShaderStorageBlockSize >= (S64(1) << 27));
        const S32 alignment = gGLManager.mShaderStorageBufferOffsetAlignment;
        ensure("storage offset alignment a power of two", alignment > 0 && (alignment & (alignment - 1)) == 0);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A compute program built through createShader links -- which GL refuses a compute object beside any other, so the
    // features' graphics objects stayed away -- from its compute object at #version 430, which it holds no longer
    // once linked; its work group size reads back; its block takes the table's point; and dispatchThreads over a count
    // that is not a multiple of the group size reaches each index exactly once.
    template<> template<>
    void llglslshader_object_t::test<11>()
    {
        setUpCompute();
        ensure_equals("a compute stage at GLSL 4.30",
                      compiledVersion(mShaders, "test/countC.glsl", GL_COMPUTE_SHADER, 0), std::string("#version 430"));
        ensure("count program builds",
               buildProgram(mCompute, "count (test)", { { "test/countC.glsl", GL_COMPUTE_SHADER } }));
        ensure("it is compute", mCompute.isCompute());
        ensure("it holds no object once linked", attachedNames(mCompute.mProgramObject).empty());

        ensure_equals("local size x", mCompute.mComputeLocalSize[0], 64u);
        ensure_equals("local size y", mCompute.mComputeLocalSize[1], 1u);
        ensure_equals("local size z", mCompute.mComputeLocalSize[2], 1u);
        ensure("the table gives its block a point", mShaders.getStorageBlockBinding(TEST_COUNTS) >= 0);
        ensure_equals("its block is at the table's point", mCompute.getStorageBinding(TEST_COUNTS),
                      mShaders.getStorageBlockBinding(TEST_COUNTS));

        // 15.6 groups' worth, in a buffer with room for 17.
        constexpr U32 COUNT = 1000;
        constexpr U32 WORDS = 17 * 64;
        ensure("counts allocate", mBuffers[0].allocate(WORDS * sizeof(U32), 0, nullptr, "counts (test)"));
        mBuffers[0].clear();

        mCompute.bind();
        mCompute.uniform1i(TEST_COUNT, (GLint)COUNT);
        mCompute.bindStorageBuffer(TEST_COUNTS, mBuffers[0]);
        mCompute.dispatchThreads(COUNT);

        const std::vector<U32> hits = readWords(mBuffers[0], WORDS);
        for (U32 i = 0; i < WORDS; ++i)
        {
            ensure_equals("index " + std::to_string(i) + (i < COUNT ? " once" : " untouched"), hits[i],
                          i < COUNT ? 1u : 0u);
        }
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // The #version a stage compiles at: the usual one for an ordinary program's own stage, never raised, and for the
    // shared objects likewise; 4.30 for a stage asking for it. A program whose stage reads a storage block builds only
    // when its features ask for 4.30, which createShader hands its own stages, and links with shared objects at the
    // usual version. On a context below 4.30 a compute program, or one asking for 4.30, does not build.
    template<> template<>
    void llglslshader_object_t::test<12>()
    {
        setUpShaders();
        if (gGLManager.mGLSLVersionMajor < 4)
        {
            skip("the context's GLSL is below 4.00");
        }
        const std::string usual = gGLManager.mGLSLVersionMinor >= 20 ? "#version 420" : "#version 400";

        ensure("ordinary program builds",
               buildProgram(mDraw, "ladder (test)",
                            { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/solidF.glsl", GL_FRAGMENT_SHADER } }));
        ensure_equals("an ordinary stage at the usual version",
                      compiledVersion(mShaders, "test/solidF.glsl", GL_FRAGMENT_SHADER, 0), usual);
        size_t shared = 0;
        for (const auto& stage : mShaders.mShaderObjects)
        {
            for (const auto& object : stage)
            {
                ensure_equals(object.first + " at the usual version", firstSourceLine(object.second), usual);
                ++shared;
            }
        }
        ensure_equals("the three shared objects", shared, size_t(3));

        if (ll_test::contextHasCompute())
        {
            ensure_equals("a stage asking for 4.30 at 4.30",
                          compiledVersion(mShaders, "test/solidF.glsl", GL_FRAGMENT_SHADER, 430),
                          std::string("#version 430"));
            if (gGLManager.mMaxVertexShaderStorageBlocks >= 1)
            {
                const std::initializer_list<std::pair<std::string, GLenum>> reader =
                {
                    { "test/compactV.glsl", GL_VERTEX_SHADER },
                    { "test/compactF.glsl", GL_FRAGMENT_SHADER },
                };
                ensure("a storage-block reader does not build at the usual version",
                       !buildProgram(mDraw2, "ladder reader (test)", reader));
                ensure("it builds asking for 4.30, beside the shared objects at the usual version",
                       buildProgram(mDraw2, "ladder reader 430 (test)", reader, 430));
            }
        }

        // As the feature manager leaves a context it held back.
        const S32 major = gGLManager.mGLSLVersionMajor;
        const S32 minor = gGLManager.mGLSLVersionMinor;
        gGLManager.mGLSLVersionMajor = 4;
        gGLManager.mGLSLVersionMinor = 20;
        const bool compute_built =
            buildProgram(mCompute, "count at 4.20 (test)", { { "test/countC.glsl", GL_COMPUTE_SHADER } });
        const bool raised_built =
            buildProgram(mDraw3, "ladder 430 at 4.20 (test)",
                         { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/solidF.glsl", GL_FRAGMENT_SHADER } },
                         430);
        gGLManager.mGLSLVersionMajor = major;
        gGLManager.mGLSLVersionMinor = minor;
        ensure("no compute program below 4.30", !compute_built);
        ensure("no program asking for 4.30 below it", !raised_built);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A kernel stores into an R32F and an RGBA16F target through their programs' image units. Past the barriers the
    // textures hold the pattern, read back directly and sampled by a fragment program.
    template<> template<>
    void llglslshader_object_t::test<13>()
    {
        setUpCompute();
        constexpr U32 W = 37;
        constexpr U32 H = 23;
        ensure("R32F target allocates", mTargets[0].allocate(W, H, GL_R32F));
        ensure("RGBA16F target allocates", mTargets[1].allocate(W, H, GL_RGBA16F));
        ensure("output target allocates", mTargets[2].allocate(W, H, GL_RGBA32F));
        ensure("store program builds",
               buildProgram(mCompute, "image store (test)", { { "test/imageStoreC.glsl", GL_COMPUTE_SHADER } }));
        ensure("sample program builds",
               buildProgram(mDraw, "image sample (test)",
                            { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/sampleF.glsl", GL_FRAGMENT_SHADER } }));

        const S32 r32f_unit = mCompute.getImageUnit(TEST_IMAGE_R32F);
        const S32 rgba_unit = mCompute.getImageUnit(TEST_IMAGE_RGBA16F);
        ensure("R32F image has a unit", r32f_unit >= 0);
        ensure("RGBA16F image has a unit", rgba_unit >= 0);
        ensure("the units differ", r32f_unit != rgba_unit);

        mCompute.bind();
        ensure_equals("R32F bound at its unit",
                      mCompute.bindImage(TEST_IMAGE_R32F, &mTargets[0], ALImageSlot::WRITE_ONLY), r32f_unit);
        ensure_equals("RGBA16F bound at its unit",
                      mCompute.bindImage(TEST_IMAGE_RGBA16F, &mTargets[1], ALImageSlot::WRITE_ONLY), rgba_unit);
        mCompute.dispatchThreads(W, H);
        gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_FETCH | LLRender::BARRIER_TEXTURE_UPDATE);
        mCompute.unbindImage(TEST_IMAGE_R32F);
        mCompute.unbindImage(TEST_IMAGE_RGBA16F);

        const std::vector<F32> r32f = readTexture<F32>(mTargets[0].getTexture(), W, H, 1, GL_RED, GL_FLOAT);
        const std::vector<F32> rgba = readTexture<F32>(mTargets[1].getTexture(), W, H, 4, GL_RGBA, GL_FLOAT);
        for (U32 y = 0; y < H; ++y)
        {
            for (U32 x = 0; x < W; ++x)
            {
                const std::string at = " at " + std::to_string(x) + "," + std::to_string(y);
                const size_t i = (size_t)y * W + x;
                ensure_equals("R32F" + at, r32f[i], F32(x * 1000 + y));
                ensure_equals("RGBA16F r" + at, rgba[i * 4 + 0], F32(x) * 0.125f);
                ensure_equals("RGBA16F g" + at, rgba[i * 4 + 1], F32(y) * 0.125f);
                ensure_equals("RGBA16F b" + at, rgba[i * 4 + 2], 0.25f);
                ensure_equals("RGBA16F a" + at, rgba[i * 4 + 3], 1.f);
            }
        }

        LLGLDisable blend(GL_BLEND);
        for (U32 source = 0; source < 2; ++source)
        {
            mTargets[2].bindTarget();
            gGL.setClearColor(LLColor4::transparent);
            mTargets[2].clear();
            mDraw.bind();
            ensure("source bound", mDraw.bindTexture(TEST_SOURCE_MAP, &mTargets[source], ALSamplers::PointClamp) >= 0);
            gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
            const std::vector<F32> drawn = readBoundTarget(W, H, 4, GL_RGBA);
            mTargets[2].flush();
            mDraw.unbindTexture(TEST_SOURCE_MAP);

            for (U32 y = 0; y < H; ++y)
            {
                for (U32 x = 0; x < W; ++x)
                {
                    const std::string at = (source ? " RGBA16F" : " R32F") + std::string(" sampled at ")
                                         + std::to_string(x) + "," + std::to_string(y);
                    const size_t i = ((size_t)y * W + x) * 4;
                    const F32 expected[4] =
                    {
                        source ? F32(x) * 0.125f : F32(x * 1000 + y),
                        source ? F32(y) * 0.125f : 0.f,
                        source ? 0.25f : 0.f,
                        1.f,
                    };
                    for (U32 c = 0; c < 4; ++c)
                    {
                        ensure_equals("component " + std::to_string(c) + at, drawn[i + c], expected[c]);
                    }
                }
            }
        }
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // GPU compaction drawn indirectly: a kernel keeps the ids passing a test, counting them into the instance count
    // of the draw command at the head of their list; past the barriers an indirect draw, whose vertex stage reads the
    // same buffer as a storage block, puts one point at each kept id's pixel and none anywhere else. The list is bound
    // once, for the kernel, and the draw finds it at the same point.
    template<> template<>
    void llglslshader_object_t::test<14>()
    {
        setUpCompute();
        if (gGLManager.mMaxVertexShaderStorageBlocks < 1)
        {
            skip("the context's vertex stage reads no storage blocks");
        }
        constexpr U32 COUNT = 100;
        constexpr U32 SIDE = 16;
        constexpr U32 KEPT = (COUNT + 2) / 3;

        // One vertex a point, no instances yet, then room for every id.
        std::vector<U32> list(4 + COUNT, 0u);
        list[0] = 1u;
        ensure("list allocates", mBuffers[0].allocate(list.size() * sizeof(U32), 0, list.data(), "compaction (test)"));
        ensure("output target allocates", mTargets[0].allocate(SIDE, SIDE, GL_R32F));
        ensure("compaction builds",
               buildProgram(mCompute, "compaction (test)", { { "test/compactC.glsl", GL_COMPUTE_SHADER } }));
        ensure("draw builds",
               buildProgram(mDraw, "compaction draw (test)",
                            { { "test/compactV.glsl", GL_VERTEX_SHADER }, { "test/compactF.glsl", GL_FRAGMENT_SHADER } },
                            430));
        const S32 point = mShaders.getStorageBlockBinding(TEST_DRAW_LIST);
        ensure_equals("the kernel's list at the table's point", mCompute.getStorageBinding(TEST_DRAW_LIST), point);
        ensure_equals("and the draw's", mDraw.getStorageBinding(TEST_DRAW_LIST), point);

        mCompute.bind();
        mCompute.uniform1i(TEST_COUNT, (GLint)COUNT);
        mCompute.bindStorageBuffer(TEST_DRAW_LIST, mBuffers[0]);
        mCompute.dispatchThreads(COUNT);
        gGL.memoryBarrier(LLRender::BARRIER_COMMAND | LLRender::BARRIER_STORAGE);

        LLGLDisable blend(GL_BLEND);
        mTargets[0].bindTarget();
        gGL.setClearColor(LLColor4::transparent);
        mTargets[0].clear();
        mDraw.bind();
        mDraw.uniform1i(TEST_WIDTH, (GLint)SIDE);
        ensure_equals("the list still bound for the draw", gGL.getStorageBuffer((U32)point), mBuffers[0].getGLName());
        gGL.drawArraysIndirect(LLRender::POINTS, mBuffers[0], 0);
        const std::vector<F32> drawn = readBoundTarget(SIDE, SIDE, 1, GL_RED);
        mTargets[0].flush();

        for (U32 id = 0; id < SIDE * SIDE; ++id)
        {
            const bool kept = id < COUNT && id % 3 == 0;
            ensure_equals("id " + std::to_string(id) + (kept ? " drawn" : " not drawn"), drawn[id],
                          kept ? F32(id + 1) : 0.f);
        }
        ensure_equals("instances counted", readWords(mBuffers[0], 2)[1], KEPT);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // dispatchIndirect runs the group counts a buffer holds, here written by a kernel before it 16 bytes in: every
    // invocation of 5 x 2 x 1 groups of 4 counted once, and the last group seen in each direction.
    template<> template<>
    void llglslshader_object_t::test<15>()
    {
        setUpCompute();
        const std::vector<U32> zeros(8, 0u);
        ensure("command buffer allocates", mBuffers[0].allocate(8 * sizeof(U32), 0, zeros.data(), "dispatch (test)"));
        ensure("counts allocate", mBuffers[1].allocate(4 * sizeof(U32), 0, zeros.data(), "counts (test)"));
        ensure("writer builds",
               buildProgram(mCompute, "dispatch writer (test)", { { "test/indirectWriteC.glsl", GL_COMPUTE_SHADER } }));
        ensure("counter builds",
               buildProgram(mCompute2, "dispatch counter (test)", { { "test/indirectCountC.glsl", GL_COMPUTE_SHADER } }));

        mCompute.bind();
        mCompute.uniform1i(TEST_COUNT, 5);
        mCompute.bindStorageBuffer(TEST_DISPATCH, mBuffers[0]);
        mCompute.dispatch(1);
        gGL.memoryBarrier(LLRender::BARRIER_COMMAND);

        mCompute2.bind();
        mCompute2.bindStorageBuffer(TEST_COUNTS, mBuffers[1]);
        mCompute2.dispatchIndirect(mBuffers[0], 16);

        const std::vector<U32> command = readWords(mBuffers[0], 7);
        ensure_equals("command x", command[4], 5u);
        ensure_equals("command y", command[5], 2u);
        ensure_equals("command z", command[6], 1u);
        const std::vector<U32> hits = readWords(mBuffers[1], 4);
        ensure_equals("invocations", hits[0], 5u * 2u * 1u * 4u);
        ensure_equals("groups in x", hits[1], 5u);
        ensure_equals("groups in y", hits[2], 2u);
        ensure_equals("groups in z", hits[3], 1u);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // Two storage blocks declared in the order opposite to the table's take the table's points, which run in its
    // order, and bindStorageBuffer routes each buffer to its own block: swapping the buffers swaps what each holds.
    template<> template<>
    void llglslshader_object_t::test<16>()
    {
        setUpCompute();
        ensure("remap builds", buildProgram(mCompute, "block remap (test)", { { "test/remapC.glsl", GL_COMPUTE_SHADER } }));
        const S32 first = mShaders.getStorageBlockBinding(TEST_BLOCK_FIRST);
        ensure_equals("the first block at its point", mCompute.getStorageBinding(TEST_BLOCK_FIRST), first);
        ensure_equals("the second at the next", mCompute.getStorageBinding(TEST_BLOCK_SECOND), first + 1);
        ensure_equals("an undeclared block has none", mCompute.getStorageBinding(TEST_COUNTS), -1);
        ensure_equals("nor does an index past the table", mCompute.getStorageBinding(TEST_STORAGE_END), -1);

        const std::vector<U32> zeros(4, 0u);
        ensure("a allocates", mBuffers[0].allocate(4 * sizeof(U32), 0, zeros.data(), "a (test)"));
        ensure("b allocates", mBuffers[1].allocate(4 * sizeof(U32), 0, zeros.data(), "b (test)"));

        mCompute.bind();
        mCompute.bindStorageBuffer(TEST_BLOCK_FIRST, mBuffers[0]);
        mCompute.bindStorageBuffer(TEST_BLOCK_SECOND, mBuffers[1]);
        mCompute.dispatch(1);
        ensure_equals("a holds the first block", readWords(mBuffers[0], 1)[0], 111u);
        ensure_equals("b holds the second block", readWords(mBuffers[1], 1)[0], 222u);

        mBuffers[0].clear();
        mBuffers[1].clear();
        gGL.memoryBarrier(LLRender::BARRIER_STORAGE);
        mCompute.bindStorageBuffer(TEST_BLOCK_FIRST, mBuffers[1]);
        mCompute.bindStorageBuffer(TEST_BLOCK_SECOND, mBuffers[0]);
        mCompute.dispatch(1);
        ensure_equals("swapped, a holds the second block", readWords(mBuffers[0], 1)[0], 222u);
        ensure_equals("swapped, b holds the first block", readWords(mBuffers[1], 1)[0], 111u);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // Two image uniforms, declared the opposite way round, take distinct units in reserved order, and bindImage
    // routes each texture to its own.
    template<> template<>
    void llglslshader_object_t::test<17>()
    {
        setUpCompute();
        constexpr U32 SIDE = 16;
        ensure("units builds",
               buildProgram(mCompute, "image units (test)", { { "test/imageUnitsC.glsl", GL_COMPUTE_SHADER } }));
        ensure_equals("the first reserved image first", mCompute.getImageUnit(TEST_IMAGE_A), 0);
        ensure_equals("the second reserved image second", mCompute.getImageUnit(TEST_IMAGE_B), 1);
        ensure_equals("an undeclared image has none", mCompute.getImageUnit(TEST_IMAGE_R32F), -1);
        ensure_equals("a sampler has none", mCompute.getImageUnit(LLShaderMgr::DIFFUSE_MAP), -1);
        ensure_equals("nor a texture unit", mCompute.getTextureChannel(TEST_IMAGE_A), -1);

        ensure("a allocates", mTargets[0].allocate(SIDE, SIDE, GL_R32F));
        ensure("b allocates", mTargets[1].allocate(SIDE, SIDE, GL_R32F));
        mCompute.bind();
        ensure_equals("a bound", mCompute.bindImage(TEST_IMAGE_A, &mTargets[0], ALImageSlot::WRITE_ONLY), 0);
        ensure_equals("b bound", mCompute.bindImage(TEST_IMAGE_B, &mTargets[1], ALImageSlot::WRITE_ONLY), 1);
        mCompute.dispatchThreads(SIDE, SIDE);
        gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_UPDATE);
        mCompute.unbindImage(TEST_IMAGE_A);
        mCompute.unbindImage(TEST_IMAGE_B);

        const std::vector<F32> a = readTexture<F32>(mTargets[0].getTexture(), SIDE, SIDE, 1, GL_RED, GL_FLOAT);
        const std::vector<F32> b = readTexture<F32>(mTargets[1].getTexture(), SIDE, SIDE, 1, GL_RED, GL_FLOAT);
        for (U32 i = 0; i < SIDE * SIDE; ++i)
        {
            ensure_equals("a holds image A at " + std::to_string(i), a[i], 1.f);
            ensure_equals("b holds image B at " + std::to_string(i), b[i], 2.f);
        }
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A usampler2D under a reserved name gets a texture unit, the one after a float sampler's, and reads an R32UI
    // texture's values exactly, past what eight bits could hold.
    template<> template<>
    void llglslshader_object_t::test<18>()
    {
        setUpCompute();
        constexpr U32 W = 13;
        constexpr U32 H = 7;
        ensure("sample builds",
               buildProgram(mCompute, "uint sample (test)", { { "test/uintSampleC.glsl", GL_COMPUTE_SHADER } }));
        const S32 float_unit = mCompute.getTextureChannel(LLShaderMgr::DIFFUSE_MAP);
        const S32 uint_unit = mCompute.getTextureChannel(TEST_UINT_MAP);
        ensure_equals("the float sampler first", float_unit, 0);
        ensure_equals("the unsigned one after it", uint_unit, 1);

        std::vector<U32> values(W * H);
        for (U32 i = 0; i < W * H; ++i)
        {
            values[i] = 0x10000u + i * 7u;
        }
        mUintImage = new LLImageGL(W, H, 1, /*usemipmaps=*/false);
        mUintImage->setExplicitFormat(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT);
        ensure("R32UI image created", mUintImage->createGLTexture(0, reinterpret_cast<const U8*>(values.data())));
        ensure("RGBA8 target allocates", mTargets[1].allocate(W, H, GL_RGBA8));
        ensure("out allocates", mBuffers[0].allocate(W * H * sizeof(U32), 0, nullptr, "uint sample (test)"));
        mBuffers[0].clear(0xFFFFFFFFu);

        mCompute.bind();
        mCompute.uniform1i(TEST_COUNT, 1);
        ensure_equals("float texture bound", mCompute.bindTexture(LLShaderMgr::DIFFUSE_MAP, &mTargets[1], ALSamplers::PointClamp),
                      float_unit);
        ensure("unsigned texture bound",
               gGL.getTextureSlot((U32)uint_unit)->bindSampled(mUintImage.get(), ALSamplers::PointClamp));
        mCompute.bindStorageBuffer(TEST_COUNTS, mBuffers[0]);
        mCompute.dispatchThreads(W, H);
        mCompute.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
        mCompute.unbindTexture(TEST_UINT_MAP);

        const std::vector<U32> read = readWords(mBuffers[0], W * H);
        for (U32 i = 0; i < W * H; ++i)
        {
            ensure_equals("texel " + std::to_string(i), read[i], values[i]);
        }
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // Integer targets read through bindTexture with no sampler named: an R32UI and an RG16I target, each written by a
    // program's integer output, come back texel for texel through a usampler2D and an isampler2D. A filtering sampler
    // leaves an integer texture incomplete, reading 0, so the default for an integer attachment is point, attachment 0
    // included.
    template<> template<>
    void llglslshader_object_t::test<19>()
    {
        setUpShaders();
        constexpr U32 W = 11;
        constexpr U32 H = 6;
        ensure("R32UI target allocates", mTargets[0].allocate(W, H, GL_R32UI));
        ensure("RG16I target allocates", mTargets[1].allocate(W, H, GL_RG16I));
        ensure("output target allocates", mTargets[2].allocate(W, H, GL_RGBA32UI));
        ensure("its signed attachment", mTargets[2].addColorAttachment(GL_RGBA32I));
        ensure("unsigned writer builds",
               buildProgram(mDraw, "uint write (test)",
                            { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/uintWriteF.glsl", GL_FRAGMENT_SHADER } }));
        ensure("signed writer builds",
               buildProgram(mDraw2, "int write (test)",
                            { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/intWriteF.glsl", GL_FRAGMENT_SHADER } }));
        ensure("reader builds",
               buildProgram(mDraw3, "integer sample (test)",
                            { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/intSampleF.glsl", GL_FRAGMENT_SHADER } }));
        ensure("the unsigned sampler has a unit", mDraw3.getTextureChannel(TEST_UINT_MAP) >= 0);
        ensure("the signed sampler has a unit", mDraw3.getTextureChannel(TEST_INT_MAP) >= 0);

        LLGLDisable blend(GL_BLEND);
        for (U32 source = 0; source < 2; ++source)
        {
            mTargets[source].bindTarget();
            (source ? mDraw2 : mDraw).bind();
            gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
            mTargets[source].flush();
        }

        mTargets[2].bindTarget();
        mDraw3.bind();
        ensure("unsigned source bound", mDraw3.bindTexture(TEST_UINT_MAP, &mTargets[0]) >= 0);
        ensure("signed source bound", mDraw3.bindTexture(TEST_INT_MAP, &mTargets[1]) >= 0);

        // Read off GL, since a driver may read an integer texture under a filtering sampler texel by texel anyway.
        for (const S32 uniform : { (S32)TEST_UINT_MAP, (S32)TEST_INT_MAP })
        {
            glActiveTexture(GL_TEXTURE0 + mDraw3.getTextureChannel(uniform));
            GLint sampler = 0;
            glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
            GLint min_filter = 0;
            GLint mag_filter = 0;
            glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_MIN_FILTER, &min_filter);
            glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_MAG_FILTER, &mag_filter);
            const std::string which = uniform == TEST_UINT_MAP ? "the unsigned source's" : "the signed source's";
            ensure_equals(which + " sampler magnifies by the nearest texel", (U32)mag_filter, (U32)GL_NEAREST);
            ensure_equals(which + " sampler minifies by the nearest texel", (U32)min_filter,
                          (U32)GL_NEAREST_MIPMAP_NEAREST);
        }
        glActiveTexture(GL_TEXTURE0 + gGL.getCurrentTexUnitIndex());
        // And the debug validator, which checks every sampler a draw reads, finds nothing to say -- where a filtering
        // sampler named over an integer texture is what it is there to catch.
        gDebugGL = true;
        const bool samplers_valid = validate_bound_samplers();
        mDraw3.bindTexture(TEST_UINT_MAP, &mTargets[0], ALSamplers::BilinearClamp);
        const bool filtered_valid = validate_bound_samplers();
        gDebugGL = false;
        ensure("the debug validator passes the binds", samplers_valid);
        ensure("and refuses a filtering sampler over an integer texture", !filtered_valid);
        mDraw3.bindTexture(TEST_UINT_MAP, &mTargets[0]);

        gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
        std::vector<U32> uints((size_t)W * H * 4, 0x55u);
        std::vector<S32> ints((size_t)W * H * 4, 0x55);
        glReadPixels(0, 0, W, H, GL_RGBA_INTEGER, GL_UNSIGNED_INT, uints.data());
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glReadPixels(0, 0, W, H, GL_RGBA_INTEGER, GL_INT, ints.data());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        mTargets[2].flush();
        mDraw3.unbindTexture(TEST_UINT_MAP);
        mDraw3.unbindTexture(TEST_INT_MAP);

        for (U32 y = 0; y < H; ++y)
        {
            for (U32 x = 0; x < W; ++x)
            {
                const std::string at = " at " + std::to_string(x) + "," + std::to_string(y);
                const size_t i = ((size_t)y * W + x) * 4;
                const U32 uint_expected[4] = { 0x01000001u + y * 1000u + x, 0u, 0u, 1u };
                const S32 int_expected[4] = { -1 - (S32)x, (S32)y * 300 - 1000, 0, 1 };
                for (U32 c = 0; c < 4; ++c)
                {
                    ensure_equals("unsigned component " + std::to_string(c) + at, uints[i + c], uint_expected[c]);
                    ensure_equals("signed component " + std::to_string(c) + at, ints[i + c], int_expected[c]);
                }
            }
        }
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A shared object is the shader manager's, not the programs' that attach it. Two programs built through
    // createShader attach the same one, as every program with a feature attaches its object, and call into it. Once
    // linked a program holds no object, and its own are deleted: the count of live objects is what it was before the
    // two built, and the shared one is alive and not even flagged. Unloading one leaves the other drawing; a third
    // built after both are gone attaches the shared object again and links. One that fails to link deletes its own
    // objects all the same. clearShaderObjects deletes every shared object at once, as no linked program holds one,
    // and the programs draw on without them.
    template<> template<>
    void llglslshader_object_t::test<20>()
    {
        setUpShaders();
        constexpr U32 SIDE = 4;
        ensure("output target allocates", mTargets[0].allocate(SIDE, SIDE, GL_RGBA32F));

        const auto& fragments = mShaders.mShaderObjects[LLShaderMgr::STAGE_FRAGMENT];
        const auto found = fragments.find("deferred/globalF.glsl");
        ensure("the shared object compiled", found != fragments.end());
        const GLuint shared = found->second;

        const std::initializer_list<std::pair<std::string, GLenum>> files =
        {
            { "test/fullscreenV.glsl", GL_VERTEX_SHADER },
            { "test/globalCallF.glsl", GL_FRAGMENT_SHADER },
        };
        const U32 live = LLShaderMgr::sLiveObjects;

        // What `program` draws, every pixel the colour the stub passes through.
        LLGLDisable blend(GL_BLEND);
        auto ensureDraws = [&](const std::string& what, LLGLSLShader& program)
        {
            mTargets[0].bindTarget();
            gGL.setClearColor(LLColor4::transparent);
            mTargets[0].clear();
            program.bind();
            gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
            const std::vector<F32> drawn = readBoundTarget(SIDE, SIDE, 4, GL_RGBA);
            mTargets[0].flush();
            LLGLSLShader::unbind();
            const F32 expected[4] = { 0.25f, 0.5f, 0.75f, 1.f };
            for (size_t i = 0; i < drawn.size(); ++i)
            {
                ensure_equals(what + ", component " + std::to_string(i), drawn[i], expected[i % 4]);
            }
        };

        ensure("A builds", buildProgram(mDraw, "shared A (test)", files));
        ensure("B builds", buildProgram(mDraw2, "shared B (test)", files));
        ensure("A holds no object once linked", attachedNames(mDraw.mProgramObject).empty());
        ensure("nor does B", attachedNames(mDraw2.mProgramObject).empty());
        ensure_equals("their own objects deleted", LLShaderMgr::sLiveObjects, live);
        ensure("a program's own stage is not shared", !fragments.contains("test/globalCallF.glsl"));
        ensure("the shared object stays", glIsShader(shared) == GL_TRUE);
        ensure("not even flagged for deletion", !flaggedForDeletion(shared));
        ensureDraws("A draws", mDraw);

        mDraw.unload();
        ensure("unloading A leaves the shared object", glIsShader(shared) == GL_TRUE && !flaggedForDeletion(shared));
        glValidateProgram(mDraw2.mProgramObject);
        GLint valid = GL_FALSE;
        glGetProgramiv(mDraw2.mProgramObject, GL_VALIDATE_STATUS, &valid);
        ensure("B validates", valid == GL_TRUE);
        ensureDraws("B draws", mDraw2);

        mDraw2.unload();
        ensure("C builds afterwards", buildProgram(mDraw3, "shared C (test)", files));
        ensure("C holds no object once linked", attachedNames(mDraw3.mProgramObject).empty());
        ensureDraws("C draws", mDraw3);

        // Without the shared object to call into, the stage cannot link, at any class.
        ensure("D does not link", !buildProgram(mDraw, "unlinked D (test)", files, 0, /*attach_nothing=*/true));
        ensure_equals("and its own objects are deleted all the same", LLShaderMgr::sLiveObjects, live);

        S32 level = 1;
        const GLuint loose = mShaders.loadShaderFile("test/solidF.glsl", level, GL_FRAGMENT_SHADER);
        ensure("an object no program holds", loose != 0 && glIsShader(loose) == GL_TRUE);
        ensure_equals("counted live", LLShaderMgr::sLiveObjects, live + 1);

        size_t held = 0;
        for (const auto& stage : mShaders.mShaderObjects)
        {
            held += stage.size();
        }
        mShaders.clearShaderObjects();
        for (const auto& stage : mShaders.mShaderObjects)
        {
            ensure("the manager holds no object", stage.empty());
        }
        ensure("one no program ever held goes at once", glIsShader(loose) == GL_FALSE);
        ensure("and one programs linked with", glIsShader(shared) == GL_FALSE);
        ensure_equals("none counted live", LLShaderMgr::sLiveObjects, live + 1 - (U32)held);
        ensureDraws("C still draws", mDraw3);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A buffer bound once at a reserved block's point serves every program after it: one program declaring the block
    // alone, then one declaring a block before it in the table as well, each find their buffers with nothing bound
    // between -- where a numbering by program would put the shared block at point 0 for the first, the second block's
    // place. And LLRender keeps the points: a bind of what a point holds issues nothing, which a bind made behind its
    // back shows, and refreshState puts back what it holds.
    template<> template<>
    void llglslshader_object_t::test<21>()
    {
        setUpCompute();
        ensure("first-only builds",
               buildProgram(mCompute, "first only (test)", { { "test/firstOnlyC.glsl", GL_COMPUTE_SHADER } }));
        ensure("first-and-counts builds",
               buildProgram(mCompute2, "first and counts (test)", { { "test/firstAndCountsC.glsl", GL_COMPUTE_SHADER } }));
        const S32 first = mShaders.getStorageBlockBinding(TEST_BLOCK_FIRST);
        const S32 counts = mShaders.getStorageBlockBinding(TEST_COUNTS);
        ensure_equals("the first program's block at the table's point", mCompute.getStorageBinding(TEST_BLOCK_FIRST), first);
        ensure_equals("the second's too", mCompute2.getStorageBinding(TEST_BLOCK_FIRST), first);
        ensure("the two blocks apart", first != counts);

        const std::vector<U32> zeros(4, 0u);
        ensure("first allocates", mBuffers[0].allocate(4 * sizeof(U32), 0, zeros.data(), "first (test)"));
        ensure("counts allocate", mBuffers[1].allocate(4 * sizeof(U32), 0, zeros.data(), "counts (test)"));

        mCompute.bind();
        gGL.bindStorageBuffer((U32)first, mBuffers[0]);
        gGL.bindStorageBuffer((U32)counts, mBuffers[1]);
        mCompute.dispatch(1);
        mCompute2.bind();
        mCompute2.dispatch(1);

        const std::vector<U32> held_first = readWords(mBuffers[0], 2);
        ensure_equals("the first program wrote the first block", held_first[0], 7u);
        ensure_equals("the second program wrote it too", held_first[1], 9u);
        ensure_equals("and its counts", readWords(mBuffers[1], 1)[0], 5u);

        // Behind LLRender's back, then the bind it holds already: nothing issued, so GL keeps the other buffer.
        auto bound_at = [](S32 point)
        {
            GLint name = 0;
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, (GLuint)point, &name);
            return (U32)name;
        };
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, (GLuint)first, mBuffers[1].getGLName());
        gGL.bindStorageBuffer((U32)first, mBuffers[0]);
        ensure_equals("a bind of what the point holds issues nothing", bound_at(first), mBuffers[1].getGLName());
        gGL.refreshState();
        ensure_equals("refreshState puts back what it holds", bound_at(first), mBuffers[0].getGLName());

        // A range, at a multiple of the context's alignment at least 16 bytes in, is a different bind from the whole
        // buffer.
        const size_t alignment = (size_t)gGLManager.mShaderStorageBufferOffsetAlignment;
        const size_t offset = (16 + alignment - 1) / alignment * alignment;
        ALGPUBuffer wide;
        ensure("wide allocates", wide.allocate(offset + 4 * sizeof(U32), 0, nullptr, "wide (test)"));
        wide.clear();
        mCompute.bind();
        gGL.bindStorageBuffer((U32)first, wide, offset, 4 * sizeof(U32));
        mCompute.dispatch(1);
        std::vector<U32> words((offset + 4 * sizeof(U32)) / sizeof(U32), 0xFFFFFFFFu);
        wide.read(0, words.data(), words.size() * sizeof(U32));
        ensure_equals("the range's first word written", words[offset / sizeof(U32)], 7u);
        ensure_equals("the word before it not", words[offset / sizeof(U32) - 1], 0u);

        // Releasing a bound buffer forgets it.
        wide.release();
        ensure_equals("a released buffer leaves its point empty", gGL.getStorageBuffer((U32)first), 0u);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // An array of blocks takes one point per element from the table's start for it, and bindStorageBuffer names the
    // element: each of two buffers takes its element's writes. A program reading only the second element finds it
    // at its point all the same, though GL may report no first element for it.
    template<> template<>
    void llglslshader_object_t::test<22>()
    {
        setUpCompute();
        ensure("array program builds",
               buildProgram(mCompute, "block array (test)", { { "test/blockArrayC.glsl", GL_COMPUTE_SHADER } }));
        ensure("second-element program builds",
               buildProgram(mCompute2, "block array one (test)", { { "test/blockArrayOneC.glsl", GL_COMPUTE_SHADER } }));
        const S32 base = mShaders.getStorageBlockBinding(TEST_BLOCK_ARRAY);
        ensure("the table gives the array a point", base >= 0);
        ensure_equals("element 0 at it", mCompute.getStorageBinding(TEST_BLOCK_ARRAY, 0), base);
        ensure_equals("element 1 at the next", mCompute.getStorageBinding(TEST_BLOCK_ARRAY, 1), base + 1);
        ensure_equals("no element 2", mCompute.getStorageBinding(TEST_BLOCK_ARRAY, 2), -1);
        ensure_equals("the second-element program's element 1 there too", mCompute2.getStorageBinding(TEST_BLOCK_ARRAY, 1),
                      base + 1);

        const std::vector<U32> zeros(4, 0u);
        ensure("a allocates", mBuffers[0].allocate(4 * sizeof(U32), 0, zeros.data(), "element 0 (test)"));
        ensure("b allocates", mBuffers[1].allocate(4 * sizeof(U32), 0, zeros.data(), "element 1 (test)"));

        mCompute.bind();
        mCompute.bindStorageBuffer(TEST_BLOCK_ARRAY, mBuffers[0], 0);
        mCompute.bindStorageBuffer(TEST_BLOCK_ARRAY, mBuffers[1], 1);
        mCompute.dispatch(1);
        ensure_equals("element 0's buffer", readWords(mBuffers[0], 1)[0], 11u);
        ensure_equals("element 1's buffer", readWords(mBuffers[1], 1)[0], 22u);

        mCompute2.bind();
        mCompute2.dispatch(1);
        ensure_equals("the second element's writer reaches element 1's buffer", readWords(mBuffers[1], 1)[0], 33u);
        ensure_equals("and not element 0's", readWords(mBuffers[0], 1)[0], 11u);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // An image array takes its units in reserved order like any image, though GL names it by its first element: it is
    // before testImageA in the table, so its two elements take units 0 and 1 and testImageA 2. bindImage names the
    // element, and each target takes its element's stores.
    template<> template<>
    void llglslshader_object_t::test<23>()
    {
        setUpCompute();
        constexpr U32 SIDE = 8;
        ensure("image array builds",
               buildProgram(mCompute, "image array (test)", { { "test/imageArrayC.glsl", GL_COMPUTE_SHADER } }));
        ensure_equals("element 0 first", mCompute.getImageUnit(TEST_IMAGES, 0), 0);
        ensure_equals("element 1 next", mCompute.getImageUnit(TEST_IMAGES, 1), 1);
        ensure_equals("the single image after the array", mCompute.getImageUnit(TEST_IMAGE_A), 2);

        for (U32 i = 0; i < 3; ++i)
        {
            ensure("target allocates", mTargets[i].allocate(SIDE, SIDE, GL_R32F));
        }
        mCompute.bind();
        ensure_equals("A bound", mCompute.bindImage(TEST_IMAGE_A, &mTargets[0], ALImageSlot::WRITE_ONLY), 2);
        ensure_equals("element 0 bound", mCompute.bindImage(TEST_IMAGES, &mTargets[1], ALImageSlot::WRITE_ONLY, 0, 0, 0), 0);
        ensure_equals("element 1 bound", mCompute.bindImage(TEST_IMAGES, &mTargets[2], ALImageSlot::WRITE_ONLY, 0, 0, 1), 1);
        mCompute.dispatchThreads(SIDE, SIDE);
        gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_UPDATE);
        mCompute.unbindImage(TEST_IMAGE_A);
        mCompute.unbindImage(TEST_IMAGES);

        const F32 expected[3] = { 1.f, 2.f, 3.f };
        for (U32 t = 0; t < 3; ++t)
        {
            const std::vector<F32> held = readTexture<F32>(mTargets[t].getTexture(), SIDE, SIDE, 1, GL_RED, GL_FLOAT);
            for (U32 i = 0; i < SIDE * SIDE; ++i)
            {
                ensure_equals("target " + std::to_string(t) + " at " + std::to_string(i), held[i], expected[t]);
            }
        }
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // The debug validator reads image units as it reads texture units: a signed integer image over a float format is
    // undefined and refused, over a signed integer one passed, and an empty unit is fine.
    template<> template<>
    void llglslshader_object_t::test<24>()
    {
        setUpCompute();
        constexpr U32 SIDE = 4;
        ensure("signed image builds",
               buildProgram(mCompute, "int image (test)", { { "test/imageIntC.glsl", GL_COMPUTE_SHADER } }));
        ensure("float target allocates", mTargets[0].allocate(SIDE, SIDE, GL_R32F));
        ensure("signed target allocates", mTargets[1].allocate(SIDE, SIDE, GL_R32I));

        mCompute.bind();
        gDebugGL = true;
        const bool empty_valid = validate_bound_samplers();
        mCompute.bindImage(TEST_IMAGE_INT, &mTargets[0], ALImageSlot::WRITE_ONLY);
        const bool float_valid = validate_bound_samplers();
        mCompute.bindImage(TEST_IMAGE_INT, &mTargets[1], ALImageSlot::WRITE_ONLY);
        const bool signed_valid = validate_bound_samplers();
        gDebugGL = false;
        mCompute.unbindImage(TEST_IMAGE_INT);

        ensure("an empty image unit passes", empty_valid);
        ensure("a signed image over a float format is refused", !float_valid);
        ensure("over a signed format it passes", signed_valid);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A depth-stencil texture reads as its depth until GL 4.3's stencil texturing turns it to its stencil: the debug
    // validator refuses an unsigned sampler over it as depth and passes it as stencil, and the reverse for a float
    // sampler.
    template<> template<>
    void llglslshader_object_t::test<25>()
    {
        setUpShaders();
        if (gGLManager.mGLVersion < 4.29f)
        {
            skip("the context has no stencil texturing");
        }
        ensure("unsigned reader builds",
               buildProgram(mDraw, "stencil read (test)",
                            { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/intSampleF.glsl", GL_FRAGMENT_SHADER } }));
        ensure("float reader builds",
               buildProgram(mDraw2, "depth read (test)",
                            { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/sampleF.glsl", GL_FRAGMENT_SHADER } }));

        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH24_STENCIL8, 4, 4);
        glBindTexture(GL_TEXTURE_2D, 0);

        // `program`'s sampler `uniform` over the texture, its mode `mode`, through a point sampler: what the validator
        // says, and nothing left bound.
        auto validates = [&](LLGLSLShader& program, S32 uniform, GLenum mode)
        {
            program.bind();
            const S32 unit = program.getTextureChannel(uniform);
            ALTextureSlot* slot = gGL.getTextureSlot((U32)unit);
            slot->bindManual(ALTextureSlot::TT_TEXTURE, texture, gGL.getSampler(ALSamplers::PointClamp));
            glActiveTexture(GL_TEXTURE0 + unit);
            glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, (GLint)mode);
            glActiveTexture(GL_TEXTURE0 + gGL.getCurrentTexUnitIndex());
            gDebugGL = true;
            const bool valid = validate_bound_samplers();
            gDebugGL = false;
            slot->unbind();
            slot->bindSampler(0);
            return valid;
        };

        ensure("an unsigned sampler over the depth refused",
               !validates(mDraw, TEST_UINT_MAP, GL_DEPTH_COMPONENT));
        ensure("over the stencil passed", validates(mDraw, TEST_UINT_MAP, GL_STENCIL_INDEX));
        ensure("a float sampler over the stencil refused", !validates(mDraw2, TEST_SOURCE_MAP, GL_STENCIL_INDEX));
        ensure("over the depth passed", validates(mDraw2, TEST_SOURCE_MAP, GL_DEPTH_COMPONENT));

        glDeleteTextures(1, &texture);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // Every kind of sampler takes a texture unit, in reserved order, an array included under its bare name though GL
    // names it by its first element: the two-element array first, then the array texture, the unsigned sampler and
    // the integer buffer.
    template<> template<>
    void llglslshader_object_t::test<26>()
    {
        setUpShaders();
        ensure("sampler types build",
               buildProgram(mDraw, "sampler types (test)",
                            { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/samplerTypesF.glsl", GL_FRAGMENT_SHADER } }));
        ensure_equals("the array first", mDraw.getTextureChannel(TEST_SAMPLERS), 0);
        ensure_equals("the array texture after its two units", mDraw.getTextureChannel(TEST_SOURCE_MAP), 2);
        ensure_equals("the unsigned sampler next", mDraw.getTextureChannel(TEST_UINT_MAP), 3);
        ensure_equals("the integer buffer last", mDraw.getTextureChannel(TEST_INT_MAP), 4);

        GLint units[2] = { -1, -1 };
        glGetUniformiv(mDraw.mProgramObject, glGetUniformLocation(mDraw.mProgramObject, "testSamplers[1]"), &units[1]);
        glGetUniformiv(mDraw.mProgramObject, glGetUniformLocation(mDraw.mProgramObject, "testSamplers[0]"), &units[0]);
        ensure_equals("the array's first element at its unit", units[0], 0);
        ensure_equals("its second at the next", units[1], 1);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }
}
