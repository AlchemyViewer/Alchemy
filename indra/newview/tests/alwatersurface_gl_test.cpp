/**
 * @file alwatersurface_gl_test.cpp
 * @brief The water surface's draws on the hidden window: the water's field as the vertex stage lays it out; whether
 *        any of it shows, and the draws that wait on that; the water's stencil bits: where the exclusion pass removes
 *        the water, and which pixels the water shades, which the haze pass reads; and the waves' patches as the
 *        tessellation stages emit them, displaced by the heights baked from the normal map, and the depth pyramid
 *        that culls those the scene hides.
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

#include "../aldepthpyramid.h"
#include "../alwaterfield.h"
#include "../alwaterheightbake.h"
#include "../alwaterstencil.h"

#include "alquerypool.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llvertexbuffer.h"
#include "v3dmath.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace
{
        constexpr S32 W = 16;
        constexpr S32 H = 16;

        // A surface of one colour, placed by the matrices.
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
            "void main() { gl_Position = modelview_projection_matrix * vec4(position, 1.0); }\n";

        const char* kColourFragment =
            "#version 330\n"
            "uniform vec4 color;\n"
            "out vec4 frag_color;\n"
            "void main() { frag_color = color; }\n";

        // The water scene: 512 m of the plane square across the target, 32 m a pixel, agent space at the global
        // origin. Four regions, one a cell: the south-east one's water at 30 m, the others' at 20 m.
        constexpr F32 SPAN = 512.f;
        constexpr F64 CELL = 256.0;
        constexpr F32 LOW = 20.f;
        constexpr F32 HIGH = 30.f;
        constexpr F32 NEAR_Z = 1.f;
        constexpr F32 FAR_Z = 200.f;

        const LLColor4 GROUND(0.5f, 0.5f, 0.5f, 1.f);
        const LLColor4 WATER(0.f, 0.f, 1.f, 1.f);

        // A camera at height `z` looking straight down, or up: eye space is agent space moved down to the camera
        // and, looking up, turned half over about x, so y and z run the other way.
        LLMatrix4a view(F32 z, bool up)
        {
            const F32 s = up ? -1.f : 1.f;
            LLMatrix4a m;
            m.setRow<0>(LLVector4a(1.f, 0.f, 0.f, 0.f));
            m.setRow<1>(LLVector4a(0.f, s, 0.f, 0.f));
            m.setRow<2>(LLVector4a(0.f, 0.f, s, 0.f));
            m.setRow<3>(LLVector4a(0.f, 0.f, -s * z, 1.f));
            return m;
        }

        // The orthographic projection of [0, SPAN] square onto the target, from that camera.
        LLMatrix4a ortho(bool up)
        {
            const F32 y0 = up ? -SPAN : 0.f;
            LLMatrix4a m;
            m.setRow<0>(LLVector4a(2.f / SPAN, 0.f, 0.f, 0.f));
            m.setRow<1>(LLVector4a(0.f, 2.f / SPAN, 0.f, 0.f));
            m.setRow<2>(LLVector4a(0.f, 0.f, -2.f / (FAR_Z - NEAR_Z), 0.f));
            m.setRow<3>(LLVector4a(-1.f, -1.f - 2.f * y0 / SPAN, -(FAR_Z + NEAR_Z) / (FAR_Z - NEAR_Z), 1.f));
            return m;
        }

        std::vector<ALWaterField::Region> regions()
        {
            return { { 0, 0, 1, LOW }, { 1, 0, 1, HIGH }, { 0, 1, 1, LOW }, { 1, 1, 1, LOW } };
        }

        // The field seen whole from above: a stretch short enough that two rings reach the rim, and a view a little
        // wider than the rim, 64 pixels across.
        constexpr S32 FIELD_SIZE = 64;
        constexpr F64 FIELD_STRETCH = 1500.0;
        constexpr F32 FIELD_VIEW = 2400.f;
        constexpr F32 FIELD_CAMERA_Z = 1000.f;
        constexpr F32 FIELD_FAR = 2000.f;

        LLMatrix4a fieldView()
        {
            return view(FIELD_CAMERA_Z, false);
        }

        // The anti-repeat statistics: one wave layer over 16 x 16 repeats of a synthetic map, TILE_TEXELS a side and as
        // many pixels a repeat, so the plain read is the map texel for texel.
        constexpr S32 TILE_TEXELS = 64;
        constexpr S32 TILING_SIZE = 16 * TILE_TEXELS;

        // A triangle over the whole target, with no inputs.
        const char* kScreenVertex =
            "#version 330\n"
            "void main()\n"
            "{\n"
            "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
            "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
            "}\n";

        // The layer through the viewer's own reads, plain (tilingMode 0) or hex-tiled (1), at a repeat a tile: its
        // normal's xy, the largest of the lattice's weights there, and its z.
        const char* kTilingFragment =
            "#version 330\n"
            "uniform sampler2D tilingMap;\n"
            "uniform int tilingMode;\n"
            "uniform float tilingRepeat;\n"
            "uniform vec4 waterLatticeFrac;\n"
            "uniform ivec4 waterLatticeBase;\n"
            "out vec4 frag_color;\n"
            "void water_hex_cells(vec2 p, vec2 frac, ivec2 base, out vec2 offset[3], out vec3 weight);\n"
            "vec3 water_tiled_layer(sampler2D map_a, sampler2D map_b, vec2 uv, vec2 p, vec2 frac, ivec2 base);\n"
            "vec3 water_plain_layer(sampler2D map_a, sampler2D map_b, vec2 uv);\n"
            "void main()\n"
            "{\n"
            "    vec2 uv = gl_FragCoord.xy / tilingRepeat;\n"
            "    vec3 n = tilingMode == 0 ? water_plain_layer(tilingMap, tilingMap, uv)\n"
            "                             : water_tiled_layer(tilingMap, tilingMap, uv, uv, waterLatticeFrac.xy, waterLatticeBase.xy);\n"
            "    vec2 offset[3];\n"
            "    vec3 weight;\n"
            "    water_hex_cells(uv, waterLatticeFrac.xy, waterLatticeBase.xy, offset, weight);\n"
            "    frag_color = vec4(n.xy, max(weight.x, max(weight.y, weight.z)), n.z);\n"
            "}\n";

        // The cell of the big layer's lattice that holds most of each pixel's point of the sea: its offset and weight.
        // The pixel's point is a repeat a tile from the target's corner, and latticeFromCamera is that corner's point
        // less the camera's, in repeats, as the viewer's camera-relative points are.
        const char* kLatticeFragment =
            "#version 330\n"
            "uniform float tilingRepeat;\n"
            "uniform vec2 latticeFromCamera;\n"
            "uniform vec4 waterLatticeFrac;\n"
            "uniform ivec4 waterLatticeBase;\n"
            "out vec4 frag_color;\n"
            "void water_hex_cells(vec2 p, vec2 frac, ivec2 base, out vec2 offset[3], out vec3 weight);\n"
            "void main()\n"
            "{\n"
            "    vec2 p = gl_FragCoord.xy / tilingRepeat + latticeFromCamera;\n"
            "    vec2 offset[3];\n"
            "    vec3 weight;\n"
            "    water_hex_cells(p, waterLatticeFrac.xy, waterLatticeBase.xy, offset, weight);\n"
            "    int top = weight.x >= weight.y ? (weight.x >= weight.z ? 0 : 2) : (weight.y >= weight.z ? 1 : 2);\n"
            "    frag_color = vec4(offset[top], weight[top], 0.0);\n"
            "}\n";

        // A tiling normal map whose slopes run mostly along one oblique direction, about 27 degrees off x, with a mean
        // tilt: a height field of sines at whole cycles a repeat, near (2, 1) multiples, its normals encoded as a
        // normal map's are.
        std::vector<U8> noiseMap()
        {
            constexpr F64 TWO_PI = 6.283185307179586;
            struct Term
            {
                F64 kx;
                F64 ky;
                F64 amp;
                F64 phase;
            };
            std::vector<Term> terms;
            U32 seed = 12345;
            const auto next = [&seed]()
            {
                seed = seed * 1664525u + 1013904223u;
                return (F64)(seed >> 8) / 16777216.0;
            };
            for (S32 m = 1; m <= 4; ++m)
            {
                for (S32 e = -1; e <= 1; ++e)
                {
                    const F64 kx = 2.0 * m + e;
                    const F64 ky = (F64)m - e;
                    terms.push_back({ kx, ky, (0.5 + next()) / std::sqrt(kx * kx + ky * ky), TWO_PI * next() });
                }
            }
            std::vector<U8> rgba((size_t)TILE_TEXELS * TILE_TEXELS * 4);
            for (S32 y = 0; y < TILE_TEXELS; ++y)
            {
                for (S32 x = 0; x < TILE_TEXELS; ++x)
                {
                    F64 sx = 0.0;
                    F64 sy = 0.0;
                    for (const Term& t : terms)
                    {
                        const F64 c = t.amp * std::cos(TWO_PI * (t.kx * x + t.ky * y) / TILE_TEXELS + t.phase);
                        sx += t.kx * c;
                        sy += t.ky * c;
                    }
                    // Slopes of about 0.47 and 0.25 rms along x and y, and a tilt.
                    F64 n[3] = { -0.2 * sx + 0.15, -0.2 * sy - 0.1, 1.0 };
                    const F64 len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    U8* texel = rgba.data() + ((size_t)y * TILE_TEXELS + x) * 4;
                    for (S32 c = 0; c < 3; ++c)
                    {
                        texel[c] = (U8)llclamp((S32)std::lround((n[c] / len * 0.5 + 0.5) * 255.0), 0, 255);
                    }
                    texel[3] = 255;
                }
            }
            return rgba;
        }

        // A render of the layer's normals, xy each, with the largest lattice weight at each pixel.
        struct SlopeField
        {
            std::vector<F32> rgba;

            F32 x(S32 i, S32 j) const { return rgba[((size_t)j * TILING_SIZE + i) * 4]; }
            F32 y(S32 i, S32 j) const { return rgba[((size_t)j * TILING_SIZE + i) * 4 + 1]; }
            F32 weight(S32 i, S32 j) const { return rgba[((size_t)j * TILING_SIZE + i) * 4 + 2]; }
        };

        struct SlopeStats
        {
            F64 mean[2] = {};
            F64 var[2] = {};
            F64 cov = 0.0;

            F64 axis() const { return 0.5 * std::atan2(2.0 * cov, var[0] - var[1]); }
            F64 major() const { return 0.5 * (var[0] + var[1]) + std::sqrt(0.25 * (var[0] - var[1]) * (var[0] - var[1]) + cov * cov); }
            F64 minor() const { return 0.5 * (var[0] + var[1]) - std::sqrt(0.25 * (var[0] - var[1]) * (var[0] - var[1]) + cov * cov); }
        };

        // The field's statistics, over every pixel or over those the lattice blends, where its largest weight is under 1.
        SlopeStats slopeStats(const SlopeField& f, bool blended_only = false)
        {
            SlopeStats s;
            F64 sum[2] = {};
            F64 sq[2] = {};
            F64 xy = 0.0;
            F64 n = 0.0;
            for (S32 j = 0; j < TILING_SIZE; ++j)
            {
                for (S32 i = 0; i < TILING_SIZE; ++i)
                {
                    if (blended_only && f.weight(i, j) > 0.999f)
                    {
                        continue;
                    }
                    n += 1.0;
                    const F64 a = f.x(i, j);
                    const F64 b = f.y(i, j);
                    sum[0] += a;
                    sum[1] += b;
                    sq[0] += a * a;
                    sq[1] += b * b;
                    xy += a * b;
                }
            }
            for (S32 c = 0; c < 2; ++c)
            {
                s.mean[c] = sum[c] / n;
                s.var[c] = sq[c] / n - s.mean[c] * s.mean[c];
            }
            s.cov = xy / n - s.mean[0] * s.mean[1];
            return s;
        }

        // The correlation of the field's x slope with itself `dx`, `dy` pixels over.
        F64 correlation(const SlopeField& f, S32 dx, S32 dy)
        {
            F64 sa = 0.0;
            F64 sb = 0.0;
            F64 saa = 0.0;
            F64 sbb = 0.0;
            F64 sab = 0.0;
            F64 n = 0.0;
            for (S32 j = 0; j + dy < TILING_SIZE; ++j)
            {
                for (S32 i = 0; i + dx < TILING_SIZE; ++i)
                {
                    const F64 a = f.x(i, j);
                    const F64 b = f.x(i + dx, j + dy);
                    sa += a;
                    sb += b;
                    saa += a * a;
                    sbb += b * b;
                    sab += a * b;
                    n += 1.0;
                }
            }
            const F64 cov = sab / n - (sa / n) * (sb / n);
            const F64 va = saa / n - (sa / n) * (sa / n);
            const F64 vb = sbb / n - (sb / n) * (sb / n);
            return cov / std::sqrt(va * vb);
        }

        // [centre - FIELD_VIEW, centre + FIELD_VIEW] square onto the target, depth over [1, FIELD_FAR] metres below the
        // camera.
        LLMatrix4a fieldOrtho(F32 cx, F32 cy)
        {
            LLMatrix4a m;
            m.setRow<0>(LLVector4a(1.f / FIELD_VIEW, 0.f, 0.f, 0.f));
            m.setRow<1>(LLVector4a(0.f, 1.f / FIELD_VIEW, 0.f, 0.f));
            m.setRow<2>(LLVector4a(0.f, 0.f, -2.f / (FIELD_FAR - 1.f), 0.f));
            m.setRow<3>(LLVector4a(-cx / FIELD_VIEW, -cy / FIELD_VIEW, -(FIELD_FAR + 1.f) / (FIELD_FAR - 1.f), 1.f));
            return m;
        }

        // The compute cull's patches: the cells of the map's texels 0 to 2 each way, global cells -1 to 1, 576
        // patches, their boxes A above and below the water.
        constexpr S32 CULL_RECT[4] = { 0, 0, 3, 3 };
        constexpr S32 CULL_PATCHES = 9 * ALWaterField::PATCHES_PER_CELL * ALWaterField::PATCHES_PER_CELL;
        constexpr F32 CULL_A = 2.f;

        // The pyramid's scene: odd both ways, so every level's last texel takes an odd row and column somewhere.
        constexpr S32 PYRAMID_W = 255;
        constexpr S32 PYRAMID_H = 191;

        // The box patch boxPatch can occupy, as the cull tests it: its square's corners, waterMaxDisplacement above
        // and below its cell's water, twelve triangles from gl_VertexID.
        const char* kBoxVertex =
            "#version 400\n"
            "layout (std140) uniform Matrices\n"
            "{\n"
            "    mat4 modelview_matrix;\n"
            "    mat4 projection_matrix;\n"
            "    mat4 modelview_projection_matrix;\n"
            "    mat4 inv_proj;\n"
            "    mat4 texture_matrix0;\n"
            "    mat3 normal_matrix;\n"
            "};\n"
            "uniform float waterMaxDisplacement;\n"
            "uniform int boxPatch;\n"
            "void water_patch_corner(int instance, int vertex, out vec3 pos);\n"
            "const int CORNER[36] = int[36](0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,\n"
            "                               1, 5, 6, 1, 6, 2, 2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0);\n"
            "void main()\n"
            "{\n"
            "    vec3 c0;\n"
            "    vec3 c2;\n"
            "    water_patch_corner(boxPatch, 0, c0);\n"
            "    water_patch_corner(boxPatch, 2, c2);\n"
            "    int corner = CORNER[gl_VertexID];\n"
            "    int ring = corner & 3;\n"
            "    vec2 xy = vec2((ring == 1 || ring == 2) ? c2.x : c0.x, ring >= 2 ? c2.y : c0.y);\n"
            "    float z = c0.z + (corner >= 4 ? waterMaxDisplacement : -waterMaxDisplacement);\n"
            "    gl_Position = modelview_projection_matrix * vec4(xy, z, 1.0);\n"
            "}\n";

        const char* kBoxFragment =
            "#version 400\n"
            "out vec4 frag_color;\n"
            "void main() { frag_color = vec4(1.0); }\n";

        // The pyramid's box test alone, a box a vertex: 1 where depth_pyramid_box_visible keeps box gl_VertexID, taken
        // to clip space by probeToClip.
        const char* kProbeVertex =
            "#version 400\n"
            "uniform vec3 probeLo[4];\n"
            "uniform vec3 probeHi[4];\n"
            "uniform mat4 probeToClip;\n"
            "out float probeVisible;\n"
            "bool depth_pyramid_box_visible(vec3 lo, vec3 hi, mat4 to_clip);\n"
            "void main()\n"
            "{\n"
            "    probeVisible = depth_pyramid_box_visible(probeLo[gl_VertexID], probeHi[gl_VertexID], probeToClip) ? 1.0 : 0.0;\n"
            "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "}\n";

        // The waves' tessellation reach over the field: the patches take the 3 x 3 cells from global (0, 0), and the
        // square seven cells a side.
        constexpr F64 REACH = 280.0;

        // A depth of its own at every pixel, hashed from where it is.
        const char* kNoiseDepthFragment =
            "#version 400\n"
            "out vec4 frag_color;\n"
            "void main()\n"
            "{\n"
            "    uint h = uint(gl_FragCoord.x) * 73856093u ^ uint(gl_FragCoord.y) * 19349663u;\n"
            "    h = (h ^ (h >> 13u)) * 0x5bd1e995u;\n"
            "    h ^= h >> 15u;\n"
            "    gl_FragDepth = float(h & 0xFFFFFFu) / 16777216.0;\n"
            "    frag_color = vec4(1.0);\n"
            "}\n";

        // GL's column-major product a b.
        LLMatrix4a product(const LLMatrix4a& a, const LLMatrix4a& b)
        {
            const F32* pa = a.getF32ptr();
            const F32* pb = b.getF32ptr();
            F32 out[16];
            for (S32 c = 0; c < 4; ++c)
            {
                for (S32 r = 0; r < 4; ++r)
                {
                    F32 sum = 0.f;
                    for (S32 k = 0; k < 4; ++k)
                    {
                        sum += pa[k * 4 + r] * pb[c * 4 + k];
                    }
                    out[c * 4 + r] = sum;
                }
            }
            LLMatrix4a m;
            m.loadu(out);
            return m;
        }

        // A camera at `eye` looking at `target`, z up.
        LLMatrix4a lookAt(const LLVector3& eye, const LLVector3& target)
        {
            LLVector3 f = target - eye;
            f.normalize();
            LLVector3 s = f % LLVector3::z_axis;
            s.normalize();
            const LLVector3 u = s % f;
            const F32 m[16] = { s.mV[0], u.mV[0], -f.mV[0], 0.f,
                                s.mV[1], u.mV[1], -f.mV[1], 0.f,
                                s.mV[2], u.mV[2], -f.mV[2], 0.f,
                                -(s * eye), -(u * eye), f * eye, 1.f };
            LLMatrix4a out;
            out.loadu(m);
            return out;
        }

        // The planes of the frustum `mvp` gives but the far one, in the order and sense the water's cull takes them:
        // left, right, near, bottom, top, a point outside a plane where dot(n, p) + d > 0. The near plane is clip
        // z = -w forward and z = w under reverse-Z's zero-to-one.
        void cullPlanes(const LLMatrix4a& mvp, bool reverse, F32 (&planes)[20])
        {
            const F32* m = mvp.getF32ptr();
            const auto row = [m](S32 r, S32 c) { return m[c * 4 + r]; };
            for (S32 c = 0; c < 4; ++c)
            {
                planes[0 * 4 + c] = -(row(0, c) + row(3, c));
                planes[1 * 4 + c] = row(0, c) - row(3, c);
                planes[2 * 4 + c] = reverse ? row(2, c) - row(3, c) : -(row(2, c) + row(3, c));
                planes[3 * 4 + c] = -(row(1, c) + row(3, c));
                planes[4 * 4 + c] = row(1, c) - row(3, c);
            }
        }

    struct alwatersurface_data
    {
        // Each test starts forward, as the window does, and with identity matrices, whatever ran before.
        alwatersurface_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
            forwardZ();
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
        }

        // The convention, compute's switch, the viewport, the matrices, the shader and the clear colour the tests set
        // go back with mGL; the depth test's cache is rebased here, under the convention put back.
        ~alwatersurface_data()
        {
            if (LLRender::sReverseZ)
            {
                forwardZ();
            }
            mPyramidProgram.unload();
            mBoxes.unload();
            mNoiseDepth.unload();
            mProbe.unload();
            mFieldCapture.unload();
            mPyramid.release();
            if (mScene.isBoundInStack())
            {
                mScene.flush();
            }
            if (mSlopes.isBoundInStack())
            {
                mSlopes.flush();
            }
            LLGLSLShader::unbind();
            mColour.unload();
            mExclusion.unload();
            mField.unload();
            mTiling.unload();
            mWaves.unload();
            mShaders.clearShaderObjects();
            mCellMap = nullptr;
            mNoise = nullptr;
            mHeights = nullptr;
            mSlopes.release();
            mQueries.release();
            mScene.release();
            mBehind.release();
        }

        // The water scene in a target whose depth carries stencil, cleared: the colour program, the viewer's own
        // exclusion program, and the cell map ALWaterSurface would make for these regions.
        void setUpWater(LLRenderTarget::eDepthFormat format)
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kColourVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kColourFragment);
            {
                INFO("colour shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mColour.mProgramObject = glCreateProgram();
            mColour.mName = "colour (test)";
            mColour.attachObject(vs);
            mColour.attachObject(fs);
            UNSCOPED_INFO("colour program links");
            REQUIRE(mColour.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            UNSCOPED_INFO("colour program maps");
            REQUIRE(mColour.mapUniforms());

            std::map<std::string, std::string> defines;
            for (const char* object : { "deferred/textureUtilV.glsl", "objects/nonindexedTextureV.glsl", "environment/waterSurface.glsl" })
            {
                S32 level = 1;
                UNSCOPED_INFO(std::string("compiled ") + object);
                REQUIRE(mShaders.loadShaderFile(object, level, GL_VERTEX_SHADER, &defines) != 0);
            }
            for (const char* object : { "deferred/globalF.glsl", "environment/waterSurface.glsl", "deferred/hexLatticeUtil.glsl" })
            {
                S32 level = 1;
                UNSCOPED_INFO(std::string("compiled ") + object);
                REQUIRE(mShaders.loadShaderFile(object, level, GL_FRAGMENT_SHADER, &defines) != 0);
            }
            mExclusion.mName = "Water Exclusion Shader";
            mExclusion.mFeatures.hasWaterSurface = true;
            mExclusion.mShaderFiles.clear();
            mExclusion.mShaderFiles.push_back(std::make_pair("environment/waterExclusionV.glsl", GL_VERTEX_SHADER));
            mExclusion.mShaderFiles.push_back(std::make_pair("environment/waterExclusionF.glsl", GL_FRAGMENT_SHADER));
            mExclusion.mShaderLevel = 1;
            UNSCOPED_INFO("exclusion program builds");
            REQUIRE(mExclusion.createShader());

            // The camera over cell (1, 1), the agent region's origin at the global one.
            mLayout = ALWaterField::layout(300.0, 300.0, CELL, 256.f, 256000.0, regions(), 0.0);
            makeCellMap();

            UNSCOPED_INFO("scene allocates");
            REQUIRE(mScene.allocate(W, H, GL_RGBA8, true, true, ALTextureSlot::TT_TEXTURE,
                    LLRenderTarget::MIPS_NONE, format));
            UNSCOPED_INFO("the scene has stencil");
            REQUIRE(mScene.hasStencil());
            mScene.bindTarget();
            gGL.setClearColor(LLColor4::black);
            mScene.clear();
        }

        // The cell map ALWaterSurface would make for these regions under mLayout, and the square it covers.
        void makeCellMap()
        {
            mSquare = ALWaterField::cellSquare(mLayout, 0.0, 0.0);
            std::vector<F32> texels;
            ALWaterField::fillCells(mLayout, regions(), LOW, texels);
            mCellMap = new LLImageGL(ALWaterField::MAP_SIZE, ALWaterField::MAP_SIZE, 2, false);
            mCellMap->setNeedsAlphaAndPickMask(false);
            mCellMap->setExplicitFormat(GL_RG32F, GL_RG, GL_FLOAT);
            UNSCOPED_INFO("cell map");
            REQUIRE(mCellMap->createGLTexture(0, reinterpret_cast<const U8*>(texels.data())));
        }

        // The camera both programs draw from.
        void look(F32 z, bool up)
        {
            mCameraZ = z;
            mView = view(z, up);
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(ortho(up));
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadMatrix(mView);
        }

        void quad(F32 x0, F32 x1, F32 y0, F32 y1, F32 z)
        {
            LLGLDisable cull(GL_CULL_FACE);
            gGL.begin(LLRender::TRIANGLES);
            gGL.vertex3f(x0, y0, z);
            gGL.vertex3f(x1, y0, z);
            gGL.vertex3f(x1, y1, z);
            gGL.vertex3f(x0, y0, z);
            gGL.vertex3f(x1, y1, z);
            gGL.vertex3f(x0, y1, z);
            gGL.end();
            gGL.flush();
        }

        // Opaque scene geometry over the whole span: depth written.
        void ground(F32 z)
        {
            opaque(0.f, SPAN, 0.f, SPAN, z);
        }

        void opaque(F32 x0, F32 x1, F32 y0, F32 y1, F32 z)
        {
            LLGLDepthTest depth(GL_TRUE, GL_TRUE);
            mColour.bind();
            mColour.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, GROUND.mV);
            quad(x0, x1, y0, y1, z);
        }

        // A PASS_INVISIBLE prim's face, in the state LLDrawPoolWaterExclusion::render draws them in.
        void exclusion(F32 x0, F32 x1, F32 y0, F32 y1, F32 z)
        {
            mExclusion.bind();
            const S32 channel = mExclusion.enableTexture(LLShaderMgr::WATER_CELL_MAP);
            if (channel > -1)
            {
                gGL.getTextureSlot(channel)->bindSampled(mCellMap.get(), ALSamplers::PointClamp);
            }
            mExclusion.uniform4fv(LLShaderMgr::WATER_CELL_SQUARE, 1, mSquare.mV);
            mExclusion.uniform1f(LLShaderMgr::WATER_DEFAULT_HEIGHT, LOW);
            LLMatrix4a eye_to_agent;
            eye_to_agent.setAffineInverse(mView);
            mExclusion.uniformMatrix4fv(LLShaderMgr::INVERSE_MODELVIEW_MATRIX, eye_to_agent);
            mExclusion.uniform3f(LLShaderMgr::WL_CAMPOSLOCAL, 300.f, 300.f, mCameraZ);

            LLGLDepthTest depth(GL_TRUE, GL_FALSE);
            LLGLSColorMask no_color(false, false);
            ALWaterStencil::Exclude stencil;
            quad(x0, x1, y0, y1, z);
        }

        // The water over the four regions at their heights, in `colour`.
        void water(const LLColor4& colour)
        {
            LLGLDepthTest depth(GL_TRUE, GL_FALSE);
            mColour.bind();
            mColour.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, colour.mV);
            for (const ALWaterField::Region& cell : regions())
            {
                const F32 x0 = (F32)(cell.mCellX * CELL);
                const F32 y0 = (F32)(cell.mCellY * CELL);
                quad(x0, x0 + (F32)CELL, y0, y0 + (F32)CELL, cell.mWaterHeight);
            }
        }

        // The water as LLDrawPoolWater draws it: where no exclusion prim removed it, marking where it shades.
        void waterUnderStencil()
        {
            ALWaterStencil::Surface stencil;
            water(WATER);
        }

        // A quad of water in the pool's state, at any height: a crest where it is above the plane.
        void waterQuad(F32 x0, F32 x1, F32 y0, F32 y1, F32 z)
        {
            LLGLDepthTest depth(GL_TRUE, GL_FALSE);
            ALWaterStencil::Surface stencil;
            mColour.bind();
            mColour.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, WATER.mV);
            quad(x0, x1, y0, y1, z);
        }

        // The haze pass's draw over the whole target, depth untested, adding `colour`.
        void hazeDraw(const LLColor4& colour)
        {
            LLGLDepthTest depth(GL_FALSE);
            mColour.bind();
            mColour.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, colour.mV);
            quad(0.f, SPAN, 0.f, SPAN, 50.f);
        }

        LLColor4U pixel(S32 x, S32 y)
        {
            U8 rgba[4] = {};
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            return LLColor4U(rgba[0], rgba[1], rgba[2], rgba[3]);
        }

        bool wet(S32 x, S32 y)
        {
            const LLColor4U c = pixel(x, y);
            return c.mV[VBLUE] == 255 && c.mV[VRED] == 0;
        }

        U8 stencil(S32 x, S32 y)
        {
            U8 value = 0;
            glReadPixels(x, y, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &value);
            return value;
        }

        static std::vector<LLRenderTarget::eDepthFormat> formats()
        {
            return { LLRenderTarget::DEPTH_FMT_24, LLRenderTarget::DEPTH_FMT_32F };
        }

        static std::string named(LLRenderTarget::eDepthFormat format, const std::string& what)
        {
            return what + (format == LLRenderTarget::DEPTH_FMT_32F ? " (32F)" : " (24)");
        }

        // The field program over the cell map, in a FIELD_SIZE target with depth and stencil, cleared, and the
        // camera above the square's centre; with a tessellation reach, the waves' patches laid out as well.
        void setUpField(F64 reach = 0.0)
        {
            setUpWater(LLRenderTarget::DEPTH_FMT_24);
            mScene.flush();
            mScene.release();

            mField.mName = "Water Field Shader";
            mField.mFeatures.hasWaterSurface = true;
            mField.mShaderFiles.clear();
            mField.mShaderFiles.push_back(std::make_pair("environment/waterFieldV.glsl", GL_VERTEX_SHADER));
            mField.mShaderFiles.push_back(std::make_pair("environment/waterFieldF.glsl", GL_FRAGMENT_SHADER));
            mField.mShaderLevel = 1;
            UNSCOPED_INFO("field program builds");
            REQUIRE(mField.createShader());
            UNSCOPED_INFO("the field program has no vertex inputs");
            REQUIRE(mField.mAttributeMask == 0U);

            mLayout = ALWaterField::layout(300.0, 300.0, CELL, 256.f, FIELD_STRETCH, regions(), reach);
            makeCellMap();
            UNSCOPED_INFO("the stretch takes two rings");
            REQUIRE(mLayout.mRings == 2);

            UNSCOPED_INFO("field target");
            REQUIRE(mScene.allocate(FIELD_SIZE, FIELD_SIZE, GL_RGBA8, true, true, ALTextureSlot::TT_TEXTURE,
                    LLRenderTarget::MIPS_NONE, LLRenderTarget::DEPTH_FMT_24));
            mScene.bindTarget();
            gGL.setClearColor(LLColor4::black);
            mScene.clear();

            mCameraZ = FIELD_CAMERA_Z;
            mView = fieldView();
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(fieldOrtho((F32)mLayout.centreX(), (F32)mLayout.centreY()));
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadMatrix(mView);
        }

        // `program` bound for the field, as ALWaterSurface::bind binds it: the void drawn or not, raised by `raise`
        // metres, and the cells beside `patches` fanned onto their corners.
        void bindField(LLGLSLShader& program, bool void_drawn, F32 raise, const ALWaterField::PatchRect& patches)
        {
            program.bind();
            program.uniform1f(LLShaderMgr::WATER_RAISE, raise);
            const S32 channel = program.enableTexture(LLShaderMgr::WATER_CELL_MAP);
            if (channel > -1)
            {
                gGL.getTextureSlot(channel)->bindSampled(mCellMap.get(), ALSamplers::PointClamp);
            }
            program.uniform4fv(LLShaderMgr::WATER_CELL_SQUARE, 1, mSquare.mV);
            program.uniform1f(LLShaderMgr::WATER_DEFAULT_HEIGHT, LOW);
            program.uniform4f(LLShaderMgr::WATER_RINGS, (F32)mLayout.mHalf, (F32)mLayout.mRings, (F32)mLayout.mRim,
                              void_drawn ? 1.f : 0.f);
            const S32 rect[4] = { patches.mMinX, patches.mMinY, patches.mEndX, patches.mEndY };
            program.uniform4iv(LLShaderMgr::WATER_PATCH_RECT, 1, rect);
        }

        // The field's draws through `program`, bound by bindField, as ALWaterSurface::draw draws them.
        void drawFieldParts(LLGLSLShader& program, const ALWaterField::PatchRect& patches)
        {
            LLGLDisable cull(GL_CULL_FACE);
            for (const ALWaterField::Draw& draw : ALWaterField::fieldDraws(mLayout, patches))
            {
                program.uniform1i(LLShaderMgr::WATER_LAYOUT, (S32)draw.mPart);
                gGL.drawArraysInstanced(draw.mStrip ? LLRender::TRIANGLE_STRIP : LLRender::TRIANGLES, 0, (U32)draw.mVertices,
                                        (U32)draw.mInstances);
            }
        }

        // The field, as ALWaterSurface::bind and ::draw draw it.
        void drawField(bool void_drawn, F32 raise = 0.f, const ALWaterField::PatchRect& patches = {})
        {
            bindField(mField, void_drawn, raise, patches);
            drawFieldParts(mField, patches);
        }

        // Whether the sky behind's draw over mQuery, ended, runs: mBehind cleared, then cleared red under the query's
        // condition, as LLDrawPoolWLSky draws the sky behind under it, and read back.
        bool conditionalClearRan()
        {
            mBehind.bindTarget();
            gGL.setClearColor(LLColor4::black);
            mBehind.clear();
            mBehind.flush();
            {
                ALConditionalRender if_water(mQueries, mQuery);
                mBehind.bindTarget();
                gGL.setClearColor(LLColor4::red);
                mBehind.clear();
                gGL.setClearColor(LLColor4::black);
                mBehind.flush();
            }
            mBehind.bindTarget();
            const LLColor4U c = pixel(1, 1);
            mBehind.flush();
            return c.mV[VRED] == 255;
        }

        // Where pixel (x, y) of the field target lies, from the square's centre, in metres.
        F32 fieldX(S32 x) const
        {
            return ((F32)x + 0.5f) / (F32)FIELD_SIZE * 2.f * FIELD_VIEW - FIELD_VIEW;
        }

        // The height the depth buffer holds at a pixel of the field target.
        F32 heightAt(S32 x, S32 y)
        {
            F32 depth = 0.f;
            glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
            const F32 ndc = depth * 2.f - 1.f;
            const F32 eye_z = -(ndc + (FIELD_FAR + 1.f) / (FIELD_FAR - 1.f)) * (FIELD_FAR - 1.f) / 2.f;
            return eye_z + FIELD_CAMERA_Z;
        }

        // Reverse-Z as the viewer runs it: zero-to-one clip control, depth cleared to 0, nearer is greater. Shaders
        // compiled after this take REVERSE_Z.
        void reverseZ()
        {
            LLRender::sReverseZ = true;
            glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
            glClearDepth(0.0);
            LLGLDepthTest::rebase();
        }

        void forwardZ()
        {
            LLRender::sReverseZ = false;
            if (gGLManager.mHasClipControl)
            {
                glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
            }
            glClearDepth(1.0);
            LLGLDepthTest::rebase();
        }

        // The depth pyramid's program, built as the viewer builds it.
        void setUpPyramid()
        {
            mPyramidProgram.mName = "Depth Pyramid Shader";
            mPyramidProgram.mShaderFiles.clear();
            mPyramidProgram.mShaderFiles.push_back(std::make_pair("deferred/depthPyramidC.glsl", GL_COMPUTE_SHADER));
            mPyramidProgram.mShaderLevel = 1;
            UNSCOPED_INFO("pyramid program builds");
            REQUIRE(mPyramidProgram.createShader());
            UNSCOPED_INFO("the pyramid is built by compute");
            REQUIRE(mPyramidProgram.isCompute());
        }

        // The field's program as the viewer links it, capturing each vertex's clip position: agent space, under
        // identity matrices.
        void setUpFieldCapture()
        {
            std::map<std::string, std::string> defines;
            mFieldCapture.mProgramObject = glCreateProgram();
            mFieldCapture.mName = "field capture (test)";
            for (const char* object : { "environment/waterFieldV.glsl", "environment/waterSurface.glsl" })
            {
                S32 level = 1;
                const GLuint handle = mShaders.loadShaderFile(object, level, GL_VERTEX_SHADER, &defines);
                UNSCOPED_INFO(std::string("compiled ") + object);
                REQUIRE(handle != 0);
                mFieldCapture.attachObject(handle);
            }
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, "#version 400\nout vec4 c;\nvoid main() { c = vec4(1.0); }\n");
            UNSCOPED_INFO("fragment compiles");
            REQUIRE(fs != 0);
            mFieldCapture.attachObject(fs);
            const char* varyings[] = { "gl_Position" };
            glTransformFeedbackVaryings(mFieldCapture.mProgramObject, 1, varyings, GL_INTERLEAVED_ATTRIBS);
            UNSCOPED_INFO("field capture program links");
            REQUIRE(mFieldCapture.mapAttributes());
            glDeleteShader(fs);
            UNSCOPED_INFO("field capture program maps");
            REQUIRE(mFieldCapture.mapUniforms());
        }

        // The pyramid's box test alone (kProbeVertex), each box's answer captured.
        void setUpProbe()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kProbeVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kBoxFragment);
            {
                INFO("probe shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            std::map<std::string, std::string> defines;
            S32 level = 1;
            const GLuint util = mShaders.loadShaderFile("deferred/depthPyramidUtil.glsl", level, GL_VERTEX_SHADER, &defines);
            UNSCOPED_INFO("the pyramid's box test compiles");
            REQUIRE(util != 0);
            mProbe.mProgramObject = glCreateProgram();
            mProbe.mName = "pyramid probe (test)";
            mProbe.attachObject(vs);
            mProbe.attachObject(fs);
            mProbe.attachObject(util);
            const char* varyings[] = { "probeVisible" };
            glTransformFeedbackVaryings(mProbe.mProgramObject, 1, varyings, GL_INTERLEAVED_ATTRIBS);
            UNSCOPED_INFO("probe program links");
            REQUIRE(mProbe.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            UNSCOPED_INFO("probe program maps");
            REQUIRE(mProbe.mapUniforms());
        }

        // Whether depth_pyramid_box_visible keeps each of up to four `boxes`, lo and hi, against mPyramid, taken to clip
        // space by `to_clip`.
        std::vector<bool> probe(const std::vector<std::pair<LLVector3, LLVector3>>& boxes, const LLMatrix4a& to_clip)
        {
            {
                INFO("a probe takes up to four boxes");
                REQUIRE(!boxes.empty());
                REQUIRE(boxes.size() <= 4);
            }
            F32 lo[12] = {};
            F32 hi[12] = {};
            for (size_t i = 0; i < boxes.size(); ++i)
            {
                for (S32 c = 0; c < 3; ++c)
                {
                    lo[i * 3 + c] = boxes[i].first.mV[c];
                    hi[i * 3 + c] = boxes[i].second.mV[c];
                }
            }
            mProbe.bind();
            mPyramid.bind(mProbe);
            glUniform3fv(glGetUniformLocation(mProbe.mProgramObject, "probeLo"), (GLsizei)boxes.size(), lo);
            glUniform3fv(glGetUniformLocation(mProbe.mProgramObject, "probeHi"), (GLsizei)boxes.size(), hi);
            glUniformMatrix4fv(glGetUniformLocation(mProbe.mProgramObject, "probeToClip"), 1, GL_FALSE, to_clip.getF32ptr());
            const std::vector<F32> answers = captureFeedback(GL_POINTS, 1, [&]()
                                                             { gGL.drawArraysInstanced(LLRender::POINTS, 0, (U32)boxes.size(), 1); });
            LLGLSLShader::unbind();
            UNSCOPED_INFO("each box answered");
            REQUIRE(answers.size() == boxes.size());
            std::vector<bool> kept;
            for (F32 answer : answers)
            {
                kept.push_back(answer > 0.5f);
            }
            return kept;
        }

        // The waves' program as the viewer links it for the patches, its wave layers hex-tiled as `hextile` says, and
        // capturing the evaluation stage's `captured`: by default its points less the camera (`view`).
        void setUpWavesCapture(const char* captured = "view", S32 hextile = 1)
        {
            std::map<std::string, std::string> defines = { { "WATER_HEXTILE", std::to_string(hextile) } };
            const std::pair<const char*, GLenum> objects[] = {
                { "environment/waterPatchV.glsl", GL_VERTEX_SHADER },
                { "environment/waterSurface.glsl", GL_VERTEX_SHADER },
                { "environment/waterTC.glsl", GL_TESS_CONTROL_SHADER },
                { "deferred/depthPyramidUtil.glsl", GL_TESS_CONTROL_SHADER },
                { "environment/waterTE.glsl", GL_TESS_EVALUATION_SHADER },
                { "environment/waterVaryingsV.glsl", GL_TESS_EVALUATION_SHADER },
                { "environment/waterSurface.glsl", GL_TESS_EVALUATION_SHADER },
                { "deferred/hexLatticeUtil.glsl", GL_TESS_EVALUATION_SHADER },
            };
            mWaves.mProgramObject = glCreateProgram();
            mWaves.mName = "waves (test)";
            for (const auto& object : objects)
            {
                S32 level = 1;
                const GLuint handle = mShaders.loadShaderFile(object.first, level, object.second, &defines);
                UNSCOPED_INFO(std::string("compiled ") + object.first);
                REQUIRE(handle != 0);
                mWaves.attachObject(handle);
            }
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, "#version 400\nout vec4 c;\nvoid main() { c = vec4(1.0); }\n");
            UNSCOPED_INFO("fragment compiles");
            REQUIRE(fs != 0);
            mWaves.attachObject(fs);
            const char* varyings[] = { captured };
            glTransformFeedbackVaryings(mWaves.mProgramObject, 1, varyings, GL_INTERLEAVED_ATTRIBS);
            UNSCOPED_INFO("waves program links");
            REQUIRE(mWaves.mapAttributes());
            glDeleteShader(fs);
            UNSCOPED_INFO("waves program maps");
            REQUIRE(mWaves.mapUniforms());
        }

        // The triangles `draw` emits through the bound program, captured from its one feedback varying of `components`
        // floats: three vertices a triangle, `components` floats each, in the order drawn. Nothing is rasterized.
        std::vector<F32> captureTriangles(S32 components, const std::function<void()>& draw)
        {
            return captureFeedback(GL_TRIANGLES, 3 * (size_t)components, draw);
        }

        // The `primitive`s `draw` emits through the bound program, `floats` of its feedback varyings each.
        std::vector<F32> captureFeedback(GLenum primitive, size_t floats, const std::function<void()>& draw)
        {
            GLuint buffer = 0;
            glGenBuffers(1, &buffer);
            glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer);
            glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, 32 << 20, nullptr, GL_DYNAMIC_READ);
            glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer);
            GLuint query = 0;
            glGenQueries(1, &query);
            glEnable(GL_RASTERIZER_DISCARD);
            glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query);
            glBeginTransformFeedback(primitive);
            draw();
            glEndTransformFeedback();
            glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
            glDisable(GL_RASTERIZER_DISCARD);
            GLuint primitives = 0;
            glGetQueryObjectuiv(query, GL_QUERY_RESULT, &primitives);
            std::vector<F32> points((size_t)primitives * floats);
            if (primitives > 0)
            {
                glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, points.size() * sizeof(F32), points.data());
            }
            glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
            glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);
            glDeleteBuffers(1, &buffer);
            glDeleteQueries(1, &query);
            return points;
        }

        // What the waves' draw is given, as ALWaterSurface::bindWaves and the pool give it: the patches of mRect in
        // mOrder, the void's drawn where mVoidDrawn, from mEye, the camera the waves were laid out around as well, at
        // mAmp over mHeights (none, flat), with the reach, A and density, against mPlanes, by default every point inside
        // each, and with mPyramid against the depth it was built from.
        struct WaveDraw
        {
            S32             mRect[4] = { 0, 0, 0, 0 };
            S32             mOrder[4] = { 0, 0, 0, 0 };
            bool            mVoidDrawn = false;
            LLVector3       mEye;
            F32             mAmp = 0.f;
            LLImageGL*      mHeights = nullptr;
            F32             mReach = 100000.f;
            F32             mA = 0.f;
            F32             mDensity = 8.f;
            F32             mPlanes[20] = { 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, -1.f,
                                            0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, -1.f };
            ALDepthPyramid* mPyramid = nullptr;

            S32 patches() const
            {
                return (mRect[2] - mRect[0]) * (mRect[3] - mRect[1]) * ALWaterField::PATCHES_PER_CELL * ALWaterField::PATCHES_PER_CELL;
            }
        };

        // The waves' program bound with `draw`'s uniforms, the wave phases and the lattice at the camera's place.
        void bindWaves(const WaveDraw& draw)
        {
            mWaves.bind();
            S32 channel = mWaves.enableTexture(LLShaderMgr::WATER_CELL_MAP);
            UNSCOPED_INFO("the patches read the cell map");
            REQUIRE(channel > -1);
            gGL.getTextureSlot(channel)->bindSampled(mCellMap.get(), ALSamplers::PointClamp);
            if (draw.mHeights)
            {
                channel = mWaves.enableTexture(LLShaderMgr::WATER_HEIGHT_MAP);
                UNSCOPED_INFO("the patches read the heights");
                REQUIRE(channel > -1);
                gGL.getTextureSlot(channel)->bindSampled(draw.mHeights, ALSamplers::BilinearWrap);
            }
            mWaves.uniform4fv(LLShaderMgr::WATER_CELL_SQUARE, 1, mSquare.mV);
            mWaves.uniform1f(LLShaderMgr::WATER_DEFAULT_HEIGHT, LOW);
            mWaves.uniform4f(LLShaderMgr::WATER_RINGS, (F32)mLayout.mHalf, (F32)mLayout.mRings, (F32)mLayout.mRim,
                             draw.mVoidDrawn ? 1.f : 0.f);
            mWaves.uniform4iv(LLShaderMgr::WATER_PATCH_RECT, 1, draw.mRect);
            mWaves.uniform4iv(LLShaderMgr::WATER_PATCH_ORDER, 1, draw.mOrder);
            mWaves.uniform2f(LLShaderMgr::WATER_FAR, 100000.f, 0.f);
            mWaves.uniform3fv(LLShaderMgr::WATER_EYEVEC, 1, draw.mEye.mV);
            mWaves.uniform3fv(LLShaderMgr::WATER_WAVE_EYE, 1, draw.mEye.mV);
            mWaves.uniform2f(LLShaderMgr::WATER_BIG_PHASE, 0.f, 0.f);
            mWaves.uniform4f(LLShaderMgr::WATER_LITTLE_PHASE, 0.f, 0.f, 0.f, 0.f);
            mWaves.uniform2f(LLShaderMgr::WATER_SWEEP_PHASE, 0.f, 0.f);
            const ALWaterField::LatticePlaces places = ALWaterField::latticePlaces(draw.mEye.mV[VX], draw.mEye.mV[VY]);
            mWaves.uniform4fv(LLShaderMgr::WATER_LATTICE_FRAC, 1, places.mFrac.mV);
            mWaves.uniform4iv(LLShaderMgr::WATER_LATTICE_BASE, 1, places.mBase);
            mWaves.uniform1f(LLShaderMgr::BLEND_FACTOR, 0.f);
            mWaves.uniform2f(LLShaderMgr::WATER_WAVE_AMP, draw.mAmp, 0.f);
            mWaves.uniform2f(LLShaderMgr::WATER_WAVE_REACH, draw.mReach, draw.mA);
            mWaves.uniform1f(LLShaderMgr::WATER_TESS_DENSITY, draw.mDensity);
            mWaves.uniform1f(LLShaderMgr::WATER_MAX_DISPLACEMENT, draw.mA);
            mWaves.uniform4fv(LLShaderMgr::WATER_CULL_PLANES, 5, draw.mPlanes);
            mWaves.uniform1i(LLShaderMgr::WATER_OCCLUSION, draw.mPyramid ? 1 : 0);
            if (draw.mPyramid)
            {
                draw.mPyramid->bind(mWaves);
            }
        }

        // Every patch of `draw`'s rectangle, through the bound waves' program.
        void drawPatches(const WaveDraw& draw)
        {
            gGL.setPatchVertices(4);
            gGL.drawArraysInstanced(LLRender::PATCHES, 0, 4, (U32)draw.patches());
            gGL.setPatchVertices(3);
        }

        // The triangles the evaluation stage emits for `draw`, through the matrices loaded: three vec3s of the varying
        // the program captures each.
        std::vector<F32> tessellate(const WaveDraw& draw)
        {
            bindWaves(draw);
            std::vector<F32> points = captureTriangles(3, [&]() { drawPatches(draw); });
            LLGLSLShader::unbind();
            return points;
        }

        // The patches of CULL_RECT, flat, from `eye`, against `planes`, in `order` (waterPatchOrder), and with
        // `pyramid` against the depth it was built from, as WaveDraw names them.
        WaveDraw cullDraw(const F32 (&planes)[20], const LLVector3& eye, ALDepthPyramid* pyramid, const S32 (&order)[4])
        {
            WaveDraw draw;
            std::copy(std::begin(CULL_RECT), std::end(CULL_RECT), draw.mRect);
            std::copy(std::begin(order), std::end(order), draw.mOrder);
            std::copy(std::begin(planes), std::end(planes), draw.mPlanes);
            draw.mEye = eye;
            draw.mA = CULL_A;
            draw.mPyramid = pyramid;
            return draw;
        }

        // The triangles the evaluation stage emits for the patches of CULL_RECT, flat, from `eye`, against `planes`, in
        // `order` (waterPatchOrder), and with `pyramid` against the depth it was built from, through the matrices
        // loaded: each one's centroid, in the order drawn.
        std::vector<LLVector2> tessellatedCentroids(const F32 (&planes)[20], const LLVector3& eye, ALDepthPyramid* pyramid,
                                                    const S32 (&order)[4])
        {
            const std::vector<F32> points = tessellate(cullDraw(planes, eye, pyramid, order));
            std::vector<LLVector2> centroids;
            centroids.reserve(points.size() / 9);
            for (size_t t = 0; t + 9 <= points.size(); t += 9)
            {
                const F32* tri = points.data() + t;
                centroids.emplace_back((tri[0] + tri[3] + tri[6]) / 3.f + eye.mV[VX], (tri[1] + tri[4] + tri[7]) / 3.f + eye.mV[VY]);
            }
            return centroids;
        }

        // The patches of CULL_RECT the control stage tessellates, named as water_patch_corner names them in its first
        // order, rows of x from the south-west: patch (i, j) of the rectangle is i + j * the patches across. Each
        // triangle lies inside its patch, which its centroid names.
        std::set<S32> tessellated(const F32 (&planes)[20], const LLVector3& eye, ALDepthPyramid* pyramid)
        {
            constexpr S32 FIRST_ORDER[4] = { 0, 0, 0, 0 };
            const S32 across = (CULL_RECT[2] - CULL_RECT[0]) * ALWaterField::PATCHES_PER_CELL;
            const F32 width = mSquare.mV[VZ] / (F32)ALWaterField::PATCHES_PER_CELL;
            const F32 x0 = mSquare.mV[VX] + (F32)CULL_RECT[0] * mSquare.mV[VZ];
            const F32 y0 = mSquare.mV[VY] + (F32)CULL_RECT[1] * mSquare.mV[VZ];
            std::set<S32> drawn;
            for (const LLVector2& c : tessellatedCentroids(planes, eye, pyramid, FIRST_ORDER))
            {
                const S32 i = (S32)floorf((c.mV[VX] - x0) / width);
                const S32 j = (S32)floorf((c.mV[VY] - y0) / width);
                drawn.insert(i + j * across);
            }
            return drawn;
        }

        // The boxes' program: the box each patch can occupy, one a draw, over the field's cells.
        void setUpBoxes()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kBoxVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kBoxFragment);
            {
                INFO("box shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mBoxes.mProgramObject = glCreateProgram();
            mBoxes.mName = "patch boxes (test)";
            mBoxes.attachObject(vs);
            mBoxes.attachObject(fs);
            UNSCOPED_INFO("the boxes place patches");
            REQUIRE(mBoxes.attachStageObject(GL_VERTEX_SHADER, "environment/waterSurface.glsl"));
            UNSCOPED_INFO("box program links");
            REQUIRE(mBoxes.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            UNSCOPED_INFO("box program maps");
            REQUIRE(mBoxes.mapUniforms());
        }

        // The patches of CULL_RECT any of whose box passes the depth test against the bound scene's depth: what a cull
        // that never hides a patch in view keeps, at least.
        std::set<S32> visibleBoxes()
        {
            mBoxes.bind();
            const S32 channel = mBoxes.enableTexture(LLShaderMgr::WATER_CELL_MAP);
            UNSCOPED_INFO("the boxes read the cell map");
            REQUIRE(channel > -1);
            gGL.getTextureSlot(channel)->bindSampled(mCellMap.get(), ALSamplers::PointClamp);
            mBoxes.uniform4fv(LLShaderMgr::WATER_CELL_SQUARE, 1, mSquare.mV);
            mBoxes.uniform4iv(LLShaderMgr::WATER_PATCH_RECT, 1, CULL_RECT);
            mBoxes.uniform1f(LLShaderMgr::WATER_MAX_DISPLACEMENT, CULL_A);
            const GLint at = glGetUniformLocation(mBoxes.mProgramObject, "boxPatch");
            UNSCOPED_INFO("the boxes take a patch");
            REQUIRE(at > -1);

            std::set<S32> visible;
            GLuint query = 0;
            glGenQueries(1, &query);
            {
                LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_LEQUAL);
                LLGLSColorMask no_color(false, false);
                LLGLDisable cull(GL_CULL_FACE);
                for (S32 i = 0; i < CULL_PATCHES; ++i)
                {
                    glUniform1i(at, i);
                    glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
                    gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 36, 1);
                    glEndQuery(GL_ANY_SAMPLES_PASSED);
                    GLuint passed = 0;
                    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &passed);
                    if (passed)
                    {
                        visible.insert(i);
                    }
                }
            }
            glDeleteQueries(1, &query);
            LLGLSLShader::unbind();
            return visible;
        }

        // Every level of the pyramid read back against the same reduction worked here from the bound scene's depth:
        // level 0 two by two of the depth's pixels, clamped to its edge, over the depth halved, rounded up and then up
        // to a whole 2^(LEVELS - 1); each level after two by two of the one before. Exactly, or within `tolerance`
        // where the depth is fixed point and a sampler may convert it to float otherwise than a read does.
        void checkPyramid(const std::string& what, F32 tolerance = 0.f)
        {
            const S32 w = (S32)mScene.getWidth();
            const S32 h = (S32)mScene.getHeight();
            std::vector<F32> depth((size_t)w * h);
            glReadPixels(0, 0, w, h, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
            // The build's last barrier is for the pyramid's readers' fetches; a read back is a texture update.
            gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_UPDATE);
            const auto farthest = [](F32 a, F32 b) { return LLRender::sReverseZ ? llmin(a, b) : llmax(a, b); };

            constexpr S32 ALIGN = 1 << (ALDepthPyramid::LEVELS - 1);
            S32 lw = ((w + 1) / 2 + ALIGN - 1) / ALIGN * ALIGN;
            S32 lh = ((h + 1) / 2 + ALIGN - 1) / ALIGN * ALIGN;
            UNSCOPED_INFO(what + ": the levels built");
            REQUIRE(mPyramid.getLevels() == ALDepthPyramid::LEVELS);
            UNSCOPED_INFO(what + ": level 0's width");
            REQUIRE((S32)mPyramid.getTarget().getWidth() == lw);
            UNSCOPED_INFO(what + ": level 0's height");
            REQUIRE((S32)mPyramid.getTarget().getHeight() == lh);

            std::vector<F32> expected((size_t)lw * lh);
            for (S32 y = 0; y < lh; ++y)
            {
                for (S32 x = 0; x < lw; ++x)
                {
                    F32 d = depth[(size_t)llmin(2 * y, h - 1) * w + llmin(2 * x, w - 1)];
                    for (S32 j = 0; j < 2; ++j)
                    {
                        for (S32 i = 0; i < 2; ++i)
                        {
                            d = farthest(d, depth[(size_t)llmin(2 * y + j, h - 1) * w + llmin(2 * x + i, w - 1)]);
                        }
                    }
                    expected[(size_t)y * lw + x] = d;
                }
            }

            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mPyramid.getTarget().getTexture());
            for (U32 level = 0; level < ALDepthPyramid::LEVELS; ++level)
            {
                if (level > 0)
                {
                    const S32 pw = lw;
                    lw /= 2;
                    lh /= 2;
                    std::vector<F32> coarser((size_t)lw * lh);
                    for (S32 y = 0; y < lh; ++y)
                    {
                        for (S32 x = 0; x < lw; ++x)
                        {
                            const F32* row0 = expected.data() + (size_t)(2 * y) * pw + 2 * x;
                            const F32* row1 = row0 + pw;
                            coarser[(size_t)y * lw + x] = farthest(farthest(row0[0], row0[1]), farthest(row1[0], row1[1]));
                        }
                    }
                    expected.swap(coarser);
                }
                std::vector<F32> got((size_t)lw * lh);
                glGetTexImage(GL_TEXTURE_2D, (GLint)level, GL_RED, GL_FLOAT, got.data());
                F32 off = 0.f;
                for (size_t n = 0; n < got.size(); ++n)
                {
                    // A NaN read back is as far off as can be.
                    const F32 d = fabsf(got[n] - expected[n]);
                    off = std::isnan(d) ? FLT_MAX : llmax(off, d);
                }
                UNSCOPED_INFO(what + ": level " + std::to_string(level) + " holds the farthest depth under each texel, off by "
                              + std::to_string(off));
                REQUIRE(off <= tolerance);
            }
            gGL.getTextureSlot(0)->unbind();
        }

        // A depth of its own at every pixel of the bound target, whatever was there.
        void noiseDepth()
        {
            if (!mNoiseDepth.isComplete())
            {
                const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kScreenVertex);
                const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kNoiseDepthFragment);
                {
                    INFO("noise shaders compile");
                    REQUIRE(vs);
                    REQUIRE(fs);
                }
                mNoiseDepth.mProgramObject = glCreateProgram();
                mNoiseDepth.mName = "noise depth (test)";
                mNoiseDepth.attachObject(vs);
                mNoiseDepth.attachObject(fs);
                UNSCOPED_INFO("noise program links");
                REQUIRE(mNoiseDepth.mapAttributes());
                glDeleteShader(vs);
                glDeleteShader(fs);
                UNSCOPED_INFO("noise program maps");
                REQUIRE(mNoiseDepth.mapUniforms());
            }
            LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_ALWAYS);
            mNoiseDepth.bind();
            gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
            LLGLSLShader::unbind();
        }

        // An opaque wall standing in the plane y = `y`, over x in [x0, x1] and z in [z0, z1].
        void wall(F32 x0, F32 x1, F32 y, F32 z0, F32 z1)
        {
            LLGLDepthTest depth(GL_TRUE, GL_TRUE);
            LLGLDisable cull(GL_CULL_FACE);
            mColour.bind();
            mColour.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, GROUND.mV);
            gGL.begin(LLRender::TRIANGLES);
            gGL.vertex3f(x0, y, z0);
            gGL.vertex3f(x1, y, z0);
            gGL.vertex3f(x1, y, z1);
            gGL.vertex3f(x0, y, z0);
            gGL.vertex3f(x1, y, z1);
            gGL.vertex3f(x0, y, z1);
            gGL.end();
            gGL.flush();
        }

        void tearDownWater()
        {
            mScene.flush();
            LLGLSLShader::unbind();
            mColour.unload();
            mExclusion.unload();
            mShaders.clearShaderObjects();
            mCellMap = nullptr;
            mScene.release();
        }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;

        LLGLSLShader           mColour;
        LLGLSLShader           mExclusion;
        LLGLSLShader           mField;
        ALQueryPool            mQueries;
        ALQueryTicket          mQuery;
        LLRenderTarget         mScene;
        LLRenderTarget         mBehind;
        LLPointer<LLImageGL>   mCellMap;
        ALWaterField::Layout   mLayout;
        LLVector4              mSquare;
        LLMatrix4a             mView;
        F32                    mCameraZ = 0.f;

        LLGLSLShader           mTiling;
        LLPointer<LLImageGL>   mNoise;
        LLRenderTarget         mSlopes;

        LLGLSLShader           mWaves;
        LLPointer<LLImageGL>   mHeights;

        LLGLSLShader           mPyramidProgram;
        LLGLSLShader           mBoxes;
        LLGLSLShader           mNoiseDepth;
        LLGLSLShader           mProbe;
        ALDepthPyramid         mPyramid;

        LLGLSLShader           mFieldCapture;
    };


    // From above, an exclusion prim over the water removes the water behind it; one under the water removes nothing,
    // since the water hides it.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: from above, an exclusion prim over the water removes the water behind it", "[alwatersurface_gl]")
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            exclusion(0.f, 256.f, 0.f, SPAN, LOW + 5.f);
            exclusion(256.f, SPAN, 256.f, SPAN, LOW - 5.f);
            waterUnderStencil();
            for (S32 y : { 2, 12 })
            {
                UNSCOPED_INFO(named(format, "a prim above the water removes it, row " + std::to_string(y)));
                REQUIRE(!wet(4, y));
            }
            UNSCOPED_INFO(named(format, "a prim under the water removes nothing"));
            REQUIRE(wet(12, 12));
            UNSCOPED_INFO(named(format, "nor does nothing"));
            REQUIRE(wet(12, 4));
            tearDownWater();
        }
    }

    // From below, the reverse: the prim under the water, on the camera's side, removes it; the one above it does not.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: from below, a prim under the water removes it and one above does not", "[alwatersurface_gl]")
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(5.f, true);
            ground(60.f);
            exclusion(0.f, 256.f, 0.f, SPAN, LOW + 5.f);
            exclusion(256.f, SPAN, 0.f, SPAN, LOW - 5.f);
            waterUnderStencil();
            for (S32 y : { 2, 12 })
            {
                UNSCOPED_INFO(named(format, "a prim above the water from below removes nothing, row " + std::to_string(y)));
                REQUIRE(wet(4, y));
                UNSCOPED_INFO(named(format, "a prim under it does, row " + std::to_string(y)));
                REQUIRE(!wet(12, y));
            }
            tearDownWater();
        }
    }

    // A prim behind opaque geometry marks nothing: the pass tests the scene's depth.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: a prim behind opaque geometry marks nothing", "[alwatersurface_gl]")
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            opaque(0.f, 256.f, 0.f, SPAN, 50.f);
            exclusion(0.f, SPAN, 0.f, SPAN, HIGH + 5.f);
            for (S32 y : { 2, 12 })
            {
                UNSCOPED_INFO(named(format, "behind the wall, row " + std::to_string(y)));
                REQUIRE((U32)stencil(4, y) == 0U);
                UNSCOPED_INFO(named(format, "in the open, row " + std::to_string(y)));
                REQUIRE((U32)stencil(12, y) == ALWaterStencil::EXCLUDE_BIT);
            }
            tearDownWater();
        }
    }

    // A prim over two regions whose water lies at different heights tests each part against its own: at 25 m it is
    // above the west region's 20 m water and under the east one's 30 m.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: a prim over two regions tests each part against its own region's water", "[alwatersurface_gl]")
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            exclusion(0.f, SPAN, 0.f, 256.f, 25.f);
            waterUnderStencil();
            UNSCOPED_INFO(named(format, "over the 20 m water it removes the water"));
            REQUIRE(!wet(4, 4));
            UNSCOPED_INFO(named(format, "under the 30 m water it removes nothing"));
            REQUIRE(wet(12, 4));
            {
                INFO(named(format, "where it is not, the water is"));
                REQUIRE(wet(4, 12));
                REQUIRE(wet(12, 12));
            }
            tearDownWater();
        }
    }

    // From below, the surface is drawn twice, once where the exclude bit is clear and once where it is set, and each
    // pixel takes exactly one of them.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: from below, each pixel takes exactly one of the surface's two draws", "[alwatersurface_gl]")
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(5.f, true);
            ground(60.f);
            exclusion(256.f, SPAN, 0.f, SPAN, LOW - 5.f);
            {
                LLGLEnable blend(GL_BLEND);
                gGL.blendFunc(LLRender::BF_ONE, LLRender::BF_ONE);
                {
                    ALWaterStencil::Surface state;
                    water(LLColor4(1.f, 0.f, 0.f, 0.f));
                }
                {
                    ALWaterStencil::Excluded state;
                    water(LLColor4(0.f, 1.f, 0.f, 0.f));
                }
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
            }
            for (S32 x = 0; x < W; ++x)
            {
                const LLColor4U c = pixel(x, 6);
                const bool distorted = c.mV[VRED] == 255;
                const bool undistorted = c.mV[VGREEN] == 255;
                UNSCOPED_INFO(named(format, "column " + std::to_string(x) + " drawn once"));
                REQUIRE(distorted != undistorted);
                UNSCOPED_INFO(named(format, "column " + std::to_string(x) + " undistorted where excluded"));
                REQUIRE(undistorted == (x >= 8));
            }
            tearDownWater();
        }
    }

    // The next frame's clear empties what the pass marked: the stencil states hand the write mask back whole, and a
    // clear writes stencil only through it.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the next frame's clear empties what the pass marked", "[alwatersurface_gl]")
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            exclusion(0.f, SPAN, 0.f, 256.f, HIGH + 5.f);
            waterUnderStencil();
            UNSCOPED_INFO(named(format, "excluded"));
            REQUIRE((U32)stencil(8, 4) == ALWaterStencil::EXCLUDE_BIT);
            UNSCOPED_INFO(named(format, "water"));
            REQUIRE((U32)stencil(8, 12) == ALWaterStencil::SURFACE_BIT);
            mScene.clear();
            UNSCOPED_INFO(named(format, "excluded, cleared"));
            REQUIRE((U32)stencil(8, 4) == 0U);
            UNSCOPED_INFO(named(format, "water, cleared"));
            REQUIRE((U32)stencil(8, 12) == 0U);
            tearDownWater();
        }
    }

    // The water marks the pixels it shades and no others: a crest a metre and a half above the plane is marked, a box
    // whose top lies two centimetres above it is not, and nor is a pixel an exclusion prim removed the water from. A
    // test of the depth against the plane would take the box for water and the crest for not.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the water marks the pixels it shades and no others", "[alwatersurface_gl]")
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            opaque(0.f, 128.f, 256.f, SPAN, LOW + 0.02f);
            exclusion(128.f, 256.f, 256.f, SPAN, LOW + 5.f);
            waterQuad(0.f, 256.f, 256.f, SPAN, LOW);
            waterQuad(256.f, SPAN, 256.f, SPAN, LOW + 1.5f);
            for (S32 x = 0; x < W; ++x)
            {
                const U32 bits = stencil(x, 12);
                const std::string column = named(format, "column " + std::to_string(x));
                UNSCOPED_INFO(column + " marked as water");
                REQUIRE(((bits & ALWaterStencil::SURFACE_BIT) != 0) == (x >= 8));
                UNSCOPED_INFO(column + " excluded");
                REQUIRE(((bits & ALWaterStencil::EXCLUDE_BIT) != 0) == (x >= 4 && x < 8));
            }
            tearDownWater();
        }
    }

    // The haze pass's two draws, one where the surface bit is clear and one where it is set, shade every pixel once,
    // the second exactly where the water is.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the haze's two draws shade every pixel once, the converging one exactly on the water", "[alwatersurface_gl]")
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            exclusion(0.f, 256.f, 0.f, SPAN, LOW + 5.f);
            waterUnderStencil();
            mScene.clear(GL_COLOR_BUFFER_BIT);
            {
                LLGLEnable blend(GL_BLEND);
                gGL.blendFunc(LLRender::BF_ONE, LLRender::BF_ONE);
                {
                    ALWaterStencil::OffWater state;
                    hazeDraw(LLColor4(1.f, 0.f, 0.f, 0.f));
                }
                {
                    ALWaterStencil::OnWater state;
                    hazeDraw(LLColor4(0.f, 1.f, 0.f, 0.f));
                }
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
            }
            for (S32 x = 0; x < W; ++x)
            {
                const LLColor4U c = pixel(x, 10);
                const bool plain = c.mV[VRED] == 255;
                const bool on_water = c.mV[VGREEN] == 255;
                const std::string column = named(format, "column " + std::to_string(x));
                UNSCOPED_INFO(column + " hazed once");
                REQUIRE(plain != on_water);
                UNSCOPED_INFO(column + " converged where the water is");
                REQUIRE(on_water == (x >= 8));
            }
            tearDownWater();
        }
    }

    // The vertex stage lays the cells and rings out as ALWaterField does, without a crack or an overlap: every pixel
    // inside the rim takes one fragment, and none past it.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the field covers every pixel inside the rim once and none past it", "[alwatersurface_gl]")
    {
        setUpField();
        {
            LLGLDepthTest depth(GL_FALSE);
            ALGLStencil count(GL_ALWAYS, 0, 0xFFu, GL_KEEP, GL_KEEP, GL_INCR, 0xFFu);
            drawField(true);
        }
        const F32 rim = (F32)mLayout.mRim;
        for (S32 y = 0; y < FIELD_SIZE; ++y)
        {
            for (S32 x = 0; x < FIELD_SIZE; ++x)
            {
                const F32 px = fieldX(x);
                const F32 py = fieldX(y);
                const bool within = fabsf(px) < rim && fabsf(py) < rim;
                UNSCOPED_INFO("pixel " + std::to_string(x) + ", " + std::to_string(y) + " covered once inside the rim");
                REQUIRE((U32)stencil(x, y) == (within ? 1U : 0U));
            }
        }
    }

    // With the void hidden only the cells a region holds are drawn: none of the square's empty cells, and no ring.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: with the void hidden only the cells a region holds are drawn", "[alwatersurface_gl]")
    {
        setUpField();
        {
            LLGLDepthTest depth(GL_FALSE);
            ALGLStencil count(GL_ALWAYS, 0, 0xFFu, GL_KEEP, GL_KEEP, GL_INCR, 0xFFu);
            drawField(false);
        }
        const F64 cx = mLayout.centreX();
        const F64 cy = mLayout.centreY();
        for (S32 y = 0; y < FIELD_SIZE; ++y)
        {
            for (S32 x = 0; x < FIELD_SIZE; ++x)
            {
                const F64 gx = cx + fieldX(x);
                const F64 gy = cy + fieldX(y);
                const bool region = gx >= 0.0 && gx < 2.0 * CELL && gy >= 0.0 && gy < 2.0 * CELL;
                UNSCOPED_INFO("pixel " + std::to_string(x) + ", " + std::to_string(y) + " drawn only over a region");
                REQUIRE((U32)stencil(x, y) == (region ? 1U : 0U));
            }
        }
    }

    // Each cell lies at its own region's water height, and the rings at the agent region's.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: each cell lies at its own region's water height, and the rings at the agent region's", "[alwatersurface_gl]")
    {
        setUpField();
        {
            LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LESS);
            drawField(true);
        }
        const F64 cx = mLayout.centreX();
        const F64 cy = mLayout.centreY();
        const auto pixel_of = [&](F64 gx, F64 gy)
        {
            return std::make_pair((S32)((gx - cx + FIELD_VIEW) / (2.0 * FIELD_VIEW) * FIELD_SIZE),
                                  (S32)((gy - cy + FIELD_VIEW) / (2.0 * FIELD_VIEW) * FIELD_SIZE));
        };
        const auto [hx, hy] = pixel_of(1.5 * CELL, 0.5 * CELL);
        UNSCOPED_INFO("the 30 m region");
        CHECK(fabsf(heightAt(hx, hy) - HIGH) < 0.05f);
        const auto [lx, ly] = pixel_of(0.5 * CELL, 1.5 * CELL);
        UNSCOPED_INFO("a 20 m region");
        CHECK(fabsf(heightAt(lx, ly) - LOW) < 0.05f);
        const auto [rx, ry] = pixel_of(cx + 1800.0, cy);
        UNSCOPED_INFO("a ring");
        CHECK(fabsf(heightAt(rx, ry) - LOW) < 0.05f);
    }

    // The sky behind: the field's samples against the world's depth decide whether the target is cleared and drawn,
    // on the GPU. Behind an occluder everywhere, it is left as it was; through one gap, it is cleared.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the sky behind is cleared only where the field shows past the world", "[alwatersurface_gl]")
    {
        for (bool gap : { false, true })
        {
            setUpField();
            UNSCOPED_INFO("behind target");
            REQUIRE(mBehind.allocate(4, 4, GL_RGBA8));

            // The world, above the water everywhere in view, or but for one column of pixels over the square's middle.
            {
                const F32 cx = (F32)mLayout.centreX();
                const F32 cy = (F32)mLayout.centreY();
                const F32 column = gap ? 2.f * FIELD_VIEW / FIELD_SIZE : 0.f;
                opaque(cx - FIELD_VIEW, cx, cy - FIELD_VIEW, cy + FIELD_VIEW, 50.f);
                opaque(cx + column, cx + FIELD_VIEW, cy - FIELD_VIEW, cy + FIELD_VIEW, 50.f);
            }

            {
                LLGLDepthTest depth(GL_TRUE, GL_FALSE);
                LLGLSColorMask no_color(false, false);
                mQuery = mQueries.begin(ALQueryKind::Occlusion);
                drawField(true);
                mQueries.end(mQuery);
            }

            UNSCOPED_INFO((gap ? "water through the gap clears the sky behind" : "water all hidden leaves it"));
            REQUIRE(conditionalClearRan() == gap);
            mBehind.release();
            tearDownWater();
            mField.unload();
            mQueries.release();
        }
    }

    // Anti-repeat: the big layer hex-tiled through the viewer's own reads breaks its repeat and keeps what the author
    // set. One repeat over, its slopes no longer match themselves, where the plain read matches exactly; their mean,
    // variance, and principal axis and spread are the plain read's; and most of the sea is one cell's sample, unblended.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: hex tiling breaks the big layer's repeat and keeps what the author set", "[alwatersurface_gl]")
    {
        const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kScreenVertex);
        const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kTilingFragment);
        {
            INFO("tiling shaders compile");
            CHECK(vs);
            CHECK(fs);
        }
        std::map<std::string, std::string> defines;
        S32 level = 1;
        const GLuint surface = mShaders.loadShaderFile("environment/waterSurface.glsl", level, GL_FRAGMENT_SHADER, &defines);
        level = 1;
        const GLuint lattice = mShaders.loadShaderFile("deferred/hexLatticeUtil.glsl", level, GL_FRAGMENT_SHADER, &defines);
        {
            INFO("the viewer's reads compile");
            CHECK(surface != 0);
            CHECK(lattice != 0);
        }
        mTiling.mProgramObject = glCreateProgram();
        mTiling.mName = "anti-repeat (test)";
        mTiling.attachObject(vs);
        mTiling.attachObject(fs);
        mTiling.attachObject(surface);
        mTiling.attachObject(lattice);
        UNSCOPED_INFO("tiling program links");
        CHECK(mTiling.mapAttributes());
        glDeleteShader(vs);
        glDeleteShader(fs);
        UNSCOPED_INFO("tiling program maps");
        CHECK(mTiling.mapUniforms());

        const std::vector<U8> rgba = noiseMap();
        mNoise = new LLImageGL(TILE_TEXELS, TILE_TEXELS, 4, false);
        mNoise->setExplicitFormat(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        UNSCOPED_INFO("noise map");
        REQUIRE(mNoise->createGLTexture(0, rgba.data()));
        const LLVector2 mean = ALWaterField::meanNormal(rgba.data(), TILE_TEXELS, TILE_TEXELS, 4);

        UNSCOPED_INFO("slopes target");
        REQUIRE(mSlopes.allocate(TILING_SIZE, TILING_SIZE, GL_RGBA32F));
        const ALWaterField::LatticePlaces places = ALWaterField::latticePlaces(1234.5, 6789.1);

        const auto render = [&](S32 mode)
        {
            SlopeField field;
            mSlopes.bindTarget();
            mSlopes.clear();
            mTiling.bind();
            glUniform1i(glGetUniformLocation(mTiling.mProgramObject, "tilingMap"), 0);
            glUniform1i(glGetUniformLocation(mTiling.mProgramObject, "tilingMode"), mode);
            glUniform1f(glGetUniformLocation(mTiling.mProgramObject, "tilingRepeat"), (F32)TILE_TEXELS);
            // Point-sampled: the plain read lands on texel centres, the cells at their offsets between them, and a
            // bilinear read there would smooth the tiled slopes alone.
            gGL.getTextureSlot(0)->bindSampled(mNoise.get(), ALSamplers::PointWrap);
            mTiling.uniform1f(LLShaderMgr::BLEND_FACTOR, 0.f);
            const LLVector4 means(mean.mV[VX], mean.mV[VY], 0.f, 0.f);
            mTiling.uniform4fv(LLShaderMgr::WATER_MAP_MEAN, 1, means.mV);
            mTiling.uniform4fv(LLShaderMgr::WATER_LATTICE_FRAC, 1, places.mFrac.mV);
            mTiling.uniform4iv(LLShaderMgr::WATER_LATTICE_BASE, 1, places.mBase);
            {
                LLGLDisable cull(GL_CULL_FACE);
                LLGLDepthTest depth(GL_FALSE);
                gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
            }
            field.rgba.resize((size_t)TILING_SIZE * TILING_SIZE * 4);
            glReadPixels(0, 0, TILING_SIZE, TILING_SIZE, GL_RGBA, GL_FLOAT, field.rgba.data());
            gGL.getTextureSlot(0)->unbind();
            mSlopes.flush();
            return field;
        };

        const SlopeField plain = render(0);
        const SlopeField tiled = render(1);

        {
            INFO("the plain read repeats");
            CHECK(correlation(plain, TILE_TEXELS, 0) > 0.99);
            CHECK(correlation(plain, 0, TILE_TEXELS) > 0.99);
        }
        const F64 across = correlation(tiled, TILE_TEXELS, 0);
        const F64 along = correlation(tiled, 0, TILE_TEXELS);
        UNSCOPED_INFO("hex-tiled, a repeat across matches no more than by chance");
        CHECK(std::fabs(across) <= 0.2);
        UNSCOPED_INFO("nor a repeat along");
        CHECK(std::fabs(along) <= 0.2);

        const SlopeStats p = slopeStats(plain);
        const SlopeStats t = slopeStats(tiled);
        for (S32 c = 0; c < 2; ++c)
        {
            const F64 shift = (t.mean[c] - p.mean[c]) / std::sqrt(p.var[c]);
            UNSCOPED_INFO("the slopes' mean, off by " + std::to_string(shift) + " sigma");
            REQUIRE(std::fabs(shift) <= 0.05);
            const F64 ratio = t.var[c] / p.var[c];
            UNSCOPED_INFO("their variance, at " + std::to_string(ratio) + " of the plain read's");
            REQUIRE(std::fabs(ratio - 1.0) <= 0.05);
        }
        const F64 turned = (t.axis() - p.axis()) * RAD_TO_DEG;
        UNSCOPED_INFO("the slopes' principal axis, turned " + std::to_string(turned) + " degrees");
        CHECK(std::fabs(turned) <= 2.0);
        const F64 spread = (t.major() / t.minor()) / (p.major() / p.minor());
        UNSCOPED_INFO("their spread along and across it, at " + std::to_string(spread));
        CHECK(std::fabs(spread - 1.0) <= 0.05);

        // Where cells blend, the same: the bands lie where the lattice says, whatever the map shows there, so the plain
        // read's statistics are theirs too. A weighting that favours one sample over another shows only here.
        const SlopeStats b = slopeStats(tiled, true);
        for (S32 c = 0; c < 2; ++c)
        {
            const F64 shift = (b.mean[c] - p.mean[c]) / std::sqrt(p.var[c]);
            UNSCOPED_INFO("the bands' mean, off by " + std::to_string(shift) + " sigma");
            REQUIRE(std::fabs(shift) <= 0.01);
            const F64 ratio = b.var[c] / p.var[c];
            UNSCOPED_INFO("the bands' variance, at " + std::to_string(ratio) + " of the plain read's");
            REQUIRE(std::fabs(ratio - 1.0) <= 0.1);
        }

        S32 single = 0;
        for (S32 j = 0; j < TILING_SIZE; ++j)
        {
            for (S32 i = 0; i < TILING_SIZE; ++i)
            {
                single += tiled.weight(i, j) > 0.999f ? 1 : 0;
            }
        }
        UNSCOPED_INFO("most of the sea is one cell's sample");
        CHECK(single > TILING_SIZE * TILING_SIZE / 2);
    }

    // The lattice stays in the world: every point of the sea shows the same cell of the big layer's lattice from two
    // cameras several cells apart, their places on it reaching the shader through the program's uniforms as the
    // water's do. A place whose floor never arrives shifts every cell by the camera's moves across the lattice.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the lattice stays in the world", "[alwatersurface_gl]")
    {
        constexpr S32 SIZE = 256;
        constexpr F32 REPEAT = 16.f;
        const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kScreenVertex);
        const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kLatticeFragment);
        {
            INFO("lattice shaders compile");
            CHECK(vs);
            CHECK(fs);
        }
        std::map<std::string, std::string> defines;
        S32 level = 1;
        const GLuint surface = mShaders.loadShaderFile("environment/waterSurface.glsl", level, GL_FRAGMENT_SHADER, &defines);
        level = 1;
        const GLuint lattice = mShaders.loadShaderFile("deferred/hexLatticeUtil.glsl", level, GL_FRAGMENT_SHADER, &defines);
        {
            INFO("the viewer's reads compile");
            CHECK(surface != 0);
            CHECK(lattice != 0);
        }
        mTiling.mProgramObject = glCreateProgram();
        mTiling.mName = "lattice (test)";
        mTiling.attachObject(vs);
        mTiling.attachObject(fs);
        mTiling.attachObject(surface);
        mTiling.attachObject(lattice);
        UNSCOPED_INFO("lattice program links");
        CHECK(mTiling.mapAttributes());
        glDeleteShader(vs);
        glDeleteShader(fs);
        UNSCOPED_INFO("lattice program maps");
        CHECK(mTiling.mapUniforms());
        UNSCOPED_INFO("cells target");
        REQUIRE(mSlopes.allocate(SIZE, SIZE, GL_RGBA32F));

        // The target's corner and the two cameras, in repeats of the big layer, well out on the grid.
        const F64 corner[2] = { 1000.37, 2000.61 };
        const F64 cameras[2][2] = { { corner[0] + 3.3, corner[1] + 4.1 }, { corner[0] + 15.0, corner[1] - 3.8 } };
        std::vector<F32> cells[2];
        for (S32 k = 0; k < 2; ++k)
        {
            const ALWaterField::LatticePlaces places = ALWaterField::latticePlaces(cameras[k][0] / ALWaterField::BIG_SCALE,
                                                                                    cameras[k][1] / ALWaterField::BIG_SCALE);
            mSlopes.bindTarget();
            mSlopes.clear();
            mTiling.bind();
            glUniform1f(glGetUniformLocation(mTiling.mProgramObject, "tilingRepeat"), REPEAT);
            glUniform2f(glGetUniformLocation(mTiling.mProgramObject, "latticeFromCamera"), (F32)(corner[0] - cameras[k][0]),
                        (F32)(corner[1] - cameras[k][1]));
            mTiling.uniform4fv(LLShaderMgr::WATER_LATTICE_FRAC, 1, places.mFrac.mV);
            mTiling.uniform4iv(LLShaderMgr::WATER_LATTICE_BASE, 1, places.mBase);
            {
                LLGLDisable cull(GL_CULL_FACE);
                LLGLDepthTest depth(GL_FALSE);
                gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
            }
            cells[k].resize((size_t)SIZE * SIZE * 4);
            glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_FLOAT, cells[k].data());
            mSlopes.flush();
        }

        // Points well inside a cell from both cameras; on a band's edge float rounding may tip either way.
        S32 compared = 0;
        S32 moved = 0;
        for (size_t n = 0; n < (size_t)SIZE * SIZE; ++n)
        {
            const F32* a = cells[0].data() + n * 4;
            const F32* b = cells[1].data() + n * 4;
            if (a[2] > 0.999f && b[2] > 0.999f)
            {
                ++compared;
                moved += (a[0] != b[0] || a[1] != b[1]) ? 1 : 0;
            }
        }
        UNSCOPED_INFO("most points lie inside a cell");
        CHECK(compared > SIZE * SIZE / 2);
        UNSCOPED_INFO("no point of the sea changes cell as the camera moves");
        CHECK(moved == 0);
    }

    // The waves' patches over one cell, as the evaluation stage emits them (transform feedback of `view`, the point
    // less the camera), at fractional levels over a big layer of sines: every point two patches share has one height,
    // and every point inside the cell is surrounded by triangles, so no edge holds a point only one side has. A patch
    // outside the frustum emits nothing, and under a camera within A of the plane the surface stays flat.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the waves' patches give a shared point one height and leave no gap", "[alwatersurface_gl]")
    {
        setUpWater(LLRenderTarget::DEPTH_FMT_24);
        mScene.flush();
        setUpWavesCapture();
        UNSCOPED_INFO("the waves' program has no vertex inputs");
        CHECK(mWaves.mAttributeMask == 0U);

        // A big layer of sines in repeats, 0.05 tall, and a mid layer of 0.03: 0.7 m of waves at a height of 1.
        constexpr S32 MAP = 64;
        std::vector<F32> heights((size_t)MAP * MAP * 2);
        for (S32 j = 0; j < MAP; ++j)
        {
            for (S32 i = 0; i < MAP; ++i)
            {
                const F32 u = (F32)i / MAP;
                const F32 v = (F32)j / MAP;
                heights[((size_t)j * MAP + i) * 2] = 0.05f * sinf(F_TWO_PI * (2.f * u + v));
                heights[((size_t)j * MAP + i) * 2 + 1] = 0.03f * cosf(F_TWO_PI * (u + 3.f * v));
            }
        }
        mHeights = new LLImageGL(MAP, MAP, 2, false);
        mHeights->setNeedsAlphaAndPickMask(false);
        mHeights->setExplicitFormat(GL_RG32F, GL_RG, GL_FLOAT);
        UNSCOPED_INFO("heights");
        REQUIRE(mHeights->createGLTexture(0, reinterpret_cast<const U8*>(heights.data())));

        // The patches over the camera's cell, texel (2, 2) of the map: global cell (1, 1), at LOW.
        constexpr F32 A = 2.f;
        constexpr S32 PATCHES = ALWaterField::PATCHES_PER_CELL * ALWaterField::PATCHES_PER_CELL;
        const auto capture = [&](const LLVector3& eye, bool inside)
        {
            WaveDraw draw;
            const S32 rect[4] = { 2, 2, 3, 3 };
            std::copy(std::begin(rect), std::end(rect), draw.mRect);
            draw.mEye = eye;
            draw.mAmp = 1.f;
            draw.mHeights = mHeights.get();
            draw.mReach = 10000.f;
            draw.mA = A;
            draw.mDensity = 64.f;
            if (!inside)
            {
                // Every point outside the first plane.
                draw.mPlanes[2] = 1.f;
                draw.mPlanes[3] = 1000000.f;
            }
            return tessellate(draw);
        };

        // Far above the cell, so the camera's fade leaves the waves whole.
        const LLVector3 high(484.f, 534.f, LOW + 200.f);
        const std::vector<F32> points = capture(high, true);
        const size_t triangles = points.size() / 9;
        UNSCOPED_INFO("the cell's patches tessellate past one quad each");
        CHECK(triangles > (size_t)PATCHES * 2);

        struct Seen
        {
            U32 mZ = 0;
            bool mHeightsAgree = true;
            F32 mLeft = FLT_MAX;
            F32 mRight = -FLT_MAX;
            F32 mBelow = FLT_MAX;
            F32 mAbove = -FLT_MAX;
        };
        std::map<std::pair<U32, U32>, Seen> seen;
        F32 lo[2] = { FLT_MAX, FLT_MAX };
        F32 hi[2] = { -FLT_MAX, -FLT_MAX };
        F32 tallest = 0.f;
        for (size_t t = 0; t < triangles; ++t)
        {
            const F32* tri = points.data() + t * 9;
            const F32 cx = (tri[0] + tri[3] + tri[6]) / 3.f;
            const F32 cy = (tri[1] + tri[4] + tri[7]) / 3.f;
            for (S32 k = 0; k < 3; ++k)
            {
                const F32* p = tri + k * 3;
                U32 bits[3];
                memcpy(bits, p, sizeof(bits));
                auto [it, added] = seen.try_emplace(std::make_pair(bits[0], bits[1]));
                Seen& s = it->second;
                if (added)
                {
                    s.mZ = bits[2];
                }
                s.mHeightsAgree = s.mHeightsAgree && s.mZ == bits[2];
                s.mLeft = llmin(s.mLeft, cx);
                s.mRight = llmax(s.mRight, cx);
                s.mBelow = llmin(s.mBelow, cy);
                s.mAbove = llmax(s.mAbove, cy);
                for (S32 c = 0; c < 2; ++c)
                {
                    lo[c] = llmin(lo[c], p[c]);
                    hi[c] = llmax(hi[c], p[c]);
                }
                tallest = llmax(tallest, fabsf(p[2] + high.mV[VZ] - LOW));
            }
        }
        UNSCOPED_INFO("the waves move the surface");
        CHECK(tallest > 0.1f);
        S32 split = 0;
        S32 open = 0;
        for (const auto& [key, s] : seen)
        {
            F32 xy[2];
            memcpy(&xy[0], &key.first, sizeof(F32));
            memcpy(&xy[1], &key.second, sizeof(F32));
            split += s.mHeightsAgree ? 0 : 1;
            const bool rim = xy[0] == lo[0] || xy[0] == hi[0] || xy[1] == lo[1] || xy[1] == hi[1];
            const bool surrounded = s.mLeft < xy[0] && s.mRight > xy[0] && s.mBelow < xy[1] && s.mAbove > xy[1];
            open += (rim || surrounded) ? 0 : 1;
        }
        UNSCOPED_INFO("a point two patches share has one height");
        CHECK(split == 0);
        UNSCOPED_INFO("no point inside the cell lies on an edge only one side has");
        CHECK(open == 0);

        UNSCOPED_INFO("a patch outside the frustum emits nothing");
        REQUIRE(capture(high, false).size() == (size_t)0);

        // On a deck in waves: within A of the plane, over a corner the patches share.
        const LLVector3 deck(384.f, 384.f, LOW + 0.5f * A);
        const std::vector<F32> near_points = capture(deck, true);
        bool under = false;
        F32 off = 0.f;
        for (size_t n = 0; n + 2 < near_points.size(); n += 3)
        {
            const F32* p = near_points.data() + n;
            const F32 rise = p[2] + deck.mV[VZ] - LOW;
            if (p[0] == 0.f && p[1] == 0.f)
            {
                under = true;
                UNSCOPED_INFO("the surface under the camera is the plane");
                REQUIRE(fabsf(rise) < 1e-4f);
            }
            if (p[0] * p[0] + p[1] * p[1] > 16.f * A * A)
            {
                off = llmax(off, fabsf(rise));
            }
        }
        UNSCOPED_INFO("a point lies under the camera");
        CHECK(under);
        UNSCOPED_INFO("the waves stand away from it");
        CHECK(off > 0.1f);
    }

    // The sky behind's query with waves: the field raised by A counts a crest over a wall the flat field misses, and
    // the flat field counts the water under a camera between the plane and A above it, which the raised field, above
    // the camera, misses.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the sky behind's query counts both the raised and the flat field", "[alwatersurface_gl]")
    {
        setUpField();
        mScene.flush();
        constexpr F32 A = 3.f;
        const F32 cx = (F32)mLayout.centreX();
        const F32 cy = (F32)mLayout.centreY();
        UNSCOPED_INFO("behind target");
        REQUIRE(mBehind.allocate(4, 4, GL_RGBA8));

        // Whether the query over `raises` lets the conditional clear run, from a camera at `camera_z`, behind a wall at
        // `wall_z` over the whole view where `wall`.
        const auto counted = [&](F32 camera_z, bool wall, F32 wall_z, std::initializer_list<F32> raises)
        {
            mScene.bindTarget();
            mScene.clear();
            mCameraZ = camera_z;
            mView = view(camera_z, false);
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(fieldOrtho(cx, cy));
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadMatrix(mView);
            if (wall)
            {
                opaque(cx - FIELD_VIEW, cx + FIELD_VIEW, cy - FIELD_VIEW, cy + FIELD_VIEW, wall_z);
            }
            {
                LLGLDepthTest depth(GL_TRUE, GL_FALSE);
                LLGLSColorMask no_color(false, false);
                mQuery = mQueries.begin(ALQueryKind::Occlusion);
                for (F32 raise : raises)
                {
                    drawField(true, raise);
                }
                mQueries.end(mQuery);
            }
            mScene.flush();
            return conditionalClearRan();
        };

        // The wall stands half A above the highest water, the south-east region's.
        UNSCOPED_INFO("a wall over all the water hides the flat field");
        CHECK(!counted(FIELD_CAMERA_Z, true, HIGH + 0.5f * A, { 0.f }));
        UNSCOPED_INFO("the field raised by A shows over it");
        CHECK(counted(FIELD_CAMERA_Z, true, HIGH + 0.5f * A, { 0.f, A }));
        UNSCOPED_INFO("from a deck within A of the plane, the raised field alone is missed");
        CHECK(!counted(LOW + 0.5f * A, false, 0.f, { A }));
        UNSCOPED_INFO("and the flat field counts the water");
        CHECK(counted(LOW + 0.5f * A, false, 0.f, { 0.f, A }));

        mBehind.release();
        // tearDownWater flushes the scene, which setUpField left bound.
        mScene.bindTarget();
        tearDownWater();
        mField.unload();
        mQueries.release();
    }

    // Whatever order the waves' patches are drawn in, each is drawn once and in the same place, and the order a view
    // asks for runs from its near end: every order the flags give tessellates the same triangles, and the slow axis run
    // from its high end draws the patches there first.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: every patch order draws the same triangles, starting from the view's near end", "[alwatersurface_gl]")
    {
        setUpWater(LLRenderTarget::DEPTH_FMT_24);
        mScene.flush();
        UNSCOPED_INFO("the square holds the patches' cells");
        CHECK(mLayout.side() >= CULL_RECT[2]);
        setUpWavesCapture();

        // Every point inside each plane.
        F32 planes[20] = {};
        for (S32 i = 0; i < 5; ++i)
        {
            planes[i * 4 + 3] = -1.f;
        }
        const LLVector3 eye(128.f, 128.f, LOW + 200.f);
        const auto before = [](const LLVector2& a, const LLVector2& b)
        {
            return a.mV[VX] != b.mV[VX] ? a.mV[VX] < b.mV[VX] : a.mV[VY] < b.mV[VY];
        };

        std::vector<LLVector2> first_order;
        for (S32 flags = 0; flags < 8; ++flags)
        {
            const S32 order[4] = { flags & 1, (flags >> 1) & 1, (flags >> 2) & 1, 0 };
            std::vector<LLVector2> drawn = tessellatedCentroids(planes, eye, nullptr, order);
            const std::string what = "order " + std::to_string(flags);
            UNSCOPED_INFO(what + ": the patches tessellate");
            REQUIRE(drawn.size() > (size_t)CULL_PATCHES);

            // The first triangle drawn lies on the slow axis's starting end, the last on its other.
            const S32 slow = order[0] ? VX : VY;
            const bool from_high = order[1] != 0;
            const F32 first = drawn.front().mV[slow];
            const F32 last = drawn.back().mV[slow];
            UNSCOPED_INFO(what + ": the slow axis runs from its " + (from_high ? "high" : "low") + " end");
            REQUIRE((from_high ? first > last : first < last));

            std::sort(drawn.begin(), drawn.end(), before);
            if (flags == 0)
            {
                first_order = drawn;
            }
            else
            {
                UNSCOPED_INFO(what + ": the same triangles as the first order");
                REQUIRE(drawn == first_order);
            }
        }

        mScene.bindTarget();
        tearDownWater();
    }

    // The patches the scene hides are culled against the depth pyramid, under both depth conventions and over an
    // odd-sized depth: every level of the pyramid is the farthest depth under its texels, exactly; and the control
    // stage, given it, tessellates every patch any of whose box shows past the scene and drops some it hides -- seen
    // from above a wall, through only the last column of a roof's view, and over ground within A of the water.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the depth pyramid holds the farthest depth and the patches it hides are culled", "[alwatersurface_gl]")
    {
        if (!gGLManager.mHasCompute)
        {
            SKIP("no compute");
        }
        LLRender::sCompute = true;

        std::vector<bool> conventions = { false };
        if (gGLManager.mHasClipControl)
        {
            conventions.push_back(true);
        }
        for (bool reverse : conventions)
        {
            reverse ? reverseZ() : forwardZ();
            const std::string convention = reverse ? "reverse-Z" : "forward-Z";
            setUpWater(LLRenderTarget::DEPTH_FMT_32F);
            mScene.flush();
            mScene.release();
            UNSCOPED_INFO("the square holds the patches' cells");
            REQUIRE(mLayout.side() >= CULL_RECT[2]);
            setUpPyramid();
            setUpWavesCapture();
            setUpBoxes();
            UNSCOPED_INFO("odd scene allocates");
            REQUIRE(mScene.allocate(PYRAMID_W, PYRAMID_H, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE,
                    LLRenderTarget::MIPS_NONE, LLRenderTarget::DEPTH_FMT_32F));

            // A depth of its own at every pixel, so each texel of every level answers for all four of its own.
            mScene.bindTarget();
            mScene.clear();
            noiseDepth();
            UNSCOPED_INFO(convention + ", noise: the pyramid builds");
            REQUIRE(mPyramid.build(mPyramidProgram, mScene));
            checkPyramid(convention + ", noise");
            mScene.flush();

            const auto check = [&](const std::string& what, const LLMatrix4a& proj, const LLMatrix4a& view_matrix,
                                   const LLVector3& eye, const std::function<void()>& occluders)
            {
                mScene.bindTarget();
                mScene.clear();
                gGL.matrixMode(LLRender::MM_PROJECTION);
                gGL.loadMatrix(proj);
                gGL.matrixMode(LLRender::MM_MODELVIEW);
                gGL.loadMatrix(view_matrix);
                occluders();

                UNSCOPED_INFO(what + ": the pyramid builds");
                REQUIRE(mPyramid.build(mPyramidProgram, mScene));
                UNSCOPED_INFO(what + ": from the depth's size");
                REQUIRE(mPyramid.getDepthWidth() == (U32)PYRAMID_W);
                checkPyramid(what);

                F32 planes[20];
                cullPlanes(product(proj, view_matrix), reverse, planes);
                const std::set<S32> in_view = tessellated(planes, eye, nullptr);
                const std::set<S32> kept = tessellated(planes, eye, &mPyramid);
                const std::set<S32> visible = visibleBoxes();
                mScene.flush();

                UNSCOPED_INFO(what + ": some patches show");
                REQUIRE(!visible.empty());
                UNSCOPED_INFO(what + ": the depth culls only patches in view");
                REQUIRE(std::includes(in_view.begin(), in_view.end(), kept.begin(), kept.end()));
                UNSCOPED_INFO(what + ": every patch whose box shows is kept (" + std::to_string(visible.size()) + " shown, "
                              + std::to_string(kept.size()) + " kept)");
                REQUIRE(std::includes(kept.begin(), kept.end(), visible.begin(), visible.end()));
                UNSCOPED_INFO(what + ": the depth hides some of the patches in view (" + std::to_string(in_view.size()) + " in view, "
                              + std::to_string(kept.size()) + " kept)");
                REQUIRE(kept.size() + 64 <= in_view.size());
            };

            // From above and behind a wall: the near half of the water hidden, the sightline over the wall's top
            // reaching the waves' box at y = 250, and the far half over it, each a band some tens of pixels tall.
            const F32 aspect = (F32)PYRAMID_W / (F32)PYRAMID_H;
            const F32 far_plane = reverse ? std::numeric_limits<F32>::infinity() : 4000.f;
            const LLVector3 behind(128.f, -200.f, LOW + 160.f);
            check(convention + ", over a wall", al_perspective(60.f * DEG_TO_RAD, aspect, 1.f, far_plane),
                  lookAt(behind, LLVector3(128.f, 200.f, LOW)), behind,
                  [&]() { wall(-3000.f, 3000.f, -100.f, LOW - 500.f, LOW + 125.f); });

            // Straight down on the cells under a roof that leaves the view's last column of pixels open: only the
            // patches under that column show.
            const F32 span = 768.f;
            const F32 pixel = span / (F32)PYRAMID_W;
            const LLVector3 above(128.f, 128.f, LOW + 100.f);
            check(convention + ", through the last column", al_ortho(-256.f, 512.f, -256.f, 512.f, 1.f, 200.f), view(above.mV[VZ], false), above,
                  [&]() { opaque(-300.f, -256.f + pixel * (F32)(PYRAMID_W - 1), -300.f, 560.f, LOW + 50.f); });

            // Straight down on ground within A of the water, as at a shore: west of x = 128 a metre above the low
            // water, which the waves' box reaches over, and east of it a metre above the box, which hides it. The
            // high region's water stands over both.
            check(convention + ", over a shore", al_ortho(-256.f, 512.f, -256.f, 512.f, 1.f, 200.f), view(above.mV[VZ], false), above,
                  [&]()
                  {
                      opaque(-300.f, 128.f, -300.f, 560.f, LOW + 0.5f * CULL_A);
                      opaque(128.f, 560.f, -300.f, 560.f, LOW + 1.5f * CULL_A);
                  });

            mPyramid.release();
            mPyramidProgram.unload();
            mWaves.unload();
            mBoxes.unload();
            mScene.bindTarget();
            tearDownWater();
        }
        forwardZ();
    }
    // With waves the field leaves the patches' cells to them and fans the cells beside them onto their corners: drawn
    // alone, it covers every pixel inside the rim once but those over the patches' cells, which it leaves; with the
    // patches drawn too, flat, every pixel inside the rim is covered once.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: with waves, the field and the patches cover every pixel inside the rim once", "[alwatersurface_gl]")
    {
        setUpField(REACH);
        const ALWaterField::PatchRect rect = ALWaterField::patchRect(mLayout);
        UNSCOPED_INFO("the reach takes patches");
        CHECK(rect == ALWaterField::PatchRect{ 2, 2, 5, 5 });
        // The patches' cells, global metres: cells 0 to 2 each way.
        const F64 cx = mLayout.centreX();
        const F64 cy = mLayout.centreY();
        const F32 rim = (F32)mLayout.mRim;
        const auto patched = [&](S32 x, S32 y)
        {
            const F64 gx = cx + fieldX(x);
            const F64 gy = cy + fieldX(y);
            return gx >= 0.0 && gx < 3.0 * CELL && gy >= 0.0 && gy < 3.0 * CELL;
        };
        const auto within = [&](S32 x, S32 y) { return fabsf(fieldX(x)) < rim && fabsf(fieldX(y)) < rim; };

        {
            LLGLDepthTest depth(GL_FALSE);
            ALGLStencil count(GL_ALWAYS, 0, 0xFFu, GL_KEEP, GL_KEEP, GL_INCR, 0xFFu);
            drawField(true, 0.f, rect);
        }
        for (S32 y = 0; y < FIELD_SIZE; ++y)
        {
            for (S32 x = 0; x < FIELD_SIZE; ++x)
            {
                UNSCOPED_INFO("pixel " + std::to_string(x) + ", " + std::to_string(y) + " covered by the field but over the patches");
                REQUIRE((U32)stencil(x, y) == (within(x, y) && !patched(x, y) ? 1U : 0U));
            }
        }

        setUpWavesCapture();
        WaveDraw draw;
        const S32 patches[4] = { rect.mMinX, rect.mMinY, rect.mEndX, rect.mEndY };
        std::copy(std::begin(patches), std::end(patches), draw.mRect);
        draw.mVoidDrawn = true;
        draw.mEye.set(300.f, 300.f, LOW + 200.f);
        draw.mReach = (F32)mLayout.mReach;
        {
            LLGLDepthTest depth(GL_FALSE);
            ALGLStencil count(GL_ALWAYS, 0, 0xFFu, GL_KEEP, GL_KEEP, GL_INCR, 0xFFu);
            LLGLDisable cull(GL_CULL_FACE);
            bindWaves(draw);
            drawPatches(draw);
            LLGLSLShader::unbind();
        }
        for (S32 y = 0; y < FIELD_SIZE; ++y)
        {
            for (S32 x = 0; x < FIELD_SIZE; ++x)
            {
                UNSCOPED_INFO("pixel " + std::to_string(x) + ", " + std::to_string(y) + " covered once with the patches");
                REQUIRE((U32)stencil(x, y) == (within(x, y) ? 1U : 0U));
            }
        }
    }

    // The field and the waves' patches meet without a T-junction: no vertex of either lies inside an edge of a triangle
    // of either but at its ends, so no crack can open along one. Captured in agent space, the field from its vertex
    // stage and the patches from their evaluation stage, flat, at the levels a camera over them gives, the edges along
    // the rectangle past the reach whole. Water at two heights, where regions meet, shares no edge to crack along.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the field and the waves' patches meet without a T-junction", "[alwatersurface_gl]")
    {
        setUpField(REACH);
        mScene.flush();
        const ALWaterField::PatchRect rect = ALWaterField::patchRect(mLayout);
        UNSCOPED_INFO("the reach takes patches");
        REQUIRE(!rect.empty());
        setUpFieldCapture();
        setUpWavesCapture("vary_position");
        // Clip space, and eye space, are agent space.
        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.loadIdentity();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.loadIdentity();

        struct Triangle
        {
            LLVector3d mCorner[3];
        };
        std::vector<Triangle> triangles;
        const auto add = [&](const std::vector<F32>& points, size_t components)
        {
            size_t added = 0;
            for (size_t t = 0; t + 3 * components <= points.size(); t += 3 * components)
            {
                Triangle f;
                for (size_t k = 0; k < 3; ++k)
                {
                    const F32* p = points.data() + t + k * components;
                    f.mCorner[k].set(p[0], p[1], p[2]);
                }
                // An instance not drawn, its vertices at one point, and a ring quad's empty first triangle.
                const LLVector3d u = f.mCorner[1] - f.mCorner[0];
                const LLVector3d v = f.mCorner[2] - f.mCorner[0];
                if (std::fabs(u.mdV[VX] * v.mdV[VY] - v.mdV[VX] * u.mdV[VY]) > 1e-6)
                {
                    triangles.push_back(f);
                    ++added;
                }
            }
            return added;
        };

        bindField(mFieldCapture, true, 0.f, rect);
        const std::vector<F32> field = captureTriangles(4, [&]() { drawFieldParts(mFieldCapture, rect); });
        LLGLSLShader::unbind();
        UNSCOPED_INFO("the field is drawn");
        CHECK(add(field, 4) > 0);

        WaveDraw draw;
        const S32 patches[4] = { rect.mMinX, rect.mMinY, rect.mEndX, rect.mEndY };
        std::copy(std::begin(patches), std::end(patches), draw.mRect);
        draw.mVoidDrawn = true;
        draw.mEye.set(300.f, 300.f, LOW + 60.f);
        draw.mReach = (F32)mLayout.mReach;
        draw.mDensity = 32.f;
        const size_t tessellated = add(tessellate(draw), 3);
        UNSCOPED_INFO("the patches tessellate past one quad each");
        CHECK(tessellated > (size_t)draw.patches() * 2);

        // Every vertex, bucketed by where it is.
        constexpr F64 TOLERANCE = 1e-3;
        constexpr F64 BUCKET = 64.0;
        const auto bucket_of = [](F64 v) { return (S64)std::floor(v / BUCKET); };
        std::set<std::tuple<F64, F64, F64>> vertices;
        for (const Triangle& f : triangles)
        {
            for (const LLVector3d& c : f.mCorner)
            {
                vertices.emplace(c.mdV[VX], c.mdV[VY], c.mdV[VZ]);
            }
        }
        std::map<std::pair<S64, S64>, std::vector<LLVector3d>> buckets;
        for (const auto& [x, y, z] : vertices)
        {
            buckets[{ bucket_of(x), bucket_of(y) }].emplace_back(x, y, z);
        }

        S32 junctions = 0;
        std::string first;
        const auto named = [](const LLVector3d& p)
        {
            return "(" + std::to_string(p.mdV[VX]) + ", " + std::to_string(p.mdV[VY]) + ", " + std::to_string(p.mdV[VZ]) + ")";
        };
        for (const Triangle& f : triangles)
        {
            for (S32 e = 0; e < 3; ++e)
            {
                const LLVector3d& a = f.mCorner[e];
                const LLVector3d& b = f.mCorner[(e + 1) % 3];
                const LLVector3d ab = b - a;
                const F64 length = ab.length();
                const F64 lo_x = llmin(a.mdV[VX], b.mdV[VX]) - TOLERANCE;
                const F64 hi_x = llmax(a.mdV[VX], b.mdV[VX]) + TOLERANCE;
                const F64 lo_y = llmin(a.mdV[VY], b.mdV[VY]) - TOLERANCE;
                const F64 hi_y = llmax(a.mdV[VY], b.mdV[VY]) + TOLERANCE;
                for (S64 i = bucket_of(lo_x); i <= bucket_of(hi_x); ++i)
                {
                    for (S64 j = bucket_of(lo_y); j <= bucket_of(hi_y); ++j)
                    {
                        const auto found = buckets.find({ i, j });
                        if (found == buckets.end())
                        {
                            continue;
                        }
                        for (const LLVector3d& p : found->second)
                        {
                            const LLVector3d ap = p - a;
                            const F64 along = (ap * ab) / length;
                            const bool at_end = ap.length() <= TOLERANCE || (p - b).length() <= TOLERANCE;
                            if (at_end || along <= 0.0 || along >= length)
                            {
                                continue;
                            }
                            if ((ap % ab).length() / length <= TOLERANCE && junctions++ == 0)
                            {
                                first = ": " + named(p) + " inside " + named(a) + " - " + named(b);
                            }
                        }
                    }
                }
            }
        }
        UNSCOPED_INFO("no vertex lies inside an edge" + first);
        CHECK(junctions == 0);
    }

    // The waves' heights are the field the normal map's slopes integrate to, read where the shading reads the map: a map
    // of known heights, encoded as normals, baked and uploaded as the viewer bakes and uploads it, displaces the patches
    // by BIG_METRES times its big layer's field at the big layer's coordinates and MID_METRES times its mid layer's at
    // the little map's zw read, the heights' sign and axes those the normals tilt by.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the waves' heights are the field the normal map's slopes integrate to", "[alwatersurface_gl]")
    {
        setUpWater(LLRenderTarget::DEPTH_FMT_24);
        mScene.flush();
        setUpWavesCapture("view", 0);

        // The heights, in repeats: sines across the map's axes and along both, at whole cycles a repeat.
        struct Term
        {
            S32 mP;
            S32 mQ;
            F64 mAmplitude;
            F64 mPhase;
        };
        const Term terms[] = { { 1, 0, 0.03, 0.3 }, { 1, 2, 0.012, 1.1 }, { 0, 1, 0.02, 2.0 }, { 3, 1, 0.004, 0.7 } };
        constexpr F64 TWO_PI = 6.283185307179586;
        constexpr S32 MAP = 64;
        std::vector<U8> rgb((size_t)MAP * MAP * 3);
        for (S32 j = 0; j < MAP; ++j)
        {
            for (S32 i = 0; i < MAP; ++i)
            {
                const F64 u = (F64)i / MAP;
                const F64 v = (F64)j / MAP;
                F64 slope_u = 0.0;
                F64 slope_v = 0.0;
                for (const Term& t : terms)
                {
                    const F64 c = t.mAmplitude * TWO_PI * std::cos(TWO_PI * (t.mP * u + t.mQ * v) + t.mPhase);
                    slope_u += t.mP * c;
                    slope_v += t.mQ * c;
                }
                // The normal the slopes lean it by, encoded as a normal map's are.
                const F64 n[3] = { -slope_u, -slope_v, 1.0 };
                const F64 length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                for (S32 c = 0; c < 3; ++c)
                {
                    rgb[((size_t)j * MAP + i) * 3 + c] = (U8)llclamp((S32)std::lround((n[c] / length * 0.5 + 0.5) * 255.0), 0, 255);
                }
            }
        }
        const ALWaterHeightBake::Heights heights = ALWaterHeightBake::bake(rgb.data(), rgb.size(), MAP, MAP, 3);
        UNSCOPED_INFO("the map bakes");
        CHECK(heights.valid());
        const std::vector<U16> texels = ALWaterHeightBake::halfTexels(heights);
        mHeights = new LLImageGL(MAP, MAP, 2, false);
        mHeights->setNeedsAlphaAndPickMask(false);
        mHeights->setExplicitFormat(GL_RG16F, GL_RG, GL_HALF_FLOAT);
        UNSCOPED_INFO("heights");
        REQUIRE(mHeights->createGLTexture(0, reinterpret_cast<const U8*>(texels.data())));

        // The field a layer recovers at map coordinate (u, v): the big layer's the heights themselves; the mid layer's
        // each term times (p^2 + a q^2) / (p^2 + a^2 q^2), a = MID_ASPECT. GL samples texel i at its centre,
        // (i + 1/2) / MAP, where the bake's texel i is the field at i / MAP.
        const auto field = [&](F64 u, F64 v, bool mid)
        {
            u -= 0.5 / MAP;
            v -= 0.5 / MAP;
            const F64 a = ALWaterHeightBake::MID_ASPECT;
            F64 h = 0.0;
            for (const Term& t : terms)
            {
                const F64 p2 = (F64)(t.mP * t.mP);
                const F64 q2 = (F64)(t.mQ * t.mQ);
                const F64 c = mid ? (p2 + a * q2) / (p2 + a * a * q2) : 1.0;
                h += c * t.mAmplitude * std::sin(TWO_PI * (t.mP * u + t.mQ * v) + t.mPhase);
            }
            return h;
        };

        // The patches over cell (1, 1), from high above them, so neither fade touches the waves.
        WaveDraw draw;
        const S32 rect[4] = { 2, 2, 3, 3 };
        std::copy(std::begin(rect), std::end(rect), draw.mRect);
        draw.mEye.set(384.f, 384.f, LOW + 200.f);
        draw.mAmp = 1.f;
        draw.mHeights = mHeights.get();
        draw.mReach = 10000.f;
        draw.mA = 1.f;
        draw.mDensity = 64.f;
        const std::vector<F32> points = tessellate(draw);
        UNSCOPED_INFO("the cell's patches tessellate");
        REQUIRE(points.size() > (size_t)draw.patches() * 18);

        F64 worst = 0.0;
        F64 tallest = 0.0;
        for (size_t n = 0; n + 3 <= points.size(); n += 3)
        {
            // The point less the camera: the r the wave coordinates are worked from.
            const F64 rx = points[n];
            const F64 ry = points[n + 1];
            const F64 got = (F64)points[n + 2] + draw.mEye.mV[VZ] - LOW;
            // water_wave_coords: the sweep bends x, then each layer scales it.
            const F64 vx = rx + (std::cos(rx * ALWaterField::SWEEP_SCALE[0]) + std::sin(ry * ALWaterField::SWEEP_SCALE[1])) *
                                    ALWaterField::SWEEP_AMPLITUDE;
            const F64 want = ALWaterHeightBake::BIG_METRES * field(vx * ALWaterField::BIG_SCALE, ry * ALWaterField::BIG_SCALE, false) +
                             ALWaterHeightBake::MID_METRES *
                                 field(vx * ALWaterField::LITTLE_ZW_SCALE[0], ry * ALWaterField::LITTLE_ZW_SCALE[1], true);
            worst = llmax(worst, std::fabs(got - want));
            tallest = llmax(tallest, std::fabs(want));
        }
        UNSCOPED_INFO("the waves stand");
        CHECK(tallest > 0.3);
        UNSCOPED_INFO("each point's height is the map's field there, off by up to " + std::to_string(worst) + " m");
        CHECK(worst <= 0.01);
        mScene.bindTarget();
        tearDownWater();
    }

    // The waves' program links with a fragment stage that reads the atmospherics' varyings, as the water's does: the
    // waves' features attach atmosphericsVarsV to the evaluation stage, the stage before the fragment one, where it
    // declares them.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the waves' program links with a fragment stage reading the atmospherics' varyings", "[alwatersurface_gl]")
    {
        std::map<std::string, std::string> defines = { { "WATER_HEXTILE", "1" } };
        // The objects the features attach, at the stages the viewer loads them for.
        const std::pair<const char*, GLenum> shared[] = {
            { "environment/waterSurface.glsl", GL_VERTEX_SHADER },
            { "deferred/textureUtilV.glsl", GL_VERTEX_SHADER },
            { "objects/nonindexedTextureV.glsl", GL_VERTEX_SHADER },
            { "deferred/depthPyramidUtil.glsl", GL_TESS_CONTROL_SHADER },
            { "environment/waterSurface.glsl", GL_TESS_EVALUATION_SHADER },
            { "deferred/hexLatticeUtil.glsl", GL_TESS_EVALUATION_SHADER },
            { "windlight/atmosphericsVarsV.glsl", GL_TESS_EVALUATION_SHADER },
            { "deferred/globalF.glsl", GL_FRAGMENT_SHADER },
            { "environment/waterSurface.glsl", GL_FRAGMENT_SHADER },
            { "deferred/hexLatticeUtil.glsl", GL_FRAGMENT_SHADER },
            { "windlight/atmosphericsVarsF.glsl", GL_FRAGMENT_SHADER },
        };
        for (const auto& object : shared)
        {
            S32 level = 1;
            UNSCOPED_INFO(std::string("compiled ") + object.first);
            REQUIRE(mShaders.loadShaderFile(object.first, level, object.second, &defines) != 0);
        }

        // The program's own stages as gWaterWavesProgram names them, but a fragment stage of the test's that reads the
        // atmospherics' varyings through atmosphericsVarsF, as calculatesAtmospherics attaches it.
        mWaves.mProgramObject = glCreateProgram();
        mWaves.mName = "Water Waves (test)";
        mWaves.mFeatures.hasWaterSurface = true;
        mWaves.mFeatures.hasWaterWaves = true;
        const std::pair<const char*, GLenum> own[] = {
            { "environment/waterPatchV.glsl", GL_VERTEX_SHADER },
            { "environment/waterTC.glsl", GL_TESS_CONTROL_SHADER },
            { "environment/waterTE.glsl", GL_TESS_EVALUATION_SHADER },
            { "environment/waterVaryingsV.glsl", GL_TESS_EVALUATION_SHADER },
        };
        for (const auto& object : own)
        {
            S32 level = 1;
            const GLuint handle = mShaders.loadShaderFile(object.first, level, object.second, &defines);
            UNSCOPED_INFO(std::string("compiled ") + object.first);
            REQUIRE(handle != 0);
            mWaves.attachObject(handle);
        }
        UNSCOPED_INFO("the waves' features attach");
        CHECK(mShaders.attachShaderFeatures(&mWaves));
        UNSCOPED_INFO("and the atmospherics' reads");
        CHECK(mWaves.attachStageObject(GL_FRAGMENT_SHADER, "windlight/atmosphericsVarsF.glsl"));

        const auto& evaluation = mShaders.mShaderObjects[LLShaderMgr::STAGE_TESS_EVALUATION];
        const auto vars = evaluation.find("windlight/atmosphericsVarsV.glsl");
        UNSCOPED_INFO("atmosphericsVarsV is compiled for the evaluation stage");
        CHECK(vars != evaluation.end());
        GLuint attached[32] = { 0 };
        GLsizei count = 0;
        glGetAttachedShaders(mWaves.mProgramObject, 32, &count, attached);
        UNSCOPED_INFO("the features attach it to the program");
        CHECK(std::find(attached, attached + llmax(count, 0), vars->second) != attached + llmax(count, 0));

        const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER,
                                                     "#version 400\n"
                                                     "in vec3 view;\n"
                                                     "out vec4 frag_color;\n"
                                                     "vec3 getAdditiveColor();\n"
                                                     "vec3 getAtmosAttenuation();\n"
                                                     "void main() { frag_color = vec4(getAdditiveColor() + getAtmosAttenuation() + view, 1.0); }\n");
        UNSCOPED_INFO("fragment compiles");
        CHECK(fs != 0);
        mWaves.attachObject(fs);
        UNSCOPED_INFO("the waves' program links");
        CHECK(mWaves.mapAttributes());
        glDeleteShader(fs);
    }

    // The pyramid builds from a depth that carries stencil, as the screen's does where the water keeps its bits, under
    // both conventions: 24-bit depth with 8 of stencil, and 32-bit float depth with 8 of stencil.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the pyramid builds from a depth that carries stencil", "[alwatersurface_gl]")
    {
        if (!gGLManager.mHasCompute)
        {
            SKIP("no compute");
        }
        LLRender::sCompute = true;

        std::vector<bool> conventions = { false };
        if (gGLManager.mHasClipControl)
        {
            conventions.push_back(true);
        }
        for (bool reverse : conventions)
        {
            reverse ? reverseZ() : forwardZ();
            setUpPyramid();
            for (LLRenderTarget::eDepthFormat format : formats())
            {
                const std::string what = named(format, reverse ? "reverse-Z" : "forward-Z");
                UNSCOPED_INFO(what + ": the scene allocates");
                REQUIRE(mScene.allocate(PYRAMID_W, PYRAMID_H, GL_RGBA8, true, true, ALTextureSlot::TT_TEXTURE,
                        LLRenderTarget::MIPS_NONE, format));
                UNSCOPED_INFO(what + ": with stencil");
                REQUIRE(mScene.hasStencil());
                mScene.bindTarget();
                mScene.clear();
                noiseDepth();
                UNSCOPED_INFO(what + ": the pyramid builds");
                REQUIRE(mPyramid.build(mPyramidProgram, mScene));
                // A 24-bit depth's unit, near enough: a sampler and a read may each round its conversion to float.
                checkPyramid(what, format == LLRenderTarget::DEPTH_FMT_24 ? 2.f / 16777215.f : 0.f);
                mScene.flush();
                mScene.release();
            }
            mPyramid.release();
            mPyramidProgram.unload();
            mNoiseDepth.unload();
            mShaders.clearShaderObjects();
        }
        forwardZ();
    }

    // The pyramid's box test keeps what it cannot see behind, under both conventions: a box behind a wall is hidden and
    // one before it kept; a box reaching the eye's plane is kept, whatever its image; and a box under more of the
    // coarsest level's texels than are read is kept unread, while one under as many as are read is read, and hidden.
    TEST_CASE_METHOD(alwatersurface_data, "alwatersurface_gl: the pyramid's box test keeps what it cannot see behind", "[alwatersurface_gl]")
    {
        if (!gGLManager.mHasCompute)
        {
            SKIP("no compute");
        }
        LLRender::sCompute = true;

        std::vector<bool> conventions = { false };
        if (gGLManager.mHasClipControl)
        {
            conventions.push_back(true);
        }
        for (bool reverse : conventions)
        {
            reverse ? reverseZ() : forwardZ();
            const std::string convention = reverse ? "reverse-Z" : "forward-Z";
            setUpWater(LLRenderTarget::DEPTH_FMT_32F);
            mScene.flush();
            mScene.release();
            setUpPyramid();
            setUpProbe();

            // Looking along y from 50 m up, a wall 10 m off over the whole view.
            UNSCOPED_INFO("the scene allocates");
            REQUIRE(mScene.allocate(PYRAMID_W, PYRAMID_H, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE,
                    LLRenderTarget::MIPS_NONE, LLRenderTarget::DEPTH_FMT_32F));
            const F32 far_plane = reverse ? std::numeric_limits<F32>::infinity() : 4000.f;
            const LLMatrix4a proj = al_perspective(60.f * DEG_TO_RAD, (F32)PYRAMID_W / (F32)PYRAMID_H, 1.f, far_plane);
            const LLMatrix4a along = lookAt(LLVector3(0.f, 0.f, 50.f), LLVector3(0.f, 100.f, 50.f));
            mScene.bindTarget();
            mScene.clear();
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(proj);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadMatrix(along);
            wall(-1000.f, 1000.f, 10.f, -1000.f, 1000.f);
            UNSCOPED_INFO(convention + ": the pyramid builds");
            REQUIRE(mPyramid.build(mPyramidProgram, mScene));
            // The third box runs from behind the camera to past the wall, off to the side: the part of it in view lies
            // behind the wall.
            const std::vector<bool> walled = probe({ { LLVector3(-1.f, 100.f, 49.f), LLVector3(1.f, 102.f, 51.f) },
                                                     { LLVector3(-1.f, 5.f, 49.f), LLVector3(1.f, 6.f, 51.f) },
                                                     { LLVector3(50.f, -5.f, 49.f), LLVector3(60.f, 100.f, 51.f) } },
                                                   product(proj, along));
            mScene.flush();
            mScene.release();
            UNSCOPED_INFO(convention + ": a box behind the wall is hidden");
            REQUIRE(!walled[0]);
            UNSCOPED_INFO(convention + ": one before it is kept");
            REQUIRE(walled[1]);
            UNSCOPED_INFO(convention + ": one reaching the eye's plane is kept");
            REQUIRE(walled[2]);

            // Straight down on a roof over a view 1280 pixels wide, a metre a pixel: the coarsest level's texels are 256
            // pixels, five across it.
            UNSCOPED_INFO("the wide scene allocates");
            REQUIRE(mScene.allocate(1280, 16, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE,
                    LLRenderTarget::MIPS_NONE, LLRenderTarget::DEPTH_FMT_32F));
            const LLMatrix4a wide = al_ortho(0.f, 1280.f, 0.f, 16.f, 1.f, 200.f);
            const LLMatrix4a down = view(LOW + 100.f, false);
            mScene.bindTarget();
            mScene.clear();
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(wide);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadMatrix(down);
            opaque(-10.f, 1290.f, -10.f, 26.f, LOW + 50.f);
            UNSCOPED_INFO(convention + ": the wide pyramid builds");
            REQUIRE(mPyramid.build(mPyramidProgram, mScene));
            const std::vector<bool> roofed = probe({ { LLVector3(0.5f, 4.f, LOW - 1.f), LLVector3(1279.5f, 12.f, LOW + 1.f) },
                                                     { LLVector3(0.5f, 4.f, LOW - 1.f), LLVector3(959.5f, 12.f, LOW + 1.f) },
                                                     { LLVector3(0.5f, 4.f, LOW + 60.f), LLVector3(959.5f, 12.f, LOW + 62.f) } },
                                                   product(wide, down));
            mScene.flush();
            mScene.release();
            UNSCOPED_INFO(convention + ": a box under five of the coarsest texels is kept unread");
            REQUIRE(roofed[0]);
            UNSCOPED_INFO(convention + ": one under four is read, and hidden under the roof");
            REQUIRE(!roofed[1]);
            UNSCOPED_INFO(convention + ": and above the roof, kept");
            REQUIRE(roofed[2]);

            mPyramid.release();
            mPyramidProgram.unload();
            mProbe.unload();
            LLGLSLShader::unbind();
            mColour.unload();
            mExclusion.unload();
            mShaders.clearShaderObjects();
            mCellMap = nullptr;
        }
        forwardZ();
    }
}
