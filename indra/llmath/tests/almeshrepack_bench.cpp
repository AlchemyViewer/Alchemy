/**
 * @file almeshrepack_bench.cpp
 * @brief What welding, dropping empty triangles and reordering a mesh LOD
 *        block's quantized vertices and triangles do to its zipped size,
 *        over the real mesh corpus.
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

// Each LOD block of the sample (almeshcorpus.h) is repacked several ways at
// the values it holds, sixteen bits a component, and zipped by
// zip_llsd_model. Every way but those marked lossy keeps the triangles the
// block draws: a vertex the same in every array as another is one vertex, a
// triangle with no area is dropped, and the rest only moves vertices and
// triangles about, and influences within a vertex, which the viewer sums.
// The case checks that each lossless way keeps every triangle with area.

#include "linden_common.h"

#include "almeshfacepack.h"
#include "llsdserialize.h"
#include "llsdutil.h"

#include "altestbenchmark.h"
#include "almeshcorpus.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr S32 REPACK_LODS = 4;

    // A vertex's influences as a key: their count, then up to four joints
    // and their weights' two bytes each, zero past the count.
    using Influences = std::array<U8, 13>;

    // One face of a LOD block at its quantized values, as LLModel::writeModel
    // writes them and LLVolume::decodeVolumeFaces reads them.
    struct QuantizedFace
    {
        size_t vertices = 0;
        std::vector<U16> positions;  // three a vertex
        std::vector<U16> normals;    // three a vertex
        std::vector<U16> tex_coords; // two a vertex
        std::vector<Influences> weights; // one a vertex, or none
        std::vector<U32> indices;
    };

    std::vector<U16> read_u16s(const LLSD::Binary& bytes, size_t count)
    {
        std::vector<U16> values(count);
        std::memcpy(values.data(), bytes.data(), count * sizeof(U16));
        return values;
    }

    LLSD::Binary write_u16s(const std::vector<U16>& values)
    {
        LLSD::Binary bytes(values.size() * sizeof(U16));
        std::memcpy(bytes.data(), values.data(), bytes.size());
        return bytes;
    }

    // Why a face is left as it is.
    enum Unread
    {
        UNREAD_NO_GEOMETRY,
        UNREAD_POSITIONS,
        UNREAD_NORMALS,
        UNREAD_TEX_COORDS,
        UNREAD_INDICES,
        UNREAD_WEIGHTS,
        UNREAD_COUNT
    };
    const char* const UNREAD_NAMES[UNREAD_COUNT] = { "no geometry", "positions", "normals", "texture coordinates", "indices", "weights" };

    // The face's arrays, where every one holds its vertices exactly and the
    // weights, if any, read to their end at the last vertex; otherwise none,
    // and the face is left as it is.
    std::optional<QuantizedFace> read_face(const LLSD& face, Unread* why = nullptr)
    {
        auto unread = [why](Unread reason)
        {
            if (why)
            {
                *why = reason;
            }
            return std::nullopt;
        };
        if (face.has("NoGeometry"))
        {
            return unread(UNREAD_NO_GEOMETRY);
        }
        const LLSD::Binary& pos = face["Position"].asBinary();
        const LLSD::Binary& norm = face["Normal"].asBinary();
        const LLSD::Binary& tc = face["TexCoord0"].asBinary();
        const LLSD::Binary& idx = face["TriangleList"].asBinary();
        QuantizedFace q;
        q.vertices = pos.size() / 6;
        if (q.vertices == 0 || pos.size() != q.vertices * 6)
        {
            return unread(UNREAD_POSITIONS);
        }
        if (norm.size() != q.vertices * 6)
        {
            return unread(UNREAD_NORMALS);
        }
        if (tc.size() != q.vertices * 4)
        {
            return unread(UNREAD_TEX_COORDS);
        }
        if (idx.size() % 6 != 0 || idx.empty())
        {
            return unread(UNREAD_INDICES);
        }
        q.positions = read_u16s(pos, q.vertices * 3);
        q.normals = read_u16s(norm, q.vertices * 3);
        q.tex_coords = read_u16s(tc, q.vertices * 2);
        const std::vector<U16> indices = read_u16s(idx, idx.size() / 2);
        q.indices.assign(indices.begin(), indices.end());
        if (*std::max_element(q.indices.begin(), q.indices.end()) >= q.vertices)
        {
            return unread(UNREAD_INDICES);
        }
        if (face.has("Weights"))
        {
            const LLSD::Binary& w = face["Weights"].asBinary();
            size_t at = 0;
            q.weights.resize(q.vertices);
            for (Influences& vertex : q.weights)
            {
                vertex.fill(0);
                while (true)
                {
                    if (at >= w.size())
                    {
                        return unread(UNREAD_WEIGHTS);
                    }
                    const U8 joint = w[at++];
                    if (joint == 0xFF)
                    {
                        break;
                    }
                    if (w.size() - at < 2)
                    {
                        return unread(UNREAD_WEIGHTS);
                    }
                    const U8 n = vertex[0];
                    vertex[1 + n * 3] = joint;
                    vertex[2 + n * 3] = w[at++];
                    vertex[3 + n * 3] = w[at++];
                    vertex[0] = (U8)(n + 1);
                    if (vertex[0] == 4)
                    {
                        break;
                    }
                }
            }
            if (at != w.size())
            {
                return unread(UNREAD_WEIGHTS);
            }
        }
        return q;
    }

    void write_face(LLSD& face, const QuantizedFace& q)
    {
        face["Position"] = write_u16s(q.positions);
        face["Normal"] = write_u16s(q.normals);
        face["TexCoord0"] = write_u16s(q.tex_coords);
        std::vector<U16> indices(q.indices.begin(), q.indices.end());
        face["TriangleList"] = write_u16s(indices);
        if (!q.weights.empty())
        {
            LLSD::Binary w;
            for (const Influences& vertex : q.weights)
            {
                w.insert(w.end(), vertex.begin() + 1, vertex.begin() + 1 + vertex[0] * 3);
                if (vertex[0] < 4)
                {
                    w.push_back(0xFF);
                }
            }
            face["Weights"] = w;
        }
    }

    Influences sorted_influences(Influences vertex)
    {
        std::array<std::array<U8, 3>, 4> each{};
        for (U8 i = 0; i < vertex[0]; ++i)
        {
            each[i] = { vertex[1 + i * 3], vertex[2 + i * 3], vertex[3 + i * 3] };
        }
        std::stable_sort(each.begin(), each.begin() + vertex[0], [](const auto& a, const auto& b) { return a[0] < b[0]; });
        for (U8 i = 0; i < vertex[0]; ++i)
        {
            std::copy(each[i].begin(), each[i].end(), vertex.begin() + 1 + i * 3);
        }
        return vertex;
    }

    enum class Order
    {
        KEEP,
        CACHE,
        STRIP,
    };

    // Where each triangle starts, its winding kept.
    enum class Rotate
    {
        KEEP,
        LOWEST,      // at its lowest vertex
        HIGHEST_LAST, // with its highest vertex last
        SHARED_EDGE, // at the edge it shares with the triangle before it, else at its lowest
    };

    struct Repack
    {
        const char* name;
        bool weld = false;
        Order order = Order::KEEP;
        Rotate rotate = Rotate::KEEP;
        bool sort_triangles = false; // by their vertices, after rotating
        bool spatial = false;        // vertices in a space-filling order rather than by first use
        bool sort_influences = false;
        int normal_bits = 16; // fewer is lossy
        bool shipped = false; // ALMeshFacePack's, which is the strip order, lowest first
    };

    // Whether two vertices lie at one place in every pose: at one position,
    // and, where the face is rigged, moved by the same influences as written,
    // as ALMeshFacePack takes them.
    bool same_place(const QuantizedFace& q, U32 a, U32 b)
    {
        return std::equal(&q.positions[a * 3], &q.positions[a * 3] + 3, &q.positions[b * 3]) &&
               (q.weights.empty() || q.weights[a] == q.weights[b]);
    }

    // As same_place, the influences in any order: what a triangle draws.
    bool same_drawn_place(const QuantizedFace& q, U32 a, U32 b)
    {
        return std::equal(&q.positions[a * 3], &q.positions[a * 3] + 3, &q.positions[b * 3]) &&
               (q.weights.empty() || sorted_influences(q.weights[a]) == sorted_influences(q.weights[b]));
    }

    // The face with each vertex v moved to place[v], those placed at ~0u
    // dropped, kept of them left.
    void renumber(QuantizedFace& q, const std::vector<U32>& place, size_t kept)
    {
        QuantizedFace out;
        out.vertices = kept;
        out.positions.resize(kept * 3);
        out.normals.resize(kept * 3);
        out.tex_coords.resize(kept * 2);
        if (!q.weights.empty())
        {
            out.weights.resize(kept);
        }
        for (size_t v = 0; v < q.vertices; ++v)
        {
            const U32 to = place[v];
            if (to == ~0u)
            {
                continue;
            }
            std::copy_n(&q.positions[v * 3], 3, &out.positions[to * 3]);
            std::copy_n(&q.normals[v * 3], 3, &out.normals[to * 3]);
            std::copy_n(&q.tex_coords[v * 2], 2, &out.tex_coords[to * 2]);
            if (!q.weights.empty())
            {
                out.weights[to] = q.weights[v];
            }
        }
        out.indices = std::move(q.indices);
        meshopt_remapIndexBuffer(out.indices.data(), out.indices.data(), out.indices.size(), place.data());
        q = std::move(out);
    }

    // Each vertex numbered by the triangle that first takes it.
    void fetch_order(QuantizedFace& q)
    {
        std::vector<U32> place(q.vertices);
        const size_t kept = meshopt_optimizeVertexFetchRemap(place.data(), q.indices.data(), q.indices.size(), q.vertices);
        renumber(q, place, kept);
    }

    void rotate_triangles(std::vector<U32>& indices, Rotate rotate)
    {
        for (size_t t = 0; rotate != Rotate::KEEP && t < indices.size(); t += 3)
        {
            U32* tri = &indices[t];
            const size_t lowest = tri[1] < tri[0] ? (tri[2] < tri[1] ? 2 : 1) : (tri[2] < tri[0] ? 2 : 0);
            size_t start = lowest;
            if (rotate == Rotate::HIGHEST_LAST)
            {
                const size_t highest = tri[1] > tri[0] ? (tri[2] > tri[1] ? 2 : 1) : (tri[2] > tri[0] ? 2 : 0);
                start = (highest + 1) % 3;
            }
            else if (rotate == Rotate::SHARED_EDGE && t > 0)
            {
                // The edge both triangles hold runs the other way in this one.
                const U32* prev = &indices[t - 3];
                for (size_t r = 0; r < 3; ++r)
                {
                    const U32 x = tri[r], y = tri[(r + 1) % 3];
                    if ((prev[0] == y && prev[1] == x) || (prev[1] == y && prev[2] == x) || (prev[2] == y && prev[0] == x))
                    {
                        start = r;
                        break;
                    }
                }
            }
            std::rotate(tri, tri + start, tri + 3);
        }
    }

    void sort_triangles(std::vector<U32>& indices)
    {
        std::vector<std::array<U32, 3>> triangles(indices.size() / 3);
        std::memcpy(triangles.data(), indices.data(), indices.size() * sizeof(U32));
        std::sort(triangles.begin(), triangles.end());
        std::memcpy(indices.data(), triangles.data(), indices.size() * sizeof(U32));
    }

    // Repacks the face as the way says. False where it would leave the face
    // no triangle, which is then left as it is.
    bool repack(QuantizedFace& q, const Repack& way)
    {
        if (way.sort_influences)
        {
            for (Influences& vertex : q.weights)
            {
                vertex = sorted_influences(vertex);
            }
        }
        if (way.normal_bits < 16)
        {
            const F64 step = (F64)(1 << (16 - way.normal_bits));
            for (U16& n : q.normals)
            {
                n = (U16)std::min(65535.0, std::round(n / step) * step);
            }
        }

        if (way.weld)
        {
            std::vector<meshopt_Stream> streams = {
                { q.positions.data(), sizeof(U16) * 3, sizeof(U16) * 3 },
                { q.normals.data(), sizeof(U16) * 3, sizeof(U16) * 3 },
                { q.tex_coords.data(), sizeof(U16) * 2, sizeof(U16) * 2 },
            };
            if (!q.weights.empty())
            {
                streams.push_back({ q.weights.data(), sizeof(Influences), sizeof(Influences) });
            }
            std::vector<U32> remap(q.vertices);
            const size_t distinct =
                meshopt_generateVertexRemapMulti(remap.data(), q.indices.data(), q.indices.size(), q.vertices, streams.data(), streams.size());
            renumber(q, remap, distinct);

            // A triangle naming a vertex twice, or two vertices at one
            // position, has no area.
            size_t kept = 0;
            for (size_t t = 0; t < q.indices.size(); t += 3)
            {
                const U32 a = q.indices[t], b = q.indices[t + 1], c = q.indices[t + 2];
                if (a == b || b == c || a == c || same_place(q, a, b) || same_place(q, b, c) || same_place(q, a, c))
                {
                    continue;
                }
                q.indices[kept++] = a;
                q.indices[kept++] = b;
                q.indices[kept++] = c;
            }
            if (kept == 0)
            {
                return false;
            }
            q.indices.resize(kept);
        }

        if (way.order == Order::CACHE)
        {
            meshopt_optimizeVertexCache(q.indices.data(), q.indices.data(), q.indices.size(), q.vertices);
        }
        else if (way.order == Order::STRIP)
        {
            meshopt_optimizeVertexCacheStrip(q.indices.data(), q.indices.data(), q.indices.size(), q.vertices);
        }
        fetch_order(q);

        if (way.spatial)
        {
            std::vector<F32> positions(q.positions.begin(), q.positions.end());
            std::vector<U32> place(q.vertices);
            meshopt_spatialSortRemap(place.data(), positions.data(), q.vertices, sizeof(F32) * 3);
            renumber(q, place, q.vertices);
        }

        rotate_triangles(q.indices, way.rotate);
        if (way.sort_triangles)
        {
            sort_triangles(q.indices);
            if (!way.spatial)
            {
                // Numbered again by first use in the sorted order, and sorted
                // again as that numbering rotates them.
                fetch_order(q);
                rotate_triangles(q.indices, way.rotate);
                sort_triangles(q.indices);
            }
        }
        return true;
    }

    // The triangles with area a face draws, each as its three vertices'
    // values from its lowest key, winding kept, sorted: two faces that draw
    // the same have the same list.
    std::vector<std::array<U8, 87>> drawn_triangles(const QuantizedFace& q)
    {
        auto key = [&](U32 v)
        {
            std::array<U8, 29> k{};
            std::memcpy(k.data(), &q.positions[v * 3], 6);
            std::memcpy(k.data() + 6, &q.normals[v * 3], 6);
            std::memcpy(k.data() + 12, &q.tex_coords[v * 2], 4);
            if (!q.weights.empty())
            {
                const Influences sorted = sorted_influences(q.weights[v]);
                std::memcpy(k.data() + 16, sorted.data(), sorted.size());
            }
            return k;
        };
        std::vector<std::array<U8, 87>> triangles;
        for (size_t t = 0; t < q.indices.size(); t += 3)
        {
            const U32 a = q.indices[t], b = q.indices[t + 1], c = q.indices[t + 2];
            if (same_drawn_place(q, a, b) || same_drawn_place(q, b, c) || same_drawn_place(q, a, c))
            {
                continue;
            }
            std::array<std::array<U8, 29>, 3> corners = { key(a), key(b), key(c) };
            const size_t lowest = std::min_element(corners.begin(), corners.end()) - corners.begin();
            std::rotate(corners.begin(), corners.begin() + lowest, corners.end());
            std::array<U8, 87> triangle;
            for (size_t i = 0; i < 3; ++i)
            {
                std::copy(corners[i].begin(), corners[i].end(), triangle.begin() + i * 29);
            }
            triangles.push_back(triangle);
        }
        std::sort(triangles.begin(), triangles.end());
        return triangles;
    }

    F64 repack_charged_tris(S64 bytes)
    {
        return std::max((F64)bytes - 384.0, 16.0) / 16.0;
    }

    TEST_CASE("almeshrepack_bench: repacked LOD blocks over the real mesh corpus", "[almeshrepack_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        std::optional<ALMeshCorpus> real = ALMeshCorpus::fromEnvironment();
        if (!real)
        {
            SKIP("AL_MESH_CORPUS_DIR names no mesh cache");
        }

        const std::vector<Repack> ways = {
            { "as written" },
            { "weld, drop empty", true },
            { "+ cache order", true, Order::CACHE },
            { "+ strip order", true, Order::STRIP },
            { "+ cache, lowest first", true, Order::CACHE, Rotate::LOWEST },
            { "+ strip, lowest first", true, Order::STRIP, Rotate::LOWEST },
            { "+ strip, highest last", true, Order::STRIP, Rotate::HIGHEST_LAST },
            { "+ strip, lowest, sorted tris", true, Order::STRIP, Rotate::LOWEST, true },
            { "+ cache, lowest, sorted tris", true, Order::CACHE, Rotate::LOWEST, true },
            { "spatial, lowest, sorted tris", true, Order::KEEP, Rotate::LOWEST, true, true },
            { "+ strip, lowest, sort infl.", true, Order::STRIP, Rotate::LOWEST, false, false, true },
            { "lossy: + normals 12 bits", true, Order::STRIP, Rotate::LOWEST, false, false, false, 12 },
            { "lossy: + normals 10 bits", true, Order::STRIP, Rotate::LOWEST, false, false, false, 10 },
            { "ALMeshFacePack::repack", true, Order::STRIP, Rotate::LOWEST, false, false, false, 16, true },
            { "ALMeshFacePack::repack, snapped", true, Order::STRIP, Rotate::LOWEST, false, false, false, 12, true },
        };
        auto way_named = [&](const char* name)
        {
            return (size_t)(std::find_if(ways.begin(), ways.end(), [&](const Repack& way) { return std::strcmp(way.name, name) == 0; }) - ways.begin());
        };
        const size_t strip_lowest = way_named("+ strip, lowest first");
        const size_t snapped_12 = way_named("lossy: + normals 12 bits");
        const size_t shipped = way_named("ALMeshFacePack::repack");
        const size_t shipped_snapped = way_named("ALMeshFacePack::repack, snapped");
        REQUIRE(shipped_snapped < ways.size());

        struct Totals
        {
            std::array<S64, REPACK_LODS> bytes{};
            std::array<F64, REPACK_LODS> tris{};
            S64 vertices = 0;
            S64 triangles = 0;
        };
        std::vector<Totals> totals(ways.size());
        size_t blocks = 0, faces = 0, faces_left = 0, changed_drawing = 0;
        std::array<size_t, UNREAD_COUNT> unread{};

        real->forEachLOD(
            [&](const CorpusLOD& lod)
            {
                ++blocks;
                for (size_t w = 0; w < ways.size(); ++w)
                {
                    LLSD block = llsd_clone(lod.block);
                    for (size_t f = 0; f < block.size(); ++f)
                    {
                        Unread why = UNREAD_COUNT;
                        std::optional<QuantizedFace> q = read_face(block[(LLSD::Integer)f], &why);
                        if (w == 0)
                        {
                            ++faces;
                            faces_left += !q;
                            if (!q)
                            {
                                ++unread[why];
                            }
                        }
                        if (!q)
                        {
                            continue;
                        }
                        QuantizedFace repacked = *q;
                        if (ways[w].shipped)
                        {
                            LLSD& face = block[(LLSD::Integer)f];
                            const bool snap = ways[w].normal_bits < 16;
                            if (ALMeshFacePack::repack(face, snap ? ALMeshFacePack::Normals::SNAP_12_BITS : ALMeshFacePack::Normals::KEEP))
                            {
                                repacked = *read_face(face);
                                if (!snap && drawn_triangles(repacked) != drawn_triangles(*q))
                                {
                                    ++changed_drawing;
                                }
                            }
                        }
                        else if (w > 0 && repack(repacked, ways[w]))
                        {
                            if (ways[w].normal_bits == 16 && drawn_triangles(repacked) != drawn_triangles(*q))
                            {
                                ++changed_drawing;
                            }
                            write_face(block[(LLSD::Integer)f], repacked);
                        }
                        else
                        {
                            repacked = *q;
                        }
                        totals[w].vertices += (S64)repacked.vertices;
                        totals[w].triangles += (S64)repacked.indices.size() / 3;
                    }
                    const std::string zipped = zip_llsd_model(block);
                    REQUIRE_FALSE(zipped.empty());
                    const S64 bytes = (S64)zipped.size();
                    totals[w].bytes[lod.lod] += bytes;
                    totals[w].tris[lod.lod] += repack_charged_tris(bytes);
                }
            });
        REQUIRE(blocks > 0);

        auto pct = [](F64 to, F64 from) { return from > 0 ? 100.0 * (to - from) / from : 0.0; };
        auto sum = [](const auto& a)
        {
            F64 s = 0;
            for (const auto& v : a)
            {
                s += (F64)v;
            }
            return s;
        };
        std::printf("repacked LOD blocks over the real mesh corpus: %s\n", real->describe().c_str());
        std::printf("  %zu blocks, %zu faces, %zu left as they are; zipped by zip_llsd_model; change against \"as written\"\n", blocks, faces,
                    faces_left);
        std::printf("  %-30s %9s %9s %9s %9s %9s | %9s | %9s %9s\n", "way", "lowest", "low", "medium", "high", "all B", "charge", "vertices",
                    "triangles");
        const Totals& base = totals[0];
        for (size_t w = 0; w < ways.size(); ++w)
        {
            const Totals& t = totals[w];
            std::printf("  %-30s", ways[w].name);
            for (S32 l = 0; l < REPACK_LODS; ++l)
            {
                std::printf(" %8.2f%%", pct((F64)t.bytes[l], (F64)base.bytes[l]));
            }
            std::printf(" %8.2f%% | %8.2f%% | %8.2f%% %8.2f%%\n", pct(sum(t.bytes), sum(base.bytes)), pct(sum(t.tris), sum(base.tris)),
                        pct((F64)t.vertices, (F64)base.vertices), pct((F64)t.triangles, (F64)base.triangles));
        }
        std::printf("  bytes as written: %.0f\n  faces left as they are:", sum(base.bytes));
        for (size_t u = 0; u < UNREAD_COUNT; ++u)
        {
            std::printf(" %s %zu%s", UNREAD_NAMES[u], unread[u], u + 1 < UNREAD_COUNT ? "," : "\n");
        }
        std::fflush(stdout);

        CHECK(changed_drawing == 0);
        CHECK(totals[shipped].bytes == totals[strip_lowest].bytes);
        CHECK(totals[shipped_snapped].bytes == totals[snapped_12].bytes);
    }
}
