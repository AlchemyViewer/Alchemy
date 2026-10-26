/**
 * @file llvolume_test.cpp
 * @brief The prim, sculpt and mesh face generators against the invariants
 *        every face they make must keep.
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

// Every face is held to the same invariants: its layout and face IDs, finite
// unit normals and tangents, no triangle facing against its own normals, caps
// facing away from the wall they close, UVs affine in position on a
// parallelogram face, and normals that agree with meshoptimizer's own on a
// smooth face. The prim sweep crosses every profile, hole and path with the
// parameters the build floater offers and builds each at all four LODs, as
// LLVolumeLODGroup does; the sculpts are synthetic maps of each stitching; the
// mesh corpus is built here and goes through cacheOptimize as a decoded LOD
// does.
//
// What the generator gets wrong is a case of its own, tagged [!shouldfail]:
// the case holds the generator to what it should do, so it fails, as
// expected, while the defect stands, and the fix that clears the defect
// removes the tag. A case may inspect only the faces of a region, which
// names them by what they are, not by how they come out.

#include "linden_common.h"

#include "llvolume.h"
#include "llvolumemgr.h"
#include "llmath.h"
#include "llquaternion.h"
#include "m3math.h"
#include "v2math.h"
#include "v3math.h"
#include "v4math.h"
#include "llvector4a.h"
#include "alscratcharena.h"
#include "alsimdkernels.h"
#include "almeshcorpus.h"
#include "altestfatal.h"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    // LLVolumeLODGroup's detail scales, lowest LOD first.
    constexpr F32 LOD_DETAIL[] = { 1.f, 1.5f, 2.5f, 4.f };
    constexpr S32 LOD_COUNT = 4;

    // Positions this close are one point.
    constexpr F32 COINCIDE = 1e-5f;

    // normalize3fast's reciprocal square root is good to about 4e-4.
    constexpr F32 UNIT_TOLERANCE = 1e-3f;

    // A vertex as the face holds it: position, normal and texture coordinate.
    constexpr size_t VERTEX_BYTES = 2 * sizeof(LLVector4a) + sizeof(LLVector2);

    LLVector3 v3(const LLVector4a& v)
    {
        return LLVector3(v.getF32ptr());
    }

    bool finite3(const LLVector3& v)
    {
        return std::isfinite(v.mV[0]) && std::isfinite(v.mV[1]) && std::isfinite(v.mV[2]);
    }

    F32 degreesBetween(const LLVector3& a, const LLVector3& b)
    {
        const F32 la = a.length();
        const F32 lb = b.length();
        if (!(la > 0.f) || !(lb > 0.f))
        {
            return 180.f;
        }
        // From the cross and dot products in double: a float's arc cosine
        // resolves nothing finer than 0.02 degrees near 0.
        const double cross[3] = { (double)a.mV[1] * b.mV[2] - (double)a.mV[2] * b.mV[1], (double)a.mV[2] * b.mV[0] - (double)a.mV[0] * b.mV[2],
                                  (double)a.mV[0] * b.mV[1] - (double)a.mV[1] * b.mV[0] };
        const double dot = (double)a.mV[0] * b.mV[0] + (double)a.mV[1] * b.mV[1] + (double)a.mV[2] * b.mV[2];
        return (F32)std::atan2(std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]), dot) * RAD_TO_DEG;
    }

    bool coincide(const LLVector4a& a, const LLVector4a& b)
    {
        return (v3(a) - v3(b)).length() <= COINCIDE;
    }

    std::array<U32, 3> bitsOf(const LLVector4a& v)
    {
        std::array<U32, 3> bits;
        std::memcpy(bits.data(), v.getF32ptr(), sizeof(bits));
        return bits;
    }

    // ------------------------------------------------------------------
    // Faces

    struct Triangle
    {
        U32 v[3];
        LLVector3 normal; // twice the area, along the winding's normal
        bool degenerate;
    };

    // A triangle is degenerate when its area is nothing against its edges:
    // two corners coincide, or the three are in a line.
    std::vector<Triangle> trianglesOf(const LLVolumeFace& face)
    {
        std::vector<Triangle> out(face.mNumIndices / 3);
        for (size_t t = 0; t < out.size(); ++t)
        {
            Triangle& tri = out[t];
            for (S32 k = 0; k < 3; ++k)
            {
                tri.v[k] = face.mIndices[t * 3 + k];
            }
            const LLVector3 a = v3(face.mPositions[tri.v[0]]);
            const LLVector3 b = v3(face.mPositions[tri.v[1]]);
            const LLVector3 c = v3(face.mPositions[tri.v[2]]);
            tri.normal = (b - a) % (c - a);
            const F32 scale = (b - a).lengthSquared() + (c - a).lengthSquared() + (c - b).lengthSquared();
            tri.degenerate = (b - a).length() <= COINCIDE || (c - a).length() <= COINCIDE || (c - b).length() <= COINCIDE ||
                             !(tri.normal.length() > 1e-5f * scale);
        }
        return out;
    }

    bool isCap(const LLVolumeFace& face)
    {
        return face.mTypeMask & LLVolumeFace::CAP_MASK;
    }

    // A flat inner side copies each of its hole's corners, so that each is a
    // hard edge: the quad between a corner and its copy has no width.
    bool isDoubled(const LLVolumeFace& face)
    {
        return (face.mTypeMask & LLVolumeFace::INNER_MASK) && (face.mTypeMask & LLVolumeFace::FLAT_MASK) && face.mNumS > 2;
    }

    // Sides are an mNumS by mNumT grid, a row a path station.
    bool isGrid(const LLVolumeFace& face)
    {
        return !isCap(face) && face.mNumS > 1 && face.mNumT > 1 && face.mNumVertices == face.mNumS * face.mNumT;
    }

    S32 gridIndex(const LLVolumeFace& face, S32 s, S32 t)
    {
        return s + face.mNumS * t;
    }

    bool columnsMeet(const LLVolumeFace& face)
    {
        for (S32 t = 0; t < face.mNumT; ++t)
        {
            if (!coincide(face.mPositions[gridIndex(face, 0, t)], face.mPositions[gridIndex(face, face.mNumS - 1, t)]))
            {
                return false;
            }
        }
        return true;
    }

    bool rowsMeet(const LLVolumeFace& face)
    {
        for (S32 s = 0; s < face.mNumS; ++s)
        {
            if (!coincide(face.mPositions[gridIndex(face, s, 0)], face.mPositions[gridIndex(face, s, face.mNumT - 1)]))
            {
                return false;
            }
        }
        return true;
    }

    std::string faceName(const LLVolumeFace& face)
    {
        const char* kind = isCap(face)                                 ? ((face.mTypeMask & LLVolumeFace::TOP_MASK) ? "top cap" : "bottom cap")
                           : (face.mTypeMask & LLVolumeFace::END_MASK)   ? "cut end"
                           : (face.mTypeMask & LLVolumeFace::INNER_MASK) ? "inner side"
                                                                         : "outer side";
        return fmt::format("face {} ({})", face.mID, kind);
    }

    // Vertices of a face that are one point, joined: the seam of a closed
    // side, the copies of a flat inner side, a pole.
    std::vector<U32> weldGroups(const LLVolumeFace& face)
    {
        const S32 count = face.mNumVertices;
        std::vector<U32> parent(count);
        std::iota(parent.begin(), parent.end(), 0);
        auto root = [&](U32 v)
        {
            while (parent[v] != v)
            {
                v = parent[v] = parent[parent[v]];
            }
            return v;
        };
        std::vector<U32> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](U32 a, U32 b) { return face.mPositions[a][0] < face.mPositions[b][0]; });
        for (S32 i = 0; i < count; ++i)
        {
            for (S32 j = i + 1; j < count && face.mPositions[order[j]][0] - face.mPositions[order[i]][0] <= COINCIDE; ++j)
            {
                if (coincide(face.mPositions[order[i]], face.mPositions[order[j]]))
                {
                    parent[root(order[j])] = root(order[i]);
                }
            }
        }
        for (S32 v = 0; v < count; ++v)
        {
            parent[v] = root(v);
        }
        return parent;
    }

    // Per-vertex normals from meshoptimizer's generator with every edge
    // smoothed, over the face's points welded as weldGroups joins them: an
    // independent answer to what a smooth face's normals should be.
    std::vector<LLVector3> oracleNormals(const LLVolumeFace& face, const std::vector<U32>& groups)
    {
        std::vector<U32> indices(face.mIndices, face.mIndices + face.mNumIndices);
        std::vector<LLVector4a> welded(face.mPositions, face.mPositions + face.mNumVertices);
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            welded[v] = face.mPositions[groups[v]];
        }
        std::vector<F32> corners(indices.size() * 3);
        meshopt_generateNormals(corners.data(), indices.data(), indices.size(), welded[0].getF32ptr(), welded.size(), sizeof(LLVector4a),
                                F_PI * 0.999f, 0.f);
        std::vector<LLVector3> out(face.mNumVertices, LLVector3::zero);
        for (size_t c = indices.size(); c-- > 0;)
        {
            out[indices[c]].set(&corners[c * 3]);
        }
        return out;
    }

    // ------------------------------------------------------------------
    // What a run found

    struct Report
    {
        S32 failures = 0;
        std::vector<std::string> examples;

        void fail(std::string what)
        {
            ++failures;
            if (examples.size() < 6)
            {
                examples.push_back(std::move(what));
            }
        }

        std::string text() const
        {
            std::string out = fmt::format("{} failure(s)", failures);
            for (const std::string& example : examples)
            {
                out += "\n  " + example;
            }
            return out;
        }
    };

    // ------------------------------------------------------------------
    // Shapes

    struct Shape
    {
        std::string name;
        U8 profile = LL_PCODE_PROFILE_SQUARE;
        U8 hole = LL_PCODE_HOLE_SAME;
        U8 path = LL_PCODE_PATH_LINE;
        F32 hollow = 0.f;
        F32 cut_begin = 0.f; // the profile cut, a sphere's dimple
        F32 cut_end = 1.f;
        F32 path_begin = 0.f; // the path cut, the advanced cut on a box
        F32 path_end = 1.f;
        F32 twist_begin = 0.f;
        F32 twist_end = 0.f;
        F32 scale_x = 1.f; // the top size on a line, the hole size on a circle
        F32 scale_y = 1.f;
        F32 shear_x = 0.f;
        F32 shear_y = 0.f;
        F32 taper_x = 0.f;
        F32 taper_y = 0.f;
        F32 skew = 0.f;
        F32 radius_offset = 0.f;
        F32 revolutions = 1.f;
        // The skew as LLVolumeMessage unpacks it, unclipped by the
        // revolutions: what another viewer or a script may have made.
        bool wire_skew = false;

        // In the order LLPanelObject sets them, each setter clamping as it
        // does there.
        LLVolumeParams params() const
        {
            LLVolumeParams p;
            p.setType((U8)(profile | hole), path);
            p.setBeginAndEndS(cut_begin, cut_end);
            p.setBeginAndEndT(path_begin, path_end);
            p.setHollow(hollow);
            p.setTwistBegin(twist_begin);
            p.setTwistEnd(twist_end);
            p.setRatio(scale_x, scale_y);
            p.setTaper(taper_x, taper_y);
            p.setRevolutions(revolutions);
            p.setRadiusOffset(radius_offset);
            p.setSkew(skew);
            p.setShear(shear_x, shear_y);
            if (wire_skew)
            {
                p.getPathParams().setSkew(skew);
            }
            return p;
        }
    };

    Shape shape(const char* name, U8 profile, U8 path)
    {
        Shape s;
        s.name = name;
        s.profile = profile;
        s.path = path;
        if (path != LL_PCODE_PATH_LINE && profile != LL_PCODE_PROFILE_CIRCLE_HALF)
        {
            // The hole size LLToolPlacer gives a new torus, tube or ring.
            s.scale_y = 0.25f;
        }
        return s;
    }

    Shape box() { return shape("box", LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE); }
    Shape cylinder() { return shape("cylinder", LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_LINE); }
    Shape prism() { return shape("prism", LL_PCODE_PROFILE_EQUALTRI, LL_PCODE_PATH_LINE); }
    Shape sphere() { return shape("sphere", LL_PCODE_PROFILE_CIRCLE_HALF, LL_PCODE_PATH_CIRCLE); }
    Shape torus() { return shape("torus", LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_CIRCLE); }
    Shape tube() { return shape("tube", LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_CIRCLE); }
    Shape ring() { return shape("ring", LL_PCODE_PROFILE_EQUALTRI, LL_PCODE_PATH_CIRCLE); }

    LLPointer<LLVolume> build(const Shape& s, S32 lod, bool unique = false)
    {
        return new LLVolume(s.params(), LOD_DETAIL[lod], false, unique);
    }

    std::string lodName(const Shape& s, S32 lod)
    {
        return fmt::format("{} at LOD {}", s.name, lod);
    }

    bool isTriangle(U8 profile)
    {
        return profile == LL_PCODE_PROFILE_ISOTRI || profile == LL_PCODE_PROFILE_EQUALTRI || profile == LL_PCODE_PROFILE_RIGHTTRI;
    }

    // Every profile and hole on every path, each plain and with each of the
    // parameters the build floater offers, and some with all of them.
    std::vector<Shape> sweepShapes()
    {
        struct Named
        {
            const char* name;
            U8 code;
        };
        const Named profiles[] = {
            { "square", LL_PCODE_PROFILE_SQUARE },
            { "circle", LL_PCODE_PROFILE_CIRCLE },
            { "half circle", LL_PCODE_PROFILE_CIRCLE_HALF },
            { "isosceles triangle", LL_PCODE_PROFILE_ISOTRI },
            { "equilateral triangle", LL_PCODE_PROFILE_EQUALTRI },
            { "right triangle", LL_PCODE_PROFILE_RIGHTTRI },
        };
        const Named paths[] = {
            { "line", LL_PCODE_PATH_LINE },
            { "circle", LL_PCODE_PATH_CIRCLE },
            { "circle2", LL_PCODE_PATH_CIRCLE2 },
        };
        struct Hole
        {
            const char* name;
            U8 code;
            F32 hollow;
        };
        const Hole holes[] = {
            { "solid", LL_PCODE_HOLE_SAME, 0.f },
            { "hollow", LL_PCODE_HOLE_SAME, 0.5f },
            { "circle hole", LL_PCODE_HOLE_CIRCLE, 0.5f },
            { "square hole", LL_PCODE_HOLE_SQUARE, 0.5f },
            { "triangle hole", LL_PCODE_HOLE_TRIANGLE, 0.5f },
            { "square hole at 0.85", LL_PCODE_HOLE_SQUARE, 0.85f },
        };

        std::vector<Shape> out;
        for (const Named& profile : profiles)
        {
            for (const Named& path : paths)
            {
                for (const Hole& hole : holes)
                {
                    Shape base = shape("", profile.code, path.code);
                    base.hole = hole.code;
                    base.hollow = hole.hollow;
                    const bool round_path = path.code != LL_PCODE_PATH_LINE;
                    auto add = [&](const char* variation, auto&& edit)
                    {
                        Shape s = base;
                        edit(s);
                        s.name = fmt::format("{} {} on a {} path, {}", profile.name, hole.name, path.name, variation);
                        out.push_back(s);
                    };
                    add("plain", [](Shape&) {});
                    add("profile cut", [](Shape& s) { s.cut_begin = 0.15f; s.cut_end = 0.8f; });
                    add("path cut", [](Shape& s) { s.path_begin = 0.2f; s.path_end = 0.85f; });
                    add("twisted", [](Shape& s) { s.twist_begin = -0.5f; s.twist_end = 0.75f; });
                    add("tapered", [&](Shape& s)
                    {
                        if (round_path) { s.taper_x = 0.4f; s.taper_y = -0.3f; }
                        else { s.scale_x = 0.4f; s.scale_y = 0.7f; }
                    });
                    add("tapered to a point", [&](Shape& s)
                    {
                        if (round_path) { s.taper_x = 1.f; s.taper_y = 1.f; }
                        else { s.scale_x = 0.f; s.scale_y = 0.f; }
                    });
                    add("top sheared", [](Shape& s) { s.shear_x = 0.3f; s.shear_y = -0.2f; });
                    add("cut twisted tapered and sheared", [&](Shape& s)
                    {
                        s.cut_begin = 0.1f; s.cut_end = 0.9f;
                        s.path_begin = 0.05f; s.path_end = 0.95f;
                        s.twist_begin = -0.25f; s.twist_end = 0.5f;
                        s.shear_x = 0.2f; s.shear_y = 0.1f;
                        if (round_path) { s.taper_x = 0.3f; s.taper_y = 0.2f; }
                        else { s.scale_x = 0.6f; s.scale_y = 0.8f; }
                    });
                    if (round_path)
                    {
                        add("skewed", [](Shape& s) { s.skew = 0.4f; });
                        add("radius offset", [](Shape& s) { s.radius_offset = 0.3f; });
                        add("one and a half turns", [](Shape& s) { s.revolutions = 1.5f; });
                        if (profile.code != LL_PCODE_PROFILE_CIRCLE_HALF)
                        {
                            add("hole size", [](Shape& s) { s.scale_x = 0.6f; s.scale_y = 0.15f; });
                        }
                        add("two skewed turns", [](Shape& s)
                        {
                            s.revolutions = 2.f; s.skew = 0.7f; s.radius_offset = -0.2f; s.taper_x = 0.2f; s.twist_end = 0.5f;
                        });
                    }
                }
            }
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Face IDs, from the parameters: the path is open unless it closes on
    // itself, and an open path has a cap at each end; the profile is open
    // where it is cut, and a hollow half circle always is.

    std::vector<LLFaceID> expectedFaceIDs(const LLVolumeParams& params)
    {
        const LLProfileParams& profile = params.getProfileParams();
        const LLPathParams& path = params.getPathParams();
        const U8 curve = profile.getCurveType() & LL_PCODE_PROFILE_MASK;
        const bool hollow = profile.getHollow() > 0.f;
        const F32 begin = profile.getBegin();
        const F32 end = profile.getEnd();

        bool path_open = true;
        if (path.getCurveType() == LL_PCODE_PATH_CIRCLE || path.getCurveType() == LL_PCODE_PATH_CIRCLE2)
        {
            F32 taper_x_begin = 1.f, taper_x_end = 1.f - path.getTaperX();
            if (taper_x_end > 1.f) { taper_x_begin = 2.f - taper_x_end; taper_x_end = 1.f; }
            F32 taper_y_begin = 1.f, taper_y_end = 1.f - path.getTaperY();
            if (taper_y_end > 1.f) { taper_y_begin = 2.f - taper_y_end; taper_y_end = 1.f; }
            path_open = path.getEnd() - path.getBegin() < 1.f || fabsf(path.getSkew()) > 0.001f || fabsf(taper_x_end - taper_x_begin) > 0.001f ||
                        fabsf(taper_y_end - taper_y_begin) > 0.001f || fabsf(path.getRadiusOffset()) > 0.001f;
        }
        if (path.getTwist() != path.getTwistBegin())
        {
            path_open = true;
        }

        const bool profile_open = curve == LL_PCODE_PROFILE_CIRCLE_HALF ? (end - begin < 1.f || hollow) : (end - begin < 0.99f);

        std::vector<LLFaceID> ids;
        if (path_open)
        {
            ids.push_back(LL_FACE_PATH_BEGIN);
        }
        if (curve == LL_PCODE_PROFILE_SQUARE || isTriangle(curve))
        {
            const F32 sides = curve == LL_PCODE_PROFILE_SQUARE ? 4.f : 3.f;
            for (S32 i = llfloor(begin * sides); i < llfloor(end * sides + .999f); ++i)
            {
                ids.push_back(LL_FACE_OUTER_SIDE_0 << i);
            }
        }
        else
        {
            ids.push_back(LL_FACE_OUTER_SIDE_0);
        }
        if (hollow)
        {
            ids.push_back(LL_FACE_INNER_SIDE);
        }
        if (path_open)
        {
            ids.push_back(LL_FACE_PATH_END);
        }
        if (profile_open)
        {
            ids.push_back(LL_FACE_PROFILE_BEGIN);
            ids.push_back(LL_FACE_PROFILE_END);
        }
        return ids;
    }

    std::vector<LLFaceID> faceIDsOf(const LLVolume& volume)
    {
        std::vector<LLFaceID> ids;
        for (const LLProfile::Face& face : volume.getProfile().mFaces)
        {
            ids.push_back(face.mFaceID);
        }
        return ids;
    }

    // ------------------------------------------------------------------
    // The checks, each a bit; a region excuses some of them on its faces.

    namespace Checks
    {
        constexpr U32 Layout = 1 << 0;      // counts, IDs, indices in range
        constexpr U32 Finite = 1 << 1;      // positions, UVs and normals
        constexpr U32 RawUnit = 1 << 2;     // normals unit length as generated
        constexpr U32 Inversion = 1 << 3;   // no triangle faces against its corners' normals
        constexpr U32 CapFacing = 1 << 4;   // a cap faces away from its wall
        constexpr U32 AffineUV = 1 << 5;    // a parallelogram face's UVs are affine in position
        constexpr U32 Oracle = 1 << 6;      // a smooth face agrees with meshoptimizer's normals
        constexpr U32 SeamsAgree = 1 << 7;  // a smooth closed seam has one normal
        constexpr U32 HardEdges = 1 << 8;   // a flat inner side is hard at every point
        constexpr U32 Tangents = 1 << 9;    // after genTangents: unit normals, unit tangents, w = +/-1
        constexpr U32 Orthogonal = 1 << 10; // after genTangents: tangent orthogonal to normal
        constexpr U32 LoneNormals = 1 << 11; // a vertex in no triangle with area takes a normal the face has
        constexpr U32 Meshopt = 1 << 12;     // after genTangents: meshoptimizer's tangents, where it gives a vertex one

        // What the sweep holds every face to. Orthogonal is broken wherever
        // the tangent generator makes a tangent up, and has a case of its
        // own.
        constexpr U32 Sweep =
            Layout | Finite | RawUnit | Inversion | CapFacing | AffineUV | Oracle | SeamsAgree | HardEdges | Tangents | LoneNormals;
    }

    namespace Regions
    {
        // A cap with no area.
        constexpr U32 CapNoArea = 1 << 2;
    }

    // Vertices that no triangle with area uses.
    std::vector<bool> loneVertices(const LLVolumeFace& face, const std::vector<Triangle>& triangles)
    {
        std::vector<bool> lone(face.mNumVertices, true);
        for (const Triangle& tri : triangles)
        {
            if (!tri.degenerate)
            {
                for (U32 v : tri.v)
                {
                    lone[v] = false;
                }
            }
        }
        return lone;
    }

    bool isSculpt(const LLVolume& volume)
    {
        return (volume.getParams().getSculptType() & LL_SCULPT_TYPE_MASK) != LL_SCULPT_TYPE_NONE;
    }

    // Whether a volume's sides take the normals of the surface they are cut
    // from: a prim on a line or circle path. A sculpt's, a flexible prim's
    // and a circle2 path's come from their triangles.
    bool sweptSides(const LLVolume& volume)
    {
        const U8 path = volume.getParams().getPathParams().getCurveType();
        return !isSculpt(volume) && (path == LL_PCODE_PATH_LINE || path == LL_PCODE_PATH_CIRCLE);
    }

    U32 regionsOf(const LLVolumeFace& face, const std::vector<Triangle>& triangles)
    {
        U32 regions = 0;
        if (isCap(face) && std::all_of(triangles.begin(), triangles.end(), [](const Triangle& t) { return t.degenerate; }))
        {
            regions |= Regions::CapNoArea;
        }
        return regions;
    }

    struct Inspect
    {
        U32 checks = Checks::Sweep;
        // Nonzero: inspect only the faces in these regions.
        U32 only_regions = 0;
        // Check inversion also where the surface folds at a point.
        bool inversion_any_spread = false;
        // Where Checks::Meshopt keeps what it found.
        struct TangentAgreement* agreement = nullptr;
    };

    // ------------------------------------------------------------------
    // The checks

    // Whether the face can be read at all: its counts and indices.
    bool checkLayout(const LLVolumeFace& face, S32 index, Report& report, const std::string& where, bool active)
    {
        auto fail = [&](std::string what)
        {
            if (active)
            {
                report.fail(fmt::format("{}: {}", where, what));
            }
        };
        if (face.mID != index)
        {
            fail(fmt::format("mID {} at index {}", face.mID, index));
        }
        if (face.mNumVertices < 3 || !face.mPositions || !face.mNormals || !face.mTexCoords)
        {
            fail(fmt::format("{} vertices", face.mNumVertices));
            return false;
        }
        if (face.mNumIndices < 3 || face.mNumIndices % 3 != 0 || !face.mIndices)
        {
            fail(fmt::format("{} indices", face.mNumIndices));
            return false;
        }
        for (S32 i = 0; i < face.mNumIndices; ++i)
        {
            if (face.mIndices[i] >= face.mNumVertices)
            {
                fail(fmt::format("index {} is {} of {} vertices", i, face.mIndices[i], face.mNumVertices));
                return false;
            }
        }
        if (!isCap(face))
        {
            if (face.mNumVertices != face.mNumS * face.mNumT)
            {
                fail(fmt::format("{} vertices for a grid of {} by {}", face.mNumVertices, face.mNumS, face.mNumT));
                return true;
            }
            // Each triangle is one of the grid's, bottom left, top right,
            // top left or bottom left, bottom right, top right, a quad, and
            // none twice; a triangle of the grid's left out has no area.
            std::map<std::array<U32, 3>, bool> grid;
            for (S32 t = 0; t + 1 < face.mNumT; ++t)
            {
                for (S32 s = 0; s + 1 < face.mNumS; ++s)
                {
                    const U32 bl = gridIndex(face, s, t), br = bl + 1, tl = gridIndex(face, s, t + 1), tr = tl + 1;
                    grid[{ bl, tr, tl }] = false;
                    grid[{ bl, br, tr }] = false;
                }
            }
            for (S32 i = 0; i + 2 < face.mNumIndices; i += 3)
            {
                const auto found = grid.find({ face.mIndices[i], face.mIndices[i + 1], face.mIndices[i + 2] });
                if (found == grid.end() || found->second)
                {
                    fail(fmt::format("triangle {} ({}, {}, {}) is {} of the grid's", i / 3, face.mIndices[i], face.mIndices[i + 1], face.mIndices[i + 2],
                                     found == grid.end() ? "none" : "a second"));
                    return true;
                }
                found->second = true;
            }
            for (const auto& [corners, used] : grid)
            {
                const LLVector3 a = v3(face.mPositions[corners[0]]);
                const LLVector3 b = v3(face.mPositions[corners[1]]);
                const LLVector3 c = v3(face.mPositions[corners[2]]);
                const F32 scale = (b - a).lengthSquared() + (c - a).lengthSquared() + (c - b).lengthSquared();
                if (!used && ((b - a) % (c - a)).length() > 1e-5f * scale)
                {
                    fail(fmt::format("the grid's triangle ({}, {}, {}), which has area, is left out", corners[0], corners[1], corners[2]));
                    return true;
                }
            }
        }
        return true;
    }

    void checkFinite(const LLVolumeFace& face, const std::vector<LLVector3>& normals, Report& report, const std::string& where)
    {
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            if (!finite3(v3(face.mPositions[v])) || !std::isfinite(face.mTexCoords[v].mV[0]) || !std::isfinite(face.mTexCoords[v].mV[1]))
            {
                report.fail(fmt::format("{}: vertex {} has a position or UV that is not finite", where, v));
                return;
            }
            if (!finite3(normals[v]))
            {
                report.fail(fmt::format("{}: vertex {} has the normal ({}, {}, {})", where, v, normals[v].mV[0], normals[v].mV[1], normals[v].mV[2]));
                return;
            }
        }
    }

    void checkRawUnit(const LLVolumeFace& face, const std::vector<LLVector3>& normals, Report& report, const std::string& where)
    {
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const F32 length = normals[v].length();
            if (!(fabsf(length - 1.f) <= UNIT_TOLERANCE))
            {
                report.fail(fmt::format("{}: vertex {} has a normal of length {}", where, v, length));
                return;
            }
        }
    }

    // The widest angle between the real triangles at each vertex's point.
    std::vector<F32> spreadAt(const LLVolumeFace& face, const std::vector<Triangle>& triangles)
    {
        const std::vector<U32> groups = weldGroups(face);
        std::vector<std::vector<U32>> around(face.mNumVertices);
        for (size_t t = 0; t < triangles.size(); ++t)
        {
            if (!triangles[t].degenerate)
            {
                for (U32 v : triangles[t].v)
                {
                    around[groups[v]].push_back((U32)t);
                }
            }
        }
        std::vector<F32> spread(face.mNumVertices, 0.f);
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const std::vector<U32>& at = around[groups[v]];
            for (size_t i = 0; i < at.size(); ++i)
            {
                for (size_t j = i + 1; j < at.size(); ++j)
                {
                    spread[v] = llmax(spread[v], degreesBetween(triangles[at[i]].normal, triangles[at[j]].normal));
                }
            }
        }
        return spread;
    }

    // Where the triangles at a point span no more than a right angle, any
    // sum of them with positive weights faces every one of them, so a
    // normal facing away from one was made of something else; so does the
    // normal of the surface they are cut from, where they surround the
    // point. Beyond a right angle the surface folds at the point, and
    // whether its normal faces each triangle depends on the weights;
    // any_spread checks there too. A vertex marked in open_edges is at an
    // edge of a side whose normals are its surface's, which its cases of its
    // own hold it to (see openEdges).
    void checkInversion(const LLVolumeFace& face, const std::vector<Triangle>& triangles, const std::vector<LLVector3>& normals, bool any_spread,
                        const std::vector<bool>& open_edges, Report& report, const std::string& where)
    {
        const std::vector<F32> spread = spreadAt(face, triangles);
        for (size_t t = 0; t < triangles.size(); ++t)
        {
            const Triangle& tri = triangles[t];
            if (tri.degenerate)
            {
                continue;
            }
            for (U32 v : tri.v)
            {
                const LLVector3& n = normals[v];
                if (!open_edges.empty() && open_edges[v])
                {
                    continue;
                }
                if ((any_spread || spread[v] <= 90.f) && !(n * tri.normal > 0.f))
                {
                    report.fail(fmt::format("{}: triangle {} is {:.1f} degrees from its vertex {}'s normal, where the triangles span {:.1f}", where, t,
                                            degreesBetween(tri.normal, n), v, spread[v]));
                    return;
                }
            }
        }
    }

    // A cap faces away from the wall it closes: its normal is on the side of
    // its plane the path leaves the wall by, from the ring of the station
    // next to it toward its own.
    void checkCapFacing(const LLVolume& volume, const LLVolumeFace& face, const std::vector<LLVector3>& normals, Report& report, const std::string& where)
    {
        const LLAlignedArray<LLVector4a, 64>& mesh = volume.getMesh();
        const S32 ring = volume.getProfile().getTotal();
        const S32 stations = volume.getPath().getPathLength();
        if (stations < 2)
        {
            return;
        }
        const bool top = face.mTypeMask & LLVolumeFace::TOP_MASK;
        const S32 own = top ? stations - 1 : 0;
        const S32 next = top ? stations - 2 : 1;
        LLVector3 away;
        for (S32 i = 0; i < ring; ++i)
        {
            away += v3(mesh[own * ring + i]) - v3(mesh[next * ring + i]);
        }
        if (away.length() <= COINCIDE * ring)
        {
            return;
        }
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const LLVector3& n = normals[v];
            if (!(n * away > 0.f))
            {
                report.fail(fmt::format("{}: vertex {}'s normal is {:.1f} degrees from the way the path leaves its wall", where, v, degreesBetween(n, away)));
                return;
            }
        }
    }

    // A face whose positions lie in a plane and, for a side, whose every
    // grid quad is a parallelogram, maps its UVs as an affine function of
    // position: least squares over the plane must fit every vertex.
    void checkAffineUV(const LLVolumeFace& face, const std::vector<Triangle>& triangles, Report& report, const std::string& where)
    {
        LLVector3 normal;
        for (const Triangle& tri : triangles)
        {
            if (!tri.degenerate)
            {
                normal += tri.normal;
            }
        }
        if (normal.length() <= 1e-6f)
        {
            return;
        }
        normal.normalize();
        const LLVector3 origin = v3(face.mPositions[0]);
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            if (fabsf((v3(face.mPositions[v]) - origin) * normal) > 1e-4f)
            {
                return; // not planar
            }
        }
        if (!isCap(face))
        {
            if (!isGrid(face))
            {
                return;
            }
            for (S32 t = 0; t + 1 < face.mNumT; ++t)
            {
                for (S32 s = 0; s + 1 < face.mNumS; ++s)
                {
                    const LLVector3 skew = v3(face.mPositions[gridIndex(face, s, t)]) + v3(face.mPositions[gridIndex(face, s + 1, t + 1)]) -
                                           v3(face.mPositions[gridIndex(face, s + 1, t)]) - v3(face.mPositions[gridIndex(face, s, t + 1)]);
                    if (skew.length() > COINCIDE)
                    {
                        return; // a trapezoid, whose UVs are affine only per triangle
                    }
                }
            }
        }
        LLVector3 e1 = normal % LLVector3(1.f, 0.f, 0.f);
        if (e1.length() < 0.1f)
        {
            e1 = normal % LLVector3(0.f, 1.f, 0.f);
        }
        e1.normalize();
        const LLVector3 e2 = normal % e1;

        F64 m[3][3] = {}, bu[3] = {}, bv[3] = {};
        std::vector<std::array<F64, 2>> xy(face.mNumVertices);
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const LLVector3 d = v3(face.mPositions[v]) - origin;
            const F64 row[3] = { d * e1, d * e2, 1.0 };
            xy[v] = { row[0], row[1] };
            for (S32 i = 0; i < 3; ++i)
            {
                for (S32 j = 0; j < 3; ++j)
                {
                    m[i][j] += row[i] * row[j];
                }
                bu[i] += row[i] * face.mTexCoords[v].mV[0];
                bv[i] += row[i] * face.mTexCoords[v].mV[1];
            }
        }
        const F64 det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        if (fabs(det) < 1e-12)
        {
            return;
        }
        auto solve = [&](const F64* b, F64* x)
        {
            for (S32 c = 0; c < 3; ++c)
            {
                F64 mc[3][3];
                std::memcpy(mc, m, sizeof(mc));
                for (S32 r = 0; r < 3; ++r)
                {
                    mc[r][c] = b[r];
                }
                x[c] = (mc[0][0] * (mc[1][1] * mc[2][2] - mc[1][2] * mc[2][1]) - mc[0][1] * (mc[1][0] * mc[2][2] - mc[1][2] * mc[2][0]) +
                        mc[0][2] * (mc[1][0] * mc[2][1] - mc[1][1] * mc[2][0])) /
                       det;
            }
        };
        F64 au[3], av[3];
        solve(bu, au);
        solve(bv, av);
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const F64 u = au[0] * xy[v][0] + au[1] * xy[v][1] + au[2];
            const F64 w = av[0] * xy[v][0] + av[1] * xy[v][1] + av[2];
            const F64 off = std::max(fabs(u - face.mTexCoords[v].mV[0]), fabs(w - face.mTexCoords[v].mV[1]));
            if (off > 1e-4)
            {
                report.fail(fmt::format("{}: vertex {}'s UV is {:.2g} off the affine map of the face's plane", where, v, off));
                return;
            }
        }
    }

    // On a smooth face, away from poles and apexes, our normal and the
    // oracle's agree to within the widest angle between the triangles at
    // the point, where that is no more than a right angle: beyond it the
    // surface folds, and any normal faces away from some of its triangles.
    // A vertex marked in open_edges is at an edge of a side whose normals
    // are its surface's, where the oracle averages triangles to one side of
    // it (see openEdges).
    void checkOracle(const LLVolumeFace& face, const std::vector<Triangle>& triangles, const std::vector<LLVector3>& normals,
                     const std::vector<bool>& open_edges, Report& report, const std::string& where)
    {
        if (isCap(face) || isDoubled(face))
        {
            return;
        }
        const std::vector<U32> groups = weldGroups(face);
        const std::vector<LLVector3> oracle = oracleNormals(face, groups);
        std::map<U32, std::vector<U32>> incident;
        for (size_t t = 0; t < triangles.size(); ++t)
        {
            for (U32 v : triangles[t].v)
            {
                incident[groups[v]].push_back((U32)t);
            }
        }
        // The oracle gives a normal only to a vertex a triangle uses: of the
        // vertices at a point the parameters collapse, as an apex's or a
        // pole's, the triangles of no area are left out, and some vertices
        // with them.
        std::vector<bool> used(face.mNumVertices, false);
        for (const Triangle& tri : triangles)
        {
            for (U32 v : tri.v)
            {
                used[v] = true;
            }
        }
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const std::vector<U32>& around = incident[groups[v]];
            if (!used[v] || around.empty() || std::any_of(around.begin(), around.end(), [&](U32 t) { return triangles[t].degenerate; }) ||
                (!open_edges.empty() && open_edges[v]))
            {
                continue;
            }
            F32 facet = 0.f;
            for (size_t i = 0; i < around.size(); ++i)
            {
                for (size_t j = i + 1; j < around.size(); ++j)
                {
                    facet = llmax(facet, degreesBetween(triangles[around[i]].normal, triangles[around[j]].normal));
                }
            }
            if (facet > 90.f)
            {
                continue;
            }
            const F32 off = degreesBetween(normals[v], oracle[v]);
            if (!(off <= facet + 1.f))
            {
                report.fail(fmt::format("{}: vertex {} is {:.1f} degrees from meshoptimizer's normal where the triangles span {:.1f}", where, v, off, facet));
                return;
            }
        }
    }

    // Where a side's first and last columns are one point, it is one
    // smooth surface across them, and both take one normal; so with the
    // rows of a closed path.
    void checkSeamsAgree(const LLVolume& volume, const LLVolumeFace& face, const std::vector<LLVector3>& normals, Report& report,
                         const std::string& where)
    {
        if (!isGrid(face) || isDoubled(face))
        {
            return;
        }
        const U8 stitching = volume.getParams().getSculptType() & LL_SCULPT_TYPE_MASK;
        const bool sculpt = stitching != LL_SCULPT_TYPE_NONE;
        const bool wraps_s = !sculpt || stitching == LL_SCULPT_TYPE_SPHERE || stitching == LL_SCULPT_TYPE_TORUS || stitching == LL_SCULPT_TYPE_CYLINDER;
        const bool wraps_t = !sculpt || stitching == LL_SCULPT_TYPE_TORUS;
        auto agree = [&](S32 a, S32 b, F32& off)
        {
            off = degreesBetween(normals[a], normals[b]);
            return off <= 1.f;
        };
        if (wraps_s && columnsMeet(face))
        {
            for (S32 t = 0; t < face.mNumT; ++t)
            {
                F32 off = 0.f;
                if (!agree(gridIndex(face, 0, t), gridIndex(face, face.mNumS - 1, t), off))
                {
                    report.fail(fmt::format("{}: the seam's two columns are {:.1f} degrees apart at row {}", where, off, t));
                    return;
                }
            }
        }
        if (wraps_t && rowsMeet(face))
        {
            for (S32 s = 0; s < face.mNumS; ++s)
            {
                F32 off = 0.f;
                if (!agree(gridIndex(face, s, 0), gridIndex(face, s, face.mNumT - 1), off))
                {
                    report.fail(fmt::format("{}: the first and last rows are {:.1f} degrees apart at column {}", where, off, s));
                    return;
                }
            }
        }
    }

    // A flat inner side is hard at every corner. Its columns run along its
    // points, each corner twice, the copy one place with the corner in every
    // row and with its UVs: the quad between them has no width. Each run of
    // columns between copies is a strip, a facet along the path, and a
    // vertex takes its normal from the triangles of its own strip at its
    // point, and from no other strip there. A vertex in no triangle with
    // area, as an apex's, is in no strip.
    std::vector<S32> stripsOf(const LLVolumeFace& face)
    {
        std::vector<S32> strip(face.mNumS, 0);
        for (S32 s = 1; s < face.mNumS; ++s)
        {
            bool copy = true;
            for (S32 t = 0; t < face.mNumT && copy; ++t)
            {
                copy = bitsOf(face.mPositions[gridIndex(face, s, t)]) == bitsOf(face.mPositions[gridIndex(face, s - 1, t)]) &&
                       face.mTexCoords[gridIndex(face, s, t)] == face.mTexCoords[gridIndex(face, s - 1, t)];
            }
            strip[s] = strip[s - 1] + (copy ? 1 : 0);
        }
        return strip;
    }

    // The vertices of a side at an edge of the surface they are on: a first
    // or last column or row that does not meet the other end, and a flat
    // inner side's columns where each of its strips ends, at its corners and
    // its seam. The triangles at such a vertex lie to one side of it. Where a
    // side's normals are its surface's, the normal there is the edge's own,
    // which leans from them by up to half a step of the surface's turn: a
    // cut column's by up to half a step, as its last partial step lies on
    // the whole step's chord, and a twisted strip's corner by half a
    // station's twist. The checks that compare a normal
    // with the triangles at its point leave those vertices to cases that
    // hold them to the surface itself.
    std::vector<bool> openEdges(const LLVolumeFace& face)
    {
        std::vector<bool> edge(face.mNumVertices, false);
        if (!isGrid(face))
        {
            return edge;
        }
        const bool doubled = isDoubled(face);
        const bool wraps_s = !doubled && columnsMeet(face);
        const bool wraps_t = rowsMeet(face);
        const std::vector<S32> strips = doubled ? stripsOf(face) : std::vector<S32>();
        for (S32 t = 0; t < face.mNumT; ++t)
        {
            for (S32 s = 0; s < face.mNumS; ++s)
            {
                bool at_edge = (!wraps_s && (s == 0 || s == face.mNumS - 1)) || (!wraps_t && (t == 0 || t == face.mNumT - 1));
                if (doubled)
                {
                    at_edge = at_edge || (s > 0 && strips[s] != strips[s - 1]) || (s + 1 < face.mNumS && strips[s] != strips[s + 1]);
                }
                edge[gridIndex(face, s, t)] = at_edge;
            }
        }
        return edge;
    }

    void checkHardEdges(const LLVolumeFace& face, const std::vector<Triangle>& triangles, const std::vector<LLVector3>& normals,
                        const std::vector<bool>& open_edges, Report& report, const std::string& where)
    {
        if (!isDoubled(face) || !isGrid(face))
        {
            return;
        }
        const std::vector<bool> lone = loneVertices(face, triangles);
        const std::vector<U32> groups = weldGroups(face);
        const std::vector<S32> strips = stripsOf(face);
        std::map<std::pair<U32, S32>, std::vector<U32>> in_strip;
        for (size_t t = 0; t < triangles.size(); ++t)
        {
            if (triangles[t].degenerate)
            {
                continue;
            }
            S32 column = face.mNumS;
            for (U32 v : triangles[t].v)
            {
                column = llmin(column, (S32)(v % face.mNumS));
            }
            for (U32 v : triangles[t].v)
            {
                in_strip[{ groups[v], strips[column] }].push_back((U32)t);
            }
        }
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const S32 column = v % face.mNumS;
            if (lone[v] || (!open_edges.empty() && open_edges[v]))
            {
                continue;
            }
            const auto found = in_strip.find({ groups[v], strips[column] });
            if (found == in_strip.end())
            {
                continue;
            }
            LLVector3 expected;
            F32 spread = 0.f;
            for (U32 a : found->second)
            {
                expected += triangles[a].normal;
                for (U32 b : found->second)
                {
                    spread = llmax(spread, degreesBetween(triangles[a].normal, triangles[b].normal));
                }
            }
            const F32 off = degreesBetween(normals[v], expected);
            if (!(off <= spread + 1.f))
            {
                report.fail(fmt::format("{}: vertex {} (column {}) is {:.1f} degrees from its own strip, whose triangles span {:.1f}", where, v, column, off,
                                        spread));
                return;
            }
        }
    }

    // A side's vertex in no triangle with area has no normal of its own, and
    // takes the normal of a vertex that has one: never none, never what
    // rounding leaves, and never a made-up one while the face has any. A
    // sphere's pole, a column that is one place in every row, takes the
    // axis instead.
    void checkLoneNormals(const LLVolume& volume, const LLVolumeFace& face, const std::vector<Triangle>& triangles, const std::vector<LLVector3>& normals,
                          Report& report, const std::string& where)
    {
        if (!isGrid(face))
        {
            return;
        }
        const LLVolumeParams& params = volume.getParams();
        const bool sphere = !isSculpt(volume) && (params.getProfileParams().getCurveType() & LL_PCODE_PROFILE_MASK) == LL_PCODE_PROFILE_CIRCLE_HALF &&
                            params.getPathParams().getCurveType() == LL_PCODE_PATH_CIRCLE;
        auto pole = [&](S32 column)
        {
            for (S32 t = 1; t < face.mNumT; ++t)
            {
                if (!coincide(face.mPositions[gridIndex(face, column, 0)], face.mPositions[gridIndex(face, column, t)]))
                {
                    return false;
                }
            }
            return true;
        };
        const std::vector<bool> lone = loneVertices(face, triangles);
        std::vector<S32> donors;
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            if (!lone[v])
            {
                donors.push_back(v);
            }
        }
        if (donors.empty())
        {
            return;
        }
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            if (lone[v] && !(sphere && pole(v % face.mNumS)) &&
                std::none_of(donors.begin(), donors.end(), [&](S32 d) { return degreesBetween(normals[v], normals[d]) <= 0.1f; }))
            {
                report.fail(fmt::format("{}: vertex {} (column {}, row {}) has none of the face's normals", where, v, v % face.mNumS, v / face.mNumS));
                return;
            }
        }
    }

    void checkTangents(const LLVolumeFace& face, U32 checks, Report& report, const std::string& where)
    {
        if (!face.mTangents)
        {
            report.fail(fmt::format("{}: no tangents after genTangents", where));
            return;
        }
        bool tangents_failed = false;
        bool orthogonal_failed = false;
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const LLVector3 n = v3(face.mNormals[v]);
            const LLVector3 t = v3(face.mTangents[v]);
            const F32 w = face.mTangents[v][3];
            if ((checks & Checks::Tangents) && !tangents_failed)
            {
                if (!finite3(n) || !(fabsf(n.length() - 1.f) <= UNIT_TOLERANCE))
                {
                    report.fail(fmt::format("{}: after genTangents vertex {} has the normal ({}, {}, {})", where, v, n.mV[0], n.mV[1], n.mV[2]));
                    tangents_failed = true;
                }
                else if (!finite3(t) || !(fabsf(t.length() - 1.f) <= UNIT_TOLERANCE) || !(w == 1.f || w == -1.f))
                {
                    report.fail(fmt::format("{}: vertex {} has the tangent ({}, {}, {}, {})", where, v, t.mV[0], t.mV[1], t.mV[2], w));
                    tangents_failed = true;
                }
            }
            if ((checks & Checks::Orthogonal) && !orthogonal_failed)
            {
                const F32 cosine = (n * t) / (n.length() * t.length());
                if (!(fabsf(cosine) <= 2e-3f))
                {
                    report.fail(fmt::format("{}: vertex {}'s tangent is {:.2f} degrees from its normal", where, v, degreesBetween(n, t)));
                    orthogonal_failed = true;
                }
            }
        }
    }

    // What comparing tangents with meshoptimizer's found over a run.
    struct TangentAgreement
    {
        S32 compared = 0; // vertices it gives one tangent with a length
        S32 split = 0;    // vertices whose corners it gives more than one, or sums with another vertex's
        S32 lone = 0;     // vertices it gives no tangent
        S32 unsure = 0;   // vertices whose tangent rounding decides
        F64 widest = 0.0; // degrees between the two, at most
        std::string widest_at;
        F64 nearest_bound = 0.0; // the most of a vertex's bound the two came apart by
        std::string nearest_bound_at;
    };

    F64 degreesApart(const LLVector3& a, const LLVector3& b)
    {
        const F64 cx = (F64)a.mV[1] * b.mV[2] - (F64)a.mV[2] * b.mV[1];
        const F64 cy = (F64)a.mV[2] * b.mV[0] - (F64)a.mV[0] * b.mV[2];
        const F64 cz = (F64)a.mV[0] * b.mV[1] - (F64)a.mV[1] * b.mV[0];
        const F64 dot = (F64)a.mV[0] * b.mV[0] + (F64)a.mV[1] * b.mV[1] + (F64)a.mV[2] * b.mV[2];
        return atan2(sqrt(cx * cx + cy * cy + cz * cz), dot) * 180.0 / F_PI;
    }

    // Whether a triangle adds nothing to tangents: twice its area, in
    // positions or in UVs, under 1e-5 of the sum of the squares of the two
    // edges from its first corner.
    bool tangentFlat(const LLVolumeFace& face, U32 a, U32 b, U32 c)
    {
        const LLVector3 pa = v3(face.mPositions[a]);
        const LLVector3 ab = v3(face.mPositions[b]) - pa;
        const LLVector3 ac = v3(face.mPositions[c]) - pa;
        const LLVector2 uv_ab = face.mTexCoords[b] - face.mTexCoords[a];
        const LLVector2 uv_ac = face.mTexCoords[c] - face.mTexCoords[a];
        const F64 edges = (F64)ab.lengthSquared() + ac.lengthSquared();
        const F64 uv_edges = (F64)uv_ab.lengthSquared() + uv_ac.lengthSquared();
        const F64 area = (F64)uv_ab.mV[0] * uv_ac.mV[1] - (F64)uv_ab.mV[1] * uv_ac.mV[0];
        return (F64)(ab % ac).lengthSquared() <= 1e-10 * edges * edges || area * area <= 1e-10 * uv_edges * uv_edges;
    }

    // How far rounding can turn each vertex's tangent, in degrees: its sum
    // of corner shares worked again in double, as meshoptimizer's default
    // mode makes them, and each share's size times how far rounding can
    // move the share, against the size of the sum. A share's direction
    // moves as far as its triangle's tangent can, from the rounding of the
    // positions and UVs it is made from and of the chain rule's two terms,
    // over the share's length in the normal's plane, short where the
    // tangent nearly lies along the normal. Its weight moves as far as its
    // edges' lengths in that plane can: short where an edge nearly lies
    // along the normal, and shorter still to round where the square is
    // taken as the edge's own less its square along the normal, as
    // createTangents takes it. And as far as their angle can: near 0 or
    // pi, where the arc cosine is a square root, by the square root of the
    // cosine's rounding, however small the weight; and at a right angle by
    // the step its arc cosine takes there, about 1e-3, where rounding
    // decides the cosine's sign. 180 where the sum is
    // nothing, or where a triangle's UVs wind one way or the other only by
    // rounding, or it is nearly flat enough for one of the two to leave out
    // and not the other.
    std::vector<F64> tangentRoundingDegrees(const LLVolumeFace& face)
    {
        constexpr F64 EPSILON = 1.0 / (1 << 24);
        using V = std::array<F64, 3>;
        auto at = [](const LLVector4a& v) { return V{ v[0], v[1], v[2] }; };
        auto sub = [](const V& a, const V& b) { return V{ a[0] - b[0], a[1] - b[1], a[2] - b[2] }; };
        auto dot = [](const V& a, const V& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        auto size = [&](const V& a) { return sqrt(dot(a, a)); };
        auto less = [&](const V& v, const V& n)
        {
            const F64 d = dot(v, n);
            return V{ v[0] - n[0] * d, v[1] - n[1] * d, v[2] - n[2] * d };
        };
        std::vector<V> sums(face.mNumVertices, { 0.0, 0.0, 0.0 });
        std::vector<F64> spread(face.mNumVertices, 0.0);
        std::vector<bool> unsure(face.mNumVertices, false);
        for (S32 i = 0; i + 2 < face.mNumIndices; i += 3)
        {
            const U32 corner[3] = { face.mIndices[i], face.mIndices[i + 1], face.mIndices[i + 2] };
            const V pa = at(face.mPositions[corner[0]]);
            const V pb = at(face.mPositions[corner[1]]);
            const V pc = at(face.mPositions[corner[2]]);
            const LLVector2& ta = face.mTexCoords[corner[0]];
            const LLVector2& tb = face.mTexCoords[corner[1]];
            const LLVector2& tc = face.mTexCoords[corner[2]];
            const V dp1 = sub(pb, pa);
            const V dp2 = sub(pc, pa);
            const F64 dt1x = (F64)tb.mV[0] - ta.mV[0];
            const F64 dt1y = (F64)tb.mV[1] - ta.mV[1];
            const F64 dt2x = (F64)tc.mV[0] - ta.mV[0];
            const F64 dt2y = (F64)tc.mV[1] - ta.mV[1];

            // How flat it is, in positions and in UVs, against the flat
            // triangle's bound.
            const V normal = { dp1[1] * dp2[2] - dp1[2] * dp2[1], dp1[2] * dp2[0] - dp1[0] * dp2[2], dp1[0] * dp2[1] - dp1[1] * dp2[0] };
            const F64 det = dt1x * dt2y - dt1y * dt2x;
            const F64 flat = llmin(size(normal) / (dot(dp1, dp1) + dot(dp2, dp2)), fabs(det) / (dt1x * dt1x + dt1y * dt1y + dt2x * dt2x + dt2y * dt2y));
            // How far the UVs' rounding can move the UV area.
            const F64 det_noise = EPSILON * ((fabs(ta.mV[0]) + fabs(tb.mV[0])) * fabs(dt2y) + fabs(dt1x) * (fabs(ta.mV[1]) + fabs(tc.mV[1])) +
                                             (fabs(ta.mV[1]) + fabs(tb.mV[1])) * fabs(dt2x) + fabs(dt1y) * (fabs(ta.mV[0]) + fabs(tc.mV[0])) +
                                             2.0 * fabs(dt1x * dt2y) + 2.0 * fabs(dt1y * dt2x));
            const bool uncertain = (flat > 0.25e-5 && flat < 4e-5) || (fabs(det) <= 4.0 * det_noise && !(flat <= 0.25e-5));
            if (uncertain)
            {
                for (U32 v : corner)
                {
                    unsure[v] = true;
                }
            }
            if (flat <= 1e-5)
            {
                continue;
            }

            const V r = { dt2y * dp1[0] - dt1y * dp2[0], dt2y * dp1[1] - dt1y * dp2[1], dt2y * dp1[2] - dt1y * dp2[2] };
            const F64 length = size(r);
            if (length == 0.0)
            {
                continue;
            }
            const F64 scale = (det > 0.0 ? 1.0 : -1.0) / length;
            const V t = { r[0] * scale, r[1] * scale, r[2] * scale };
            // How far r can turn, against its length, in units of EPSILON:
            // the rounding of its terms and of the differences they are
            // made from.
            const F64 noise_dt1y = fabs(ta.mV[1]) + fabs(tb.mV[1]) + fabs(dt1y);
            const F64 noise_dt2y = fabs(ta.mV[1]) + fabs(tc.mV[1]) + fabs(dt2y);
            const F64 noise_dp1 = size(pa) + size(pb) + size(dp1);
            const F64 noise_dp2 = size(pa) + size(pc) + size(dp2);
            const F64 turn = (noise_dt2y * size(dp1) + fabs(dt2y) * noise_dp1 + noise_dt1y * size(dp2) + fabs(dt1y) * noise_dp2 +
                              2.0 * (fabs(dt2y) * size(dp1) + fabs(dt1y) * size(dp2))) / length;
            for (int k = 0; k < 3; ++k)
            {
                const U32 v = corner[k];
                const V n = at(face.mNormals[v]);
                const V e1 = sub(at(face.mPositions[corner[(k + 1) % 3]]), at(face.mPositions[v]));
                const V e2 = sub(at(face.mPositions[corner[(k + 2) % 3]]), at(face.mPositions[v]));
                const V st = less(t, n);
                const V p1 = less(e1, n);
                const V p2 = less(e2, n);
                const F64 sl = size(st);
                const F64 l1 = size(p1);
                const F64 l2 = size(p2);
                if (sl == 0.0 || l1 == 0.0 || l2 == 0.0)
                {
                    continue;
                }
                const F64 cosine = llclamp(dot(p1, p2) / (l1 * l2), -1.0, 1.0);
                const F64 ax = fabs(cosine);
                F64 angle = (1.570337 + ax * (-0.2053972 + ax * 0.05147786)) * sqrt(1.0 - ax);
                angle = cosine < 0.0 ? 3.1415926 - angle : angle;
                const F64 weight = angle * l1 * l2;
                for (int j = 0; j < 3; ++j)
                {
                    sums[v][j] += st[j] * weight / sl;
                }
                const F64 lengths = 1.0 + (size(e1) + size(at(face.mPositions[v]))) / l1 + (size(e2) + size(at(face.mPositions[v]))) / l2 +
                                    dot(e1, e1) / (l1 * l1) + dot(e2, e2) / (l2 * l2);
                const F64 cosine_noise = 2.0 * EPSILON * lengths;
                const F64 angle_noise = cosine_noise + 1.570337 * (sqrt(1.0 - ax + cosine_noise) - sqrt(1.0 - ax)) +
                                        (ax <= cosine_noise ? 3.1415926 - 2.0 * 1.570337 : 0.0);
                spread[v] += weight * EPSILON * ((turn + 4.0) / sl + lengths) + l1 * l2 * angle_noise;
            }
        }
        std::vector<F64> degrees(face.mNumVertices, 180.0);
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const F64 length = size(sums[v]);
            if (length > 0.0 && !unsure[v])
            {
                degrees[v] = llmin(180.0, 4.0 * spread[v] / length * 180.0 / F_PI);
            }
        }
        return degrees;
    }

    // A face's tangents against meshoptimizer's default mode over its
    // triangles that are not flat, which makes one tangent for each corner,
    // and none where nothing adds to it: a vertex whose corners it gives
    // one tangent must have that tangent's w, and its direction to within
    // rounding, the larger of a thousandth of a degree and how far
    // rounding can turn the vertex's sum.
    constexpr F64 MESHOPT_TANGENT_DEGREES = 0.001;

    void checkMeshoptTangents(const LLVolumeFace& face, TangentAgreement& agreement, Report& report, const std::string& where)
    {
        if (!face.mTangents || !face.mNumIndices)
        {
            return;
        }
        std::vector<U32> indices;
        for (S32 i = 0; i + 2 < face.mNumIndices; i += 3)
        {
            if (!tangentFlat(face, face.mIndices[i], face.mIndices[i + 1], face.mIndices[i + 2]))
            {
                indices.insert(indices.end(), face.mIndices + i, face.mIndices + i + 3);
            }
        }
        if (indices.empty())
        {
            return;
        }
        std::vector<F32> corners(indices.size() * 4);
        meshopt_generateTangents(corners.data(), indices.data(), indices.size(), face.mPositions[0].getF32ptr(), face.mNumVertices,
                                 sizeof(LLVector4a), face.mNormals[0].getF32ptr(), sizeof(LLVector4a), face.mTexCoords[0].mV,
                                 sizeof(LLVector2), meshopt_TangentZeroFallback);
        const std::vector<F64> rounding = tangentRoundingDegrees(face);
        std::vector<S32> first(face.mNumVertices, -1);
        std::vector<bool> split(face.mNumVertices, false);
        // meshoptimizer welds vertices whose positions, normals and UVs
        // compare equal and sums all their corners, where createTangents
        // sums each vertex's own. A flat inner side's copied columns share a
        // position and a UV, and where their normals come out equal to the
        // last bit, as rounding decides on one platform and not another, it
        // gives each the sum of both: such a vertex is not held to it.
        std::map<std::array<U32, 8>, U32> places;
        for (U32 v : indices)
        {
            std::array<U32, 8> key;
            std::memcpy(key.data(), face.mPositions[v].getF32ptr(), 3 * sizeof(F32));
            std::memcpy(key.data() + 3, face.mNormals[v].getF32ptr(), 3 * sizeof(F32));
            std::memcpy(key.data() + 6, face.mTexCoords[v].mV, 2 * sizeof(F32));
            for (U32& bits : key)
            {
                bits = bits == 0x80000000u ? 0u : bits; // -0 is 0, as meshoptimizer compares them
            }
            const auto [place, fresh] = places.emplace(key, v);
            if (!fresh && place->second != v)
            {
                split[v] = true;
                split[place->second] = true;
            }
        }
        for (size_t c = 0; c < indices.size(); ++c)
        {
            const U32 v = indices[c];
            if (first[v] < 0)
            {
                first[v] = (S32)c;
            }
            else if (std::memcmp(&corners[c * 4], &corners[first[v] * 4], 4 * sizeof(F32)) != 0)
            {
                split[v] = true;
            }
        }
        bool failed = false;
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            if (first[v] < 0)
            {
                continue;
            }
            if (split[v])
            {
                ++agreement.split;
                continue;
            }
            const F32* theirs = &corners[first[v] * 4];
            const LLVector3 direction(theirs[0], theirs[1], theirs[2]);
            if (direction.isExactlyZero())
            {
                ++agreement.lone;
                continue;
            }
            if (rounding[v] >= 180.0)
            {
                ++agreement.unsure;
                continue;
            }
            ++agreement.compared;
            const F64 apart = degreesApart(v3(face.mTangents[v]), direction);
            const F64 bound = llmax(MESHOPT_TANGENT_DEGREES, rounding[v]);
            if (apart > agreement.widest)
            {
                agreement.widest = apart;
                agreement.widest_at = fmt::format("{}: vertex {}, bound {} degrees", where, v, bound);
            }
            if (apart / bound > agreement.nearest_bound)
            {
                agreement.nearest_bound = apart / bound;
                agreement.nearest_bound_at = fmt::format("{}: vertex {}, {} of {} degrees", where, v, apart, bound);
            }
            if (!failed && (!(apart <= bound) || (bound < 180.0 && face.mTangents[v][3] != theirs[3])))
            {
                report.fail(fmt::format("{}: vertex {} has the tangent ({}, {}, {}, {}), {} degrees from meshoptimizer's ({}, {}, {}, {}), past {}", where, v,
                                        face.mTangents[v][0], face.mTangents[v][1], face.mTangents[v][2], face.mTangents[v][3], apart,
                                        theirs[0], theirs[1], theirs[2], theirs[3], bound));
                failed = true;
            }
        }
    }

    // Every check the inspection asks for, on every face of the volume.
    // Tangents come last, from genTangents, once every face's normals have
    // been checked as generation left them.
    void inspect(LLVolume& volume, const std::string& name, const Inspect& how, Report& report)
    {
        const S32 faces = volume.getNumVolumeFaces();
        struct State
        {
            bool inspected = false;
            U32 checks = 0;
            U32 regions = 0;
            std::vector<LLVector3> raw;
        };
        std::vector<State> states(faces);
        for (S32 i = 0; i < faces; ++i)
        {
            LLVolumeFace& face = volume.getVolumeFace(i);
            const std::string where = fmt::format("{}: {}", name, faceName(face));
            if (!checkLayout(face, i, report, where, how.checks & Checks::Layout))
            {
                continue;
            }
            const std::vector<Triangle> triangles = trianglesOf(face);
            State& state = states[i];
            state.regions = regionsOf(face, triangles);
            if (how.only_regions && !(state.regions & how.only_regions))
            {
                continue;
            }
            state.inspected = true;
            state.checks = how.checks;
            state.raw.resize(face.mNumVertices);
            for (S32 v = 0; v < face.mNumVertices; ++v)
            {
                state.raw[v] = v3(face.mNormals[v]);
            }
            const std::vector<bool> open_edges = sweptSides(volume) ? openEdges(face) : std::vector<bool>();

            if (state.checks & Checks::Finite)
            {
                checkFinite(face, state.raw, report, where);
            }
            if (state.checks & Checks::RawUnit)
            {
                checkRawUnit(face, state.raw, report, where);
            }
            if (state.checks & Checks::Inversion)
            {
                checkInversion(face, triangles, state.raw, how.inversion_any_spread, open_edges, report, where);
            }
            if ((state.checks & Checks::CapFacing) && isCap(face))
            {
                checkCapFacing(volume, face, state.raw, report, where);
            }
            // A sculpt's UVs are its grid, wherever the map puts the points.
            if ((state.checks & Checks::AffineUV) && !isSculpt(volume))
            {
                checkAffineUV(face, triangles, report, where);
            }
            if (state.checks & Checks::Oracle)
            {
                checkOracle(face, triangles, state.raw, open_edges, report, where);
            }
            if (state.checks & Checks::SeamsAgree)
            {
                checkSeamsAgree(volume, face, state.raw, report, where);
            }
            if (state.checks & Checks::HardEdges)
            {
                checkHardEdges(face, triangles, state.raw, open_edges, report, where);
            }
            if (state.checks & Checks::LoneNormals)
            {
                checkLoneNormals(volume, face, triangles, state.raw, report, where);
            }
        }

        for (S32 i = 0; i < faces; ++i)
        {
            const State& state = states[i];
            if (!state.inspected || !(state.checks & (Checks::Tangents | Checks::Orthogonal | Checks::Meshopt)))
            {
                continue;
            }
            volume.genTangents(i);
            const LLVolumeFace& face = volume.getVolumeFace(i);
            checkTangents(face, state.checks, report, fmt::format("{}: {}", name, faceName(face)));
            if (state.checks & Checks::Meshopt)
            {
                checkMeshoptTangents(face, *how.agreement, report, fmt::format("{}: {}", name, faceName(face)));
            }
        }
    }

    // The prim's face IDs against the rule, then every face's checks.
    void inspectPrim(const Shape& s, S32 lod, const Inspect& how, Report& report)
    {
        LLPointer<LLVolume> volume = build(s, lod);
        const std::string name = lodName(s, lod);
        if (how.checks & Checks::Layout)
        {
            const std::vector<LLFaceID> expected = expectedFaceIDs(volume->getParams());
            if (faceIDsOf(*volume) != expected || volume->getNumFaces() != (S32)expected.size() ||
                volume->getNumVolumeFaces() != (S32)expected.size())
            {
                report.fail(fmt::format("{}: {} faces ({} volume faces) where the rule gives {}", name, volume->getNumFaces(),
                                        volume->getNumVolumeFaces(), expected.size()));
                return;
            }
        }
        inspect(*volume, name, how, report);
    }

    void inspectSweep(const Inspect& how, Report& report, S32& volumes)
    {
        for (const Shape& s : sweepShapes())
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                inspectPrim(s, lod, how, report);
                ++volumes;
            }
        }
    }

    // ------------------------------------------------------------------
    // Sculpts

    struct SculptMap
    {
        U16 width = 0;
        U16 height = 0;
        std::vector<U8> rgb;
    };

    U8 toByte(F32 value)
    {
        return (U8)llclamp(ll_round((value + 0.5f) * 255.f), 0, 255);
    }

    // A map whose pixel (x, y) holds surface(x, y): positions in
    // [-0.5, 0.5], eight bits an axis, as LLVolume::sculpt reads them.
    template <typename F>
    SculptMap sculptMap(U16 width, U16 height, F&& surface)
    {
        SculptMap map;
        map.width = width;
        map.height = height;
        map.rgb.resize((size_t)width * height * 3);
        for (U16 y = 0; y < height; ++y)
        {
            for (U16 x = 0; x < width; ++x)
            {
                const LLVector3 p = surface(x, y);
                U8* out = &map.rgb[((size_t)y * width + x) * 3];
                out[0] = toByte(p.mV[0]);
                out[1] = toByte(p.mV[1]);
                out[2] = toByte(p.mV[2]);
            }
        }
        return map;
    }

    // The sampler reads x from 0 up to width, the last wrapping or clamping
    // by the stitching; a sphere's first and last rows are its poles.
    SculptMap sphereMap(U16 width, U16 height)
    {
        return sculptMap(width, height, [&](U16 x, U16 y)
        {
            const F32 u = (F32)x / width;
            const F32 v = (F32)y / (height - 1);
            return LLVector3(0.45f * sinf(F_PI * v) * cosf(F_TWO_PI * u), -0.45f * sinf(F_PI * v) * sinf(F_TWO_PI * u), 0.45f * cosf(F_PI * v));
        });
    }

    SculptMap torusMap(U16 width, U16 height)
    {
        return sculptMap(width, height, [&](U16 x, U16 y)
        {
            const F32 u = (F32)x / width;
            const F32 v = (F32)y / height;
            const F32 r = 0.3f + 0.15f * cosf(F_TWO_PI * v);
            return LLVector3(r * cosf(F_TWO_PI * u), -r * sinf(F_TWO_PI * u), 0.15f * sinf(F_TWO_PI * v));
        });
    }

    SculptMap planeMap(U16 width, U16 height)
    {
        return sculptMap(width, height, [&](U16 x, U16 y)
        {
            const F32 u = (F32)x / (width - 1);
            const F32 v = (F32)y / (height - 1);
            return LLVector3(u - 0.5f, v - 0.5f, 0.2f * sinf(F_TWO_PI * u) * cosf(F_PI * v));
        });
    }

    SculptMap cylinderMap(U16 width, U16 height)
    {
        return sculptMap(width, height, [&](U16 x, U16 y)
        {
            const F32 u = (F32)x / width;
            const F32 v = (F32)y / (height - 1);
            return LLVector3(0.4f * cosf(F_TWO_PI * u), -0.4f * sinf(F_TWO_PI * u), v - 0.5f);
        });
    }

    SculptMap mapFor(U8 stitching, U16 width, U16 height)
    {
        switch (stitching)
        {
            case LL_SCULPT_TYPE_SPHERE: return sphereMap(width, height);
            case LL_SCULPT_TYPE_TORUS: return torusMap(width, height);
            case LL_SCULPT_TYPE_CYLINDER: return cylinderMap(width, height);
            default: return planeMap(width, height);
        }
    }

    const char* stitchingName(U8 type)
    {
        switch (type & LL_SCULPT_TYPE_MASK)
        {
            case LL_SCULPT_TYPE_SPHERE: return "sphere";
            case LL_SCULPT_TYPE_TORUS: return "torus";
            case LL_SCULPT_TYPE_PLANE: return "plane";
            case LL_SCULPT_TYPE_CYLINDER: return "cylinder";
            default: return "unknown";
        }
    }

    constexpr U8 STITCHINGS[] = { LL_SCULPT_TYPE_SPHERE, LL_SCULPT_TYPE_TORUS, LL_SCULPT_TYPE_PLANE, LL_SCULPT_TYPE_CYLINDER };

    // A sculpted prim's volume as LLPanelObject sets one up, sculpted from
    // the map at the LOD's detail.
    LLPointer<LLVolume> buildSculpt(U8 type, const SculptMap& map, S32 lod, bool visible_placeholder = true)
    {
        LLVolumeParams p;
        p.setType(LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_CIRCLE);
        p.setRatio(1.f, 0.5f);
        p.setSculptID(LLUUID("c8b26d0b-6f5a-4f10-8d1e-5c3b3e4a9a01"), type);
        LLPointer<LLVolume> volume = new LLVolume(p, LOD_DETAIL[lod]);
        volume->sculpt(map.width, map.height, 3, map.rgb.empty() ? nullptr : map.rgb.data(), 0, visible_placeholder);
        return volume;
    }

    void inspectSculpt(U8 type, const SculptMap& map, S32 lod, const std::string& what, const Inspect& how, Report& report)
    {
        LLPointer<LLVolume> volume = buildSculpt(type, map, lod);
        const std::string name = fmt::format("{} sculpt ({}{}{}) at LOD {}", stitchingName(type), what,
                                             (type & LL_SCULPT_FLAG_MIRROR) ? ", mirrored" : "", (type & LL_SCULPT_FLAG_INVERT) ? ", inverted" : "", lod);
        if ((how.checks & Checks::Layout) && volume->getNumVolumeFaces() != 1)
        {
            report.fail(fmt::format("{}: {} faces", name, volume->getNumVolumeFaces()));
            return;
        }
        inspect(*volume, name, how, report);
    }

    // Each stitching, plain, mirrored, inverted and both, from a square map
    // and from maps wider and taller than they are high.
    void inspectSculpts(const Inspect& how, Report& report)
    {
        const U8 flags[] = { 0, LL_SCULPT_FLAG_MIRROR, LL_SCULPT_FLAG_INVERT, LL_SCULPT_FLAG_MIRROR | LL_SCULPT_FLAG_INVERT };
        const std::array<U16, 2> sizes[] = { { 64, 64 }, { 128, 32 }, { 32, 128 }, { 64, 16 } };
        for (U8 stitching : STITCHINGS)
        {
            for (const auto& size : sizes)
            {
                const SculptMap map = mapFor(stitching, size[0], size[1]);
                for (U8 flag : flags)
                {
                    for (S32 lod = 0; lod < LOD_COUNT; ++lod)
                    {
                        inspectSculpt((U8)(stitching | flag), map, lod, fmt::format("{} by {}", size[0], size[1]), how, report);
                    }
                }
            }
        }
    }

    // The rows of a 64 by 64 map that LOD 3's mesh rows sample, 33 of them,
    // two pixels apart: mesh row s reads pixel row 2s, the last clamped.
    constexpr U16 PINCH_MAP = 64;

    // A plane whose middle rows are all the same colour: the mesh rows
    // sampled from them land in one place, which is how a sculpt pinches
    // its surface into a thread.
    SculptMap pinchedMap()
    {
        return sculptMap(PINCH_MAP, PINCH_MAP, [](U16 x, U16 y)
        {
            const U16 row = (y >= 16 && y <= 48) ? 16 : y;
            return LLVector3((F32)x / (PINCH_MAP - 1) - 0.5f, (F32)row / (PINCH_MAP - 1) - 0.5f, 0.f);
        });
    }

    // A plane folded up along mesh row 10 with its row copied: mesh rows 10
    // and 11 (pixel rows 20 and 22) are one point, the quads between them
    // have no area, and the fold is a hard edge.
    SculptMap foldedMap()
    {
        return sculptMap(PINCH_MAP, PINCH_MAP, [](U16 x, U16 y)
        {
            const F32 u = (F32)x / (PINCH_MAP - 1) - 0.5f;
            const F32 fold = 20.f / (PINCH_MAP - 1) - 0.5f;
            const F32 v = (y <= 22 ? llmin((F32)y, 20.f) : (F32)y - 2.f) / (PINCH_MAP - 1) - 0.5f;
            return v <= fold ? LLVector3(u, v, -0.25f) : LLVector3(u, fold, -0.25f + (v - fold));
        });
    }

    // ------------------------------------------------------------------
    // The mesh corpus: faces as a decoded LOD hands them to cacheOptimize.

    struct CorpusMesh
    {
        std::string name;
        std::vector<LLVector3> positions;
        std::vector<LLVector3> normals;
        std::vector<LLVector2> uvs;
        std::vector<LLVector4> weights;
        std::vector<U16> indices;

        LLVolumeFace face() const
        {
            LLVolumeFace f;
            f.resizeVertices((S32)positions.size());
            f.resizeIndices((S32)indices.size());
            for (size_t v = 0; v < positions.size(); ++v)
            {
                f.mPositions[v].load3(positions[v].mV);
                f.mNormals[v].load3(normals[v].mV);
                f.mTexCoords[v] = uvs[v];
            }
            std::copy(indices.begin(), indices.end(), f.mIndices);
            if (!weights.empty())
            {
                f.allocateWeights((S32)weights.size());
                for (size_t v = 0; v < weights.size(); ++v)
                {
                    f.mWeights[v].loadua(weights[v].mV);
                }
            }
            LLVector4a& min = f.mExtents[0];
            LLVector4a& max = f.mExtents[1];
            min = max = f.mPositions[0];
            for (S32 v = 1; v < f.mNumVertices; ++v)
            {
                min.setMin(min, f.mPositions[v]);
                max.setMax(max, f.mPositions[v]);
            }
            f.mCenter->setAdd(min, max);
            f.mCenter->mul(0.5f);
            return f;
        }
    };

    // A latitude-longitude sphere as an exporter writes one: the seam column
    // twice, at u = 0 and u = 1, and each pole a row of points with one
    // triangle a column.
    CorpusMesh uvSphere(S32 columns, S32 rows)
    {
        CorpusMesh mesh;
        mesh.name = fmt::format("UV sphere {} by {}", columns, rows);
        for (S32 r = 0; r <= rows; ++r)
        {
            for (S32 c = 0; c <= columns; ++c)
            {
                const F32 theta = F_PI * r / rows;
                const F32 phi = F_TWO_PI * c / columns;
                const LLVector3 n(sinf(theta) * cosf(phi), sinf(theta) * sinf(phi), cosf(theta));
                mesh.positions.push_back(n * 0.5f);
                mesh.normals.push_back(n);
                mesh.uvs.emplace_back((F32)c / columns, 1.f - (F32)r / rows);
            }
        }
        auto at = [&](S32 c, S32 r) { return (U16)(c + r * (columns + 1)); };
        for (S32 r = 0; r < rows; ++r)
        {
            for (S32 c = 0; c < columns; ++c)
            {
                if (r > 0)
                {
                    mesh.indices.insert(mesh.indices.end(), { at(c, r), at(c, r + 1), at(c + 1, r) });
                }
                if (r < rows - 1)
                {
                    mesh.indices.insert(mesh.indices.end(), { at(c + 1, r), at(c, r + 1), at(c + 1, r + 1) });
                }
            }
        }
        return mesh;
    }

    // Six faces of four vertices, each with its own normal and UVs.
    CorpusMesh splitCube()
    {
        CorpusMesh mesh;
        mesh.name = "cube with split normals";
        for (S32 axis = 0; axis < 3; ++axis)
        {
            for (F32 sign : { 1.f, -1.f })
            {
                LLVector3 n, a;
                n.mV[axis] = sign;
                a.mV[(axis + 1) % 3] = 1.f;
                const LLVector3 b = n % a;
                const U16 base = (U16)mesh.positions.size();
                const F32 corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
                for (const auto& corner : corners)
                {
                    mesh.positions.push_back((n + a * corner[0] + b * corner[1]) * 0.5f);
                    mesh.normals.push_back(n);
                    mesh.uvs.emplace_back(corner[0] * 0.5f + 0.5f, corner[1] * 0.5f + 0.5f);
                }
                mesh.indices.insert(mesh.indices.end(), { base, (U16)(base + 1), (U16)(base + 2), base, (U16)(base + 2), (U16)(base + 3) });
            }
        }
        return mesh;
    }

    // A curved strip mapped with its halves mirrored about x = 0, the centre
    // column shared, as a symmetric model's halves share one UV island.
    CorpusMesh mirroredStrip()
    {
        CorpusMesh mesh;
        mesh.name = "strip with mirrored UVs";
        constexpr S32 columns = 12, rows = 6;
        for (S32 r = 0; r <= rows; ++r)
        {
            for (S32 c = 0; c <= columns; ++c)
            {
                const F32 x = 2.f * c / columns - 1.f;
                const F32 y = (F32)r / rows;
                mesh.positions.emplace_back(x * 0.5f, y - 0.5f, 0.2f * (1.f - x * x));
                LLVector3 n(0.8f * x, 0.f, 1.f);
                n.normalize();
                mesh.normals.push_back(n);
                mesh.uvs.emplace_back(fabsf(x), y);
            }
        }
        for (S32 r = 0; r < rows; ++r)
        {
            for (S32 c = 0; c < columns; ++c)
            {
                const U16 a = (U16)(c + r * (columns + 1));
                const U16 b = (U16)(a + 1);
                const U16 d = (U16)(a + columns + 1);
                const U16 e = (U16)(d + 1);
                mesh.indices.insert(mesh.indices.end(), { a, b, e, a, e, d });
            }
        }
        return mesh;
    }

    // A sphere skinned to two joints a vertex, as the asset carries weights:
    // the joint in the integer part, its weight in the fraction.
    CorpusMesh riggedSphere()
    {
        CorpusMesh mesh = uvSphere(16, 8);
        mesh.name = "rigged UV sphere";
        for (size_t v = 0; v < mesh.positions.size(); ++v)
        {
            const F32 w = 0.25f + 0.25f * (v % 3);
            mesh.weights.emplace_back((F32)(v % 5) + w, (F32)(5 + (v + 1) % 5) + (1.f - w), 0.f, 0.f);
        }
        return mesh;
    }

    // A grid with triangles that have no area added: a repeated index,
    // three points in a line, and a copy of a real triangle.
    CorpusMesh degenerateGrid()
    {
        CorpusMesh mesh;
        mesh.name = "grid with degenerate triangles";
        constexpr S32 size = 6;
        for (S32 r = 0; r <= size; ++r)
        {
            for (S32 c = 0; c <= size; ++c)
            {
                mesh.positions.emplace_back((F32)c / size - 0.5f, (F32)r / size - 0.5f, 0.f);
                mesh.normals.emplace_back(0.f, 0.f, 1.f);
                mesh.uvs.emplace_back((F32)c / size, (F32)r / size);
            }
        }
        for (S32 r = 0; r < size; ++r)
        {
            for (S32 c = 0; c < size; ++c)
            {
                const U16 a = (U16)(c + r * (size + 1));
                mesh.indices.insert(mesh.indices.end(), { a, (U16)(a + 1), (U16)(a + size + 2), a, (U16)(a + size + 2), (U16)(a + size + 1) });
            }
        }
        mesh.indices.insert(mesh.indices.end(), { 3, 3, 4, 0, 1, 2, 8, 9, 10 });
        mesh.indices.insert(mesh.indices.end(), { mesh.indices[0], mesh.indices[1], mesh.indices[2] });
        return mesh;
    }

    // A curved grid whose vertices are each held twice, the same in every
    // attribute: each quad's first triangle takes the first copies and its
    // second the others.
    CorpusMesh duplicatedGrid()
    {
        CorpusMesh mesh;
        mesh.name = "grid of vertices held twice";
        constexpr S32 size = 6;
        for (S32 copy = 0; copy < 2; ++copy)
        {
            for (S32 r = 0; r <= size; ++r)
            {
                for (S32 c = 0; c <= size; ++c)
                {
                    const F32 x = (F32)c / size - 0.5f;
                    mesh.positions.emplace_back(x, (F32)r / size - 0.5f, 0.25f * x * x);
                    LLVector3 n(-0.5f * x, 0.f, 1.f);
                    n.normalize();
                    mesh.normals.push_back(n);
                    mesh.uvs.emplace_back((F32)c / size, (F32)r / size);
                }
            }
        }
        const U16 copy = (U16)((size + 1) * (size + 1));
        for (S32 r = 0; r < size; ++r)
        {
            for (S32 c = 0; c < size; ++c)
            {
                const U16 a = (U16)(c + r * (size + 1));
                mesh.indices.insert(mesh.indices.end(), { a, (U16)(a + 1), (U16)(a + size + 2) });
                mesh.indices.insert(mesh.indices.end(), { (U16)(a + copy), (U16)(a + size + 2 + copy), (U16)(a + size + 1 + copy) });
            }
        }
        return mesh;
    }

    std::vector<CorpusMesh> corpus()
    {
        return { uvSphere(24, 12), splitCube(), mirroredStrip(), riggedSphere(), degenerateGrid(), duplicatedGrid(), uvSphere(64, 32) };
    }

    // A strip of 32,760 columns by two rows whose U runs back and forth, a
    // mirror seam at every column, gently curved: each vertex's corners take
    // two tangents, which would split it into more vertices than 16-bit
    // indices reach. Its last 16 vertices are copies of others, which its
    // triangles take in place of them.
    constexpr S32 ZIGZAG_COPIES = 16;
    CorpusMesh zigzagStrip()
    {
        CorpusMesh mesh;
        mesh.name = "strip of 32,760 mirrored columns";
        constexpr S32 columns = 32760;
        for (S32 r = 0; r < 2; ++r)
        {
            for (S32 c = 0; c < columns; ++c)
            {
                const F32 x = c * 0.01f;
                mesh.positions.emplace_back(x, (F32)r, 0.1f * sinf(x));
                LLVector3 n(-0.1f * cosf(x), 0.f, 1.f);
                n.normalize();
                mesh.normals.push_back(n);
                mesh.uvs.emplace_back((F32)(c % 2), (F32)r);
            }
        }
        for (S32 c = 0; c + 1 < columns; ++c)
        {
            const U16 a = (U16)c;
            const U16 b = (U16)(c + 1);
            const U16 d = (U16)(c + columns);
            const U16 e = (U16)(d + 1);
            mesh.indices.insert(mesh.indices.end(), { a, b, e, a, e, d });
        }
        for (S32 k = 0; k < ZIGZAG_COPIES; ++k)
        {
            const U16 original = (U16)(k * 997 + 3);
            const U16 copy = (U16)mesh.positions.size();
            mesh.positions.push_back(mesh.positions[original]);
            mesh.normals.push_back(mesh.normals[original]);
            mesh.uvs.push_back(mesh.uvs[original]);
            for (size_t i = (size_t)k * 6; i < mesh.indices.size(); ++i)
            {
                if (mesh.indices[i] == original)
                {
                    mesh.indices[i] = copy;
                    break;
                }
            }
        }
        return mesh;
    }

    // A triangle by its corners' positions, rotated to start at the least,
    // so that the same triangle reads the same in any index order.
    using TriangleKey = std::array<std::array<U32, 3>, 3>;

    TriangleKey keyOf(const LLVolumeFace& face, S32 t, S32* first = nullptr)
    {
        TriangleKey corners;
        for (S32 k = 0; k < 3; ++k)
        {
            corners[k] = bitsOf(face.mPositions[face.mIndices[t * 3 + k]]);
        }
        S32 start = 0;
        for (S32 k = 1; k < 3; ++k)
        {
            if (corners[k] < corners[start])
            {
                start = k;
            }
        }
        if (first)
        {
            *first = start;
        }
        return { corners[start], corners[(start + 1) % 3], corners[(start + 2) % 3] };
    }

    LLVector3 unitOrZero(LLVector3 v)
    {
        if (v.lengthSquared() > 0.f)
        {
            v.normalize();
        }
        return v;
    }

    // Whether an optimized corner keeps a decoded one's normal at unit
    // length, UV and weights.
    bool sameCorner(const LLVolumeFace& before, U16 i, const LLVolumeFace& after, U16 o)
    {
        if ((v3(after.mNormals[o]) - unitOrZero(v3(before.mNormals[i]))).length() > 1e-5f || !(after.mTexCoords[o] == before.mTexCoords[i]))
        {
            return false;
        }
        return !before.mWeights ||
               (after.mWeights && v3(after.mWeights[o]) == v3(before.mWeights[i]) && after.mWeights[o][3] == before.mWeights[i][3]);
    }

    // Whether no two vertices of a face are the same in every attribute,
    // the tangent among them where with_tangent says.
    bool verticesDistinct(const LLVolumeFace& face, bool with_tangent)
    {
        using Key = std::array<U32, 16>;
        std::vector<Key> keys(face.mNumVertices);
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            Key& key = keys[v];
            key.fill(0);
            std::memcpy(&key[0], face.mPositions[v].getF32ptr(), 3 * sizeof(F32));
            std::memcpy(&key[3], face.mNormals[v].getF32ptr(), 3 * sizeof(F32));
            std::memcpy(&key[6], face.mTexCoords[v].mV, 2 * sizeof(F32));
            if (with_tangent && face.mTangents)
            {
                std::memcpy(&key[8], face.mTangents[v].getF32ptr(), 4 * sizeof(F32));
            }
            if (face.mWeights)
            {
                std::memcpy(&key[12], face.mWeights[v].getF32ptr(), 4 * sizeof(F32));
            }
        }
        std::sort(keys.begin(), keys.end());
        return std::adjacent_find(keys.begin(), keys.end()) == keys.end();
    }

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off", "no-associative-math", "no-reciprocal-math")
#endif
    // Whether MikkTSpace takes a triangle of a decoded face as one with a
    // tangent space of its own, by its own float arithmetic over the face as
    // authored, the positions scaled out of the unit cube: distinct corners,
    // a UV area, and a tangent and bitangent whose lengths over that area
    // exceed FLT_MIN. A triangle that is not takes its orientation from the
    // triangles about it. This compiles precise, as meshoptimizer does, so
    // that the squares and products round as there.
    bool mikkGood(const LLVolumeFace& face, S32 t)
    {
        const LLVector3& scale = face.mNormalizedScale;
        F32 p[3][3];
        F32 uv[3][2];
        for (S32 k = 0; k < 3; ++k)
        {
            const U16 v = face.mIndices[t * 3 + k];
            for (S32 i = 0; i < 3; ++i)
            {
                p[k][i] = face.mPositions[v][i] * scale.mV[i];
            }
            uv[k][0] = face.mTexCoords[v].mV[0];
            uv[k][1] = face.mTexCoords[v].mV[1];
        }
        for (S32 a = 0; a < 3; ++a)
        {
            const S32 b = (a + 1) % 3;
            if (p[a][0] == p[b][0] && p[a][1] == p[b][1] && p[a][2] == p[b][2])
            {
                return false;
            }
        }
        const F32 t21x = uv[1][0] - uv[0][0], t21y = uv[1][1] - uv[0][1];
        const F32 t31x = uv[2][0] - uv[0][0], t31y = uv[2][1] - uv[0][1];
        const F32 area = fabsf(t21x * t31y - t21y * t31x);
        if (!(area > FLT_MIN))
        {
            return false;
        }
        F32 os[3], ot[3];
        for (S32 i = 0; i < 3; ++i)
        {
            const F32 d1 = p[1][i] - p[0][i], d2 = p[2][i] - p[0][i];
            os[i] = t31y * d1 - t21y * d2;
            ot[i] = -t31x * d1 + t21x * d2;
        }
        const F32 los = sqrtf(os[0] * os[0] + os[1] * os[1] + os[2] * os[2]);
        const F32 lot = sqrtf(ot[0] * ot[0] + ot[1] * ot[1] + ot[2] * ot[2]);
        return los / area > FLT_MIN && lot / area > FLT_MIN;
    }
#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif

    // cacheOptimize keeps every triangle, and every corner's position,
    // normal at unit length, UV and weights, while it welds and reorders;
    // it gives every vertex a unit tangent orthogonal to its normal, whose
    // w follows the UV winding of each triangle the vertex is in, as
    // MikkTSpace's handedness does. A face decoded with a normal of no
    // length keeps it so. Triangles are matched by their corners'
    // positions, and among those of the same positions by the rest.
    void checkOptimized(const LLVolumeFace& before, const LLVolumeFace& after, Report& report, const std::string& where)
    {
        if (after.mNumIndices != before.mNumIndices)
        {
            report.fail(fmt::format("{}: {} indices became {}", where, before.mNumIndices, after.mNumIndices));
            return;
        }
        for (S32 i = 0; i < after.mNumIndices; ++i)
        {
            if (after.mIndices[i] >= after.mNumVertices)
            {
                report.fail(fmt::format("{}: index {} is {} of {} vertices", where, i, after.mIndices[i], after.mNumVertices));
                return;
            }
        }
        const std::vector<Triangle> in = trianglesOf(before);
        const std::vector<Triangle> out = trianglesOf(after);
        std::multimap<TriangleKey, S32> by_key;
        std::vector<TriangleKey> in_keys, out_keys;
        for (S32 t = 0; t < (S32)in.size(); ++t)
        {
            if (!in[t].degenerate)
            {
                in_keys.push_back(keyOf(before, t));
                by_key.emplace(in_keys.back(), t);
            }
        }
        for (S32 t = 0; t < (S32)out.size(); ++t)
        {
            if (!out[t].degenerate)
            {
                out_keys.push_back(keyOf(after, t));
            }
        }
        std::sort(in_keys.begin(), in_keys.end());
        std::sort(out_keys.begin(), out_keys.end());
        if (in_keys != out_keys)
        {
            report.fail(fmt::format("{}: {} real triangles in and {} out, not the same set", where, in_keys.size(), out_keys.size()));
            return;
        }

        // A face too large for its weld, with one of each vertex, gives each
        // its first corner's tangent, so that a vertex of triangles winding
        // both ways, or whose first triangle's UVs have no winding, follows
        // only some of them: all but a hundredth of its vertices follow one.
        const bool unsplit = before.mNumIndices >= 65535 && verticesDistinct(after, false);
        std::vector<U8> windings(after.mNumVertices, 0);

        for (S32 t = 0; t < (S32)out.size(); ++t)
        {
            if (out[t].degenerate)
            {
                continue;
            }
            S32 out_first = 0;
            const TriangleKey key = keyOf(after, t, &out_first);
            bool kept = false;
            S32 source = -1;
            for (auto [it, end] = by_key.equal_range(key); it != end && !kept; ++it)
            {
                S32 in_first = 0;
                keyOf(before, it->second, &in_first);
                kept = true;
                for (S32 k = 0; k < 3 && kept; ++k)
                {
                    kept = sameCorner(before, before.mIndices[it->second * 3 + (in_first + k) % 3], after, after.mIndices[t * 3 + (out_first + k) % 3]);
                }
                source = kept ? it->second : -1;
            }
            if (!kept)
            {
                report.fail(fmt::format("{}: triangle {} lost a corner's normal, UV or weights", where, t));
                return;
            }
            const LLVector2 uv0 = after.mTexCoords[after.mIndices[t * 3]];
            const LLVector2 uv1 = after.mTexCoords[after.mIndices[t * 3 + 1]];
            const LLVector2 uv2 = after.mTexCoords[after.mIndices[t * 3 + 2]];
            if (!after.mTangents)
            {
                report.fail(fmt::format("{}: no tangents", where));
                return;
            }
            // The UVs' winding from their differences at a float, as
            // meshoptimizer takes them, and their products exact. A triangle
            // whose UVs are all but in a line, against their size, wind by
            // less than MikkTSpace's least area, or have products past a
            // float's range, has no winding to follow, nor has one that
            // MikkTSpace, taking the triangle as it was decoded, gives no
            // tangent space of its own.
            const F32 d1u = uv1.mV[0] - uv0.mV[0], d1v = uv1.mV[1] - uv0.mV[1];
            const F32 d2u = uv2.mV[0] - uv0.mV[0], d2v = uv2.mV[1] - uv0.mV[1];
            const F64 along = (F64)d1u * d2v, across = (F64)d2u * d1v;
            const F64 uv_area = along - across;
            const F64 edges = sqrt(((F64)d1u * d1u + (F64)d1v * d1v) * ((F64)d2u * d2u + (F64)d2v * d2v));
            const F32 expected_w = uv_area > 0. ? 1.f : -1.f;
            const bool winds = fabs(along) <= FLT_MAX && fabs(across) <= FLT_MAX && fabs(uv_area) > llmax(1e-5 * edges, (F64)FLT_MIN) &&
                               mikkGood(before, source);
            for (S32 k = 0; k < 3 && winds; ++k)
            {
                const U16 o = after.mIndices[t * 3 + k];
                if (unsplit)
                {
                    windings[o] |= expected_w > 0.f ? 1 : 2;
                    continue;
                }
                if (after.mTangents[o][3] != expected_w)
                {
                    report.fail(fmt::format("{}: triangle {}'s UVs wind for w = {} but vertex {} has {}", where, t, expected_w, o, after.mTangents[o][3]));
                    return;
                }
            }
        }

        if (unsplit)
        {
            S32 against = 0;
            for (S32 v = 0; v < after.mNumVertices; ++v)
            {
                against += windings[v] && !(windings[v] & (after.mTangents[v][3] > 0.f ? 1 : 2));
            }
            if (against * 100 > after.mNumVertices)
            {
                report.fail(fmt::format("{}: {} of {} vertices have a w none of their triangles' UVs wind for", where, against, after.mNumVertices));
                return;
            }
        }

        bool zero_in = false;
        for (S32 v = 0; v < before.mNumVertices; ++v)
        {
            zero_in |= v3(before.mNormals[v]).lengthSquared() == 0.f;
        }
        for (S32 v = 0; v < after.mNumVertices; ++v)
        {
            const LLVector3 n = v3(after.mNormals[v]);
            const LLVector3 t = after.mTangents ? v3(after.mTangents[v]) : LLVector3::zero;
            const F32 w = after.mTangents ? after.mTangents[v][3] : 0.f;
            const bool unit_n = fabsf(n.length() - 1.f) <= UNIT_TOLERANCE || (zero_in && n.lengthSquared() == 0.f);
            if (!unit_n || !(fabsf(t.length() - 1.f) <= UNIT_TOLERANCE) || !(w == 1.f || w == -1.f) || !(fabsf(n * t) <= 2e-3f))
            {
                report.fail(fmt::format("{}: vertex {} has normal length {}, tangent length {}, w {} and n.t {}", where, v, n.length(), t.length(), w, n * t));
                return;
            }
        }
    }

    // The tangent cacheOptimize gives each corner of a decoded face:
    // meshoptimizer's compatible one over the face as it was authored, its
    // positions scaled out of the unit cube and its unit normals by the
    // inverse, taken back into the face's frame as a direction along its
    // surface. A corner that gives no direction is (0, 0, 0, w).
    std::vector<LLVector4a> expectedTangents(const LLVolumeFace& face)
    {
        const LLVector3& scale = face.mNormalizedScale;
        const LLVector3 inv_scale(1.f / scale.mV[0], 1.f / scale.mV[1], 1.f / scale.mV[2]);
        std::vector<LLVector4a> positions(face.mNumVertices), normals(face.mNumVertices);
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            LLVector3 p = v3(face.mPositions[v]);
            p.scaleVec(scale);
            positions[v].load3(p.mV);
            LLVector3 n = v3(face.mNormals[v]);
            n.normalize();
            n.scaleVec(inv_scale);
            n.normalize();
            normals[v].load3(n.mV);
        }
        const std::vector<U32> indices(face.mIndices, face.mIndices + face.mNumIndices);
        std::vector<F32> corners(indices.size() * 4);
        meshopt_generateTangents(corners.data(), indices.data(), indices.size(), positions[0].getF32ptr(), positions.size(), sizeof(LLVector4a),
                                 normals[0].getF32ptr(), sizeof(LLVector4a), face.mTexCoords[0].mV, sizeof(LLVector2),
                                 meshopt_TangentCompatible | meshopt_TangentZeroFallback);
        std::vector<LLVector4a> out(indices.size());
        for (size_t c = 0; c < indices.size(); ++c)
        {
            LLVector3 t(&corners[c * 4]);
            t.scaleVec(inv_scale);
            if (t.lengthSquared() > 0.f)
            {
                t.normalize();
            }
            out[c].set(t.mV[0], t.mV[1], t.mV[2], corners[c * 4 + 3]);
        }
        return out;
    }

    // The optimized face's tangents against expectedTangents, corner for
    // corner over its real triangles, each matched to the decoded triangle
    // of the same corners. A corner that gives no direction takes one of
    // unit length in the plane of its normal.
    void checkTangentsExpected(const LLVolumeFace& before, const LLVolumeFace& after, Report& report, const std::string& where)
    {
        const std::vector<LLVector4a> expected = expectedTangents(before);
        const std::vector<Triangle> in = trianglesOf(before);
        const std::vector<Triangle> out = trianglesOf(after);
        std::multimap<TriangleKey, S32> by_key;
        for (S32 t = 0; t < (S32)in.size(); ++t)
        {
            if (!in[t].degenerate)
            {
                by_key.emplace(keyOf(before, t), t);
            }
        }
        F32 worst = 0.f;
        for (S32 t = 0; t < (S32)out.size(); ++t)
        {
            if (out[t].degenerate)
            {
                continue;
            }
            S32 out_first = 0;
            const auto found = by_key.find(keyOf(after, t, &out_first));
            if (found == by_key.end())
            {
                report.fail(fmt::format("{}: triangle {} is no triangle of the decoded face", where, t));
                return;
            }
            S32 in_first = 0;
            keyOf(before, found->second, &in_first);
            for (S32 k = 0; k < 3; ++k)
            {
                const U16 o = after.mIndices[t * 3 + (out_first + k) % 3];
                const LLVector4a& want = expected[found->second * 3 + (in_first + k) % 3];
                const LLVector3 got = v3(after.mTangents[o]);
                if (after.mTangents[o][3] != want[3])
                {
                    report.fail(fmt::format("{}: vertex {} has w {} where its corner's is {}", where, o, after.mTangents[o][3], want[3]));
                    return;
                }
                if (v3(want).lengthSquared() == 0.f)
                {
                    if (!(fabsf(got.length() - 1.f) <= UNIT_TOLERANCE) || !(fabsf(got * v3(after.mNormals[o])) <= 2e-3f))
                    {
                        report.fail(fmt::format("{}: vertex {}, of a corner with no direction, has tangent length {} and n.t {}", where, o,
                                                got.length(), got * v3(after.mNormals[o])));
                        return;
                    }
                    continue;
                }
                // By the sine as well as the cosine: acos alone cannot tell
                // apart directions a float's rounding apart.
                const LLVector3 w3 = v3(want);
                worst = llmax(worst, atan2f((got % w3).length(), got * w3) * RAD_TO_DEG);
            }
        }
        if (!(worst <= 0.01f))
        {
            report.fail(fmt::format("{}: a tangent is {:.4f} degrees from meshoptimizer's", where, worst));
        }
    }

    // No two vertices of an optimized face are the same in every attribute,
    // so a weld of position, normal, UV, weights and tangent together would
    // leave as many.
    void checkWeldedOnce(const LLVolumeFace& face, Report& report, const std::string& where)
    {
        if (!verticesDistinct(face, true))
        {
            report.fail(fmt::format("{}: two vertices are the same in every attribute", where));
        }
    }

    // A face as a mesh LOD whose authored shape is the corpus mesh's
    // positions times scale, with that shape's normals.
    LLVolumeFace scaledFace(const CorpusMesh& mesh, const LLVector3& scale)
    {
        LLVolumeFace face = mesh.face();
        face.mNormalizedScale = scale;
        return face;
    }

    // meshoptimizer on the thread's arena while it lives, and on its own
    // default, operator new and delete, after.
    struct ArenaInstalled
    {
        static void* MESHOPTIMIZER_ALLOC_CALLCONV newBlock(size_t bytes) { return ::operator new(bytes); }
        static void MESHOPTIMIZER_ALLOC_CALLCONV deleteBlock(void* block) { ::operator delete(block); }

        ArenaInstalled() { meshopt_setAllocator(ALScratchArena::allocate, ALScratchArena::deallocate); }
        ~ArenaInstalled() { meshopt_setAllocator(newBlock, deleteBlock); }
    };

    // ------------------------------------------------------------------
    // Vertex cache and fetch figures, as meshoptimizer models them: a
    // 16-entry FIFO, and fetches of whole 64-byte lines.

    struct Figures
    {
        F64 transformed = 0;
        F64 triangles = 0;
        F64 fetched = 0;
        F64 buffer = 0;

        void add(const LLVolumeFace& face)
        {
            const std::vector<U32> indices(face.mIndices, face.mIndices + face.mNumIndices);
            const meshopt_VertexCacheStatistics cache = meshopt_analyzeVertexCache(indices.data(), indices.size(), face.mNumVertices, 16, 0, 0);
            const meshopt_VertexFetchStatistics fetch = meshopt_analyzeVertexFetch(indices.data(), indices.size(), face.mNumVertices, VERTEX_BYTES);
            transformed += cache.vertices_transformed;
            triangles += indices.size() / 3;
            fetched += fetch.bytes_fetched;
            buffer += (F64)face.mNumVertices * VERTEX_BYTES;
        }

        void add(const LLVolume& volume)
        {
            for (S32 i = 0; i < volume.getNumVolumeFaces(); ++i)
            {
                add(volume.getVolumeFace(i));
            }
        }

        // Vertices transformed a triangle.
        F64 acmr() const { return triangles > 0 ? transformed / triangles : 0; }
        // Bytes fetched over the bytes in the buffer.
        F64 overfetch() const { return buffer > 0 ? fetched / buffer : 0; }
    };

    // ------------------------------------------------------------------
    // Flexible prims

    // A flexible prim's volume as LLVolumeImplFlexible keeps one: unique,
    // its path a dynamic one that the rebuild sizes and fills.
    LLPointer<LLVolume> flexiVolume(Shape s, S32 lod)
    {
        s.path = LL_PCODE_PATH_FLEXIBLE;
        return new LLVolume(s.params(), LOD_DETAIL[lod], false, true);
    }

    LLPointer<LLVolume> flexiVolume(U8 profile, U8 hole, F32 hollow, S32 lod)
    {
        Shape s;
        s.profile = profile;
        s.hole = hole;
        s.hollow = hollow;
        return flexiVolume(s, lod);
    }

    // The path a flexible rebuild writes: a unit length bent into an arc
    // of the given angle about Y, section by section.
    void bendPath(LLVolume& volume, S32 sections, F32 bend)
    {
        LLPath& path = volume.getPath();
        if (path.getPathLength() != sections + 1)
        {
            volume.resizePath(sections + 1);
        }
        for (S32 i = 0; i <= sections; ++i)
        {
            const F32 t = (F32)i / sections;
            const F32 a = bend * t;
            const LLVector3 pos = fabsf(bend) > 1e-4f ? LLVector3((1.f - cosf(a)) / bend, 0.f, sinf(a) / bend - 0.5f) : LLVector3(0.f, 0.f, t - 0.5f);
            LLPath::PathPt& point = path.mPath[i];
            point.mPos.load3(pos.mV);
            point.mRot.set(LLMatrix3(LLQuaternion(a, LLVector3(0.f, 1.f, 0.f))));
            point.mScale.set(1.f, 1.f, 0.f, 1.f);
            point.mTexT = t;
        }
    }

    void genAllTangents(LLVolume& volume)
    {
        for (S32 i = 0; i < volume.getNumVolumeFaces(); ++i)
        {
            volume.genTangents(i);
        }
    }

    struct Buffers
    {
        const LLVector4a* positions;
        const U16* indices;
        const LLVector4a* tangents;
    };

    std::vector<Buffers> buffersOf(const LLVolume& volume)
    {
        std::vector<Buffers> out;
        for (S32 i = 0; i < volume.getNumVolumeFaces(); ++i)
        {
            const LLVolumeFace& face = volume.getVolumeFace(i);
            out.push_back({ face.mPositions, face.mIndices, face.mTangents });
        }
        return out;
    }

    F32 farthest(const LLVector4a* a, const LLVector4a* b, S32 count)
    {
        F32 off = 0.f;
        for (S32 i = 0; i < count; ++i)
        {
            const F32 d = (v3(a[i]) - v3(b[i])).length();
            off = std::isfinite(d) ? llmax(off, d) : 1e30f;
        }
        return off;
    }

    // Two builds of the same geometry hold the same faces.
    void compareBuilds(const LLVolume& partial, const LLVolume& full, Report& report, const std::string& name)
    {
        if (partial.getNumVolumeFaces() != full.getNumVolumeFaces())
        {
            report.fail(fmt::format("{}: {} faces against {}", name, partial.getNumVolumeFaces(), full.getNumVolumeFaces()));
            return;
        }
        for (S32 i = 0; i < full.getNumVolumeFaces(); ++i)
        {
            const LLVolumeFace& a = partial.getVolumeFace(i);
            const LLVolumeFace& b = full.getVolumeFace(i);
            const std::string where = fmt::format("{}: {}", name, faceName(b));
            if (a.mNumVertices != b.mNumVertices || a.mNumIndices != b.mNumIndices)
            {
                report.fail(fmt::format("{}: {} vertices and {} indices against {} and {}", where, a.mNumVertices, a.mNumIndices, b.mNumVertices, b.mNumIndices));
                continue;
            }
            const S32 n = b.mNumVertices;
            if (farthest(a.mPositions, b.mPositions, n) > 1e-5f)
            {
                report.fail(fmt::format("{}: positions differ by {}", where, farthest(a.mPositions, b.mPositions, n)));
            }
            if (farthest(a.mNormals, b.mNormals, n) > 1e-4f)
            {
                report.fail(fmt::format("{}: normals differ by {}", where, farthest(a.mNormals, b.mNormals, n)));
            }
            if (!a.mTangents || !b.mTangents || farthest(a.mTangents, b.mTangents, n) > 1e-4f)
            {
                report.fail(fmt::format("{}: tangents differ by {}", where, a.mTangents && b.mTangents ? farthest(a.mTangents, b.mTangents, n) : -1.f));
            }
            for (S32 v = 0; v < n; ++v)
            {
                if ((a.mTexCoords[v] - b.mTexCoords[v]).length() > 1e-6f)
                {
                    report.fail(fmt::format("{}: UV {} differs", where, v));
                    break;
                }
            }
            if (!std::equal(a.mIndices, a.mIndices + a.mNumIndices, b.mIndices))
            {
                report.fail(fmt::format("{}: indices differ", where));
            }
            if (farthest(a.mExtents, b.mExtents, 3) > 1e-5f)
            {
                report.fail(fmt::format("{}: extents or centre differ by {}", where, farthest(a.mExtents, b.mExtents, 3)));
            }
        }
    }

    // A flexible prim's frame, as the rebuild runs it: the path moves, the
    // volume is rebuilt in place, and genTangents brings its faces'
    // tangents up to the moved positions. The rebuild must come out as a
    // fresh volume built in full on the moved path, in the buffers it had.
    void checkFlexiFrame(U8 profile, U8 hole, F32 hollow, S32 sections, S32 lod, Report& report, const std::string& name)
    {
        LLPointer<LLVolume> volume = flexiVolume(profile, hole, hollow, lod);
        bendPath(*volume, sections, 0.4f);
        volume->regen();
        genAllTangents(*volume);
        const std::vector<Buffers> before = buffersOf(*volume);

        bendPath(*volume, sections, 1.1f);
        volume->regen();
        const std::vector<Buffers> after = buffersOf(*volume);
        genAllTangents(*volume);

        LLPointer<LLVolume> fresh = flexiVolume(profile, hole, hollow, lod);
        bendPath(*fresh, sections, 1.1f);
        fresh->regen();
        genAllTangents(*fresh);

        compareBuilds(*volume, *fresh, report, name);
        for (size_t i = 0; i < before.size() && i < after.size(); ++i)
        {
            const std::string where = fmt::format("{}: {}", name, faceName(volume->getVolumeFace((S32)i)));
            if (after[i].positions != before[i].positions || after[i].indices != before[i].indices)
            {
                report.fail(fmt::format("{}: the partial build moved its vertices or indices", where));
            }
            if (after[i].tangents != before[i].tangents)
            {
                report.fail(fmt::format("{}: the partial build dropped its tangent buffer", where));
            }
        }
    }

    // ------------------------------------------------------------------

    TEST_CASE("llvolume: representative prims have the face IDs texture entries map by", "[llvolume]")
    {
        const LLFaceID B = LL_FACE_PATH_BEGIN, E = LL_FACE_PATH_END, I = LL_FACE_INNER_SIDE, PB = LL_FACE_PROFILE_BEGIN, PE = LL_FACE_PROFILE_END;
        const LLFaceID O0 = LL_FACE_OUTER_SIDE_0, O1 = LL_FACE_OUTER_SIDE_1, O2 = LL_FACE_OUTER_SIDE_2, O3 = LL_FACE_OUTER_SIDE_3;
        struct Expected
        {
            Shape shape;
            std::vector<LLFaceID> ids;
        };
        auto with = [](Shape s, const char* name, auto&& edit)
        {
            edit(s);
            s.name = name;
            return s;
        };
        const Expected table[] = {
            { box(), { B, O0, O1, O2, O3, E } },
            { with(box(), "hollow box", [](Shape& s) { s.hollow = 0.5f; }), { B, O0, O1, O2, O3, I, E } },
            { with(box(), "cut box", [](Shape& s) { s.cut_begin = 0.2f; s.cut_end = 0.7f; }), { B, O0, O1, O2, E, PB, PE } },
            { with(box(), "cut hollow box", [](Shape& s) { s.cut_begin = 0.2f; s.cut_end = 0.7f; s.hollow = 0.5f; }), { B, O0, O1, O2, I, E, PB, PE } },
            { with(box(), "twisted box", [](Shape& s) { s.twist_end = 1.f; }), { B, O0, O1, O2, O3, E } },
            { cylinder(), { B, O0, E } },
            { with(cylinder(), "hollow cylinder", [](Shape& s) { s.hollow = 0.5f; }), { B, O0, I, E } },
            { with(cylinder(), "cut cylinder", [](Shape& s) { s.cut_begin = 0.25f; }), { B, O0, E, PB, PE } },
            { with(cylinder(), "cut hollow cylinder", [](Shape& s) { s.cut_begin = 0.25f; s.hollow = 0.3f; }), { B, O0, I, E, PB, PE } },
            { prism(), { B, O0, O1, O2, E } },
            { sphere(), { O0 } },
            { with(sphere(), "hollow sphere", [](Shape& s) { s.hollow = 0.5f; }), { O0, I, PB, PE } },
            { with(sphere(), "dimpled sphere", [](Shape& s) { s.cut_begin = 0.2f; s.cut_end = 0.8f; }), { O0, PB, PE } },
            { with(sphere(), "hemisphere", [](Shape& s) { s.path_end = 0.5f; }), { B, O0, E } },
            { torus(), { O0 } },
            { with(torus(), "cut torus", [](Shape& s) { s.path_end = 0.75f; }), { B, O0, E } },
            { with(torus(), "twisted torus", [](Shape& s) { s.twist_end = 0.5f; }), { B, O0, E } },
            { tube(), { O0, O1, O2, O3 } },
            { ring(), { O0, O1, O2 } },
        };
        for (const Expected& expected : table)
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = build(expected.shape, lod);
                INFO(lodName(expected.shape, lod));
                CHECK(faceIDsOf(*volume) == expected.ids);
                CHECK(expectedFaceIDs(volume->getParams()) == expected.ids);
                CHECK(volume->getNumVolumeFaces() == (S32)expected.ids.size());
            }
        }
    }

    TEST_CASE("llvolume: every prim of the sweep keeps the invariants at every LOD", "[llvolume]")
    {
        Report report;
        S32 volumes = 0;
        inspectSweep(Inspect(), report, volumes);
        INFO(fmt::format("{} volumes", volumes));
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // Flexible prims as their rebuild shapes them: each profile and hole,
    // whole and cut, on paths of 4 and 16 sections, straight and bent.
    TEST_CASE("llvolume: every flexible prim keeps the invariants at every LOD", "[llvolume]")
    {
        Report report;
        S32 volumes = 0;
        for (const Shape& base : sweepShapes())
        {
            // The sweep's line-path shapes, plain and profile cut: a
            // flexible path takes nothing else from the parameters.
            if (base.path != LL_PCODE_PATH_LINE || (base.name.find(", plain") == std::string::npos && base.name.find(", profile cut") == std::string::npos))
            {
                continue;
            }
            for (S32 sections : { 4, 16 })
            {
                for (F32 bend : { 0.f, 1.1f })
                {
                    for (S32 lod = 0; lod < LOD_COUNT; ++lod)
                    {
                        LLPointer<LLVolume> volume = flexiVolume(base, lod);
                        bendPath(*volume, sections, bend);
                        volume->regen();
                        const std::string name = fmt::format("flexible {}, {} sections bent {} at LOD {}", base.name, sections, bend, lod);
                        const std::vector<LLFaceID> expected = expectedFaceIDs(volume->getParams());
                        if (faceIDsOf(*volume) != expected || volume->getNumVolumeFaces() != (S32)expected.size())
                        {
                            report.fail(fmt::format("{}: {} faces where the rule gives {}", name, volume->getNumVolumeFaces(), expected.size()));
                            continue;
                        }
                        inspect(*volume, name, Inspect(), report);
                        ++volumes;
                    }
                }
            }
        }
        INFO(fmt::format("{} volumes", volumes));
        INFO(report.text());
        CHECK(volumes > 0);
        CHECK(report.failures == 0);
    }

    TEST_CASE("llvolume: every sculpt stitching keeps the invariants at every LOD", "[llvolume]")
    {
        Report report;
        inspectSculpts(Inspect(), report);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    TEST_CASE("llvolume: a pinched sculpt and a folded one keep the invariants", "[llvolume]")
    {
        Report report;
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            inspectSculpt(LL_SCULPT_TYPE_PLANE, pinchedMap(), lod, "pinched", Inspect(), report);
            inspectSculpt(LL_SCULPT_TYPE_PLANE, foldedMap(), lod, "folded", Inspect(), report);
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // Each triangle of a face, turned to start at its least index with its
    // winding kept, sorted.
    std::vector<std::array<U32, 3>> sortedTriangles(const std::vector<std::array<U32, 3>>& in)
    {
        std::vector<std::array<U32, 3>> out;
        for (std::array<U32, 3> t : in)
        {
            while (t[0] > t[1] || t[0] > t[2])
            {
                t = { t[1], t[2], t[0] };
            }
            out.push_back(t);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    bool samePoint(const LLVector4a& a, const LLVector4a& b)
    {
        return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
    }

    // A sculpt on a shared volume keeps each of its grid's triangles but
    // those two of whose corners are one point, once each, whatever its
    // stitching, map and flags. A unique volume, as a flexible sculpt's is,
    // keeps every one. Normals come from the grid's quads either way.
    TEST_CASE("llvolume: a sculpt leaves out the triangles two of whose corners are one point", "[llvolume]")
    {
        Report report;
        S32 left_out = 0;
        const auto check = [&](U8 type, const SculptMap& map, S32 lod, const std::string& name)
        {
            LLPointer<LLVolume> shared = buildSculpt(type, map, lod);
            LLVolumeParams p = shared->getParams();
            LLPointer<LLVolume> unique = new LLVolume(p, LOD_DETAIL[lod], false, true);
            unique->sculpt(map.width, map.height, 3, map.rgb.data(), 0, true);
            REQUIRE(shared->getNumVolumeFaces() == 1);
            REQUIRE(unique->getNumVolumeFaces() == 1);
            const LLVolumeFace& face = shared->getVolumeFace(0);
            const LLVolumeFace& whole = unique->getVolumeFace(0);
            REQUIRE(isGrid(face));
            std::vector<std::array<U32, 3>> grid, kept;
            for (S32 t = 0; t + 1 < face.mNumT; ++t)
            {
                for (S32 s = 0; s + 1 < face.mNumS; ++s)
                {
                    const U32 bl = gridIndex(face, s, t), br = bl + 1, tl = bl + face.mNumS, tr = tl + 1;
                    for (const std::array<U32, 3>& tri : { std::array<U32, 3>{ bl, tr, tl }, std::array<U32, 3>{ bl, br, tr } })
                    {
                        grid.push_back(tri);
                        const LLVector4a* pos = face.mPositions;
                        if (!samePoint(pos[tri[0]], pos[tri[1]]) && !samePoint(pos[tri[1]], pos[tri[2]]) && !samePoint(pos[tri[0]], pos[tri[2]]))
                        {
                            kept.push_back(tri);
                        }
                    }
                }
            }
            if (kept.empty())
            {
                kept = grid;
            }
            std::vector<std::array<U32, 3>> got, all;
            for (S32 i = 0; i + 2 < face.mNumIndices; i += 3)
            {
                got.push_back({ face.mIndices[i], face.mIndices[i + 1], face.mIndices[i + 2] });
            }
            for (S32 i = 0; i + 2 < whole.mNumIndices; i += 3)
            {
                all.push_back({ whole.mIndices[i], whole.mIndices[i + 1], whole.mIndices[i + 2] });
            }
            if (sortedTriangles(got) != sortedTriangles(kept))
            {
                report.fail(fmt::format("{}: {} triangles where {} of the grid's {} have three corners apart", name, got.size(), kept.size(), grid.size()));
            }
            if (sortedTriangles(all) != sortedTriangles(grid))
            {
                report.fail(fmt::format("{}: the unique volume has {} triangles of the grid's {}", name, all.size(), grid.size()));
            }
            if (whole.mNumVertices != face.mNumVertices || farthest(whole.mPositions, face.mPositions, face.mNumVertices) != 0.f ||
                farthest(whole.mNormals, face.mNormals, face.mNumVertices) != 0.f)
            {
                report.fail(fmt::format("{}: the unique volume's points or normals differ", name));
            }
            left_out += (S32)(grid.size() - got.size());
        };
        const U8 flags[] = { 0, LL_SCULPT_FLAG_MIRROR, LL_SCULPT_FLAG_INVERT };
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            for (U8 flag : flags)
            {
                check((U8)(LL_SCULPT_TYPE_PLANE | flag), pinchedMap(), lod, fmt::format("pinched plane, flags {}, LOD {}", flag, lod));
                check((U8)(LL_SCULPT_TYPE_PLANE | flag), foldedMap(), lod, fmt::format("folded plane, flags {}, LOD {}", flag, lod));
                for (U8 stitching : STITCHINGS)
                {
                    check((U8)(stitching | flag), mapFor(stitching, 64, 64), lod, fmt::format("{}, flags {}, LOD {}", stitchingName(stitching), flag, lod));
                }
            }
        }
        INFO(fmt::format("{} triangles left out", left_out));
        INFO(report.text());
        CHECK(left_out > 0);
        CHECK(report.failures == 0);
    }

    TEST_CASE("llvolume: a sculpt's copied row is a hard edge", "[llvolume]")
    {
        LLPointer<LLVolume> volume = buildSculpt(LL_SCULPT_TYPE_PLANE, foldedMap(), 3);
        REQUIRE(volume->getNumVolumeFaces() == 1);
        const LLVolumeFace& face = volume->getVolumeFace(0);
        REQUIRE(isGrid(face));
        // The rows run along the path, the map's y.
        REQUIRE(face.mNumT == 33);
        for (S32 s = 1; s + 1 < face.mNumS; ++s)
        {
            INFO(fmt::format("column {}", s));
            REQUIRE(coincide(face.mPositions[gridIndex(face, s, 10)], face.mPositions[gridIndex(face, s, 11)]));
            const LLVector3 below = v3(face.mNormals[gridIndex(face, s, 10)]);
            const LLVector3 above = v3(face.mNormals[gridIndex(face, s, 11)]);
            CHECK(degreesBetween(below, v3(face.mNormals[gridIndex(face, s, 5)])) < 1.f);
            CHECK(degreesBetween(above, v3(face.mNormals[gridIndex(face, s, 20)])) < 1.f);
            CHECK(degreesBetween(below, above) > 85.f);
        }
    }

    // A plane whose mesh vertex (10, 10) has a quad of no area at each of
    // its corners, each folded so that its diagonal's ends are one place:
    // vertices (9, 9), (10, 10) and (11, 11) are one point, and so are
    // (9, 10) and (10, 11), and (10, 9) and (11, 10). Vertex (c, r) reads
    // pixel (2c, 2r).
    SculptMap pinwheelMap()
    {
        auto surface = [](S32 x, S32 y)
        {
            const F32 u = (F32)x / (PINCH_MAP - 1);
            const F32 v = (F32)y / (PINCH_MAP - 1);
            return LLVector3(u - 0.5f, v - 0.5f, 0.2f * sinf(F_TWO_PI * u) * cosf(F_PI * v));
        };
        return sculptMap(PINCH_MAP, PINCH_MAP, [&](U16 x, U16 y)
        {
            if (x % 2 == 0 && y % 2 == 0)
            {
                const S32 dc = x / 2 - 10;
                const S32 dr = y / 2 - 10;
                if (dc == dr && dc >= -1 && dc <= 1)
                {
                    return surface(20, 20);
                }
                if ((dc == -1 && dr == 0) || (dc == 0 && dr == 1))
                {
                    return surface(18, 20);
                }
                if ((dc == 0 && dr == -1) || (dc == 1 && dr == 0))
                {
                    return surface(20, 18);
                }
            }
            return surface(x, y);
        });
    }

    // A vertex whose quads all have no area has no normal of its own, and
    // takes the nearest one down or up its column. Under fused multiply-add
    // the corners off a folded quad's diagonal would take the rounding of
    // an edge crossed with its own reverse for one.
    TEST_CASE("llvolume: a vertex among quads folded along their diagonals takes a neighbour's normal", "[llvolume]")
    {
        LLPointer<LLVolume> volume = buildSculpt(LL_SCULPT_TYPE_PLANE, pinwheelMap(), 3);
        REQUIRE(volume->getNumVolumeFaces() == 1);
        const LLVolumeFace& face = volume->getVolumeFace(0);
        REQUIRE(isGrid(face));
        auto at = [&](S32 c, S32 r) { return gridIndex(face, c, r); };
        REQUIRE(coincide(face.mPositions[at(9, 9)], face.mPositions[at(10, 10)]));
        REQUIRE(coincide(face.mPositions[at(11, 11)], face.mPositions[at(10, 10)]));
        REQUIRE(coincide(face.mPositions[at(9, 10)], face.mPositions[at(10, 11)]));
        REQUIRE(coincide(face.mPositions[at(10, 9)], face.mPositions[at(11, 10)]));
        REQUIRE(!coincide(face.mPositions[at(10, 10)], face.mPositions[at(10, 9)]));

        const LLVector4a& normal = face.mNormals[at(10, 10)];
        const LLVector4a& below = face.mNormals[at(10, 9)];
        INFO(fmt::format("({}, {}, {}) against ({}, {}, {}) below it", normal[0], normal[1], normal[2], below[0], below[1], below[2]));
        CHECK(std::memcmp(normal.getF32ptr(), below.getF32ptr(), 3 * sizeof(F32)) == 0);
    }

    // A map whose every pixel is one colour has no area, and makes the
    // sphere above LOD 0, where the area is tested.
    SculptMap oneColourMap()
    {
        return sculptMap(32, 32, [](U16, U16) { return LLVector3(0.1f, 0.1f, 0.1f); });
    }

    // The sphere a sculpt shows while its map is missing or has no area,
    // whatever its stitching, and the empty one it shows otherwise.
    void inspectPlaceholders(const Inspect& how, bool spheres_only, Report& report)
    {
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            for (U8 stitching : STITCHINGS)
            {
                for (bool visible : { true, false })
                {
                    if (spheres_only && !visible)
                    {
                        continue;
                    }
                    LLPointer<LLVolume> volume = buildSculpt(stitching, SculptMap(), lod, visible);
                    const std::string name =
                        fmt::format("{} placeholder of a {} sculpt at LOD {}", visible ? "sphere" : "empty", stitchingName(stitching), lod);
                    if (volume->getSculptLevel() != -1 || volume->getNumVolumeFaces() != 1)
                    {
                        report.fail(
                            fmt::format("{}: sculpt level {} and {} faces", name, volume->getSculptLevel(), volume->getNumVolumeFaces()));
                        continue;
                    }
                    inspect(*volume, name, how, report);
                }
                if (!spheres_only || lod > 0)
                {
                    inspectSculpt(stitching, oneColourMap(), lod, "one colour", how, report);
                }
            }
        }
    }

    // The placeholders' normals have a case of their own, below.
    TEST_CASE("llvolume: an empty sculpt map makes a placeholder", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::Layout | Checks::Finite | Checks::Tangents;
        Report report;
        inspectPlaceholders(how, false, report);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A long, thin map builds its surface at its own proportions, and the volume records the pixels it was given. The
    // same map with every row alike, as a decode with none of the detail across its short side comes out, has no area
    // and builds the sphere.
    TEST_CASE("llvolume: a long, thin sculpt map builds its surface, and one with its rows alike the sphere", "[llvolume]")
    {
        const S32 lod = LOD_COUNT - 1;
        LLPointer<LLVolume> volume = buildSculpt(LL_SCULPT_TYPE_CYLINDER, cylinderMap(512, 8), lod);
        CHECK_FALSE(volume->isSculptSpherePlaceholder());
        CHECK(volume->getSculptPixels() == 512 * 8);

        const SculptMap rows_alike = sculptMap(512, 8, [](U16 x, U16)
        {
            const F32 u = (F32)x / 512;
            return LLVector3(0.4f * cosf(F_TWO_PI * u), -0.4f * sinf(F_TWO_PI * u), -0.5f);
        });
        volume = buildSculpt(LL_SCULPT_TYPE_CYLINDER, rows_alike, lod);
        CHECK(volume->isSculptSpherePlaceholder());
        CHECK(volume->getSculptPixels() == 512 * 8);

        volume = buildSculpt(LL_SCULPT_TYPE_CYLINDER, SculptMap(), lod);
        CHECK(volume->getSculptPixels() == 0);
    }

    // An oblong map as Second Life's 512x8 sculpts are made: each ring of a tube on a pair of identical columns, and
    // every eighth column collapsed to the tube's axis. The coarser details sample every eighth or thirty-second
    // column, all collapsed, so the map as it is has no area there; it builds from the map averaged down instead of the
    // sphere, and the finest detail, whose samples have their area, builds from the map as it is.
    TEST_CASE("llvolume: an oblong sculpt map whose coarse samples have no area builds from it averaged down", "[llvolume]")
    {
        const SculptMap tube = sculptMap(512, 8, [](U16 x, U16 y)
        {
            if (x % 8 == 0)
            {
                return LLVector3(0.f, 0.f, (F32)x / 512 - 0.5f);
            }
            const F32 a = F_TWO_PI * (F32)y / 8;
            return LLVector3(0.2f * cosf(a), 0.2f * sinf(a), (F32)(x / 2 * 2) / 512 - 0.5f);
        });
        // From LOD 1: LOD 0's detail tests no area (SCULPT_MIN_AREA_DETAIL), and builds whatever the samples make.
        for (S32 lod = 1; lod < LOD_COUNT; ++lod)
        {
            INFO("LOD " << lod);
            LLPointer<LLVolume> volume = buildSculpt(LL_SCULPT_TYPE_TORUS, tube, lod);
            CHECK_FALSE(volume->isSculptSpherePlaceholder());
            CHECK(volume->getSculptPixels() == 512 * 8);
        }
        // As it is: 257 samples along its 512 columns, which an averaged map would not have the pixels for.
        LLPointer<LLVolume> finest = buildSculpt(LL_SCULPT_TYPE_TORUS, tube, LOD_COUNT - 1);
        CHECK(finest->getProfile().mProfile.size() == 257);
    }

    // A map of noise, whose samples have more surface than a sculpt may: it builds the sphere, as it always has and as
    // other viewers show it, rather than from the map averaged down until the noise has smoothed into bounds.
    TEST_CASE("llvolume: a sculpt map of noise has too much surface and builds the sphere", "[llvolume]")
    {
        const auto hash = [](U32 v)
        {
            v ^= v >> 16;
            v *= 0x7feb352du;
            v ^= v >> 15;
            v *= 0x846ca68bu;
            return v ^ (v >> 16);
        };
        // Each pixel a corner of the cube at random.
        const SculptMap noise = sculptMap(128, 128, [&](U16 x, U16 y)
        {
            const U32 h = hash(U32(y) * 128u + x);
            return LLVector3((h & 1) ? 0.5f : -0.5f, (h & 2) ? 0.5f : -0.5f, (h & 4) ? 0.5f : -0.5f);
        });
        LLPointer<LLVolume> volume = buildSculpt(LL_SCULPT_TYPE_PLANE, noise, LOD_COUNT - 1);
        INFO("area " << volume->getSurfaceArea());
        CHECK(volume->getSurfaceArea() > 384.f);
        CHECK(volume->isSculptSpherePlaceholder());
    }

    // Mirror reverses the sampling of each row and reflects x; invert
    // reverses the sampling alone, turning the surface inside out. Each
    // flips the tangent frame's handedness, and both together leave it.
    TEST_CASE("llvolume: a sculpt's tangent handedness follows mirror and invert", "[llvolume]")
    {
        const SculptMap map = sphereMap(64, 64);
        for (S32 lod = 1; lod < LOD_COUNT; ++lod)
        {
            LLPointer<LLVolume> plain = buildSculpt(LL_SCULPT_TYPE_SPHERE, map, lod);
            genAllTangents(*plain);
            const LLVolumeFace& base = plain->getVolumeFace(0);
            REQUIRE(isGrid(base));
            for (U8 flags : { LL_SCULPT_FLAG_MIRROR, LL_SCULPT_FLAG_INVERT, (U8)(LL_SCULPT_FLAG_MIRROR | LL_SCULPT_FLAG_INVERT) })
            {
                LLPointer<LLVolume> flipped = buildSculpt((U8)(LL_SCULPT_TYPE_SPHERE | flags), map, lod);
                genAllTangents(*flipped);
                const LLVolumeFace& face = flipped->getVolumeFace(0);
                REQUIRE(face.mNumS == base.mNumS);
                REQUIRE(face.mNumT == base.mNumT);
                const bool mirror = flags & LL_SCULPT_FLAG_MIRROR;
                const bool invert = flags & LL_SCULPT_FLAG_INVERT;
                const bool reversed = mirror != invert;
                const F32 handedness = mirror == invert ? 1.f : -1.f;
                S32 differ = 0;
                // Away from the poles, whose tangents have no direction to
                // follow.
                for (S32 t = 2; t + 2 < face.mNumT; ++t)
                {
                    for (S32 s = 1; s + 1 < face.mNumS; ++s)
                    {
                        const S32 v = gridIndex(face, s, t);
                        const S32 u = gridIndex(base, reversed ? base.mNumS - 1 - s : s, t);
                        LLVector3 expected = v3(base.mPositions[u]);
                        if (mirror)
                        {
                            expected.mV[0] = -expected.mV[0];
                        }
                        REQUIRE((v3(face.mPositions[v]) - expected).length() <= COINCIDE);
                        differ += face.mTangents[v][3] != handedness * base.mTangents[u][3];
                    }
                }
                INFO(fmt::format("LOD {}, {}{}", lod, mirror ? "mirrored" : "", invert ? " inverted" : ""));
                CHECK(differ == 0);
            }
        }
    }

    TEST_CASE("llvolume: cacheOptimize keeps every triangle and corner of the corpus", "[llvolume]")
    {
        for (const CorpusMesh& mesh : corpus())
        {
            const LLVolumeFace before = mesh.face();
            LLVolumeFace after = mesh.face();
            REQUIRE(after.cacheOptimize(true));
            Report report;
            checkOptimized(before, after, report, mesh.name);
            checkWeldedOnce(after, report, mesh.name);
            INFO(report.text());
            CHECK(report.failures == 0);
        }
    }

    TEST_CASE("llvolume: cacheOptimize splits the vertices a mirrored UV seam joins", "[llvolume]")
    {
        const CorpusMesh mesh = mirroredStrip();
        LLVolumeFace face = mesh.face();
        REQUIRE(face.cacheOptimize(true));
        // The centre column, shared by both halves, takes a vertex of each
        // handedness.
        CHECK(face.mNumVertices == (S32)mesh.positions.size() + 7);
    }

    // Each mesh scaled unevenly out of its unit cube, as most authored
    // meshes are: its tangents are taken in the authored shape and stored
    // as directions in the face's frame, where they lie in the plane of the
    // face's own normals.
    std::vector<LLVector3> unevenScales()
    {
        return { LLVector3(2.f, 0.5f, 1.f), LLVector3(0.25f, 3.f, 1.5f), LLVector3(1.f, 1.f, 4.f) };
    }

    std::string scaledName(const CorpusMesh& mesh, const LLVector3& scale)
    {
        return fmt::format("{} scaled {} by {} by {}", mesh.name, scale.mV[0], scale.mV[1], scale.mV[2]);
    }

    TEST_CASE("llvolume: cacheOptimize keeps every triangle and corner of a mesh scaled unevenly", "[llvolume]")
    {
        for (const CorpusMesh& mesh : corpus())
        {
            for (const LLVector3& scale : unevenScales())
            {
                const LLVolumeFace before = scaledFace(mesh, scale);
                LLVolumeFace after = before;
                REQUIRE(after.cacheOptimize(true));
                Report report;
                checkOptimized(before, after, report, scaledName(mesh, scale));
                INFO(report.text());
                CHECK(report.failures == 0);
            }
        }
    }

    TEST_CASE("llvolume: cacheOptimize's tangents are meshoptimizer's MikkTSpace-compatible ones", "[llvolume]")
    {
        for (const CorpusMesh& mesh : corpus())
        {
            std::vector<LLVector3> scales = unevenScales();
            scales.emplace_back(1.f, 1.f, 1.f);
            for (const LLVector3& scale : scales)
            {
                const LLVolumeFace before = scaledFace(mesh, scale);
                LLVolumeFace after = before;
                REQUIRE(after.cacheOptimize(true));
                Report report;
                checkTangentsExpected(before, after, report, scaledName(mesh, scale));
                INFO(report.text());
                CHECK(report.failures == 0);
            }
        }
    }

    // A weld of every attribute of every corner, tangent included, as
    // cacheOptimize made before it welded each vertex by its copies and its
    // tangents: the vertices that gave the corpus. A vertex the face holds
    // twice is one either way.
    TEST_CASE("llvolume: cacheOptimize makes no more vertices than a weld of every attribute", "[llvolume]")
    {
        struct Count
        {
            const char* name;
            S32 vertices;
        };
        const Count counts[] = {
            { "UV sphere 24 by 12", 323 },       { "cube with split normals", 24 },        { "strip with mirrored UVs", 98 },
            { "rigged UV sphere", 151 },         { "grid with degenerate triangles", 58 }, { "grid of vertices held twice", 49 },
            { "UV sphere 64 by 32", 2143 },
        };
        for (const CorpusMesh& mesh : corpus())
        {
            LLVolumeFace face = mesh.face();
            REQUIRE(face.cacheOptimize(true));
            const Count* count = std::find_if(std::begin(counts), std::end(counts), [&](const Count& c) { return mesh.name == c.name; });
            REQUIRE(count != std::end(counts));
            INFO(fmt::format("{}: {} vertices", mesh.name, face.mNumVertices));
            CHECK(face.mNumVertices <= count->vertices);
        }
    }

    // Too many vertices once split for 16-bit indices: the face keeps one of
    // each vertex it has, its copies welded, each with the tangent of its
    // first corner, and every triangle. Its scratch is larger than the
    // arena, which hands its largest blocks to the heap.
    TEST_CASE("llvolume: a face too large for its weld keeps one of each vertex and gets a tangent at each", "[llvolume]")
    {
        ArenaInstalled installed;
        const CorpusMesh mesh = zigzagStrip();
        const LLVolumeFace before = mesh.face();
        LLVolumeFace after = before;
        ALScratchArena::resetCounters();
        REQUIRE(after.cacheOptimize(true));
        const ALScratchArena::Counters counters = ALScratchArena::counters();
        CHECK(counters.heap_blocks > 0);

        CHECK(after.mNumVertices == before.mNumVertices - ZIGZAG_COPIES);
        REQUIRE(after.mNumIndices == before.mNumIndices);
        REQUIRE(after.mTangents);
        std::vector<TriangleKey> in_keys, out_keys;
        for (S32 t = 0; t < before.mNumIndices / 3; ++t)
        {
            in_keys.push_back(keyOf(before, t));
            out_keys.push_back(keyOf(after, t));
        }
        std::sort(in_keys.begin(), in_keys.end());
        std::sort(out_keys.begin(), out_keys.end());
        CHECK(in_keys == out_keys);

        Report report;
        for (S32 v = 0; v < after.mNumVertices; ++v)
        {
            const LLVector3 n = v3(after.mNormals[v]);
            const LLVector3 t = v3(after.mTangents[v]);
            const F32 w = after.mTangents[v][3];
            if (!(fabsf(t.length() - 1.f) <= UNIT_TOLERANCE) || !(fabsf(n * t) <= 2e-3f) || !(w == 1.f || w == -1.f))
            {
                report.fail(fmt::format("vertex {} has tangent length {}, n.t {} and w {}", v, t.length(), n * t, w));
            }
        }
        checkWeldedOnce(after, report, mesh.name);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // The corpus, the largest of it 8,385 vertices, through cacheOptimize
    // with meshoptimizer on the arena: everything from the arena.
    TEST_CASE("llvolume: cacheOptimize takes nothing from the heap for a face its arena holds", "[llvolume]")
    {
        ArenaInstalled installed;
        std::vector<CorpusMesh> meshes = corpus();
        meshes.push_back(uvSphere(128, 64));
        for (const CorpusMesh& mesh : meshes)
        {
            LLVolumeFace face = mesh.face();
            ALScratchArena::resetCounters();
            REQUIRE(face.cacheOptimize(true));
            const ALScratchArena::Counters counters = ALScratchArena::counters();
            INFO(fmt::format("{}: {} blocks from the arena, {} from the heap, {} bytes at most", mesh.name, counters.arena_blocks,
                             counters.heap_blocks, counters.peak));
            CHECK(counters.arena_blocks > 0);
            CHECK(counters.heap_blocks == 0);
        }
    }

    // Today's figures for each class, so that no later change orders a
    // class's indices or vertices worse than it is ordered now.
    TEST_CASE("llvolume: vertex cache and fetch figures stay within today's bounds", "[llvolume]")
    {
        struct Bound
        {
            const char* name;
            F64 transformed[LOD_COUNT];
            F64 overfetch[LOD_COUNT];
        };
        // Vertices transformed with a 16-entry FIFO cache over all the
        // class's faces, and bytes fetched over the buffer's, per LOD. The
        // vertices, not the vertices a triangle: a triangle left out for
        // having no area takes no vertex, and must not count against the
        // order of the rest.
        const Bound bounds[] = {
            { "box", { 24, 24, 24, 24 }, { 1.2000, 1.2000, 1.2000, 1.2000 } },
            { "cylinder", { 30, 42, 66, 104 }, { 1.0133, 1.0286, 1.0182, 1.0353 } },
            { "sphere", { 26, 48, 126, 347 }, { 1.0286, 1.0240, 1.0000, 1.0043 } },
            { "torus", { 49, 108, 286, 697 }, { 1.0122, 1.0080, 1.0000, 1.0010 } },
            { "sculpt sphere", { 47, 85, 317, 1242 }, { 1.0122, 1.0074, 1.0021, 1.0006 } },
            { "sculpt plane", { 49, 88, 321, 1249 }, { 1.0122, 1.0074, 1.0021, 1.0006 } },
        };
        const Shape shapes[] = { box(), cylinder(), sphere(), torus() };
        for (const Bound& bound : bounds)
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                Figures figures;
                const std::string name = bound.name;
                if (name == "sculpt sphere")
                {
                    figures.add(*buildSculpt(LL_SCULPT_TYPE_SPHERE, sphereMap(64, 64), lod));
                }
                else if (name == "sculpt plane")
                {
                    figures.add(*buildSculpt(LL_SCULPT_TYPE_PLANE, planeMap(64, 64), lod));
                }
                else
                {
                    const Shape* s = std::find_if(std::begin(shapes), std::end(shapes), [&](const Shape& x) { return x.name == name; });
                    figures.add(*build(*s, lod));
                }
                INFO(fmt::format("{} at LOD {}: {:.0f} transformed for {:.0f} triangles, overfetch {:.4f}", name, lod, figures.transformed,
                                 figures.triangles, figures.overfetch()));
                CHECK(figures.transformed <= bound.transformed[lod]);
                CHECK(figures.overfetch() <= bound.overfetch[lod] + 1e-4);
            }
        }

        struct MeshBound
        {
            const char* name;
            F64 acmr;
            F64 overfetch;
        };
        const MeshBound mesh_bounds[] = {
            { "UV sphere 24 by 12", 0.6951, 1.0006 },
            { "cube with split normals", 2.0000, 1.0000 },
            { "strip with mirrored UVs", 0.7361, 1.0122 },
            { "rigged UV sphere", 0.7366, 1.0066 },
            // Its degenerate triangles' tangents group as the compiler that
            // built meshoptimizer rounds its tests of them: MSVC's build gives
            // 0.7763 and 1.0182, GCC's and Clang's on ARM 0.8158 and 1.0207.
            { "grid with degenerate triangles", 0.8158, 1.0207 },
            { "grid of vertices held twice", 0.7361, 1.0122 },
            { "UV sphere 64 by 32", 0.6557, 1.0005 },
            { "UV sphere 128 by 64", 0.6425, 1.0001 },
        };
        std::vector<CorpusMesh> meshes = corpus();
        meshes.push_back(uvSphere(128, 64));
        for (const CorpusMesh& mesh : meshes)
        {
            LLVolumeFace face = mesh.face();
            REQUIRE(face.cacheOptimize(true));
            Figures figures;
            figures.add(face);
            const MeshBound* bound = std::find_if(std::begin(mesh_bounds), std::end(mesh_bounds), [&](const MeshBound& b) { return mesh.name == b.name; });
            REQUIRE(bound != std::end(mesh_bounds));
            INFO(fmt::format("{}: {:.4f} transformed a triangle, overfetch {:.4f}", mesh.name, figures.acmr(), figures.overfetch()));
            CHECK(figures.acmr() <= bound->acmr + 1e-4);
            CHECK(figures.overfetch() <= bound->overfetch + 1e-4);
        }
    }

    // createSide builds its index order where meshoptimizer would compute
    // one: every side of the sweep, of the flexible prims and of the
    // sculpts transforms at most five percent more vertices a triangle than
    // meshoptimizer's cache order of the same triangles.
    TEST_CASE("llvolume: every side's index order is within five percent of meshoptimizer's", "[llvolume]")
    {
        Report report;
        S32 sides = 0;
        F64 worst = 0;
        std::string worst_at;
        const auto check = [&](const LLVolume& volume, const std::string& name)
        {
            for (S32 i = 0; i < volume.getNumVolumeFaces(); ++i)
            {
                const LLVolumeFace& face = volume.getVolumeFace(i);
                if (isCap(face) || face.mNumIndices == 0)
                {
                    continue;
                }
                const std::vector<U32> own(face.mIndices, face.mIndices + face.mNumIndices);
                std::vector<U32> optimized(own.size());
                meshopt_optimizeVertexCache(optimized.data(), own.data(), own.size(), face.mNumVertices);
                const F64 ours = meshopt_analyzeVertexCache(own.data(), own.size(), face.mNumVertices, 16, 0, 0).acmr;
                const F64 theirs = meshopt_analyzeVertexCache(optimized.data(), optimized.size(), face.mNumVertices, 16, 0, 0).acmr;
                ++sides;
                if (ours / theirs > worst)
                {
                    worst = ours / theirs;
                    worst_at = fmt::format("{}: {}, {} by {}", name, faceName(face), face.mNumS, face.mNumT);
                }
                if (ours > theirs * 1.05 + 1e-6)
                {
                    report.fail(fmt::format("{}: {}, {} by {}: {:.3f} vertices a triangle against meshoptimizer's {:.3f}", name, faceName(face), face.mNumS,
                                            face.mNumT, ours, theirs));
                }
            }
        };
        for (const Shape& s : sweepShapes())
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                check(*build(s, lod), lodName(s, lod));
            }
        }
        for (const Shape& base : sweepShapes())
        {
            if (base.path != LL_PCODE_PATH_LINE || (base.name.find(", plain") == std::string::npos && base.name.find(", profile cut") == std::string::npos))
            {
                continue;
            }
            for (S32 sections : { 4, 16 })
            {
                for (S32 lod = 0; lod < LOD_COUNT; ++lod)
                {
                    LLPointer<LLVolume> volume = flexiVolume(base, lod);
                    bendPath(*volume, sections, 1.1f);
                    volume->regen();
                    check(*volume, fmt::format("flexible {}, {} sections at LOD {}", base.name, sections, lod));
                }
            }
        }
        const std::array<U16, 2> sizes[] = { { 64, 64 }, { 128, 32 }, { 32, 128 }, { 64, 16 } };
        for (U8 stitching : STITCHINGS)
        {
            for (const auto& size : sizes)
            {
                for (S32 lod = 0; lod < LOD_COUNT; ++lod)
                {
                    check(*buildSculpt(stitching, mapFor(stitching, size[0], size[1]), lod),
                          fmt::format("{} sculpt from a {} by {} map at LOD {}", stitchingName(stitching), size[0], size[1], lod));
                }
            }
        }
        INFO(fmt::format("{} sides; the most against meshoptimizer {:.3f}, {}", sides, worst, worst_at));
        INFO(report.text());
        CHECK(sides > 0);
        CHECK(report.failures == 0);
    }

    TEST_CASE("llvolume: resizePath builds the flexible volume again in full", "[llvolume]")
    {
        for (const auto& [profile, hole, hollow, name] : { std::tuple{ LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_SAME, 0.f, "box" },
                                                          std::tuple{ LL_PCODE_PROFILE_CIRCLE, LL_PCODE_HOLE_SAME, 0.f, "cylinder" },
                                                          std::tuple{ LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_SQUARE, 0.5f, "hollow box" } })
        {
            LLPointer<LLVolume> volume = flexiVolume(profile, hole, hollow, 2);
            bendPath(*volume, 4, 0.4f);
            volume->regen();
            genAllTangents(*volume);
            bendPath(*volume, 16, 1.1f);
            volume->regen();
            genAllTangents(*volume);

            LLPointer<LLVolume> fresh = flexiVolume(profile, hole, hollow, 2);
            bendPath(*fresh, 16, 1.1f);
            fresh->regen();
            genAllTangents(*fresh);

            Report report;
            compareBuilds(*volume, *fresh, report, fmt::format("flexible {} from 4 sections to 16", name));
            INFO(report.text());
            CHECK(report.failures == 0);
        }
    }

    // meshoptimizer's remap table gives each vertex's new index at its old
    // one, and numbers the welded vertices in the order the indices first
    // use them. A weld gives every corner back the vertex it had, whatever
    // that order is, with copies made one and unused vertices gone.
    TEST_CASE("llvolume: a weld keeps every corner's vertex when first use is out of vertex order", "[llvolume]")
    {
        // Position, normal and texture coordinate each differ from those of
        // a vertex made from another n.
        const auto vertex = [](F32 n)
        {
            LLVolumeFace::VertexData v;
            v.setPosition(LLVector4a(n, n + 0.25f, n + 0.5f));
            v.setNormal(LLVector4a(-n, 1.f, n * 2.f));
            v.mTexCoord = LLVector2(n * 0.125f, 1.f - n * 0.125f);
            return v;
        };
        // 5 is a copy of 0, and no index uses 6.
        const std::vector<LLVolumeFace::VertexData> vertices = { vertex(1.f), vertex(2.f), vertex(3.f), vertex(4.f),
                                                                 vertex(5.f), vertex(1.f), vertex(6.f) };
        const std::vector<U32> indices = { 3, 5, 1, 0, 2, 4 };

        std::vector<LLVolumeFace::VertexData> welded(vertices.size());
        std::vector<U32> welded_indices(indices.size());
        const size_t count = LLVolumeFace::weldVertices(welded.data(), welded_indices.data(), vertices.data(), vertices.size(),
                                                        indices.data(), indices.size());

        CHECK(count == 5);
        CHECK(welded_indices[1] == welded_indices[3]);
        for (size_t i = 0; i < indices.size(); ++i)
        {
            INFO(fmt::format("corner {}, vertex {}, welded {}", i, indices[i], welded_indices[i]));
            REQUIRE(welded_indices[i] < count);
            CHECK(welded[welded_indices[i]] == vertices[indices[i]]);
        }
    }

    // A vertex whose position, normal and texture coordinate each differ from
    // those of a vertex made from another n.
    LLVolumeFace::VertexData distinct_vertex(F32 n)
    {
        LLVolumeFace::VertexData v;
        v.setPosition(LLVector4a(n, n + 0.25f, n + 0.5f));
        v.setNormal(LLVector4a(-n, 1.f, n * 2.f));
        v.mTexCoord = LLVector2(n * 0.125f, 1.f - n * 0.125f);
        return v;
    }

    // A face of these vertices and indices, as an importer hands one to remap.
    LLVolumeFace face_of(std::vector<LLVolumeFace::VertexData> vertices, std::vector<U16> indices)
    {
        LLVolumeFace face;
        face.fillFromLegacyData(vertices, indices);
        return face;
    }

    LLVolumeFace::VertexData vertex_at(const LLVolumeFace& face, U16 index)
    {
        LLVolumeFace::VertexData v;
        v.setPosition(face.mPositions[index]);
        v.setNormal(face.mNormals[index]);
        v.mTexCoord = face.mTexCoords[index];
        return v;
    }

    // remap welds an uploaded face as weldVertices does: copies made one,
    // unused vertices gone, and every corner given back the vertex it had.
    TEST_CASE("llvolume: remap welds copies and drops unused vertices with every corner keeping its vertex", "[llvolume]")
    {
        // 5 is a copy of 0, and no index uses 6.
        const std::vector<LLVolumeFace::VertexData> vertices = { distinct_vertex(1.f), distinct_vertex(2.f), distinct_vertex(3.f),
                                                                 distinct_vertex(4.f), distinct_vertex(5.f), distinct_vertex(1.f),
                                                                 distinct_vertex(6.f) };
        const std::vector<U16> indices = { 3, 5, 1, 0, 2, 4 };

        LLVolumeFace face = face_of(vertices, indices);
        face.remap();

        REQUIRE(face.mNumIndices == (S32)indices.size());
        CHECK(face.mNumVertices == 5);
        CHECK(face.mIndices[1] == face.mIndices[3]);
        for (size_t i = 0; i < indices.size(); ++i)
        {
            INFO(fmt::format("corner {}, vertex {}, welded {}", i, indices[i], face.mIndices[i]));
            REQUIRE(face.mIndices[i] < face.mNumVertices);
            CHECK(vertex_at(face, face.mIndices[i]) == vertices[indices[i]]);
        }
    }

    // An uploaded face's indices are untrusted. remap takes one past the
    // face's vertices, however far past, as vertex 0, and reads its weld's
    // table, one entry a vertex, only through indices it has checked.
    TEST_CASE("llvolume: remap takes an index past the face's vertices as vertex 0", "[llvolume]")
    {
        const std::vector<LLVolumeFace::VertexData> vertices = { distinct_vertex(1.f), distinct_vertex(2.f), distinct_vertex(3.f),
                                                                 distinct_vertex(4.f) };
        // Vertex 0 is not the first the indices use, so the weld does not
        // number it 0.
        const std::vector<U16> indices = { 1, 2, 3, 1, 3, 5, 2, 3, 65535 };

        LLVolumeFace face = face_of(vertices, indices);
        face.remap();

        REQUIRE(face.mNumIndices == (S32)indices.size());
        CHECK(face.mNumVertices == 4);
        for (size_t i = 0; i < indices.size(); ++i)
        {
            const size_t vertex = indices[i] < vertices.size() ? size_t(indices[i]) : size_t(0);
            INFO(fmt::format("corner {}, index {}, welded {}", i, indices[i], face.mIndices[i]));
            REQUIRE(face.mIndices[i] < face.mNumVertices);
            CHECK(vertex_at(face, face.mIndices[i]) == vertices[vertex]);
        }
    }

    // A flat parallelogram as authored, P = a e1 + b e2 for a and b in
    // [-0.5, 0.5], whose box is 4 by 1 by 1. Its UVs are turned 45 degrees
    // against its edges, u = (a + b) / 2 + 0.5 and v = (b - a) / 2 + 0.5, so u
    // runs along e1 + e2, across the box's long side and its short ones at
    // once. The face holds it as a decoded mesh does: positions divided by
    // the box's size, which is mNormalizedScale, and normals taken through
    // the inverse transpose of that.
    //
    // A mesh rezzed at its authored size has the box's size as its scale, so
    // the face's vertex transform is diag(mNormalizedScale) and its normal
    // transform the inverse of that. Through them the face's tangents must
    // be the authored surface's dP/du, at right angles to its normal, with
    // a handedness that makes n x t * w run the way v does.
    TEST_CASE("llvolume: a mesh face's tangents follow its UVs at the size it was authored", "[llvolume]")
    {
        const LLVector3 e1(4.f, 0.5f, 0.f);
        const LLVector3 e2(0.f, 0.5f, 1.f);
        const LLVector3 size(4.f, 1.f, 1.f);
        const LLVector3 inv_size(1.f / size.mV[VX], 1.f / size.mV[VY], 1.f / size.mV[VZ]);

        const LLVector3 dp_du = e1 + e2;
        const LLVector3 dp_dv = e2 - e1;
        LLVector3 authored_normal = e1 % e2;
        authored_normal.normalize();

        constexpr F32 corners[4][2] = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
        constexpr U16 indices[6] = { 0, 1, 2, 0, 2, 3 };

        LLVolumeFace face;
        face.resizeVertices(4);
        face.resizeIndices(6);
        REQUIRE(face.mNumVertices == 4);
        REQUIRE(face.mNumIndices == 6);
        face.mNormalizedScale = size;

        for (S32 i = 0; i < 4; ++i)
        {
            const F32 a = corners[i][0];
            const F32 b = corners[i][1];

            LLVector3 position = e1 * a + e2 * b;
            position.scaleVec(inv_size);
            face.mPositions[i].load3(position.mV);

            LLVector3 normal = authored_normal;
            normal.scaleVec(size);
            normal.normalize();
            face.mNormals[i].load3(normal.mV);

            face.mTexCoords[i].set((a + b) * 0.5f + 0.5f, (b - a) * 0.5f + 0.5f);
        }
        std::copy(std::begin(indices), std::end(indices), face.mIndices);

        REQUIRE(face.cacheOptimize(true));
        REQUIRE(face.mTangents != nullptr);
        REQUIRE(face.mNumVertices == 4);

        LLVector3 expected = dp_du;
        expected.normalize();

        for (S32 i = 0; i < face.mNumVertices; ++i)
        {
            LLVector3 tangent = v3(face.mTangents[i]);
            tangent.scaleVec(size);
            tangent.normalize();

            LLVector3 normal = v3(face.mNormals[i]);
            normal.scaleVec(inv_size);
            normal.normalize();

            const F32 w = face.mTangents[i][3];
            const LLVector3 bitangent = (normal % tangent) * w;

            // By the sine as well as the cosine: acos alone cannot tell
            // apart directions a float's rounding apart.
            const F32 degrees_from_du = atan2f((tangent % expected).length(), tangent * expected) * RAD_TO_DEG;
            const F32 degrees_off_surface = asinf(llclamp(tangent * normal, -1.f, 1.f)) * RAD_TO_DEG;

            INFO(fmt::format("vertex {}: tangent ({}, {}, {}), normal ({}, {}, {}), w {}", i, tangent.mV[VX], tangent.mV[VY], tangent.mV[VZ],
                             normal.mV[VX], normal.mV[VY], normal.mV[VZ], w));
            CHECK(degrees_from_du <= 0.01f);
            CHECK(fabsf(degrees_off_surface) <= 0.01f);
            CHECK(fabsf(w) == 1.f);
            CHECK(bitangent * dp_dv > 0.f);
        }
    }

    // The real mesh corpus (almeshcorpus.h), where AL_MESH_CORPUS_DIR names
    // one: every face of every LOD of the sample through cacheOptimize, with
    // meshoptimizer on the arena, keeps every triangle and corner and makes
    // no vertex twice, and nothing it is handed fails but what its guards
    // refuse, which are counted by what they refused.
    TEST_CASE("llvolume: cacheOptimize keeps every triangle and corner of the real mesh corpus", "[llvolume][corpus][.]")
    {
        std::optional<ALMeshCorpus> real = ALMeshCorpus::fromEnvironment();
        if (!real)
        {
            SKIP("AL_MESH_CORPUS_DIR names no mesh cache");
        }
        ArenaInstalled installed;
        Report report;
        size_t faces = 0, rigged = 0, past_vertices = 0, no_scale = 0, refused = 0, fatal = 0, over_arena = 0;
        real->forEachLOD(
            [&](const CorpusLOD& lod)
            {
                for (S32 f = 0; f < lod.volume->getNumVolumeFaces(); ++f)
                {
                    const LLVolumeFace& before = lod.volume->getVolumeFace(f);
                    if (before.mNumVertices <= 0 || before.mNumIndices <= 0)
                    {
                        continue;
                    }
                    ++faces;
                    rigged += before.mWeights != nullptr;
                    const std::string where = fmt::format("mesh {} LOD {} face {}", lod.mesh, lod.lod, f);
                    LLVolumeFace after = before;
                    ALScratchArena::resetCounters();
                    bool optimized = false;
                    try
                    {
                        optimized = after.cacheOptimize(true);
                    }
                    catch (const ll_test::FatalError& error)
                    {
                        ++fatal;
                        report.fail(fmt::format("{}: {}", where, error.what()));
                        continue;
                    }
                    over_arena += ALScratchArena::counters().heap_blocks > 0;
                    if (!optimized)
                    {
                        const bool past = std::any_of(before.mIndices, before.mIndices + before.mNumIndices,
                                                      [&](U16 index) { return index >= before.mNumVertices; });
                        const LLVector3& scale = before.mNormalizedScale;
                        const bool flat = scale.mV[0] == 0.f || scale.mV[1] == 0.f || scale.mV[2] == 0.f;
                        ++(past ? past_vertices : (flat ? no_scale : refused));
                        continue;
                    }
                    checkOptimized(before, after, report, where);
                    checkWeldedOnce(after, report, where);
                }
            });
        INFO(real->describe());
        INFO(fmt::format("{} faces, {} of them rigged; refused: {} for an index past their vertices, {} for a scale of no size, {} "
                         "otherwise; {} fatal; {} faces larger than the arena",
                         faces, rigged, past_vertices, no_scale, refused, fatal, over_arena));
        INFO(report.text());
        CHECK(faces > 0);
        CHECK(refused == 0);
        CHECK(report.failures == 0);
    }

    // ------------------------------------------------------------------
    // Defects, a case each: tagged [!shouldfail] while the generator has one

    // A cylinder side's quads are rectangles of one size, so a vertex has
    // as much area on either side of it, in every row: a weight that went to
    // some corners of a quad and not others, as the top right corner or the
    // corners of one triangle, would lean the normals around the axis, by
    // an amount that depends on the row.
    TEST_CASE("llvolume: a cylinder's side normals point straight out from its axis", "[llvolume]")
    {
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            LLPointer<LLVolume> volume = build(cylinder(), lod);
            const LLVolumeFace& face = volume->getVolumeFace(1);
            REQUIRE(isGrid(face));
            std::string rows;
            F32 worst = 0.f;
            for (S32 t = 0; t < face.mNumT; ++t)
            {
                F32 row_lean = 0.f;
                // The seam's columns have a defect of their own.
                for (S32 s = 1; s + 1 < face.mNumS; ++s)
                {
                    const S32 v = gridIndex(face, s, t);
                    const LLVector3 p = v3(face.mPositions[v]);
                    const LLVector3 n = v3(face.mNormals[v]);
                    const F32 lean = atan2f(p.mV[0] * n.mV[1] - p.mV[1] * n.mV[0], p.mV[0] * n.mV[0] + p.mV[1] * n.mV[1]) * RAD_TO_DEG;
                    row_lean = s == 1 ? lean : row_lean;
                    worst = llmax(worst, fabsf(lean));
                }
                rows += fmt::format(" {:+.1f}", row_lean);
            }
            INFO(fmt::format("LOD {}, {} sides by {} rows: each row leans{} degrees", lod, face.mNumS - 1, face.mNumT, rows));
            CHECK(worst < 0.5f);
        }
    }

    // A straight path's surface, in double, from the path's parameters as
    // LLPath::generate lays a line out: at t, a profile point p is scaled by
    // S(t), twisted about z by phi(t), Rz, and moved to pos(t), each a lerp
    // in t. Where q is the profile's tangent at p, its normal there is
    // (q S Rz) x (p S' Rz + phi' p S Kz Rz + pos'), Kz taking (x, y) to
    // (-y, x).
    struct LinePathSurface
    {
        F64 begin_scale[2];
        F64 end_scale[2];
        F64 twist_begin;
        F64 twist_end;
        F64 shear[2];

        explicit LinePathSurface(const LLPathParams& path)
        {
            const LLVector2 begin = path.getBeginScale();
            const LLVector2 end = path.getEndScale();
            for (S32 i = 0; i < 2; ++i)
            {
                begin_scale[i] = begin.mV[i];
                end_scale[i] = end.mV[i];
                shear[i] = path.getShear().mV[i];
            }
            twist_begin = F_PI * (F64)path.getTwistBegin();
            twist_end = F_PI * (F64)path.getTwist();
        }

        F64 scale(S32 axis, F64 t) const { return begin_scale[axis] + (end_scale[axis] - begin_scale[axis]) * t; }

        // (x, y) Rz(phi(t)): (1, 0) turned to (cos, sin).
        LLVector3d turned(F64 x, F64 y, F64 t) const
        {
            const F64 phi = twist_begin + (twist_end - twist_begin) * t;
            return LLVector3d(x * cos(phi) - y * sin(phi), x * sin(phi) + y * cos(phi), 0.0);
        }

        // The profile point a position at t was laid out from: moved back,
        // turned back and scaled back.
        LLVector3d pointOf(const LLVector4a& at, F64 t) const
        {
            const F64 phi = twist_begin + (twist_end - twist_begin) * t;
            const F64 x = at[0] - shear[0] * t;
            const F64 y = at[1] - shear[1] * t;
            return LLVector3d((x * cos(phi) + y * sin(phi)) / scale(0, t), (y * cos(phi) - x * sin(phi)) / scale(1, t), 0.0);
        }

        LLVector3d normal(const LLVector3d& p, const LLVector3d& q, F64 t) const
        {
            const F64 sx = scale(0, t);
            const F64 sy = scale(1, t);
            const LLVector3d across = turned(q.mdV[0] * sx, q.mdV[1] * sy, t);
            const LLVector3d widening = turned(p.mdV[0] * (end_scale[0] - begin_scale[0]), p.mdV[1] * (end_scale[1] - begin_scale[1]), t);
            const LLVector3d twisting = turned(-p.mdV[1] * sy, p.mdV[0] * sx, t) * (twist_end - twist_begin);
            const LLVector3d along = widening + twisting + LLVector3d(shear[0], shear[1], 1.0);
            return across % along;
        }
    };

    F64 degreesApart3d(const LLVector3d& a, const LLVector3d& b)
    {
        return atan2((a % b).length(), a * b) * RAD_TO_DEG;
    }

    // A straight path's sides take the normals of the surface they are cut
    // from at every vertex, their edges and corners included: the rows a
    // twist turns between, the corners of a twisted strip, a cut's column,
    // an apex. The surface is worked out again here, in double, from the
    // path's parameters, and each column's profile point from the side's
    // own positions: a flat side's tangent from the ends of the edge its
    // column is on, between a flat inner side's corner copies; a round
    // side's square to its point, but at a cut, where it is the profile's
    // own, at the cut's angle, which the case of the cut holds it to. A row
    // a scale takes to a line or a point is taken a step into the face, as
    // its limit.
    TEST_CASE("llvolume: a straight path's side normals are its swept surface's at its edges too", "[llvolume]")
    {
        std::vector<Shape> shapes;
        for (const Shape& s : sweepShapes())
        {
            if (s.path == LL_PCODE_PATH_LINE)
            {
                shapes.push_back(s);
            }
        }
        Shape cone = cylinder();
        cone.name = "cone";
        cone.scale_x = cone.scale_y = 0.f;
        Shape chisel = cylinder();
        chisel.name = "cylinder narrowed to a line";
        chisel.scale_x = 0.f;
        Shape pyramid = box();
        pyramid.name = "pyramid";
        pyramid.scale_x = pyramid.scale_y = 0.f;
        shapes.insert(shapes.end(), { cone, chisel, pyramid });
        for (Shape s : { box(), cylinder(), prism() })
        {
            s.twist_begin = -1.f;
            s.twist_end = 1.f;
            s.hole = LL_PCODE_HOLE_SQUARE;
            s.hollow = 0.6f;
            s.name = "hollow " + s.name + " twisted a whole turn";
            shapes.push_back(s);
        }

        Report report;
        S64 compared = 0;
        F64 worst = 0.0;
        for (const Shape& s : shapes)
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = build(s, lod);
                const LinePathSurface surface(volume->getParams().getPathParams());
                const LLPath& path = volume->getPath();
                const LLProfile& profile = volume->getProfile();
                const LLProfileParams& cut = volume->getParams().getProfileParams();
                const bool profile_cut = cut.getBegin() > 0.f || cut.getEnd() < 1.f;
                for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
                {
                    const LLVolumeFace& face = volume->getVolumeFace(f);
                    if (isCap(face) || !isGrid(face))
                    {
                        continue;
                    }
                    const std::string where = fmt::format("{}: {}", lodName(s, lod), faceName(face));

                    // The profile points, from the row the scale leaves widest.
                    S32 widest = 0;
                    auto width = [&](S32 t)
                    {
                        const F64 at = path.mPath[t].mTexT;
                        return std::min(fabs(surface.scale(0, at)), fabs(surface.scale(1, at)));
                    };
                    for (S32 t = 1; t < face.mNumT; ++t)
                    {
                        widest = width(t) > width(widest) ? t : widest;
                    }
                    if (width(widest) < 1e-3)
                    {
                        continue;
                    }
                    std::vector<LLVector3d> p(face.mNumS), q(face.mNumS);
                    for (S32 k = 0; k < face.mNumS; ++k)
                    {
                        p[k] = surface.pointOf(face.mPositions[gridIndex(face, k, widest)], path.mPath[widest].mTexT);
                    }
                    if (face.mTypeMask & LLVolumeFace::FLAT_MASK)
                    {
                        const std::vector<S32> strips = isDoubled(face) ? stripsOf(face) : std::vector<S32>(face.mNumS, 0);
                        for (S32 k = 0; k < face.mNumS; ++k)
                        {
                            S32 first = k;
                            S32 last = k;
                            while (first > 0 && strips[first - 1] == strips[k])
                            {
                                --first;
                            }
                            while (last + 1 < face.mNumS && strips[last + 1] == strips[k])
                            {
                                ++last;
                            }
                            q[k] = p[last] - p[first];
                        }
                    }
                    else
                    {
                        for (S32 k = 0; k < face.mNumS; ++k)
                        {
                            if (profile_cut && (k == 0 || k == face.mNumS - 1))
                            {
                                const LLVector4a tangent = profile.getTangent((face.mBeginS + k) % profile.getTotal());
                                q[k].setVec(tangent[0], tangent[1], 0.0);
                            }
                            else
                            {
                                q[k].setVec(-p[k].mdV[1], p[k].mdV[0], 0.0);
                            }
                            const LLVector3d run = p[llmin(k + 1, face.mNumS - 1)] - p[llmax(k - 1, 0)];
                            if (q[k] * run < 0.0)
                            {
                                q[k] = -q[k];
                            }
                        }
                    }
                    // A point on an axis has the rounding of its position
                    // for its other coordinate, which a row narrowed along
                    // the axis would take for the tangent's direction.
                    for (LLVector3d& tangent : q)
                    {
                        const F64 size = std::max(fabs(tangent.mdV[0]), fabs(tangent.mdV[1]));
                        for (S32 i = 0; i < 2; ++i)
                        {
                            tangent.mdV[i] = fabs(tangent.mdV[i]) <= 1e-6 * size ? 0.0 : tangent.mdV[i];
                        }
                    }

                    for (S32 t = 0; t < face.mNumT; ++t)
                    {
                        F64 at = path.mPath[t].mTexT;
                        const bool narrowed = std::min(fabs(surface.scale(0, at)), fabs(surface.scale(1, at))) < 1e-6;
                        if (narrowed)
                        {
                            at += t == 0 ? 1e-5 : -1e-5;
                        }
                        for (S32 k = 0; k < face.mNumS; ++k)
                        {
                            // An edge of no length, as where a hole meets its
                            // wall, has no direction to take.
                            const LLVector3d expected = surface.normal(p[k], q[k], at);
                            if (q[k].length() <= 1e-5 || expected.length() <= 1e-9 * q[k].length())
                            {
                                continue;
                            }
                            const LLVector4a& made = face.mNormals[gridIndex(face, k, t)];
                            const F64 off = degreesApart3d(LLVector3d(made[0], made[1], made[2]), expected);
                            const F64 bound = narrowed ? 0.05 : 0.02;
                            ++compared;
                            worst = std::max(worst, off);
                            if (!(off <= bound))
                            {
                                report.fail(fmt::format("{}: column {} row {} is {:.4f} degrees from its surface's normal", where, k, t, off));
                                break;
                            }
                        }
                    }
                }
            }
        }
        INFO(fmt::format("{} normals compared, the worst {:.4f} degrees off", compared, worst));
        INFO(report.text());
        CHECK(compared > 0);
        CHECK(report.failures == 0);
    }

    // Whether planar texgen projects a cut round side as one plane depends
    // on the normals at its cut, as far from the planar normal as any. Those
    // are the circle's at the cut's angle at every LOD, so a side spanning
    // more than 120 degrees is projected per vertex at every LOD, and one
    // within 120 as one plane. Summed from the triangles, they would be the
    // normal of the chord of the whole step the cut falls in, up to half a
    // step to either side of the cut and moving with the LOD's steps: a side
    // cut from a twentieth of a turn through 115 degrees would be projected
    // per vertex at LOD 0 to 2 and as one plane at LOD 3.
    TEST_CASE("llvolume: a cut round side's normals at its cut are the circle's and its planar projection is the same at every LOD", "[llvolume][planar]")
    {
        Report report;
        for (F32 begin : { 0.f, 0.05f, 0.3f })
        {
            for (F32 span : { 100.f, 115.f, 125.f, 140.f, 160.f, 175.f })
            {
                Shape s = cylinder();
                s.cut_begin = begin;
                s.cut_end = begin + span / 360.f;
                s.name = fmt::format("cylinder cut from {} through {} degrees", begin, span);
                std::optional<bool> one_plane;
                for (S32 lod = 0; lod < LOD_COUNT; ++lod)
                {
                    LLPointer<LLVolume> volume = build(s, lod);
                    const LLVolumeFace* side = nullptr;
                    for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
                    {
                        const LLVolumeFace& face = volume->getVolumeFace(f);
                        if ((face.mTypeMask & LLVolumeFace::OUTER_MASK) && isGrid(face))
                        {
                            side = &face;
                        }
                    }
                    REQUIRE(side);
                    const LLProfileParams& cut = volume->getParams().getProfileParams();
                    const F32 angles[] = { F_TWO_PI * cut.getBegin(), F_TWO_PI * cut.getEnd() };
                    for (S32 end = 0; end < 2; ++end)
                    {
                        const S32 column = end == 0 ? 0 : side->mNumS - 1;
                        const LLVector3 radial(cosf(angles[end]), sinf(angles[end]), 0.f);
                        for (S32 t = 0; t < side->mNumT; ++t)
                        {
                            const F32 off = degreesBetween(v3(side->mNormals[gridIndex(*side, column, t)]), radial);
                            if (!(off <= 0.01f))
                            {
                                report.fail(fmt::format("{} at LOD {}: the cut's column {} is {:.3f} degrees from the circle's normal at {:.2f} degrees",
                                                        s.name, lod, column, off, angles[end] * RAD_TO_DEG));
                                break;
                            }
                        }
                    }
                    const bool plane = side->getPlanarNormal()[3] > 0.f;
                    if (one_plane && *one_plane != plane)
                    {
                        report.fail(fmt::format("{}: one plane at LOD {} is {}, where it was {} below", s.name, lod, plane, *one_plane));
                    }
                    one_plane = plane;
                }
                if (one_plane && *one_plane != (span < 120.f))
                {
                    report.fail(fmt::format("{}: one plane is {} at every LOD", s.name, *one_plane));
                }
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A mirrored sculpt's rows run the other way, so a weight that went to
    // whichever corner is top right in the grid would not give it the mirror
    // image of the plain one's normals. The map is a cylinder's, whose quads
    // are flat even in eight bits: a mirrored sculpt's diagonals run the
    // other way across its surface, and where a quad is warped, as eight bits
    // warp a torus's, its two triangles are not the mirror image of the plain
    // one's, and neither are normals that follow them.
    TEST_CASE("llvolume: a mirrored sculpt's normals mirror the plain one's", "[llvolume]")
    {
        const SculptMap map = cylinderMap(64, 64);
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            LLPointer<LLVolume> plain = buildSculpt(LL_SCULPT_TYPE_CYLINDER, map, lod);
            LLPointer<LLVolume> mirrored = buildSculpt((U8)(LL_SCULPT_TYPE_CYLINDER | LL_SCULPT_FLAG_MIRROR), map, lod);
            const LLVolumeFace& a = plain->getVolumeFace(0);
            const LLVolumeFace& b = mirrored->getVolumeFace(0);
            REQUIRE(a.mNumS == b.mNumS);
            REQUIRE(a.mNumT == b.mNumT);
            F32 worst = 0.f;
            for (S32 t = 0; t < a.mNumT; ++t)
            {
                for (S32 s = 0; s < a.mNumS; ++s)
                {
                    LLVector3 expected = v3(a.mNormals[gridIndex(a, a.mNumS - 1 - s, t)]);
                    expected.mV[0] = -expected.mV[0];
                    worst = llmax(worst, degreesBetween(v3(b.mNormals[gridIndex(b, s, t)]), expected));
                }
            }
            INFO(fmt::format("LOD {}: the mirrored normals are up to {:.2f} degrees from the mirror image", lod, worst));
            CHECK(worst < 0.1f);
        }
    }

    // Where a smooth side creases sharply, as a flattened torus tube does at
    // LOD 0, weights that favour one side of the crease turn a normal past
    // the triangles on its light side. Equal weights cannot: a crease turns
    // by less than 180 degrees, and its normal by half that from each side.
    TEST_CASE("llvolume: a torus's normals face every triangle at a sharp crease", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::Inversion;
        how.inversion_any_spread = true;
        Report report;
        for (const Shape& s : { torus(), tube(), ring() })
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                inspectPrim(s, lod, how, report);
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A side's first and last columns are joined only where they are one
    // place. A wrap by the profile's closing alone would average each side
    // of a twisted box or tube with its own other edge, sum a half
    // cylinder's two opposite rims at LOD 2 and 3, and a skewed sphere's
    // two poles, and turn their normals over.
    TEST_CASE("llvolume: the S wrap joins only edges that meet", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::Oracle | Checks::Inversion;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        for (Shape s : { box(), tube(), prism() })
        {
            s.twist_begin = -1.f;
            s.twist_end = 1.f;
            s.name = "fully twisted " + s.name;
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                inspectPrim(s, lod, how, report);
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A path's openness ignores revolutions: at one and a half turns its
    // first and last rows are half a turn apart, and a wrap by openness would
    // sum their normals. The other way, a skewed circle2 path counts as open
    // though its rows meet, and would never be wrapped.
    TEST_CASE("llvolume: the T wrap follows whether the first and last rows meet", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::Oracle | Checks::Inversion | Checks::SeamsAgree | Checks::HardEdges;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        for (Shape s : { torus(), tube(), ring() })
        {
            s.revolutions = 1.5f;
            s.wire_skew = true;
            s.name += " at one and a half turns";
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                inspectPrim(s, lod, how, report);
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A cap with no area, as on a pyramid or a prism, has no triangle to
    // take a normal from, and still has a unit normal facing away from its
    // wall. Where a circle2 path's alternating stations, or a half circle
    // on a circle path tapered to a point, leave the cap within its plane,
    // that normal is close to square to the way the wall runs, but on its
    // far side.
    TEST_CASE("llvolume: a cap with no area still faces away from its wall", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::Finite | Checks::RawUnit | Checks::CapFacing | Checks::Tangents;
        how.only_regions = Regions::CapNoArea;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        Shape pyramid = box();
        pyramid.name = "pyramid";
        pyramid.scale_x = pyramid.scale_y = 0.f;
        Shape wedge = box();
        wedge.name = "prism";
        wedge.scale_x = 0.f;
        wedge.shear_x = -0.5f;
        Shape cone = cylinder();
        cone.name = "cone";
        cone.scale_x = cone.scale_y = 0.f;
        Shape horn = torus();
        horn.name = "torus tapered to a line at one and a quarter turns";
        horn.taper_x = 1.f;
        horn.revolutions = 1.25f;
        for (const Shape& s : { pyramid, wedge, cone, horn })
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                inspectPrim(s, lod, how, report);
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A tangent is a sum of shares each in the plane of its vertex's
    // normal, put back in that plane once summed; where nothing adds to it,
    // it is an axis put in that plane. Either way it is orthogonal to the
    // normal: at a cap with no area, at a vertex in no triangle with area,
    // and at the point where a twisted half circle's pole crosses the axis
    // it sweeps about.
    TEST_CASE("llvolume: every prim tangent is orthogonal to its normal", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::Orthogonal;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        inspectSculpts(how, report);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // Prims' and sculpts' tangents are meshoptimizer's default mode summed
    // a vertex at a time, which is its sum over the vertex's corners
    // wherever it gives them all one tangent.
    TEST_CASE("llvolume: prim and sculpt tangents agree with meshoptimizer's", "[llvolume]")
    {
        TangentAgreement agreement;
        Inspect how;
        how.checks = Checks::Meshopt;
        how.agreement = &agreement;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        inspectSculpts(how, report);
        INFO(fmt::format("{} vertices compared, {} whose corners meshoptimizer splits, {} it gives no tangent, {} whose tangent rounding "
                         "decides; at most {} degrees apart, at {}; at most {} of a vertex's bound, at {}",
                         agreement.compared, agreement.split, agreement.lone, agreement.unsure, agreement.widest, agreement.widest_at,
                         agreement.nearest_bound, agreement.nearest_bound_at));
        INFO(report.text());
        CHECK(report.failures == 0);
        CHECK(agreement.compared > 0);
    }

    // The flexible prims the partial-build cases build: the sweep's
    // line-path shapes, plain and profile cut, on paths of 4 and 16
    // sections, straight and bent, at every LOD, rebuilt in place once.
    template <class Each>
    void forEachFlexiFrame(Each each)
    {
        for (const Shape& base : sweepShapes())
        {
            if (base.path != LL_PCODE_PATH_LINE || (base.name.find(", plain") == std::string::npos && base.name.find(", profile cut") == std::string::npos))
            {
                continue;
            }
            for (S32 sections : { 4, 16 })
            {
                for (F32 bend : { 0.f, 1.1f })
                {
                    for (S32 lod = 0; lod < LOD_COUNT; ++lod)
                    {
                        LLPointer<LLVolume> volume = flexiVolume(base, lod);
                        bendPath(*volume, sections, 0.4f);
                        volume->regen();
                        genAllTangents(*volume);
                        bendPath(*volume, sections, bend);
                        volume->regen();
                        genAllTangents(*volume);
                        each(*volume, fmt::format("flexible {}, {} sections bent {} at LOD {}", base.name, sections, bend, lod), sections, bend);
                    }
                }
            }
        }
    }

    // A flexible prim's tangents are its cheap ones: unit length, finite,
    // orthogonal to their normals, and with the w each triangle's UVs wind
    // for, where the triangle has area in positions and in UVs.
    TEST_CASE("llvolume: a flexible prim's tangents are unit and orthogonal and wind with its UVs", "[llvolume]")
    {
        Report report;
        S32 faces = 0;
        forEachFlexiFrame([&](LLVolume& volume, const std::string& name, S32, F32)
        {
            for (S32 i = 0; i < volume.getNumVolumeFaces(); ++i)
            {
                const LLVolumeFace& face = volume.getVolumeFace(i);
                const std::string where = fmt::format("{}: {}", name, faceName(face));
                ++faces;
                checkTangents(face, Checks::Tangents | Checks::Orthogonal, report, where);
                for (S32 t = 0; t + 2 < face.mNumIndices; t += 3)
                {
                    const U16 a = face.mIndices[t], b = face.mIndices[t + 1], c = face.mIndices[t + 2];
                    // The generator takes points within 1e-5 of each other
                    // as one place: a strip that narrow, as a cut end where
                    // the hole meets its wall, winds as rounding has it.
                    auto apart = [&](U16 p, U16 q) { return (v3(face.mPositions[p]) - v3(face.mPositions[q])).lengthSquared() > 1e-10f; };
                    if (tangentFlat(face, a, b, c) || !apart(a, b) || !apart(b, c) || !apart(a, c))
                    {
                        continue;
                    }
                    const LLVector2 uv_ab = face.mTexCoords[b] - face.mTexCoords[a];
                    const LLVector2 uv_ac = face.mTexCoords[c] - face.mTexCoords[a];
                    const F32 uv_area = uv_ab.mV[0] * uv_ac.mV[1] - uv_ab.mV[1] * uv_ac.mV[0];
                    const LLVector3 geometric = (v3(face.mPositions[b]) - v3(face.mPositions[a])) % (v3(face.mPositions[c]) - v3(face.mPositions[a]));
                    bool failed = false;
                    for (U16 v : { a, b, c })
                    {
                        const F32 facing = geometric * v3(face.mNormals[v]);
                        if (fabsf(facing) <= 1e-3f * geometric.length())
                        {
                            continue;
                        }
                        const F32 expected = (uv_area > 0.f) == (facing > 0.f) ? 1.f : -1.f;
                        if (face.mTangents[v][3] != expected)
                        {
                            report.fail(fmt::format("{}: triangle {}'s UVs wind for w = {} at vertex {}, which has {}", where, t / 3, expected, v,
                                                    face.mTangents[v][3]));
                            failed = true;
                            break;
                        }
                    }
                    if (failed)
                    {
                        break;
                    }
                }
            }
        });
        INFO(fmt::format("{} faces", faces));
        INFO(report.text());
        CHECK(faces > 0);
        CHECK(report.failures == 0);
    }

    // The cheap tangents stay near the meshoptimizer-weighted ones every
    // other prim takes, on the same geometry, away from the points where
    // either has nothing to go by: a vertex in a triangle with no area or
    // no UV area, and one whose weighted tangent rounding decides. A cap's
    // and a straight side's are the weighted ones to within rounding. On a
    // bent path a side's row turns from one station to the next, and the
    // weighted tangent takes in the edges of the rows beside a vertex's
    // own, where the cheap one follows its own row: they are as far apart
    // as the path turns over a section, at most.
    TEST_CASE("llvolume: a flexible prim's tangents stay near the weighted ones", "[llvolume]")
    {
        Report report;
        F64 widest = 0.0;
        std::string widest_at;
        S32 compared = 0;
        forEachFlexiFrame([&](LLVolume& volume, const std::string& name, S32 sections, F32 bend)
        {
            const F64 bound = llmax(0.01, fabs((F64)bend) / sections * 180.0 / F_PI);
            for (S32 i = 0; i < volume.getNumVolumeFaces(); ++i)
            {
                const LLVolumeFace& face = volume.getVolumeFace(i);
                const std::string where = fmt::format("{}: {}", name, faceName(face));
                std::vector<LLVector4a> weighted(face.mNumVertices);
                const bool grid = !(face.mTypeMask & LLVolumeFace::CAP_MASK) && face.mNumS * face.mNumT == face.mNumVertices;
                if (grid)
                {
                    alsimd::grid_tangents(face.mPositions, face.mNormals, face.mTexCoords, face.mNumS, face.mNumT, weighted.data());
                }
                else
                {
                    alsimd::indexed_tangents(face.mPositions, face.mNormals, face.mTexCoords, face.mNumVertices, face.mIndices, face.mNumIndices,
                                             weighted.data());
                }
                std::vector<bool> skip(face.mNumVertices, false);
                for (S32 t = 0; t + 2 < face.mNumIndices; t += 3)
                {
                    if (tangentFlat(face, face.mIndices[t], face.mIndices[t + 1], face.mIndices[t + 2]))
                    {
                        skip[face.mIndices[t]] = skip[face.mIndices[t + 1]] = skip[face.mIndices[t + 2]] = true;
                    }
                }
                const std::vector<F64> rounding = tangentRoundingDegrees(face);
                for (S32 v = 0; v < face.mNumVertices; ++v)
                {
                    if (skip[v] || rounding[v] >= 1.0)
                    {
                        continue;
                    }
                    ++compared;
                    const F64 apart = degreesApart(v3(face.mTangents[v]), v3(weighted[v]));
                    if (apart > widest)
                    {
                        widest = apart;
                        widest_at = fmt::format("{}: vertex {}", where, v);
                    }
                    if (!(apart <= bound) || face.mTangents[v][3] != weighted[v][3])
                    {
                        report.fail(fmt::format("{}: vertex {} is {} degrees from the weighted tangent, past {}, w {} against {}", where, v, apart,
                                                bound, face.mTangents[v][3], weighted[v][3]));
                        break;
                    }
                }
            }
        });
        INFO(fmt::format("{} vertices compared; at most {} degrees apart, at {}", compared, widest, widest_at));
        INFO(report.text());
        CHECK(compared > 0);
        CHECK(report.failures == 0);
    }

    // The grid form takes every quad the index buffer would, at widths
    // whose lines a division by a rounded reciprocal of the width would
    // count a lane short at their first quad: a rippled grid, its UVs
    // rippled too, so that each quad's tangent differs from its
    // neighbours' and a quad left out moves its corners' sums. The two
    // add the same shares in different orders, and agree to within that.
    TEST_CASE("llvolume: a grid's tangents take every quad at widths a reciprocal rounds", "[llvolume]")
    {
        Report report;
        for (size_t columns : { 41, 47, 55, 61, 97, 107 })
        {
            const size_t rows = 6;
            const size_t count = columns * rows;
            std::vector<LLVector4a> positions(count), normals(count);
            std::vector<LLVector2> uvs(count);
            for (size_t t = 0; t < rows; ++t)
            {
                for (size_t s = 0; s < columns; ++s)
                {
                    const F32 x = 0.1f * (F32)s;
                    const F32 y = 0.1f * (F32)t;
                    const F32 phase = 0.37f * (F32)s + 0.21f * (F32)t;
                    const size_t v = s + columns * t;
                    positions[v].set(x, y, 0.05f * sinf(phase), 0.f);
                    // The surface's normal: (-dz/dx, -dz/dy, 1), normalized.
                    normals[v].set(-0.05f * 3.7f * cosf(phase), -0.05f * 2.1f * cosf(phase), 1.f, 0.f);
                    normals[v].normalize3fast();
                    uvs[v].set((F32)s / (F32)(columns - 1) + 0.03f * sinf(0.5f * (F32)t + (F32)s),
                               (F32)t / (F32)(rows - 1) + 0.02f * cosf(0.7f * (F32)s));
                }
            }
            std::vector<U16> indices;
            for (size_t t = 0; t + 1 < rows; ++t)
            {
                for (size_t s = 0; s + 1 < columns; ++s)
                {
                    const U16 bl = (U16)(s + columns * t);
                    const U16 br = (U16)(bl + 1);
                    const U16 tl = (U16)(bl + columns);
                    const U16 tr = (U16)(tl + 1);
                    indices.insert(indices.end(), { bl, tr, tl, bl, br, tr });
                }
            }

            std::vector<LLVector4a> grid(count), indexed(count);
            alsimd::grid_tangents(positions.data(), normals.data(), uvs.data(), columns, rows, grid.data());
            alsimd::indexed_tangents(positions.data(), normals.data(), uvs.data(), count, indices.data(), indices.size(), indexed.data());
            for (size_t v = 0; v < count; ++v)
            {
                const F64 apart = degreesApart(v3(grid[v]), v3(indexed[v]));
                if (!(apart <= 1e-3) || grid[v][3] != indexed[v][3])
                {
                    report.fail(fmt::format("{} columns: vertex {} (column {}, row {}) is {} degrees from the indexed tangent, "
                                            "w {} against {}",
                                            columns, v, v % columns, v / columns, apart, grid[v][3], indexed[v][3]));
                    break;
                }
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A card drawn both ways, the one triangle twice sharing two vertices,
    // its third corner mirrored in U a hair off true, and turned off the
    // axes: at the shared vertices the two triangles' tangents all but
    // cancel, and what MikkTSpace's sum leaves of them is rounding, off the
    // normal's plane. The tangent cacheOptimize keeps is in the plane.
    TEST_CASE("llvolume: a mesh tangent whose triangles all but cancel lies in its normal's plane", "[llvolume]")
    {
        const LLQuaternion turn(0.7f, LLVector3(0.3f, -0.5f, 0.8f));
        for (F32 hair : { 1e-6f, 3e-6f, 1e-5f, 3e-5f, 1e-4f })
        {
            CorpusMesh mesh;
            for (const LLVector3& corner : { LLVector3(0.f, 0.f, 0.f), LLVector3(1.f, 0.f, 0.f), LLVector3(0.f, 1.f, 0.f), LLVector3(0.f, 0.f, 0.f) })
            {
                mesh.positions.push_back(corner * turn);
                mesh.normals.push_back(LLVector3(0.f, 0.f, 1.f) * turn);
            }
            mesh.uvs = { { 0.f, 0.f }, { 1.f, 0.f }, { 0.f, 1.f }, { 2.f, hair } };
            mesh.indices = { 0, 1, 2, 2, 1, 3 };
            const LLVolumeFace before = mesh.face();
            LLVolumeFace after = before;
            REQUIRE(after.cacheOptimize(true));
            Report report;
            checkOptimized(before, after, report, fmt::format("card a hair of {} off its mirror", hair));
            INFO(report.text());
            CHECK(report.failures == 0);
        }
    }

    // A mesh vertex no triangle gives a tangent direction takes an axis
    // put in the plane of its normal: unit length and orthogonal to it,
    // whichever way the normal points.
    TEST_CASE("llvolume: a mesh tangent with no direction lies in the plane of its normal", "[llvolume]")
    {
        // Two triangles whose UVs are one point, so that neither gives a
        // tangent, with a normal along each axis and between them.
        const LLVector3 normals[] = { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0.6f, 0.f, 0.8f }, { 0.8f, -0.6f, 0.f } };
        for (const LLVector3& normal : normals)
        {
            CorpusMesh mesh;
            mesh.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 } };
            mesh.normals.assign(4, normal);
            mesh.uvs.assign(4, LLVector2(0.5f, 0.5f));
            mesh.indices = { 0, 1, 2, 2, 1, 3 };
            LLVolumeFace face = mesh.face();
            REQUIRE(face.cacheOptimize(true));
            REQUIRE(face.mTangents);
            for (S32 v = 0; v < face.mNumVertices; ++v)
            {
                const LLVector3 t = v3(face.mTangents[v]);
                INFO(fmt::format("normal ({}, {}, {}): vertex {} has the tangent ({}, {}, {}, {})", normal.mV[0], normal.mV[1], normal.mV[2], v,
                                 t.mV[0], t.mV[1], t.mV[2], face.mTangents[v][3]));
                CHECK(fabsf(t.length() - 1.f) <= UNIT_TOLERANCE);
                CHECK(fabsf(t * normal) <= 2e-3f);
                CHECK((face.mTangents[v][3] == 1.f || face.mTangents[v][3] == -1.f));
            }
        }
    }

    // A mesh face's vertex count is its positions'. A normal or texture
    // coordinate array that falls short of it is taken as none, its zeroes
    // in their place, rather than read past its end.
    TEST_CASE("llvolume: a mesh face whose normals or texture coordinates fall short of its positions decodes without them", "[llvolume]")
    {
        auto binary = [](std::initializer_list<U16> values)
        {
            LLSD::Binary bytes;
            for (U16 value : values)
            {
                bytes.push_back((U8)(value & 0xff));
                bytes.push_back((U8)(value >> 8));
            }
            return LLSD(bytes);
        };
        LLSD face;
        face["Position"] = binary({ 0, 0, 0, 65535, 0, 0, 0, 65535, 0 });
        face["Normal"] = binary({ 32768, 32768, 65535 });
        face["TexCoord0"] = binary({ 65535, 65535 });
        face["TriangleList"] = binary({ 0, 1, 2 });
        face["PositionDomain"]["Min"] = LLVector3(-0.5f, -0.5f, -0.5f).getValue();
        face["PositionDomain"]["Max"] = LLVector3(0.5f, 0.5f, 0.5f).getValue();
        face["TexCoord0Domain"]["Min"] = LLVector2(0.f, 0.f).getValue();
        face["TexCoord0Domain"]["Max"] = LLVector2(1.f, 1.f).getValue();
        LLSD mdl = LLSD::emptyArray();
        mdl.append(face);

        LLVolumeParams params;
        params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
        params.setSculptID(LLUUID("00000000-0000-0000-0000-00000000000a"), LL_SCULPT_TYPE_MESH);
        LLPointer<LLVolume> volume = new LLVolume(params, LLVolumeLODGroup::getVolumeScaleFromDetail(3));
        REQUIRE(ll_test::VolumeProbe::decode(*volume, mdl));
        REQUIRE(volume->getNumVolumeFaces() == 1);
        const LLVolumeFace& decoded = volume->getVolumeFace(0);
        REQUIRE(decoded.mNumVertices == 3);
        for (S32 v = 0; v < decoded.mNumVertices; ++v)
        {
            CAPTURE(v);
            CHECK(decoded.mNormals[v].equals3(LLVector4a::getZero()));
            CHECK(decoded.mTexCoords[v] == LLVector2(0.f, 0.f));
        }
    }

    // A rigged face's weights are read vertex by vertex until they run out;
    // a vertex they run out before takes the weight of one with no
    // influence, all of it on the first joint, rather than whatever its
    // allocation held.
    TEST_CASE("llvolume: a mesh face whose weights run out before its vertices weighs the rest by no influence", "[llvolume]")
    {
        auto binary = [](std::initializer_list<U16> values)
        {
            LLSD::Binary bytes;
            for (U16 value : values)
            {
                bytes.push_back((U8)(value & 0xff));
                bytes.push_back((U8)(value >> 8));
            }
            return LLSD(bytes);
        };
        LLSD face;
        face["Position"] = binary({ 0, 0, 0, 65535, 0, 0, 0, 65535, 0 });
        face["TriangleList"] = binary({ 0, 1, 2 });
        face["PositionDomain"]["Min"] = LLVector3(-0.5f, -0.5f, -0.5f).getValue();
        face["PositionDomain"]["Max"] = LLVector3(0.5f, 0.5f, 0.5f).getValue();
        face["TexCoord0Domain"]["Min"] = LLVector2(0.f, 0.f).getValue();
        face["TexCoord0Domain"]["Max"] = LLVector2(1.f, 1.f).getValue();
        // The first vertex's influences, joint 3 at half, and no more.
        face["Weights"] = LLSD(LLSD::Binary{ 3, 0xff, 0x7f, 0xff });
        LLSD mdl = LLSD::emptyArray();
        mdl.append(face);

        LLVolumeParams params;
        params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
        params.setSculptID(LLUUID("00000000-0000-0000-0000-00000000000b"), LL_SCULPT_TYPE_MESH);
        LLPointer<LLVolume> volume = new LLVolume(params, LLVolumeLODGroup::getVolumeScaleFromDetail(3));
        REQUIRE(ll_test::VolumeProbe::decode(*volume, mdl));
        REQUIRE(volume->getNumVolumeFaces() == 1);
        const LLVolumeFace& decoded = volume->getVolumeFace(0);
        REQUIRE(decoded.mNumVertices == 3);
        REQUIRE(decoded.mWeights);
        CHECK((S32)decoded.mWeights[0][0] == 3);
        for (S32 v = 1; v < decoded.mNumVertices; ++v)
        {
            CAPTURE(v);
            const LLVector4a& weight = decoded.mWeights[v];
            CHECK(weight[0] == 0.999f);
            CHECK(weight[1] == 0.f);
            CHECK(weight[2] == 0.f);
            CHECK(weight[3] == 0.f);
        }
    }

    // One triangle of a mesh face as the wire carries it, its vertices on
    // the unit cube's axes and each value sixteen bits, low byte first.
    LLSD wireTriangle()
    {
        auto binary = [](std::initializer_list<U16> values)
        {
            LLSD::Binary bytes;
            for (U16 value : values)
            {
                bytes.push_back((U8)(value & 0xff));
                bytes.push_back((U8)(value >> 8));
            }
            return LLSD(bytes);
        };
        LLSD face;
        face["Position"] = binary({ 0, 0, 0, 65535, 0, 0, 0, 65535, 0 });
        face["TriangleList"] = binary({ 0, 1, 2 });
        face["PositionDomain"]["Min"] = LLVector3(-0.5f, -0.5f, -0.5f).getValue();
        face["PositionDomain"]["Max"] = LLVector3(0.5f, 0.5f, 0.5f).getValue();
        face["TexCoord0Domain"]["Min"] = LLVector2(0.f, 0.f).getValue();
        face["TexCoord0Domain"]["Max"] = LLVector2(1.f, 1.f).getValue();
        return face;
    }

    // A LOD of the faces, decoded into a mesh volume of its own.
    LLPointer<LLVolume> decodeFaces(const LLSD& mdl, bool& decoded)
    {
        LLVolumeParams params;
        params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
        params.setSculptID(LLUUID("00000000-0000-0000-0000-00000000000c"), LL_SCULPT_TYPE_MESH);
        LLPointer<LLVolume> volume = new LLVolume(params, LLVolumeLODGroup::getVolumeScaleFromDetail(3));
        decoded = ll_test::VolumeProbe::decode(*volume, mdl);
        return volume;
    }

    // A LOD of the one face, decoded into a mesh volume of its own.
    LLPointer<LLVolume> decodeLOD(const LLSD& face, bool& decoded)
    {
        LLSD mdl = LLSD::emptyArray();
        mdl.append(face);
        return decodeFaces(mdl, decoded);
    }

    // A face's TriangleList of zeros deflates to almost nothing, so that one
    // the unzip cap lets through could hold a hundred million indices, and
    // cost every viewer that decodes it seconds and gigabytes. A LOD with a
    // face of more indices than LL_SCULPT_MESH_MAX_FACE_INDICES does not
    // decode; one with a face of as many as that does.
    TEST_CASE("llvolume: a mesh face of more indices than a face may hold does not decode", "[llvolume]")
    {
        const S32 most = LL_SCULPT_MESH_MAX_FACE_INDICES - LL_SCULPT_MESH_MAX_FACE_INDICES % 3;
        for (const S32 indices : { most, most + 3 })
        {
            CAPTURE(indices);
            LLSD face = wireTriangle();
            face["TriangleList"] = LLSD(LLSD::Binary((size_t)indices * sizeof(U16), 0));
            bool decoded = false;
            LLPointer<LLVolume> volume = decodeLOD(face, decoded);
            CHECK(decoded == (indices <= LL_SCULPT_MESH_MAX_FACE_INDICES));
            if (decoded)
            {
                REQUIRE(volume->getNumVolumeFaces() == 1);
                CHECK(volume->getVolumeFace(0).mNumIndices == indices);
            }
            else
            {
                CHECK(volume->getNumVolumeFaces() == 0);
            }
        }
    }

    // Faces each within LL_SCULPT_MESH_MAX_FACE_INDICES could still carry,
    // between them, all the indices the unzip cap lets through. A LOD whose
    // faces hold more than LL_SCULPT_MESH_MAX_LOD_INDICES between them does
    // not decode; one whose hold as many as that does.
    TEST_CASE("llvolume: a mesh LOD of more indices than a LOD may hold does not decode", "[llvolume]")
    {
        const S32 most = LL_SCULPT_MESH_MAX_FACE_INDICES - LL_SCULPT_MESH_MAX_FACE_INDICES % 3;
        REQUIRE(2 * most <= LL_SCULPT_MESH_MAX_LOD_INDICES);
        REQUIRE(2 * most + 6 > LL_SCULPT_MESH_MAX_LOD_INDICES);
        for (const S32 last : { 0, 6 })
        {
            CAPTURE(last);
            LLSD mdl = LLSD::emptyArray();
            for (const S32 indices : { most, most, last })
            {
                if (indices > 0)
                {
                    LLSD face = wireTriangle();
                    face["TriangleList"] = LLSD(LLSD::Binary((size_t)indices * sizeof(U16), 0));
                    mdl.append(face);
                }
            }
            bool decoded = false;
            LLPointer<LLVolume> volume = decodeFaces(mdl, decoded);
            CHECK(decoded == (last == 0));
            CHECK(volume->getNumVolumeFaces() == (decoded ? (S32)mdl.size() : 0));
        }
    }

    // A face with no geometry costs nothing against the index budget, but
    // is made all the same, so that a LOD of many would cost what the index
    // budget does not count. A LOD of more than LL_SCULPT_MESH_MAX_FACES
    // faces does not decode; one of as many as that does.
    TEST_CASE("llvolume: a mesh LOD of more faces than a LOD may hold does not decode", "[llvolume]")
    {
        for (const S32 faces : { LL_SCULPT_MESH_MAX_FACES, LL_SCULPT_MESH_MAX_FACES + 1 })
        {
            CAPTURE(faces);
            LLSD mdl = LLSD::emptyArray();
            for (S32 f = 0; f < faces; ++f)
            {
                LLSD face;
                face["NoGeometry"] = true;
                mdl.append(face);
            }
            bool decoded = false;
            LLPointer<LLVolume> volume = decodeFaces(mdl, decoded);
            CHECK(decoded == (faces <= LL_SCULPT_MESH_MAX_FACES));
            CHECK(volume->getNumVolumeFaces() == (decoded ? faces : 0));
        }
    }

    // A rigged face's weights are an influence after another, each a joint
    // and two bytes of weight. An array that ends inside an influence leaves
    // its vertex the influences before it, and is not read past its end.
    TEST_CASE("llvolume: a mesh face whose weights end inside an influence keeps the influences before it", "[llvolume]")
    {
        struct Cut
        {
            const char* what;
            LLSD::Binary weights;
            S32 joint; // the first vertex's first joint, or -1 for none
        };
        const Cut cuts[] = {
            { "joint 5 at half, then joint 7 and one byte of its weight", { 5, 0xff, 0x7f, 7, 0xff }, 5 },
            { "joint 5 and one byte of its weight", { 5, 0xff }, -1 },
        };
        for (const Cut& cut : cuts)
        {
            CAPTURE(cut.what);
            LLSD face = wireTriangle();
            face["Weights"] = LLSD(cut.weights);
            bool decoded = false;
            LLPointer<LLVolume> volume = decodeLOD(face, decoded);
            REQUIRE(decoded);
            const LLVolumeFace& rigged = volume->getVolumeFace(0);
            REQUIRE(rigged.mNumVertices == 3);
            REQUIRE(rigged.mWeights);
            for (S32 v = 0; v < rigged.mNumVertices; ++v)
            {
                CAPTURE(v);
                const LLVector4a& weight = rigged.mWeights[v];
                if (v == 0 && cut.joint >= 0)
                {
                    CHECK((S32)weight[0] == cut.joint);
                }
                else
                {
                    // No influence: all of the weight on the first joint.
                    CHECK(weight[0] == 0.999f);
                }
                CHECK(weight[1] == 0.f);
                CHECK(weight[2] == 0.f);
                CHECK(weight[3] == 0.f);
            }
        }
    }

    // A face may come without normals or texture coordinates, as the DAE
    // loader leaves one whose source has none. Reordered without tangents,
    // which need both, it keeps every corner's position and comes out
    // without what it came without.
    TEST_CASE("llvolume: a face without normals or texture coordinates reorders without them", "[llvolume]")
    {
        struct Without
        {
            const char* what;
            bool normals;
            bool tex_coords;
        };
        const Without cases[] = {
            { "normals", true, false },
            { "texture coordinates", false, true },
            { "either", true, true },
        };
        const CorpusMesh mesh = uvSphere(16, 8);
        for (const Without& without : cases)
        {
            CAPTURE(without.what);
            // Each triangle as its corners' positions, and normals and
            // texture coordinates where the face keeps them, from its least
            // corner on; sorted, as the reordering leaves them in another
            // order.
            using Corner = std::array<F32, 8>;
            auto triangles = [&](const LLVolumeFace& f)
            {
                std::vector<std::array<Corner, 3>> out(f.mNumIndices / 3);
                for (size_t t = 0; t < out.size(); ++t)
                {
                    for (S32 k = 0; k < 3; ++k)
                    {
                        const U16 v = f.mIndices[t * 3 + k];
                        Corner& corner = out[t][k];
                        corner.fill(0.f);
                        std::memcpy(&corner[0], f.mPositions[v].getF32ptr(), 3 * sizeof(F32));
                        if (!without.normals)
                        {
                            std::memcpy(&corner[3], f.mNormals[v].getF32ptr(), 3 * sizeof(F32));
                        }
                        if (!without.tex_coords)
                        {
                            std::memcpy(&corner[6], f.mTexCoords[v].mV, 2 * sizeof(F32));
                        }
                    }
                    std::rotate(out[t].begin(), std::min_element(out[t].begin(), out[t].end()), out[t].end());
                }
                std::sort(out.begin(), out.end());
                return out;
            };

            LLVolumeFace face = mesh.face();
            const auto before = triangles(face);
            if (without.normals)
            {
                face.mNormals = nullptr;
            }
            if (without.tex_coords)
            {
                face.mTexCoords = nullptr;
            }
            REQUIRE(face.cacheOptimize(true));
            CHECK((face.mNormals != nullptr) == !without.normals);
            CHECK((face.mTexCoords != nullptr) == !without.tex_coords);
            CHECK(!face.mTangents);
            REQUIRE(face.mNumIndices == (S32)before.size() * 3);
            for (S32 i = 0; i < face.mNumIndices; ++i)
            {
                CAPTURE(i);
                REQUIRE(face.mIndices[i] < face.mNumVertices);
            }
            CHECK(triangles(face) == before);
        }
    }

    // A face may come without normals or texture coordinates, as the DAE
    // loader leaves one whose source has none. remap welds its vertices by
    // what it has, and it comes out without what it came without. Two
    // triangles share an edge whose two vertices are each held twice, the
    // copies apart in both normal and texture coordinate: they weld only
    // where the face has neither.
    TEST_CASE("llvolume: a face without normals or texture coordinates remaps without them", "[llvolume]")
    {
        struct Without
        {
            const char* what;
            bool normals;
            bool tex_coords;
            S32 vertices; // after the weld
        };
        const Without cases[] = {
            { "normals", true, false, 6 },
            { "texture coordinates", false, true, 6 },
            { "either", true, true, 4 },
        };
        CorpusMesh mesh;
        mesh.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 } };
        mesh.normals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0.6f, 0.8f }, { 0, 0.6f, 0.8f }, { 0, 0, 1 } };
        mesh.uvs = { { 0, 0 }, { 0.5f, 0 }, { 0, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 1 }, { 1, 1 } };
        mesh.indices = { 0, 1, 2, 4, 3, 5 };
        for (const Without& without : cases)
        {
            CAPTURE(without.what);
            LLVolumeFace face = mesh.face();
            if (without.normals)
            {
                face.mNormals = nullptr;
            }
            if (without.tex_coords)
            {
                face.mTexCoords = nullptr;
            }
            face.remap();
            CHECK((face.mNormals != nullptr) == !without.normals);
            CHECK((face.mTexCoords != nullptr) == !without.tex_coords);
            CHECK(face.mNumVertices == without.vertices);
            REQUIRE(face.mNumIndices == (S32)mesh.indices.size());
            for (S32 i = 0; i < face.mNumIndices; ++i)
            {
                CAPTURE(i);
                const U16 v = face.mIndices[i];
                const U16 was = mesh.indices[i];
                REQUIRE(v < face.mNumVertices);
                CHECK(v3(face.mPositions[v]) == mesh.positions[was]);
                if (!without.normals)
                {
                    CHECK(v3(face.mNormals[v]) == mesh.normals[was]);
                }
                if (!without.tex_coords)
                {
                    CHECK(face.mTexCoords[v] == mesh.uvs[was]);
                }
            }
        }
    }

    // A face copied from one without normals or texture coordinates, by
    // the copy constructor or by assignment, comes out without them too,
    // rather than with room for them that nothing wrote.
    TEST_CASE("llvolume: a copy of a face without normals or texture coordinates is without them", "[llvolume]")
    {
        struct Without
        {
            const char* what;
            bool normals;
            bool tex_coords;
        };
        const Without cases[] = {
            { "normals", true, false },
            { "texture coordinates", false, true },
            { "either", true, true },
        };
        const CorpusMesh mesh = uvSphere(16, 8);
        for (const Without& without : cases)
        {
            CAPTURE(without.what);
            LLVolumeFace face = mesh.face();
            if (without.normals)
            {
                face.mNormals = nullptr;
            }
            if (without.tex_coords)
            {
                face.mTexCoords = nullptr;
            }
            const LLVolumeFace constructed(face);
            LLVolumeFace assigned;
            assigned = face;
            const LLVolumeFace* copies[] = { &constructed, &assigned };
            for (const LLVolumeFace* copy : copies)
            {
                INFO((copy == &constructed ? "by the copy constructor" : "by assignment"));
                CHECK((copy->mNormals != nullptr) == !without.normals);
                CHECK((copy->mTexCoords != nullptr) == !without.tex_coords);
                REQUIRE(copy->mNumVertices == face.mNumVertices);
                for (S32 v = 0; v < face.mNumVertices; ++v)
                {
                    CAPTURE(v);
                    CHECK(v3(copy->mPositions[v]) == v3(face.mPositions[v]));
                    if (!without.normals)
                    {
                        CHECK(v3(copy->mNormals[v]) == v3(face.mNormals[v]));
                    }
                    if (!without.tex_coords)
                    {
                        CHECK(copy->mTexCoords[v] == face.mTexCoords[v]);
                    }
                }
            }
        }
    }

    // A circle path's stations step around it and end at its end cut. The
    // steps add up with rounding, and a station they bring a rounding short
    // of the end would leave a row with no height before the end's: no two
    // stations are within rounding of each other.
    TEST_CASE("llvolume: a circle path's stations are apart", "[llvolume]")
    {
        Report report;
        for (const Shape& s : sweepShapes())
        {
            if (s.path == LL_PCODE_PATH_LINE)
            {
                continue;
            }
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = build(s, lod);
                const auto& path = volume->getPath().mPath;
                for (S32 i = 0; i + 1 < (S32)path.size(); ++i)
                {
                    if (!(path[i + 1].mTexT - path[i].mTexT > 1e-4f))
                    {
                        report.fail(fmt::format("{}: stations {} and {} of {} are at t {} and {}", lodName(s, lod), i, i + 1, path.size(),
                                                path[i].mTexT, path[i + 1].mTexT));
                        break;
                    }
                }
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A line or circle path's frames are its stations' and how fast they
    // change: the rows of a station's scale and turn, M, against the
    // station's own, and their rates and the station's velocity against
    // its first station, at its begin cut, on paths cut a little before and
    // after it, by central difference. Its frames repeat where nothing
    // along it turns or scales. A circle2 path, whose stations alternate
    // sides, and a sculpt's have none.
    TEST_CASE("llvolume: a path's frames change at the rates its stations do", "[llvolume]")
    {
        constexpr F32 STEP = 1e-3f;
        Report report;
        S32 paths = 0;
        auto row = [](const LLPath::PathPt& pt, S32 i)
        {
            LLVector4a r = i == 0 ? pt.mRot.getRow<0>() : pt.mRot.getRow<1>();
            r.mul(pt.mScale[i]);
            return r;
        };
        for (const Shape& s : sweepShapes())
        {
            for (S32 lod : { 0, 3 })
            {
                const LLVolumeParams params = s.params();
                LLPath path;
                path.generate(params.getPathParams(), LOD_DETAIL[lod]);
                if (path.hasFrames() != (s.path != LL_PCODE_PATH_CIRCLE2))
                {
                    report.fail(fmt::format("{}: has frames is {}", lodName(s, lod), path.hasFrames()));
                }
                if (!path.hasFrames())
                {
                    continue;
                }
                LLPath sculpted;
                sculpted.generate(params.getPathParams(), LOD_DETAIL[lod], 0, true, 16);
                if (sculpted.hasFrames())
                {
                    report.fail(fmt::format("{}: a sculpt's path has frames", lodName(s, lod)));
                }
                const LLPathParams& line = params.getPathParams();
                const bool still = s.path == LL_PCODE_PATH_LINE && line.getTwistBegin() == line.getTwist() &&
                                   line.getBeginScale() == line.getEndScale();
                if (path.framesRepeat() != still)
                {
                    report.fail(fmt::format("{}: frames repeat is {}", lodName(s, lod), path.framesRepeat()));
                }
                for (S32 t = 0; t < path.getPathLength(); ++t)
                {
                    for (S32 i = 0; i < 2; ++i)
                    {
                        if (!path.mPath[t].mFrame.mRow[i].equals3(row(path.mPath[t], i), 1e-6f))
                        {
                            report.fail(fmt::format("{}: station {}'s frame row {} is not its scale and turn", lodName(s, lod), t, i));
                        }
                    }
                }

                LLPath before, after;
                LLPathParams early = params.getPathParams();
                LLPathParams late = params.getPathParams();
                early.setBegin(early.getBegin() - STEP);
                late.setBegin(late.getBegin() + STEP);
                before.generate(early, LOD_DETAIL[lod]);
                after.generate(late, LOD_DETAIL[lod]);
                const LLPath::PathPt& a = before.mPath[0];
                const LLPath::PathPt& b = after.mPath[0];
                const LLPath::PathFrame& frame = path.mPath[0].mFrame;
                auto compare = [&](const char* what, const LLVector4a& low, const LLVector4a& high, const LLVector4a& rate)
                {
                    for (S32 i = 0; i < 3; ++i)
                    {
                        const F32 expected = (high[i] - low[i]) / (2.f * STEP);
                        if (!(fabsf(rate[i] - expected) <= 2e-3f * llmax(1.f, fabsf(expected))))
                        {
                            report.fail(fmt::format("{}: the {}'s rate {} is {}, where its stations change at {}", lodName(s, lod), what, i, rate[i],
                                                    expected));
                            return;
                        }
                    }
                };
                compare("position", a.mPos, b.mPos, frame.mVelocity);
                compare("first row", row(a, 0), row(b, 0), frame.mRate[0]);
                compare("second row", row(a, 1), row(b, 1), frame.mRate[1]);
                ++paths;
            }
        }
        INFO(fmt::format("{} paths", paths));
        INFO(report.text());
        CHECK(paths > 0);
        CHECK(report.failures == 0);
    }

    // A begin cut just short of a step, as the cuts the wire carries can
    // be, snaps the path's first step to within a hundredth of a step of
    // the cut, and so can the rounding of the step's size where the cut is
    // on a step: a twist of 1.4 turns at LOD 3 gives a torus 41 sides,
    // whose first step rounds back to a cut at 0. That step is the cut's
    // own, as the end's last step is, and the first row has height.
    TEST_CASE("llvolume: a circle path's first station after its begin cut is apart from it", "[llvolume]")
    {
        Report report;
        S32 cuts = 0;
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            const F32 detail = LOD_DETAIL[lod];
            for (F32 twist : { 0.f, 0.7f })
            {
                const S32 sides = llfloor(MIN_DETAIL_FACES * detail + 2.f * twist * 3.5f * (detail - 0.5f));
                const F32 step = 1.f / sides;
                std::vector<F32> begins = { 0.f };
                for (S32 k = 1; k < sides; ++k)
                {
                    const S32 quanta = (S32)((F32)k / (F32)sides / CUT_QUANTA);
                    for (S32 j = 0; j < 3; ++j)
                    {
                        begins.push_back((F32)(quanta - j) * CUT_QUANTA);
                    }
                }
                for (F32 begin : begins)
                {
                    Shape s = torus();
                    s.twist_begin = -twist;
                    s.twist_end = twist;
                    s.path_begin = begin;
                    LLPath path;
                    path.generate(s.params().getPathParams(), detail);
                    ++cuts;
                    for (S32 i = 0; i + 1 < (S32)path.mPath.size(); ++i)
                    {
                        if (!(path.mPath[i + 1].mTexT - path.mPath[i].mTexT >= step * 0.01f))
                        {
                            report.fail(fmt::format("{} sides at LOD {}, cut at {}: stations {} and {} are at t {} and {}", sides, lod,
                                                    begin, i, i + 1, path.mPath[i].mTexT, path.mPath[i + 1].mTexT));
                            break;
                        }
                    }
                }
            }
        }
        INFO(fmt::format("{} cuts", cuts));
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // The streaming cost counts a circle2 path's points with
    // getNumNGonPoints, which walks the stations as genNGon places them. An
    // end cut on a step, which the summed steps reach a rounding short of,
    // is one station to both.
    TEST_CASE("llvolume: a circle2 path's point count is its stations", "[llvolume]")
    {
        Report report;
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            const S32 sides = llfloor(MIN_DETAIL_FACES * LOD_DETAIL[lod]);
            for (S32 k = 1; k <= sides; ++k)
            {
                Shape s = torus();
                s.name = "circle2 torus";
                s.path = LL_PCODE_PATH_CIRCLE2;
                s.path_end = (F32)k / (F32)sides;
                const LLVolumeParams params = s.params();
                LLPointer<LLVolume> volume = new LLVolume(params, LOD_DETAIL[lod]);
                const S32 counted = LLPath::getNumPoints(params.getPathParams(), LOD_DETAIL[lod]);
                const S32 stations = (S32)volume->getPath().mPath.size();
                if (counted != stations)
                {
                    report.fail(fmt::format("{} cut to {} of {} sides at LOD {}: {} points counted, {} stations", s.name, k, sides, lod,
                                            counted, stations));
                }
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A sculpt samples a row of its map at each station of its path, as
    // every viewer samples it, so its path keeps the station the summed
    // steps bring a rounding short of the end, which a prim's leaves out:
    // its last station before the end is the last step the steps take
    // short of the end, and a step on from it is not short of the end.
    TEST_CASE("llvolume: a sculpt's path keeps a station a rounding short of its end", "[llvolume]")
    {
        Report report;
        S32 kept = 0;
        const LLPathParams params = sphere().params().getPathParams();
        for (S32 sides = 2; sides <= 300; ++sides)
        {
            LLPath path;
            path.generate(params, 1.f, 0, true, sides);
            const S32 count = (S32)path.mPath.size();
            const F32 step = 1.0f / sides;
            const F32 last = path.mPath[count - 2].mTexT;
            if (!(last < params.getEnd()) || last + step < params.getEnd())
            {
                report.fail(
                    fmt::format("{} sides: the last station before the end is at t {}, a step of {} short of the end", sides, last, step));
            }
            if (last > params.getEnd() - step * 0.01f)
            {
                ++kept;
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
        CHECK(kept > 0);
    }

    // A triangle whose UVs have no area has no dP/du, and one whose
    // positions have no area has no surface to give one: neither adds to
    // its corners' tangents, so a real neighbour's tangent stands alone.
    TEST_CASE("llvolume: a triangle with no area or no UV area adds nothing to a tangent", "[llvolume]")
    {
        // Vertex 0 is in one real triangle, whose tangent is +X, and in one
        // other: a triangle whose UVs lie in a line, or one whose first two
        // corners are one point.
        struct Neighbour
        {
            const char* name;
            LLVector3 positions[2];
            LLVector2 uvs[2];
        };
        const Neighbour neighbours[] = {
            { "beside a triangle whose UVs are a line", { { 0, -1, 0 }, { -1, 0, 0 } }, { { 0, 1 }, { 0, 2 } } },
            { "beside a triangle of no area", { { 0, 0, 0 }, { -3, 0, 0 } }, { { 0, 1 }, { 1, 0 } } },
        };
        for (const Neighbour& neighbour : neighbours)
        {
            const LLVector3 positions[] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, neighbour.positions[0], neighbour.positions[1] };
            const LLVector2 uvs[] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, neighbour.uvs[0], neighbour.uvs[1] };
            const U16 indices[] = { 0, 1, 2, 0, 3, 4 };
            LLVolumeFace face;
            face.resizeVertices(5);
            face.resizeIndices(6);
            for (S32 v = 0; v < 5; ++v)
            {
                face.mPositions[v].load3(positions[v].mV);
                face.mNormals[v].set(0.f, 0.f, 1.f);
                face.mTexCoords[v] = uvs[v];
            }
            std::copy(std::begin(indices), std::end(indices), face.mIndices);
            face.createTangents();
            const LLVector3 tangent = v3(face.mTangents[0]);
            INFO(fmt::format("{}: vertex 0's tangent is ({:.3f}, {:.3f}, {:.3f})", neighbour.name, tangent.mV[0], tangent.mV[1], tangent.mV[2]));
            CHECK(degreesBetween(tangent, LLVector3(1.f, 0.f, 0.f)) < 1.f);
        }
    }

    // A side's normals are sums over the quads around each vertex, and must
    // leave the build unit length: the vertex buffer takes them as they are
    // where a face has no tangents.
    TEST_CASE("llvolume: normals are unit length straight out of generation", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::RawUnit;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        inspectSculpts(how, report);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // SetHollow no longer holds a square hole in a round profile to
    // 0.7, and the hole's corners, at 0.707 of the hollow, would go through
    // the wall above it: the hole's points must stay inside the wall.
    TEST_CASE("llvolume: a square hole above 0.707 stays inside its wall", "[llvolume]")
    {
        Report report;
        for (const Shape& base : { cylinder(), sphere(), prism() })
        {
            for (F32 hollow : { 0.75f, 0.85f, 0.95f })
            {
                Shape s = base;
                s.hole = LL_PCODE_HOLE_SQUARE;
                s.hollow = hollow;
                s.name = fmt::format("{} with a square hole at {}", base.name, hollow);
                for (S32 lod = 0; lod < LOD_COUNT; ++lod)
                {
                    LLPointer<LLVolume> volume = build(s, lod);
                    const LLProfile& outline = volume->getProfile();
                    const S32 outer = outline.getTotalOut();
                    // The wall's outline is convex, its points in order:
                    // a point is inside where it is on the inner side of
                    // every edge.
                    F32 area = 0.f;
                    for (S32 i = 0; i < outer; ++i)
                    {
                        const LLVector4a& a = outline.mProfile[i];
                        const LLVector4a& b = outline.mProfile[(i + 1) % outer];
                        area += a[0] * b[1] - b[0] * a[1];
                    }
                    for (S32 h = outer; h < outline.getTotal(); ++h)
                    {
                        const LLVector4a& p = outline.mProfile[h];
                        for (S32 i = 0; i < outer; ++i)
                        {
                            const LLVector4a& a = outline.mProfile[i];
                            const LLVector4a& b = outline.mProfile[(i + 1) % outer];
                            const F32 side = ((b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])) * (area > 0.f ? 1.f : -1.f);
                            if (side < -1e-5f)
                            {
                                report.fail(fmt::format("{} at LOD {}: hole point {} ({:.3f}, {:.3f}) is outside the wall", s.name, lod, h, p[0], p[1]));
                                break;
                            }
                        }
                    }
                }
            }
        }
        // The sweep's square holes above 0.707 in round and triangular
        // profiles, cut, twisted and tapered: where hole and wall touch, the
        // cap's strip and the inner side still have a layout.
        Inspect how;
        how.checks = Checks::Inversion | Checks::CapFacing | Checks::Oracle;
        for (const Shape& s : sweepShapes())
        {
            if (s.hole == LL_PCODE_HOLE_SQUARE && s.hollow > 0.70711f && s.profile != LL_PCODE_PROFILE_SQUARE)
            {
                for (S32 lod = 0; lod < LOD_COUNT; ++lod)
                {
                    inspectPrim(s, lod, how, report);
                }
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // Partial builds: a flexible prim's every frame rebuilds its volume in
    // place, and must come out as a full build would, in the buffers it had.
    TEST_CASE("llvolume: a flexible box's partial build equals a full one", "[llvolume]")
    {
        Report report;
        for (S32 sections : { 4, 16 })
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                checkFlexiFrame(LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_SAME, 0.f, sections, lod, report, fmt::format("flexible box, {} sections, LOD {}", sections, lod));
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    TEST_CASE("llvolume: a flexible cylinder's partial build equals a full one", "[llvolume]")
    {
        Report report;
        for (S32 sections : { 4, 16 })
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                checkFlexiFrame(LL_PCODE_PROFILE_CIRCLE, LL_PCODE_HOLE_SAME, 0.f, sections, lod, report,
                                fmt::format("flexible cylinder, {} sections, LOD {}", sections, lod));
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    TEST_CASE("llvolume: a flexible box with a square hole has a partial build equal to a full one", "[llvolume]")
    {
        Report report;
        for (S32 sections : { 4, 16 })
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                checkFlexiFrame(LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_SQUARE, 0.5f, sections, lod, report,
                                fmt::format("flexible hollow box, {} sections, LOD {}", sections, lod));
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A flexible prim of one section, as a distant one is drawn, keeps the
    // two stations its volume is first built with, both at the origin
    // before the rebuild gives them a pose, so every frame is a partial
    // build of that first one. Its first and last rows were one place
    // there, and are not joined for it: its normals are those a full build
    // at its pose makes.
    TEST_CASE("llvolume: a flexible prim of one section keeps its first and last rows apart", "[llvolume]")
    {
        Report report;
        for (U8 profile : { LL_PCODE_PROFILE_SQUARE, LL_PCODE_PROFILE_CIRCLE })
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                const std::string name =
                    fmt::format("flexible {}, one section, LOD {}", profile == LL_PCODE_PROFILE_SQUARE ? "box" : "cylinder", lod);
                LLPointer<LLVolume> volume = flexiVolume(profile, LL_PCODE_HOLE_SAME, 0.f, lod);
                REQUIRE(volume->getPath().getPathLength() == 2);
                bendPath(*volume, 1, 1.1f);
                volume->regen();
                genAllTangents(*volume);

                LLPointer<LLVolume> full = flexiVolume(profile, LL_PCODE_HOLE_SAME, 0.f, lod);
                full->resizePath(2);
                bendPath(*full, 1, 1.1f);
                full->regen();
                genAllTangents(*full);
                compareBuilds(*volume, *full, report, name);
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A flexible prim's sides have no area before its first pose, and keep
    // no planar normal from it: a one-section flexible prim's frames, every
    // one a partial build of that first one, would otherwise project every
    // side along z. Once posed, each side's planar normal is the one a full
    // build at the pose makes. Its caps have area from the first, and keep
    // the planar normal they first have, as every face of a flexible prim
    // does.
    TEST_CASE("llvolume: a flexible prim keeps no planar normal from before its first pose", "[llvolume]")
    {
        Report report;
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            LLPointer<LLVolume> volume = flexiVolume(LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_SAME, 0.f, lod);
            for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
            {
                volume->getVolumeFace(f).getPlanarNormal();
            }
            bendPath(*volume, 1, 1.1f);
            volume->regen();

            LLPointer<LLVolume> full = flexiVolume(LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_SAME, 0.f, lod);
            full->resizePath(2);
            bendPath(*full, 1, 1.1f);
            full->regen();
            REQUIRE(volume->getNumVolumeFaces() == full->getNumVolumeFaces());
            for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
            {
                if (!(volume->getVolumeFace(f).mTypeMask & LLVolumeFace::SIDE_MASK))
                {
                    continue;
                }
                const LLVector4a got = volume->getVolumeFace(f).getPlanarNormal();
                const LLVector4a want = full->getVolumeFace(f).getPlanarNormal();
                if (std::memcmp(got.getF32ptr(), want.getF32ptr(), 4 * sizeof(F32)) != 0)
                {
                    report.fail(fmt::format("flexible box, one section, LOD {}: {} has planar normal ({}, {}, {}, {}), "
                                            "a full build ({}, {}, {}, {})",
                                            lod, faceName(volume->getVolumeFace(f)), got[0], got[1], got[2], got[3], want[0], want[1],
                                            want[2], want[3]));
                }
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // The uncut cube cap is built only on a line path, which flexi never
    // has; its partial build is the one a regen of an unmoved box makes.
    TEST_CASE("llvolume: an uncut cube cap's partial build keeps its buffers", "[llvolume]")
    {
        Report report;
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            LLPointer<LLVolume> volume = build(box(), lod, true);
            genAllTangents(*volume);
            const std::vector<Buffers> before = buffersOf(*volume);
            volume->regen();
            const std::vector<Buffers> after = buffersOf(*volume);
            genAllTangents(*volume);
            LLPointer<LLVolume> fresh = build(box(), lod, true);
            genAllTangents(*fresh);
            const std::string name = fmt::format("box at LOD {}", lod);
            compareBuilds(*volume, *fresh, report, name);
            for (S32 i = 0; i < volume->getNumVolumeFaces(); ++i)
            {
                if (after[i].tangents != before[i].tangents || after[i].positions != before[i].positions || after[i].indices != before[i].indices)
                {
                    report.fail(fmt::format("{}: {} was reallocated by its partial build", name, faceName(volume->getVolumeFace(i))));
                }
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A pole test that compared a side's first row with the row before its
    // last would compare the first row with itself on a side of two rows,
    // take every such side's first column for a pole and leave its seam
    // unwrapped: a cylinder's seam at LOD 0 and 1 would be two one-sided
    // normals.
    TEST_CASE("llvolume: a closed side of two rows has one normal at its seam", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::SeamsAgree;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A general cap's centre vertex takes the middle of the cap's bounding
    // box for its position. A twist rotates the cap, and the middle of the
    // UVs' box is then another point: a centre that took that UV would
    // slide the texture at the centre of every twisted cap.
    TEST_CASE("llvolume: a twisted cap's centre vertex has the UV of its position", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::AffineUV;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A sphere's half disc sweeps about the axis it touches. A twist turns
    // it about its own centre on that axis, to the other side of the axis,
    // and the sphere then leaves the cap the other way: a cap that kept the
    // winding and normal its profile gives it would face into the sphere.
    TEST_CASE("llvolume: a twisted sphere's caps face out of the sphere", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::CapFacing | Checks::Inversion;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A sphere's poles are where its half circle touches the axis it sweeps
    // about, and their normals lie along that axis, the way the side's own
    // normals face beside them: out of the sphere on its outer side, into it
    // on a hollow sphere's inner side. A twist of a half turn at both ends
    // puts each pole at the other end of the axis and turns the half circle
    // over, so that it sweeps the sphere the other way round: its triangles
    // then face into the sphere, and so do all its normals, its poles' too.
    TEST_CASE("llvolume: a sphere's pole normals lie along its axis the way its side faces at any half turn of twist", "[llvolume]")
    {
        Report report;
        S32 poles = 0;
        for (F32 twist : { 0.f, 0.5f, -0.5f, 1.f })
        {
            for (F32 hollow : { 0.f, 0.5f })
            {
                Shape s = sphere();
                s.twist_begin = s.twist_end = twist;
                s.hollow = hollow;
                s.name = fmt::format("sphere twisted {} hollow {}", twist, hollow);
                // Untwisted or a whole turn, the half circle sweeps the outer
                // side out of the sphere.
                const bool turned_over = twist == 0.5f || twist == -0.5f;
                for (S32 lod = 0; lod < LOD_COUNT; ++lod)
                {
                    LLPointer<LLVolume> volume = build(s, lod);
                    for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
                    {
                        const LLVolumeFace& face = volume->getVolumeFace(f);
                        if (!(face.mTypeMask & LLVolumeFace::SIDE_MASK) || face.mNumS * face.mNumT != face.mNumVertices)
                        {
                            continue;
                        }
                        const F32 inward = ((face.mTypeMask & LLVolumeFace::INNER_MASK) ? -1.f : 1.f) * (turned_over ? -1.f : 1.f);
                        for (S32 column : { 0, face.mNumS - 1 })
                        {
                            bool pole = true;
                            for (S32 t = 1; t < face.mNumT && pole; ++t)
                            {
                                pole = face.mPositions[column + face.mNumS * t].equals3(face.mPositions[column], 1e-6f);
                            }
                            if (!pole)
                            {
                                continue;
                            }
                            ++poles;
                            const S32 beside = column == 0 ? 1 : column - 1;
                            for (S32 t = 0; t < face.mNumT; ++t)
                            {
                                const S32 v = column + face.mNumS * t;
                                const LLVector3 n = v3(face.mNormals[v]);
                                const F32 out = inward * (n * v3(face.mPositions[v]));
                                const F32 off_axis = llmin(degreesBetween(n, LLVector3(1.f, 0.f, 0.f)), degreesBetween(n, LLVector3(-1.f, 0.f, 0.f)));
                                const F32 with_side = n * v3(face.mNormals[beside + face.mNumS * t]);
                                if (!(out > 0.f) || !(off_axis <= 0.01f) || !(with_side > 0.f))
                                {
                                    report.fail(fmt::format("{}: {}'s pole at column {} has normal ({}, {}, {}) at ({}, {}, {}), {:.3f} degrees off the axis",
                                                            lodName(s, lod), faceName(face), column, n.mV[0], n.mV[1], n.mV[2], face.mPositions[v][0],
                                                            face.mPositions[v][1], face.mPositions[v][2], off_axis));
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        INFO(report.text());
        CHECK(poles > 0);
        CHECK(report.failures == 0);
    }

    // The sphere placeholder gets a sphere sculpt's normal stitching, so it
    // must run as a sphere sculpt's map does, around along the profile and
    // pole to pole along the path. The other way round, the poles' average
    // would land on one meridian, and the S wrap would sum the north pole
    // with the south. It gets the sphere's stitching whatever its own: a
    // torus's would sum its two poles, and a plane's leave its seam open.
    TEST_CASE("llvolume: the sphere placeholder's normals follow its surface", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::Inversion | Checks::Oracle;
        Report report;
        inspectPlaceholders(how, true, report);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A vertex whose every triangle has no area sums to a zero normal, which
    // a normalize makes NaN: the copied last column of a cut flat inner
    // side, the inner side of a half circle with a square or triangle hole,
    // the middle of a sculpt's pinch, the whole of the empty placeholder.
    // It takes a neighbour's normal instead; where a copy is one place with
    // its neighbour only to rounding, the edge between them must count as
    // having no length, or it gives the copy a normal of rounding.
    TEST_CASE("llvolume: a vertex with only zero-area triangles takes a neighbour's normal", "[llvolume]")
    {
        Inspect how;
        how.checks = Checks::Tangents | Checks::LoneNormals;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            inspectSculpt(LL_SCULPT_TYPE_PLANE, pinchedMap(), lod, "pinched", how, report);
            LLPointer<LLVolume> empty = buildSculpt(LL_SCULPT_TYPE_SPHERE, SculptMap(), lod, false);
            inspect(*empty, fmt::format("empty placeholder at LOD {}", lod), how, report);
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // ------------------------------------------------------------------
    // Vertex and triangle counts

    struct Counts
    {
        S32 vertices = 0;
        S32 triangles = 0;

        bool operator==(const Counts& other) const { return vertices == other.vertices && triangles == other.triangles; }
    };

    Counts countsOf(const LLVolume& volume)
    {
        Counts counts;
        counts.triangles = volume.getNumTriangles(&counts.vertices);
        return counts;
    }

    struct ExpectedCounts
    {
        Shape shape;
        Counts lods[LOD_COUNT];
    };

    void checkCounts(const ExpectedCounts& expected)
    {
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            LLPointer<LLVolume> volume = build(expected.shape, lod);
            const Counts counts = countsOf(*volume);
            INFO(fmt::format("{}: {} vertices and {} triangles, where {} and {} are expected", lodName(expected.shape, lod), counts.vertices,
                             counts.triangles, expected.lods[lod].vertices, expected.lods[lod].triangles));
            CHECK(counts == expected.lods[lod]);
        }
    }

    // A straight path that neither twists nor tapers sweeps every profile
    // edge into a parallelogram, which two triangles map exactly: a flat
    // side is one quad, an uncut box's cap is one, and the path has its two
    // ends alone. A path that twists or tapers, a circle and a flexible path
    // keep the points they lay out.
    TEST_CASE("llvolume: a straight path that neither twists nor tapers is one quad a profile edge", "[llvolume]")
    {
        Shape twisted = box();
        twisted.name = "twisted box";
        twisted.twist_begin = -0.5f;
        twisted.twist_end = 0.5f;
        Shape tapered = cylinder();
        tapered.name = "tapered cylinder";
        tapered.scale_x = tapered.scale_y = 0.5f;
        const ExpectedCounts table[] = {
            { box(), { { 24, 12 }, { 24, 12 }, { 24, 12 }, { 24, 12 } } },
            { cylinder(), { { 30, 24 }, { 42, 36 }, { 66, 60 }, { 102, 96 } } },
            { twisted, { { 32, 20 }, { 48, 36 }, { 126, 144 }, { 256, 348 } } },
            { tapered, { { 30, 24 }, { 42, 36 }, { 82, 90 }, { 152, 192 } } },
            { torus(), { { 49, 72 }, { 100, 162 }, { 256, 450 }, { 625, 1152 } } },
        };
        for (const ExpectedCounts& expected : table)
        {
            checkCounts(expected);
        }

        Report report;
        for (const Shape& s : sweepShapes())
        {
            if (s.path != LL_PCODE_PATH_LINE || s.twist_begin != s.twist_end || s.scale_x != 1.f || s.scale_y != 1.f)
            {
                continue;
            }
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = build(s, lod);
                const std::string name = lodName(s, lod);
                if (volume->getPath().getPathLength() != 2)
                {
                    report.fail(fmt::format("{}: {} path points", name, volume->getPath().getPathLength()));
                    continue;
                }
                // A hole drawn in to its wall is not straight between its
                // corners, and keeps its split points; its outer sides
                // keep theirs with it.
                const bool drawn_in = volume->getProfile().holeTouchesWall();
                for (S32 i = 0; i < volume->getNumVolumeFaces(); ++i)
                {
                    const LLVolumeFace& face = volume->getVolumeFace(i);
                    const LLProfile::Face& outline = volume->getProfile().mFaces[i];
                    const bool flat_outer = (face.mTypeMask & LLVolumeFace::OUTER_MASK) && (face.mTypeMask & LLVolumeFace::FLAT_MASK);
                    if (flat_outer && !drawn_in && outline.mCount != 2)
                    {
                        report.fail(fmt::format("{}: {} has {} profile points", name, faceName(face), outline.mCount));
                    }
                    if (isCap(face) && !(face.mTypeMask & (LLVolumeFace::HOLLOW_MASK | LLVolumeFace::OPEN_MASK)) && s.profile == LL_PCODE_PROFILE_SQUARE &&
                        s.path_begin == 0.f && s.path_end == 1.f && face.mNumVertices != 4)
                    {
                        report.fail(fmt::format("{}: {} has {} vertices", name, faceName(face), face.mNumVertices));
                    }
                }
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);

        // A square hole in a cylinder: its corners alone where it stays clear
        // of the wall, and every split point where it is drawn in to it.
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            const S32 split = (S32)(LOD_DETAIL[lod] * 0.66f);
            for (F32 hollow : { 0.5f, 0.85f })
            {
                Shape s = cylinder();
                s.hole = LL_PCODE_HOLE_SQUARE;
                s.hollow = hollow;
                s.name = fmt::format("cylinder with a square hole at {}", hollow);
                LLPointer<LLVolume> volume = build(s, lod);
                const LLProfile& outline = volume->getProfile();
                const S32 hole = outline.getTotal() - outline.getTotalOut();
                INFO(fmt::format("{}: {} hole points", lodName(s, lod), hole));
                CHECK(outline.holeTouchesWall() == (hollow > 0.75f));
                CHECK(hole == (outline.holeTouchesWall() ? 4 * (split + 1) + 1 : 5));
                CHECK(volume->getPath().getPathLength() == 2);
            }
        }
    }


    // A flat inner side's columns, against its hole's points: a column for
    // each, and a copy after each corner, a point where the hole turns, or
    // after every point between the ends where the hole is drawn in to its
    // wall; no copy of an end or of a split point; and no triangle in the
    // quad between a corner and its copy.
    void checkCorners(const LLVolume& volume, const std::string& name, Report& report)
    {
        const LLProfile& outline = volume.getProfile();
        for (S32 i = 0; i < volume.getNumVolumeFaces(); ++i)
        {
            const LLVolumeFace& face = volume.getVolumeFace(i);
            if (!isDoubled(face) || !isGrid(face))
            {
                continue;
            }
            const std::string where = fmt::format("{}: {}", name, faceName(face));
            const S32 points = outline.mFaces[i].mCount;
            const std::vector<S32> strips = stripsOf(face);
            std::vector<S32> copies(points, 0);
            S32 point = 0;
            for (S32 s = 1; s < face.mNumS; ++s)
            {
                if (strips[s] != strips[s - 1])
                {
                    ++copies[point];
                }
                else
                {
                    ++point;
                }
            }
            if (point != points - 1)
            {
                report.fail(fmt::format("{}: {} columns for {} points", where, face.mNumS, points));
                continue;
            }
            for (S32 k = 0; k < points; ++k)
            {
                bool corner = false;
                if (k > 0 && k + 1 < points)
                {
                    const LLVector4a& a = outline.mProfile[face.mBeginS + k - 1];
                    const LLVector4a& b = outline.mProfile[face.mBeginS + k];
                    const LLVector4a& c = outline.mProfile[face.mBeginS + k + 1];
                    const F32 e1x = b[0] - a[0], e1y = b[1] - a[1], e2x = c[0] - b[0], e2y = c[1] - b[1];
                    corner = fabsf(e1x * e2y - e1y * e2x) > 1e-4f * sqrtf((e1x * e1x + e1y * e1y) * (e2x * e2x + e2y * e2y)) || outline.holeTouchesWall();
                }
                if (copies[k] != (corner ? 1 : 0))
                {
                    report.fail(fmt::format("{}: point {} of {}, {}, has {} copies", where, k, points, corner ? "a corner" : "no corner", copies[k]));
                    break;
                }
            }
            for (S32 t = 0; t + 2 < face.mNumIndices; t += 3)
            {
                S32 low = face.mNumS, high = 0;
                for (S32 k = 0; k < 3; ++k)
                {
                    low = llmin(low, (S32)(face.mIndices[t + k] % face.mNumS));
                    high = llmax(high, (S32)(face.mIndices[t + k] % face.mNumS));
                }
                if (strips[low] != strips[high])
                {
                    report.fail(fmt::format("{}: triangle {} spans a corner and its copy", where, t / 3));
                    break;
                }
            }
        }
    }

    // A flat inner side copies its hole's corners, where its normals turn,
    // and no point between them, so that it is smooth from corner to corner
    // as an outer side is; it has no triangles between a corner and its
    // copy. A hollow box's inner side is one quad a side of its hole.
    TEST_CASE("llvolume: a flat inner side copies its corners and nothing else", "[llvolume]")
    {
        Shape hollow_box = box();
        hollow_box.name = "hollow box";
        hollow_box.hole = LL_PCODE_HOLE_SQUARE;
        hollow_box.hollow = 0.5f;
        Shape hollow_prism = prism();
        hollow_prism.name = "hollow prism";
        hollow_prism.hollow = 0.5f;
        Shape twisted = hollow_box;
        twisted.name = "twisted hollow box";
        twisted.twist_begin = -0.5f;
        twisted.twist_end = 0.5f;
        Shape tube_hole = tube();
        tube_hole.name = "tube with a square hole";
        tube_hole.hole = LL_PCODE_HOLE_SQUARE;
        tube_hole.hollow = 0.5f;
        checkCounts({ hollow_box, { { 52, 32 }, { 52, 32 }, { 52, 32 }, { 52, 32 } } });
        checkCounts({ hollow_prism, { { 40, 24 }, { 40, 24 }, { 40, 24 }, { 40, 24 } } });
        checkCounts({ twisted, { { 68, 48 }, { 100, 80 }, { 252, 288 }, { 500, 672 } } });
        checkCounts({ tube_hole, { { 112, 96 }, { 160, 144 }, { 384, 480 }, { 800, 1152 } } });

        // A flexible hollow box, as a frame lays it out: its path's sections
        // keep their split points, and its inner side copies only corners.
        struct Flexi
        {
            S32 sections;
            S32 lod;
            Counts counts;
        };
        for (const Flexi& expected : { Flexi{ 4, 1, { 100, 80 } }, Flexi{ 4, 3, { 212, 240 } }, Flexi{ 16, 1, { 292, 272 } }, Flexi{ 16, 3, { 596, 816 } } })
        {
            LLPointer<LLVolume> volume = flexiVolume(LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_SQUARE, 0.5f, expected.lod);
            bendPath(*volume, expected.sections, 1.1f);
            volume->regen();
            const Counts counts = countsOf(*volume);
            INFO(fmt::format("flexible hollow box, {} sections, LOD {}: {} vertices and {} triangles, where {} and {} are expected", expected.sections,
                             expected.lod, counts.vertices, counts.triangles, expected.counts.vertices, expected.counts.triangles));
            CHECK(counts == expected.counts);
        }

        Report report;
        S32 volumes = 0;
        for (const Shape& s : sweepShapes())
        {
            if (s.hole != LL_PCODE_HOLE_SQUARE && s.hole != LL_PCODE_HOLE_TRIANGLE && !(s.hole == LL_PCODE_HOLE_SAME && s.profile != LL_PCODE_PROFILE_CIRCLE &&
                                                                                     s.profile != LL_PCODE_PROFILE_CIRCLE_HALF))
            {
                continue;
            }
            if (s.hollow <= 0.f)
            {
                continue;
            }
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = build(s, lod);
                checkCorners(*volume, lodName(s, lod), report);
                ++volumes;
            }
        }
        for (S32 sections : { 4, 16 })
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = flexiVolume(LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_SQUARE, 0.5f, lod);
                bendPath(*volume, sections, 1.1f);
                volume->regen();
                checkCorners(*volume, fmt::format("flexible hollow box, {} sections, LOD {}", sections, lod), report);
            }
        }
        // A hollow sphere's flat inner side runs from pole to pole, a cone
        // at each: both its ends take the normals the strips they end give
        // them, not the axis.
        for (U8 hole : { LL_PCODE_HOLE_SQUARE, LL_PCODE_HOLE_TRIANGLE })
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                Shape s = sphere();
                s.hole = hole;
                s.hollow = 0.75f;
                s.name = hole == LL_PCODE_HOLE_SQUARE ? "sphere with a square hole" : "sphere with a triangle hole";
                LLPointer<LLVolume> volume = build(s, lod);
                for (S32 i = 0; i < volume->getNumVolumeFaces(); ++i)
                {
                    const LLVolumeFace& face = volume->getVolumeFace(i);
                    if (!isDoubled(face) || !isGrid(face))
                    {
                        continue;
                    }
                    for (S32 column : { 0, face.mNumS - 1 })
                    {
                        for (S32 t = 0; t < face.mNumT; ++t)
                        {
                            const LLVector3 n = v3(face.mNormals[gridIndex(face, column, t)]);
                            if (degreesBetween(n, LLVector3(1.f, 0.f, 0.f)) < 1.f || degreesBetween(n, LLVector3(-1.f, 0.f, 0.f)) < 1.f)
                            {
                                report.fail(fmt::format("{}: {} column {} row {} takes the axis", lodName(s, lod), faceName(face), column, t));
                                break;
                            }
                        }
                    }
                }
            }
        }
        INFO(fmt::format("{} volumes", volumes));
        INFO(report.text());
        CHECK(volumes > 0);
        CHECK(report.failures == 0);
    }


    // A shared volume's faces against its unique twin's: the same vertices,
    // and the same triangles but for some of no area, which it leaves out.
    // Returns how many it left out.
    S32 checkAgainstUnique(const Shape& s, S32 lod, Report& report)
    {
        LLPointer<LLVolume> shared = build(s, lod);
        LLPointer<LLVolume> unique = build(s, lod, true);
        const std::string name = lodName(s, lod);
        if (shared->getNumVolumeFaces() != unique->getNumVolumeFaces())
        {
            report.fail(fmt::format("{}: {} faces against {}", name, shared->getNumVolumeFaces(), unique->getNumVolumeFaces()));
            return 0;
        }
        S32 left_out = 0;
        for (S32 i = 0; i < shared->getNumVolumeFaces(); ++i)
        {
            const LLVolumeFace& a = shared->getVolumeFace(i);
            const LLVolumeFace& b = unique->getVolumeFace(i);
            const std::string where = fmt::format("{}: {}", name, faceName(a));
            if (a.mNumVertices != b.mNumVertices || std::memcmp(a.mPositions, b.mPositions, sizeof(LLVector4a) * a.mNumVertices) != 0 ||
                std::memcmp(a.mTexCoords, b.mTexCoords, sizeof(LLVector2) * a.mNumVertices) != 0)
            {
                report.fail(fmt::format("{}: its vertices are not its unique twin's", where));
                continue;
            }
            std::map<std::array<U16, 3>, S32> kept;
            for (S32 k = 0; k + 2 < a.mNumIndices; k += 3)
            {
                ++kept[{ a.mIndices[k], a.mIndices[k + 1], a.mIndices[k + 2] }];
            }
            const std::vector<Triangle> triangles = trianglesOf(b);
            for (const Triangle& tri : triangles)
            {
                const auto found = kept.find({ (U16)tri.v[0], (U16)tri.v[1], (U16)tri.v[2] });
                if (found != kept.end() && found->second > 0)
                {
                    --found->second;
                    continue;
                }
                ++left_out;
                if (!tri.degenerate)
                {
                    report.fail(fmt::format("{}: triangle ({}, {}, {}), which has area, is left out", where, tri.v[0], tri.v[1], tri.v[2]));
                    return left_out;
                }
            }
            // Every triangle it kept is one of the twin's.
            for (const auto& [corners, count] : kept)
            {
                if (count != 0)
                {
                    report.fail(fmt::format("{}: triangle ({}, {}, {}) is not its unique twin's", where, corners[0], corners[1], corners[2]));
                    return left_out;
                }
            }
        }
        return left_out;
    }

    // On a shared volume, a row or column its parameters take to one point,
    // the apex of a taper to a point or a sphere's pole, has no triangles:
    // those with two corners there have no area. It keeps its vertices,
    // each with its own UV and normal. The parameters decide it, so a
    // unique volume, whose path a rebuild may move, keeps every triangle,
    // and so does a sphere whose twist turns its poles off the axis.
    TEST_CASE("llvolume: a shared volume has no triangles where its parameters collapse a row or column", "[llvolume]")
    {
        Shape pyramid = box();
        pyramid.name = "pyramid";
        pyramid.scale_x = pyramid.scale_y = 0.f;
        Shape inverted = box();
        inverted.name = "box tapered to a point at its base";
        inverted.scale_x = inverted.scale_y = 2.f;
        Shape cone = cylinder();
        cone.name = "cone";
        cone.scale_x = cone.scale_y = 0.f;
        Shape half_turned = sphere();
        half_turned.name = "sphere turned half a turn";
        half_turned.twist_begin = half_turned.twist_end = 0.5f;
        checkCounts({ sphere(), { { 28, 24 }, { 50, 54 }, { 128, 180 }, { 325, 528 } } });
        checkCounts({ half_turned, { { 28, 24 }, { 50, 54 }, { 128, 180 }, { 325, 528 } } });
        checkCounts({ pyramid, { { 24, 8 }, { 24, 8 }, { 24, 8 }, { 24, 8 } } });
        checkCounts({ inverted, { { 24, 8 }, { 24, 8 }, { 24, 8 }, { 24, 8 } } });
        checkCounts({ cone, { { 30, 18 }, { 42, 27 }, { 82, 75 }, { 152, 168 } } });

        Report report;
        Shape twisted = sphere();
        twisted.name = "twisted sphere";
        twisted.twist_begin = -0.5f;
        twisted.twist_end = 0.75f;
        Shape quarter_turned = sphere();
        quarter_turned.name = "sphere turned a quarter turn";
        quarter_turned.twist_begin = quarter_turned.twist_end = 0.25f;
        for (const Shape& s : { twisted, quarter_turned })
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                if (checkAgainstUnique(s, lod, report) != 0)
                {
                    report.fail(fmt::format("{}: triangles left out where the poles leave the axis", lodName(s, lod)));
                }
            }
        }
        S32 left_out = 0;
        for (const Shape& s : sweepShapes())
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                left_out += checkAgainstUnique(s, lod, report);
            }
        }
        for (const Shape& s : { pyramid, inverted, cone, half_turned })
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                left_out += checkAgainstUnique(s, lod, report);
            }
        }
        INFO(fmt::format("{} triangles left out", left_out));
        INFO(report.text());
        CHECK(left_out > 0);
        CHECK(report.failures == 0);
    }


    // What first differs between two volumes, or nothing where they are the
    // same bytes: the profile's faces and points, the path's length, the
    // mesh, and each face's layout, bounds, positions, normals, UVs,
    // tangents and indices.
    std::string differenceBetween(LLVolume& a, LLVolume& b)
    {
        auto same = [](const void* x, const void* y, size_t bytes) { return bytes == 0 || std::memcmp(x, y, bytes) == 0; };
        const LLProfile& pa = a.getProfile();
        const LLProfile& pb = b.getProfile();
        if (pa.mFaces.size() != pb.mFaces.size())
        {
            return fmt::format("{} profile faces against {}", pa.mFaces.size(), pb.mFaces.size());
        }
        for (size_t i = 0; i < pa.mFaces.size(); ++i)
        {
            const LLProfile::Face& fa = pa.mFaces[i];
            const LLProfile::Face& fb = pb.mFaces[i];
            if (fa.mIndex != fb.mIndex || fa.mCount != fb.mCount || fa.mScaleU != fb.mScaleU || fa.mCap != fb.mCap || fa.mFlat != fb.mFlat ||
                fa.mFaceID != fb.mFaceID || fa.mFirstCorner != fb.mFirstCorner || fa.mCornerStep != fb.mCornerStep)
            {
                return fmt::format("profile face {}", i);
            }
        }
        if (pa.mProfile.size() != pb.mProfile.size() || !same(pa.mProfile.data(), pb.mProfile.data(), sizeof(LLVector4a) * pa.mProfile.size()))
        {
            return "the profile's points";
        }
        if (a.getPath().getPathLength() != b.getPath().getPathLength())
        {
            return fmt::format("{} path points against {}", a.getPath().getPathLength(), b.getPath().getPathLength());
        }
        if (a.getMesh().size() != b.getMesh().size() || !same(a.getMesh().data(), b.getMesh().data(), sizeof(LLVector4a) * a.getMesh().size()))
        {
            return "the mesh";
        }
        if (a.getNumVolumeFaces() != b.getNumVolumeFaces())
        {
            return fmt::format("{} faces against {}", a.getNumVolumeFaces(), b.getNumVolumeFaces());
        }
        for (S32 i = 0; i < a.getNumVolumeFaces(); ++i)
        {
            a.genTangents(i);
            b.genTangents(i);
            const LLVolumeFace& fa = a.getVolumeFace(i);
            const LLVolumeFace& fb = b.getVolumeFace(i);
            const std::string name = faceName(fa);
            if (fa.mID != fb.mID || fa.mTypeMask != fb.mTypeMask || fa.mBeginS != fb.mBeginS || fa.mBeginT != fb.mBeginT || fa.mNumS != fb.mNumS ||
                fa.mNumT != fb.mNumT || fa.mJoinsS != fb.mJoinsS || fa.mJoinsT != fb.mJoinsT || fa.mReversed != fb.mReversed ||
                fa.mNumVertices != fb.mNumVertices || fa.mNumIndices != fb.mNumIndices)
            {
                return fmt::format("{}: its layout", name);
            }
            if (!same(fa.mExtents, fb.mExtents, 2 * sizeof(LLVector4a)) || !same(fa.mCenter, fb.mCenter, sizeof(LLVector4a)) ||
                !same(fa.mTexCoordExtents, fb.mTexCoordExtents, sizeof(fa.mTexCoordExtents)))
            {
                return fmt::format("{}: its bounds", name);
            }
            const size_t vertices = fa.mNumVertices;
            if (!same(fa.mPositions, fb.mPositions, sizeof(LLVector4a) * vertices))
            {
                return fmt::format("{}: its positions", name);
            }
            if (!same(fa.mNormals, fb.mNormals, sizeof(LLVector4a) * vertices))
            {
                return fmt::format("{}: its normals", name);
            }
            if (!same(fa.mTexCoords, fb.mTexCoords, sizeof(LLVector2) * vertices))
            {
                return fmt::format("{}: its UVs", name);
            }
            if ((fa.mTangents == nullptr) != (fb.mTangents == nullptr) || (fa.mTangents && !same(fa.mTangents, fb.mTangents, sizeof(LLVector4a) * vertices)))
            {
                return fmt::format("{}: its tangents", name);
            }
            if (!same(fa.mIndices, fb.mIndices, sizeof(U16) * fa.mNumIndices))
            {
                return fmt::format("{}: its indices", name);
            }
        }
        return {};
    }

    // A prim is LOD-invariant where its parameters build it the same at
    // every detail: a sculpt, a mesh and a flexible prim are not; nor is a
    // prim whose path is round, twists, or tapers a profile whose hole type
    // is not the default, nor one whose profile or hole is round, nor one
    // whose hole is drawn in to its wall. Every prim of the sweep the parameters
    // call invariant is the same bytes at every LOD, and every other one
    // differs at some LOD.
    TEST_CASE("llvolume: a prim LOD-invariant by its parameters is the same bytes at every LOD", "[llvolume]")
    {
        Shape hollow_box = box();
        hollow_box.name = "hollow box";
        hollow_box.hollow = 0.5f;
        Shape tapered = box();
        tapered.name = "tapered box";
        tapered.scale_x = 0.4f;
        Shape tapered_hole = tapered;
        tapered_hole.name = "tapered box with a square hole";
        tapered_hole.hole = LL_PCODE_HOLE_SQUARE;
        tapered_hole.hollow = 0.5f;
        Shape twisted = box();
        twisted.name = "twisted box";
        twisted.twist_end = 0.5f;
        Shape turned = box();
        turned.name = "box turned a quarter turn";
        turned.twist_begin = turned.twist_end = 0.25f;
        Shape circle_hole = box();
        circle_hole.name = "box with a circle hole";
        circle_hole.hole = LL_PCODE_HOLE_CIRCLE;
        circle_hole.hollow = 0.5f;
        Shape solid_circle_hole = circle_hole;
        solid_circle_hole.name = "box with a circle hole of no size";
        solid_circle_hole.hollow = 0.f;
        Shape drawn_in = prism();
        drawn_in.name = "prism with a square hole drawn in to its wall";
        drawn_in.hole = LL_PCODE_HOLE_SQUARE;
        drawn_in.hollow = 0.85f;
        Shape clear = drawn_in;
        clear.name = "prism with a square hole clear of its wall";
        clear.hollow = 0.5f;
        Report report;
        std::vector<std::string> rejected_but_same;
        // A shape at each LOD against its LOD 0: every LOD is the same bytes
        // where its parameters call it invariant, and it is listed where they
        // do not and no LOD differs. Returns what they call it.
        auto check = [&](const Shape& s)
        {
            const bool invariant = LLVolume::isLODInvariant(s.params());
            LLPointer<LLVolume> lowest = build(s, 0);
            if (lowest->isLODInvariant() != invariant)
            {
                report.fail(fmt::format("{}: the volume and its parameters disagree", s.name));
            }
            for (S32 lod = 1; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> other = build(s, lod);
                const std::string difference = differenceBetween(*lowest, *other);
                if (!difference.empty())
                {
                    if (invariant)
                    {
                        report.fail(fmt::format("{}: LOD 0 and {} differ in {}", s.name, lod, difference));
                    }
                    return invariant;
                }
            }
            if (!invariant)
            {
                rejected_but_same.push_back(s.name);
            }
            return invariant;
        };

        struct Expected
        {
            Shape shape;
            bool invariant;
        };
        for (const Expected& expected : { Expected{ box(), true }, Expected{ hollow_box, true }, Expected{ prism(), true }, Expected{ tapered, true },
                                          Expected{ turned, true }, Expected{ solid_circle_hole, true }, Expected{ clear, true },
                                          Expected{ tapered_hole, false }, Expected{ twisted, false }, Expected{ circle_hole, false },
                                          Expected{ drawn_in, false }, Expected{ cylinder(), false }, Expected{ sphere(), false },
                                          Expected{ tube(), false } })
        {
            if (check(expected.shape) != expected.invariant)
            {
                report.fail(fmt::format("{}: {} invariant", expected.shape.name, expected.invariant ? "not" : "called"));
            }
        }
        const LLUUID asset("6d3b2a1e-4c5f-4e8a-9b7d-0f1e2d3c4b5a");
        LLVolumeParams sculpt = box().params();
        sculpt.setSculptID(asset, LL_SCULPT_TYPE_SPHERE);
        CHECK_FALSE(LLVolume::isLODInvariant(sculpt));
        LLVolumeParams mesh = box().params();
        mesh.setSculptID(asset, LL_SCULPT_TYPE_MESH);
        CHECK_FALSE(LLVolume::isLODInvariant(mesh));
        LLVolumeParams flexible = box().params();
        flexible.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_FLEXIBLE);
        CHECK_FALSE(LLVolume::isLODInvariant(flexible));

        // A volume says so only where it is shared.
        CHECK(build(box(), 0)->isLODInvariant());
        CHECK_FALSE(build(box(), 0, true)->isLODInvariant());
        CHECK_FALSE(build(cylinder(), 0)->isLODInvariant());

        // The sweep's invariant prims: its straight untwisted squares and
        // triangles, plain, cut and sheared, with no hole or one of straight
        // sides clear of the wall, and tapered with no hole or one of the
        // default type.
        S32 accepted = 0;
        S32 rejected = 0;
        for (const Shape& s : sweepShapes())
        {
            ++(check(s) ? accepted : rejected);
        }
        INFO(fmt::format("{} prims of the sweep invariant and {} not", accepted, rejected));
        INFO(fmt::format("{} not invariant but the same at every LOD{}{}", rejected_but_same.size(), rejected_but_same.empty() ? "" : ", the first ",
                         rejected_but_same.empty() ? "" : rejected_but_same.front()));
        INFO(report.text());
        CHECK(report.failures == 0);
        CHECK(rejected_but_same.empty());
        CHECK(accepted == 84);
    }

    // The volume manager refs one volume, the highest LOD's, for every LOD
    // of an LOD-invariant prim, and a volume of its own for each LOD of any
    // other; each unref returns the ref its LOD took, in any order.
    TEST_CASE("llvolume: the volume manager shares one volume between the LODs of an LOD-invariant prim", "[llvolume]")
    {
        LLVolumeMgr manager;
        Shape hollow_box = box();
        hollow_box.hollow = 0.5f;
        for (const Shape& s : { box(), hollow_box, prism(), cylinder(), torus() })
        {
            const LLVolumeParams params = s.params();
            const bool invariant = LLVolume::isLODInvariant(params);
            INFO(s.name);
            LLVolume* volumes[LOD_COUNT];
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                volumes[lod] = manager.refVolume(params, lod);
            }
            // A second object at LOD 1 shares what the first one has.
            CHECK(manager.refVolume(params, 1) == volumes[1]);
            const LLVolumeLODGroup* group = manager.getGroup(params);
            REQUIRE(group != nullptr);
            CHECK(group->getNumRefs() == LOD_COUNT + 1);
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                CHECK(volumes[lod]->isLODInvariant() == invariant);
                CHECK(volumes[lod]->getDetail() == (invariant ? LOD_DETAIL[LOD_COUNT - 1] : LOD_DETAIL[lod]));
                CHECK((volumes[lod] == volumes[LOD_COUNT - 1]) == (invariant || lod == LOD_COUNT - 1));
            }
            for (S32 lod : { 3, 1, 0, 1, 2 })
            {
                manager.unrefVolume(volumes[lod]);
            }
            CHECK(manager.getGroup(params) == nullptr);
        }
        CHECK(manager.cleanup());
    }

    // ------------------------------------------------------------------
    // Planar texgen

    // LLFace's planar projection and texture transform as its vertex loops
    // ran them, one vertex at a time along that vertex's normal, with the
    // switch as a parameter: the reference the kernel is held to.
    void perVertexProjection(LLVector2& tc, const LLVector4a& normal, const LLVector4a& vec, F32 switch_x)
    {
        LLVector4a binormal;
        F32 d = normal[0];

        if (d >= switch_x || d <= -switch_x)
        {
            if (d < 0)
            {
                binormal.set(0,-1,0);
            }
            else
            {
                binormal.set(0, 1, 0);
            }
        }
        else
        {
            if (normal[1] > 0)
            {
                binormal.set(-1,0,0);
            }
            else
            {
                binormal.set(1,0,0);
            }
        }
        LLVector4a tangent;
        tangent.setCross3(binormal,normal);

        tc.mV[1] = -((tangent.dot3(vec).getF32())*2 - 0.5f);
        tc.mV[0] = 1.0f+((binormal.dot3(vec).getF32())*2 - 0.5f);
    }

    void perVertexXform(LLVector2 &tex_coord, F32 cosAng, F32 sinAng, F32 offS, F32 offT, F32 magS, F32 magT)
    {
        // New, good way
        F32 s = tex_coord.mV[0];
        F32 t = tex_coord.mV[1];

        // Texture transforms are done about the center of the face.
        s -= 0.5;
        t -= 0.5;

        // Handle rotation
        F32 temp = s;
        s  = s     * cosAng + t * sinAng;
        t  = -temp * sinAng + t * cosAng;

        // Then scale
        s *= magS;
        t *= magT;

        // Then offset
        s += offS + 0.5f;
        t += offT + 0.5f;

        tex_coord.mV[0] = s;
        tex_coord.mV[1] = t;
    }

    // The loops, with no transform and with the texture entry's, each
    // vertex along normals[i].
    std::vector<LLVector2> perVertexTexCoords(const LLVolumeFace& vf, const LLVector4a* normals, const LLVector4a& scalea,
                                              const alsimd::TexTransform* transform, F32 switch_x)
    {
        const S32 num_vertices = vf.mNumVertices;
        std::vector<LLVector2> out(num_vertices);
        LLVector2* tex_coords0 = out.data();
        if (transform)
        {
            const F32 cos_ang = transform->cos_ang, sin_ang = transform->sin_ang;
            const F32 ms = transform->scale_s, mt = transform->scale_t, os = transform->offset_s, ot = transform->offset_t;
            for (S32 i = 0; i < num_vertices; i++)
            {
                LLVector2 tc(vf.mTexCoords[i]);
                const LLVector4a& norm = normals[i];
                LLVector4a vec = vf.mPositions[i];
                vec.mul(scalea);
                perVertexProjection(tc, norm, vec, switch_x);

                perVertexXform(tc, cos_ang, sin_ang, os, ot, ms, mt);

                *tex_coords0++ = tc;
            }
        }
        else
        {
            for (S32 i = 0; i < num_vertices; i++)
            {
                LLVector2 tc(vf.mTexCoords[i]);
                const LLVector4a& norm = normals[i];
                LLVector4a vec = vf.mPositions[i];
                vec.mul(scalea);
                perVertexProjection(tc, norm, vec, switch_x);

                *tex_coords0++ = tc;
            }
        }
        return out;
    }

#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(precise, on, push)
#endif
    // The same loops as the viewer's MSVC build compiles them in
    // llface.cpp, step by step, each step rounded where it is written.
    // 1 + (2 b.p - 0.5) folds to 2 b.p + 0.5, and -(2 t.p - 0.5) to
    // 0.5 - 2 t.p. Through the transform, s - 0.5 and t - 0.5 fold to
    // 2 b.p and -2 t.p; each turned sum fuses its first product and rounds
    // its second; and the offset is added before the 0.5.
    std::vector<LLVector2> builtTexCoords(const LLVolumeFace& vf, const LLVector4a* normals, const LLVector4a& scalea,
                                          const alsimd::TexTransform* transform)
    {
        std::vector<LLVector2> out(vf.mNumVertices);
        for (S32 i = 0; i < vf.mNumVertices; i++)
        {
            LLVector4a binormal, tangent;
            LLVolumeFace::getPlanarBasis(normals[i], 0.5f, binormal, tangent);
            F32 p[3];
            for (S32 k = 0; k < 3; ++k)
            {
                p[k] = vf.mPositions[i][k] * scalea[k];
            }
            const F32 b0 = binormal[0] * p[0], b1 = binormal[1] * p[1], b2 = binormal[2] * p[2];
            const F32 t0 = tangent[0] * p[0], t1 = tangent[1] * p[1], t2 = tangent[2] * p[2];
            const F32 b_half = (b0 + b1) + b2;
            const F32 t_half = (t0 + t1) + t2;
            const F32 db = b_half + b_half;
            const F32 dt = t_half + t_half;
            if (!transform)
            {
                out[i].set(db + 0.5f, 0.5f - dt);
                continue;
            }
            const F32 dt_sin = dt * transform->sin_ang;
            const F32 db_sin = transform->sin_ang * db;
            const F32 turned_s = std::fma(db, transform->cos_ang, -dt_sin);
            const F32 turned_t = std::fma(-dt, transform->cos_ang, -db_sin);
            const F32 scaled_s = turned_s * transform->scale_s;
            const F32 scaled_t = turned_t * transform->scale_t;
            const F32 offset_s = scaled_s + transform->offset_s;
            const F32 offset_t = scaled_t + transform->offset_t;
            out[i].set(offset_s + 0.5f, offset_t + 0.5f);
        }
        return out;
    }
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#endif

    std::vector<LLVector2> planarTexCoords(const LLVolumeFace& face, const LLVector4a& scale, const alsimd::TexTransform* transform)
    {
        std::vector<LLVector2> out(face.mNumVertices + 1, LLVector2(1234.f, 1234.f));
        face.getPlanarTexCoords(scale, transform, out[0].mV);
        REQUIRE(out.back() == LLVector2(1234.f, 1234.f));
        out.pop_back();
        return out;
    }

    bool oneNormal(const LLVolumeFace& face)
    {
        for (S32 v = 1; v < face.mNumVertices; ++v)
        {
            if (bitsOf(face.mNormals[v]) != bitsOf(face.mNormals[0]))
            {
                return false;
            }
        }
        return true;
    }

    // MSVC's rounding of the per-vertex code is what the kernel takes, so
    // there the two agree bit for bit. Another compiler fuses and
    // reassociates that code as it chooses, and agrees to within a few
    // units in the last place of the sums it is made from, of size sums.
    bool sameTexCoord(const LLVector2& a, const LLVector2& b, F32 sums)
    {
#if defined(_MSC_VER) && !defined(__clang__)
        return std::memcmp(a.mV, b.mV, sizeof(a.mV)) == 0;
#else
        auto close = [&](F32 x, F32 y) { return fabsf(x - y) <= 4.f * FLT_EPSILON * llmax(sums, fabsf(x), fabsf(y)); };
        return close(a.mV[0], b.mV[0]) && close(a.mV[1], b.mV[1]);
#endif
    }

    // The scales an object gives its volume, and the texture entry
    // transforms: none, a turn with scale and offset, and a flip.
    const LLVector4a PLANAR_SCALES[] = { LLVector4a(1.f, 1.f, 1.f), LLVector4a(0.37f, 2.5f, 11.f) };

    std::vector<std::pair<std::string, std::optional<alsimd::TexTransform>>> planarTransforms()
    {
        return { { "no transform", std::nullopt },
                 { "turned, scaled and offset", alsimd::TexTransform{ cosf(0.3f), sinf(0.3f), 2.f, 0.75f, 0.1f, -0.2f } },
                 { "flipped", alsimd::TexTransform{ cosf(2.5f), sinf(2.5f), -1.5f, 3.f, 0.45f, 0.8f } } };
    }

    // A face whose vertices share one normal is projected along it, and
    // the kernel gives every vertex what the per-vertex loops gave it as
    // the viewer's build rounds them: the flat faces, where nearly all
    // planar content is, do not move. Without a transform that is also
    // the loops' source as this file's build rounds it; through one, this
    // file's build adds the offset and the 0.5 in another order, a few
    // units of rounding apart in the size of the sums. The single-point
    // form surfaceToTexture
    // takes gives each vertex the same.
    TEST_CASE("llvolume: a face whose vertices share one normal maps planar coordinates as the per-vertex code did", "[llvolume]")
    {
        Report report;
        S32 faces = 0;
        S64 compared = 0;
        F32 worst_source = 0.f;
        const auto transforms = planarTransforms();
        for (const Shape& s : sweepShapes())
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = build(s, lod);
                for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
                {
                    const LLVolumeFace& face = volume->getVolumeFace(f);
                    if (face.mNumVertices == 0 || !oneNormal(face))
                    {
                        continue;
                    }
                    ++faces;
                    const std::string where = fmt::format("{}: {}", lodName(s, lod), faceName(face));
                    const LLVector4a planar = face.getPlanarNormal();
                    if (planar[3] != 1.f || bitsOf(planar) != bitsOf(face.mNormals[0]))
                    {
                        report.fail(fmt::format("{}: planar normal {} {} {} w {}", where, planar[0], planar[1], planar[2], planar[3]));
                        continue;
                    }
                    for (const LLVector4a& scale : PLANAR_SCALES)
                    {
                        for (const auto& [name, transform] : transforms)
                        {
                            const alsimd::TexTransform* tr = transform ? &*transform : nullptr;
                            const std::vector<LLVector2> got = planarTexCoords(face, scale, tr);
                            const std::vector<LLVector2> built = builtTexCoords(face, face.mNormals, scale, tr);
                            const std::vector<LLVector2> source = perVertexTexCoords(face, face.mNormals, scale, tr, 0.5f);
                            for (S32 v = 0; v < face.mNumVertices; ++v)
                            {
                                ++compared;
                                LLVector4a at = face.mPositions[v];
                                at.mul(scale);
                                const LLVector2 point = face.getPlanarTexCoord(at, face.mNormals[v], tr);
                                const F32 sums = llmax(1.f, 2.f * sqrtf(at.dot3(at).getF32()) * (tr ? llmax(fabsf(tr->scale_s), fabsf(tr->scale_t)) : 1.f));
                                worst_source = llmax(worst_source, llmax(fabsf(got[v].mV[0] - source[v].mV[0]), fabsf(got[v].mV[1] - source[v].mV[1])) /
                                                                       (sums * FLT_EPSILON));
                                if (!sameTexCoord(got[v], built[v], sums) || (!tr && !sameTexCoord(got[v], source[v], sums)) ||
                                    std::memcmp(point.mV, got[v].mV, sizeof(point.mV)) != 0)
                                {
                                    report.fail(fmt::format("{}, {}: vertex {} at {} {} {}: kernel {:.9g} {:.9g}, as built {:.9g} {:.9g}, source {:.9g} {:.9g}, "
                                                            "point {:.9g} {:.9g}",
                                                            where, name, v, at[0], at[1], at[2], got[v].mV[0], got[v].mV[1], built[v].mV[0],
                                                            built[v].mV[1], source[v].mV[0], source[v].mV[1], point.mV[0], point.mV[1]));
                                }
                            }
                        }
                    }
                }
            }
        }
        INFO(fmt::format("{} faces with one normal, {} coordinates compared, the source as this file builds it at most {:.2f} units of rounding apart",
                         faces, compared, worst_source));
        INFO(report.text());
        CHECK(faces > 0);
        CHECK(report.failures == 0);
        CHECK(worst_source <= 4.f);
    }

    // Whether texture coordinates are one affine function of position: the
    // planar projection along normal, evaluated in double.
    void checkAffine(const LLVolumeFace& face, const LLVector4a& normal, const LLVector4a& scale, const std::vector<LLVector2>& uvs,
                     const std::string& where, Report& report)
    {
        LLVector4a binormal, tangent;
        LLVolumeFace::getPlanarBasis(normal, LLVolumeFace::PLANAR_SWITCH_X, binormal, tangent);
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            double b = 0.0, t = 0.0, size = 1.0;
            for (S32 k = 0; k < 3; ++k)
            {
                const double p = (double)face.mPositions[v][k] * scale[k];
                b += binormal[k] * p;
                t += tangent[k] * p;
                size = std::max(size, std::fabs(2.0 * p));
            }
            const double s = 2.0 * b + 0.5;
            const double u = 0.5 - 2.0 * t;
            if (std::fabs(uvs[v].mV[0] - s) > 1e-6 * size || std::fabs(uvs[v].mV[1] - u) > 1e-6 * size)
            {
                report.fail(fmt::format("{}: vertex {} maps to {:.7g} {:.7g}, the plane gives {:.7g} {:.7g}", where, v, uvs[v].mV[0], uvs[v].mV[1], s, u));
                return;
            }
        }
    }

    // A quarter of a cylinder's side turns its normals through 90 degrees:
    // it is projected as one plane, along the middle of its turn, at every
    // LOD, and its coordinates are one affine function of position. Half a
    // cylinder turns through 180 and is projected per vertex.
    TEST_CASE("llvolume: a quarter cylinder's side is projected as one plane, its coordinates affine in position", "[llvolume]")
    {
        Report report;
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            for (const auto& [begin, end, plane] : { std::tuple{ 0.375f, 0.625f, true }, std::tuple{ 0.f, 0.25f, true },
                                                     std::tuple{ 0.f, 0.5f, false } })
            {
                Shape s = cylinder();
                s.cut_begin = begin;
                s.cut_end = end;
                s.name = fmt::format("cylinder cut {} to {}", begin, end);
                LLPointer<LLVolume> volume = build(s, lod);
                const LLVolumeFace* side = nullptr;
                for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
                {
                    const LLVolumeFace& face = volume->getVolumeFace(f);
                    if ((face.mTypeMask & LLVolumeFace::OUTER_MASK) && !(face.mTypeMask & LLVolumeFace::FLAT_MASK))
                    {
                        side = &face;
                    }
                }
                REQUIRE(side);
                const std::string where = fmt::format("{}: {}", lodName(s, lod), faceName(*side));
                const LLVector4a planar = side->getPlanarNormal();
                if ((planar[3] > 0.f) != plane)
                {
                    report.fail(fmt::format("{}: w {}", where, planar[3]));
                    continue;
                }
                if (!plane)
                {
                    continue;
                }

                // The triangles' normals weighted by area, and within the
                // cone of every vertex normal.
                LLVector3 expected = LLVector3::zero;
                for (const Triangle& tri : trianglesOf(*side))
                {
                    expected += tri.normal;
                }
                if (degreesBetween(v3(planar), expected) > 0.01f)
                {
                    report.fail(fmt::format("{}: planar normal {} {} {}, {:.3f} degrees from the triangles' sum", where, planar[0], planar[1],
                                            planar[2], degreesBetween(v3(planar), expected)));
                }
                for (S32 v = 0; v < side->mNumVertices; ++v)
                {
                    if (degreesBetween(v3(planar), v3(side->mNormals[v])) > 60.f)
                    {
                        report.fail(fmt::format("{}: vertex {} is {:.3f} degrees from the planar normal", where, v,
                                                degreesBetween(v3(planar), v3(side->mNormals[v]))));
                    }
                }
                for (const LLVector4a& scale : PLANAR_SCALES)
                {
                    checkAffine(*side, planar, scale, planarTexCoords(*side, scale, nullptr), where, report);
                }
            }
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A full cylinder side is projected per vertex. Its normals at 60 and
    // 120 degrees put x on the switch, and each must take the y binormal on
    // every row and at every LOD rather than as its rounding falls; the
    // kernel takes each vertex along the binormal the tie gives it.
    void checkRoundSideBinormals(std::initializer_list<Shape> shapes, Report& report, S32& on_switch)
    {
        for (const Shape& s : shapes)
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = build(s, lod);
                for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
                {
                    const LLVolumeFace& face = volume->getVolumeFace(f);
                    if (!isGrid(face) || (face.mTypeMask & LLVolumeFace::FLAT_MASK))
                    {
                        continue;
                    }
                    const std::string where = fmt::format("{}: {}", lodName(s, lod), faceName(face));
                    if (face.getPlanarNormal()[3] > 0.f)
                    {
                        report.fail(where + ": projected as one plane");
                        continue;
                    }
                    const F32 tie = LLVolumeFace::PLANAR_SWITCH_X - LLVolumeFace::PLANAR_TIE;
                    for (S32 col = 0; col < face.mNumS; ++col)
                    {
                        LLVector4a first, binormal, tangent;
                        LLVolumeFace::getPlanarBasis(face.mNormals[gridIndex(face, col, 0)], tie, first, tangent);
                        for (S32 row = 0; row < face.mNumT; ++row)
                        {
                            const LLVector4a& n = face.mNormals[gridIndex(face, col, row)];
                            LLVolumeFace::getPlanarBasis(n, tie, binormal, tangent);
                            if (bitsOf(binormal) != bitsOf(first))
                            {
                                report.fail(fmt::format("{}: column {} row {}, x {:.9g}, takes another binormal than row 0, x {:.9g}", where, col,
                                                        row, n[0], face.mNormals[gridIndex(face, col, 0)][0]));
                            }
                            if (fabsf(fabsf(n[0]) - LLVolumeFace::PLANAR_SWITCH_X) < 1e-3f)
                            {
                                ++on_switch;
                                if (binormal[1] == 0.f)
                                {
                                    report.fail(fmt::format("{}: column {} row {}, x {:.9g} on the switch, takes the x binormal", where, col, row, n[0]));
                                }
                            }
                        }
                    }
                    const std::vector<LLVector2> got = planarTexCoords(face, LLVector4a(1.f, 1.f, 1.f), nullptr);
                    const std::vector<LLVector2> expected = perVertexTexCoords(face, face.mNormals, LLVector4a(1.f, 1.f, 1.f), nullptr, tie);
                    for (S32 v = 0; v < face.mNumVertices; ++v)
                    {
                        const F32 sums = llmax(1.f, 2.f * sqrtf(face.mPositions[v].dot3(face.mPositions[v]).getF32()));
                        if (!sameTexCoord(got[v], expected[v], sums))
                        {
                            report.fail(fmt::format("{}: vertex {} maps to {:.9g} {:.9g} and along its tie's binormal to {:.9g} {:.9g}", where, v,
                                                    got[v].mV[0], got[v].mV[1], expected[v].mV[0], expected[v].mV[1]));
                        }
                    }
                }
            }
        }
    }

    TEST_CASE("llvolume: every row of a round side takes the same binormal at each column", "[llvolume]")
    {
        Report report;
        S32 on_switch = 0;
        Shape hollow = cylinder();
        hollow.hole = LL_PCODE_HOLE_CIRCLE;
        hollow.hollow = 0.5f;
        hollow.name = "hollow cylinder";
        checkRoundSideBinormals({ cylinder(), hollow }, report, on_switch);
        INFO(fmt::format("{} vertices on the switch", on_switch));
        INFO(report.text());
        CHECK(on_switch > 0);
        CHECK(report.failures == 0);
    }

    // A tapered side's quads are trapezoids, whose two triangles would weigh
    // unequally on normals summed from them, turning them about the axis by a
    // few tenths of a degree from row to row, past the tie at a column near
    // 60 or 120 degrees. Its surface's normals point the one way all along a
    // column.
    TEST_CASE("llvolume: every row of a tapered round side takes the same binormal at each column", "[llvolume]")
    {
        Report report;
        S32 on_switch = 0;
        Shape tapered = cylinder();
        tapered.scale_x = 0.5f;
        tapered.scale_y = 0.5f;
        tapered.name = "tapered cylinder";
        checkRoundSideBinormals({ tapered }, report, on_switch);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // surfaceToTexture takes a touched point with its normal through the
    // single-point form: at each vertex it gives what that vertex is drawn
    // with, bit for bit. It normalizes the normal again: that changes
    // nothing where the face is one plane, and elsewhere moves the point
    // by what the normal's rounding moves its tangent.
    void checkTouches(const LLVolumeFace& face, const std::string& where, Report& report, S64& touched)
    {
        const auto transforms = planarTransforms();
        const bool plane = face.getPlanarNormal()[3] > 0.f;
        for (const LLVector4a& scale : PLANAR_SCALES)
        {
            for (const auto& [name, transform] : transforms)
            {
                const alsimd::TexTransform* tr = transform ? &*transform : nullptr;
                const F32 stretch = tr ? llmax(1.f, fabsf(tr->scale_s), fabsf(tr->scale_t)) : 1.f;
                const std::vector<LLVector2> drawn = planarTexCoords(face, scale, tr);
                for (S32 v = 0; v < face.mNumVertices; ++v)
                {
                    ++touched;
                    LLVector4a at = face.mPositions[v];
                    at.mul(scale);
                    LLVector4a renormalized = face.mNormals[v];
                    renormalized.normalize3();
                    const LLVector2 exact = face.getPlanarTexCoord(at, face.mNormals[v], tr);
                    const LLVector2 touch = face.getPlanarTexCoord(at, renormalized, tr);
                    const F32 off = llmax(fabsf(touch.mV[0] - drawn[v].mV[0]), fabsf(touch.mV[1] - drawn[v].mV[1]));
                    const F32 reach = llmax(1.f, 2.f * sqrtf(at.dot3(at).getF32()) * stretch);
                    if (std::memcmp(exact.mV, drawn[v].mV, sizeof(exact.mV)) != 0 || (plane ? off != 0.f : off > 1e-5f * reach))
                    {
                        report.fail(fmt::format("{}, {}: vertex {} is drawn at {:.9g} {:.9g} and touched at {:.9g} {:.9g}, renormalized {:.9g} {:.9g}",
                                                where, name, v, drawn[v].mV[0], drawn[v].mV[1], exact.mV[0], exact.mV[1], touch.mV[0], touch.mV[1]));
                        return;
                    }
                }
            }
        }
    }

    TEST_CASE("llvolume: a touch at a vertex lands on the planar coordinate it is drawn with", "[llvolume]")
    {
        Report report;
        S64 touched = 0;
        for (const Shape& s : sweepShapes())
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = build(s, lod);
                for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
                {
                    const LLVolumeFace& face = volume->getVolumeFace(f);
                    checkTouches(face, fmt::format("{}: {}", lodName(s, lod), faceName(face)), report, touched);
                }
            }
        }
        for (U8 stitching : STITCHINGS)
        {
            LLPointer<LLVolume> volume = buildSculpt(stitching, mapFor(stitching, 64, 64), 3);
            checkTouches(volume->getVolumeFace(0), fmt::format("{} sculpt", stitchingName(stitching)), report, touched);
        }
        INFO(fmt::format("{} touches", touched));
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // Mesh faces take the same rule from their own triangles and normals:
    // the gently curved strip and grid are one plane, facing up, as the flat
    // grid is; the spheres and the cube with split normals are per vertex;
    // and every touch lands where it is drawn.
    TEST_CASE("llvolume: a mesh face takes planar coordinates by the same rule", "[llvolume]")
    {
        Report report;
        S64 touched = 0;
        for (const CorpusMesh& mesh : corpus())
        {
            LLVolumeFace face = mesh.face();
            REQUIRE(face.cacheOptimize(true));
            const LLVector4a planar = face.getPlanarNormal();
            const bool curved = mesh.name == "strip with mirrored UVs" || mesh.name == "grid of vertices held twice";
            const bool plane = curved || oneNormal(face);
            if ((planar[3] > 0.f) != plane)
            {
                report.fail(fmt::format("{}: w {} where it is {}", mesh.name, planar[3], plane ? "one plane" : "not"));
                continue;
            }
            if (curved && degreesBetween(v3(planar), LLVector3(0.f, 0.f, 1.f)) > 0.5f)
            {
                report.fail(fmt::format("{}: planar normal {} {} {}", mesh.name, planar[0], planar[1], planar[2]));
            }
            if (plane)
            {
                for (const LLVector4a& scale : PLANAR_SCALES)
                {
                    checkAffine(face, planar, scale, planarTexCoords(face, scale, nullptr), mesh.name, report);
                }
            }
            checkTouches(face, mesh.name, report, touched);
        }
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A face with no area keeps no planar normal, and one with one normal is
    // one plane along it all the same, made again at each call: it is drawn
    // and touched along that normal as a face that keeps it is, whatever the
    // touch's normal rounds to. Its normal here is off unit length, as a
    // mesh's quantized normals are, so a touch projected along its own
    // renormalized normal lands elsewhere.
    TEST_CASE("llvolume: a face with no area is drawn and touched along its one normal", "[llvolume]")
    {
        LLVolumeFace face;
        face.resizeVertices(3);
        face.resizeIndices(3);
        REQUIRE(face.mNumVertices == 3);
        REQUIRE(face.mNumIndices == 3);
        for (S32 v = 0; v < 3; ++v)
        {
            face.mPositions[v].set(0.25f, -0.375f, 0.125f);
            face.mNormals[v].set(0.f, 0.6f, 0.9f);
            face.mTexCoords[v].set(0.f, 0.f);
            face.mIndices[v] = static_cast<U16>(v);
        }
        const LLVector4a planar = face.getPlanarNormal();
        CHECK(planar[3] > 0.f);
        CHECK(std::memcmp(planar.getF32ptr(), face.mNormals[0].getF32ptr(), 3 * sizeof(F32)) == 0);

        Report report;
        S64 touched = 0;
        checkTouches(face, "a face of no area", report, touched);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A normal map planar texgen lays out shades by the frame of its own
    // coordinates. Where a face is projected as one plane those are affine
    // in position, and its planar tangents are the ones its own tangent
    // generator makes for UVs that are the planar coordinates. They have
    // the same handedness wherever a vertex normal leans less than 45
    // degrees from each of its triangles: a folded face has triangles
    // nearly edge on to their normals, whose winding in UV flips. Nor can
    // they agree at a vertex one of whose triangles lies across the
    // projection from its normal, which winds the coordinates the other
    // way, as a coarse twisted face's edge can be where its surface's
    // normal is within the cone and its last triangle is not. They have
    // the same direction to rounding on a face with one normal; on a curved
    // one the generator puts each triangle's way of constant t in the
    // vertex normal's plane, which turns it by up to about twice the angle
    // between that normal and the triangle's, within the cone. That holds
    // at an even scale. A scale that stretches one axis far past the others
    // can lay t's gradient nearly along a curved face's normals, where the
    // generator's sum swings and the vertex's own frame does not. There,
    // and where a face is projected per vertex, each tangent lies in its
    // normal's plane and runs the way s does and t does not, by the frame
    // the vertex is projected with.
    struct TangentFit
    {
        S64 compared = 0;
        F32 worst_flat = 0.f;
        F32 worst_curved = 0.f; // against the bound
    };

    void checkPlanarTangents(const LLVolumeFace& face, const std::string& where, Report& report, TangentFit& fit)
    {
        const LLVector4a planar = face.getPlanarNormal();
        const bool flat = oneNormal(face);
        const std::vector<Triangle> triangles = trianglesOf(face);
        const std::vector<bool> lone = loneVertices(face, triangles);
        // How far each vertex normal leans from its triangles'.
        std::vector<F32> lean(face.mNumVertices, 0.f);
        for (const Triangle& tri : triangles)
        {
            if (!tri.degenerate)
            {
                for (U32 v : tri.v)
                {
                    lean[v] = llmax(lean[v], degreesBetween(v3(face.mNormals[v]), tri.normal));
                }
            }
        }
        for (const LLVector4a& scale : PLANAR_SCALES)
        {
            std::vector<LLVector4a> tangents(face.mNumVertices + 1, LLVector4a(1234.f));
            face.getPlanarTangents(scale, tangents.data());
            if (bitsOf(tangents.back()) != bitsOf(LLVector4a(1234.f)))
            {
                report.fail(where + ": written past the vertices");
                return;
            }

            const bool even = scale[0] == scale[1] && scale[1] == scale[2];
            if (planar[3] > 0.f && (flat || even))
            {
                LLVolumeFace planar_uvs(face);
                const std::vector<LLVector2> uvs = planarTexCoords(face, scale, nullptr);
                std::copy(uvs.begin(), uvs.end(), planar_uvs.mTexCoords);
                ll_aligned_free_16(planar_uvs.mTangents);
                planar_uvs.mTangents = nullptr;
                planar_uvs.createTangents();
                // The vertices with a triangle across the projection from
                // their normal, by the way the coordinates' gradients wind.
                LLVector4a b, t;
                LLVolumeFace::getPlanarBasis(planar, LLVolumeFace::PLANAR_SWITCH_X, b, t);
                LLVector3 gs, gt;
                for (S32 k = 0; k < 3; ++k)
                {
                    gs.mV[k] = b[k] * scale[k];
                    gt.mV[k] = -t[k] * scale[k];
                }
                const LLVector3 winding = gs % gt;
                std::vector<bool> across(face.mNumVertices, false);
                for (const Triangle& tri : triangles)
                {
                    for (U32 v : tri.v)
                    {
                        across[v] = across[v] || (!tri.degenerate && ((tri.normal * winding > 0.f) != (v3(face.mNormals[v]) * winding > 0.f)));
                    }
                }
                for (S32 v = 0; v < face.mNumVertices; ++v)
                {
                    if (lone[v] || across[v])
                    {
                        continue;
                    }
                    ++fit.compared;
                    const LLVector4a& made = planar_uvs.mTangents[v];
                    const F32 off = degreesBetween(v3(made), v3(tangents[v]));
                    const F32 bound = flat ? 0.05f : 0.5f + 2.f * lean[v];
                    if (flat)
                    {
                        fit.worst_flat = llmax(fit.worst_flat, off);
                    }
                    else
                    {
                        fit.worst_curved = llmax(fit.worst_curved, off / bound);
                    }
                    if ((made[3] != tangents[v][3] && lean[v] < 45.f) || off > bound)
                    {
                        report.fail(fmt::format("{}: vertex {} planar tangent {} {} {} w {}, the generator's {} {} {} w {}, {:.3f} degrees apart, normal {:.1f} "
                                                "degrees from its triangles'",
                                                where, v, tangents[v][0], tangents[v][1], tangents[v][2], tangents[v][3], made[0], made[1], made[2], made[3],
                                                off, lean[v]));
                        return;
                    }
                }
                continue;
            }

            for (S32 v = 0; v < face.mNumVertices; ++v)
            {
                LLVector4a b, t;
                if (planar[3] > 0.f)
                {
                    LLVolumeFace::getPlanarBasis(planar, LLVolumeFace::PLANAR_SWITCH_X, b, t);
                }
                else
                {
                    LLVolumeFace::getPlanarBasis(face.mNormals[v], LLVolumeFace::PLANAR_SWITCH_X - LLVolumeFace::PLANAR_TIE, b, t);
                }
                LLVector3 gs, gt;
                for (S32 k = 0; k < 3; ++k)
                {
                    gs.mV[k] = b[k] * scale[k];
                    gt.mV[k] = -t[k] * scale[k];
                }
                const LLVector3 tangent = v3(tangents[v]);
                LLVector3 n = v3(face.mNormals[v]);
                n.normalize();
                const LLVector3 bitangent = (n % tangent) * tangents[v][3];
                if (fabsf(tangent.length() - 1.f) > 1e-5f || fabsf(tangent * n) > 1e-5f || !(tangent * gs > 0.f) ||
                    fabsf(tangent * gt) > 1e-5f * gt.length() || !(bitangent * gt > 0.f))
                {
                    report.fail(fmt::format("{}: vertex {} planar tangent {} {} {} w {} against s {} {} {} and t {} {} {}", where, v, tangent.mV[0],
                                            tangent.mV[1], tangent.mV[2], tangents[v][3], gs.mV[0], gs.mV[1], gs.mV[2], gt.mV[0], gt.mV[1], gt.mV[2]));
                    return;
                }
            }
        }
    }

    TEST_CASE("llvolume: planar tangents are the generator's for the planar coordinates", "[llvolume]")
    {
        Report report;
        TangentFit fit;
        for (const Shape& s : sweepShapes())
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = build(s, lod);
                for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
                {
                    const LLVolumeFace& face = volume->getVolumeFace(f);
                    checkPlanarTangents(face, fmt::format("{}: {}", lodName(s, lod), faceName(face)), report, fit);
                }
            }
        }
        for (const CorpusMesh& mesh : corpus())
        {
            LLVolumeFace face = mesh.face();
            REQUIRE(face.cacheOptimize(true));
            checkPlanarTangents(face, mesh.name, report, fit);
        }
        INFO(fmt::format("{} tangents compared with the generator's: worst {:.4f} degrees on faces with one normal, {:.3f} of the bound on curved ones",
                         fit.compared, fit.worst_flat, fit.worst_curved));
        INFO(report.text());
        CHECK(fit.compared > 0);
        CHECK(report.failures == 0);
    }

    // The planar normal is made once and kept: a copy takes it, a flexible
    // prim's partial build keeps its first build's, and a full build of the
    // face makes it again from what was built, as a new sculpt map does.
    TEST_CASE("llvolume: a face's planar normal lasts until its geometry is built again", "[llvolume]")
    {
        auto same = [](const LLVector4a& a, const LLVector4a& b) { return bitsOf(a) == bitsOf(b) && a[3] == b[3]; };

        LLPointer<LLVolume> cut = build([] { Shape s = cylinder(); s.cut_begin = 0.375f; s.cut_end = 0.625f; return s; }(), 3);
        const LLVolumeFace& side = cut->getVolumeFace(1);
        const LLVector4a planar = side.getPlanarNormal();
        REQUIRE(planar[3] == 1.f);
        LLVolumeFace copied(side);
        CHECK(same(copied.getPlanarNormal(), planar));
        LLVolumeFace assigned;
        assigned = side;
        CHECK(same(assigned.getPlanarNormal(), planar));

        // A flexible box bent across its sides: its sides' normals turn, and
        // its first build's planar normals stay until each face is built in
        // full.
        LLPointer<LLVolume> flexi = flexiVolume(LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_SAME, 0.f, 2);
        bendPath(*flexi, 4, 0.f);
        flexi->regen();
        std::vector<LLVector4a> first;
        for (S32 f = 0; f < flexi->getNumVolumeFaces(); ++f)
        {
            first.push_back(flexi->getVolumeFace(f).getPlanarNormal());
        }
        bendPath(*flexi, 4, 1.1f);
        flexi->regen();
        bool bent = false;
        for (S32 f = 0; f < flexi->getNumVolumeFaces(); ++f)
        {
            CHECK(same(flexi->getVolumeFace(f).getPlanarNormal(), first[f]));
            bent = bent || !oneNormal(flexi->getVolumeFace(f));
        }
        CHECK(bent);
        // A copy of a bent face takes the planar normal its first build made,
        // not one made again from the bent geometry.
        for (S32 f = 0; f < flexi->getNumVolumeFaces(); ++f)
        {
            LLVolumeFace copied_bent(flexi->getVolumeFace(f));
            CHECK(same(copied_bent.getPlanarNormal(), first[f]));
            LLVolumeFace assigned_bent;
            assigned_bent = flexi->getVolumeFace(f);
            CHECK(same(assigned_bent.getPlanarNormal(), first[f]));
        }
        LLPointer<LLVolume> fresh = flexiVolume(LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_SAME, 0.f, 2);
        bendPath(*fresh, 4, 1.1f);
        fresh->regen();
        bool changed = false;
        for (S32 f = 0; f < flexi->getNumVolumeFaces(); ++f)
        {
            LLVolumeFace& face = flexi->getVolumeFace(f);
            REQUIRE(face.create(flexi, false));
            CHECK(same(face.getPlanarNormal(), fresh->getVolumeFace(f).getPlanarNormal()));
            changed = changed || !same(face.getPlanarNormal(), first[f]);
        }
        CHECK(changed);

        // A sculpt given a new map keeps its layout and is built again.
        LLPointer<LLVolume> sculpt = buildSculpt(LL_SCULPT_TYPE_PLANE, planeMap(64, 64), 3);
        const LLVector4a wavy = sculpt->getVolumeFace(0).getPlanarNormal();
        const SculptMap flat_map = sculptMap(64, 64, [](U16 x, U16 y) { return LLVector3(x / 63.f - 0.5f, y / 63.f - 0.5f, 0.f); });
        sculpt->sculpt(flat_map.width, flat_map.height, 3, flat_map.rgb.data(), 0, true);
        LLPointer<LLVolume> flat = buildSculpt(LL_SCULPT_TYPE_PLANE, flat_map, 3);
        CHECK(same(sculpt->getVolumeFace(0).getPlanarNormal(), flat->getVolumeFace(0).getPlanarNormal()));
        CHECK(!same(sculpt->getVolumeFace(0).getPlanarNormal(), wavy));
    }

    // How the sweep's faces fall against the cone: the share projected as
    // one plane at each half-angle, and the faces whose normals all agree
    // to rounding without being one normal.
    TEST_CASE("llvolume: planar texgen's cone over the sweep", "[llvolume][planar][.]")
    {
        const F32 cones[] = { 30.f, 45.f, 60.f, 75.f, 89.f };
        S64 counts[std::size(cones)] = {};
        S64 total = 0, one_normal = 0, near_flat = 0, near_flat_moved = 0, empty = 0;
        F32 near_flat_worst = 0.f;
        std::vector<std::string> moved_examples;
        for (const Shape& s : sweepShapes())
        {
            for (S32 lod = 0; lod < LOD_COUNT; ++lod)
            {
                LLPointer<LLVolume> volume = build(s, lod);
                for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
                {
                    const LLVolumeFace& face = volume->getVolumeFace(f);
                    ++total;
                    if (oneNormal(face))
                    {
                        ++one_normal;
                        for (S64& count : counts)
                        {
                            ++count;
                        }
                        continue;
                    }
                    const LLVector4a planar = face.getPlanarNormal();
                    if (!(planar.dot3(planar).getF32() > 0.f))
                    {
                        ++empty;
                        continue;
                    }
                    F32 widest = 0.f;
                    for (S32 v = 0; v < face.mNumVertices; ++v)
                    {
                        widest = llmax(widest, degreesBetween(v3(face.mNormals[v]), v3(planar)));
                    }
                    if (widest < 1e-3f)
                    {
                        ++near_flat;
                        const std::vector<LLVector2> got = planarTexCoords(face, LLVector4a(1.f, 1.f, 1.f), nullptr);
                        const std::vector<LLVector2> old = perVertexTexCoords(face, face.mNormals, LLVector4a(1.f, 1.f, 1.f), nullptr, 0.5f);
                        F32 moved = 0.f;
                        for (S32 v = 0; v < face.mNumVertices; ++v)
                        {
                            moved = llmax(moved, (got[v] - old[v]).length());
                        }
                        near_flat_worst = llmax(near_flat_worst, moved);
                        if (moved > 1e-4f)
                        {
                            ++near_flat_moved;
                            if (moved_examples.size() < 12)
                            {
                                F32 low = face.mNormals[0][0], high = low;
                                for (S32 v = 1; v < face.mNumVertices; ++v)
                                {
                                    low = llmin(low, face.mNormals[v][0]);
                                    high = llmax(high, face.mNormals[v][0]);
                                }
                                moved_examples.push_back(fmt::format("  {}: {}, moves {:.3g}, normal x {:.9g} to {:.9g}\n", lodName(s, lod),
                                                                     faceName(face), moved, low, high));
                            }
                        }
                    }
                    for (size_t c = 0; c < std::size(cones); ++c)
                    {
                        counts[c] += widest <= cones[c];
                    }
                }
            }
        }
        std::string table = fmt::format("{} faces: {} with one normal, {} flat to rounding ({} moved past 1e-4, worst {:.3g}), {} with no planar normal\n",
                                        total, one_normal, near_flat, near_flat_moved, near_flat_worst, empty);
        for (const std::string& example : moved_examples)
        {
            table += example;
        }
        for (size_t c = 0; c < std::size(cones); ++c)
        {
            table += fmt::format("  within {:2.0f} degrees: {:6} faces, {:5.1f}%, {} curved\n", cones[c], counts[c], 100.0 * counts[c] / total,
                                 counts[c] - one_normal - near_flat);
        }
        WARN(table);
    }
}
