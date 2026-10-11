/**
 * @file v3dmath_test.cpp
 * @author Vir
 * @date 2011-12
 * @brief v3dmath test cases.
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

// Tests related to allocating objects with alignment constraints, particularly for SSE support.

#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>
#include "../llmath.h"
#include "../llsimdmath.h"
#include "../llvector4a.h"
#include "../llmatrix4a.h"
#include "../llvolume.h"

#include <vector>

namespace
{

#define is_aligned(ptr,alignment) ((reinterpret_cast<uintptr_t>(ptr))%(alignment)==0)
#define is_aligned_relative(ptr,base_ptr,alignment) ((reinterpret_cast<uintptr_t>(ptr)-reinterpret_cast<uintptr_t>(base_ptr))%(alignment)==0)

// The SIMD types declare their alignment and nothing else: no operator new.
// A plain new must place them, which the language guarantees up to the
// default new alignment and, past it, through the aligned operator new.
class alignas(16) MyVector4a
{
public:
    LLQuad mQ;
};

struct alignas(64) MyCacheLine
{
    LLQuad mQ[4];
};

// Verify that aligned allocators perform as advertised.
TEST_CASE("LLAlignment: the aligned allocators return aligned memory", "[LLAlignment]")
{
    const int num_tests = 7;
    void *align_ptr;
    for (int i=0; i<num_tests; i++)
    {
        align_ptr = ll_aligned_malloc_16(sizeof(MyVector4a));
        UNSCOPED_INFO("ll_aligned_malloc_16 failed");
        REQUIRE(is_aligned(align_ptr,16));

        align_ptr = ll_aligned_realloc_16(align_ptr,2*sizeof(MyVector4a), sizeof(MyVector4a));
        UNSCOPED_INFO("ll_aligned_realloc_16 failed");
        REQUIRE(is_aligned(align_ptr,16));

        ll_aligned_free_16(align_ptr);

        align_ptr = ll_aligned_malloc_32(sizeof(MyVector4a));
        UNSCOPED_INFO("ll_aligned_malloc_32 failed");
        REQUIRE(is_aligned(align_ptr,32));
        ll_aligned_free_32(align_ptr);
    }
}

// In-place allocation of objects and arrays.
TEST_CASE("LLAlignment: over-aligned types on the stack singly and in arrays", "[LLAlignment]")
{
    MyVector4a vec1;
    UNSCOPED_INFO("LLAlignment vec1 unaligned");
    CHECK(is_aligned(&vec1,16));

    MyVector4a veca[12];
    UNSCOPED_INFO("LLAlignment veca unaligned");
    CHECK(is_aligned(veca,16));

    MyCacheLine line;
    UNSCOPED_INFO("LLAlignment line unaligned");
    CHECK(is_aligned(&line,64));
}

// Heap allocation of objects and arrays.
TEST_CASE("LLAlignment: plain new aligns over-aligned objects and arrays", "[LLAlignment]")
{
    const int ARR_SIZE = 7;
    for(int i=0; i<ARR_SIZE; i++)
    {
        MyVector4a *vecp = new MyVector4a;
        UNSCOPED_INFO("LLAlignment vecp unaligned");
        REQUIRE(is_aligned(vecp,16));
        delete vecp;

        MyCacheLine *linep = new MyCacheLine;
        UNSCOPED_INFO("LLAlignment linep unaligned");
        REQUIRE(is_aligned(linep,64));
        delete linep;
    }

    MyVector4a *veca = new MyVector4a[ARR_SIZE];
    UNSCOPED_INFO("LLAligment veca base");
    CHECK(is_aligned(veca,16));
    for(int i=0; i<ARR_SIZE; i++)
    {
        UNSCOPED_INFO("LLAlignment veca member unaligned");
        REQUIRE(is_aligned(&veca[i],16));
    }
    delete [] veca;

    MyCacheLine *linea = new MyCacheLine[ARR_SIZE];
    UNSCOPED_INFO("LLAligment linea base");
    CHECK(is_aligned(linea,64));
    for(int i=0; i<ARR_SIZE; i++)
    {
        UNSCOPED_INFO("LLAlignment linea member unaligned");
        REQUIRE(is_aligned(&linea[i],64));
    }
    delete [] linea;
}

// The real types, on the heap and in a container.
TEST_CASE("LLAlignment: LLVector4a and LLMatrix4a align on the heap and in containers", "[LLAlignment]")
{
    UNSCOPED_INFO("LLVector4a size");
    CHECK(sizeof(LLVector4a) == size_t(16));
    UNSCOPED_INFO("LLVector4a alignment");
    CHECK(alignof(LLVector4a) == size_t(16));
    UNSCOPED_INFO("LLMatrix4a size");
    CHECK(sizeof(LLMatrix4a) == size_t(64));
    UNSCOPED_INFO("LLMatrix4a alignment");
    CHECK(alignof(LLMatrix4a) == size_t(16));

    LLVector4a *vecp = new LLVector4a;
    UNSCOPED_INFO("LLVector4a unaligned");
    CHECK(is_aligned(vecp,16));
    delete vecp;

    LLMatrix4a *matp = new LLMatrix4a[5];
    UNSCOPED_INFO("LLMatrix4a array unaligned");
    CHECK(is_aligned(matp,16));
    delete [] matp;

    std::vector<LLVector4a> vecs(33);
    for (const LLVector4a& v : vecs)
    {
        UNSCOPED_INFO("LLVector4a in a vector unaligned");
        REQUIRE(is_aligned(&v,16));
    }

    std::vector<LLMatrix4a> mats(9);
    for (const LLMatrix4a& m : mats)
    {
        UNSCOPED_INFO("LLMatrix4a in a vector unaligned");
        REQUIRE(is_aligned(&m,16));
    }
}

// A face vertex holds its position and normal in the object itself: a
// vector of them is one allocation, every vector in it placed.
TEST_CASE("LLAlignment: LLVolumeFace::VertexData aligns in a vector and copies whole", "[LLAlignment]")
{
    UNSCOPED_INFO("VertexData size");
    CHECK(sizeof(LLVolumeFace::VertexData) == size_t(48));
    UNSCOPED_INFO("VertexData alignment");
    CHECK(alignof(LLVolumeFace::VertexData) == size_t(16));
    UNSCOPED_INFO("VertexData trivially copyable");
    CHECK(std::is_trivially_copyable<LLVolumeFace::VertexData>::value);

    std::vector<LLVolumeFace::VertexData> verts(17);
    for (size_t i = 0; i < verts.size(); ++i)
    {
        UNSCOPED_INFO("VertexData position in a vector unaligned");
        REQUIRE(is_aligned(&verts[i].getPosition(),16));
        UNSCOPED_INFO("VertexData normal in a vector unaligned");
        REQUIRE(is_aligned(&verts[i].getNormal(),16));
        verts[i].setPosition(LLVector4a(F32(i), 1.f, 2.f));
        verts[i].setNormal(LLVector4a(0.f, 0.f, 1.f));
        verts[i].mTexCoord.set(F32(i) * 0.5f, 0.25f);
    }

    std::vector<LLVolumeFace::VertexData> copy = verts;
    for (size_t i = 0; i < copy.size(); ++i)
    {
        UNSCOPED_INFO("copied position");
        REQUIRE(copy[i].getPosition().equals3(LLVector4a(F32(i), 1.f, 2.f)));
        UNSCOPED_INFO("copied normal");
        REQUIRE(copy[i].getNormal().equals3(LLVector4a(0.f, 0.f, 1.f)));
        UNSCOPED_INFO("copied texcoord");
        REQUIRE(copy[i].mTexCoord.mV[0] == F32(i) * 0.5f);
        UNSCOPED_INFO("copy equals source");
        REQUIRE(copy[i] == verts[i]);
    }
    UNSCOPED_INFO("ordering");
    CHECK(copy[0] < copy[1]);
    UNSCOPED_INFO("ordering");
    CHECK_FALSE(copy[1] < copy[0]);
}

}
