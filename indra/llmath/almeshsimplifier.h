/**
 * @file almeshsimplifier.h
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

#ifndef AL_MESHSIMPLIFIER_H
#define AL_MESHSIMPLIFIER_H

#include "stdtypes.h"

#include <memory>

class LLVolume;
class LLVolumeFace;

// The mesh uploader's simplification of a model: the faces of one volume
// simplified into the faces of another, all of them together or one at a
// time. The error the simplifier weighs is the model's in the shape it was
// authored in, its positions scaled back out of the unit cube by each face's
// mNormalizedScale, and it counts how far the normals and texture
// coordinates drift as well as the positions. A simplified face takes its
// vertices from the base face unchanged, each attribute as it was: its
// position too, so whatever is keyed by position, as a model's skin weights
// are, still finds it. A simplifier takes its model's faces as it weighs
// them once, for every simplification it makes of the model.
class ALMeshSimplifier
{
public:
    // Which seams a simplification keeps in place. A seam is where one
    // position is several vertices, that differ in normal or texture
    // coordinate; a simplification free to cross one moves or removes it
    // where the error allows. The borders between a model's faces stay
    // where they are in every mode but SLOPPY.
    enum class Seams
    {
        ALL,    // every seam
        UV,     // texture coordinate seams; normal seams are free
        NONE,   // none but the borders between faces
        SLOPPY, // none: vertices merge by place, whatever joins them
    };

    struct Target
    {
        // The share of the base's triangles to keep, as the base's index
        // count over this; 0 keeps a triangle and leaves the error to stop
        // the simplification.
        F32 decimator = 0.f;

        // The most error to accept, relative to the model's size: 0.01 is
        // a deviation of one hundredth of its largest extent.
        F32 error = 1.f;

        // A level of detail seen from far away: its triangles are of more
        // even size and shape, at a little cost in error, and it may drop
        // detached parts that are small next to the model.
        bool distant = false;
    };

    // base's faces as the simplifier takes them.
    struct Mesh;

    // A simplifier of base, which must outlive it unchanged.
    explicit ALMeshSimplifier(const LLVolume& base);
    ~ALMeshSimplifier();

    // Simplifies base's faces together, as one mesh, into target's, which
    // has as many faces. A triangle belongs to the face its first vertex
    // came from. A part detached from the rest is dropped only with all of
    // its faces. Returns the ratio of base's indices to the simplification's,
    // or -1 where no triangle was left, in which case target's faces are as
    // they were. A face left with no triangle is one of three indices to a
    // vertex at the origin. Where a face would take more vertices than 16-bit
    // indices reach, each face is simplified on its own instead, as
    // simplifyFace simplifies it.
    F32 simplifyModel(LLVolume& target, const Target& goal, Seams seams) const;

    // Simplifies base's face on its own into target. A face that shares a
    // position with any other face of base keeps its vertices where they
    // are at those of the positions that lie on either face's open border,
    // so that no gap opens between the two, and keeps all of its parts;
    // only one that shares none may lose its small detached parts. Where two
    // faces overlap, each is free inside its borders. Returns as
    // simplifyModel does; where no triangle is left, target is the face of
    // three indices to a vertex at the origin.
    F32 simplifyFace(S32 face, LLVolumeFace& target, const Target& goal, Seams seams) const;

    // The face of base's face's layout that the given triangles make: the
    // vertices they name and no others, each with every attribute it has in
    // base, in the order the triangles first name them. The indices number
    // base's vertices face after face, so that a triangle may take vertices
    // from any face. Returns false, leaving target as it was, where the
    // triangles name more vertices than 16-bit indices reach.
    static bool compact(const LLVolume& base, S32 face, const U32* indices, size_t index_count, LLVolumeFace& target);

private:
    const LLVolume& mBase;
    std::unique_ptr<const Mesh> mMesh;
};

#endif // AL_MESHSIMPLIFIER_H
