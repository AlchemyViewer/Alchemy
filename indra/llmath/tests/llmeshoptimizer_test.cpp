/**
 * @file llmeshoptimizer_test.cpp
 * @brief meshoptimizer's scratch from the calling thread's arena.
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

#include "llmeshoptimizer.h"
#include "alscratcharena.h"

#include <catch2/catch_test_macros.hpp>
#include <meshoptimizer.h>

#include <numeric>
#include <vector>

namespace
{
    // meshoptimizer on the arena while it lives, and on its own default,
    // operator new and delete, after.
    struct ArenaInstalled
    {
        static void* MESHOPTIMIZER_ALLOC_CALLCONV newBlock(size_t bytes) { return ::operator new(bytes); }
        static void MESHOPTIMIZER_ALLOC_CALLCONV deleteBlock(void* block) { ::operator delete(block); }

        ArenaInstalled() { LLMeshOptimizer::initClass(); }
        ~ArenaInstalled() { meshopt_setAllocator(newBlock, deleteBlock); }
    };

    // Vertex i is (3i, 3i + 1, 3i + 2) modulo 97 a coordinate: 97 vertices,
    // each count times over.
    void repeating(size_t count, std::vector<U32>& indices, std::vector<F32>& positions)
    {
        indices.resize(count);
        std::iota(indices.begin(), indices.end(), 0);
        positions.resize(count * 3);
        for (size_t i = 0; i < positions.size(); ++i)
        {
            positions[i] = (F32)(i % 97);
        }
    }

    TEST_CASE("llmeshoptimizer: meshoptimizer takes its blocks from the arena", "[llmeshoptimizer]")
    {
        ArenaInstalled installed;
        std::vector<U32> indices;
        std::vector<F32> positions;
        repeating(3000, indices, positions);
        std::vector<U32> remap(indices.size());

        ALScratchArena::resetCounters();
        CHECK(meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), positions.data(), indices.size(), sizeof(F32) * 3) == 97);
        ALScratchArena::Counters counters = ALScratchArena::counters();
        CHECK(counters.arena_blocks > 0);
        CHECK(counters.heap_blocks == 0);

        // A hash table for more vertices than the arena holds comes from the
        // heap, and the weld still comes out.
        const size_t big = ALScratchArena::CAPACITY / sizeof(U32);
        repeating(big, indices, positions);
        remap.resize(big);
        ALScratchArena::resetCounters();
        CHECK(meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), positions.data(), big, sizeof(F32) * 3) == 97);
        counters = ALScratchArena::counters();
        CHECK(counters.heap_blocks > 0);
        CHECK(remap[97] == remap[0]);
    }
}
