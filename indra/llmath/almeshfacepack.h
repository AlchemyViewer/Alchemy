/**
 * @file almeshfacepack.h
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

#ifndef AL_MESHFACEPACK_H
#define AL_MESHFACEPACK_H

class LLSD;

// A face of a mesh asset's LOD block, as LLModel::writeModel writes it and
// LLVolume::decodeVolumeFaces reads it, at the sixteen-bit values it holds.
// The download weight is charged by the zipped size of the block, and what
// a viewer draws of a face does not depend on the order of its vertices or
// triangles: it welds the vertices and orders both again as it loads them.
// A repacked face holds the same triangles in an order that zips smaller:
//  - a vertex the same in every array as another is one vertex;
//  - a triangle naming a vertex twice, or two vertices at one position that
//    the same influences move, has no area in any pose and is dropped, and a
//    vertex no triangle names goes with it;
//  - the triangles run as meshoptimizer's strips order them, the vertices
//    numbered as the triangles first take them, and each triangle then
//    starts at its lowest-numbered vertex, its winding kept.
// Over a viewer's mesh cache this takes about 6% off a LOD block zipped by
// zip_llsd_model.
//
// It may also snap the normals, the one change to what a face draws: each
// component to the nearest multiple of 16 (12 bits) or 64 (10 bits) of its
// sixteen bits, the top step held at 65535 so that an axis keeps its ends
// and its middle, and vertices that then match in every array are welded
// with the rest. On the 12-bit grid a normal turns by 0.024 degrees at most,
// well under what an 8-bit normal map holds or what shading across a
// triangle makes of a curved surface, and that takes about 4% more off. On
// the 10-bit grid it turns by 0.096 degrees at most, 0.042 on average, under
// half what the G-buffer keeps of a normal without HDR; a smooth, curved,
// mirror-like surface may show it in a sharp reflection. That takes about
// 7% more off.
//
// A rigged face's skin weights may be rounded to 8 bits as well, as glTF's
// unsigned-byte weights hold them: each to the nearest multiple of 256, the
// top step held at 65535. A viewer normalizes a vertex's weights as it skins
// it, so a skinned vertex moves by at most 0.5% (0.1% on average) of how far
// apart its joints would each carry it, half a millimetre where they part by
// 10 cm; clothing fitted closely over another mesh may show it in a pose
// that bends a joint far. Vertices that then match are welded, and
// triangles that then have no area in any pose dropped, with the rest. That
// takes about 11% more off a rigged LOD block.
class ALMeshFacePack
{
public:
    enum class Normals
    {
        KEEP,
        SNAP_12_BITS,
        SNAP_10_BITS,
    };

    enum class Weights
    {
        KEEP,
        ROUND_8_BITS,
    };

    // Repacks one face in place. False, leaving it as it was, for a face
    // with no geometry, one whose arrays do not each hold its vertices, whose
    // weights do not read to their end at its last vertex, which names a
    // vertex it does not hold, or which has no triangle with area in any
    // pose.
    static bool repack(LLSD& face, Normals normals = Normals::KEEP, Weights weights = Weights::KEEP);
};

#endif // AL_MESHFACEPACK_H
