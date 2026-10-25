/**
 * @file almeshsimplifier.cpp
 * @brief A model's levels of detail, made from its faces by meshoptimizer's
 *        simplifier
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

#include "llvolume.h"
#include "llvector4a.h"
#include "v2math.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <vector>

// base's faces as meshoptimizer takes them, their vertices and indices
// numbered face after face.
struct ALMeshSimplifier::Mesh
{
    std::vector<U32> first;            // each face's first vertex, then the vertex count
    std::vector<U32> first_index;      // each face's first index, then the index count
    std::vector<U32> indices;          // each face's triangles, those of its indices that name its vertices
    std::vector<LLVector4a> positions; // in the authored shape
    std::vector<F32> attributes;       // ATTRIBUTE_COUNT a vertex
    std::vector<U8> shared;            // at a position another face has a vertex at too
    std::vector<U8> held;              // shared, and at a position on the open border of a face
    std::vector<U8> uv_seam;           // at a position its face has a vertex with another texture coordinate at
    F32 extent = 0.f;                  // the largest extent of the positions

    size_t vertexCount() const { return first.back(); }
    size_t indexCount() const { return first_index.back(); }

    S32 faceOf(U32 vertex) const
    {
        return (S32)(std::upper_bound(first.begin(), first.end(), vertex) - first.begin()) - 1;
    }
};

namespace
{
    using Mesh = ALMeshSimplifier::Mesh;

    // What the simplifier weighs at a vertex besides its position: its
    // normal, as a direction in the authored shape, and its texture
    // coordinate over its face's density.
    constexpr size_t ATTRIBUTE_COUNT = 5;

    // A change of 1/weight in an attribute across a distance d costs as
    // much as moving the surface by d, in a model scaled to a size of 1.
    // A normal that turns through a right angle across a triangle then
    // costs about a third of the triangle's width in displacement.
    constexpr F32 NORMAL_WEIGHT = 0.25f;

    // Texture coordinates are taken over the square root of their face's
    // mean triangle area in texture space, at any texture scale: a
    // coordinate that slides by a triangle's width in the texture, across a
    // triangle, costs a tenth of that triangle's width in displacement.
    constexpr F32 UV_WEIGHT = 0.1f;

    constexpr F32 ATTRIBUTE_WEIGHTS[ATTRIBUTE_COUNT] = { NORMAL_WEIGHT, NORMAL_WEIGHT, NORMAL_WEIGHT, UV_WEIGHT, UV_WEIGHT };

    LLVector4a unscale(const LLVector3& normalized_scale)
    {
        // A loader keeps a size of nothing as 1, and so does this.
        LLVector4a scale;
        scale.set(normalized_scale.mV[0] != 0.f ? normalized_scale.mV[0] : 1.f,
                  normalized_scale.mV[1] != 0.f ? normalized_scale.mV[1] : 1.f,
                  normalized_scale.mV[2] != 0.f ? normalized_scale.mV[2] : 1.f, 1.f);
        return scale;
    }

    void prepare(const LLVolume& base, Mesh& mesh)
    {
        const S32 faces = base.getNumVolumeFaces();
        mesh.first.assign(faces + 1, 0);
        mesh.first_index.assign(faces + 1, 0);
        size_t index_capacity = 0;
        for (S32 f = 0; f < faces; ++f)
        {
            const LLVolumeFace& face = base.getVolumeFace(f);
            mesh.first[f + 1] = mesh.first[f] + (face.mPositions && face.mNumVertices > 0 ? (U32)face.mNumVertices : 0);
            index_capacity += face.mIndices && face.mNumIndices > 0 ? (size_t)face.mNumIndices : 0;
        }

        const size_t vertex_count = mesh.vertexCount();
        mesh.indices.clear();
        mesh.indices.reserve(index_capacity);
        mesh.positions.resize(vertex_count);
        mesh.attributes.assign(vertex_count * ATTRIBUTE_COUNT, 0.f);
        for (S32 f = 0; f < faces; ++f)
        {
            const LLVolumeFace& face = base.getVolumeFace(f);
            const U32 first = mesh.first[f];
            const U32 vertices = mesh.first[f + 1] - first;
            const U32 indices = vertices && face.mIndices && face.mNumIndices > 0 ? (U32)(face.mNumIndices - face.mNumIndices % 3) : 0;

            // A triangle that names a vertex the face does not have is left
            // out.
            for (U32 i = 0; i < indices; i += 3)
            {
                const U16* corner = face.mIndices + i;
                if (corner[0] < vertices && corner[1] < vertices && corner[2] < vertices)
                {
                    mesh.indices.push_back(first + corner[0]);
                    mesh.indices.push_back(first + corner[1]);
                    mesh.indices.push_back(first + corner[2]);
                }
            }
            mesh.first_index[f + 1] = (U32)mesh.indices.size();

            const LLVector4a scale = unscale(face.mNormalizedScale);
            const LLVector4a inv_scale(1.f / scale[0], 1.f / scale[1], 1.f / scale[2], 1.f);
            for (U32 v = 0; v < vertices; ++v)
            {
                mesh.positions[first + v].setMul(face.mPositions[v], scale);
            }

            if (face.mNormals)
            {
                for (U32 v = 0; v < vertices; ++v)
                {
                    // The face keeps the authored normal times the scale.
                    LLVector4a normal;
                    normal.setMul(face.mNormals[v], inv_scale);
                    const F32 length = sqrtf(normal.dot3(normal).getF32());
                    F32* attribute = &mesh.attributes[(first + v) * ATTRIBUTE_COUNT];
                    if (length > 0.f && std::isfinite(length))
                    {
                        attribute[0] = normal[0] / length;
                        attribute[1] = normal[1] / length;
                        attribute[2] = normal[2] / length;
                    }
                }
            }

            if (face.mTexCoords && vertices)
            {
                // A coordinate that is not finite counts for nothing, and is
                // weighed as none: in the simplifier's error it would make
                // every collapse near it free.
                auto finite = [](const LLVector2& uv) { return std::isfinite(uv.mV[0]) && std::isfinite(uv.mV[1]); };

                F64 area = 0.0;
                size_t triangles = 0;
                for (U32 i = mesh.first_index[f]; i < mesh.first_index[f + 1]; i += 3)
                {
                    const LLVector2& a = face.mTexCoords[mesh.indices[i] - first];
                    const LLVector2& b = face.mTexCoords[mesh.indices[i + 1] - first];
                    const LLVector2& c = face.mTexCoords[mesh.indices[i + 2] - first];
                    if (finite(a) && finite(b) && finite(c))
                    {
                        area += fabs(0.5 * ((F64)(b.mV[0] - a.mV[0]) * (c.mV[1] - a.mV[1]) -
                                            (F64)(c.mV[0] - a.mV[0]) * (b.mV[1] - a.mV[1])));
                        ++triangles;
                    }
                }
                const F64 mean = triangles ? area / (F64)triangles : 0.0;
                const F32 density = mean > 0.0 && std::isfinite(mean) ? (F32)(1.0 / sqrt(mean)) : 0.f;

                // Measured from the face's least coordinates, so that their
                // size stays near the density's whatever the texture's offset.
                LLVector2 least(F32_MAX, F32_MAX);
                for (U32 v = 0; v < vertices; ++v)
                {
                    if (finite(face.mTexCoords[v]))
                    {
                        least.mV[0] = llmin(least.mV[0], face.mTexCoords[v].mV[0]);
                        least.mV[1] = llmin(least.mV[1], face.mTexCoords[v].mV[1]);
                    }
                }
                for (U32 v = 0; v < vertices; ++v)
                {
                    if (density > 0.f && finite(face.mTexCoords[v]))
                    {
                        F32* attribute = &mesh.attributes[(first + v) * ATTRIBUTE_COUNT];
                        attribute[3] = (face.mTexCoords[v].mV[0] - least.mV[0]) * density;
                        attribute[4] = (face.mTexCoords[v].mV[1] - least.mV[1]) * density;
                    }
                }
            }
        }

        // Each vertex at the first vertex at its position, so that a
        // position's vertices are known together, as the simplifier knows
        // them.
        std::vector<U32> remap(vertex_count);
        if (vertex_count)
        {
            meshopt_generatePositionRemap(remap.data(), mesh.positions[0].getF32ptr(), vertex_count, sizeof(LLVector4a));
        }
        std::vector<U8> spans(vertex_count, 0);
        std::vector<U8> seam(vertex_count, 0);
        for (U32 v = 0; v < vertex_count; ++v)
        {
            const U32 r = remap[v];
            if (r == v)
            {
                continue;
            }
            const S32 face = mesh.faceOf(v);
            if (face != mesh.faceOf(r))
            {
                spans[r] = 1;
            }
            else
            {
                const LLVolumeFace& volume_face = base.getVolumeFace(face);
                const LLVector2* uv = volume_face.mTexCoords;
                if (uv && uv[v - mesh.first[face]] != uv[r - mesh.first[face]])
                {
                    seam[r] = 1;
                }
            }
        }
        // Each position on the open border of a face, where an edge of its
        // runs that no other of its triangles has, its vertices taken at
        // their positions. Where faces meet, one's border lies along the
        // other; faces that overlap, as a garment's inside is a copy of its
        // outside, share positions across their insides too.
        std::vector<U8> border(vertex_count, 0);
        std::vector<U64> edges;
        for (S32 f = 0; f < faces; ++f)
        {
            edges.clear();
            for (U32 i = mesh.first_index[f]; i < mesh.first_index[f + 1]; i += 3)
            {
                for (U32 k = 0; k < 3; ++k)
                {
                    const U32 a = remap[mesh.indices[i + k]];
                    const U32 b = remap[mesh.indices[i + (k + 1) % 3]];
                    if (a != b)
                    {
                        edges.push_back(((U64)llmin(a, b) << 32) | llmax(a, b));
                    }
                }
            }
            std::sort(edges.begin(), edges.end());
            for (size_t e = 0; e < edges.size();)
            {
                size_t run = e + 1;
                while (run < edges.size() && edges[run] == edges[e])
                {
                    ++run;
                }
                if (run - e == 1)
                {
                    border[(U32)(edges[e] >> 32)] = 1;
                    border[(U32)edges[e]] = 1;
                }
                e = run;
            }
        }

        mesh.shared.resize(vertex_count);
        mesh.held.resize(vertex_count);
        mesh.uv_seam.resize(vertex_count);
        for (U32 v = 0; v < vertex_count; ++v)
        {
            mesh.shared[v] = spans[remap[v]];
            mesh.held[v] = spans[remap[v]] && border[remap[v]];
            mesh.uv_seam[v] = seam[remap[v]];
        }

        mesh.extent = vertex_count ? meshopt_simplifyScale(mesh.positions[0].getF32ptr(), vertex_count, sizeof(LLVector4a)) : 0.f;
    }

    // The vertex flags for a simplification free to cross seams: the
    // borders between faces held, and the texture coordinate seams too for
    // Seams::UV.
    void protect(const Mesh& mesh, ALMeshSimplifier::Seams seams, U32 first, U32 count, std::vector<U8>& locks)
    {
        locks.assign(count, 0);
        for (U32 v = 0; v < count; ++v)
        {
            const bool held = mesh.shared[first + v] || (seams == ALMeshSimplifier::Seams::UV && mesh.uv_seam[first + v]);
            locks[v] = held ? (U8)meshopt_SimplifyVertex_Protect : 0;
        }
    }

    size_t targetCount(size_t index_count, F32 decimator)
    {
        if (decimator > 0.f)
        {
            return llclamp((size_t)llfloor((F32)index_count / decimator), (size_t)3, index_count);
        }
        return 3;
    }

    // The largest detached part a simplification may drop: one whose
    // vertices all lie within this distance of their mean, relative to the
    // model's largest extent.
    constexpr F32 PRUNE_LIMIT = 0.05f;

    // The radius of the largest part, connected by shared positions, that
    // indices has and simplified has none of, as meshoptimizer measures a
    // part: its vertices' farthest from their mean, over the extent.
    F32 largestDropped(const LLVector4a* positions, size_t vertex_count, const U32* indices, size_t index_count, const U32* simplified,
                       size_t simplified_count, F32 extent)
    {
        std::vector<U32> remap(vertex_count);
        meshopt_generatePositionRemap(remap.data(), positions[0].getF32ptr(), vertex_count, sizeof(LLVector4a));
        std::vector<U32> parent(vertex_count);
        for (U32 v = 0; v < vertex_count; ++v)
        {
            parent[v] = v;
        }
        auto root = [&](U32 v)
        {
            while (parent[v] != v)
            {
                v = parent[v] = parent[parent[v]];
            }
            return v;
        };
        for (size_t i = 0; i < index_count; i += 3)
        {
            // a stays the root of the corners joined so far.
            U32 a = root(remap[indices[i]]);
            for (S32 k = 1; k < 3; ++k)
            {
                const U32 b = root(remap[indices[i + k]]);
                if (a != b)
                {
                    const U32 joined = llmin(a, b);
                    parent[llmax(a, b)] = joined;
                    a = joined;
                }
            }
        }

        std::vector<U8> kept(vertex_count, 0);
        for (size_t i = 0; i < simplified_count; ++i)
        {
            kept[root(remap[simplified[i]])] = 1;
        }
        std::vector<U8> used(vertex_count, 0);
        for (size_t i = 0; i < index_count; ++i)
        {
            used[indices[i]] = 1;
        }

        // Each dropped part's mean, then its radius.
        std::vector<LLVector4a> mean(vertex_count);
        std::vector<F32> weight(vertex_count, 0.f);
        for (U32 v = 0; v < vertex_count; ++v)
        {
            mean[v].clear();
        }
        for (U32 v = 0; v < vertex_count; ++v)
        {
            const U32 r = root(remap[v]);
            if (used[v] && !kept[r])
            {
                mean[r].add(positions[v]);
                weight[r] += 1.f;
            }
        }
        F32 largest = 0.f;
        for (U32 v = 0; v < vertex_count; ++v)
        {
            const U32 r = root(remap[v]);
            if (used[v] && !kept[r])
            {
                LLVector4a centre = mean[r];
                centre.mul(1.f / weight[r]);
                LLVector4a offset;
                offset.setSub(positions[v], centre);
                largest = llmax(largest, sqrtf(offset.dot3(offset).getF32()));
            }
        }
        return extent > 0.f ? largest / extent : 0.f;
    }

    // A simplification of vertices [first, first + count) of mesh, by
    // indices numbered from first. Where prune is set, it may drop detached
    // parts as its error grows past their size, none larger than
    // PRUNE_LIMIT; where it would have dropped a larger one to reach its
    // target, or every part, it drops each part within PRUNE_LIMIT first
    // and simplifies what is left without dropping any more. An error
    // within PRUNE_LIMIT drops no part larger than it.
    size_t simplifyMesh(const Mesh& mesh, U32 first, U32 count, const U32* indices, size_t index_count, size_t target_count, F32 error,
                        U32 options, const std::vector<U8>& locks, bool prune, U32* destination, F32& result_error)
    {
        const LLVector4a* vertices = &mesh.positions[first];
        const F32* positions = vertices[0].getF32ptr();
        const F32* attributes = &mesh.attributes[(size_t)first * ATTRIBUTE_COUNT];
        const U8* lock = locks.empty() ? nullptr : locks.data();
        auto simplify = [&](const U32* from, size_t from_count, U32 with)
        {
            result_error = 0.f;
            return meshopt_simplifyWithAttributes(destination, from, from_count, positions, count, sizeof(LLVector4a), attributes,
                                                  ATTRIBUTE_COUNT * sizeof(F32), ATTRIBUTE_WEIGHTS, ATTRIBUTE_COUNT, lock,
                                                  llmin(target_count, from_count), error, with, &result_error);
        };
        if (!prune)
        {
            return simplify(indices, index_count, options);
        }

        const size_t simplified = simplify(indices, index_count, options | meshopt_SimplifyPrune);
        if (simplified >= 3 &&
            (error <= PRUNE_LIMIT ||
             largestDropped(vertices, count, indices, index_count, destination, simplified,
                            meshopt_simplifyScale(positions, count, sizeof(LLVector4a))) <= PRUNE_LIMIT))
        {
            return simplified;
        }

        std::vector<U32> small_parts_gone(index_count);
        const size_t left = meshopt_simplifyPrune(small_parts_gone.data(), indices, index_count, positions, count, sizeof(LLVector4a),
                                                  llmin(error, PRUNE_LIMIT));
        if (left < 3)
        {
            return simplify(indices, index_count, options);
        }
        return simplify(small_parts_gone.data(), left, options);
    }

    U32 seamOptions(ALMeshSimplifier::Seams seams, const ALMeshSimplifier::Target& goal)
    {
        U32 options = goal.distant ? meshopt_SimplifyRegularizeLight : 0;
        if (seams == ALMeshSimplifier::Seams::UV || seams == ALMeshSimplifier::Seams::NONE)
        {
            options |= meshopt_SimplifyPermissive;
        }
        return options;
    }

    // mesh's face simplified on its own, as ALMeshSimplifier::simplifyFace
    // makes it: its triangles, numbered as mesh's vertices are, in
    // simplified. Returns their index count, 0 where no triangle is left.
    size_t simplifyOwnFace(const Mesh& mesh, S32 face, const ALMeshSimplifier::Target& goal, ALMeshSimplifier::Seams seams,
                           std::vector<U32>& simplified)
    {
        using Seams = ALMeshSimplifier::Seams;
        const U32 first = mesh.first[face];
        const U32 vertex_count = mesh.first[face + 1] - first;
        const size_t index_count = mesh.first_index[face + 1] - mesh.first_index[face];
        simplified.clear();
        if (index_count < 3)
        {
            return 0;
        }

        // The face's own triangles, numbered from its first vertex.
        std::vector<U32> indices(mesh.indices.begin() + mesh.first_index[face], mesh.indices.begin() + mesh.first_index[face + 1]);
        for (U32& index : indices)
        {
            index -= first;
        }

        // Error is the model's, relative to its size; the simplifier takes it
        // relative to the face's.
        const F32 face_extent = meshopt_simplifyScale(mesh.positions[first].getF32ptr(), vertex_count, sizeof(LLVector4a));
        const F32 to_face = face_extent > 0.f ? mesh.extent / face_extent : 1.f;
        const F32 error = goal.error * to_face;

        const size_t target_count = targetCount(index_count, goal.decimator);
        simplified.resize(index_count);
        F32 result_error = 0.f;
        size_t count = 0;
        if (seams == Seams::SLOPPY)
        {
            count = meshopt_simplifySloppy(simplified.data(), indices.data(), index_count, mesh.positions[first].getF32ptr(), vertex_count,
                                           sizeof(LLVector4a), nullptr, target_count, llmin(error, 1.f), &result_error);
        }
        else
        {
            // A face that meets another keeps its vertices where it meets it,
            // which the other's simplification keeps as well, and all of its
            // parts, as a part of one face may be joined to another face's. A
            // face meets another along a border, one's or the other's; its
            // borders that meet no face are free, and so is its inside where
            // another face lies over it.
            const bool meets = std::any_of(mesh.shared.begin() + first, mesh.shared.begin() + first + vertex_count,
                                           [](U8 shared) { return shared != 0; });
            std::vector<U8> locks;
            if (seams != Seams::ALL)
            {
                protect(mesh, seams, first, vertex_count, locks);
            }
            if (meets)
            {
                locks.resize(vertex_count, 0);
                for (U32 v = 0; v < vertex_count; ++v)
                {
                    if (mesh.held[first + v])
                    {
                        locks[v] |= (U8)meshopt_SimplifyVertex_Lock;
                    }
                }
            }
            count = simplifyMesh(mesh, first, vertex_count, indices.data(), index_count, target_count, error, seamOptions(seams, goal), locks,
                                 goal.distant && !meets, simplified.data(), result_error);
        }
        if (count < 3)
        {
            simplified.clear();
            return 0;
        }

        simplified.resize(count);
        for (U32& index : simplified)
        {
            index += first;
        }
        return count;
    }

    // The vertices the triangles name.
    size_t namedVertexCount(const std::vector<U32>& indices, size_t vertex_count)
    {
        std::vector<U8> named(vertex_count, 0);
        size_t count = 0;
        for (U32 index : indices)
        {
            if (!named[index])
            {
                named[index] = 1;
                ++count;
            }
        }
        return count;
    }

    // What a simplified face takes from the face it is made from besides
    // its vertices and triangles.
    void copyLayout(const LLVolumeFace& layout, LLVolumeFace& face)
    {
        face.mID = layout.mID;
        face.mTypeMask = layout.mTypeMask;
        face.mExtents[0] = layout.mExtents[0];
        face.mExtents[1] = layout.mExtents[1];
        *face.mCenter = *layout.mCenter;
        face.mTexCoordExtents[0] = layout.mTexCoordExtents[0];
        face.mTexCoordExtents[1] = layout.mTexCoordExtents[1];
        face.mNormalizedScale = layout.mNormalizedScale;
        face.mOptimized = false;
    }

    // The face of one vertex at the origin and one triangle of it, that a
    // face left with no triangle becomes.
    void makeEmpty(const LLVolumeFace& layout, LLVolumeFace& face)
    {
        copyLayout(layout, face);
        ll_aligned_free_16(face.mWeights);
        face.mWeights = nullptr;
        face.resizeVertices(1);
        face.resizeIndices(3);
        memset(face.mIndices, 0, sizeof(U16) * 3);
        face.mPositions[0].clear();
        face.mNormals[0].clear();
        face.mTexCoords[0].setZero();
    }
} // namespace

// static
bool ALMeshSimplifier::compact(const LLVolume& base, S32 face, const U32* indices, size_t index_count, LLVolumeFace& target)
{
    const S32 faces = base.getNumVolumeFaces();
    std::vector<U32> first(faces + 1, 0);
    for (S32 f = 0; f < faces; ++f)
    {
        const LLVolumeFace& from = base.getVolumeFace(f);
        first[f + 1] = first[f] + (from.mPositions && from.mNumVertices > 0 ? (U32)from.mNumVertices : 0);
    }
    const size_t vertex_count = first.back();

    // Each vertex's place in target, in the order the triangles first name
    // it; one they do not name has none.
    std::vector<U32> place(vertex_count);
    const size_t kept = vertex_count ? meshopt_optimizeVertexFetchRemap(place.data(), indices, index_count, vertex_count) : 0;
    if (kept > U16_MAX)
    {
        return false;
    }

    const LLVolumeFace& layout = base.getVolumeFace(face);
    copyLayout(layout, target);
    target.resizeVertices((S32)kept);
    target.resizeIndices((S32)index_count);
    ll_aligned_free_16(target.mWeights);
    target.mWeights = nullptr;
    if (layout.mWeights)
    {
        target.allocateWeights((S32)kept);
    }
    if (layout.mTangents)
    {
        target.allocateTangents((S32)kept);
    }

    for (S32 f = 0; f < faces; ++f)
    {
        const LLVolumeFace& from = base.getVolumeFace(f);
        for (U32 v = first[f]; v < first[f + 1]; ++v)
        {
            const U32 to = place[v];
            if (to == U32_MAX)
            {
                continue;
            }
            const U32 at = v - first[f];
            target.mPositions[to] = from.mPositions[at];
            if (from.mNormals)
            {
                target.mNormals[to] = from.mNormals[at];
            }
            else
            {
                target.mNormals[to].clear();
            }
            if (from.mTexCoords)
            {
                target.mTexCoords[to] = from.mTexCoords[at];
            }
            else
            {
                target.mTexCoords[to].setZero();
            }
            if (target.mWeights)
            {
                if (from.mWeights)
                {
                    target.mWeights[to] = from.mWeights[at];
                }
                else
                {
                    target.mWeights[to].clear();
                }
            }
            if (target.mTangents)
            {
                if (from.mTangents)
                {
                    target.mTangents[to] = from.mTangents[at];
                }
                else
                {
                    target.mTangents[to].clear();
                }
            }
        }
    }

    for (size_t i = 0; i < index_count; ++i)
    {
        target.mIndices[i] = (U16)place[indices[i]];
    }

    if (!layout.mNormals)
    {
        target.mNormals = nullptr;
    }
    if (!layout.mTexCoords)
    {
        target.mTexCoords = nullptr;
    }
    return true;
}

ALMeshSimplifier::ALMeshSimplifier(const LLVolume& base)
    : mBase(base)
{
    auto mesh = std::make_unique<Mesh>();
    prepare(base, *mesh);
    mMesh = std::move(mesh);
}

ALMeshSimplifier::~ALMeshSimplifier() = default;

F32 ALMeshSimplifier::simplifyModel(LLVolume& target, const Target& goal, Seams seams) const
{
    const LLVolume& base = mBase;
    const Mesh& mesh = *mMesh;
    const size_t index_count = mesh.indices.size();
    if (index_count < 3)
    {
        return -1.f;
    }

    const size_t target_count = targetCount(index_count, goal.decimator);
    std::vector<U32> simplified(index_count);
    F32 result_error = 0.f;
    size_t count = 0;
    if (seams == Seams::SLOPPY)
    {
        count = meshopt_simplifySloppy(simplified.data(), mesh.indices.data(), index_count, mesh.positions[0].getF32ptr(),
                                       mesh.vertexCount(), sizeof(LLVector4a), nullptr, target_count, goal.error, &result_error);
    }
    else
    {
        // The simplifier keeps a seam between faces closed as it keeps any
        // other; free to cross seams, it is held at the faces' borders.
        std::vector<U8> locks;
        if (seams != Seams::ALL)
        {
            protect(mesh, seams, 0, (U32)mesh.vertexCount(), locks);
        }
        count = simplifyMesh(mesh, 0, (U32)mesh.vertexCount(), mesh.indices.data(), index_count, target_count, goal.error,
                             seamOptions(seams, goal), locks, goal.distant, simplified.data(), result_error);
    }
    if (count < 3)
    {
        return -1.f;
    }

    // Each face's triangles, those whose first vertex is one of its own.
    const S32 faces = base.getNumVolumeFaces();
    std::vector<std::vector<U32>> face_indices(faces);
    for (size_t i = 0; i < count; i += 3)
    {
        std::vector<U32>& to = face_indices[mesh.faceOf(simplified[i])];
        to.insert(to.end(), simplified.begin() + i, simplified.begin() + i + 3);
    }

    // Nothing is written to target until every face is known to fit.
    if (std::any_of(face_indices.begin(), face_indices.end(),
                    [&](const std::vector<U32>& indices) { return namedVertexCount(indices, mesh.vertexCount()) > U16_MAX; }))
    {
        LL_WARNS("MeshSimplifier") << "A face of a simplified model takes more vertices than 16-bit indices reach; "
                                   << "simplifying each face on its own" << LL_ENDL;
        count = 0;
        for (S32 f = 0; f < faces; ++f)
        {
            count += simplifyOwnFace(mesh, f, goal, seams, face_indices[f]);
        }
        if (count < 3)
        {
            return -1.f;
        }
    }

    for (S32 f = 0; f < faces; ++f)
    {
        if (face_indices[f].size() < 3 || !compact(base, f, face_indices[f].data(), face_indices[f].size(), target.getVolumeFace(f)))
        {
            // Every face fits, as counted above.
            llassert(face_indices[f].size() < 3);
            makeEmpty(base.getVolumeFace(f), target.getVolumeFace(f));
        }
    }
    return (F32)index_count / (F32)count;
}

F32 ALMeshSimplifier::simplifyFace(S32 face, LLVolumeFace& target, const Target& goal, Seams seams) const
{
    const Mesh& mesh = *mMesh;
    std::vector<U32> simplified;
    const size_t count = simplifyOwnFace(mesh, face, goal, seams, simplified);
    if (count < 3 || !compact(mBase, face, simplified.data(), count, target))
    {
        // A face's own vertices are never more than 16-bit indices reach.
        llassert(count < 3);
        makeEmpty(mBase.getVolumeFace(face), target);
        return -1.f;
    }
    return (F32)(mesh.first_index[face + 1] - mesh.first_index[face]) / (F32)count;
}
