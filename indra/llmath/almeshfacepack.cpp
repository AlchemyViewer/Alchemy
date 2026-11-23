/**
 * @file almeshfacepack.cpp
 * @brief A mesh asset's LOD face, repacked so that it zips smaller and
 *        draws the same
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

#include "llsd.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <vector>

namespace
{
    // The byte that ends a vertex's influences short of four.
    constexpr U8 END_INFLUENCES = 0xFF;

    // A vertex's influences as a key: their count, then up to four joints
    // and their weights' two bytes each, zero past the count.
    using Influences = std::array<U8, 13>;

    // A face at its sixteen-bit values.
    struct Face
    {
        size_t vertices = 0;
        std::vector<U16> positions;  // three a vertex
        std::vector<U16> normals;    // three a vertex
        std::vector<U16> tex_coords; // two a vertex
        std::vector<Influences> weights; // one a vertex, or none
        std::vector<U32> indices;
    };

    std::vector<U16> read_u16s(const LLSD::Binary& bytes)
    {
        std::vector<U16> values(bytes.size() / sizeof(U16));
        std::memcpy(values.data(), bytes.data(), values.size() * sizeof(U16));
        return values;
    }

    LLSD::Binary write_u16s(const U16* values, size_t count)
    {
        LLSD::Binary bytes(count * sizeof(U16));
        std::memcpy(bytes.data(), values, bytes.size());
        return bytes;
    }

    // Each vertex's influences, as LLVolume::decodeVolumeFaces reads them,
    // where the array ends exactly at the last vertex's.
    bool read_weights(const LLSD::Binary& bytes, Face& face)
    {
        face.weights.resize(face.vertices);
        size_t at = 0;
        for (Influences& vertex : face.weights)
        {
            vertex.fill(0);
            while (vertex[0] < 4)
            {
                if (at >= bytes.size())
                {
                    return false;
                }
                const U8 joint = bytes[at++];
                if (joint == END_INFLUENCES)
                {
                    break;
                }
                if (bytes.size() - at < 2)
                {
                    return false;
                }
                const size_t slot = 1 + (size_t)vertex[0] * 3;
                vertex[slot] = joint;
                vertex[slot + 1] = bytes[at++];
                vertex[slot + 2] = bytes[at++];
                ++vertex[0];
            }
        }
        return at == bytes.size();
    }

    std::optional<Face> read_face(const LLSD& sd)
    {
        if (!sd.isMap() || sd.has("NoGeometry"))
        {
            return std::nullopt;
        }
        const LLSD::Binary& pos = sd["Position"].asBinary();
        const LLSD::Binary& norm = sd["Normal"].asBinary();
        const LLSD::Binary& tc = sd["TexCoord0"].asBinary();
        const LLSD::Binary& idx = sd["TriangleList"].asBinary();
        Face face;
        face.vertices = pos.size() / (3 * sizeof(U16));
        if (face.vertices == 0 || pos.size() != face.vertices * 3 * sizeof(U16) || norm.size() != pos.size() ||
            tc.size() != face.vertices * 2 * sizeof(U16) || idx.empty() || idx.size() % (3 * sizeof(U16)) != 0)
        {
            return std::nullopt;
        }
        face.positions = read_u16s(pos);
        face.normals = read_u16s(norm);
        face.tex_coords = read_u16s(tc);
        const std::vector<U16> indices = read_u16s(idx);
        face.indices.assign(indices.begin(), indices.end());
        if (*std::max_element(face.indices.begin(), face.indices.end()) >= face.vertices)
        {
            return std::nullopt;
        }
        if (sd.has("Weights") && !read_weights(sd["Weights"].asBinary(), face))
        {
            return std::nullopt;
        }
        return face;
    }

    void write_face(LLSD& sd, const Face& face)
    {
        sd["Position"] = write_u16s(face.positions.data(), face.positions.size());
        sd["Normal"] = write_u16s(face.normals.data(), face.normals.size());
        sd["TexCoord0"] = write_u16s(face.tex_coords.data(), face.tex_coords.size());
        const std::vector<U16> indices(face.indices.begin(), face.indices.end());
        sd["TriangleList"] = write_u16s(indices.data(), indices.size());
        if (!face.weights.empty())
        {
            LLSD::Binary bytes;
            for (const Influences& vertex : face.weights)
            {
                bytes.insert(bytes.end(), vertex.begin() + 1, vertex.begin() + 1 + (size_t)vertex[0] * 3);
                if (vertex[0] < 4)
                {
                    bytes.push_back(END_INFLUENCES);
                }
            }
            sd["Weights"] = std::move(bytes);
        }
    }

    // The face with each vertex v moved to place[v], those placed at ~0u
    // dropped, kept of them left.
    void renumber(Face& face, const std::vector<U32>& place, size_t kept)
    {
        Face out;
        out.vertices = kept;
        out.positions.resize(kept * 3);
        out.normals.resize(kept * 3);
        out.tex_coords.resize(kept * 2);
        if (!face.weights.empty())
        {
            out.weights.resize(kept);
        }
        for (size_t v = 0; v < face.vertices; ++v)
        {
            const U32 to = place[v];
            if (to == ~0u)
            {
                continue;
            }
            std::copy_n(&face.positions[v * 3], 3, &out.positions[(size_t)to * 3]);
            std::copy_n(&face.normals[v * 3], 3, &out.normals[(size_t)to * 3]);
            std::copy_n(&face.tex_coords[v * 2], 2, &out.tex_coords[(size_t)to * 2]);
            if (!face.weights.empty())
            {
                out.weights[to] = face.weights[v];
            }
        }
        out.indices = std::move(face.indices);
        meshopt_remapIndexBuffer(out.indices.data(), out.indices.data(), out.indices.size(), place.data());
        face = std::move(out);
    }

    // Whether two vertices lie at one place in every pose: at one position,
    // and, where the face is rigged, moved by the same influences.
    bool same_place(const Face& face, U32 a, U32 b)
    {
        return std::equal(&face.positions[(size_t)a * 3], &face.positions[(size_t)a * 3] + 3, &face.positions[(size_t)b * 3]) &&
               (face.weights.empty() || face.weights[a] == face.weights[b]);
    }

    // A sixteen-bit value on a grid of the given bits: the nearest multiple of
    // the step, the top step held at 65535, which is a normal's +1 and a
    // weight's whole.
    U16 rounded_to_bits(U16 value, U32 bits)
    {
        const U32 step = 1u << (16u - bits);
        return (U16)std::min<U32>(65535u, ((U32)value + step / 2u) & ~(step - 1u));
    }
}

bool ALMeshFacePack::repack(LLSD& sd, Normals normals, Weights weights)
{
    std::optional<Face> read = read_face(sd);
    if (!read)
    {
        return false;
    }
    Face& face = *read;

    if (normals != Normals::KEEP)
    {
        const U32 bits = normals == Normals::SNAP_12_BITS ? 12u : 10u;
        for (U16& component : face.normals)
        {
            component = rounded_to_bits(component, bits);
        }
    }

    if (weights == Weights::ROUND_8_BITS)
    {
        for (Influences& vertex : face.weights)
        {
            // Each influence is its joint and its weight, low byte first.
            for (size_t slot = 1; slot < 1 + (size_t)vertex[0] * 3; slot += 3)
            {
                const U16 weight = rounded_to_bits((U16)(vertex[slot + 1] | (vertex[slot + 2] << 8)), 8);
                vertex[slot + 1] = (U8)(weight & 0xFF);
                vertex[slot + 2] = (U8)(weight >> 8);
            }
        }
    }

    // One vertex for each that differs in some array from every other.
    std::vector<meshopt_Stream> streams = {
        { face.positions.data(), sizeof(U16) * 3, sizeof(U16) * 3 },
        { face.normals.data(), sizeof(U16) * 3, sizeof(U16) * 3 },
        { face.tex_coords.data(), sizeof(U16) * 2, sizeof(U16) * 2 },
    };
    if (!face.weights.empty())
    {
        streams.push_back({ face.weights.data(), sizeof(Influences), sizeof(Influences) });
    }
    std::vector<U32> place(face.vertices);
    const size_t distinct =
        meshopt_generateVertexRemapMulti(place.data(), face.indices.data(), face.indices.size(), face.vertices, streams.data(), streams.size());
    renumber(face, place, distinct);

    // The triangles with area in some pose.
    size_t kept = 0;
    for (size_t t = 0; t < face.indices.size(); t += 3)
    {
        const U32 a = face.indices[t];
        const U32 b = face.indices[t + 1];
        const U32 c = face.indices[t + 2];
        if (same_place(face, a, b) || same_place(face, b, c) || same_place(face, a, c))
        {
            continue;
        }
        face.indices[kept++] = a;
        face.indices[kept++] = b;
        face.indices[kept++] = c;
    }
    if (kept == 0)
    {
        return false;
    }
    face.indices.resize(kept);

    meshopt_optimizeVertexCacheStrip(face.indices.data(), face.indices.data(), face.indices.size(), face.vertices);

    // Each vertex numbered by the triangle that first takes it.
    place.assign(face.vertices, 0);
    const size_t used = meshopt_optimizeVertexFetchRemap(place.data(), face.indices.data(), face.indices.size(), face.vertices);
    renumber(face, place, used);

    // Each triangle from its lowest vertex.
    for (size_t t = 0; t < face.indices.size(); t += 3)
    {
        U32* triangle = &face.indices[t];
        const size_t lowest = triangle[1] < triangle[0] ? (triangle[2] < triangle[1] ? 2 : 1) : (triangle[2] < triangle[0] ? 2 : 0);
        std::rotate(triangle, triangle + lowest, triangle + 3);
    }

    write_face(sd, face);
    return true;
}
