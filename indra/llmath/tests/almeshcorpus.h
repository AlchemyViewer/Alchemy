/**
 * @file almeshcorpus.h
 * @brief Real mesh assets from a viewer's cache, for the tests and
 *        benchmarks that want real content
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

#ifndef AL_MESHCORPUS_H
#define AL_MESHCORPUS_H

// The mesh repository's cache files under the directory AL_MESH_CORPUS_DIR
// names: a case that reads them skips where it is not set. A run takes a
// sample of AL_MESH_CORPUS_SAMPLE meshes, 2,000 unless set and every one
// for 0, in an order AL_MESH_CORPUS_SEED fixes, 1 unless set, the same on
// every machine. AL_MESH_CORPUS_SHARD, as k/n, keeps the k-th of every n
// files of that order, so that n runs share a corpus between them. Each LOD
// a mesh's file holds is handed over with its faces as they are decoded,
// before cacheOptimize, and whether the mesh is rigged. The assets are
// other people's: nothing from them goes into the tree, and what a case
// prints of them is counts and places in the sample.

#include "llvolume.h"
#include "llvolumemgr.h"
#include "llsdserialize.h"
#include "lluuid.h"

#include <fmt/format.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace ll_test
{
    struct VolumeProbe
    {
        // A LOD block's faces as the volume holds them, before cacheOptimize.
        static bool decode(LLVolume& volume, const LLSD& mdl) { return volume.decodeVolumeFaces(mdl); }
    };
}

namespace
{
    // One LOD of one mesh of the sample.
    struct CorpusLOD
    {
        size_t mesh = 0; // the mesh's place in the sample
        S32 lod = 0;     // 0 for the lowest, 3 for the high
        bool rigged = false;
        LLPointer<LLVolume> volume;
    };

    struct CorpusCounts
    {
        size_t files = 0;      // cache files looked at
        size_t meshes = 0;     // of them, meshes with a header that reads
        size_t not_mesh = 0;   // without the mesh preamble: textures and the rest
        size_t bad_header = 0; // with it, but no header that reads
        size_t lods = 0;       // LODs decoded
        size_t lods_absent = 0; // LODs the header names that the file does not hold
        size_t lods_bad = 0;   // LODs that do not unzip or decode
    };

    class ALMeshCorpus
    {
    public:
        static constexpr size_t DEFAULT_SAMPLE = 2000;

        // The corpus AL_MESH_CORPUS_DIR names, or none where it names none.
        // A name that is not a directory fails the case, as a mistyped one
        // would otherwise pass it as though no corpus had been asked for.
        static std::optional<ALMeshCorpus> fromEnvironment()
        {
            const char* dir = std::getenv("AL_MESH_CORPUS_DIR");
            if (!dir || !*dir)
            {
                return std::nullopt;
            }
            std::error_code is_dir;
            if (!std::filesystem::is_directory(dir, is_dir))
            {
                throw std::filesystem::filesystem_error("AL_MESH_CORPUS_DIR", dir,
                                                        is_dir ? is_dir : std::make_error_code(std::errc::not_a_directory));
            }
            ALMeshCorpus corpus;
            corpus.mSample = number("AL_MESH_CORPUS_SAMPLE", DEFAULT_SAMPLE);
            const U32 seed = (U32)number("AL_MESH_CORPUS_SEED", 1);
            // A directory that is there is read in full: an error in the walk
            // fails the case rather than leave it a part of the corpus. A
            // viewer running on the cache may evict a file between the walk
            // listing it and its status being read, and that file is passed
            // over.
            std::error_code ec;
            for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            {
                const std::string name = it->path().filename().string();
                if (name.rfind("sl_cache_", 0) != 0 || name.size() <= 17 || name.compare(name.size() - 8, 8, "_0.asset") != 0)
                {
                    continue;
                }
                std::error_code status;
                if (it->is_regular_file(status))
                {
                    corpus.mFiles.push_back(it->path());
                }
                else if (status && status != std::errc::no_such_file_or_directory)
                {
                    throw std::filesystem::filesystem_error("AL_MESH_CORPUS_DIR", it->path(), status);
                }
            }
            if (ec)
            {
                throw std::filesystem::filesystem_error("AL_MESH_CORPUS_DIR", dir, ec);
            }
            // Sorted, then shuffled by the seed's own sequence, so that the
            // order is the standard engine's and no library's shuffle.
            std::sort(corpus.mFiles.begin(), corpus.mFiles.end());
            std::mt19937 rng(seed);
            for (size_t i = corpus.mFiles.size(); i > 1; --i)
            {
                std::swap(corpus.mFiles[i - 1], corpus.mFiles[rng() % i]);
            }
            size_t shard = 0, shards = 1;
            if (const char* value = std::getenv("AL_MESH_CORPUS_SHARD"); value && std::sscanf(value, "%zu/%zu", &shard, &shards) == 2 && shards > 1)
            {
                std::vector<std::filesystem::path> kept;
                for (size_t i = shard; i < corpus.mFiles.size(); i += shards)
                {
                    kept.push_back(corpus.mFiles[i]);
                }
                corpus.mFiles.swap(kept);
            }
            return corpus;
        }

        // Each LOD of each mesh of the sample, in its order.
        template <typename F>
        void forEachLOD(F&& visit)
        {
            for (const std::filesystem::path& path : mFiles)
            {
                if (mSample && mCounts.meshes >= mSample)
                {
                    break;
                }
                readMesh(path, visit);
            }
        }

        const CorpusCounts& counts() const { return mCounts; }

        // The counts, as a case prints them.
        std::string describe() const
        {
            return fmt::format("{} files: {} meshes, {} not meshes, {} without a header that reads; {} LODs decoded, {} named but absent, {} that "
                               "do not decode",
                               mCounts.files, mCounts.meshes, mCounts.not_mesh, mCounts.bad_header, mCounts.lods, mCounts.lods_absent,
                               mCounts.lods_bad);
        }

    private:
        static size_t number(const char* variable, size_t fallback)
        {
            const char* value = std::getenv(variable);
            return value && *value ? (size_t)std::strtoull(value, nullptr, 10) : fallback;
        }

        template <typename F>
        void readMesh(const std::filesystem::path& path, F& visit)
        {
            ++mCounts.files;
            std::ifstream file(path, std::ios::binary);
            std::vector<U8> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            // The mesh repository's preamble: version, header bytes, and
            // flags for what the file holds.
            constexpr size_t PREAMBLE = sizeof(U32) * 3;
            U32 preamble[3] = {};
            if (data.size() > PREAMBLE)
            {
                std::memcpy(preamble, data.data(), PREAMBLE);
            }
            if (preamble[0] != 1 || preamble[1] == 0 || PREAMBLE + preamble[1] > data.size())
            {
                ++mCounts.not_mesh;
                return;
            }

            llssize header_bytes = preamble[1];
            char* header_start = strip_deprecated_header(reinterpret_cast<char*>(data.data() + PREAMBLE), header_bytes);
            std::istringstream header_stream(std::string(header_start, (size_t)header_bytes));
            LLSD header;
            if (LLSDSerialize::fromBinary(header, header_stream, header_bytes) <= 0 || !header.isMap() || header["version"].asInteger() > 999)
            {
                ++mCounts.bad_header;
                return;
            }
            const size_t mesh = mCounts.meshes++;
            const bool rigged = header.has("skin") && header["skin"]["size"].asInteger() > 0;

            const std::string name = path.filename().string();
            LLVolumeParams params;
            params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
            params.setSculptID(LLUUID(name.substr(9, name.size() - 17)), LL_SCULPT_TYPE_MESH);

            const char* lods[] = { "lowest_lod", "low_lod", "medium_lod", "high_lod" };
            for (S32 lod = 0; lod < 4; ++lod)
            {
                const S64 offset = header[lods[lod]]["offset"].asInteger();
                const S64 size = header[lods[lod]]["size"].asInteger();
                if (size <= 0)
                {
                    continue;
                }
                // The file has room for every LOD, and its flags say which it
                // holds, a bit a LOD; a file written before they did has
                // none set, and holds what it has room for.
                const S64 at = (S64)PREAMBLE + preamble[1] + offset;
                const bool held = preamble[2] ? (preamble[2] & (1u << lod)) != 0 : true;
                if (!held || offset < 0 || at + size > (S64)data.size())
                {
                    ++mCounts.lods_absent;
                    continue;
                }
                LLSD mdl;
                CorpusLOD found;
                found.mesh = mesh;
                found.lod = lod;
                found.rigged = rigged;
                found.volume = new LLVolume(params, LLVolumeLODGroup::getVolumeScaleFromDetail(lod));
                if (LLUZipHelper::unzip_llsd(mdl, data.data() + at, (S32)size) != LLUZipHelper::ZR_OK ||
                    !ll_test::VolumeProbe::decode(*found.volume, mdl))
                {
                    ++mCounts.lods_bad;
                    continue;
                }
                ++mCounts.lods;
                visit(found);
            }
        }

        std::vector<std::filesystem::path> mFiles;
        size_t mSample = DEFAULT_SAMPLE;
        CorpusCounts mCounts;
    };
}

#endif // AL_MESHCORPUS_H
