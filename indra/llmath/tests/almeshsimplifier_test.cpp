/**
 * @file almeshsimplifier_test.cpp
 * @brief The mesh uploader's simplification and normals, against what each
 *        mode promises to keep.
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

#include "almeshsimplifier.h"
#include "alscratcharena.h"
#include "llmath.h"
#include "llvector4a.h"
#include "llvolume.h"
#include "v2math.h"
#include "v3math.h"
#include "almeshcorpus.h"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using Seams = ALMeshSimplifier::Seams;

    // The uploader's own levels of detail: a third of the triangles a level.
    constexpr F32 DECIMATORS[] = { 3.f, 9.f, 27.f };

    // How far over its target the uploader's Auto mode lets a simplification
    // come and still take it.
    constexpr F32 ALLOWED_DRIFT = 1.8f;

    struct ArenaInstalled
    {
        static void* MESHOPTIMIZER_ALLOC_CALLCONV newBlock(size_t bytes) { return ::operator new(bytes); }
        static void MESHOPTIMIZER_ALLOC_CALLCONV deleteBlock(void* block) { ::operator delete(block); }

        ArenaInstalled() { meshopt_setAllocator(ALScratchArena::allocate, ALScratchArena::deallocate); }
        ~ArenaInstalled() { meshopt_setAllocator(newBlock, deleteBlock); }
    };

    LLVector3 v3(const LLVector4a& v)
    {
        return LLVector3(v.getF32ptr());
    }

    // ------------------------------------------------------------------
    // Geometry

    // A face as a test writes it.
    struct Geometry
    {
        std::vector<LLVector3> positions;
        std::vector<LLVector3> normals;
        std::vector<LLVector2> uvs;
        std::vector<U16> indices;
        LLVector3 scale = LLVector3(1.f, 1.f, 1.f);

        U16 add(const LLVector3& p, const LLVector3& n, const LLVector2& uv)
        {
            positions.push_back(p);
            normals.push_back(n);
            uvs.push_back(uv);
            return (U16)(positions.size() - 1);
        }

        void triangle(U16 a, U16 b, U16 c) { indices.insert(indices.end(), { a, b, c }); }

        void write(LLVolumeFace& f) const
        {
            f.resizeVertices((S32)positions.size());
            f.resizeIndices((S32)indices.size());
            for (size_t v = 0; v < positions.size(); ++v)
            {
                f.mPositions[v].load3(positions[v].mV);
                f.mNormals[v].load3(normals[v].mV);
                f.mTexCoords[v] = uvs[v];
            }
            std::copy(indices.begin(), indices.end(), f.mIndices);
            f.mNormalizedScale = scale;
            LLVector4a& min = f.mExtents[0];
            LLVector4a& max = f.mExtents[1];
            min = max = f.mPositions[0];
            for (S32 v = 1; v < f.mNumVertices; ++v)
            {
                min.setMin(min, f.mPositions[v]);
                max.setMax(max, f.mPositions[v]);
            }
            f.mCenter->setAdd(min, max);
            f.mCenter->mul(0.5f);
        }
    };

    LLPointer<LLVolume> volumeOf(const std::vector<Geometry>& faces)
    {
        LLVolumeParams params;
        params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
        params.setSculptID(LLUUID::null, LL_SCULPT_TYPE_MESH);
        LLPointer<LLVolume> volume = new LLVolume(params, 1.f);
        volume->getVolumeFaces().resize(faces.size());
        for (size_t f = 0; f < faces.size(); ++f)
        {
            faces[f].write(volume->getVolumeFace((S32)f));
        }
        return volume;
    }

    // A volume of as many faces as base, for a simplification to fill.
    LLPointer<LLVolume> targetFor(const LLVolume& base)
    {
        LLVolumeParams params;
        params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
        params.setSculptID(LLUUID::null, LL_SCULPT_TYPE_MESH);
        LLPointer<LLVolume> volume = new LLVolume(params, 1.f);
        volume->getVolumeFaces().resize(base.getNumVolumeFaces());
        return volume;
    }

    // Rows first_row to last_row of a latitude-longitude sphere as an
    // exporter writes one: the seam column twice, at u = 0 and u = 1, and
    // each pole a row of points with one triangle a column. The seam's two
    // columns, a pole's points, and the row two calls share are each the
    // same position to the bit.
    Geometry sphereRows(S32 columns, S32 rows, S32 first_row, S32 last_row, F32 u_offset = 0.f)
    {
        Geometry g;
        for (S32 r = first_row; r <= last_row; ++r)
        {
            for (S32 c = 0; c <= columns; ++c)
            {
                const F32 theta = F_PI * r / rows;
                const F32 phi = F_TWO_PI * (c % columns) / columns;
                const F32 ring = (r == 0 || r == rows) ? 0.f : sinf(theta);
                const F32 height = r == 0 ? 1.f : (r == rows ? -1.f : cosf(theta));
                const LLVector3 n(ring * cosf(phi), ring * sinf(phi), height);
                g.add(n * 0.5f, n, LLVector2(u_offset + (F32)c / columns, 1.f - (F32)r / rows));
            }
        }
        auto at = [&](S32 c, S32 r) { return (U16)(c + (r - first_row) * (columns + 1)); };
        for (S32 r = first_row; r < last_row; ++r)
        {
            for (S32 c = 0; c < columns; ++c)
            {
                if (r > 0)
                {
                    g.triangle(at(c, r), at(c, r + 1), at(c + 1, r));
                }
                if (r < rows - 1)
                {
                    g.triangle(at(c + 1, r), at(c, r + 1), at(c + 1, r + 1));
                }
            }
        }
        return g;
    }

    Geometry sphere(S32 columns, S32 rows)
    {
        return sphereRows(columns, rows, 0, rows);
    }

    // Each triangle its own three vertices, with the triangle's own normal:
    // a model exported flat shaded.
    Geometry flatShaded(const Geometry& from)
    {
        Geometry g;
        for (size_t i = 0; i < from.indices.size(); i += 3)
        {
            const LLVector3& a = from.positions[from.indices[i]];
            const LLVector3& b = from.positions[from.indices[i + 1]];
            const LLVector3& c = from.positions[from.indices[i + 2]];
            LLVector3 n = (b - a) % (c - a);
            n.normalize();
            const U16 first = g.add(a, n, from.uvs[from.indices[i]]);
            g.add(b, n, from.uvs[from.indices[i + 1]]);
            g.add(c, n, from.uvs[from.indices[i + 2]]);
            g.triangle(first, (U16)(first + 1), (U16)(first + 2));
        }
        return g;
    }

    // A cube of six sides, each a grid of cells by cells quads with its own
    // normal. Each side is a texture island of its own, or with shared_uvs
    // every side takes its coordinates from x and y alike, so that its only
    // seams are its normals'.
    Geometry cube(S32 cells, bool shared_uvs = false)
    {
        Geometry g;
        for (S32 axis = 0; axis < 3; ++axis)
        {
            for (F32 sign : { 1.f, -1.f })
            {
                LLVector3 n, a;
                n.mV[axis] = sign;
                a.mV[(axis + 1) % 3] = 1.f;
                const LLVector3 b = n % a;
                const U16 first = (U16)g.positions.size();
                for (S32 j = 0; j <= cells; ++j)
                {
                    for (S32 i = 0; i <= cells; ++i)
                    {
                        const F32 s = 2.f * i / cells - 1.f;
                        const F32 t = 2.f * j / cells - 1.f;
                        const LLVector3 p = (n + a * s + b * t) * 0.5f;
                        const LLVector2 uv =
                            shared_uvs ? LLVector2(p.mV[0] + 0.5f, p.mV[1] + 0.5f) : LLVector2(s * 0.5f + 0.5f, t * 0.5f + 0.5f);
                        g.add(p, n, uv);
                    }
                }
                for (S32 j = 0; j < cells; ++j)
                {
                    for (S32 i = 0; i < cells; ++i)
                    {
                        const U16 c0 = (U16)(first + i + j * (cells + 1));
                        const U16 c1 = (U16)(c0 + 1);
                        const U16 c2 = (U16)(c0 + cells + 1);
                        const U16 c3 = (U16)(c2 + 1);
                        g.triangle(c0, c1, c3);
                        g.triangle(c0, c3, c2);
                    }
                }
            }
        }
        return g;
    }

    // A flat square of cells by cells quads in z = 0, x from x0, facing up.
    Geometry plane(S32 cells, F32 x0 = -0.5f)
    {
        Geometry g;
        for (S32 j = 0; j <= cells; ++j)
        {
            for (S32 i = 0; i <= cells; ++i)
            {
                const F32 u = (F32)i / cells;
                const F32 v = (F32)j / cells;
                g.add(LLVector3(x0 + u, v - 0.5f, 0.f), LLVector3(0.f, 0.f, 1.f), LLVector2(u, v));
            }
        }
        for (S32 j = 0; j < cells; ++j)
        {
            for (S32 i = 0; i < cells; ++i)
            {
                const U16 c0 = (U16)(i + j * (cells + 1));
                const U16 c1 = (U16)(c0 + 1);
                const U16 c2 = (U16)(c0 + cells + 1);
                const U16 c3 = (U16)(c2 + 1);
                g.triangle(c0, c1, c3);
                g.triangle(c0, c3, c2);
            }
        }
        return g;
    }

    // ------------------------------------------------------------------
    // Measures

    using PositionKey = std::array<U32, 3>;
    using EdgeKey = std::pair<PositionKey, PositionKey>;

    PositionKey keyOf(const LLVector4a& p)
    {
        PositionKey key;
        for (S32 k = 0; k < 3; ++k)
        {
            const F32 c = p[k] == 0.f ? 0.f : p[k];
            std::memcpy(&key[k], &c, sizeof(F32));
        }
        return key;
    }

    EdgeKey edgeOf(const PositionKey& a, const PositionKey& b)
    {
        return a < b ? EdgeKey(a, b) : EdgeKey(b, a);
    }

    // The edges of a face, by position, that one triangle of it has: where
    // it ends, as far as its positions go.
    std::set<EdgeKey> borderOf(const LLVolumeFace& face)
    {
        std::map<EdgeKey, S32> uses;
        for (S32 i = 0; i < face.mNumIndices; i += 3)
        {
            for (S32 k = 0; k < 3; ++k)
            {
                ++uses[edgeOf(keyOf(face.mPositions[face.mIndices[i + k]]), keyOf(face.mPositions[face.mIndices[i + (k + 1) % 3]]))];
            }
        }
        std::set<EdgeKey> border;
        for (const auto& [edge, count] : uses)
        {
            if (count == 1)
            {
                border.insert(edge);
            }
        }
        return border;
    }

    std::set<PositionKey> positionsOf(const LLVolumeFace& face)
    {
        std::set<PositionKey> keys;
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            keys.insert(keyOf(face.mPositions[v]));
        }
        return keys;
    }

    // The edges of a's border on positions b had, that b's border does not
    // have: a gap between the two.
    size_t gaps(const LLVolumeFace& a, const LLVolumeFace& b, const std::set<PositionKey>& shared)
    {
        const std::set<EdgeKey> border_b = borderOf(b);
        size_t open = 0;
        for (const EdgeKey& edge : borderOf(a))
        {
            if (shared.count(edge.first) && shared.count(edge.second) && !border_b.count(edge))
            {
                ++open;
            }
        }
        return open;
    }

    // Every index names a vertex, and there are whole triangles.
    bool wellFormed(const LLVolumeFace& face)
    {
        if (face.mNumIndices < 3 || face.mNumIndices % 3 != 0 || face.mNumVertices <= 0)
        {
            return false;
        }
        return std::all_of(face.mIndices, face.mIndices + face.mNumIndices, [&](U16 i) { return i < face.mNumVertices; });
    }

    // Every vertex of face is one of base's, the same in each attribute.
    bool fromBase(const LLVolumeFace& face, const LLVolume& base)
    {
        std::set<std::vector<U32>> vertices;
        auto bits = [](const LLVolumeFace& f, S32 v)
        {
            std::vector<U32> out(8);
            std::memcpy(&out[0], f.mPositions[v].getF32ptr(), 12);
            std::memcpy(&out[3], f.mNormals[v].getF32ptr(), 12);
            std::memcpy(&out[6], f.mTexCoords[v].mV, 8);
            return out;
        };
        for (S32 f = 0; f < base.getNumVolumeFaces(); ++f)
        {
            const LLVolumeFace& from = base.getVolumeFace(f);
            for (S32 v = 0; v < from.mNumVertices; ++v)
            {
                vertices.insert(bits(from, v));
            }
        }
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            if (!vertices.count(bits(face, v)))
            {
                return false;
            }
        }
        return true;
    }

    LLVector3 closestOnTriangle(const LLVector3& p, const LLVector3& a, const LLVector3& b, const LLVector3& c)
    {
        const LLVector3 ab = b - a, ac = c - a, ap = p - a;
        const F32 d1 = ab * ap, d2 = ac * ap;
        if (d1 <= 0.f && d2 <= 0.f)
        {
            return a;
        }
        const LLVector3 bp = p - b;
        const F32 d3 = ab * bp, d4 = ac * bp;
        if (d3 >= 0.f && d4 <= d3)
        {
            return b;
        }
        const F32 vc = d1 * d4 - d3 * d2;
        if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
        {
            return a + ab * (d1 / (d1 - d3));
        }
        const LLVector3 cp = p - c;
        const F32 d5 = ab * cp, d6 = ac * cp;
        if (d6 >= 0.f && d5 <= d6)
        {
            return c;
        }
        const F32 vb = d5 * d2 - d1 * d6;
        if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
        {
            return a + ac * (d2 / (d2 - d6));
        }
        const F32 va = d3 * d6 - d5 * d4;
        if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
        {
            return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        }
        const F32 denom = 1.f / (va + vb + vc);
        return a + ab * (vb * denom) + ac * (vc * denom);
    }

    // The farthest any vertex of base lies from the simplified surface, over
    // base's largest extent.
    F32 deviation(const LLVolume& base, const LLVolume& simplified)
    {
        std::vector<std::array<LLVector3, 3>> triangles;
        for (S32 f = 0; f < simplified.getNumVolumeFaces(); ++f)
        {
            const LLVolumeFace& face = simplified.getVolumeFace(f);
            for (S32 i = 0; i < face.mNumIndices; i += 3)
            {
                triangles.push_back({ v3(face.mPositions[face.mIndices[i]]), v3(face.mPositions[face.mIndices[i + 1]]),
                                      v3(face.mPositions[face.mIndices[i + 2]]) });
            }
        }
        LLVector3 least(F32_MAX, F32_MAX, F32_MAX), most(-F32_MAX, -F32_MAX, -F32_MAX);
        F32 farthest = 0.f;
        for (S32 f = 0; f < base.getNumVolumeFaces(); ++f)
        {
            const LLVolumeFace& face = base.getVolumeFace(f);
            for (S32 v = 0; v < face.mNumVertices; ++v)
            {
                const LLVector3 p = v3(face.mPositions[v]);
                for (S32 k = 0; k < 3; ++k)
                {
                    least.mV[k] = llmin(least.mV[k], p.mV[k]);
                    most.mV[k] = llmax(most.mV[k], p.mV[k]);
                }
                F32 nearest = F32_MAX;
                for (const auto& t : triangles)
                {
                    nearest = llmin(nearest, (closestOnTriangle(p, t[0], t[1], t[2]) - p).lengthSquared());
                }
                farthest = llmax(farthest, nearest);
            }
        }
        const LLVector3 size = most - least;
        return sqrtf(farthest) / llmax(size.mV[0], llmax(size.mV[1], size.mV[2]));
    }

    S32 triangleCount(const LLVolume& volume)
    {
        S32 count = 0;
        for (S32 f = 0; f < volume.getNumVolumeFaces(); ++f)
        {
            count += volume.getVolumeFace(f).mNumIndices / 3;
        }
        return count;
    }

    ALMeshSimplifier::Target toCount(F32 decimator)
    {
        ALMeshSimplifier::Target goal;
        goal.decimator = decimator;
        goal.error = 1.f;
        return goal;
    }

    const char* nameOf(Seams seams)
    {
        switch (seams)
        {
            case Seams::ALL: return "every seam held";
            case Seams::UV: return "texture seams held";
            case Seams::NONE: return "no seam held";
            case Seams::SLOPPY: return "sloppy";
        }
        return "?";
    }

    // ------------------------------------------------------------------
    // Targets and error

    TEST_CASE("almeshsimplifier: a model reaches the uploader's triangle targets", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        const std::pair<const char*, Geometry> models[] = {
            { "UV sphere 64 by 32", sphere(64, 32) },
            { "cube of 16 by 16 cells a side", cube(16) },
        };
        for (const auto& [name, geometry] : models)
        {
            LLPointer<LLVolume> base = volumeOf({ geometry });
            const S32 base_triangles = triangleCount(*base);
            for (F32 decimator : DECIMATORS)
            {
                for (Seams seams : { Seams::ALL, Seams::UV, Seams::NONE })
                {
                    INFO(fmt::format("{}, a {}th of {} triangles, {}", name, decimator, base_triangles, nameOf(seams)));
                    LLPointer<LLVolume> target = targetFor(*base);
                    const F32 ratio = ALMeshSimplifier(*base).simplifyModel(*target, toCount(decimator), seams);
                    REQUIRE(ratio > 0.f);
                    REQUIRE(wellFormed(target->getVolumeFace(0)));
                    CHECK(fromBase(target->getVolumeFace(0), *base));
                    const S32 triangles = triangleCount(*target);
                    INFO(fmt::format("{} triangles", triangles));
                    // Holding every seam, the simplifier runs out of edges it
                    // may collapse near the target, where the rounding of
                    // its errors decides which are left: the UV sphere's
                    // 27th, a target of 147, ends at 146 triangles as MSVC
                    // builds it and at 150 as GCC does. Free to move a seam,
                    // it reaches the target.
                    const F32 over = seams == Seams::ALL ? 1.05f : 1.f;
                    CHECK(triangles <= (S32)ceilf(base_triangles / decimator * over));
                    CHECK(ratio * ALLOWED_DRIFT >= decimator);
                }
            }
        }
    }

    // A flat shaded model is a seam at every edge. Held to them, its
    // simplification can hardly start; free to cross them, it reaches the
    // target.
    TEST_CASE("almeshsimplifier: free to cross normal seams, a flat shaded model reaches its target", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        LLPointer<LLVolume> base = volumeOf({ flatShaded(sphere(32, 16)) });
        const F32 decimator = 9.f;

        LLPointer<LLVolume> held = targetFor(*base);
        const F32 held_ratio = ALMeshSimplifier(*base).simplifyModel(*held, toCount(decimator), Seams::ALL);
        INFO(fmt::format("every seam held: a ratio of {}", held_ratio));
        CHECK(held_ratio * ALLOWED_DRIFT < decimator);

        for (Seams seams : { Seams::UV, Seams::NONE })
        {
            LLPointer<LLVolume> free = targetFor(*base);
            const F32 ratio = ALMeshSimplifier(*base).simplifyModel(*free, toCount(decimator), seams);
            INFO(fmt::format("{}: a ratio of {}", nameOf(seams), ratio));
            REQUIRE(ratio > 0.f);
            REQUIRE(wellFormed(free->getVolumeFace(0)));
            CHECK(ratio * ALLOWED_DRIFT >= decimator);
        }
    }

    // The error the simplifier holds to is its estimate, each vertex's
    // distance from the planes of the triangles it has taken over; a vertex
    // of the base left out in the middle of a curved region lies a little
    // further than that from the simplified surface. How much further
    // depends on the collapses rounding leads it to: 1.37 times the error
    // as MSVC builds it, 1.65 as Clang does with fused multiply-adds.
    TEST_CASE("almeshsimplifier: a simplification to an error stays within it", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        LLPointer<LLVolume> base = volumeOf({ sphere(48, 24) });
        S32 coarser = 0;
        for (F32 error : { 0.02f, 0.005f })
        {
            ALMeshSimplifier::Target goal;
            goal.error = error;
            LLPointer<LLVolume> target = targetFor(*base);
            const F32 ratio = ALMeshSimplifier(*base).simplifyModel(*target, goal, Seams::ALL);
            const F32 measured = deviation(*base, *target);
            const S32 triangles = triangleCount(*target);
            INFO(fmt::format("an error of {}: {} of {} triangles, a ratio of {}, {} measured", error, triangles, triangleCount(*base),
                             ratio, measured));
            CHECK(ratio > 1.25f);
            CHECK(measured <= 2.f * error);
            CHECK(triangles > coarser);
            coarser = triangles;
        }
    }

    // A texture coordinate that is not finite, as a loader may pass on,
    // counts for nothing, and the rest of its face's coordinates weigh as
    // they did: one would otherwise make the face's texture density not a
    // number, and the face's coordinates weigh nothing at all. A flat plane
    // whose coordinates bend across it is held up by them alone.
    TEST_CASE("almeshsimplifier: a texture coordinate that is not finite leaves its face's others their weight", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        Geometry model = plane(16);
        for (LLVector2& uv : model.uvs)
        {
            uv.mV[0] += 0.2f * sinf(F_TWO_PI * 2.f * uv.mV[1]);
        }
        ALMeshSimplifier::Target goal;
        goal.error = 0.005f;
        auto simplified = [&](const Geometry& geometry)
        {
            LLPointer<LLVolume> base = volumeOf({ geometry });
            LLPointer<LLVolume> target = targetFor(*base);
            REQUIRE(ALMeshSimplifier(*base).simplifyModel(*target, goal, Seams::ALL) > 0.f);
            return triangleCount(*target);
        };
        const S32 whole = simplified(model);
        model.uvs[0] = LLVector2(std::numeric_limits<F32>::quiet_NaN(), 0.f);
        const S32 with_nan = simplified(model);
        INFO(fmt::format("{} triangles from finite coordinates, {} with one not", whole, with_nan));
        REQUIRE(whole > 8);
        CHECK(with_nan * 2 >= whole);
    }

    // ------------------------------------------------------------------
    // Borders between faces

    // A sphere's two halves, each its own face: they share the equator's
    // positions, and each vertex's weight names its face, so that a vertex of
    // one in the other shows. With flat_unmapped the upper half is flat
    // shaded, a seam at every edge, and neither has texture coordinates, so
    // that a simplification free to cross seams has vertices of the upper
    // half's own to move the equator's onto at no cost in texture.
    LLPointer<LLVolume> halves(bool flat_unmapped = false)
    {
        Geometry upper = sphereRows(48, 24, 0, 12);
        Geometry lower = sphereRows(48, 24, 12, 24, 10.f);
        if (flat_unmapped)
        {
            upper = flatShaded(upper);
            std::fill(upper.uvs.begin(), upper.uvs.end(), LLVector2(0.f, 0.f));
            std::fill(lower.uvs.begin(), lower.uvs.end(), LLVector2(0.f, 0.f));
        }
        LLPointer<LLVolume> volume = volumeOf({ upper, lower });
        for (S32 f = 0; f < 2; ++f)
        {
            LLVolumeFace& face = volume->getVolumeFace(f);
            face.allocateWeights(face.mNumVertices);
            for (S32 v = 0; v < face.mNumVertices; ++v)
            {
                face.mWeights[v].set((F32)f, 0.f, 0.f, 0.f);
            }
        }
        return volume;
    }

    // Every vertex of the face is one of base's face's own.
    bool ownVertices(const LLVolumeFace& face, S32 f)
    {
        return face.mWeights &&
               std::all_of(face.mWeights, face.mWeights + face.mNumVertices, [&](const LLVector4a& w) { return w[0] == (F32)f; });
    }

    std::set<PositionKey> sharedPositions(const LLVolume& base)
    {
        const std::set<PositionKey> a = positionsOf(base.getVolumeFace(0));
        const std::set<PositionKey> b = positionsOf(base.getVolumeFace(1));
        std::set<PositionKey> shared;
        std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::inserter(shared, shared.begin()));
        return shared;
    }

    // Each half on its own, as a position-only simplification without a
    // border lock took them: their equators come out differently.
    LLPointer<LLVolume> unlockedHalves(const LLVolume& base, F32 decimator)
    {
        LLPointer<LLVolume> target = targetFor(base);
        for (S32 f = 0; f < 2; ++f)
        {
            const LLVolumeFace& face = base.getVolumeFace(f);
            std::vector<U32> indices(face.mIndices, face.mIndices + face.mNumIndices);
            std::vector<U32> simplified(indices.size());
            const size_t target_count = (size_t)(indices.size() / decimator);
            const size_t count = meshopt_simplify(simplified.data(), indices.data(), indices.size(), face.mPositions[0].getF32ptr(),
                                                  face.mNumVertices, sizeof(LLVector4a), target_count, 1.f, 0, nullptr);
            for (U32& index : simplified)
            {
                index += f ? (U32)base.getVolumeFace(0).mNumVertices : 0;
            }
            REQUIRE(ALMeshSimplifier::compact(base, f, simplified.data(), count, target->getVolumeFace(f)));
        }
        return target;
    }

    TEST_CASE("almeshsimplifier: simplified face by face, two faces that meet still meet", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        LLPointer<LLVolume> base = halves();
        const std::set<PositionKey> shared = sharedPositions(*base);
        REQUIRE(shared.size() == 48);

        // The scene holds the gap open where the border is not locked.
        LLPointer<LLVolume> unlocked = unlockedHalves(*base, 9.f);
        const size_t unlocked_gaps = gaps(unlocked->getVolumeFace(0), unlocked->getVolumeFace(1), shared) +
                                     gaps(unlocked->getVolumeFace(1), unlocked->getVolumeFace(0), shared);
        INFO(fmt::format("{} edges open without the lock", unlocked_gaps));
        CHECK(unlocked_gaps > 0);

        for (F32 decimator : DECIMATORS)
        {
            for (Seams seams : { Seams::ALL, Seams::UV, Seams::NONE })
            {
                INFO(fmt::format("a {}th, {}", decimator, nameOf(seams)));
                LLPointer<LLVolume> target = targetFor(*base);
                for (S32 f = 0; f < 2; ++f)
                {
                    CHECK(ALMeshSimplifier(*base).simplifyFace(f, target->getVolumeFace(f), toCount(decimator), seams) > 0.f);
                    REQUIRE(wellFormed(target->getVolumeFace(f)));
                    CHECK(fromBase(target->getVolumeFace(f), *base));
                }
                CHECK(gaps(target->getVolumeFace(0), target->getVolumeFace(1), shared) == 0);
                CHECK(gaps(target->getVolumeFace(1), target->getVolumeFace(0), shared) == 0);
            }
        }
    }

    TEST_CASE("almeshsimplifier: simplified together, two faces meet and keep their own vertices", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        for (bool flat_unmapped : { false, true })
        {
            LLPointer<LLVolume> base = halves(flat_unmapped);
            const std::set<PositionKey> shared = sharedPositions(*base);
            for (F32 decimator : DECIMATORS)
            {
                for (Seams seams : { Seams::ALL, Seams::UV, Seams::NONE })
                {
                    INFO(fmt::format("{} halves, a {}th, {}", flat_unmapped ? "flat shaded, unmapped" : "smooth", decimator,
                                     nameOf(seams)));
                    LLPointer<LLVolume> target = targetFor(*base);
                    REQUIRE(ALMeshSimplifier(*base).simplifyModel(*target, toCount(decimator), seams) > 0.f);
                    for (S32 f = 0; f < 2; ++f)
                    {
                        const LLVolumeFace& face = target->getVolumeFace(f);
                        REQUIRE(wellFormed(face));
                        CHECK(fromBase(face, *base));
                        CHECK(ownVertices(face, f));
                    }
                    CHECK(gaps(target->getVolumeFace(0), target->getVolumeFace(1), shared) == 0);
                    CHECK(gaps(target->getVolumeFace(1), target->getVolumeFace(0), shared) == 0);
                }
            }
        }
    }

    // A face that meets no other is free at its border, where one that
    // meets another is held where it meets it: the held square keeps the
    // 32 edges along the side it shares, and is free along its other
    // three, which it simplifies as the square on its own does.
    TEST_CASE("almeshsimplifier: a face is held at its border only where it meets another", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        const F32 decimator = 100.f;

        LLPointer<LLVolume> alone = volumeOf({ plane(32) });
        LLPointer<LLVolume> alone_target = targetFor(*alone);
        const F32 alone_ratio = ALMeshSimplifier(*alone).simplifyFace(0, alone_target->getVolumeFace(0), toCount(decimator), Seams::ALL);
        INFO(fmt::format("on its own: a ratio of {}", alone_ratio));
        CHECK(alone_ratio * ALLOWED_DRIFT >= decimator);

        // The same square beside another, which it meets along x = 0.5.
        LLPointer<LLVolume> pair = volumeOf({ plane(32), plane(32, 0.5f) });
        LLPointer<LLVolume> pair_target = targetFor(*pair);
        const F32 pair_ratio = ALMeshSimplifier(*pair).simplifyFace(0, pair_target->getVolumeFace(0), toCount(decimator), Seams::ALL);
        INFO(fmt::format("beside another: a ratio of {}", pair_ratio));
        CHECK(pair_ratio > 0.f);
        const std::set<PositionKey> shared = sharedPositions(*pair);
        REQUIRE(shared.size() == 33);
        const std::set<EdgeKey> border = borderOf(pair_target->getVolumeFace(0));
        S32 held = 0;
        for (const EdgeKey& edge : border)
        {
            held += shared.count(edge.first) && shared.count(edge.second);
        }
        CHECK(held == 32);
        // Fewer triangles than a border of 128 edges, all held, would need.
        CHECK(pair_target->getVolumeFace(0).mNumIndices / 3 < 4 * 32);
    }

    // Two faces at the same positions, as a garment's inside is a copy of
    // its outside turned over, with a material of its own: each is held
    // where its open border runs, which the other's is too, and simplifies
    // inside it as a face on its own does.
    TEST_CASE("almeshsimplifier: faces that lie over one another simplify face by face", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        const F32 decimator = 9.f;
        const Geometry front = plane(32);
        Geometry back = front;
        for (size_t i = 0; i < back.indices.size(); i += 3)
        {
            std::swap(back.indices[i + 1], back.indices[i + 2]);
        }
        for (LLVector3& normal : back.normals)
        {
            normal = -normal;
        }
        LLPointer<LLVolume> base = volumeOf({ front, back });
        LLPointer<LLVolume> target = targetFor(*base);
        for (S32 f = 0; f < 2; ++f)
        {
            INFO(fmt::format("face {}", f));
            const F32 ratio = ALMeshSimplifier(*base).simplifyFace(f, target->getVolumeFace(f), toCount(decimator), Seams::ALL);
            INFO(fmt::format("a ratio of {}", ratio));
            CHECK(ratio * ALLOWED_DRIFT >= decimator);
            REQUIRE(wellFormed(target->getVolumeFace(f)));
            CHECK(borderOf(target->getVolumeFace(f)).size() == 4 * 32);
        }
    }

    // ------------------------------------------------------------------
    // Seams

    // The meridian where u runs from 1 back to 0 is a texture seam; a
    // triangle across it spans most of the texture.
    TEST_CASE("almeshsimplifier: texture seams stay where the mode holds them", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        LLPointer<LLVolume> base = volumeOf({ sphere(48, 24) });
        for (Seams seams : { Seams::ALL, Seams::UV })
        {
            for (F32 decimator : DECIMATORS)
            {
                INFO(fmt::format("a {}th, {}", decimator, nameOf(seams)));
                LLPointer<LLVolume> target = targetFor(*base);
                REQUIRE(ALMeshSimplifier(*base).simplifyModel(*target, toCount(decimator), seams) > 0.f);
                const LLVolumeFace& face = target->getVolumeFace(0);
                REQUIRE(wellFormed(face));
                size_t across = 0;
                for (S32 i = 0; i < face.mNumIndices; i += 3)
                {
                    const F32 u0 = face.mTexCoords[face.mIndices[i]].mV[0];
                    const F32 u1 = face.mTexCoords[face.mIndices[i + 1]].mV[0];
                    const F32 u2 = face.mTexCoords[face.mIndices[i + 2]].mV[0];
                    across += llmax(u0, llmax(u1, u2)) - llmin(u0, llmin(u1, u2)) > 0.5f;
                }
                CHECK(across == 0);
            }
        }
    }

    // A square of tiles, each quad its own texture island a little apart
    // from its neighbours', so that every inner vertex is a texture seam.
    // Held, no triangle spans two islands, which would smear the texture
    // between them; free, the simplification crosses them.
    TEST_CASE("almeshsimplifier: a model of texture islands keeps them apart where the mode holds its texture seams", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        constexpr S32 TILES = 16;
        constexpr F32 PITCH = 1.f / TILES;
        Geometry tiles;
        for (S32 j = 0; j < TILES; ++j)
        {
            for (S32 i = 0; i < TILES; ++i)
            {
                U16 corner[4];
                for (S32 k = 0; k < 4; ++k)
                {
                    const S32 x = i + (k & 1), y = j + (k >> 1);
                    const LLVector2 uv(PITCH * (i + 0.1f + 0.8f * (k & 1)), PITCH * (j + 0.1f + 0.8f * (k >> 1)));
                    corner[k] = tiles.add(LLVector3(PITCH * x - 0.5f, PITCH * y - 0.5f, 0.f), LLVector3(0.f, 0.f, 1.f), uv);
                }
                tiles.triangle(corner[0], corner[1], corner[3]);
                tiles.triangle(corner[0], corner[3], corner[2]);
            }
        }
        LLPointer<LLVolume> base = volumeOf({ tiles });
        auto spanning = [](const LLVolumeFace& face)
        {
            size_t count = 0;
            for (S32 i = 0; i < face.mNumIndices; i += 3)
            {
                const LLVector2& a = face.mTexCoords[face.mIndices[i]];
                bool apart = false;
                for (S32 k = 1; k < 3; ++k)
                {
                    const LLVector2& b = face.mTexCoords[face.mIndices[i + k]];
                    apart |= llfloor(a.mV[0] * TILES) != llfloor(b.mV[0] * TILES) || llfloor(a.mV[1] * TILES) != llfloor(b.mV[1] * TILES);
                }
                count += apart;
            }
            return count;
        };
        for (F32 decimator : DECIMATORS)
        {
            INFO(fmt::format("a {}th", decimator));
            for (Seams seams : { Seams::ALL, Seams::UV })
            {
                LLPointer<LLVolume> held = targetFor(*base);
                REQUIRE(ALMeshSimplifier(*base).simplifyModel(*held, toCount(decimator), seams) > 0.f);
                CHECK(spanning(held->getVolumeFace(0)) == 0);
            }
            LLPointer<LLVolume> free = targetFor(*base);
            REQUIRE(ALMeshSimplifier(*base).simplifyModel(*free, toCount(decimator), Seams::NONE) > 0.f);
            CHECK(spanning(free->getVolumeFace(0)) > 0);
        }
    }

    // A cube whose sides share their texture coordinates is seamed only by
    // its normals. Held, every triangle keeps its side's normal at all three
    // corners; free, the simplification crosses the edges between sides.
    TEST_CASE("almeshsimplifier: normal seams stay where the mode holds them", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        LLPointer<LLVolume> base = volumeOf({ cube(8, true) });
        auto mixed = [](const LLVolumeFace& face)
        {
            size_t count = 0;
            for (S32 i = 0; i < face.mNumIndices; i += 3)
            {
                const LLVector3 n0 = v3(face.mNormals[face.mIndices[i]]);
                count += n0 != v3(face.mNormals[face.mIndices[i + 1]]) || n0 != v3(face.mNormals[face.mIndices[i + 2]]);
            }
            return count;
        };
        for (F32 decimator : DECIMATORS)
        {
            INFO(fmt::format("a {}th", decimator));
            LLPointer<LLVolume> held = targetFor(*base);
            REQUIRE(ALMeshSimplifier(*base).simplifyModel(*held, toCount(decimator), Seams::ALL) > 0.f);
            CHECK(mixed(held->getVolumeFace(0)) == 0);
        }

        // Free to cross them, at a level of detail the held one cannot
        // reach, it does.
        LLPointer<LLVolume> free = targetFor(*base);
        REQUIRE(ALMeshSimplifier(*base).simplifyModel(*free, toCount(100.f), Seams::UV) > 0.f);
        CHECK(mixed(free->getVolumeFace(0)) > 0);
    }

    // ------------------------------------------------------------------
    // Small parts

    TEST_CASE("almeshsimplifier: a distant level of detail short of its target loses its small detached parts first", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        Geometry model = sphere(32, 16);
        // Bolts: cubes of split normals, which no simplification that keeps
        // seams can reduce, a fiftieth of the sphere's size.
        for (S32 bolt = 0; bolt < 16; ++bolt)
        {
            const Geometry part = cube(1);
            const F32 phi = F_TWO_PI * bolt / 16;
            const LLVector3 at(0.6f * cosf(phi), 0.6f * sinf(phi), 0.f);
            const U16 first = (U16)model.positions.size();
            for (size_t v = 0; v < part.positions.size(); ++v)
            {
                model.add(at + part.positions[v] * 0.02f, part.normals[v], part.uvs[v]);
            }
            for (U16 index : part.indices)
            {
                model.indices.push_back((U16)(first + index));
            }
        }
        LLPointer<LLVolume> base = volumeOf({ model });
        const S32 bolt_triangles = 16 * 12;
        const S32 target_triangles = (S32)(triangleCount(*base) / 9.f);
        REQUIRE(target_triangles < bolt_triangles);

        LLPointer<LLVolume> target = targetFor(*base);
        ALMeshSimplifier::Target goal = toCount(9.f);
        goal.distant = true;
        REQUIRE(ALMeshSimplifier(*base).simplifyModel(*target, goal, Seams::ALL) > 0.f);
        const LLVolumeFace& face = target->getVolumeFace(0);
        REQUIRE(wellFormed(face));
        S32 bolt_vertices = 0;
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            bolt_vertices += v3(face.mPositions[v]).length() > 0.55f;
        }
        INFO(fmt::format("{} triangles for a target of {}, {} vertices left on the bolts", triangleCount(*target), target_triangles,
                         bolt_vertices));
        CHECK(bolt_vertices == 0);
        CHECK(triangleCount(*target) <= target_triangles);

        // A level of detail seen from near keeps them, short of its target.
        LLPointer<LLVolume> near_target = targetFor(*base);
        REQUIRE(ALMeshSimplifier(*base).simplifyModel(*near_target, toCount(9.f), Seams::ALL) > 0.f);
        CHECK(triangleCount(*near_target) > target_triangles);
    }

    // A distant level of detail made to an error, with no triangle target,
    // of a model that is nothing but detached parts within that error: the
    // parts are all it has, and it keeps them rather than drop every one,
    // as a simplification to nothing would leave the uploader to take the
    // whole model for it. The modes tried hold the cubes' seams, so only
    // dropping a part could take one away; a mode free to cross them may
    // collapse a cube smaller than the error to nothing.
    TEST_CASE("almeshsimplifier: a distant level of detail to an error keeps a model of small parts", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        Geometry model;
        for (S32 bead = 0; bead < 16; ++bead)
        {
            const Geometry part = cube(1);
            const F32 phi = F_TWO_PI * bead / 16;
            const LLVector3 at(0.5f * cosf(phi), 0.5f * sinf(phi), 0.f);
            const U16 first = (U16)model.positions.size();
            for (size_t v = 0; v < part.positions.size(); ++v)
            {
                model.add(at + part.positions[v] * 0.02f, part.normals[v], part.uvs[v]);
            }
            for (U16 index : part.indices)
            {
                model.indices.push_back((U16)(first + index));
            }
        }
        LLPointer<LLVolume> base = volumeOf({ model });

        ALMeshSimplifier::Target goal;
        goal.decimator = 0.f;
        goal.error = 0.03f;
        goal.distant = true;
        for (Seams seams : { Seams::ALL, Seams::UV })
        {
            INFO(nameOf(seams));
            LLPointer<LLVolume> target = targetFor(*base);
            CHECK(ALMeshSimplifier(*base).simplifyModel(*target, goal, seams) > 0.f);
            REQUIRE(wellFormed(target->getVolumeFace(0)));
            CHECK(triangleCount(*target) >= 16);
        }
    }

    // ------------------------------------------------------------------
    // Compaction

    TEST_CASE("almeshsimplifier: compaction keeps each vertex the triangles name, and no other", "[almeshsimplifier]")
    {
        LLPointer<LLVolume> base = volumeOf({ sphere(16, 8), cube(2) });
        LLVolumeFace& rigged = base->getVolumeFace(1);
        rigged.allocateWeights(rigged.mNumVertices);
        for (S32 v = 0; v < rigged.mNumVertices; ++v)
        {
            rigged.mWeights[v].set(1.25f + v % 7, 0.5f, 0.f, 0.f);
        }

        // Every other triangle of each face, numbered face after face, and
        // some that take vertices from both, as a sloppy simplification's
        // may.
        const U32 second = (U32)base->getVolumeFace(0).mNumVertices;
        std::vector<U32> indices;
        for (S32 f = 0; f < 2; ++f)
        {
            const LLVolumeFace& face = base->getVolumeFace(f);
            for (S32 i = 0; i < face.mNumIndices; i += 6)
            {
                for (S32 k = 0; k < 3; ++k)
                {
                    indices.push_back(face.mIndices[i + k] + (f ? second : 0));
                }
            }
        }
        indices.insert(indices.end(), { 3, second + 5, 7, second + 1, 9, second + 2 });

        LLVolumeFace compacted;
        REQUIRE(ALMeshSimplifier::compact(*base, 1, indices.data(), indices.size(), compacted));
        REQUIRE(wellFormed(compacted));
        REQUIRE((size_t)compacted.mNumIndices == indices.size());
        REQUIRE(compacted.mWeights);
        CHECK((size_t)compacted.mNumVertices == std::set<U32>(indices.begin(), indices.end()).size());

        auto from = [&](U32 index) -> std::pair<const LLVolumeFace*, S32>
        {
            return index < second ? std::make_pair(&base->getVolumeFace(0), (S32)index)
                                  : std::make_pair(&base->getVolumeFace(1), (S32)(index - second));
        };
        size_t differ = 0;
        for (size_t i = 0; i < indices.size(); ++i)
        {
            const auto [face, v] = from(indices[i]);
            const S32 c = compacted.mIndices[i];
            differ += memcmp(face->mPositions[v].getF32ptr(), compacted.mPositions[c].getF32ptr(), 12) != 0;
            differ += memcmp(face->mNormals[v].getF32ptr(), compacted.mNormals[c].getF32ptr(), 12) != 0;
            differ += face->mTexCoords[v] != compacted.mTexCoords[c];
            const F32 weight = face->mWeights ? face->mWeights[v][0] : 0.f;
            differ += weight != compacted.mWeights[c][0];
        }
        CHECK(differ == 0);
    }

    // A flat sheet of 600 columns by 200 rows of points as two faces of
    // 60,000 vertices: the first has every other column's points, two
    // columns a quad, and the second every point of the first 300 columns,
    // a column a quad. A sloppy simplification that keeps every triangle
    // takes the first face's vertex where the second's lies on it, so that
    // the second's triangles with their first corner there are the first
    // face's, and name half of the second's vertices besides its own.
    LLPointer<LLVolume> sheetOverColumns()
    {
        constexpr S32 COLUMNS = 600;
        constexpr S32 ROWS = 200;
        auto point = [](S32 column, S32 row, Geometry& g)
        {
            const F32 x = (F32)column / (COLUMNS - 1);
            const F32 y = (F32)row / (ROWS - 1);
            g.add(LLVector3(x - 0.5f, y - 0.5f, 0.f), LLVector3(0.f, 0.f, 1.f), LLVector2(x, y));
        };
        auto grid = [](S32 columns, Geometry& g)
        {
            for (S32 j = 0; j + 1 < ROWS; ++j)
            {
                for (S32 i = 0; i + 1 < columns; ++i)
                {
                    const U16 c0 = (U16)(i + j * columns);
                    const U16 c1 = (U16)(c0 + 1);
                    const U16 c2 = (U16)(c0 + columns);
                    const U16 c3 = (U16)(c2 + 1);
                    g.triangle(c0, c1, c3);
                    g.triangle(c0, c3, c2);
                }
            }
        };
        Geometry first, second;
        for (S32 row = 0; row < ROWS; ++row)
        {
            for (S32 column = 0; column < COLUMNS; column += 2)
            {
                point(column, row, first);
            }
            for (S32 column = 0; column < COLUMNS / 2; ++column)
            {
                point(column, row, second);
            }
        }
        grid(COLUMNS / 2, first);
        grid(COLUMNS / 2, second);
        return volumeOf({ first, second });
    }

    TEST_CASE("almeshsimplifier: simplified together, a face past 16-bit indices makes each face simplify on its own", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        LLPointer<LLVolume> base = sheetOverColumns();
        const ALMeshSimplifier simplifier(*base);
        const ALMeshSimplifier::Target goal = toCount(1.f);

        // The scene: the sheet simplified as one mesh, as simplifyModel
        // simplifies it, leaves the first face's triangles naming more
        // vertices than 16-bit indices reach.
        const LLVolumeFace& first = base->getVolumeFace(0);
        const LLVolumeFace& second = base->getVolumeFace(1);
        std::vector<LLVector4a> positions(first.mPositions, first.mPositions + first.mNumVertices);
        positions.insert(positions.end(), second.mPositions, second.mPositions + second.mNumVertices);
        std::vector<U32> indices(first.mIndices, first.mIndices + first.mNumIndices);
        for (S32 i = 0; i < second.mNumIndices; ++i)
        {
            indices.push_back(second.mIndices[i] + (U32)first.mNumVertices);
        }
        std::vector<U32> together(indices.size());
        const size_t together_count = meshopt_simplifySloppy(together.data(), indices.data(), indices.size(), positions[0].getF32ptr(),
                                                             positions.size(), sizeof(LLVector4a), nullptr, indices.size(), 1.f, nullptr);
        std::set<U32> named;
        for (size_t i = 0; i < together_count; i += 3)
        {
            if (together[i] < (U32)first.mNumVertices)
            {
                named.insert(together.begin() + i, together.begin() + i + 3);
            }
        }
        INFO(fmt::format("simplified as one mesh, the first face's triangles name {} vertices", named.size()));
        REQUIRE(named.size() > U16_MAX);

        LLPointer<LLVolume> target = targetFor(*base);
        REQUIRE(simplifier.simplifyModel(*target, goal, Seams::SLOPPY) > 0.f);
        for (S32 f = 0; f < 2; ++f)
        {
            INFO(fmt::format("face {}", f));
            LLVolumeFace alone;
            REQUIRE(simplifier.simplifyFace(f, alone, goal, Seams::SLOPPY) > 0.f);
            const LLVolumeFace& face = target->getVolumeFace(f);
            REQUIRE(wellFormed(face));
            CHECK(fromBase(face, *base));
            REQUIRE(face.mNumVertices == alone.mNumVertices);
            REQUIRE(face.mNumIndices == alone.mNumIndices);
            CHECK(std::equal(face.mIndices, face.mIndices + face.mNumIndices, alone.mIndices));
            CHECK(memcmp(face.mPositions, alone.mPositions, sizeof(LLVector4a) * face.mNumVertices) == 0);
        }
    }

    // ------------------------------------------------------------------
    // Normals

    // A cube of eight corners and one vertex each, whose sides meet at right
    // angles, and a triangle of no area.
    Geometry cornerCube()
    {
        Geometry g;
        for (S32 corner = 0; corner < 8; ++corner)
        {
            const LLVector3 p((corner & 1) ? 0.5f : -0.5f, (corner & 2) ? 0.5f : -0.5f, (corner & 4) ? 0.5f : -0.5f);
            g.add(p, LLVector3(0.f, 0.f, 1.f), LLVector2(0.f, 0.f));
        }
        const U16 quads[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for (const auto& q : quads)
        {
            g.triangle(q[0], q[1], q[2]);
            g.triangle(q[0], q[2], q[3]);
        }
        g.triangle(0, 1, 1);
        return g;
    }

    LLVector3 faceNormal(const LLVolumeFace& face, S32 i)
    {
        const LLVector3 a = v3(face.mPositions[face.mIndices[i]]);
        LLVector3 n = (v3(face.mPositions[face.mIndices[i + 1]]) - a) % (v3(face.mPositions[face.mIndices[i + 2]]) - a);
        n.normalize();
        return n;
    }

    TEST_CASE("almeshsimplifier: generated normals are hard across a crease and smooth within one", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        LLPointer<LLVolume> base = volumeOf({ cornerCube() });

        LLVolumeFace hard = base->getVolumeFace(0);
        REQUIRE(hard.generateNormals(75.f * DEG_TO_RAD, 0.f));
        REQUIRE(wellFormed(hard));
        CHECK(hard.mNumVertices == 24);
        CHECK(positionsOf(hard) == positionsOf(base->getVolumeFace(0)));
        for (S32 i = 0; i + 3 < hard.mNumIndices; i += 3)
        {
            const LLVector3 n = faceNormal(hard, i);
            for (S32 k = 0; k < 3; ++k)
            {
                CHECK(v3(hard.mNormals[hard.mIndices[i + k]]) * n > 0.9999f);
            }
        }
        // The triangle of no area takes a normal its corners already have.
        const S32 last = hard.mNumIndices - 3;
        for (S32 k = 0; k < 3; ++k)
        {
            CHECK(fabsf(v3(hard.mNormals[hard.mIndices[last + k]]).length() - 1.f) < 1e-4f);
        }

        // Past a right angle, each corner is one vertex whose normal leans
        // out of the cube.
        LLVolumeFace smooth = base->getVolumeFace(0);
        REQUIRE(smooth.generateNormals(100.f * DEG_TO_RAD, 0.f));
        CHECK(smooth.mNumVertices == 8);
        for (S32 v = 0; v < smooth.mNumVertices; ++v)
        {
            const LLVector3 p = v3(smooth.mPositions[v]);
            const LLVector3 n = v3(smooth.mNormals[v]);
            CHECK(n.mV[0] * p.mV[0] > 0.f);
            CHECK(n.mV[1] * p.mV[1] > 0.f);
            CHECK(n.mV[2] * p.mV[2] > 0.f);
        }
    }

    TEST_CASE("almeshsimplifier: a sphere's generated normals point out from its centre", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        LLPointer<LLVolume> base = volumeOf({ sphere(32, 16) });
        LLVolumeFace face = base->getVolumeFace(0);
        REQUIRE(face.generateNormals(75.f * DEG_TO_RAD, 0.f));
        REQUIRE(wellFormed(face));
        // Its seam and its poles are texture seams, and stay so: each vertex
        // a triangle names stays one vertex.
        const LLVolumeFace& before = base->getVolumeFace(0);
        CHECK((size_t)face.mNumVertices == std::set<U16>(before.mIndices, before.mIndices + before.mNumIndices).size());
        F32 worst = 0.f;
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            LLVector3 radial = v3(face.mPositions[v]);
            radial.normalize();
            const LLVector3 n = v3(face.mNormals[v]);
            worst = llmax(worst, acosf(llclamp(n * radial, -1.f, 1.f)) * RAD_TO_DEG);
        }
        // A facet's own normal would be up to 8 degrees off; the corners of
        // the thin triangles around a pole take a degree or two.
        INFO(fmt::format("{} degrees off radial at worst", worst));
        CHECK(worst < 2.f);
    }

    // A roof whose slopes meet at 60 degrees, kept, as the uploader keeps a
    // model, in the unit cube with its size in mNormalizedScale. Squeezed
    // into the cube the slopes meet at 127 degrees, past the crease angle;
    // in the shape it was made in they meet within it, and its ridge is
    // smooth.
    TEST_CASE("almeshsimplifier: the crease angle is taken in the shape the model was made in", "[almeshsimplifier]")
    {
        ArenaInstalled installed;
        constexpr F32 WIDTH = 4.f;                      // eave to eave
        const F32 height = 0.5f * WIDTH * tanf(30.f * DEG_TO_RAD); // each slope 30 degrees off level
        Geometry roof;
        roof.scale = LLVector3(1.f, WIDTH, height);
        for (S32 row = 0; row < 3; ++row)
        {
            const F32 x = 0.5f * row - 0.5f;
            roof.add(LLVector3(x, -0.5f, -0.5f), LLVector3(0.f, 0.f, 1.f), LLVector2(0.f, 0.f));
            roof.add(LLVector3(x, 0.f, 0.5f), LLVector3(0.f, 0.f, 1.f), LLVector2(0.f, 0.f));
            roof.add(LLVector3(x, 0.5f, -0.5f), LLVector3(0.f, 0.f, 1.f), LLVector2(0.f, 0.f));
        }
        for (S32 row = 0; row < 2; ++row)
        {
            for (S32 slope = 0; slope < 2; ++slope)
            {
                const U16 a = (U16)(row * 3 + slope);
                roof.triangle(a, (U16)(a + 3), (U16)(a + 4));
                roof.triangle(a, (U16)(a + 4), (U16)(a + 1));
            }
        }
        LLPointer<LLVolume> base = volumeOf({ roof });
        LLVolumeFace face = base->getVolumeFace(0);
        REQUIRE(face.generateNormals(75.f * DEG_TO_RAD, 0.f));
        CHECK(face.mNumVertices == 9);

        // A slope's normal as the face keeps it is its own, made in the
        // authored shape, times the scale.
        LLVector3 slope_normal(0.f, -sinf(30.f * DEG_TO_RAD), cosf(30.f * DEG_TO_RAD));
        slope_normal.scaleVec(roof.scale);
        slope_normal.normalize();
        LLVector3 other_slope = slope_normal;
        other_slope.mV[1] = -other_slope.mV[1];
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const LLVector3 p = v3(face.mPositions[v]);
            const LLVector3 n = v3(face.mNormals[v]);
            if (p.mV[2] > 0.f)
            {
                // The ridge's normals lie between the slopes'.
                CHECK(n * slope_normal > 0.f);
                CHECK(n * other_slope > 0.f);
                CHECK(n.mV[2] > 0.9f);
            }
            else
            {
                // An eave's is its slope's own.
                CHECK(n * (p.mV[1] < 0.f ? slope_normal : other_slope) > 0.9999f);
            }
        }
    }

    // ------------------------------------------------------------------
    // The real mesh corpus

    struct Tally
    {
        size_t simplified = 0;
        size_t failed = 0;
        size_t short_of_target = 0;   // more than the drift over its target
        size_t malformed = 0;         // an index past its vertices, or no whole triangles
        size_t foreign = 0;           // a vertex not one of the base's
        size_t crossed = 0;           // a face holding another's vertex
        F64 reached = 0.0;            // the sum of each simplification's triangles over its target's
    };

    // The real mesh corpus (almeshcorpus.h), where AL_MESH_CORPUS_DIR names
    // one: each mesh's high LOD simplified to the uploader's three lower
    // levels in each mode that keeps the borders between faces, and its
    // normals made again, keep every face well formed and every vertex one
    // of the base's, and each face's vertices its own. How often each mode
    // stops short of its target is printed, and is what the uploader's Auto
    // mode works through.
    TEST_CASE("almeshsimplifier: the real mesh corpus simplifies into well formed faces", "[almeshsimplifier][corpus][.]")
    {
        std::optional<ALMeshCorpus> real = ALMeshCorpus::fromEnvironment();
        if (!real)
        {
            SKIP("AL_MESH_CORPUS_DIR names no mesh cache");
        }
        ArenaInstalled installed;
        std::map<std::pair<S32, F32>, Tally> tallies;
        size_t meshes = 0, normals_made = 0, normals_kept = 0, normals_bad = 0;
        real->forEachLOD(
            [&](const CorpusLOD& lod)
            {
                if (lod.lod != 3)
                {
                    return;
                }
                const LLVolume& base = *lod.volume;
                if (triangleCount(base) < 3)
                {
                    return;
                }
                ++meshes;

                // Each face's texture coordinates as a sign of whose vertex it is.
                std::vector<std::set<std::vector<U32>>> own(base.getNumVolumeFaces());
                for (S32 f = 0; f < base.getNumVolumeFaces(); ++f)
                {
                    const LLVolumeFace& face = base.getVolumeFace(f);
                    for (S32 v = 0; v < face.mNumVertices; ++v)
                    {
                        std::vector<U32> bits(8);
                        std::memcpy(&bits[0], face.mPositions[v].getF32ptr(), 12);
                        std::memcpy(&bits[3], face.mNormals[v].getF32ptr(), 12);
                        std::memcpy(&bits[6], face.mTexCoords[v].mV, 8);
                        own[f].insert(bits);
                    }
                }

                for (Seams seams : { Seams::ALL, Seams::UV, Seams::NONE })
                {
                    for (F32 decimator : DECIMATORS)
                    {
                        Tally& tally = tallies[{ (S32)seams, decimator }];
                        LLPointer<LLVolume> target = targetFor(base);
                        const F32 ratio = ALMeshSimplifier(base).simplifyModel(*target, toCount(decimator), seams);
                        if (ratio < 0.f)
                        {
                            ++tally.failed;
                            continue;
                        }
                        ++tally.simplified;
                        tally.short_of_target += ratio * ALLOWED_DRIFT < decimator;
                        tally.reached += decimator / ratio;
                        for (S32 f = 0; f < target->getNumVolumeFaces(); ++f)
                        {
                            const LLVolumeFace& face = target->getVolumeFace(f);
                            if (!wellFormed(face))
                            {
                                ++tally.malformed;
                                continue;
                            }
                            if (face.mNumVertices == 1)
                            {
                                continue;
                            }
                            bool foreign = false, crossed = false;
                            for (S32 v = 0; v < face.mNumVertices; ++v)
                            {
                                std::vector<U32> bits(8);
                                std::memcpy(&bits[0], face.mPositions[v].getF32ptr(), 12);
                                std::memcpy(&bits[3], face.mNormals[v].getF32ptr(), 12);
                                std::memcpy(&bits[6], face.mTexCoords[v].mV, 8);
                                if (!own[f].count(bits))
                                {
                                    const bool anywhere =
                                        std::any_of(own.begin(), own.end(), [&](const auto& set) { return set.count(bits) != 0; });
                                    foreign |= !anywhere;
                                    crossed |= anywhere;
                                }
                            }
                            tally.foreign += foreign;
                            tally.crossed += crossed;
                        }
                    }
                }

                for (S32 f = 0; f < base.getNumVolumeFaces(); ++f)
                {
                    LLVolumeFace face = base.getVolumeFace(f);
                    if (face.mNumIndices < 3)
                    {
                        continue;
                    }
                    if (!face.generateNormals(75.f * DEG_TO_RAD, 0.f))
                    {
                        ++normals_kept;
                        continue;
                    }
                    ++normals_made;
                    const std::set<PositionKey> before = positionsOf(base.getVolumeFace(f));
                    bool bad = !wellFormed(face) || positionsOf(face) != before;
                    for (S32 v = 0; v < face.mNumVertices && !bad; ++v)
                    {
                        const F32 length = v3(face.mNormals[v]).length();
                        bad = !(fabsf(length - 1.f) < 1e-3f);
                    }
                    normals_bad += bad;
                }
            });

        INFO(real->describe());
        std::string table = fmt::format("{} meshes' high LODs\n", meshes);
        size_t problems = 0;
        for (const auto& [key, tally] : tallies)
        {
            table += fmt::format("  {:<18} a {:>2}th: {} simplified, {} failed, {} short of the target, "
                                 "triangles at {:.2f} of the target on average; "
                                 "{} malformed, {} with a foreign vertex, {} with another face's\n",
                                 nameOf((Seams)key.first), key.second, tally.simplified, tally.failed, tally.short_of_target,
                                 tally.simplified ? tally.reached / tally.simplified : 0.0, tally.malformed, tally.foreign, tally.crossed);
            problems += tally.malformed + tally.foreign + tally.crossed;
        }
        table += fmt::format("normals: {} faces made again, {} kept, {} with a bad normal or a moved position\n", normals_made,
                             normals_kept, normals_bad);
        INFO(table);
        CHECK(meshes > 0);
        CHECK(problems == 0);
        CHECK(normals_bad == 0);
        WARN(table);
    }
}
