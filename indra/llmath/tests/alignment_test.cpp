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
#include "../test/lltut.h"
#include "../llmath.h"
#include "../llsimdmath.h"
#include "../llvector4a.h"
#include "../llmatrix4a.h"
#include "../llvolume.h"

#include <vector>

namespace tut
{

#define is_aligned(ptr,alignment) ((reinterpret_cast<uintptr_t>(ptr))%(alignment)==0)
#define is_aligned_relative(ptr,base_ptr,alignment) ((reinterpret_cast<uintptr_t>(ptr)-reinterpret_cast<uintptr_t>(base_ptr))%(alignment)==0)

struct alignment_test {};

typedef test_group<alignment_test> alignment_test_t;
typedef alignment_test_t::object alignment_test_object_t;
tut::alignment_test_t tut_alignment_test("LLAlignment");

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
template<> template<>
void alignment_test_object_t::test<1>()
{
    const int num_tests = 7;
    void *align_ptr;
    for (int i=0; i<num_tests; i++)
    {
        align_ptr = ll_aligned_malloc_16(sizeof(MyVector4a));
        ensure("ll_aligned_malloc_16 failed", is_aligned(align_ptr,16));

        align_ptr = ll_aligned_realloc_16(align_ptr,2*sizeof(MyVector4a), sizeof(MyVector4a));
        ensure("ll_aligned_realloc_16 failed", is_aligned(align_ptr,16));

        ll_aligned_free_16(align_ptr);

        align_ptr = ll_aligned_malloc_32(sizeof(MyVector4a));
        ensure("ll_aligned_malloc_32 failed", is_aligned(align_ptr,32));
        ll_aligned_free_32(align_ptr);
    }
}

// In-place allocation of objects and arrays.
template<> template<>
void alignment_test_object_t::test<2>()
{
    MyVector4a vec1;
    ensure("LLAlignment vec1 unaligned", is_aligned(&vec1,16));

    MyVector4a veca[12];
    ensure("LLAlignment veca unaligned", is_aligned(veca,16));

    MyCacheLine line;
    ensure("LLAlignment line unaligned", is_aligned(&line,64));
}

// Heap allocation of objects and arrays.
template<> template<>
void alignment_test_object_t::test<3>()
{
    const int ARR_SIZE = 7;
    for(int i=0; i<ARR_SIZE; i++)
    {
        MyVector4a *vecp = new MyVector4a;
        ensure("LLAlignment vecp unaligned", is_aligned(vecp,16));
        delete vecp;

        MyCacheLine *linep = new MyCacheLine;
        ensure("LLAlignment linep unaligned", is_aligned(linep,64));
        delete linep;
    }

    MyVector4a *veca = new MyVector4a[ARR_SIZE];
    ensure("LLAligment veca base", is_aligned(veca,16));
    for(int i=0; i<ARR_SIZE; i++)
    {
        ensure("LLAlignment veca member unaligned", is_aligned(&veca[i],16));
    }
    delete [] veca;

    MyCacheLine *linea = new MyCacheLine[ARR_SIZE];
    ensure("LLAligment linea base", is_aligned(linea,64));
    for(int i=0; i<ARR_SIZE; i++)
    {
        ensure("LLAlignment linea member unaligned", is_aligned(&linea[i],64));
    }
    delete [] linea;
}

// The real types, on the heap and in a container.
template<> template<>
void alignment_test_object_t::test<4>()
{
    ensure_equals("LLVector4a size", sizeof(LLVector4a), size_t(16));
    ensure_equals("LLVector4a alignment", alignof(LLVector4a), size_t(16));
    ensure_equals("LLMatrix4a size", sizeof(LLMatrix4a), size_t(64));
    ensure_equals("LLMatrix4a alignment", alignof(LLMatrix4a), size_t(16));

    LLVector4a *vecp = new LLVector4a;
    ensure("LLVector4a unaligned", is_aligned(vecp,16));
    delete vecp;

    LLMatrix4a *matp = new LLMatrix4a[5];
    ensure("LLMatrix4a array unaligned", is_aligned(matp,16));
    delete [] matp;

    std::vector<LLVector4a> vecs(33);
    for (const LLVector4a& v : vecs)
    {
        ensure("LLVector4a in a vector unaligned", is_aligned(&v,16));
    }

    std::vector<LLMatrix4a> mats(9);
    for (const LLMatrix4a& m : mats)
    {
        ensure("LLMatrix4a in a vector unaligned", is_aligned(&m,16));
    }
}

// A face vertex holds its position and normal in the object itself: a
// vector of them is one allocation, every vector in it placed.
template<> template<>
void alignment_test_object_t::test<5>()
{
    ensure_equals("VertexData size", sizeof(LLVolumeFace::VertexData), size_t(48));
    ensure_equals("VertexData alignment", alignof(LLVolumeFace::VertexData), size_t(16));
    ensure("VertexData trivially copyable", std::is_trivially_copyable<LLVolumeFace::VertexData>::value);

    std::vector<LLVolumeFace::VertexData> verts(17);
    for (size_t i = 0; i < verts.size(); ++i)
    {
        ensure("VertexData position in a vector unaligned", is_aligned(&verts[i].getPosition(),16));
        ensure("VertexData normal in a vector unaligned", is_aligned(&verts[i].getNormal(),16));
        verts[i].setPosition(LLVector4a(F32(i), 1.f, 2.f));
        verts[i].setNormal(LLVector4a(0.f, 0.f, 1.f));
        verts[i].mTexCoord.set(F32(i) * 0.5f, 0.25f);
    }

    std::vector<LLVolumeFace::VertexData> copy = verts;
    for (size_t i = 0; i < copy.size(); ++i)
    {
        ensure("copied position", copy[i].getPosition().equals3(LLVector4a(F32(i), 1.f, 2.f)));
        ensure("copied normal", copy[i].getNormal().equals3(LLVector4a(0.f, 0.f, 1.f)));
        ensure_equals("copied texcoord", copy[i].mTexCoord.mV[0], F32(i) * 0.5f);
        ensure("copy equals source", copy[i] == verts[i]);
    }
    ensure("ordering", copy[0] < copy[1]);
    ensure("ordering", !(copy[1] < copy[0]));
}

}
