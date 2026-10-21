/**
 * @file llvolume_bench.cpp
 * @brief What generating a prim or sculpt, its tangents, a flexible frame,
 *        a mesh LOD's cacheOptimize and each index order cost, and how many
 *        blocks meshoptimizer allocates a call.
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

// Each benchmark is one call of what the main thread or a mesh thread does
// once: a volume built from its parameters, a sculpt from its map, one
// face's tangents, one flexible frame, one LOD's cacheOptimize, one face's
// index order. A row differs from the row beside it in one thing. The
// figures that are not times, an index order's vertices transformed a
// triangle and meshoptimizer's allocations a call, are printed as a table
// by the case that measures them.
//
// The output is a table, not a verdict; a number is read against the row
// next to it from the same run on the same quiet machine.

#include "linden_common.h"

#include "llvolume.h"
#include "llmath.h"
#include "llquaternion.h"
#include "m3math.h"
#include "v2math.h"
#include "v3math.h"
#include "llvector4a.h"

#include "altestbenchmark.h"

#include <fmt/format.h>
#include <meshoptimizer.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{
    // LLVolumeLODGroup's detail scales, lowest LOD first.
    constexpr F32 LOD_DETAIL[] = { 1.f, 1.5f, 2.5f, 4.f };
    constexpr S32 LOD_COUNT = 4;

    // ------------------------------------------------------------------
    // Prims

    struct Prim
    {
        const char* name;
        LLVolumeParams params;
    };

    LLVolumeParams primParams(U8 profile, U8 path, F32 hollow = 0.f, F32 twist = 0.f)
    {
        LLVolumeParams p;
        p.setType(profile, path);
        p.setBeginAndEndS(0.f, 1.f);
        p.setBeginAndEndT(0.f, 1.f);
        p.setHollow(hollow);
        p.setTwistBegin(-twist);
        p.setTwistEnd(twist);
        // The hole size LLToolPlacer gives a new torus.
        p.setRatio(1.f, path == LL_PCODE_PATH_CIRCLE && profile != LL_PCODE_PROFILE_CIRCLE_HALF ? 0.25f : 1.f);
        return p;
    }

    std::vector<Prim> primClasses()
    {
        return {
            { "box", primParams(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE) },
            { "cylinder", primParams(LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_LINE) },
            { "hollow box", primParams((U8)(LL_PCODE_PROFILE_SQUARE | LL_PCODE_HOLE_SQUARE), LL_PCODE_PATH_LINE, 0.5f) },
            { "sphere", primParams(LL_PCODE_PROFILE_CIRCLE_HALF, LL_PCODE_PATH_CIRCLE) },
            { "torus", primParams(LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_CIRCLE) },
            { "twisted box", primParams(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE, 0.f, 0.5f) },
        };
    }

    std::string counts(const LLVolume& volume)
    {
        S32 vertices = 0;
        const S32 triangles = volume.getNumTriangles(&vertices);
        return fmt::format("{} vertices, {} triangles", vertices, triangles);
    }

    // ------------------------------------------------------------------
    // Sculpts

    struct SculptMap
    {
        U16 width = 64;
        U16 height = 64;
        std::vector<U8> rgb;
    };

    U8 toByte(F32 value)
    {
        return (U8)llclamp(ll_round((value + 0.5f) * 255.f), 0, 255);
    }

    // A 64 by 64 map of each stitching's own surface, as the sculpt
    // sampler reads it.
    SculptMap sculptMap(U8 stitching)
    {
        SculptMap map;
        map.rgb.resize((size_t)map.width * map.height * 3);
        for (U16 y = 0; y < map.height; ++y)
        {
            for (U16 x = 0; x < map.width; ++x)
            {
                const F32 u = (F32)x / map.width;
                const F32 v = (F32)y / (map.height - 1);
                LLVector3 p;
                switch (stitching)
                {
                    case LL_SCULPT_TYPE_SPHERE:
                        p.set(0.45f * sinf(F_PI * v) * cosf(F_TWO_PI * u), -0.45f * sinf(F_PI * v) * sinf(F_TWO_PI * u), 0.45f * cosf(F_PI * v));
                        break;
                    case LL_SCULPT_TYPE_TORUS:
                    {
                        const F32 r = 0.3f + 0.15f * cosf(F_TWO_PI * y / map.height);
                        p.set(r * cosf(F_TWO_PI * u), -r * sinf(F_TWO_PI * u), 0.15f * sinf(F_TWO_PI * y / map.height));
                        break;
                    }
                    case LL_SCULPT_TYPE_CYLINDER:
                        p.set(0.4f * cosf(F_TWO_PI * u), -0.4f * sinf(F_TWO_PI * u), v - 0.5f);
                        break;
                    default:
                        p.set((F32)x / (map.width - 1) - 0.5f, v - 0.5f, 0.2f * sinf(F_TWO_PI * u) * cosf(F_PI * v));
                        break;
                }
                U8* out = &map.rgb[((size_t)y * map.width + x) * 3];
                out[0] = toByte(p.mV[0]);
                out[1] = toByte(p.mV[1]);
                out[2] = toByte(p.mV[2]);
            }
        }
        return map;
    }

    LLPointer<LLVolume> sculptVolume(U8 stitching, S32 lod)
    {
        LLVolumeParams p;
        p.setType(LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_CIRCLE);
        p.setRatio(1.f, 0.5f);
        p.setSculptID(LLUUID("c8b26d0b-6f5a-4f10-8d1e-5c3b3e4a9a01"), stitching);
        return new LLVolume(p, LOD_DETAIL[lod]);
    }

    struct Named
    {
        const char* name;
        U8 type;
    };
    constexpr Named STITCHINGS[] = {
        { "sphere", LL_SCULPT_TYPE_SPHERE },
        { "torus", LL_SCULPT_TYPE_TORUS },
        { "plane", LL_SCULPT_TYPE_PLANE },
        { "cylinder", LL_SCULPT_TYPE_CYLINDER },
    };

    // ------------------------------------------------------------------
    // The mesh corpus: faces as a decoded LOD hands them to cacheOptimize.

    struct CorpusMesh
    {
        std::string name;
        std::vector<LLVector3> positions;
        std::vector<LLVector3> normals;
        std::vector<LLVector2> uvs;
        std::vector<LLVector4> weights;
        std::vector<U16> indices;

        LLVolumeFace face() const
        {
            LLVolumeFace f;
            f.resizeVertices((S32)positions.size());
            f.resizeIndices((S32)indices.size());
            for (size_t v = 0; v < positions.size(); ++v)
            {
                f.mPositions[v].load3(positions[v].mV);
                f.mNormals[v].load3(normals[v].mV);
                f.mTexCoords[v] = uvs[v];
            }
            std::copy(indices.begin(), indices.end(), f.mIndices);
            if (!weights.empty())
            {
                f.allocateWeights((S32)weights.size());
                for (size_t v = 0; v < weights.size(); ++v)
                {
                    f.mWeights[v].loadua(weights[v].mV);
                }
            }
            return f;
        }
    };

    // A latitude-longitude sphere with its seam column twice and each pole
    // a row of points, as an exporter writes one; rigged, two joints a
    // vertex.
    CorpusMesh uvSphere(S32 columns, S32 rows, bool rigged)
    {
        CorpusMesh mesh;
        mesh.name = fmt::format("{}UV sphere {} by {}", rigged ? "rigged " : "", columns, rows);
        for (S32 r = 0; r <= rows; ++r)
        {
            for (S32 c = 0; c <= columns; ++c)
            {
                const F32 theta = F_PI * r / rows;
                const F32 phi = F_TWO_PI * c / columns;
                const LLVector3 n(sinf(theta) * cosf(phi), sinf(theta) * sinf(phi), cosf(theta));
                mesh.positions.push_back(n * 0.5f);
                mesh.normals.push_back(n);
                mesh.uvs.emplace_back((F32)c / columns, 1.f - (F32)r / rows);
                if (rigged)
                {
                    const size_t v = mesh.positions.size();
                    const F32 w = 0.25f + 0.25f * (v % 3);
                    mesh.weights.emplace_back((F32)(v % 5) + w, (F32)(5 + (v + 1) % 5) + (1.f - w), 0.f, 0.f);
                }
            }
        }
        auto at = [&](S32 c, S32 r) { return (U16)(c + r * (columns + 1)); };
        for (S32 r = 0; r < rows; ++r)
        {
            for (S32 c = 0; c < columns; ++c)
            {
                if (r > 0)
                {
                    mesh.indices.insert(mesh.indices.end(), { at(c, r), at(c, r + 1), at(c + 1, r) });
                }
                if (r < rows - 1)
                {
                    mesh.indices.insert(mesh.indices.end(), { at(c + 1, r), at(c, r + 1), at(c + 1, r + 1) });
                }
            }
        }
        return mesh;
    }

    // Six faces of four vertices, each with its own normal and UVs.
    CorpusMesh splitCube()
    {
        CorpusMesh mesh;
        mesh.name = "cube with split normals";
        for (S32 axis = 0; axis < 3; ++axis)
        {
            for (F32 sign : { 1.f, -1.f })
            {
                LLVector3 n, a;
                n.mV[axis] = sign;
                a.mV[(axis + 1) % 3] = 1.f;
                const LLVector3 b = n % a;
                const U16 base = (U16)mesh.positions.size();
                const F32 corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
                for (const auto& corner : corners)
                {
                    mesh.positions.push_back((n + a * corner[0] + b * corner[1]) * 0.5f);
                    mesh.normals.push_back(n);
                    mesh.uvs.emplace_back(corner[0] * 0.5f + 0.5f, corner[1] * 0.5f + 0.5f);
                }
                mesh.indices.insert(mesh.indices.end(), { base, (U16)(base + 1), (U16)(base + 2), base, (U16)(base + 2), (U16)(base + 3) });
            }
        }
        return mesh;
    }

    std::vector<CorpusMesh> corpus()
    {
        return { uvSphere(24, 12, false), splitCube(), uvSphere(16, 8, true), uvSphere(64, 32, false), uvSphere(128, 64, false) };
    }

    // ------------------------------------------------------------------
    // Flexible prims, as LLVolumeImplFlexible keeps one: unique, on a
    // dynamic path the rebuild sizes and fills section by section.

    LLPointer<LLVolume> flexiVolume(U8 profile, S32 lod)
    {
        LLVolumeParams p;
        p.setType(profile, LL_PCODE_PATH_FLEXIBLE);
        return new LLVolume(p, LOD_DETAIL[lod], false, true);
    }

    // A unit length bent into an arc of the given angle about Y.
    void bendPath(LLVolume& volume, S32 sections, F32 bend)
    {
        LLPath& path = volume.getPath();
        if (path.getPathLength() != sections + 1)
        {
            volume.resizePath(sections + 1);
        }
        for (S32 i = 0; i <= sections; ++i)
        {
            const F32 t = (F32)i / sections;
            const F32 a = bend * t;
            const LLVector3 pos((1.f - cosf(a)) / bend, 0.f, sinf(a) / bend - 0.5f);
            LLPath::PathPt& point = path.mPath[i];
            point.mPos.load3(pos.mV);
            point.mRot.set(LLMatrix3(LLQuaternion(a, LLVector3(0.f, 1.f, 0.f))));
            point.mScale.set(1.f, 1.f, 0.f, 1.f);
            point.mTexT = t;
        }
    }

    void genAllTangents(LLVolume& volume)
    {
        for (S32 i = 0; i < volume.getNumVolumeFaces(); ++i)
        {
            volume.genTangents(i);
        }
    }

    // ------------------------------------------------------------------
    // One face's tangent inputs as meshoptimizer takes them: 32-bit
    // indices, unit normals, the face's own strides.

    struct TangentInputs
    {
        std::vector<U32> indices;
        std::vector<LLVector4a> normals;
        std::vector<F32> corners;
        std::vector<LLVector4a> tangents;

        explicit TangentInputs(const LLVolumeFace& face)
            : indices(face.mIndices, face.mIndices + face.mNumIndices),
              normals(face.mNormals, face.mNormals + face.mNumVertices),
              corners(face.mNumIndices * 4),
              tangents(face.mNumVertices)
        {
            for (LLVector4a& n : normals)
            {
                if (n.dot3(n).getF32() > 0.f)
                {
                    n.normalize3();
                }
            }
        }

        // Each vertex takes its corners' tangent, as a prim's grid would.
        F32 generate(const LLVolumeFace& face, unsigned int options)
        {
            meshopt_generateTangents(corners.data(), indices.data(), indices.size(), face.mPositions[0].getF32ptr(), face.mNumVertices,
                                     sizeof(LLVector4a), normals[0].getF32ptr(), sizeof(LLVector4a), face.mTexCoords[0].mV, sizeof(LLVector2), options);
            for (size_t c = 0; c < indices.size(); ++c)
            {
                tangents[indices[c]].loadua(&corners[c * 4]);
            }
            return tangents[0][0];
        }
    };

    struct FaceClass
    {
        std::string name;
        LLPointer<LLVolume> volume;
        S32 face;
    };

    // The faces whose tangents the main thread makes: prim sides and caps
    // of each kind, a sculpt, and a mesh LOD face as a large one.
    std::vector<FaceClass> faceClasses()
    {
        std::vector<FaceClass> out;
        for (const Prim& prim : primClasses())
        {
            LLPointer<LLVolume> volume = new LLVolume(prim.params, LOD_DETAIL[3]);
            for (S32 i = 0; i < volume->getNumVolumeFaces(); ++i)
            {
                const LLVolumeFace& face = volume->getVolumeFace(i);
                const bool cap = face.mTypeMask & LLVolumeFace::CAP_MASK;
                // One side and, where there is one, one cap of each prim.
                const bool first_side = !cap && (i == 0 || (volume->getVolumeFace(i - 1).mTypeMask & LLVolumeFace::CAP_MASK));
                const bool first_cap = cap && i == 0;
                if (first_side || first_cap)
                {
                    out.push_back({ fmt::format("{} {} at LOD 3, {} vertices", prim.name, cap ? "cap" : "side", face.mNumVertices), volume, i });
                }
            }
        }
        for (const Named& stitching : { STITCHINGS[0], STITCHINGS[2] })
        {
            LLPointer<LLVolume> volume = sculptVolume(stitching.type, 3);
            const SculptMap map = sculptMap(stitching.type);
            volume->sculpt(map.width, map.height, 3, map.rgb.data(), 0, false);
            out.push_back({ fmt::format("{} sculpt at LOD 3, {} vertices", stitching.name, volume->getVolumeFace(0).mNumVertices), volume, 0 });
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Index orders over a grid of columns by rows, as createSide lays its
    // vertices out: s + columns * t.

    void emitQuad(std::vector<U32>& out, U32 columns, U32 s, U32 t)
    {
        out.push_back(s + columns * t);
        out.push_back(s + 1 + columns * (t + 1));
        out.push_back(s + columns * (t + 1));
        out.push_back(s + columns * t);
        out.push_back(s + 1 + columns * t);
        out.push_back(s + 1 + columns * (t + 1));
    }

    // createSide's order: every quad of a row, row after row.
    void rowMajor(std::vector<U32>& out, U32 columns, U32 rows)
    {
        out.clear();
        for (U32 t = 0; t + 1 < rows; ++t)
        {
            for (U32 s = 0; s + 1 < columns; ++s)
            {
                emitQuad(out, columns, s, t);
            }
        }
    }

    // Bands of BAND quads across, each band row by row before the next: a
    // band's row reuses the BAND + 1 vertices the row before it loaded,
    // and two rows of a band fit a 16-entry cache.
    constexpr U32 BAND = 7;

    void banded(std::vector<U32>& out, U32 columns, U32 rows)
    {
        out.clear();
        for (U32 first = 0; first + 1 < columns; first += BAND)
        {
            const U32 last = llmin(first + BAND, columns - 1);
            for (U32 t = 0; t + 1 < rows; ++t)
            {
                for (U32 s = first; s < last; ++s)
                {
                    emitQuad(out, columns, s, t);
                }
            }
        }
    }

    F64 transformedPerTriangle(const std::vector<U32>& indices, U32 vertices)
    {
        return meshopt_analyzeVertexCache(indices.data(), indices.size(), vertices, 16, 0, 0).acmr;
    }

    struct Grid
    {
        std::string name;
        U32 columns;
        U32 rows;
    };

    // The sweep's grids of more than two rows, from the prims and sculpts
    // that have them.
    std::vector<Grid> sweepGrids()
    {
        std::vector<Grid> out;
        for (const Prim& prim : primClasses())
        {
            for (S32 lod = 2; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = new LLVolume(prim.params, LOD_DETAIL[lod]);
                for (S32 i = 0; i < volume->getNumVolumeFaces(); ++i)
                {
                    const LLVolumeFace& face = volume->getVolumeFace(i);
                    if (!(face.mTypeMask & LLVolumeFace::CAP_MASK) && face.mNumT > 2)
                    {
                        out.push_back({ fmt::format("{} side at LOD {}, {} by {}", prim.name, lod, face.mNumS, face.mNumT), (U32)face.mNumS,
                                        (U32)face.mNumT });
                        break;
                    }
                }
            }
        }
        LLPointer<LLVolume> sculpted = sculptVolume(LL_SCULPT_TYPE_SPHERE, 3);
        const SculptMap map = sculptMap(LL_SCULPT_TYPE_SPHERE);
        sculpted->sculpt(map.width, map.height, 3, map.rgb.data(), 0, false);
        const LLVolumeFace& face = sculpted->getVolumeFace(0);
        out.push_back({ fmt::format("sculpt at LOD 3, {} by {}", face.mNumS, face.mNumT), (U32)face.mNumS, (U32)face.mNumT });
        return out;
    }

    // ------------------------------------------------------------------
    // meshoptimizer's allocations, counted through its allocator hook.

    struct Allocations
    {
        static inline size_t blocks = 0;
        static inline size_t bytes = 0;

        static void* MESHOPTIMIZER_ALLOC_CALLCONV allocate(size_t size)
        {
            ++blocks;
            bytes += size;
            return ::operator new(size);
        }

        static void MESHOPTIMIZER_ALLOC_CALLCONV deallocate(void* p)
        {
            ::operator delete(p);
        }

        // meshoptimizer's own default: operator new and operator delete.
        static void* MESHOPTIMIZER_ALLOC_CALLCONV defaultAllocate(size_t size)
        {
            return ::operator new(size);
        }

        static void MESHOPTIMIZER_ALLOC_CALLCONV defaultDeallocate(void* p)
        {
            ::operator delete(p);
        }
    };

    // Counts while it lives, and puts meshoptimizer's default back however
    // the case ends.
    struct CountAllocations
    {
        CountAllocations()
        {
            meshopt_setAllocator(Allocations::allocate, Allocations::deallocate);
        }

        ~CountAllocations()
        {
            meshopt_setAllocator(Allocations::defaultAllocate, Allocations::defaultDeallocate);
        }

        template <typename F>
        void row(const std::string& name, F&& call)
        {
            Allocations::blocks = 0;
            Allocations::bytes = 0;
            call();
            std::printf("  %-72s %4zu blocks %9zu bytes\n", name.c_str(), Allocations::blocks, Allocations::bytes);
        }
    };

    // ------------------------------------------------------------------

    TEST_CASE("llvolume_bench: a prim volume of each class built from its parameters", "[llvolume_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        for (const Prim& prim : primClasses())
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                const LLVolumeParams params = prim.params;
                const F32 detail = LOD_DETAIL[lod];
                LLPointer<LLVolume> sample = new LLVolume(params, detail);
                BENCHMARK(fmt::format("{}, LOD {}, {}", prim.name, lod, counts(*sample)))
                {
                    LLPointer<LLVolume> volume = new LLVolume(params, detail);
                    return volume->getNumVolumeFaces();
                };
            }
        }
    }

    TEST_CASE("llvolume_bench: a sculpt of each stitching built from its map", "[llvolume_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        for (const Named& stitching : STITCHINGS)
        {
            const SculptMap map = sculptMap(stitching.type);
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = sculptVolume(stitching.type, lod);
                volume->sculpt(map.width, map.height, 3, map.rgb.data(), 0, false);
                BENCHMARK(fmt::format("{} sculpt, LOD {}, 64 by 64 map, {}", stitching.name, lod, counts(*volume)))
                {
                    volume->sculpt(map.width, map.height, 3, map.rgb.data(), 0, false);
                    return volume->getNumVolumeFaces();
                };
            }
        }
    }

    // createTangents is Lengyel's generator; meshoptimizer's default mode
    // weights by angle times the edges' lengths, its compatible mode by
    // angle alone, as MikkTSpace does. Each row makes one face's tangents
    // a vertex.
    TEST_CASE("llvolume_bench: one face's tangents from each generator", "[llvolume_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        for (const FaceClass& face_class : faceClasses())
        {
            LLVolumeFace& face = face_class.volume->getVolumeFace(face_class.face);
            BENCHMARK(fmt::format("Lengyel (createTangents), {}", face_class.name))
            {
                ll_aligned_free_16(face.mTangents);
                face.mTangents = nullptr;
                face.createTangents();
                return face.mTangents[0][0];
            };
            TangentInputs inputs(face);
            BENCHMARK(fmt::format("meshoptimizer default, {}", face_class.name))
            {
                return inputs.generate(face, 0);
            };
            BENCHMARK(fmt::format("meshoptimizer compatible, {}", face_class.name))
            {
                return inputs.generate(face, meshopt_TangentCompatible);
            };
        }
        for (const CorpusMesh& mesh : corpus())
        {
            LLVolumeFace face = mesh.face();
            const std::string name = fmt::format("{}, {} vertices", mesh.name, face.mNumVertices);
            BENCHMARK(fmt::format("Lengyel (createTangents), {}", name))
            {
                ll_aligned_free_16(face.mTangents);
                face.mTangents = nullptr;
                face.createTangents();
                return face.mTangents[0][0];
            };
            TangentInputs inputs(face);
            BENCHMARK(fmt::format("meshoptimizer default, {}", name))
            {
                return inputs.generate(face, 0);
            };
            BENCHMARK(fmt::format("meshoptimizer compatible, {}", name))
            {
                return inputs.generate(face, meshopt_TangentCompatible);
            };
        }
    }

    // A flexible prim's frame as LLVolumeImplFlexible runs it: the path's
    // sections written, the volume rebuilt in place, and the tangents of
    // every face taken again where a face lost them. Each call bends the
    // path the other way.
    TEST_CASE("llvolume_bench: one flexible frame", "[llvolume_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        const Named profiles[] = { { "box", LL_PCODE_PROFILE_SQUARE }, { "cylinder", LL_PCODE_PROFILE_CIRCLE } };
        for (const Named& profile : profiles)
        {
            for (S32 sections : { 4, 16 })
            {
                for (S32 lod : { 1, 3 })
                {
                    LLPointer<LLVolume> volume = flexiVolume(profile.type, lod);
                    bendPath(*volume, sections, 0.4f);
                    volume->regen();
                    genAllTangents(*volume);
                    U32 frame = 0;
                    BENCHMARK(fmt::format("flexible {}, {} sections, LOD {}, {}", profile.name, sections, lod, counts(*volume)))
                    {
                        bendPath(*volume, sections, (++frame & 1) ? 1.1f : 0.4f);
                        volume->regen();
                        genAllTangents(*volume);
                        return volume->getVolumeFace(0).mNumVertices;
                    };
                }
            }
        }
    }

    // cacheOptimize as a mesh thread runs it on each decoded LOD: tangents,
    // the weld, and the cache order. Each call takes a fresh copy of the
    // decoded face, made outside the measurement.
    TEST_CASE("llvolume_bench: cacheOptimize over the mesh corpus", "[llvolume_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        std::vector<LLVolumeFace> all;
        for (const CorpusMesh& mesh : corpus())
        {
            const LLVolumeFace decoded = mesh.face();
            all.push_back(decoded);
            BENCHMARK_ADVANCED(fmt::format("cacheOptimize, {}, {} vertices, {} triangles", mesh.name, decoded.mNumVertices, decoded.mNumIndices / 3))
            (Catch::Benchmark::Chronometer meter)
            {
                std::vector<LLVolumeFace> faces(meter.runs(), decoded);
                meter.measure([&](int i) { return faces[i].cacheOptimize(true); });
            };
        }
        BENCHMARK_ADVANCED(fmt::format("cacheOptimize, the whole corpus, {} faces", all.size()))(Catch::Benchmark::Chronometer meter)
        {
            std::vector<std::vector<LLVolumeFace>> runs(meter.runs(), all);
            meter.measure(
                [&](int i)
                {
                    bool ok = true;
                    for (LLVolumeFace& face : runs[i])
                    {
                        ok &= face.cacheOptimize(true);
                    }
                    return ok;
                });
        };
    }

    // Each grid's index order three ways: createSide's rows, bands of seven
    // columns made the same way, and meshoptimizer's cache order over the
    // rows. The vertices each transforms a triangle in a 16-entry cache are
    // printed first.
    TEST_CASE("llvolume_bench: a grid's index order", "[llvolume_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        const std::vector<Grid> grids = sweepGrids();
        std::printf("vertices transformed a triangle, 16-entry cache\n  %-40s %10s %10s %13s\n", "grid", "row-major", "banded", "meshoptimizer");
        for (const Grid& grid : grids)
        {
            std::vector<U32> rows, bands, optimized;
            rowMajor(rows, grid.columns, grid.rows);
            banded(bands, grid.columns, grid.rows);
            optimized.resize(rows.size());
            meshopt_optimizeVertexCache(optimized.data(), rows.data(), rows.size(), grid.columns * grid.rows);
            std::printf("  %-40s %10.3f %10.3f %13.3f\n", grid.name.c_str(), transformedPerTriangle(rows, grid.columns * grid.rows),
                        transformedPerTriangle(bands, grid.columns * grid.rows), transformedPerTriangle(optimized, grid.columns * grid.rows));
        }
        std::fflush(stdout);

        for (const Grid& grid : grids)
        {
            std::vector<U32> out;
            out.reserve((grid.columns - 1) * (grid.rows - 1) * 6);
            BENCHMARK(fmt::format("row-major, {}", grid.name))
            {
                rowMajor(out, grid.columns, grid.rows);
                return out.back();
            };
            BENCHMARK(fmt::format("banded, {}", grid.name))
            {
                banded(out, grid.columns, grid.rows);
                return out.back();
            };
            std::vector<U32> rows;
            rowMajor(rows, grid.columns, grid.rows);
            std::vector<U32> optimized(rows.size());
            BENCHMARK(fmt::format("meshoptimizer over row-major, {}", grid.name))
            {
                meshopt_optimizeVertexCache(optimized.data(), rows.data(), rows.size(), grid.columns * grid.rows);
                return optimized.back();
            };
        }
    }

    // The blocks each meshoptimizer call takes from its allocator, counted
    // through meshopt_setAllocator, and what cacheOptimize's calls take
    // together.
    TEST_CASE("llvolume_bench: meshoptimizer's allocations a call", "[llvolume_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        std::printf("meshoptimizer allocations a call\n");
        size_t blocks = 0;
        {
            CountAllocations count;
            for (const FaceClass& face_class : faceClasses())
            {
                const LLVolumeFace& face = face_class.volume->getVolumeFace(face_class.face);
                TangentInputs inputs(face);
                count.row(fmt::format("generateTangents default, {}", face_class.name), [&] { inputs.generate(face, 0); });
                count.row(fmt::format("generateTangents compatible, {}", face_class.name), [&] { inputs.generate(face, meshopt_TangentCompatible); });
                std::vector<U32> optimized(inputs.indices.size());
                count.row(fmt::format("optimizeVertexCache, {}", face_class.name),
                          [&] { meshopt_optimizeVertexCache(optimized.data(), inputs.indices.data(), inputs.indices.size(), face.mNumVertices); });
                std::vector<F32> normals(inputs.indices.size() * 3);
                count.row(fmt::format("generateNormals, {}", face_class.name),
                          [&]
                          {
                              meshopt_generateNormals(normals.data(), inputs.indices.data(), inputs.indices.size(), face.mPositions[0].getF32ptr(),
                                                      face.mNumVertices, sizeof(LLVector4a), F_PI * 0.999f, 0.f);
                          });
            }
            for (const CorpusMesh& mesh : corpus())
            {
                LLVolumeFace face = mesh.face();
                count.row(fmt::format("cacheOptimize, {}, {} vertices", mesh.name, face.mNumVertices), [&] { face.cacheOptimize(true); });
                blocks += Allocations::blocks;
            }
        }
        std::fflush(stdout);
        CHECK(blocks > 0);
    }
}
