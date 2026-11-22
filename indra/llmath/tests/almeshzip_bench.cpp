/**
 * @file almeshzip_bench.cpp
 * @brief The bytes, the charge and the time of a mesh LOD block zipped by
 *        zip_llsd_model against zip_llsd, over the real mesh corpus.
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

// Every LOD block of the sample (almeshcorpus.h) is zipped again both ways
// from the value it unzipped to, as an upload writes it, and set beside the
// block as the file stores it, whatever zipped that. A block's charge is the
// one the download weight takes from its compressed size; a mesh's is that
// charge weighted over its LODs for an object of a given radius, for the
// meshes whose file holds all four. The time of each zip includes the LLSD
// binary it compresses, and is a time only on a quiet machine with the
// process held to one core.

#include "linden_common.h"

#include "llsdserialize.h"

#include "altestbenchmark.h"
#include "almeshcorpus.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    constexpr S32 LODS = 4;
    constexpr std::array<F64, 4> RADII = { 0.5, 2.0, 8.0, 32.0 };
    const char* const LOD_NAMES[LODS] = { "lowest", "low", "medium", "high" };

    // LLMeshCostData's charge for a LOD block of this many compressed bytes,
    // at the settings' defaults: the bytes less MeshMetaDataDiscount (384),
    // no fewer than MeshMinimumByteSize (16), over MeshBytesPerTriangle (16).
    F64 charged_tris(S64 bytes)
    {
        return std::max((F64)bytes - 384.0, 16.0) / 16.0;
    }

    // LLMeshCostData::getRadiusWeightedTris: each LOD's charge weighted by the
    // share of a region-sized circle over which it is the LOD drawn, for an
    // object of this radius.
    F64 radius_weighted_tris(const std::array<F64, LODS>& tris, F64 radius)
    {
        constexpr F64 MAX_DISTANCE = 512.0;
        constexpr F64 MAX_AREA = 102944.0;
        constexpr F64 MIN_AREA = 1.0;
        constexpr F64 PI = 3.14159265358979323846;
        const F64 dlowest = std::min(radius / 0.03, MAX_DISTANCE);
        const F64 dlow = std::min(radius / 0.06, MAX_DISTANCE);
        const F64 dmid = std::min(radius / 0.24, MAX_DISTANCE);
        F64 high_area = std::min(PI * dmid * dmid, MAX_AREA);
        F64 mid_area = std::min(PI * dlow * dlow, MAX_AREA);
        F64 low_area = std::min(PI * dlowest * dlowest, MAX_AREA);
        F64 lowest_area = MAX_AREA;
        lowest_area -= low_area;
        low_area -= mid_area;
        mid_area -= high_area;
        high_area = std::clamp(high_area, MIN_AREA, MAX_AREA);
        mid_area = std::clamp(mid_area, MIN_AREA, MAX_AREA);
        low_area = std::clamp(low_area, MIN_AREA, MAX_AREA);
        lowest_area = std::clamp(lowest_area, MIN_AREA, MAX_AREA);
        const F64 total = high_area + mid_area + low_area + lowest_area;
        return (tris[3] * high_area + tris[2] * mid_area + tris[1] * low_area + tris[0] * lowest_area) / total;
    }

    // One way of zipping, or the bytes as stored.
    struct Encoding
    {
        std::array<S64, LODS> bytes{};
        std::array<F64, LODS> tris{};
        std::array<F64, RADII.size()> weighted{}; // over the meshes holding every LOD
        F64 ns = 0;
    };

    F64 quantile(std::vector<F64> values, F64 q)
    {
        if (values.empty())
        {
            return 0;
        }
        std::sort(values.begin(), values.end());
        return values[std::min(values.size() - 1, (size_t)(q * (F64)(values.size() - 1) + 0.5))];
    }

    F64 change(F64 to, F64 from)
    {
        return from > 0 ? 100.0 * (to - from) / from : 0;
    }

    TEST_CASE("almeshzip_bench: zip_llsd_model against zip_llsd over the real mesh corpus", "[almeshzip_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        std::optional<ALMeshCorpus> real = ALMeshCorpus::fromEnvironment();
        if (!real)
        {
            SKIP("AL_MESH_CORPUS_DIR names no mesh cache");
        }

        Encoding stored, zlib, model;
        std::array<size_t, LODS> blocks{}, larger{};
        std::array<S64, LODS> raw_bytes{};
        S64 most_growth = 0;
        size_t round_trip_failures = 0, whole_meshes = 0;
        std::vector<F64> charged_ratio; // a block's bytes, zip_llsd_model over zip_llsd, where its charge is above the floor

        // The LODs of the mesh being read, by encoding, and which it held.
        size_t mesh = SIZE_MAX;
        U32 held = 0;
        std::array<std::array<F64, LODS>, 3> mesh_tris{};
        auto finish_mesh = [&]()
        {
            if (held != (1u << LODS) - 1)
            {
                return;
            }
            ++whole_meshes;
            Encoding* encodings[3] = { &stored, &zlib, &model };
            for (size_t e = 0; e < 3; ++e)
            {
                for (size_t r = 0; r < RADII.size(); ++r)
                {
                    encodings[e]->weighted[r] += radius_weighted_tris(mesh_tris[e], RADII[r]);
                }
            }
        };

        real->forEachLOD(
            [&](const CorpusLOD& lod)
            {
                if (lod.mesh != mesh)
                {
                    finish_mesh();
                    mesh = lod.mesh;
                    held = 0;
                }

                LLSD block = lod.block;
                std::ostringstream binary;
                LLSDSerialize::toBinary(block, binary);
                const std::string raw = std::move(binary).str();

                auto start = std::chrono::steady_clock::now();
                const std::string by_zlib = zip_llsd(block);
                const F64 zlib_ns = (F64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
                start = std::chrono::steady_clock::now();
                const std::string by_model = zip_llsd_model(block);
                const F64 model_ns = (F64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
                REQUIRE_FALSE(by_zlib.empty());

                LLSD back;
                bool round_trip = !by_model.empty() &&
                    LLUZipHelper::unzip_llsd(back, (const U8*)by_model.data(), (S32)by_model.size()) == LLUZipHelper::ZR_OK;
                if (round_trip)
                {
                    std::ostringstream again;
                    LLSDSerialize::toBinary(back, again);
                    round_trip = again.str() == raw;
                }
                if (!round_trip)
                {
                    ++round_trip_failures;
                    return;
                }

                const S32 l = lod.lod;
                ++blocks[l];
                raw_bytes[l] += (S64)raw.size();
                const S64 sizes[3] = { lod.stored_bytes, (S64)by_zlib.size(), (S64)by_model.size() };
                Encoding* encodings[3] = { &stored, &zlib, &model };
                for (size_t e = 0; e < 3; ++e)
                {
                    encodings[e]->bytes[l] += sizes[e];
                    encodings[e]->tris[l] += charged_tris(sizes[e]);
                    mesh_tris[e][l] = charged_tris(sizes[e]);
                }
                zlib.ns += zlib_ns;
                model.ns += model_ns;
                if (sizes[2] > sizes[1])
                {
                    ++larger[l];
                    most_growth = std::max(most_growth, sizes[2] - sizes[1]);
                }
                if (sizes[1] > 384 + 16)
                {
                    charged_ratio.push_back((F64)sizes[2] / (F64)sizes[1]);
                }
                held |= 1u << l;
            });
        finish_mesh();

        auto sum = [](const auto& per_lod)
        {
            typename std::decay_t<decltype(per_lod)>::value_type total{};
            for (const auto& value : per_lod)
            {
                total += value;
            }
            return total;
        };
        const size_t total_blocks = sum(blocks);
        const size_t total_larger = sum(larger);
        const S64 total_raw = sum(raw_bytes);
        REQUIRE(total_blocks > 0);

        std::printf("zip_llsd_model against zip_llsd over the real mesh corpus: %s\n", real->describe().c_str());
        std::printf("  %zu LOD blocks, %.1f MB of LLSD binary; %zu meshes hold all four LODs\n", total_blocks, (F64)total_raw / 1e6, whole_meshes);
        std::printf("\n  %-8s %7s %13s %13s %13s %9s %9s | %12s %12s %12s %9s %9s %8s\n", "LOD", "blocks", "stored B", "zip_llsd B",
                    "model B", "vs zlib", "vs store", "stored tris", "zlib tris", "model tris", "vs zlib", "vs store", "larger");
        auto row = [](const char* name, size_t count, S64 stored_bytes, S64 zlib_bytes, S64 model_bytes, F64 stored_tris, F64 zlib_tris,
                      F64 model_tris, size_t grew)
        {
            std::printf("  %-8s %7zu %13lld %13lld %13lld %8.2f%% %8.2f%% | %12.0f %12.0f %12.0f %8.2f%% %8.2f%% %8zu\n", name, count,
                        (long long)stored_bytes, (long long)zlib_bytes, (long long)model_bytes, change((F64)model_bytes, (F64)zlib_bytes),
                        change((F64)model_bytes, (F64)stored_bytes), stored_tris, zlib_tris, model_tris, change(model_tris, zlib_tris),
                        change(model_tris, stored_tris), grew);
        };
        for (S32 l = 0; l < LODS; ++l)
        {
            row(LOD_NAMES[l], blocks[l], stored.bytes[l], zlib.bytes[l], model.bytes[l], stored.tris[l], zlib.tris[l], model.tris[l], larger[l]);
        }
        row("all", total_blocks, sum(stored.bytes), sum(zlib.bytes), sum(model.bytes), sum(stored.tris), sum(zlib.tris), sum(model.tris),
            total_larger);

        std::printf("\n  radius-weighted charge, summed over the %zu meshes holding every LOD:\n", whole_meshes);
        std::printf("  %-8s %14s %14s %14s %9s %9s\n", "radius", "stored", "zip_llsd", "model", "vs zlib", "vs store");
        for (size_t r = 0; r < RADII.size(); ++r)
        {
            std::printf("  %6.1f m %14.0f %14.0f %14.0f %8.2f%% %8.2f%%\n", RADII[r], stored.weighted[r], zlib.weighted[r], model.weighted[r],
                        change(model.weighted[r], zlib.weighted[r]), change(model.weighted[r], stored.weighted[r]));
        }

        std::printf("\n  a charged block's bytes, zip_llsd_model over zip_llsd (%zu blocks): 10th percentile %.4f, median %.4f, 90th %.4f, most %.4f\n",
                    charged_ratio.size(), quantile(charged_ratio, 0.1), quantile(charged_ratio, 0.5), quantile(charged_ratio, 0.9),
                    quantile(charged_ratio, 1.0));
        std::printf("  zip_llsd_model larger than zip_llsd: %zu blocks, by %lld bytes at most\n", total_larger, (long long)most_growth);
        std::printf("  time, LLSD binary included: zip_llsd %.1f MB/s, zip_llsd_model %.1f MB/s (%.2fx the time)\n",
                    (F64)total_raw / 1e6 / (zlib.ns * 1e-9), (F64)total_raw / 1e6 / (model.ns * 1e-9), model.ns / zlib.ns);
        std::fflush(stdout);

        CHECK(round_trip_failures == 0);
    }
}
