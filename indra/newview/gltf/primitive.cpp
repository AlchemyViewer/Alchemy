/**
 * @file primitive.cpp
 * @brief LL GLTF Implementation
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
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

#include "../llviewerprecompiledheaders.h"

#include "asset.h"
#include "buffer_util.h"
#include "../llviewershadermgr.h"

#include <meshoptimizer.h>


using namespace LL::GLTF;


// get the vertex index of each corner of a primitive's triangles, as a triangle list
// returns false if the Primitive cannot be converted to a triangle list
static bool get_triangle_corners(const Primitive* prim, std::vector<U32>& corners)
{
    bool indexed = !prim->mIndexArray.empty();
    size_t count = indexed ? prim->mIndexArray.size() : prim->mPositions.size();
    llassert(count <= size_t(U32_MAX));

    corners.clear();

    if (prim->mMode == Primitive::Mode::TRIANGLES)
    {
        for (U32 i = 0; i < U32(count / 3 * 3); ++i)
        {
            corners.push_back(i);
        }
    }
    else if (prim->mMode == Primitive::Mode::TRIANGLE_STRIP ||
             prim->mMode == Primitive::Mode::TRIANGLE_FAN)
    {
        for (U32 tri_idx = 0; size_t(tri_idx) + 2 < count; ++tri_idx)
        {
            U32 idx[3] = { tri_idx, tri_idx + 1, tri_idx + 2 };

            if (prim->mMode == Primitive::Mode::TRIANGLE_FAN)
            {
                idx[0] = 0;
            }
            else if (tri_idx % 2 != 0)
            {
                std::swap(idx[1], idx[2]);
            }

            corners.insert(corners.end(), idx, idx + 3);
        }
    }
    else
    {
        LL_WARNS("GLTF") << "Unsupported primitive mode for conversion to triangles: " << (S32)prim->mMode << LL_ENDL;
        return false;
    }

    if (indexed)
    {
        for (U32& corner : corners)
        {
            corner = prim->mIndexArray[corner];
        }
    }

    return true;
}

// A primitive unrolled into a triangle list, for its flat normals
struct TriangleList
{
    std::vector<LLVector3> p;       //positions
    std::vector<LLVector3> n;       //normals
    std::vector<LLVector2> tc0;     //texcoords 0
    std::vector<LLVector2> tc1;     //texcoords 1
    std::vector<LLColor4U> c;       //colors
    std::vector<LLVector4> w;       //weights
    std::vector<U64> j;             //joints

    // initialize from src primitive and make an unrolled triangle list
    // returns false if the Primitive cannot be converted to a triangle list
    bool copy(const Primitive* prim)
    {
        std::vector<U32> corners;
        if (!get_triangle_corners(prim, corners))
        {
            return false;
        }

        size_t vert_count = corners.size();

        p.resize(vert_count);
        n.resize(vert_count);
        tc0.resize(vert_count);
        c.resize(vert_count);

        bool has_normals = !prim->mNormals.empty();

        bool rigged = !prim->mWeights.empty();
        if (rigged)
        {
            w.resize(vert_count);
            j.resize(vert_count);
        }

        bool multi_uv = !prim->mTexCoords1.empty();
        if (multi_uv)
        {
            tc1.resize(vert_count);
        }

        for (size_t i = 0; i < vert_count; ++i)
        {
            U32 idx = corners[i];
            p[i].set(prim->mPositions[idx].getF32ptr());
            tc0[i].set(prim->mTexCoords0[idx]);
            c[i] = prim->mColors[idx];

            if (multi_uv)
            {
                tc1[i].set(prim->mTexCoords1[idx]);
            }

            if (has_normals)
            {
                n[i].set(prim->mNormals[idx].getF32ptr());
            }

            if (rigged)
            {
                w[i].set(prim->mWeights[idx].getF32ptr());
                j[i] = prim->mJoints[idx];
            }
        }

        return true;
    }

    void genNormals()
    {
        size_t tri_count = p.size() / 3;
        for (size_t i = 0; i < tri_count; ++i)
        {
            LLVector3 v0 = p[i * 3];
            LLVector3 v1 = p[i * 3 + 1];
            LLVector3 v2 = p[i * 3 + 2];

            LLVector3 normal = (v1 - v0) % (v2 - v0);
            normal.normalize();

            n[i * 3] = normal;
            n[i * 3 + 1] = normal;
            n[i * 3 + 2] = normal;
        }
    }

    // write to target primitive as an indexed triangle list
    // Only modifies runtime data, does not modify the original GLTF data
    void write(Primitive* prim) const
    {
        //re-weld
        std::vector<meshopt_Stream> mos =
        {
            { &p[0], sizeof(LLVector3), sizeof(LLVector3) },
            { &n[0], sizeof(LLVector3), sizeof(LLVector3) },
            { &tc0[0], sizeof(LLVector2), sizeof(LLVector2) },
            { &c[0], sizeof(LLColor4U), sizeof(LLColor4U) }
        };

        if (!w.empty())
        {
            mos.push_back({ &w[0], sizeof(LLVector4), sizeof(LLVector4) });
            mos.push_back({ &j[0], sizeof(U64), sizeof(U64) });
        }

        if (!tc1.empty())
        {
            mos.push_back({ &tc1[0], sizeof(LLVector2), sizeof(LLVector2) });
        }

        std::vector<U32> remap;
        remap.resize(p.size());

        size_t stream_count = mos.size();

        size_t vert_count = meshopt_generateVertexRemapMulti(&remap[0], nullptr, p.size(), p.size(), mos.data(), stream_count);

        prim->mTexCoords0.resize(vert_count);
        prim->mNormals.resize(vert_count);
        prim->mPositions.resize(vert_count);
        prim->mColors.resize(vert_count);
        if (!w.empty())
        {
            prim->mWeights.resize(vert_count);
            prim->mJoints.resize(vert_count);
        }
        if (!tc1.empty())
                    {
            prim->mTexCoords1.resize(vert_count);
        }

        prim->mIndexArray.resize(remap.size());

        for (int i = 0; i < remap.size(); ++i)
        {
            U32 src_idx = i;
            U32 dst_idx = remap[i];

            prim->mIndexArray[i] = dst_idx;

            prim->mPositions[dst_idx].load3(p[src_idx].mV);
            prim->mNormals[dst_idx].load3(n[src_idx].mV);
            prim->mTexCoords0[dst_idx] = tc0[src_idx];
            prim->mColors[dst_idx] = c[src_idx];

            if (!w.empty())
            {
                prim->mWeights[dst_idx].loadua(w[src_idx].mV);
                prim->mJoints[dst_idx] = j[src_idx];
            }

            if (!tc1.empty())
            {
                prim->mTexCoords1[dst_idx] = tc1[src_idx];
            }
        }

        prim->mGLMode = LLRender::TRIANGLES;
    }
};

template<class T>
static void remap_vertices(std::vector<T>& vertices, const std::vector<U32>& remap, size_t vert_count)
{
    std::vector<T> remapped(vert_count);
    meshopt_remapVertexBuffer(remapped.data(), vertices.data(), remap.size(), sizeof(T), remap.data());
    vertices.swap(remapped);
}

// weld a primitive in place into an indexed triangle list of the vertices its triangles use
// Only modifies runtime data, does not modify the original GLTF data
static bool weld(Primitive* prim)
{
    std::vector<U32> corners;
    if (!get_triangle_corners(prim, corners))
    {
        return false;
    }

    std::vector<meshopt_Stream> mos =
    {
        { prim->mPositions.data(), sizeof(LLVector3), sizeof(LLVector4a) },
        { prim->mNormals.data(), sizeof(LLVector3), sizeof(LLVector4a) },
        { prim->mTexCoords0.data(), sizeof(LLVector2), sizeof(LLVector2) },
        { prim->mColors.data(), sizeof(LLColor4U), sizeof(LLColor4U) }
    };

    bool rigged = !prim->mWeights.empty();
    if (rigged)
    {
        mos.push_back({ prim->mWeights.data(), sizeof(LLVector4a), sizeof(LLVector4a) });
        mos.push_back({ prim->mJoints.data(), sizeof(U64), sizeof(U64) });
    }

    bool multi_uv = !prim->mTexCoords1.empty();
    if (multi_uv)
    {
        mos.push_back({ prim->mTexCoords1.data(), sizeof(LLVector2), sizeof(LLVector2) });
    }

    // only the vertices the corners name are read, and those no corner names are dropped
    std::vector<U32> remap(prim->mPositions.size());
    size_t vert_count = meshopt_generateVertexRemapMulti(remap.data(), corners.data(), corners.size(), remap.size(), mos.data(), mos.size());

    prim->mIndexArray.resize(corners.size());
    meshopt_remapIndexBuffer(prim->mIndexArray.data(), corners.data(), corners.size(), remap.data());

    remap_vertices(prim->mPositions, remap, vert_count);
    remap_vertices(prim->mNormals, remap, vert_count);
    remap_vertices(prim->mTexCoords0, remap, vert_count);
    remap_vertices(prim->mColors, remap, vert_count);
    if (rigged)
    {
        remap_vertices(prim->mWeights, remap, vert_count);
        remap_vertices(prim->mJoints, remap, vert_count);
    }
    if (multi_uv)
    {
        remap_vertices(prim->mTexCoords1, remap, vert_count);
    }

    prim->mGLMode = LLRender::TRIANGLES;
    return true;
}


static void vertical_flip(std::vector<LLVector2>& texcoords)
{
    for (auto& tc : texcoords)
    {
        tc[1] = 1.f - tc[1];
    }
}

bool Primitive::prep(Asset& asset)
{
    // allocate vertex buffer
    // We diverge from the intent of the GLTF format here to work with our existing render pipeline
    // GLTF wants us to copy the buffer views into GPU storage as is and build render commands that source that data.
    // For our engine, though, it's better to rearrange the buffers at load time into a layout that's more consistent.
    // The GLTF native approach undoubtedly works well if you can count on VAOs, but VAOs perform much worse with our scenes.

    // load vertex data
    for (auto& it : mAttributes)
    {
        const std::string& attribName = it.first;
        Accessor& accessor = asset.mAccessors[it.second];

        // load vertex data
        if (attribName == "POSITION")
        {
            copy(asset, accessor, mPositions);
        }
        else if (attribName == "NORMAL")
        {
            copy(asset, accessor, mNormals);
        }
        else if (attribName == "TANGENT")
        {
            copy(asset, accessor, mTangents);
        }
        else if (attribName == "COLOR_0")
        {
            copy(asset, accessor, mColors);
        }
        else if (attribName == "TEXCOORD_0")
        {
            copy(asset, accessor, mTexCoords0);
        }
        else if (attribName == "TEXCOORD_1")
        {
            copy(asset, accessor, mTexCoords1);
        }
        else if (attribName == "JOINTS_0")
        {
            copy(asset, accessor, mJoints);
        }
        else if (attribName == "WEIGHTS_0")
        {
            copy(asset, accessor, mWeights);
        }
    }

    // copy index buffer
    if (mIndices != INVALID_INDEX)
    {
        Accessor& accessor = asset.mAccessors[mIndices];
        copy(asset, accessor, mIndexArray);

        for (auto& idx : mIndexArray)
        {
            if (idx >= mPositions.size())
            {
                LL_WARNS("GLTF") << "Invalid index array" << LL_ENDL;
                return false;
            }
        }
    }
    else
    { //everything must be indexed at runtime
        mIndexArray.resize(mPositions.size());
        for (U32 i = 0; i < mPositions.size(); ++i)
        {
            mIndexArray[i] = i;
        }
    }

    U32 mask = LLVertexBuffer::MAP_VERTEX;

    if (!mWeights.empty())
    {
        mask |= LLVertexBuffer::MAP_WEIGHT4;
        mask |= LLVertexBuffer::MAP_JOINT;
    }

    if (mTexCoords0.empty())
    {
        mTexCoords0.resize(mPositions.size());
    }

    mask |= LLVertexBuffer::MAP_TEXCOORD0;

    if (!mTexCoords1.empty())
    {
        mask |= LLVertexBuffer::MAP_TEXCOORD1;
    }

    if (mColors.empty())
    {
        mColors.resize(mPositions.size(), LLColor4U::white);
    }

    mask |= LLVertexBuffer::MAP_COLOR;

    bool unlit = false;

    // bake material basecolor into color array
    if (mMaterial != INVALID_INDEX)
    {
        const Material& material = asset.mMaterials[mMaterial];
        LLColor4 baseColor(material.mPbrMetallicRoughness.mBaseColorFactor.getF32ptr());
        for (auto& dst : mColors)
        {
            dst = LLColor4U(baseColor * LLColor4(dst));
        }

        if (material.mUnlit.mPresent)
        { // material uses KHR_materials_unlit
            unlit = true;
        }
    }

    if (mNormals.empty() && !unlit)
    {
        mTangents.clear();

        if (mMode == Mode::POINTS || mMode == Mode::LINES || mMode == Mode::LINE_LOOP || mMode == Mode::LINE_STRIP)
        { //no normals and no surfaces, this primitive is unlit
            unlit = true;
        }
        else
        {
            // unroll into non-indexed array of flat shaded triangles
            TriangleList data;
            if (!data.copy(this))
            {
                return false;
            }

            data.genNormals();
            data.write(this);
        }
    }
    else if (mTangents.empty() && !unlit &&
             mMode != Mode::POINTS && mMode != Mode::LINES && mMode != Mode::LINE_LOOP && mMode != Mode::LINE_STRIP)
    {
        if (!weld(this))
        {
            return false;
        }
    }

    if (!mNormals.empty())
    {
        mask |= LLVertexBuffer::MAP_NORMAL;
    }

    if (!mTangents.empty())
    {
        mask |= LLVertexBuffer::MAP_TANGENT;
    }

    mAttributeMask = mask;

    createOctree();

    return true;
}

void Primitive::upload(LLVertexBuffer* buffer)
{
    mVertexBuffer = buffer;
    // we store these buffer sizes as S32 elsewhere
    llassert(mPositions.size() <= size_t(S32_MAX));
    llassert(mIndexArray.size() <= size_t(S32_MAX / 2));

    llassert(mVertexBuffer != nullptr);

    // assert that buffer can hold this primitive
    llassert(mVertexBuffer->getNumVerts() >= mPositions.size() + mVertexOffset);
    llassert(mVertexBuffer->getNumIndices() >= mIndexArray.size() + mIndexOffset);
    llassert(mVertexBuffer->getTypeMask() == mAttributeMask);

    U32 offset = mVertexOffset;
    U32 count = getVertexCount();

    mVertexBuffer->setPositionData(mPositions.data(), offset, count);
    mVertexBuffer->setColorData(mColors.data(), offset, count);

    if (!mNormals.empty())
    {
        mVertexBuffer->setNormalData(mNormals.data(), offset, count);
    }
    if (!mTangents.empty())
    {
        mVertexBuffer->setTangentData(mTangents.data(), offset, count);
    }

    if (!mWeights.empty())
    {
        mVertexBuffer->setWeight4Data(mWeights.data(), offset, count);
        mVertexBuffer->setJointData(mJoints.data(), offset, count);
    }

    // flip texcoord y, upload, then flip back (keep the off-spec data in vram only)
    vertical_flip(mTexCoords0);
    mVertexBuffer->setTexCoord0Data(mTexCoords0.data(), offset, count);
    vertical_flip(mTexCoords0);

    if (!mTexCoords1.empty())
    {
        vertical_flip(mTexCoords1);
        mVertexBuffer->setTexCoord1Data(mTexCoords1.data(), offset, count);
        vertical_flip(mTexCoords1);
    }

    if (!mIndexArray.empty())
    {
        std::vector<U32> index_array;
        index_array.resize(mIndexArray.size());
        for (U32 i = 0; i < mIndexArray.size(); ++i)
        {
            index_array[i] = mIndexArray[i] + mVertexOffset;
        }
        mVertexBuffer->setIndexData(index_array.data(), mIndexOffset, getIndexCount());
    }
}

void initOctreeTriangle(LLVolumeTriangle* tri, F32 scaler, S32 i0, S32 i1, S32 i2, const LLVector4a& v0, const LLVector4a& v1, const LLVector4a& v2)
{
    //store pointers to vertex data
    tri->mV[0] = &v0;
    tri->mV[1] = &v1;
    tri->mV[2] = &v2;

    //store indices
    tri->mIndex[0] = i0;
    tri->mIndex[1] = i1;
    tri->mIndex[2] = i2;

    //get minimum point
    LLVector4a min = v0;
    min.setMin(min, v1);
    min.setMin(min, v2);

    //get maximum point
    LLVector4a max = v0;
    max.setMax(max, v1);
    max.setMax(max, v2);

    //compute center
    LLVector4a center;
    center.setAdd(min, max);
    center.mul(0.5f);

    tri->mPositionGroup = center;

    //compute "radius"
    LLVector4a size;
    size.setSub(max, min);

    tri->mRadius = size.getLength3().getF32() * scaler;
}

void Primitive::createOctree()
{
    // create octree
    mOctree = new LLVolumeOctree();

    F32 scaler = 0.25f;

    if (mMode == Mode::TRIANGLES)
    {
        const U32 num_triangles = getIndexCount() / 3;
        // Initialize all the triangles we need
        mOctreeTriangles.resize(num_triangles);

        for (U32 triangle_index = 0; triangle_index < num_triangles; ++triangle_index)
        { //for each triangle
            const U32 index = triangle_index * 3;
            LLVolumeTriangle* tri = &mOctreeTriangles[triangle_index];
            S32 i0 = mIndexArray[index];
            S32 i1 = mIndexArray[index + 1];
            S32 i2 = mIndexArray[index + 2];

            const LLVector4a& v0 = mPositions[i0];
            const LLVector4a& v1 = mPositions[i1];
            const LLVector4a& v2 = mPositions[i2];

            initOctreeTriangle(tri, scaler, i0, i1, i2, v0, v1, v2);

            //insert
            mOctree->insert(tri);
        }
    }
    else if (mMode == Mode::TRIANGLE_STRIP)
    {
        const U32 num_triangles = getIndexCount() - 2;
        // Initialize all the triangles we need
        mOctreeTriangles.resize(num_triangles);

        for (U32 triangle_index = 0; triangle_index < num_triangles; ++triangle_index)
        { //for each triangle
            const U32 index = triangle_index + 2;
            LLVolumeTriangle* tri = &mOctreeTriangles[triangle_index];
            S32 i0 = mIndexArray[index];
            S32 i1 = mIndexArray[index - 1];
            S32 i2 = mIndexArray[index - 2];

            const LLVector4a& v0 = mPositions[i0];
            const LLVector4a& v1 = mPositions[i1];
            const LLVector4a& v2 = mPositions[i2];

            initOctreeTriangle(tri, scaler, i0, i1, i2, v0, v1, v2);

            //insert
            mOctree->insert(tri);
        }
    }
    else if (mMode == Mode::TRIANGLE_FAN)
    {
        const U32 num_triangles = getIndexCount() - 2;
        // Initialize all the triangles we need
        mOctreeTriangles.resize(num_triangles);

        for (U32 triangle_index = 0; triangle_index < num_triangles; ++triangle_index)
        { //for each triangle
            const U32 index = triangle_index + 2;
            LLVolumeTriangle* tri = &mOctreeTriangles[triangle_index];
            S32 i0 = mIndexArray[0];
            S32 i1 = mIndexArray[index - 1];
            S32 i2 = mIndexArray[index - 2];

            const LLVector4a& v0 = mPositions[i0];
            const LLVector4a& v1 = mPositions[i1];
            const LLVector4a& v2 = mPositions[i2];

            initOctreeTriangle(tri, scaler, i0, i1, i2, v0, v1, v2);

            //insert
            mOctree->insert(tri);
        }
    }
    else if (mMode == Mode::POINTS ||
            mMode == Mode::LINES ||
        mMode == Mode::LINE_LOOP ||
        mMode == Mode::LINE_STRIP)
    {
        // nothing to do, no volume... maybe add some collision geometry around these primitive types?
    }

    else
    {
        LL_ERRS() << "Unsupported Primitive mode" << LL_ENDL;
    }

    //remove unneeded octree layers
    while (!mOctree->balance()) {}

    //calculate AABB for each node
    LLVolumeOctreeRebound rebound;
    rebound.traverse(mOctree);
}

const LLVolumeTriangle* Primitive::lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end,
    LLVector4a* intersection, LLVector2* tex_coord, LLVector4a* normal, LLVector4a* tangent_out)
{
    if (mOctree.isNull())
    {
        return nullptr;
    }

    LLVector4a dir;
    dir.setSub(end, start);

    F32 closest_t = 2.f; // must be larger than 1

    //create a proxy LLVolumeFace for the raycast
    LLVolumeFace face;
    face.mPositions = mPositions.data();
    face.mTexCoords = mTexCoords0.data();
    face.mNormals = mNormals.data();
    face.mTangents = mTangents.data();
    face.mIndices = nullptr; // unreferenced

    face.mNumIndices = S32(mIndexArray.size());
    face.mNumVertices = S32(mPositions.size());

    LLOctreeTriangleRayIntersect intersect(start, dir, &face, &closest_t, intersection, tex_coord, normal, tangent_out);
    intersect.traverse(mOctree);

    // null out proxy data so it doesn't get freed
    face.mPositions = face.mNormals = face.mTangents = nullptr;
    face.mIndices = nullptr;
    face.mTexCoords = nullptr;

    return intersect.mHitTriangle;
}

Primitive::~Primitive()
{
    mOctree = nullptr;
}

LLRender::eGeomModes gltf_mode_to_gl_mode(Primitive::Mode mode)
{
    switch (mode)
    {
    case Primitive::Mode::POINTS:
        return LLRender::POINTS;
    case Primitive::Mode::LINES:
        return LLRender::LINES;
    case Primitive::Mode::LINE_LOOP:
        return LLRender::LINE_LOOP;
    case Primitive::Mode::LINE_STRIP:
        return LLRender::LINE_STRIP;
    case Primitive::Mode::TRIANGLES:
        return LLRender::TRIANGLES;
    case Primitive::Mode::TRIANGLE_STRIP:
        return LLRender::TRIANGLE_STRIP;
    case Primitive::Mode::TRIANGLE_FAN:
        return LLRender::TRIANGLE_FAN;
    default:
        return LLRender::TRIANGLES;
    }
}

void Primitive::serialize(JsonWriter& dst) const
{
    write(mMaterial, "material", dst, -1);
    write(mMode, "mode", dst, Primitive::Mode::TRIANGLES);
    write(mIndices, "indices", dst, INVALID_INDEX);
    write(mAttributes, "attributes", dst);
}

const Primitive& Primitive::operator=(const Value& src)
{
    if (src.is_object())
    {
        copy(src, "material", mMaterial);
        copy(src, "mode", mMode);
        copy(src, "indices", mIndices);
        copy(src, "attributes", mAttributes);

        mGLMode = gltf_mode_to_gl_mode(mMode);
    }
    return *this;
}

