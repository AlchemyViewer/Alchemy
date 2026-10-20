/**
 * @file llglslshader_gl_test.cpp
 * @brief LLGLSLShader on the hidden window. The latched layout flags its binary cache key folds in. Its uniform value
 *        cache: what a program holds, read back from GL, after
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
#include "../llrender.h"
#include "../llrendertarget.h"
#include "../llshadermgr.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

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
            TEST_BOOLS,
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
            "testBools",
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

            // A fragment stage that does not compile.
            { "test/brokenF.glsl",
              "out vec4 frag_color;\n"
              "void main()\n"
              "{\n"
              "    frag_color = not_declared_anywhere;\n"
              "}\n" },
            // A fragment stage that compiles for a program and not for its skinned variant.
            { "test/skinBrokenF.glsl",
              "out vec4 frag_color;\n"
              "void main()\n"
              "{\n"
              "#ifdef HAS_SKIN\n"
              "    frag_color = not_declared_anywhere;\n"
              "#else\n"
              "    frag_color = vec4(1.0);\n"
              "#endif\n"
              "}\n" },

            // A bool array under a reserved name, both elements read so the whole array stays active.
            { "test/boolsF.glsl",
              "uniform bool testBools[2];\n"
              "out vec4 frag_color;\n"
              "void main()\n"
              "{\n"
              "    frag_color = vec4(testBools[0] ? 1.0 : 0.0, testBools[1] ? 1.0 : 0.0, 0.0, 1.0);\n"
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
            UNSCOPED_INFO("the test's names follow the product's");
            REQUIRE(mShaders.mReservedUniforms.size() == size_t(LLShaderMgr::END_RESERVED_UNIFORMS));
            for (const char* name : kTestReservedNames)
            {
                mShaders.mReservedUniforms.push_back(name);
            }
            UNSCOPED_INFO("the test's blocks follow the product's");
            REQUIRE(mShaders.mReservedStorageBlocks.size() == size_t(LLShaderMgr::END_RESERVED_STORAGE_BLOCKS));
            for (const auto& block : kTestStorageBlocks)
            {
                mShaders.mReservedStorageBlocks.push_back({ block.first, block.second });
            }
            for (const auto& object : kStubObjects)
            {
                S32 level = 1;
                CAPTURE(object.first);
                REQUIRE(mShaders.loadShaderFile(object.first, level, object.second) != 0);
            }
        }

        // setUpShaders, and compute in use, or the case skipped where the context has none.
        void setUpCompute()
        {
            if (!ll_test::contextHasCompute())
            {
                SKIP("the context has no compute");
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
            {
                INFO("shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mShader.mProgramObject = glCreateProgram();
            mShader.mName = "uniform cache (test)";
            mShader.attachObject(vs);
            mShader.attachObject(fs);
            const bool linked = mShader.mapAttributes();
            glDeleteShader(vs);
            glDeleteShader(fs);
            REQUIRE(linked);
            REQUIRE(mShader.mapUniforms());
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

        // The location of an active uniform. At -1 the read back raises an error and fills nothing, so every
        // expectation of 0 would be met.
        GLint require_location(const char* name) const
        {
            const GLint loc = location(name);
            CAPTURE(name);
            REQUIRE(loc >= 0);
            return loc;
        }

        // What the program holds at a uniform, as GL reads it back, against what a check expects of its first
        // components, at most four. Floats are compared by their bits, so a check is exact.
        void require_ints(const std::string& what, const char* name, std::initializer_list<GLint> expected) const
        {
            INFO(what);
            const GLint loc = require_location(name);
            std::array<GLint, 4> held = {};
            REQUIRE(expected.size() <= held.size());
            glGetUniformiv(mShader.mProgramObject, loc, held.data());
            size_t component = 0;
            for (const GLint value : expected)
            {
                CAPTURE(component);
                REQUIRE(held[component] == value);
                ++component;
            }
        }

        void require_uints(const std::string& what, const char* name, std::initializer_list<GLuint> expected) const
        {
            INFO(what);
            const GLint loc = require_location(name);
            std::array<GLuint, 4> held = {};
            REQUIRE(expected.size() <= held.size());
            glGetUniformuiv(mShader.mProgramObject, loc, held.data());
            size_t component = 0;
            for (const GLuint value : expected)
            {
                CAPTURE(component);
                REQUIRE(held[component] == value);
                ++component;
            }
        }

        void require_floats(const std::string& what, const char* name, std::initializer_list<F32> expected) const
        {
            INFO(what);
            const GLint loc = require_location(name);
            std::array<F32, 4> held = {};
            REQUIRE(expected.size() <= held.size());
            glGetUniformfv(mShader.mProgramObject, loc, held.data());
            size_t component = 0;
            for (const F32 value : expected)
            {
                CAPTURE(component);
                REQUIRE(std::bit_cast<U32>(held[component]) == std::bit_cast<U32>(value));
                ++component;
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


    // The binary cache is keyed by hash(). REVERSE_Z and GBUFFER_NORM_HDR are injected at compile time from latched
    // statics rather than carried in a defines map, so each has to reach the key on its own: a program compiled
    // for RGBA16 normals and handed back for RGB10_A2 ones unpacks the geometric normal as noise.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: the cache key follows the normal layout and the depth convention", "[llglslshader_gl]")
    {
        const bool norm_hdr = LLRender::sGBufferNormHDR;
        const bool reverse_z = LLRender::sReverseZ;
        mShader.mName = "Cache Key Probe";

        LLRender::sGBufferNormHDR = true;
        LLRender::sReverseZ = false;
        const LLUUID base = mShader.hash();
        const LLUUID again = mShader.hash();
        LLRender::sGBufferNormHDR = false;
        const LLUUID packed_normals = mShader.hash();
        LLRender::sGBufferNormHDR = true;
        LLRender::sReverseZ = true;
        const LLUUID reversed = mShader.hash();

        LLRender::sGBufferNormHDR = norm_hdr;
        LLRender::sReverseZ = reverse_z;

        CHECK(again == base);
        CHECK(packed_normals != base);
        CHECK(reversed != base);
        CHECK(packed_normals != reversed);
    }

    // A load configures every program from nothing and asks createShader for it again (LLViewerShaderMgr::setShaders). One
    // configured as it linked is kept: its name, its uniforms and its hook's work stand, and nothing is compiled. One
    // configured otherwise links again. GL may hand a deleted program's name straight back out, so the link count, not
    // the name, says which.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: createShader keeps a program configured as it linked and links one configured otherwise", "[llglslshader_gl]")
    {
        setUpShaders();

        U32 hooks = 0;
        const auto configure = [&](LLGLSLShader& shader)
        {
            shader.clearConfiguration();
            shader.mName = "kept (test)";
            shader.mShaderLevel = 1;
            shader.mShaderFiles.push_back({ "test/fullscreenV.glsl", GL_VERTEX_SHADER });
            shader.mShaderFiles.push_back({ "test/boolsF.glsl", GL_FRAGMENT_SHADER });
            shader.mOnLink = [&hooks](LLGLSLShader&) { ++hooks; };
        };
        const auto held = [this]()
        {
            GLint value = -1;
            glGetUniformiv(mDraw.mProgramObject, glGetUniformLocation(mDraw.mProgramObject, "testBools[1]"), &value);
            return value;
        };

        mDraw.unload();
        configure(mDraw);
        REQUIRE(mDraw.createShader());
        const GLuint name  = mDraw.mProgramObject;
        const U32    links = mDraw.linkCount();
        CHECK(hooks == 1);
        mDraw.bind();
        const GLint ints[2] = { 0, 1 };
        mDraw.uniform1iv(TEST_BOOLS, 2, ints);
        mDraw.unbind();
        REQUIRE(held() == 1);

        SECTION("configured as it linked")
        {
            configure(mDraw);
            CHECK(mDraw.createShader());
            CHECK(mDraw.mProgramObject == name);
            CHECK(mDraw.linkCount() == links);
            CHECK(hooks == 1);
            CHECK(held() == 1);
        }

        SECTION("with a define of its own")
        {
            configure(mDraw);
            mDraw.addPermutation("TEST_DEFINE", "1");
            CHECK(mDraw.createShader());
            CHECK(mDraw.linkCount() == links + 1);
            CHECK(hooks == 2);
            CHECK(held() == 0);
        }

        SECTION("with a feature")
        {
            // One that attaches nothing either way, so the program links with the objects the test has.
            configure(mDraw);
            mDraw.mFeatures.attachNothing = true;
            CHECK(mDraw.createShader());
            CHECK(mDraw.linkCount() == links + 1);
            CHECK(hooks == 2);
        }

        SECTION("against shared objects compiled from other inputs")
        {
            const LLUUID key = LLGLSLShader::sSharedObjectsKey;
            LLGLSLShader::sSharedObjectsKey.generate();
            configure(mDraw);
            CHECK(mDraw.createShader());
            CHECK(mDraw.linkCount() == links + 1);
            LLGLSLShader::sSharedObjectsKey = key;
        }

        SECTION("unloaded")
        {
            mDraw.unload();
            configure(mDraw);
            CHECK(mDraw.createShader());
            CHECK(mDraw.linkCount() == links + 1);
            CHECK(hooks == 2);
        }

        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A program takes the uniforms every program shares (LLShaderMgr::updateShaderUniforms) on its first bind after it
    // links, with no environment change since: mapUniforms binds it before any location is known, and a set applied
    // there would reach none of them.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a program's first bind after it links applies the uniforms every program shares", "[llglslshader_gl]")
    {
        setUpShaders();

        mShaders.mUpdateUniforms = [](LLGLSLShader* shader)
        {
            const GLint ints[2] = { 0, 1 };
            shader->uniform1iv(TEST_BOOLS, 2, ints);
        };
        const auto held = [this]()
        {
            GLint value = -1;
            glGetUniformiv(mDraw.mProgramObject, glGetUniformLocation(mDraw.mProgramObject, "testBools[1]"), &value);
            return value;
        };
        const auto configure = [this](bool define)
        {
            mDraw.clearConfiguration();
            mDraw.mName = "shared uniforms (test)";
            mDraw.mShaderLevel = 1;
            mDraw.mShaderFiles.push_back({ "test/fullscreenV.glsl", GL_VERTEX_SHADER });
            mDraw.mShaderFiles.push_back({ "test/boolsF.glsl", GL_FRAGMENT_SHADER });
            if (define)
            {
                mDraw.addPermutation("TEST_DEFINE", "1");
            }
        };
        const U32 generation = LLGLSLShader::sEnvironmentGeneration;

        mDraw.unload();
        configure(false);
        REQUIRE(mDraw.createShader());
        mDraw.bind();
        mDraw.unbind();
        CHECK(held() == 1);

        SECTION("linked again in place")
        {
            const U32 links = mDraw.linkCount();
            configure(true);
            REQUIRE(mDraw.createShader());
            REQUIRE(mDraw.linkCount() == links + 1);
            UNSCOPED_INFO("a link resets what the program held");
            CHECK(held() == 0);
            mDraw.bind();
            mDraw.unbind();
            CHECK(held() == 1);
        }

        SECTION("kept")
        {
            configure(false);
            REQUIRE(mDraw.createShader());
            CHECK(held() == 1);
        }

        CHECK(LLGLSLShader::sEnvironmentGeneration == generation);
        mShaders.mUpdateUniforms = nullptr;
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A program that did not build takes nothing down: bind() puts a stand-in in its place that draws nothing, skinned
    // or not, and its uniform, texture and dispatch calls do nothing. A solid program drawn the same way first shows the
    // draw would have covered the target.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a program that did not build draws nothing and its calls do nothing", "[llglslshader_gl]")
    {
        setUpShaders();
        constexpr U32 SIDE = 4;
        REQUIRE(mTargets[0].allocate(SIDE, SIDE, GL_RGBA8));
        REQUIRE(buildProgram(mDraw2, "solid (test)",
                             { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/solidF.glsl", GL_FRAGMENT_SHADER } }));

        CHECK_FALSE(buildProgram(mDraw, "broken (test)",
                                 { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/brokenF.glsl", GL_FRAGMENT_SHADER } }));
        REQUIRE_FALSE(mDraw.isComplete());
        // The failed compile's own error, which is not what is under test.
        while (glGetError() != GL_NO_ERROR)
        {
        }

        LLGLDisable blend(GL_BLEND);
        const auto draw = [&](LLGLSLShader& shader, bool rigged)
        {
            mTargets[0].bindTarget();
            gGL.setClearColor(LLColor4::transparent);
            mTargets[0].clear();
            shader.bind(rigged);
            gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
            const std::vector<F32> drawn = readBoundTarget(SIDE, SIDE, 4, GL_RGBA);
            mTargets[0].flush();
            LLGLSLShader::unbind();
            return drawn;
        };

        UNSCOPED_INFO("the solid program covers the target");
        CHECK(draw(mDraw2, false) == std::vector<F32>(SIDE * SIDE * 4, 1.f));

        mDraw.bind();
        CHECK(LLGLSLShader::sCurBoundShaderPtr == &mDraw);
        GLint current = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &current);
        UNSCOPED_INFO("a linked program stands in");
        CHECK(current != 0);
        GLint linked = GL_FALSE;
        glGetProgramiv((GLuint)llmax(current, 0), GL_LINK_STATUS, &linked);
        CHECK(linked == GL_TRUE);
        mDraw.uniform1f(LLShaderMgr::MINIMUM_ALPHA, 0.5f);
        CHECK(mDraw.enableTexture(LLShaderMgr::DIFFUSE_MAP) == -1);
        CHECK(mDraw.bindTexture(LLShaderMgr::DIFFUSE_MAP, &mTargets[0]) == -1);
        CHECK(mDraw.disableTexture(LLShaderMgr::DIFFUSE_MAP) == -1);
        CHECK(mDraw.getTextureChannel(LLShaderMgr::DIFFUSE_MAP) == -1);
        mDraw.dispatch(1);
        LLGLSLShader::unbind();

        UNSCOPED_INFO("it draws nothing");
        CHECK(draw(mDraw, false) == std::vector<F32>(SIDE * SIDE * 4, 0.f));
        UNSCOPED_INFO("nor skinned, having no skinned variant");
        CHECK(draw(mDraw, true) == std::vector<F32>(SIDE * SIDE * 4, 0.f));

        LLGLSLShader::releaseStandIn();
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A program whose skinned variant does not build fails whole. The passes bind the variant for skinned draws and set
    // its uniforms through the program, so a base left built would set them on the stand-in bound in the variant's
    // place. A program with no variant is its own riggedVariant(), and binding that binds the stand-in, its calls doing
    // nothing. Asked for without the variant, the same sources build.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a program whose skinned variant does not build fails whole", "[llglslshader_gl]")
    {
        setUpShaders();
        const auto configure = [&]()
        {
            mDraw.unload();
            mDraw.mName        = "skin broken (test)";
            mDraw.mShaderLevel = 1;
            mDraw.mShaderFiles = { { "test/fullscreenV.glsl", GL_VERTEX_SHADER },
                                   { "test/skinBrokenF.glsl", GL_FRAGMENT_SHADER } };
        };

        configure();
        CHECK_FALSE(mDraw.createShader(LLGLSLShader::VARIANT_RIGGED));
        // The failed compile's own error, which is not what is under test.
        while (glGetError() != GL_NO_ERROR)
        {
        }
        CHECK_FALSE(mDraw.isComplete());
        CHECK(mDraw.mRiggedVariant == nullptr);
        CHECK(mDraw.riggedVariant() == &mDraw);

        mDraw.riggedVariant()->bind();
        CHECK(LLGLSLShader::sCurBoundShaderPtr == &mDraw);
        mDraw.uniform1f(LLShaderMgr::MINIMUM_ALPHA, 0.5f);
        CHECK(mDraw.enableTexture(LLShaderMgr::DIFFUSE_MAP) == -1);
        LLGLSLShader::unbind();
        LLGLSLShader::releaseStandIn();
        CHECK(glGetError() == GLenum(GL_NO_ERROR));

        configure();
        CHECK(mDraw.createShader());
        CHECK(mDraw.isComplete());
        CHECK(mDraw.riggedVariant() == &mDraw);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A copy of a shared object compiled under a key of its own that does not build is attached by that key all the
    // same, so a program wanting it fails rather than linking the base object in its place: only a copy never compiled
    // falls back to the base, as a source that does not vary on the axis needs no second object. A copy that builds
    // under the key later, or the objects cleared, forgets the failure.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a shared object's copy that does not build is not replaced by the base", "[llglslshader_gl]")
    {
        setUpShaders();
        const std::string base = "test/brokenF.glsl";
        const std::string copy = base + LLShaderMgr::CLASSIC_OBJECT_SUFFIX;
        LLGLSLShader classic;
        classic.addPermutation("CLASSIC_MODE", "1");
        const auto key = [&]()
        { return mShaders.variantObjectKey(base, LLShaderMgr::OBJ_AXIS_CLASSIC, &classic, GL_FRAGMENT_SHADER); };

        UNSCOPED_INFO("no copy compiled: the base object");
        CHECK(key() == base);

        S32 level = 1;
        std::map<std::string, std::string> defines{ { "CLASSIC_MODE", "1" } };
        CHECK(mShaders.loadShaderFile(base, level, GL_FRAGMENT_SHADER, &defines, -1, copy) == 0);
        while (glGetError() != GL_NO_ERROR)
        {
        }
        UNSCOPED_INFO("a copy that did not build: its own key, which attaches nothing");
        CHECK(key() == copy);
        CHECK_FALSE(mShaders.mShaderObjects[LLShaderMgr::STAGE_FRAGMENT].contains(copy));

        level = 1;
        CHECK(mShaders.loadShaderFile("test/solidF.glsl", level, GL_FRAGMENT_SHADER, &defines, -1, copy) != 0);
        UNSCOPED_INFO("a copy built under the key since");
        CHECK(key() == copy);
        CHECK(mShaders.mShaderObjects[LLShaderMgr::STAGE_FRAGMENT].contains(copy));

        mShaders.clearShaderObjects();
        level = 1;
        CHECK(mShaders.loadShaderFile(base, level, GL_FRAGMENT_SHADER, &defines, -1, copy) == 0);
        while (glGetError() != GL_NO_ERROR)
        {
        }
        mShaders.clearShaderObjects();
        UNSCOPED_INFO("cleared, the failure goes with the objects");
        CHECK(key() == base);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Ints and uints arrive exactly past 2^24: each set below differs from the one before only in values that
    // convert to the same float, and only in some of its components.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: ints and uints arrive exactly past 2^24", "[llglslshader_gl]")
    {
        setUp();

        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, BIG);
        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, BIG + 1);
        require_ints("int", "light_count", { BIG + 1 });

        const GLint first[4]  = { BIG,     -BIG,     BIG, BIG };
        const GLint second[4] = { BIG,     -BIG,     BIG, BIG + 1 };
        const GLint third[4]  = { BIG + 1, -BIG - 1, BIG, BIG + 1 };
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, first);
        require_ints("ivec4", "sun_up_factor", { BIG, -BIG, BIG, BIG });
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, second);
        require_ints("ivec4, w alone changed", "sun_up_factor", { BIG, -BIG, BIG, BIG + 1 });
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, third);
        require_ints("ivec4, x and y changed", "sun_up_factor", { BIG + 1, -BIG - 1, BIG, BIG + 1 });

        mShader.uniform1ui(LLShaderMgr::FRAME_ID, (GLuint)BIG);
        mShader.uniform1ui(LLShaderMgr::FRAME_ID, (GLuint)BIG + 1);
        require_uints("uint", "uFrameId", { (GLuint)BIG + 1 });

        // The sharpening constants are floats' bits. 1.0's and the next float up's are both 1065353216 as floats.
        const GLuint one[4]  = { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u };
        const GLuint next[4] = { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800001u };
        mShader.uniform4uiv(LLShaderMgr::CAS_PARAM_0, 1, one);
        mShader.uniform4uiv(LLShaderMgr::CAS_PARAM_0, 1, next);
        require_uints("uvec4, w alone changed", "cas_param_0", { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800001u });

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A, then B, then A again holds A, for every kind of set: each upload replaces what its location's slot holds,
    // so the third set is compared with B. The pairs mix a setter's value and array forms, which share a kind.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: setting A then B then A again holds A for every uniform type", "[llglslshader_gl]")
    {
        setUp();

        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 0.5f);
        require_floats("vec4 B", "color", { 0.25f, 0.5f, 0.75f, 0.5f });
        const F32 color[4] = { 0.25f, 0.5f, 0.75f, 1.f };
        mShader.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color);
        require_floats("vec4 A again", "color", { 0.25f, 0.5f, 0.75f, 1.f });

        mShader.uniform3f(LLShaderMgr::DEFERRED_SUN_DIR, 0.f, 0.5f, 1.f);
        const F32 sun[3] = { 0.f, 0.5f, 0.25f };
        mShader.uniform3fv(LLShaderMgr::DEFERRED_SUN_DIR, 1, sun);
        require_floats("vec3 B", "sun_dir", { 0.f, 0.5f, 0.25f });
        mShader.uniform3f(LLShaderMgr::DEFERRED_SUN_DIR, 0.f, 0.5f, 1.f);
        require_floats("vec3 A again", "sun_dir", { 0.f, 0.5f, 1.f });

        mShader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, 640.f, 480.f);
        const F32 res[2] = { 640.f, 360.f };
        mShader.uniform2fv(LLShaderMgr::DEFERRED_SCREEN_RES, 1, res);
        require_floats("vec2 B", "screen_res", { 640.f, 360.f });
        mShader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, 640.f, 480.f);
        require_floats("vec2 A again", "screen_res", { 640.f, 480.f });

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.25f);
        const F32 intensity = 0.5f;
        mShader.uniform1fv(LLShaderMgr::ENVIRONMENT_INTENSITY, 1, &intensity);
        require_floats("float B", "env_intensity", { 0.5f });
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.25f);
        require_floats("float A again", "env_intensity", { 0.25f });

        const GLint ivec_a[4] = { 1, 2, 3, 4 };
        const GLint ivec_b[4] = { 1, 2, 3, 5 };
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, ivec_a);
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, ivec_b);
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, ivec_a);
        require_ints("ivec4 A again", "sun_up_factor", { 1, 2, 3, 4 });

        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        const GLint count = 4;
        mShader.uniform1iv(LLShaderMgr::MULTI_LIGHT_COUNT, 1, &count);
        require_ints("int B", "light_count", { 4 });
        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        require_ints("int A again", "light_count", { 3 });

        mShader.uniform1ui(LLShaderMgr::FRAME_ID, 7u);
        mShader.uniform1ui(LLShaderMgr::FRAME_ID, 8u);
        mShader.uniform1ui(LLShaderMgr::FRAME_ID, 7u);
        require_uints("uint A again", "uFrameId", { 7u });

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A float set twice holds its value, and a new value replaces it. Raw uploads behind the cache's back show
    // which sets reached GL: just after a link the cache holds nothing, not a zero, so a set of 0 uploads; and a set
    // of the value last uploaded does not.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a float set skips GL only when it repeats the last value uploaded", "[llglslshader_gl]")
    {
        setUp();
        const GLint loc = location("env_intensity");
        UNSCOPED_INFO("env_intensity is active");
        CHECK(loc >= 0);

        glUniform1f(loc, 5.f);
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.f);
        require_floats("the first set uploads, a zero too", "env_intensity", { 0.f });

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        require_floats("set twice", "env_intensity", { 0.5f });

        glUniform1f(loc, 2.f);
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        require_floats("the value last uploaded is not uploaded again", "env_intensity", { 2.f });

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.75f);
        require_floats("a new value replaces it", "env_intensity", { 0.75f });

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // LLGLSLShader::link forgets the cache: the relink resets every uniform to 0, and the value set before it,
    // set again, arrives.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: LLGLSLShader::link resets uniforms to 0 and forgets the cache", "[llglslshader_gl]")
    {
        setUp();
        const GLint before = location("color");
        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        require_floats("set before the relink", "color", { 0.25f, 0.5f, 0.75f, 1.f });

        CHECK(mShader.link());
        if (location("color") != before)
        {
            SKIP("the relink moved the uniform, so a stale slot would not be consulted");
        }
        require_floats("the relink reset it", "color", { 0.f, 0.f, 0.f, 0.f });

        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        require_floats("set again after the relink", "color", { 0.25f, 0.5f, 0.75f, 1.f });

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // mapUniforms forgets the cache, and setMinimumAlpha's own: a new program, linked by GL alone and mapped, holds
    // the values set again after it, though they are the ones set on the program it replaced.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: mapUniforms forgets the uniform cache and setMinimumAlpha's own", "[llglslshader_gl]")
    {
        setUp();
        const GLint before = location("color");
        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        mShader.setMinimumAlpha(0.5f);
        require_floats("colour on the old program", "color", { 0.25f, 0.5f, 0.75f, 1.f });
        require_floats("minimum alpha on the old program", "minimum_alpha", { 0.5f });

        const GLuint fresh = linkRaw();
        UNSCOPED_INFO("the new program links");
        REQUIRE(fresh != 0);
        LLGLSLShader::unbind();
        const GLuint old = mShader.mProgramObject;
        mShader.mProgramObject = fresh;
        forget_program_samplers(old);
        glDeleteProgram(old);
        CHECK(mShader.mapUniforms());
        mShader.bind();
        if (location("color") != before)
        {
            SKIP("the new program put the uniform elsewhere, so a stale slot would not be consulted");
        }
        require_floats("the new program starts at 0", "color", { 0.f, 0.f, 0.f, 0.f });

        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        require_floats("colour set again", "color", { 0.25f, 0.5f, 0.75f, 1.f });
        mShader.setMinimumAlpha(0.5f);
        require_floats("minimum alpha set again", "minimum_alpha", { 0.5f });

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A bool takes the int setters and the float ones, and holds whichever set it last, the int set after a float
    // one included. INT_MIN and -0.0 share their bits: true as an int, false as a float, so only the kind a slot
    // records tells the float set from the int one before it.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a bool takes int and float setters and holds whichever set it last", "[llglslshader_gl]")
    {
        setUp();

        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 1);
        require_ints("true by int", "uGrainAnimate", { 1 });
        mShader.uniform1f(LLShaderMgr::GRAIN_ANIMATE, 0.f);
        require_ints("false by float", "uGrainAnimate", { 0 });
        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 1);
        require_ints("true by int again", "uGrainAnimate", { 1 });

        // Read at run time, so nothing folds the sign of the zero away.
        volatile U32 sign_bit = 0x80000000u;
        const F32 negative_zero = std::bit_cast<F32>(static_cast<U32>(sign_bit));
        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, INT_MIN);
        require_ints("true by INT_MIN", "uGrainAnimate", { 1 });
        mShader.uniform1f(LLShaderMgr::GRAIN_ANIMATE, negative_zero);
        require_ints("false by -0.0, the same bits", "uGrainAnimate", { 0 });

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // The other setters a bool takes -- the uint one, and the float one's fast and array forms -- each followed by
    // the int set that repeats the last one: AMD's driver drops that set unless every set of a bool reaches GL as an
    // int (see LLGLSLShader::mBoolLocations). Any float but zero is true, 0.5 included.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a bool holds an int set made after its uint and float setters' other forms", "[llglslshader_gl]")
    {
        setUp();

        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 1);
        mShader.uniform1ui(LLShaderMgr::GRAIN_ANIMATE, 0u);
        require_ints("false by uint", "uGrainAnimate", { 0 });
        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 1);
        require_ints("true by int after the uint", "uGrainAnimate", { 1 });

        mShader.fastUniform1f(LLShaderMgr::GRAIN_ANIMATE, 0.f);
        require_ints("false by the fast float", "uGrainAnimate", { 0 });
        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 1);
        require_ints("true by int after the fast float", "uGrainAnimate", { 1 });

        const F32 zero = 0.f;
        mShader.uniform1fv(LLShaderMgr::GRAIN_ANIMATE, 1, &zero);
        require_ints("false by the float array form", "uGrainAnimate", { 0 });
        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 1);
        require_ints("true by int after the float array form", "uGrainAnimate", { 1 });

        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 0);
        mShader.uniform1f(LLShaderMgr::GRAIN_ANIMATE, 0.5f);
        require_ints("true by 0.5", "uGrainAnimate", { 1 });
        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 0);
        require_ints("false by int after 0.5", "uGrainAnimate", { 0 });

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A bool array set whole by ints, then by floats, then by the ints again holds the ints: the driver that drops a
    // bool's repeated int set drops an array's too.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a bool array takes int and float array sets and holds whichever set it last", "[llglslshader_gl]")
    {
        setUpShaders();
        REQUIRE(buildProgram(mDraw, "bools (test)",
                             { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/boolsF.glsl", GL_FRAGMENT_SHADER } }));
        mDraw.bind();

        // What the two elements hold, as GL reads them back.
        const auto require_elements = [this](const char* what, GLint first, GLint second)
        {
            INFO(what);
            GLint held[2] = { -1, -1 };
            glGetUniformiv(mDraw.mProgramObject, glGetUniformLocation(mDraw.mProgramObject, "testBools[0]"), &held[0]);
            glGetUniformiv(mDraw.mProgramObject, glGetUniformLocation(mDraw.mProgramObject, "testBools[1]"), &held[1]);
            REQUIRE(held[0] == first);
            REQUIRE(held[1] == second);
        };

        const GLint ints[2] = { 1, 0 };
        const F32 floats[2] = { 0.f, 0.5f };
        mDraw.uniform1iv(TEST_BOOLS, 2, ints);
        require_elements("by ints", 1, 0);
        mDraw.uniform1fv(TEST_BOOLS, 2, floats);
        require_elements("by floats", 0, 1);
        mDraw.uniform1iv(TEST_BOOLS, 2, ints);
        require_elements("by the ints again", 1, 0);

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // The reserved index and the name of one uniform share its slot: A by index, B by name, then A by index holds A.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: the reserved index and the name of one uniform share its cache slot", "[llglslshader_gl]")
    {
        setUp();
        const LLStaticHashedString light_count("light_count");

        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        mShader.uniform1i(light_count, 4);
        require_ints("by name", "light_count", { 4 });
        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        require_ints("by index after the name", "light_count", { 3 });
        mShader.uniform1i(light_count, 4);
        require_ints("by name after the index", "light_count", { 4 });

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // The fast setters upload without comparing and leave the cache describing what they uploaded: a cached set of
    // the value before a fast one arrives.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: fast setters upload unconditionally and leave the cache matching GL", "[llglslshader_gl]")
    {
        setUp();

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        mShader.fastUniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.25f);
        require_floats("fast float", "env_intensity", { 0.25f });
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        require_floats("cached float after the fast one", "env_intensity", { 0.5f });

        const F32 a[4] = { 0.25f, 0.5f, 0.75f, 1.f };
        const F32 b[4] = { 1.f, 0.75f, 0.5f, 0.25f };
        mShader.uniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, a);
        mShader.fastUniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, b);
        require_floats("fast vec4", "specular_color", { 1.f, 0.75f, 0.5f, 0.25f });
        mShader.uniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, a);
        require_floats("cached vec4 after the fast one", "specular_color", { 0.25f, 0.5f, 0.75f, 1.f });

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // An array set uploads every time and forgets what its location held: element 0 set alone, then the array over
    // it, then element 0 alone again holds element 0's value; and an array whose element 0 is unchanged still
    // reaches GL.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: an array set uploads every time and forgets what its location held", "[llglslshader_gl]")
    {
        setUp();

        const F32 single[4]    = { 0.25f, 0.5f, 0.75f, 1.f };
        const F32 two[8]       = { 1.f, 1.f, 1.f, 1.f,   0.5f, 0.5f, 0.5f, 0.5f };
        const F32 two_again[8] = { 1.f, 1.f, 1.f, 1.f,   0.25f, 0.25f, 0.25f, 0.25f };

        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 1, single);
        require_floats("element 0 alone", "light_diffuse[0]", { 0.25f, 0.5f, 0.75f, 1.f });
        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 2, two);
        require_floats("element 0 by the array", "light_diffuse[0]", { 1.f, 1.f, 1.f, 1.f });
        require_floats("element 1 by the array", "light_diffuse[1]", { 0.5f, 0.5f, 0.5f, 0.5f });
        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 1, single);
        require_floats("element 0 alone again", "light_diffuse[0]", { 0.25f, 0.5f, 0.75f, 1.f });

        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 2, two);
        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 2, two_again);
        require_floats("element 1 by an array whose element 0 did not change", "light_diffuse[1]",
                       { 0.25f, 0.25f, 0.25f, 0.25f });

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // The reserved names the depth pyramid and the water's patches use sit where their enum entries say.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: the depth pyramid's and the water patches' reserved names sit at their entries", "[llglslshader_gl]")
    {
        const std::pair<S32, const char*> names[] =
        {
            { LLShaderMgr::DEPTH_PYRAMID,        "depthPyramid" },
            { LLShaderMgr::DEPTH_PYRAMID_SOURCE, "depthPyramidSource" },
            { LLShaderMgr::DEPTH_PYRAMID_TARGET, "depthPyramidTarget" },
            { LLShaderMgr::DEPTH_PYRAMID_LEVEL,  "depthPyramidLevel" },
            { LLShaderMgr::DEPTH_PYRAMID_SIZE,   "depthPyramidSize" },
            { LLShaderMgr::DEPTH_PYRAMID_COUNT,  "depthPyramidCount" },
            { LLShaderMgr::WATER_OCCLUSION,      "waterOcclusion" },
            { LLShaderMgr::WATER_PATCH_ORDER,    "waterPatchOrder" },
        };
        for (const auto& entry : names)
        {
            CAPTURE(entry.second);
            CHECK(mShaders.mReservedUniforms[entry.first] == std::string(entry.second));
        }
    }

    // Where the context has compute, its limits are at least what GL 4.3 promises, which is what the features size
    // themselves by.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: compute limits are at least what GL 4.3 promises", "[llglslshader_gl]")
    {
        if (!gGLManager.mHasCompute)
        {
            SKIP("the context has no compute");
        }
        for (U32 i = 0; i < 3; ++i)
        {
            CAPTURE(i);
            REQUIRE(gGLManager.mMaxComputeWorkGroupCount[i] >= 65535);
        }
        CHECK(gGLManager.mMaxComputeWorkGroupSize[0] >= 1024);
        CHECK(gGLManager.mMaxComputeWorkGroupSize[1] >= 1024);
        CHECK(gGLManager.mMaxComputeWorkGroupSize[2] >= 64);
        CHECK(gGLManager.mMaxComputeWorkGroupInvocations >= 1024);
        CHECK(gGLManager.mMaxComputeSharedMemorySize >= 32768);
        CHECK(gGLManager.mMaxImageUnits >= 8);
        CHECK(gGLManager.mMaxComputeShaderStorageBlocks >= 8);
        CHECK(gGLManager.mMaxShaderStorageBufferBindings >= 8);
        CHECK(gGLManager.mMaxShaderStorageBlockSize >= (S64(1) << 27));
        const S32 alignment = gGLManager.mShaderStorageBufferOffsetAlignment;
        {
            INFO("storage offset alignment a power of two");
            CHECK(alignment > 0);
            CHECK((alignment & (alignment - 1)) == 0);
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A compute program built through createShader links -- which GL refuses a compute object beside any other, so the
    // features' graphics objects stayed away -- from its compute object at #version 430, which it holds no longer
    // once linked; its work group size reads back; its block takes the table's point; and dispatchThreads over a count
    // that is not a multiple of the group size reaches each index exactly once.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: createShader builds a compute program that reaches each index once", "[llglslshader_gl]")
    {
        setUpCompute();
        CHECK(compiledVersion(mShaders, "test/countC.glsl", GL_COMPUTE_SHADER, 0) == std::string("#version 430"));
        REQUIRE(buildProgram(mCompute, "count (test)", { { "test/countC.glsl", GL_COMPUTE_SHADER } }));
        CHECK(mCompute.isCompute());
        UNSCOPED_INFO("it holds no object once linked");
        CHECK(attachedNames(mCompute.mProgramObject).empty());

        CHECK(mCompute.mComputeLocalSize[0] == 64u);
        CHECK(mCompute.mComputeLocalSize[1] == 1u);
        CHECK(mCompute.mComputeLocalSize[2] == 1u);
        UNSCOPED_INFO("the table gives its block a point");
        CHECK(mShaders.getStorageBlockBinding(TEST_COUNTS) >= 0);
        UNSCOPED_INFO("its block is at the table's point");
        CHECK(mCompute.getStorageBinding(TEST_COUNTS) == mShaders.getStorageBlockBinding(TEST_COUNTS));

        // 15.6 groups' worth, in a buffer with room for 17.
        constexpr U32 COUNT = 1000;
        constexpr U32 WORDS = 17 * 64;
        REQUIRE(mBuffers[0].allocate(WORDS * sizeof(U32), 0, nullptr, "counts (test)"));
        mBuffers[0].clear();

        mCompute.bind();
        mCompute.uniform1i(TEST_COUNT, (GLint)COUNT);
        mCompute.bindStorageBuffer(TEST_COUNTS, mBuffers[0]);
        mCompute.dispatchThreads(COUNT);

        const std::vector<U32> hits = readWords(mBuffers[0], WORDS);
        for (U32 i = 0; i < WORDS; ++i)
        {
            CAPTURE(i);
            REQUIRE(hits[i] == (i < COUNT ? 1u : 0u));
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // The #version a stage compiles at: the usual one for an ordinary program's own stage, never raised, and for the
    // shared objects likewise; 4.30 for a stage asking for it. A program whose stage reads a storage block builds only
    // when its features ask for 4.30, which createShader hands its own stages, and links with shared objects at the
    // usual version. On a context below 4.30 a compute program, or one asking for 4.30, does not build.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: stages compile at the usual GLSL version unless they ask for 4.30", "[llglslshader_gl]")
    {
        setUpShaders();
        const std::string usual = gGLManager.mGLSLVersionMinor >= 20 ? "#version 420" : "#version 400";

        CHECK(buildProgram(mDraw, "ladder (test)",
                           { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/solidF.glsl", GL_FRAGMENT_SHADER } }));
        CHECK(compiledVersion(mShaders, "test/solidF.glsl", GL_FRAGMENT_SHADER, 0) == usual);
        size_t shared = 0;
        for (const auto& stage : mShaders.mShaderObjects)
        {
            for (const auto& object : stage)
            {
                CAPTURE(object.first);
                REQUIRE(firstSourceLine(object.second) == usual);
                ++shared;
            }
        }
        CHECK(shared == size_t(3));

        if (ll_test::contextHasCompute())
        {
            CHECK(compiledVersion(mShaders, "test/solidF.glsl", GL_FRAGMENT_SHADER, 430) == std::string("#version 430"));
            if (gGLManager.mMaxVertexShaderStorageBlocks >= 1)
            {
                const std::initializer_list<std::pair<std::string, GLenum>> reader =
                {
                    { "test/compactV.glsl", GL_VERTEX_SHADER },
                    { "test/compactF.glsl", GL_FRAGMENT_SHADER },
                };
                UNSCOPED_INFO("a storage-block reader does not build at the usual version");
                CHECK_FALSE(buildProgram(mDraw2, "ladder reader (test)", reader));
                UNSCOPED_INFO("it builds asking for 4.30, beside the shared objects at the usual version");
                CHECK(buildProgram(mDraw2, "ladder reader 430 (test)", reader, 430));
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
        UNSCOPED_INFO("no compute program below 4.30");
        CHECK_FALSE(compute_built);
        UNSCOPED_INFO("no program asking for 4.30 below it");
        CHECK_FALSE(raised_built);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A kernel stores into an R32F and an RGBA16F target through their programs' image units. Past the barriers the
    // textures hold the pattern, read back directly and sampled by a fragment program.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a kernel stores into R32F and RGBA16F targets through image units", "[llglslshader_gl]")
    {
        setUpCompute();
        constexpr U32 W = 37;
        constexpr U32 H = 23;
        REQUIRE(mTargets[0].allocate(W, H, GL_R32F));
        REQUIRE(mTargets[1].allocate(W, H, GL_RGBA16F));
        UNSCOPED_INFO("output target allocates");
        REQUIRE(mTargets[2].allocate(W, H, GL_RGBA32F));
        REQUIRE(buildProgram(mCompute, "image store (test)", { { "test/imageStoreC.glsl", GL_COMPUTE_SHADER } }));
        REQUIRE(buildProgram(mDraw, "image sample (test)",
                             { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/sampleF.glsl", GL_FRAGMENT_SHADER } }));

        const S32 r32f_unit = mCompute.getImageUnit(TEST_IMAGE_R32F);
        const S32 rgba_unit = mCompute.getImageUnit(TEST_IMAGE_RGBA16F);
        UNSCOPED_INFO("R32F image has a unit");
        CHECK(r32f_unit >= 0);
        UNSCOPED_INFO("RGBA16F image has a unit");
        CHECK(rgba_unit >= 0);
        CHECK(r32f_unit != rgba_unit);

        mCompute.bind();
        UNSCOPED_INFO("R32F bound at its unit");
        CHECK(mCompute.bindImage(TEST_IMAGE_R32F, &mTargets[0], ALImageSlot::WRITE_ONLY) == r32f_unit);
        UNSCOPED_INFO("RGBA16F bound at its unit");
        CHECK(mCompute.bindImage(TEST_IMAGE_RGBA16F, &mTargets[1], ALImageSlot::WRITE_ONLY) == rgba_unit);
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
                CAPTURE(x, y);
                const size_t i = (size_t)y * W + x;
                REQUIRE(r32f[i] == F32(x * 1000 + y));
                REQUIRE(rgba[i * 4 + 0] == F32(x) * 0.125f);
                REQUIRE(rgba[i * 4 + 1] == F32(y) * 0.125f);
                REQUIRE(rgba[i * 4 + 2] == 0.25f);
                REQUIRE(rgba[i * 4 + 3] == 1.f);
            }
        }

        LLGLDisable blend(GL_BLEND);
        for (U32 source = 0; source < 2; ++source)
        {
            INFO("sampling the " << (source ? "RGBA16F" : "R32F") << " target");
            mTargets[2].bindTarget();
            gGL.setClearColor(LLColor4::transparent);
            mTargets[2].clear();
            mDraw.bind();
            REQUIRE(mDraw.bindTexture(TEST_SOURCE_MAP, &mTargets[source], ALSamplers::PointClamp) >= 0);
            gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
            const std::vector<F32> drawn = readBoundTarget(W, H, 4, GL_RGBA);
            mTargets[2].flush();
            mDraw.unbindTexture(TEST_SOURCE_MAP);

            for (U32 y = 0; y < H; ++y)
            {
                for (U32 x = 0; x < W; ++x)
                {
                    CAPTURE(x, y);
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
                        CAPTURE(c);
                        REQUIRE(drawn[i + c] == expected[c]);
                    }
                }
            }
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // GPU compaction drawn indirectly: a kernel keeps the ids passing a test, counting them into the instance count
    // of the draw command at the head of their list; past the barriers an indirect draw, whose vertex stage reads the
    // same buffer as a storage block, puts one point at each kept id's pixel and none anywhere else. The list is bound
    // once, for the kernel, and the draw finds it at the same point.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a compacted id list drawn indirectly puts one point at each kept id", "[llglslshader_gl]")
    {
        setUpCompute();
        if (gGLManager.mMaxVertexShaderStorageBlocks < 1)
        {
            SKIP("the context's vertex stage reads no storage blocks");
        }
        constexpr U32 COUNT = 100;
        constexpr U32 SIDE = 16;
        constexpr U32 KEPT = (COUNT + 2) / 3;

        // One vertex a point, no instances yet, then room for every id.
        std::vector<U32> list(4 + COUNT, 0u);
        list[0] = 1u;
        REQUIRE(mBuffers[0].allocate(list.size() * sizeof(U32), 0, list.data(), "compaction (test)"));
        UNSCOPED_INFO("output target allocates");
        REQUIRE(mTargets[0].allocate(SIDE, SIDE, GL_R32F));
        REQUIRE(buildProgram(mCompute, "compaction (test)", { { "test/compactC.glsl", GL_COMPUTE_SHADER } }));
        REQUIRE(buildProgram(mDraw, "compaction draw (test)",
                             { { "test/compactV.glsl", GL_VERTEX_SHADER }, { "test/compactF.glsl", GL_FRAGMENT_SHADER } },
                             430));
        const S32 point = mShaders.getStorageBlockBinding(TEST_DRAW_LIST);
        REQUIRE(point >= 0);
        UNSCOPED_INFO("the kernel's list at the table's point");
        CHECK(mCompute.getStorageBinding(TEST_DRAW_LIST) == point);
        UNSCOPED_INFO("and the draw's");
        CHECK(mDraw.getStorageBinding(TEST_DRAW_LIST) == point);

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
        UNSCOPED_INFO("the list still bound for the draw");
        CHECK(gGL.getStorageBuffer((U32)point) == mBuffers[0].getGLName());
        gGL.drawArraysIndirect(LLRender::POINTS, mBuffers[0], 0);
        const std::vector<F32> drawn = readBoundTarget(SIDE, SIDE, 1, GL_RED);
        mTargets[0].flush();

        for (U32 id = 0; id < SIDE * SIDE; ++id)
        {
            const bool kept = id < COUNT && id % 3 == 0;
            CAPTURE(id, kept);
            REQUIRE(drawn[id] == (kept ? F32(id + 1) : 0.f));
        }
        UNSCOPED_INFO("instances counted");
        CHECK(readWords(mBuffers[0], 2)[1] == KEPT);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // dispatchIndirect runs the group counts a buffer holds, here written by a kernel before it 16 bytes in: every
    // invocation of 5 x 2 x 1 groups of 4 counted once, and the last group seen in each direction.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: dispatchIndirect runs the group counts a buffer holds", "[llglslshader_gl]")
    {
        setUpCompute();
        const std::vector<U32> zeros(8, 0u);
        REQUIRE(mBuffers[0].allocate(8 * sizeof(U32), 0, zeros.data(), "dispatch (test)"));
        REQUIRE(mBuffers[1].allocate(4 * sizeof(U32), 0, zeros.data(), "counts (test)"));
        REQUIRE(buildProgram(mCompute, "dispatch writer (test)", { { "test/indirectWriteC.glsl", GL_COMPUTE_SHADER } }));
        REQUIRE(buildProgram(mCompute2, "dispatch counter (test)", { { "test/indirectCountC.glsl", GL_COMPUTE_SHADER } }));

        mCompute.bind();
        mCompute.uniform1i(TEST_COUNT, 5);
        mCompute.bindStorageBuffer(TEST_DISPATCH, mBuffers[0]);
        mCompute.dispatch(1);
        gGL.memoryBarrier(LLRender::BARRIER_COMMAND);

        mCompute2.bind();
        mCompute2.bindStorageBuffer(TEST_COUNTS, mBuffers[1]);
        mCompute2.dispatchIndirect(mBuffers[0], 16);

        const std::vector<U32> command = readWords(mBuffers[0], 7);
        UNSCOPED_INFO("command x");
        CHECK(command[4] == 5u);
        UNSCOPED_INFO("command y");
        CHECK(command[5] == 2u);
        UNSCOPED_INFO("command z");
        CHECK(command[6] == 1u);
        const std::vector<U32> hits = readWords(mBuffers[1], 4);
        UNSCOPED_INFO("invocations");
        CHECK(hits[0] == 5u * 2u * 1u * 4u);
        UNSCOPED_INFO("groups in x");
        CHECK(hits[1] == 5u);
        UNSCOPED_INFO("groups in y");
        CHECK(hits[2] == 2u);
        UNSCOPED_INFO("groups in z");
        CHECK(hits[3] == 1u);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Two storage blocks declared in the order opposite to the table's take the table's points, which run in its
    // order, and bindStorageBuffer routes each buffer to its own block: swapping the buffers swaps what each holds.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: storage blocks declared out of the table's order still take its points", "[llglslshader_gl]")
    {
        setUpCompute();
        REQUIRE(buildProgram(mCompute, "block remap (test)", { { "test/remapC.glsl", GL_COMPUTE_SHADER } }));
        const S32 first = mShaders.getStorageBlockBinding(TEST_BLOCK_FIRST);
        REQUIRE(first >= 0);
        UNSCOPED_INFO("the first block at its point");
        CHECK(mCompute.getStorageBinding(TEST_BLOCK_FIRST) == first);
        UNSCOPED_INFO("the second at the next");
        CHECK(mCompute.getStorageBinding(TEST_BLOCK_SECOND) == first + 1);
        UNSCOPED_INFO("an undeclared block has none");
        CHECK(mCompute.getStorageBinding(TEST_COUNTS) == -1);
        UNSCOPED_INFO("nor does an index past the table");
        CHECK(mCompute.getStorageBinding(TEST_STORAGE_END) == -1);

        const std::vector<U32> zeros(4, 0u);
        REQUIRE(mBuffers[0].allocate(4 * sizeof(U32), 0, zeros.data(), "a (test)"));
        REQUIRE(mBuffers[1].allocate(4 * sizeof(U32), 0, zeros.data(), "b (test)"));

        mCompute.bind();
        mCompute.bindStorageBuffer(TEST_BLOCK_FIRST, mBuffers[0]);
        mCompute.bindStorageBuffer(TEST_BLOCK_SECOND, mBuffers[1]);
        mCompute.dispatch(1);
        UNSCOPED_INFO("a holds the first block");
        CHECK(readWords(mBuffers[0], 1)[0] == 111u);
        UNSCOPED_INFO("b holds the second block");
        CHECK(readWords(mBuffers[1], 1)[0] == 222u);

        mBuffers[0].clear();
        mBuffers[1].clear();
        gGL.memoryBarrier(LLRender::BARRIER_STORAGE);
        mCompute.bindStorageBuffer(TEST_BLOCK_FIRST, mBuffers[1]);
        mCompute.bindStorageBuffer(TEST_BLOCK_SECOND, mBuffers[0]);
        mCompute.dispatch(1);
        UNSCOPED_INFO("swapped, a holds the second block");
        CHECK(readWords(mBuffers[0], 1)[0] == 222u);
        UNSCOPED_INFO("swapped, b holds the first block");
        CHECK(readWords(mBuffers[1], 1)[0] == 111u);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Two image uniforms, declared the opposite way round, take distinct units in reserved order, and bindImage
    // routes each texture to its own.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: image uniforms declared out of order take units in reserved order", "[llglslshader_gl]")
    {
        setUpCompute();
        constexpr U32 SIDE = 16;
        REQUIRE(buildProgram(mCompute, "image units (test)", { { "test/imageUnitsC.glsl", GL_COMPUTE_SHADER } }));
        UNSCOPED_INFO("the first reserved image first");
        CHECK(mCompute.getImageUnit(TEST_IMAGE_A) == 0);
        UNSCOPED_INFO("the second reserved image second");
        CHECK(mCompute.getImageUnit(TEST_IMAGE_B) == 1);
        UNSCOPED_INFO("an undeclared image has none");
        CHECK(mCompute.getImageUnit(TEST_IMAGE_R32F) == -1);
        UNSCOPED_INFO("a sampler has none");
        CHECK(mCompute.getImageUnit(LLShaderMgr::DIFFUSE_MAP) == -1);
        UNSCOPED_INFO("nor a texture unit");
        CHECK(mCompute.getTextureChannel(TEST_IMAGE_A) == -1);

        REQUIRE(mTargets[0].allocate(SIDE, SIDE, GL_R32F));
        REQUIRE(mTargets[1].allocate(SIDE, SIDE, GL_R32F));
        mCompute.bind();
        CHECK(mCompute.bindImage(TEST_IMAGE_A, &mTargets[0], ALImageSlot::WRITE_ONLY) == 0);
        CHECK(mCompute.bindImage(TEST_IMAGE_B, &mTargets[1], ALImageSlot::WRITE_ONLY) == 1);
        mCompute.dispatchThreads(SIDE, SIDE);
        gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_UPDATE);
        mCompute.unbindImage(TEST_IMAGE_A);
        mCompute.unbindImage(TEST_IMAGE_B);

        const std::vector<F32> a = readTexture<F32>(mTargets[0].getTexture(), SIDE, SIDE, 1, GL_RED, GL_FLOAT);
        const std::vector<F32> b = readTexture<F32>(mTargets[1].getTexture(), SIDE, SIDE, 1, GL_RED, GL_FLOAT);
        for (U32 i = 0; i < SIDE * SIDE; ++i)
        {
            CAPTURE(i);
            UNSCOPED_INFO("a holds image A");
            REQUIRE(a[i] == 1.f);
            UNSCOPED_INFO("b holds image B");
            REQUIRE(b[i] == 2.f);
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A usampler2D under a reserved name gets a texture unit, the one after a float sampler's, and reads an R32UI
    // texture's values exactly, past what eight bits could hold.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a usampler2D takes the unit after a float one and reads R32UI exactly", "[llglslshader_gl]")
    {
        setUpCompute();
        constexpr U32 W = 13;
        constexpr U32 H = 7;
        REQUIRE(buildProgram(mCompute, "uint sample (test)", { { "test/uintSampleC.glsl", GL_COMPUTE_SHADER } }));
        const S32 float_unit = mCompute.getTextureChannel(LLShaderMgr::DIFFUSE_MAP);
        const S32 uint_unit = mCompute.getTextureChannel(TEST_UINT_MAP);
        CHECK(float_unit == 0);
        CHECK(uint_unit == 1);

        std::vector<U32> values(W * H);
        for (U32 i = 0; i < W * H; ++i)
        {
            values[i] = 0x10000u + i * 7u;
        }
        mUintImage = new LLImageGL(W, H, 1, /*usemipmaps=*/false);
        mUintImage->setExplicitFormat(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT);
        REQUIRE(mUintImage->createGLTexture(0, reinterpret_cast<const U8*>(values.data())));
        REQUIRE(mTargets[1].allocate(W, H, GL_RGBA8));
        REQUIRE(mBuffers[0].allocate(W * H * sizeof(U32), 0, nullptr, "uint sample (test)"));
        mBuffers[0].clear(0xFFFFFFFFu);

        mCompute.bind();
        mCompute.uniform1i(TEST_COUNT, 1);
        CHECK(mCompute.bindTexture(LLShaderMgr::DIFFUSE_MAP, &mTargets[1], ALSamplers::PointClamp) == float_unit);
        CHECK(gGL.getTextureSlot((U32)uint_unit)->bindSampled(mUintImage.get(), ALSamplers::PointClamp));
        mCompute.bindStorageBuffer(TEST_COUNTS, mBuffers[0]);
        mCompute.dispatchThreads(W, H);
        mCompute.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
        mCompute.unbindTexture(TEST_UINT_MAP);

        const std::vector<U32> read = readWords(mBuffers[0], W * H);
        for (U32 i = 0; i < W * H; ++i)
        {
            CAPTURE(i);
            REQUIRE(read[i] == values[i]);
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Integer targets read through bindTexture with no sampler named: an R32UI and an RG16I target, each written by a
    // program's integer output, come back texel for texel through a usampler2D and an isampler2D. A filtering sampler
    // leaves an integer texture incomplete, reading 0, so the default for an integer attachment is point, attachment 0
    // included.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: bindTexture with no sampler named point-samples integer targets", "[llglslshader_gl]")
    {
        setUpShaders();
        constexpr U32 W = 11;
        constexpr U32 H = 6;
        REQUIRE(mTargets[0].allocate(W, H, GL_R32UI));
        REQUIRE(mTargets[1].allocate(W, H, GL_RG16I));
        UNSCOPED_INFO("output target allocates");
        REQUIRE(mTargets[2].allocate(W, H, GL_RGBA32UI));
        UNSCOPED_INFO("the output's signed attachment");
        REQUIRE(mTargets[2].addColorAttachment(GL_RGBA32I));
        REQUIRE(buildProgram(mDraw, "uint write (test)",
                             { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/uintWriteF.glsl", GL_FRAGMENT_SHADER } }));
        REQUIRE(buildProgram(mDraw2, "int write (test)",
                             { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/intWriteF.glsl", GL_FRAGMENT_SHADER } }));
        REQUIRE(buildProgram(mDraw3, "integer sample (test)",
                             { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/intSampleF.glsl", GL_FRAGMENT_SHADER } }));
        UNSCOPED_INFO("the unsigned sampler has a unit");
        REQUIRE(mDraw3.getTextureChannel(TEST_UINT_MAP) >= 0);
        UNSCOPED_INFO("the signed sampler has a unit");
        REQUIRE(mDraw3.getTextureChannel(TEST_INT_MAP) >= 0);

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
        CHECK(mDraw3.bindTexture(TEST_UINT_MAP, &mTargets[0]) >= 0);
        CHECK(mDraw3.bindTexture(TEST_INT_MAP, &mTargets[1]) >= 0);

        // Read off GL, since a driver may read an integer texture under a filtering sampler texel by texel anyway.
        for (const S32 uniform : { (S32)TEST_UINT_MAP, (S32)TEST_INT_MAP })
        {
            INFO((uniform == TEST_UINT_MAP ? "the unsigned source's sampler" : "the signed source's sampler"));
            glActiveTexture(GL_TEXTURE0 + mDraw3.getTextureChannel(uniform));
            GLint sampler = 0;
            glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
            GLint min_filter = 0;
            GLint mag_filter = 0;
            glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_MIN_FILTER, &min_filter);
            glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_MAG_FILTER, &mag_filter);
            REQUIRE((U32)mag_filter == (U32)GL_NEAREST);
            REQUIRE((U32)min_filter == (U32)GL_NEAREST_MIPMAP_NEAREST);
        }
        glActiveTexture(GL_TEXTURE0 + gGL.getCurrentTexUnitIndex());
        // And the debug validator, which checks every sampler a draw reads, finds nothing to say -- where a filtering
        // sampler named over an integer texture is what it is there to catch.
        gDebugGL = true;
        const bool samplers_valid = validate_bound_samplers();
        mDraw3.bindTexture(TEST_UINT_MAP, &mTargets[0], ALSamplers::BilinearClamp);
        const bool filtered_valid = validate_bound_samplers();
        gDebugGL = false;
        UNSCOPED_INFO("the debug validator passes the binds");
        CHECK(samplers_valid);
        UNSCOPED_INFO("and refuses a filtering sampler over an integer texture");
        CHECK_FALSE(filtered_valid);
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
                CAPTURE(x, y);
                const size_t i = ((size_t)y * W + x) * 4;
                const U32 uint_expected[4] = { 0x01000001u + y * 1000u + x, 0u, 0u, 1u };
                const S32 int_expected[4] = { -1 - (S32)x, (S32)y * 300 - 1000, 0, 1 };
                for (U32 c = 0; c < 4; ++c)
                {
                    CAPTURE(c);
                    REQUIRE(uints[i + c] == uint_expected[c]);
                    REQUIRE(ints[i + c] == int_expected[c]);
                }
            }
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A shared object is the shader manager's, not the programs' that attach it. Two programs built through
    // createShader attach the same one, as every program with a feature attaches its object, and call into it. Once
    // linked a program holds no object, and its own are deleted: the count of live objects is what it was before the
    // two built, and the shared one is alive and not even flagged. Unloading one leaves the other drawing; a third
    // built after both are gone attaches the shared object again and links. One that fails to link deletes its own
    // objects all the same. clearShaderObjects deletes every shared object at once, as no linked program holds one,
    // and the programs draw on without them.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a shared object belongs to the shader manager and not to its programs", "[llglslshader_gl]")
    {
        setUpShaders();
        constexpr U32 SIDE = 4;
        UNSCOPED_INFO("output target allocates");
        REQUIRE(mTargets[0].allocate(SIDE, SIDE, GL_RGBA32F));

        const auto& fragments = mShaders.mShaderObjects[LLShaderMgr::STAGE_FRAGMENT];
        const auto found = fragments.find("deferred/globalF.glsl");
        UNSCOPED_INFO("the shared object compiled");
        REQUIRE(found != fragments.end());
        const GLuint shared = found->second;

        const std::initializer_list<std::pair<std::string, GLenum>> files =
        {
            { "test/fullscreenV.glsl", GL_VERTEX_SHADER },
            { "test/globalCallF.glsl", GL_FRAGMENT_SHADER },
        };
        const U32 live = LLShaderMgr::sLiveObjects;

        // That `program` draws every pixel the colour the stub passes through.
        LLGLDisable blend(GL_BLEND);
        auto require_draws = [&](const std::string& what, LLGLSLShader& program)
        {
            INFO(what);
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
                CAPTURE(i);
                REQUIRE(drawn[i] == expected[i % 4]);
            }
        };

        REQUIRE(buildProgram(mDraw, "shared A (test)", files));
        REQUIRE(buildProgram(mDraw2, "shared B (test)", files));
        UNSCOPED_INFO("A holds no object once linked");
        CHECK(attachedNames(mDraw.mProgramObject).empty());
        UNSCOPED_INFO("nor does B");
        CHECK(attachedNames(mDraw2.mProgramObject).empty());
        UNSCOPED_INFO("their own objects deleted");
        CHECK(LLShaderMgr::sLiveObjects == live);
        UNSCOPED_INFO("a program's own stage is not shared");
        CHECK_FALSE(fragments.contains("test/globalCallF.glsl"));
        UNSCOPED_INFO("the shared object stays");
        CHECK(glIsShader(shared) == GL_TRUE);
        UNSCOPED_INFO("not even flagged for deletion");
        CHECK_FALSE(flaggedForDeletion(shared));
        require_draws("A draws", mDraw);

        mDraw.unload();
        UNSCOPED_INFO("unloading A leaves the shared object");
        CHECK((glIsShader(shared) == GL_TRUE && !flaggedForDeletion(shared)));
        glValidateProgram(mDraw2.mProgramObject);
        GLint valid = GL_FALSE;
        glGetProgramiv(mDraw2.mProgramObject, GL_VALIDATE_STATUS, &valid);
        CHECK(valid == GL_TRUE);
        require_draws("B draws", mDraw2);

        mDraw2.unload();
        UNSCOPED_INFO("C builds after A and B are gone");
        REQUIRE(buildProgram(mDraw3, "shared C (test)", files));
        UNSCOPED_INFO("C holds no object once linked");
        CHECK(attachedNames(mDraw3.mProgramObject).empty());
        require_draws("C draws", mDraw3);

        // Without the shared object to call into, the stage cannot link, at any class.
        UNSCOPED_INFO("D does not link");
        CHECK_FALSE(buildProgram(mDraw, "unlinked D (test)", files, 0, /*attach_nothing=*/true));
        UNSCOPED_INFO("and its own objects are deleted all the same");
        CHECK(LLShaderMgr::sLiveObjects == live);

        S32 level = 1;
        const GLuint loose = mShaders.loadShaderFile("test/solidF.glsl", level, GL_FRAGMENT_SHADER);
        UNSCOPED_INFO("an object no program holds");
        CHECK((loose != 0 && glIsShader(loose) == GL_TRUE));
        UNSCOPED_INFO("the loose object counted live");
        CHECK(LLShaderMgr::sLiveObjects == live + 1);

        size_t held = 0;
        for (const auto& stage : mShaders.mShaderObjects)
        {
            held += stage.size();
        }
        mShaders.clearShaderObjects();
        for (const auto& stage : mShaders.mShaderObjects)
        {
            UNSCOPED_INFO("the manager holds no object");
            REQUIRE(stage.empty());
        }
        UNSCOPED_INFO("one no program ever held goes at once");
        CHECK(glIsShader(loose) == GL_FALSE);
        UNSCOPED_INFO("and one programs linked with");
        CHECK(glIsShader(shared) == GL_FALSE);
        UNSCOPED_INFO("none counted live");
        CHECK(LLShaderMgr::sLiveObjects == live + 1 - (U32)held);
        require_draws("C still draws", mDraw3);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A buffer bound once at a reserved block's point serves every program after it: one program declaring the block
    // alone, then one declaring a block before it in the table as well, each find their buffers with nothing bound
    // between -- where a numbering by program would put the shared block at point 0 for the first, the second block's
    // place. And LLRender keeps the points: a bind of what a point holds issues nothing, which a bind made behind its
    // back shows, and refreshState puts back what it holds.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a buffer bound once at a block's point serves every program after it", "[llglslshader_gl]")
    {
        setUpCompute();
        REQUIRE(buildProgram(mCompute, "first only (test)", { { "test/firstOnlyC.glsl", GL_COMPUTE_SHADER } }));
        REQUIRE(buildProgram(mCompute2, "first and counts (test)", { { "test/firstAndCountsC.glsl", GL_COMPUTE_SHADER } }));
        const S32 first = mShaders.getStorageBlockBinding(TEST_BLOCK_FIRST);
        const S32 counts = mShaders.getStorageBlockBinding(TEST_COUNTS);
        REQUIRE(first >= 0);
        REQUIRE(counts >= 0);
        UNSCOPED_INFO("the first program's block at the table's point");
        CHECK(mCompute.getStorageBinding(TEST_BLOCK_FIRST) == first);
        UNSCOPED_INFO("the second's too");
        CHECK(mCompute2.getStorageBinding(TEST_BLOCK_FIRST) == first);
        UNSCOPED_INFO("the two blocks apart");
        CHECK(first != counts);

        const std::vector<U32> zeros(4, 0u);
        REQUIRE(mBuffers[0].allocate(4 * sizeof(U32), 0, zeros.data(), "first (test)"));
        REQUIRE(mBuffers[1].allocate(4 * sizeof(U32), 0, zeros.data(), "counts (test)"));

        mCompute.bind();
        gGL.bindStorageBuffer((U32)first, mBuffers[0]);
        gGL.bindStorageBuffer((U32)counts, mBuffers[1]);
        mCompute.dispatch(1);
        mCompute2.bind();
        mCompute2.dispatch(1);

        const std::vector<U32> held_first = readWords(mBuffers[0], 2);
        UNSCOPED_INFO("the first program wrote the first block");
        CHECK(held_first[0] == 7u);
        UNSCOPED_INFO("the second program wrote it too");
        CHECK(held_first[1] == 9u);
        UNSCOPED_INFO("and its counts");
        CHECK(readWords(mBuffers[1], 1)[0] == 5u);

        // Behind LLRender's back, then the bind it holds already: nothing issued, so GL keeps the other buffer.
        auto bound_at = [](S32 point)
        {
            GLint name = 0;
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, (GLuint)point, &name);
            return (U32)name;
        };
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, (GLuint)first, mBuffers[1].getGLName());
        gGL.bindStorageBuffer((U32)first, mBuffers[0]);
        UNSCOPED_INFO("a bind of what the point holds issues nothing");
        CHECK(bound_at(first) == mBuffers[1].getGLName());
        gGL.refreshState();
        UNSCOPED_INFO("refreshState puts back what it holds");
        CHECK(bound_at(first) == mBuffers[0].getGLName());

        // A range, at a multiple of the context's alignment at least 16 bytes in, is a different bind from the whole
        // buffer.
        const size_t alignment = (size_t)gGLManager.mShaderStorageBufferOffsetAlignment;
        const size_t offset = (16 + alignment - 1) / alignment * alignment;
        ALGPUBuffer wide;
        REQUIRE(wide.allocate(offset + 4 * sizeof(U32), 0, nullptr, "wide (test)"));
        wide.clear();
        mCompute.bind();
        gGL.bindStorageBuffer((U32)first, wide, offset, 4 * sizeof(U32));
        mCompute.dispatch(1);
        std::vector<U32> words((offset + 4 * sizeof(U32)) / sizeof(U32), 0xFFFFFFFFu);
        wide.read(0, words.data(), words.size() * sizeof(U32));
        UNSCOPED_INFO("the range's first word written");
        CHECK(words[offset / sizeof(U32)] == 7u);
        UNSCOPED_INFO("the word before it not");
        CHECK(words[offset / sizeof(U32) - 1] == 0u);

        // Releasing a bound buffer forgets it.
        wide.release();
        UNSCOPED_INFO("a released buffer leaves its point empty");
        CHECK(gGL.getStorageBuffer((U32)first) == 0u);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // An array of blocks takes one point per element from the table's start for it, and bindStorageBuffer names the
    // element: each of two buffers takes its element's writes. A program reading only the second element finds it
    // at its point all the same, though GL may report no first element for it.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: a block array takes consecutive points and binds buffers by element", "[llglslshader_gl]")
    {
        setUpCompute();
        REQUIRE(buildProgram(mCompute, "block array (test)", { { "test/blockArrayC.glsl", GL_COMPUTE_SHADER } }));
        REQUIRE(buildProgram(mCompute2, "block array one (test)", { { "test/blockArrayOneC.glsl", GL_COMPUTE_SHADER } }));
        const S32 base = mShaders.getStorageBlockBinding(TEST_BLOCK_ARRAY);
        UNSCOPED_INFO("the table gives the array a point");
        CHECK(base >= 0);
        UNSCOPED_INFO("element 0 at it");
        CHECK(mCompute.getStorageBinding(TEST_BLOCK_ARRAY, 0) == base);
        UNSCOPED_INFO("element 1 at the next");
        CHECK(mCompute.getStorageBinding(TEST_BLOCK_ARRAY, 1) == base + 1);
        UNSCOPED_INFO("no element 2");
        CHECK(mCompute.getStorageBinding(TEST_BLOCK_ARRAY, 2) == -1);
        UNSCOPED_INFO("the second-element program's element 1 there too");
        CHECK(mCompute2.getStorageBinding(TEST_BLOCK_ARRAY, 1) == base + 1);

        const std::vector<U32> zeros(4, 0u);
        REQUIRE(mBuffers[0].allocate(4 * sizeof(U32), 0, zeros.data(), "element 0 (test)"));
        REQUIRE(mBuffers[1].allocate(4 * sizeof(U32), 0, zeros.data(), "element 1 (test)"));

        mCompute.bind();
        mCompute.bindStorageBuffer(TEST_BLOCK_ARRAY, mBuffers[0], 0);
        mCompute.bindStorageBuffer(TEST_BLOCK_ARRAY, mBuffers[1], 1);
        mCompute.dispatch(1);
        UNSCOPED_INFO("element 0's buffer");
        CHECK(readWords(mBuffers[0], 1)[0] == 11u);
        UNSCOPED_INFO("element 1's buffer");
        CHECK(readWords(mBuffers[1], 1)[0] == 22u);

        mCompute2.bind();
        mCompute2.dispatch(1);
        UNSCOPED_INFO("the second element's writer reaches element 1's buffer");
        CHECK(readWords(mBuffers[1], 1)[0] == 33u);
        UNSCOPED_INFO("and not element 0's");
        CHECK(readWords(mBuffers[0], 1)[0] == 11u);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // An image array takes its units in reserved order like any image, though GL names it by its first element: it is
    // before testImageA in the table, so its two elements take units 0 and 1 and testImageA 2. bindImage names the
    // element, and each target takes its element's stores.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: an image array takes its units in reserved order like any image", "[llglslshader_gl]")
    {
        setUpCompute();
        constexpr U32 SIDE = 8;
        REQUIRE(buildProgram(mCompute, "image array (test)", { { "test/imageArrayC.glsl", GL_COMPUTE_SHADER } }));
        UNSCOPED_INFO("element 0 first");
        CHECK(mCompute.getImageUnit(TEST_IMAGES, 0) == 0);
        UNSCOPED_INFO("element 1 next");
        CHECK(mCompute.getImageUnit(TEST_IMAGES, 1) == 1);
        UNSCOPED_INFO("the single image after the array");
        CHECK(mCompute.getImageUnit(TEST_IMAGE_A) == 2);

        for (U32 i = 0; i < 3; ++i)
        {
            CAPTURE(i);
            REQUIRE(mTargets[i].allocate(SIDE, SIDE, GL_R32F));
        }
        mCompute.bind();
        CHECK(mCompute.bindImage(TEST_IMAGE_A, &mTargets[0], ALImageSlot::WRITE_ONLY) == 2);
        CHECK(mCompute.bindImage(TEST_IMAGES, &mTargets[1], ALImageSlot::WRITE_ONLY, 0, 0, 0) == 0);
        CHECK(mCompute.bindImage(TEST_IMAGES, &mTargets[2], ALImageSlot::WRITE_ONLY, 0, 0, 1) == 1);
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
                CAPTURE(t, i);
                REQUIRE(held[i] == expected[t]);
            }
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // The debug validator reads image units as it reads texture units: a signed integer image over a float format is
    // undefined and refused, over a signed integer one passed, and an empty unit is fine.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: the debug validator reads image units as it reads texture units", "[llglslshader_gl]")
    {
        setUpCompute();
        constexpr U32 SIDE = 4;
        REQUIRE(buildProgram(mCompute, "int image (test)", { { "test/imageIntC.glsl", GL_COMPUTE_SHADER } }));
        REQUIRE(mTargets[0].allocate(SIDE, SIDE, GL_R32F));
        REQUIRE(mTargets[1].allocate(SIDE, SIDE, GL_R32I));

        mCompute.bind();
        gDebugGL = true;
        const bool empty_valid = validate_bound_samplers();
        mCompute.bindImage(TEST_IMAGE_INT, &mTargets[0], ALImageSlot::WRITE_ONLY);
        const bool float_valid = validate_bound_samplers();
        mCompute.bindImage(TEST_IMAGE_INT, &mTargets[1], ALImageSlot::WRITE_ONLY);
        const bool signed_valid = validate_bound_samplers();
        gDebugGL = false;
        mCompute.unbindImage(TEST_IMAGE_INT);

        UNSCOPED_INFO("an empty image unit passes");
        CHECK(empty_valid);
        UNSCOPED_INFO("a signed image over a float format is refused");
        CHECK_FALSE(float_valid);
        UNSCOPED_INFO("over a signed format it passes");
        CHECK(signed_valid);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A depth-stencil texture reads as its depth until GL 4.3's stencil texturing turns it to its stencil: the debug
    // validator refuses an unsigned sampler over it as depth and passes it as stencil, and the reverse for a float
    // sampler.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: the debug validator reads a depth-stencil texture by its texture mode", "[llglslshader_gl]")
    {
        setUpShaders();
        if (gGLManager.mGLVersion < 4.29f)
        {
            SKIP("the context has no stencil texturing");
        }
        REQUIRE(buildProgram(mDraw, "stencil read (test)",
                             { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/intSampleF.glsl", GL_FRAGMENT_SHADER } }));
        REQUIRE(buildProgram(mDraw2, "depth read (test)",
                             { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/sampleF.glsl", GL_FRAGMENT_SHADER } }));
        // The units the reads below bind the texture at. At -1 the bind would go to LLRender's dummy slot and the mode
        // to whatever texture the active unit holds.
        REQUIRE(mDraw.getTextureChannel(TEST_UINT_MAP) >= 0);
        REQUIRE(mDraw2.getTextureChannel(TEST_SOURCE_MAP) >= 0);

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

        UNSCOPED_INFO("an unsigned sampler over the depth refused");
        CHECK_FALSE(validates(mDraw, TEST_UINT_MAP, GL_DEPTH_COMPONENT));
        UNSCOPED_INFO("over the stencil passed");
        CHECK(validates(mDraw, TEST_UINT_MAP, GL_STENCIL_INDEX));
        UNSCOPED_INFO("a float sampler over the stencil refused");
        CHECK_FALSE(validates(mDraw2, TEST_SOURCE_MAP, GL_STENCIL_INDEX));
        UNSCOPED_INFO("over the depth passed");
        CHECK(validates(mDraw2, TEST_SOURCE_MAP, GL_DEPTH_COMPONENT));

        glDeleteTextures(1, &texture);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Every kind of sampler takes a texture unit, in reserved order, an array included under its bare name though GL
    // names it by its first element: the two-element array first, then the array texture, the unsigned sampler and
    // the integer buffer.
    TEST_CASE_METHOD(llglslshader_data, "llglslshader_gl: every kind of sampler takes a texture unit in reserved order", "[llglslshader_gl]")
    {
        setUpShaders();
        REQUIRE(buildProgram(mDraw, "sampler types (test)",
                             { { "test/fullscreenV.glsl", GL_VERTEX_SHADER }, { "test/samplerTypesF.glsl", GL_FRAGMENT_SHADER } }));
        UNSCOPED_INFO("the array first");
        CHECK(mDraw.getTextureChannel(TEST_SAMPLERS) == 0);
        UNSCOPED_INFO("the array texture after its two units");
        CHECK(mDraw.getTextureChannel(TEST_SOURCE_MAP) == 2);
        UNSCOPED_INFO("the unsigned sampler next");
        CHECK(mDraw.getTextureChannel(TEST_UINT_MAP) == 3);
        UNSCOPED_INFO("the integer buffer last");
        CHECK(mDraw.getTextureChannel(TEST_INT_MAP) == 4);

        GLint units[2] = { -1, -1 };
        glGetUniformiv(mDraw.mProgramObject, glGetUniformLocation(mDraw.mProgramObject, "testSamplers[1]"), &units[1]);
        glGetUniformiv(mDraw.mProgramObject, glGetUniformLocation(mDraw.mProgramObject, "testSamplers[0]"), &units[0]);
        UNSCOPED_INFO("the array's first element at its unit");
        CHECK(units[0] == 0);
        UNSCOPED_INFO("its second at the next");
        CHECK(units[1] == 1);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A profile's draws: triangles over a small target, from no vertex arrays. Its program and target go here, and
    // what the case changes of the context with mGL.
    struct llglslshader_profile_data
    {
        ~llglslshader_profile_data()
        {
            if (mFBO)
            {
                glDeleteFramebuffers(1, &mFBO);
            }
            if (mColor)
            {
                glDeleteRenderbuffers(1, &mColor);
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

        void setUp()
        {
            // Every triangle the same one, over the whole viewport.
            const char* vertex =
                "#version 400\n"
                "void main()\n"
                "{\n"
                "    int v = gl_VertexID % 3;\n"
                "    gl_Position = vec4(vec2((v << 1) & 2, v & 2) * 2.0 - 1.0, 0.0, 1.0);\n"
                "}\n";
            const char* fragment =
                "#version 400\n"
                "out vec4 frag_color;\n"
                "void main() { frag_color = vec4(1.0); }\n";
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, vertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, fragment);
            REQUIRE(vs);
            REQUIRE(fs);
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
            glGenRenderbuffers(1, &mColor);
            glBindRenderbuffer(GL_RENDERBUFFER, mColor);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 8, 8);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glGenFramebuffers(1, &mFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, mColor);
            REQUIRE((U32)glCheckFramebufferStatus(GL_FRAMEBUFFER) == (U32)GL_FRAMEBUFFER_COMPLETE);
            glViewport(0, 0, 8, 8);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_DEPTH_TEST);
        }

        void drawTriangles(GLsizei count)
        {
            glUseProgram(mProgram);
            glBindVertexArray(mVAO);
            glDrawArrays(GL_TRIANGLES, 0, count * 3);
            glBindVertexArray(0);
        }

        ll_test::SharedGLScope mGL;
        GLuint mProgram = 0;
        GLuint mVAO = 0;
        GLuint mColor = 0;
        GLuint mFBO = 0;
    };

    // A bind's triangles are the primitives its draws generate, counted as they are, whichever counters the profile
    // takes: samples passed and primitives generated, or the pipeline statistics where the context has them.
    TEST_CASE_METHOD(llglslshader_profile_data, "llglslshader_gl: a profile counts the triangles a bind draws", "[llglslshader_gl]")
    {
        setUp();
        for (const bool count_samples : { true, false })
        {
            LLGLSLShader profile;
            profile.clearStats();
            LLGLSLShader::initProfile(count_samples);
            profile.placeProfileQuery();
            drawTriangles(6);
            LLGLSLShader::endProfileQuery();
            LLSD stats;
            LLGLSLShader::finishProfile(stats);
            INFO((count_samples ? "samples passed and primitives generated" : "the profile's own counters"));
            CHECK(profile.mBinds == 1);
            CHECK(profile.mTrianglesDrawn == 6);
            CHECK(profile.mSamplesDrawn > 0);
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A render pass ends a bind's queries, as no query may cross one, and they count on in the next: the bind's draws
    // on both sides are its own, and it is still one bind.
    TEST_CASE_METHOD(llglslshader_profile_data, "llglslshader_gl: a render pass splits a bind's queries, not the bind", "[llglslshader_gl]")
    {
        setUp();
        LLRenderTarget target;
        REQUIRE(target.allocate(8, 8, GL_RGBA8));
        for (const bool count_samples : { true, false })
        {
            LLGLSLShader profile;
            profile.clearStats();
            LLGLSLShader::initProfile(count_samples);
            profile.placeProfileQuery();
            drawTriangles(2);
            target.bindTarget();
            drawTriangles(3);
            target.flush();
            drawTriangles(1);
            LLGLSLShader::endProfileQuery();
            LLSD stats;
            LLGLSLShader::finishProfile(stats);
            INFO((count_samples ? "samples passed and primitives generated" : "the profile's own counters"));
            CHECK(profile.mBinds == 1);
            CHECK(profile.mTrianglesDrawn == 6);
        }
        target.release();
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A profile begun over one still open abandons it: the first's queries end and its targets go back to the frame,
    // as abandonProfile() gives them back, reading nothing.
    TEST_CASE_METHOD(llglslshader_profile_data, "llglslshader_gl: a profile left open is abandoned, its targets given back", "[llglslshader_gl]")
    {
        setUp();
        ALQueryPool& queries = gGL.queries();
        LLGLSLShader profile;
        profile.clearStats();
        LLGLSLShader::initProfile(true);
        profile.placeProfileQuery();
        drawTriangles(1);
        LLGLSLShader::initProfile(true);
        profile.placeProfileQuery();
        drawTriangles(2);
        LLSD stats;
        LLGLSLShader::finishProfile(stats);
        {
            INFO("the second profile counted its own bind alone");
            CHECK(profile.mBinds == 1);
            CHECK(profile.mTrianglesDrawn == 2);
        }
        {
            INFO("and every target is the frame's again");
            CHECK(queries.canBegin(ALQueryKind::Occlusion));
            CHECK(queries.canBegin(ALQueryKind::Primitives));
        }

        profile.clearStats();
        LLGLSLShader::initProfile(true);
        profile.placeProfileQuery();
        drawTriangles(1);
        LLGLSLShader::abandonProfile();
        {
            INFO("an abandoned profile reads nothing and gives its targets back");
            CHECK_FALSE(LLGLSLShader::sProfileEnabled);
            CHECK(profile.mBinds == 0);
            CHECK(queries.canBegin(ALQueryKind::Occlusion));
            CHECK(queries.canBegin(ALQueryKind::Primitives));
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }
}
