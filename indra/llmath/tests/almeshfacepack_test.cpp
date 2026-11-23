/**
 * @file almeshfacepack_test.cpp
 * @brief ALMeshFacePack: a repacked LOD face draws what it drew, smaller
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

#include "almeshfacepack.h"

#include "llsdserialize.h"
#include "llsdutil.h"
#include "llvolume.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    // A face's sixteen-bit arrays, as a case lays them out.
    struct Arrays
    {
        std::vector<U16> positions;
        std::vector<U16> normals;
        std::vector<U16> tex_coords;
        std::vector<std::vector<std::pair<U8, U16>>> weights; // a vertex's joints and weights, or none
        std::vector<U16> indices;

        size_t vertices() const { return positions.size() / 3; }

        void add(const std::array<U16, 3>& p, const std::array<U16, 3>& n, const std::array<U16, 2>& uv,
                 std::vector<std::pair<U8, U16>> influences = {})
        {
            positions.insert(positions.end(), p.begin(), p.end());
            normals.insert(normals.end(), n.begin(), n.end());
            tex_coords.insert(tex_coords.end(), uv.begin(), uv.end());
            weights.push_back(std::move(influences));
        }
    };

    LLSD::Binary bytes_of(const std::vector<U16>& values)
    {
        LLSD::Binary bytes(values.size() * sizeof(U16));
        std::memcpy(bytes.data(), values.data(), bytes.size());
        return bytes;
    }

    // The face as LLModel::writeModel writes one.
    LLSD face_of(const Arrays& a, bool rigged)
    {
        LLSD face;
        face["Position"] = bytes_of(a.positions);
        face["Normal"] = bytes_of(a.normals);
        face["TexCoord0"] = bytes_of(a.tex_coords);
        face["TriangleList"] = bytes_of(a.indices);
        face["PositionDomain"]["Min"] = llsd::array(-0.5, -0.5, -0.5);
        face["PositionDomain"]["Max"] = llsd::array(0.5, 0.5, 0.5);
        face["TexCoord0Domain"]["Min"] = llsd::array(0.0, 0.0);
        face["TexCoord0Domain"]["Max"] = llsd::array(1.0, 1.0);
        if (rigged)
        {
            LLSD::Binary weights;
            for (const auto& vertex : a.weights)
            {
                for (const auto& [joint, weight] : vertex)
                {
                    weights.push_back(joint);
                    weights.push_back((U8)(weight & 0xFF));
                    weights.push_back((U8)(weight >> 8));
                }
                if (vertex.size() < 4)
                {
                    weights.push_back(0xFF);
                }
            }
            face["Weights"] = weights;
        }
        return face;
    }

    U16 unit_to_u16(F32 value) // over [-1, 1]
    {
        return (U16)std::clamp((value + 1.f) * 0.5f * 65535.f + 0.5f, 0.f, 65535.f);
    }

    // A rolling grid of quads as a triangle soup: every triangle has three
    // vertices of its own, each the same in every array as the grid point it
    // copies, and a point's influences number from one to four by its
    // column. Then a triangle naming a vertex twice, one whose corners hold
    // two vertices at one position, a vertex no triangle names, and the
    // first triangle again with a corner whose vertex differs from the
    // grid's only in its weights.
    Arrays soup_grid(S32 columns, S32 rows)
    {
        Arrays a;
        auto point = [&](S32 c, S32 r)
        {
            const F32 x = (F32)c / columns - 0.5f;
            const F32 y = (F32)r / rows - 0.5f;
            const F32 z = 0.1f * std::sin(6.f * x) * std::cos(4.f * y);
            const F32 nx = -0.6f * std::cos(6.f * x) * std::cos(4.f * y);
            const F32 ny = 0.4f * std::sin(6.f * x) * std::sin(4.f * y);
            const F32 len = std::sqrt(nx * nx + ny * ny + 1.f);
            std::vector<std::pair<U8, U16>> influences;
            for (S32 i = 0; i <= c % 4; ++i)
            {
                influences.emplace_back((U8)(i * 3 + r % 2), (U16)(65535 / (c % 4 + 1)));
            }
            a.add({ unit_to_u16(2.f * x), unit_to_u16(2.f * y), unit_to_u16(2.f * z) },
                  { unit_to_u16(nx / len), unit_to_u16(ny / len), unit_to_u16(1.f / len) },
                  { (U16)(65535 * c / columns), (U16)(65535 * r / rows) }, influences);
            return (U16)(a.vertices() - 1);
        };
        for (S32 r = 0; r < rows; ++r)
        {
            for (S32 c = 0; c < columns; ++c)
            {
                for (const auto& [dc, dr] : { std::pair{ 0, 0 }, std::pair{ 1, 0 }, std::pair{ 0, 1 }, std::pair{ 1, 0 }, std::pair{ 1, 1 }, std::pair{ 0, 1 } })
                {
                    a.indices.push_back(point(c + dc, r + dr));
                }
            }
        }
        // no area: a vertex named twice, and two vertices at one position
        a.indices.insert(a.indices.end(), { 0, 0, 1 });
        a.add({ a.positions[0], a.positions[1], a.positions[2] }, { 0, 0, 65535 }, { 0, 0 }, { { 0, 65535 } });
        a.indices.insert(a.indices.end(), { (U16)(a.vertices() - 1), 1, 0 });
        // named by none
        a.add({ 1, 2, 3 }, { 4, 5, 6 }, { 7, 8 }, { { 1, 65535 } });
        // the first triangle again, a corner weighted otherwise
        const U16 first[3] = { a.indices[0], a.indices[1], a.indices[2] };
        a.add({ a.positions[first[1] * 3], a.positions[first[1] * 3 + 1], a.positions[first[1] * 3 + 2] },
              { a.normals[first[1] * 3], a.normals[first[1] * 3 + 1], a.normals[first[1] * 3 + 2] },
              { a.tex_coords[first[1] * 2], a.tex_coords[first[1] * 2 + 1] }, { { 7, 40000 }, { 8, 25535 } });
        a.indices.insert(a.indices.end(), { first[0], (U16)(a.vertices() - 1), first[2] });
        return a;
    }

    std::string binary_of(const LLSD& sd)
    {
        std::ostringstream out;
        LLSDSerialize::toBinary(sd, out);
        return out.str();
    }

    // The triangles with area the face draws once the viewer has loaded it
    // as it loads a mesh asset's LOD, each as its corners' positions,
    // normals, texture coordinates and weights from its lowest corner, its
    // winding kept, sorted.
    std::vector<std::vector<F32>> drawn(const LLSD& face)
    {
        LLSD block = LLSD::emptyArray();
        block.append(face);
        std::string zipped = zip_llsd(block);
        LLVolumeParams params;
        params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
        params.setSculptID(LLUUID("2cbd8a4c-7d9c-4f8e-b33a-3c2f1d55b0a1"), LL_SCULPT_TYPE_MESH);
        LLPointer<LLVolume> volume = new LLVolume(params, 1.f);
        REQUIRE(volume->unpackVolumeFaces((U8*)zipped.data(), (S32)zipped.size()));
        REQUIRE(volume->getNumVolumeFaces() == 1);
        const LLVolumeFace& f = volume->getVolumeFace(0);

        std::vector<std::vector<F32>> triangles;
        for (S32 t = 0; t + 2 < f.mNumIndices; t += 3)
        {
            std::vector<std::vector<F32>> corners;
            for (S32 i = 0; i < 3; ++i)
            {
                const U16 v = f.mIndices[t + i];
                std::vector<F32> corner(f.mPositions[v].getF32ptr(), f.mPositions[v].getF32ptr() + 3);
                corner.insert(corner.end(), f.mNormals[v].getF32ptr(), f.mNormals[v].getF32ptr() + 3);
                corner.insert(corner.end(), f.mTexCoords[v].mV, f.mTexCoords[v].mV + 2);
                if (f.mWeights)
                {
                    corner.insert(corner.end(), f.mWeights[v].getF32ptr(), f.mWeights[v].getF32ptr() + 4);
                }
                corners.push_back(std::move(corner));
            }
            // two corners at one position, moved alike in every pose
            auto same = [&](S32 a, S32 b)
            {
                return std::equal(corners[a].begin(), corners[a].begin() + 3, corners[b].begin()) &&
                       (!f.mWeights || std::equal(corners[a].begin() + 8, corners[a].end(), corners[b].begin() + 8));
            };
            if (same(0, 1) || same(1, 2) || same(0, 2))
            {
                continue;
            }
            std::rotate(corners.begin(), std::min_element(corners.begin(), corners.end()), corners.end());
            std::vector<F32> triangle;
            for (const auto& corner : corners)
            {
                triangle.insert(triangle.end(), corner.begin(), corner.end());
            }
            triangles.push_back(std::move(triangle));
        }
        std::sort(triangles.begin(), triangles.end());
        return triangles;
    }

    std::vector<U16> u16s_of(const LLSD::Binary& bytes)
    {
        std::vector<U16> values(bytes.size() / sizeof(U16));
        std::memcpy(values.data(), bytes.data(), values.size() * sizeof(U16));
        return values;
    }

    TEST_CASE("almeshfacepack: a repacked face draws what it drew once loaded", "[almeshfacepack]")
    {
        for (bool rigged : { false, true })
        {
            CAPTURE(rigged);
            const LLSD original = face_of(soup_grid(24, 16), rigged);
            LLSD repacked = llsd_clone(original);
            REQUIRE(ALMeshFacePack::repack(repacked));
            const auto before = drawn(original);
            CHECK(before.size() == 24 * 16 * 2 + 1);
            CHECK(drawn(repacked) == before);
        }
    }

    TEST_CASE("almeshfacepack: a repacked face holds one vertex for each and only triangles with area", "[almeshfacepack]")
    {
        constexpr S32 COLUMNS = 24;
        constexpr S32 ROWS = 16;
        LLSD face = face_of(soup_grid(COLUMNS, ROWS), true);
        REQUIRE(ALMeshFacePack::repack(face));
        // The grid's points and the one weighted otherwise; the grid's
        // triangles and the first again.
        const size_t vertices = face["Position"].asBinary().size() / 6;
        CHECK(vertices == (size_t)(COLUMNS + 1) * (ROWS + 1) + 1);
        CHECK(face["Normal"].asBinary().size() == vertices * 6);
        CHECK(face["TexCoord0"].asBinary().size() == vertices * 4);
        const std::vector<U16> indices = u16s_of(face["TriangleList"].asBinary());
        CHECK(indices.size() == (size_t)COLUMNS * ROWS * 6 + 3);

        // The vertices are numbered as the triangles first take them, those
        // a triangle takes first numbered next whatever their order in it,
        // and each triangle starts at its lowest vertex.
        bool lowest_first = true;
        bool first_use = true;
        U32 next = 0;
        for (size_t t = 0; t < indices.size(); t += 3)
        {
            const U16* triangle = &indices[t];
            lowest_first &= triangle[0] < triangle[1] && triangle[0] < triangle[2];
            std::vector<U16> taken;
            std::copy_if(triangle, triangle + 3, std::back_inserter(taken), [&](U16 v) { return v >= next; });
            std::sort(taken.begin(), taken.end());
            for (U16 v : taken)
            {
                first_use &= v == next;
                ++next;
            }
        }
        CHECK(lowest_first);
        CHECK(first_use);
        CHECK(next == vertices);
    }

    TEST_CASE("almeshfacepack: a repacked face zips smaller", "[almeshfacepack]")
    {
        LLSD block = llsd::array(face_of(soup_grid(24, 16), true));
        LLSD repacked = llsd_clone(block);
        REQUIRE(ALMeshFacePack::repack(repacked[0]));
        const size_t before = zip_llsd_model(block).size();
        const size_t after = zip_llsd_model(repacked).size();
        INFO("zipped: " << after << " bytes repacked, " << before << " as written");
        CHECK(after < before);
    }

    TEST_CASE("almeshfacepack: snapped normals sit on their grid within half a step of those written", "[almeshfacepack]")
    {
        const Arrays arrays = soup_grid(24, 16);
        const LLSD original = face_of(arrays, true);
        LLSD kept = llsd_clone(original);
        REQUIRE(ALMeshFacePack::repack(kept));

        // Each grid point's normal as written, by its position: the soup's
        // last three vertices are the one at a grid point's position with
        // another normal, which no triangle with area names, the one named
        // by none, and a grid point's copy with its normal.
        std::map<std::array<U16, 3>, std::array<U16, 3>> written;
        for (size_t v = 0; v + 3 < arrays.vertices(); ++v)
        {
            written[{ arrays.positions[v * 3], arrays.positions[v * 3 + 1], arrays.positions[v * 3 + 2] }] = {
                arrays.normals[v * 3], arrays.normals[v * 3 + 1], arrays.normals[v * 3 + 2]
            };
        }

        // Each grid zips smaller than the finer one before it.
        LLSD kept_block = llsd::array(kept);
        size_t finer_bytes = zip_llsd_model(kept_block).size();
        for (const auto& [grid, step] :
             { std::pair{ ALMeshFacePack::Normals::SNAP_12_BITS, 16 }, std::pair{ ALMeshFacePack::Normals::SNAP_10_BITS, 64 } })
        {
            CAPTURE(step);
            LLSD snapped = llsd_clone(original);
            REQUIRE(ALMeshFacePack::repack(snapped, grid));

            const std::vector<U16> positions = u16s_of(snapped["Position"].asBinary());
            const std::vector<U16> normals = u16s_of(snapped["Normal"].asBinary());
            REQUIRE(normals.size() == positions.size());
            bool found = true;
            bool on_grid = true;
            bool within_half_a_step = true;
            for (size_t v = 0; v < positions.size() / 3; ++v)
            {
                const auto at = written.find({ positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2] });
                found &= at != written.end();
                for (size_t k = 0; at != written.end() && k < 3; ++k)
                {
                    const S32 n = normals[v * 3 + k];
                    on_grid &= n % step == 0 || n == 65535;
                    within_half_a_step &= std::abs(n - (S32)at->second[k]) <= step / 2;
                }
            }
            CHECK(found);
            CHECK(on_grid);
            CHECK(within_half_a_step);

            LLSD snapped_block = llsd::array(snapped);
            const size_t snapped_bytes = zip_llsd_model(snapped_block).size();
            INFO("zipped: " << snapped_bytes << " bytes snapped, " << finer_bytes << " on the finer grid");
            CHECK(snapped_bytes < finer_bytes);
            finer_bytes = snapped_bytes;
        }
    }

    TEST_CASE("almeshfacepack: snapping welds vertices that differ only below its grid", "[almeshfacepack]")
    {
        // A quad of two triangles, the second naming a copy of the second
        // vertex whose normal is twenty sixteen-bit steps off, under the
        // 10-bit grid's half step but over the 12-bit grid's, and a copy of
        // the third whose normal is three off, under both.
        Arrays a;
        a.add({ 0, 0, 0 }, { 32768, 32768, 65535 }, { 0, 0 });
        a.add({ 65535, 0, 0 }, { 32768, 32768, 65535 }, { 65535, 0 });
        a.add({ 0, 65535, 0 }, { 32768, 32768, 65535 }, { 0, 65535 });
        a.add({ 65535, 65535, 0 }, { 32768, 32768, 65535 }, { 65535, 65535 });
        a.add({ 0, 65535, 0 }, { 32771, 32768, 65535 }, { 0, 65535 });
        a.add({ 65535, 0, 0 }, { 32788, 32768, 65535 }, { 65535, 0 });
        a.indices = { 0, 1, 2, 5, 3, 4 };
        const LLSD written = face_of(a, false);
        for (const auto& [grid, vertices] : { std::tuple{ ALMeshFacePack::Normals::KEEP, 6 },
                                              std::tuple{ ALMeshFacePack::Normals::SNAP_12_BITS, 5 },
                                              std::tuple{ ALMeshFacePack::Normals::SNAP_10_BITS, 4 } })
        {
            CAPTURE(vertices);
            LLSD face = llsd_clone(written);
            REQUIRE(ALMeshFacePack::repack(face, grid));
            CHECK(face["Position"].asBinary().size() / 6 == (size_t)vertices);
            CHECK(u16s_of(face["TriangleList"].asBinary()).size() == 6);
        }
    }

    TEST_CASE("almeshfacepack: rounded weights sit on the 8-bit grid within half a step of those written", "[almeshfacepack]")
    {
        // The soup with weights whose low bytes vary, as a real mesh's do:
        // a grid point's first two influences moved off their even shares by
        // a step its position sets, alike for every copy of the point.
        Arrays arrays = soup_grid(24, 16);
        for (size_t v = 0; v + 3 < arrays.vertices(); ++v)
        {
            auto& influences = arrays.weights[v];
            if (influences.size() >= 2)
            {
                const U32 at = (U32)arrays.positions[v * 3] * 31u + (U32)arrays.positions[v * 3 + 1] * 17u;
                const S32 shift = (S32)(at % 509u) - 254;
                influences[0].second = (U16)(influences[0].second + shift);
                influences[1].second = (U16)(influences[1].second - shift);
            }
        }
        const LLSD original = face_of(arrays, true);
        LLSD kept = llsd_clone(original);
        LLSD rounded = llsd_clone(original);
        REQUIRE(ALMeshFacePack::repack(kept));
        REQUIRE(ALMeshFacePack::repack(rounded, ALMeshFacePack::Normals::KEEP, ALMeshFacePack::Weights::ROUND_8_BITS));

        // Each vertex's weights as written, by its position and its joints,
        // which tell every vertex of the soup from the others at its place.
        std::map<std::vector<U16>, std::vector<U16>> written;
        for (size_t v = 0; v < arrays.vertices(); ++v)
        {
            std::vector<U16> key(&arrays.positions[v * 3], &arrays.positions[v * 3] + 3);
            std::vector<U16> weights;
            for (const auto& [joint, weight] : arrays.weights[v])
            {
                key.push_back(joint);
                weights.push_back(weight);
            }
            written[key] = weights;
        }

        const std::vector<U16> positions = u16s_of(rounded["Position"].asBinary());
        const LLSD::Binary& bytes = rounded["Weights"].asBinary();
        size_t at = 0;
        bool found = true;
        bool on_grid = true;
        bool within_half_a_step = true;
        for (size_t v = 0; v < positions.size() / 3; ++v)
        {
            std::vector<U16> key(&positions[v * 3], &positions[v * 3] + 3);
            std::vector<U16> weights;
            while (weights.size() < 4 && at < bytes.size() && bytes[at] != 0xFF)
            {
                REQUIRE(bytes.size() - at >= 3);
                key.push_back(bytes[at]);
                weights.push_back((U16)(bytes[at + 1] | (bytes[at + 2] << 8)));
                at += 3;
            }
            if (weights.size() < 4)
            {
                REQUIRE(at < bytes.size());
                ++at;
            }
            const auto it = written.find(key);
            found &= it != written.end() && it->second.size() == weights.size();
            for (size_t i = 0; it != written.end() && i < weights.size() && i < it->second.size(); ++i)
            {
                on_grid &= weights[i] % 256 == 0 || weights[i] == 65535;
                within_half_a_step &= std::abs((S32)weights[i] - (S32)it->second[i]) <= 128;
            }
        }
        CHECK(at == bytes.size());
        CHECK(found);
        CHECK(on_grid);
        CHECK(within_half_a_step);

        LLSD kept_block = llsd::array(kept);
        LLSD rounded_block = llsd::array(rounded);
        const size_t kept_bytes = zip_llsd_model(kept_block).size();
        const size_t rounded_bytes = zip_llsd_model(rounded_block).size();
        INFO("zipped: " << rounded_bytes << " bytes rounded, " << kept_bytes << " kept");
        CHECK(rounded_bytes < kept_bytes);
    }

    TEST_CASE("almeshfacepack: rounding welds vertices weighted apart only below the 8-bit grid", "[almeshfacepack]")
    {
        // A quad of two triangles, the second naming a copy of the third
        // vertex whose weights are fifty sixteen-bit steps off, under the
        // grid's half step, and a third triangle with both at its corners.
        const std::vector<std::pair<U8, U16>> weights = { { 0, 40000 }, { 1, 25535 } };
        Arrays a;
        a.add({ 0, 0, 0 }, { 32768, 32768, 65535 }, { 0, 0 }, weights);
        a.add({ 65535, 0, 0 }, { 32768, 32768, 65535 }, { 65535, 0 }, weights);
        a.add({ 0, 65535, 0 }, { 32768, 32768, 65535 }, { 0, 65535 }, weights);
        a.add({ 65535, 65535, 0 }, { 32768, 32768, 65535 }, { 65535, 65535 }, weights);
        a.add({ 0, 65535, 0 }, { 32768, 32768, 65535 }, { 0, 65535 }, { { 0, 40050 }, { 1, 25485 } });
        a.indices = { 0, 1, 2, 1, 3, 4, 2, 4, 3 };
        const LLSD written = face_of(a, true);
        for (const auto& [rounding, vertices, indices] : { std::tuple{ ALMeshFacePack::Weights::KEEP, 5, 9 },
                                                           std::tuple{ ALMeshFacePack::Weights::ROUND_8_BITS, 4, 6 } })
        {
            CAPTURE(vertices);
            LLSD face = llsd_clone(written);
            REQUIRE(ALMeshFacePack::repack(face, ALMeshFacePack::Normals::KEEP, rounding));
            CHECK(face["Position"].asBinary().size() / 6 == (size_t)vertices);
            CHECK(u16s_of(face["TriangleList"].asBinary()).size() == (size_t)indices);
        }
    }

    TEST_CASE("almeshfacepack: a triangle whose corners at one position are weighted apart is kept", "[almeshfacepack]")
    {
        // A triangle, and one whose first corner lies at the position of the
        // first's first, moved by another joint: in a pose it has area.
        Arrays a;
        a.add({ 0, 0, 0 }, { 32768, 32768, 65535 }, { 0, 0 }, { { 0, 65535 } });
        a.add({ 65535, 0, 0 }, { 32768, 32768, 65535 }, { 65535, 0 }, { { 0, 65535 } });
        a.add({ 0, 65535, 0 }, { 32768, 32768, 65535 }, { 0, 65535 }, { { 0, 65535 } });
        a.add({ 0, 0, 0 }, { 32768, 32768, 65535 }, { 0, 0 }, { { 1, 65535 } });
        a.indices = { 0, 1, 2, 3, 0, 1 };

        LLSD rigged = face_of(a, true);
        REQUIRE(ALMeshFacePack::repack(rigged));
        CHECK(u16s_of(rigged["TriangleList"].asBinary()).size() == 6);
        CHECK(rigged["Position"].asBinary().size() / 6 == 4);

        // Unrigged, the two are one vertex, and the second triangle has none.
        LLSD unrigged = face_of(a, false);
        REQUIRE(ALMeshFacePack::repack(unrigged));
        CHECK(u16s_of(unrigged["TriangleList"].asBinary()).size() == 3);
        CHECK(unrigged["Position"].asBinary().size() / 6 == 3);
    }

    TEST_CASE("almeshfacepack: a face it cannot read is left as it was", "[almeshfacepack]")
    {
        const LLSD good = face_of(soup_grid(4, 4), true);

        LLSD no_geometry;
        no_geometry["NoGeometry"] = true;

        LLSD short_normals = llsd_clone(good);
        LLSD::Binary normals = short_normals["Normal"].asBinary();
        normals.resize(normals.size() - 6);
        short_normals["Normal"] = normals;

        LLSD no_tex_coords = llsd_clone(good);
        no_tex_coords["TexCoord0"] = LLSD::Binary();

        LLSD past_the_vertices = llsd_clone(good);
        std::vector<U16> indices = u16s_of(good["TriangleList"].asBinary());
        indices[4] = (U16)(good["Position"].asBinary().size() / 6);
        past_the_vertices["TriangleList"] = bytes_of(indices);

        LLSD part_triangle = llsd_clone(good);
        LLSD::Binary list = part_triangle["TriangleList"].asBinary();
        list.resize(list.size() - 2);
        part_triangle["TriangleList"] = list;

        LLSD short_weights = llsd_clone(good);
        LLSD::Binary weights = short_weights["Weights"].asBinary();
        weights.pop_back();
        short_weights["Weights"] = weights;

        LLSD long_weights = llsd_clone(good);
        weights = long_weights["Weights"].asBinary();
        weights.push_back(0xFF);
        long_weights["Weights"] = weights;

        Arrays flat;
        flat.add({ 1, 1, 1 }, { 0, 0, 65535 }, { 0, 0 });
        flat.add({ 1, 1, 1 }, { 0, 0, 65535 }, { 65535, 0 });
        flat.add({ 9, 9, 9 }, { 0, 0, 65535 }, { 0, 65535 });
        flat.indices = { 0, 1, 2 };
        const LLSD no_area = face_of(flat, false);

        for (const auto& [name, face] : { std::pair{ "no geometry", no_geometry }, std::pair{ "short normals", short_normals },
                                          std::pair{ "no texture coordinates", no_tex_coords },
                                          std::pair{ "an index past the vertices", past_the_vertices },
                                          std::pair{ "part of a triangle", part_triangle }, std::pair{ "weights short of the vertices", short_weights },
                                          std::pair{ "weights past the vertices", long_weights }, std::pair{ "no triangle with area", no_area } })
        {
            CAPTURE(name);
            LLSD copy = llsd_clone(face);
            CHECK_FALSE(ALMeshFacePack::repack(copy));
            CHECK(binary_of(copy) == binary_of(face));
        }
    }
}
