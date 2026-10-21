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
// What the generator gets wrong today is a case of its own, tagged
// [!shouldfail]: the case holds the generator to what it should do, so it
// fails, as expected, while the defect stands, and the fix that clears the
// defect removes the tag. Where a defect breaks one of the sweep's checks on
// some faces, the sweep leaves that check on those faces to the defect's
// case: a region below names the faces by what they are, not by how they
// come out, and the fix removes the region with the tag.

#include "linden_common.h"

#include "llvolume.h"
#include "llmath.h"
#include "llquaternion.h"
#include "m3math.h"
#include "v2math.h"
#include "v3math.h"
#include "v4math.h"
#include "llvector4a.h"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <numeric>
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
        return acosf(llclamp((a * b) / (la * lb), -1.f, 1.f)) * RAD_TO_DEG;
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

    // A flat inner side copies every profile point, so that each is a hard
    // edge: the quad between a point and its copy has no width.
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

    bool isRoundOrTriangle(U8 profile)
    {
        return profile != LL_PCODE_PROFILE_SQUARE;
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

        // What the sweep holds every face to. RawUnit is broken on every
        // side today, and Orthogonal on most curved ones: each has a case of
        // its own.
        constexpr U32 Sweep = Layout | Finite | Inversion | CapFacing | AffineUV | Oracle | SeamsAgree | HardEdges | Tangents;
    }

    namespace Regions
    {
        // A side of a closed profile whose first and last columns are
        // apart, as each side of a box is, and of more than two rows, where
        // the S wrap runs: it averages the side's two edges together.
        constexpr U32 OpenSWrap = 1 << 0;
        // A side whose path's openness disagrees with its rows. A path
        // that counts as closed but whose first and last rows are apart, a
        // torus at one and a half turns, has the T wrap sum rows half a
        // turn apart; one that counts as open but whose rows meet, a
        // skewed circle2 path, is never wrapped.
        constexpr U32 TWrapByOpenness = 1 << 1;
        // A cap with no area, which has no triangle to take a normal
        // from.
        constexpr U32 CapNoArea = 1 << 2;
        // A square hole above 0.707 in a round or triangular profile,
        // whose corners go through the wall.
        constexpr U32 SquareHole = 1 << 3;
        // A smooth closed side of two rows: the S wrap's test for a pole
        // compares the first row with itself and passes, so the seam is
        // not wrapped.
        constexpr U32 TwoRowSeam = 1 << 4;
        // A vertex in no triangle with area sums to no normal, or to the
        // rounding left of one, which createTangents' normalize turns into
        // NaN or infinity where its copies are exactly one point: the copied
        // last column of a cut flat inner side, a half circle's square or
        // triangle hole on its axis, the middle of a sculpt's pinch.
        constexpr U32 ZeroNormal = 1 << 5;
        // A cap with a centre vertex on a twisted path: the centre takes the
        // middle of the cap's bounding box and the middle of its UVs', which
        // a rotation makes two different points.
        constexpr U32 TwistedCapHub = 1 << 6;
        // A cap of a twisted sphere: the twist turns the half disc to the
        // other side of the axis it sweeps about, which reverses the way
        // the sphere leaves the cap, but not the cap.
        constexpr U32 InsideOutCap = 1 << 7;
    }

    U32 excusedBy(U32 regions)
    {
        U32 checks = 0;
        if (regions & Regions::OpenSWrap)
        {
            checks |= Checks::Oracle | Checks::Inversion;
        }
        if (regions & Regions::TWrapByOpenness)
        {
            checks |= Checks::Oracle | Checks::Inversion | Checks::SeamsAgree | Checks::HardEdges;
        }
        if (regions & Regions::CapNoArea)
        {
            checks |= Checks::Finite | Checks::RawUnit | Checks::CapFacing | Checks::Tangents | Checks::Orthogonal;
        }
        if (regions & Regions::SquareHole)
        {
            checks |= Checks::Inversion | Checks::CapFacing | Checks::Oracle;
        }
        if (regions & Regions::TwoRowSeam)
        {
            checks |= Checks::SeamsAgree;
        }
        if (regions & Regions::TwistedCapHub)
        {
            checks |= Checks::AffineUV;
        }
        if (regions & Regions::InsideOutCap)
        {
            checks |= Checks::CapFacing;
        }
        return checks;
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

    U32 regionsOf(const LLVolume& volume, const LLVolumeFace& face, const std::vector<Triangle>& triangles, const std::vector<bool>& lone)
    {
        U32 regions = 0;
        const LLVolumeParams& params = volume.getParams();
        const U8 profile = params.getProfileParams().getCurveType() & LL_PCODE_PROFILE_MASK;
        const U8 hole = params.getProfileParams().getCurveType() & LL_PCODE_HOLE_MASK;
        const bool twisted = params.getPathParams().getTwistBegin() != params.getPathParams().getTwist();
        if (!isSculpt(volume) && hole == LL_PCODE_HOLE_SQUARE && params.getHollow() > 0.70711f && isRoundOrTriangle(profile))
        {
            regions |= Regions::SquareHole;
        }
        if (isCap(face))
        {
            if (std::all_of(triangles.begin(), triangles.end(), [](const Triangle& t) { return t.degenerate; }))
            {
                regions |= Regions::CapNoArea;
            }
            if (twisted && !(face.mTypeMask & (LLVolumeFace::HOLLOW_MASK | LLVolumeFace::OPEN_MASK)))
            {
                regions |= Regions::TwistedCapHub;
            }
            if (twisted && profile == LL_PCODE_PROFILE_CIRCLE_HALF && params.getPathParams().getCurveType() != LL_PCODE_PATH_LINE)
            {
                regions |= Regions::InsideOutCap;
            }
        }
        else if (!isSculpt(volume) && isGrid(face))
        {
            const bool profile_closed = !volume.getProfile().isOpen();
            const bool columns_meet = columnsMeet(face);
            if (profile_closed && face.mNumT > 2 && !columns_meet)
            {
                regions |= Regions::OpenSWrap;
            }
            if (volume.getPath().isOpen() == rowsMeet(face))
            {
                regions |= Regions::TWrapByOpenness;
            }
            if (columns_meet && face.mNumT == 2 && !isDoubled(face))
            {
                regions |= Regions::TwoRowSeam;
            }
        }
        if (std::find(lone.begin(), lone.end(), true) != lone.end())
        {
            regions |= Regions::ZeroNormal;
        }
        return regions;
    }

    struct Inspect
    {
        U32 checks = Checks::Sweep;
        // Nonzero: inspect only the faces in these regions, nothing of them
        // excused. Either way, on each face the checks that its other
        // regions break are excused.
        U32 only_regions = 0;
        // How far a cap may lean from the direction its wall runs in: the
        // cosine of the widest angle allowed.
        F32 cap_facing = 0.f;
        // Check inversion also where the surface folds at a point.
        bool inversion_any_spread = false;
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
            }
            if (face.mNumIndices != (face.mNumS - 1) * (face.mNumT - 1) * 6)
            {
                fail(fmt::format("{} indices for a grid of {} by {}", face.mNumIndices, face.mNumS, face.mNumT));
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

    // A vertex in skip is left to the ZeroNormal region's case.
    void checkRawUnit(const LLVolumeFace& face, const std::vector<LLVector3>& normals, const std::vector<bool>& skip, Report& report, const std::string& where)
    {
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const F32 length = normals[v].length();
            if (skip[v])
            {
                continue;
            }
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
    // normal facing away from one was made of something else. Beyond a
    // right angle the surface folds at the point, and whether its normal
    // faces each triangle depends on the weights; any_spread checks there
    // too.
    void checkInversion(const LLVolumeFace& face, const std::vector<Triangle>& triangles, const std::vector<LLVector3>& normals, bool any_spread,
                        Report& report, const std::string& where)
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
                if ((any_spread || spread[v] <= 90.f) && !(n * tri.normal > 0.f))
                {
                    report.fail(fmt::format("{}: triangle {} is {:.1f} degrees from its vertex {}'s normal, where the triangles span {:.1f}", where, t,
                                            degreesBetween(tri.normal, n), v, spread[v]));
                    return;
                }
            }
        }
    }

    // A cap faces along the path, away from the wall it closes: from the
    // ring of the station next to it toward its own.
    void checkCapFacing(const LLVolume& volume, const LLVolumeFace& face, const std::vector<LLVector3>& normals, F32 min_cos, Report& report,
                        const std::string& where)
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
            if (!(n * away > min_cos * n.length() * away.length()))
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
    void checkOracle(const LLVolumeFace& face, const std::vector<Triangle>& triangles, const std::vector<LLVector3>& normals, Report& report,
                     const std::string& where)
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
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const std::vector<U32>& around = incident[groups[v]];
            if (around.empty() || std::any_of(around.begin(), around.end(), [&](U32 t) { return triangles[t].degenerate; }))
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
    void checkSeamsAgree(const LLVolume& volume, const LLVolumeFace& face, const std::vector<LLVector3>& normals, const std::vector<bool>& skip,
                         Report& report, const std::string& where)
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
            return skip[a] || skip[b] || off <= 1.f;
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

    // A flat inner side is hard at every point. Its columns run p0, then
    // each later point twice, then p0 again (or the last point, where the
    // profile is cut), so the quads after even columns are the real ones
    // and the quads after odd columns have no width: a vertex takes its
    // normal from the one strip of real quads beside it, up and down the
    // path, and from no other strip at its point.
    S32 stripOf(S32 column)
    {
        return column % 2 == 0 ? column : column - 1;
    }

    void checkHardEdges(const LLVolumeFace& face, const std::vector<Triangle>& triangles, const std::vector<LLVector3>& normals,
                        const std::vector<bool>& skip, Report& report, const std::string& where)
    {
        if (!isDoubled(face) || !isGrid(face))
        {
            return;
        }
        const std::vector<U32> groups = weldGroups(face);
        std::map<std::pair<U32, S32>, std::vector<U32>> in_strip;
        for (size_t t = 0; t < triangles.size(); ++t)
        {
            if (triangles[t].degenerate)
            {
                continue;
            }
            S32 strip = face.mNumS;
            for (U32 v : triangles[t].v)
            {
                strip = llmin(strip, (S32)(v % face.mNumS));
            }
            for (U32 v : triangles[t].v)
            {
                in_strip[{ groups[v], strip }].push_back((U32)t);
            }
        }
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const S32 column = v % face.mNumS;
            if (skip[v])
            {
                continue;
            }
            const auto found = in_strip.find({ groups[v], stripOf(column) });
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

    void checkTangents(const LLVolumeFace& face, const std::vector<bool>& skip, U32 checks, Report& report, const std::string& where)
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
            if (skip[v])
            {
                continue;
            }
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

    // Every check the inspection asks for, on every face of the volume.
    // Tangents come last: genTangents normalizes the normals it is given.
    void inspect(LLVolume& volume, const std::string& name, const Inspect& how, Report& report)
    {
        const S32 faces = volume.getNumVolumeFaces();
        struct State
        {
            bool inspected = false;
            U32 checks = 0;
            U32 regions = 0;
            std::vector<LLVector3> raw;
            std::vector<bool> skip;
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
            const std::vector<bool> lone = loneVertices(face, triangles);
            State& state = states[i];
            state.regions = regionsOf(volume, face, triangles, lone);
            if (how.only_regions && !(state.regions & how.only_regions))
            {
                continue;
            }
            state.inspected = true;
            // A face in another defect's region still has that defect's
            // checks excused.
            state.checks = how.checks & ~excusedBy(state.regions & ~how.only_regions);
            state.raw.resize(face.mNumVertices);
            for (S32 v = 0; v < face.mNumVertices; ++v)
            {
                state.raw[v] = v3(face.mNormals[v]);
            }
            // The ZeroNormal region is of vertices, not faces: everything
            // else passes over those vertices alone.
            state.skip = (how.only_regions & Regions::ZeroNormal) ? std::vector<bool>(face.mNumVertices, false) : lone;

            if (state.checks & Checks::Finite)
            {
                checkFinite(face, state.raw, report, where);
            }
            if (state.checks & Checks::RawUnit)
            {
                checkRawUnit(face, state.raw, state.skip, report, where);
            }
            if (state.checks & Checks::Inversion)
            {
                checkInversion(face, triangles, state.raw, how.inversion_any_spread, report, where);
            }
            if ((state.checks & Checks::CapFacing) && isCap(face))
            {
                checkCapFacing(volume, face, state.raw, how.cap_facing, report, where);
            }
            // A sculpt's UVs are its grid, wherever the map puts the points.
            if ((state.checks & Checks::AffineUV) && !isSculpt(volume))
            {
                checkAffineUV(face, triangles, report, where);
            }
            if (state.checks & Checks::Oracle)
            {
                checkOracle(face, triangles, state.raw, report, where);
            }
            if (state.checks & Checks::SeamsAgree)
            {
                checkSeamsAgree(volume, face, state.raw, state.skip, report, where);
            }
            if (state.checks & Checks::HardEdges)
            {
                checkHardEdges(face, triangles, state.raw, state.skip, report, where);
            }
        }

        for (S32 i = 0; i < faces; ++i)
        {
            const State& state = states[i];
            if (!state.inspected || !(state.checks & (Checks::Tangents | Checks::Orthogonal)))
            {
                continue;
            }
            volume.genTangents(i);
            const LLVolumeFace& face = volume.getVolumeFace(i);
            checkTangents(face, state.skip, state.checks, report, fmt::format("{}: {}", name, faceName(face)));
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

    std::vector<CorpusMesh> corpus()
    {
        return { uvSphere(24, 12), splitCube(), mirroredStrip(), riggedSphere(), degenerateGrid(), uvSphere(64, 32) };
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

    // cacheOptimize keeps every triangle, and every corner's position,
    // normal, UV and weights, while it welds and reorders; it gives every
    // vertex a unit tangent orthogonal to its normal, whose w follows the
    // UV winding of each triangle the vertex is in.
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

        for (S32 t = 0; t < (S32)out.size(); ++t)
        {
            if (out[t].degenerate)
            {
                continue;
            }
            S32 out_first = 0;
            const TriangleKey key = keyOf(after, t, &out_first);
            const S32 source = by_key.find(key)->second;
            S32 in_first = 0;
            keyOf(before, source, &in_first);
            const LLVector2 uv0 = after.mTexCoords[after.mIndices[t * 3]];
            const LLVector2 uv1 = after.mTexCoords[after.mIndices[t * 3 + 1]];
            const LLVector2 uv2 = after.mTexCoords[after.mIndices[t * 3 + 2]];
            const F32 uv_area = (uv1.mV[0] - uv0.mV[0]) * (uv2.mV[1] - uv0.mV[1]) - (uv2.mV[0] - uv0.mV[0]) * (uv1.mV[1] - uv0.mV[1]);
            for (S32 k = 0; k < 3; ++k)
            {
                const U16 o = after.mIndices[t * 3 + (out_first + k) % 3];
                const U16 i = before.mIndices[source * 3 + (in_first + k) % 3];
                if ((v3(after.mNormals[o]) - v3(before.mNormals[i])).length() > 1e-5f || !(after.mTexCoords[o] == before.mTexCoords[i]))
                {
                    report.fail(fmt::format("{}: triangle {} corner {} lost its normal or UV", where, t, k));
                    return;
                }
                if (before.mWeights && !(after.mWeights && v3(after.mWeights[o]) == v3(before.mWeights[i]) && after.mWeights[o][3] == before.mWeights[i][3]))
                {
                    report.fail(fmt::format("{}: triangle {} corner {} lost its weights", where, t, k));
                    return;
                }
            }
            if (!after.mTangents)
            {
                report.fail(fmt::format("{}: no tangents", where));
                return;
            }
            const F32 geometric = out[t].normal * v3(after.mNormals[after.mIndices[t * 3]]);
            const F32 expected_w = (uv_area > 0.f) == (geometric > 0.f) ? 1.f : -1.f;
            for (S32 k = 0; k < 3 && fabsf(uv_area) > 1e-6f; ++k)
            {
                const U16 o = after.mIndices[t * 3 + k];
                if (after.mTangents[o][3] != expected_w)
                {
                    report.fail(fmt::format("{}: triangle {}'s UVs wind for w = {} but vertex {} has {}", where, t, expected_w, o, after.mTangents[o][3]));
                    return;
                }
            }
        }

        for (S32 v = 0; v < after.mNumVertices; ++v)
        {
            const LLVector3 n = v3(after.mNormals[v]);
            const LLVector3 t = after.mTangents ? v3(after.mTangents[v]) : LLVector3::zero;
            const F32 w = after.mTangents ? after.mTangents[v][3] : 0.f;
            if (!(fabsf(n.length() - 1.f) <= UNIT_TOLERANCE) || !(fabsf(t.length() - 1.f) <= UNIT_TOLERANCE) || !(w == 1.f || w == -1.f) ||
                !(fabsf(n * t) <= 2e-3f))
            {
                report.fail(fmt::format("{}: vertex {} has normal length {}, tangent length {}, w {} and n.t {}", where, v, n.length(), t.length(), w, n * t));
                return;
            }
        }
    }

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
    // volume is rebuilt in place, and its faces' tangents are taken again
    // where a face lost them. The rebuild must come out as a fresh volume
    // built in full on the moved path, in the buffers it had.
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

    // A map whose every pixel is one colour has no area, and makes the
    // sphere above LOD 0, where the area is tested.
    SculptMap oneColourMap()
    {
        return sculptMap(32, 32, [](U16, U16) { return LLVector3(0.1f, 0.1f, 0.1f); });
    }

    // The sphere a sculpt shows while its map is missing or has no area,
    // and the empty one it shows otherwise.
    void inspectPlaceholders(const Inspect& how, bool spheres_only, Report& report)
    {
        for (S32 lod = 0; lod < LOD_COUNT; ++lod)
        {
            for (bool visible : { true, false })
            {
                if (spheres_only && !visible)
                {
                    continue;
                }
                LLPointer<LLVolume> volume = buildSculpt(LL_SCULPT_TYPE_SPHERE, SculptMap(), lod, visible);
                const std::string name = fmt::format("{} placeholder at LOD {}", visible ? "sphere" : "empty", lod);
                if (volume->getSculptLevel() != -1 || volume->getNumVolumeFaces() != 1)
                {
                    report.fail(fmt::format("{}: sculpt level {} and {} faces", name, volume->getSculptLevel(), volume->getNumVolumeFaces()));
                    continue;
                }
                inspect(*volume, name, how, report);
            }
            if (!spheres_only || lod > 0)
            {
                inspectSculpt(LL_SCULPT_TYPE_SPHERE, oneColourMap(), lod, "one colour", how, report);
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

    // Today's figures for each class, so that no later change orders a
    // class's indices or vertices worse than it is ordered now.
    TEST_CASE("llvolume: vertex cache and fetch figures stay within today's bounds", "[llvolume]")
    {
        struct Bound
        {
            const char* name;
            F64 acmr[LOD_COUNT];
            F64 overfetch[LOD_COUNT];
        };
        // Vertices transformed a triangle with a 16-entry FIFO cache, and
        // bytes fetched over the buffer's, per LOD.
        const Bound bounds[] = {
            { "box", { 2.0000, 2.0000, 1.1250, 0.8889 }, { 1.2000, 1.2000, 1.0667, 1.0000 } },
            { "cylinder", { 1.2500, 1.1667, 1.0889, 1.0625 }, { 1.0133, 1.0286, 1.0146, 1.0211 } },
            { "sphere", { 0.7778, 0.6944, 0.6429, 1.0833 }, { 1.0286, 1.0240, 1.0000, 1.0043 } },
            { "torus", { 0.6806, 1.1111, 1.0667, 1.0417 }, { 1.0122, 1.0080, 1.0000, 1.0010 } },
            { "sculpt sphere", { 0.6806, 1.1250, 1.0625, 1.0312 }, { 1.0122, 1.0074, 1.0021, 1.0006 } },
            { "sculpt plane", { 0.6806, 1.1250, 1.0625, 1.0312 }, { 1.0122, 1.0074, 1.0021, 1.0006 } },
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
                INFO(fmt::format("{} at LOD {}: {:.4f} transformed a triangle, overfetch {:.4f}", name, lod, figures.acmr(), figures.overfetch()));
                CHECK(figures.acmr() <= bound.acmr[lod] + 1e-4);
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
            { "grid with degenerate triangles", 0.8158, 1.0207 },
            { "UV sphere 64 by 32", 0.6557, 1.0005 },
        };
        for (const CorpusMesh& mesh : corpus())
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

    // ------------------------------------------------------------------
    // Today's defects

    // The even-out switch adds each triangle's normal a second time to
    // its top-right vertex, so a round prim's vertex normals lean around
    // the axis, by an amount that depends on the row.
    TEST_CASE("llvolume: a cylinder's side normals point straight out from its axis", "[llvolume][!shouldfail]")
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

    // The double count goes to whichever vertex is top right in
    // the grid, so a mirrored sculpt, whose rows run the other way, does
    // not get the mirror image of the plain one's normals.
    TEST_CASE("llvolume: a mirrored sculpt's normals mirror the plain one's", "[llvolume][!shouldfail]")
    {
        const SculptMap map = torusMap(64, 64);
        LLPointer<LLVolume> plain = buildSculpt(LL_SCULPT_TYPE_TORUS, map, 3);
        LLPointer<LLVolume> mirrored = buildSculpt((U8)(LL_SCULPT_TYPE_TORUS | LL_SCULPT_FLAG_MIRROR), map, 3);
        const LLVolumeFace& a = plain->getVolumeFace(0);
        const LLVolumeFace& b = mirrored->getVolumeFace(0);
        REQUIRE(a.mNumS == b.mNumS);
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
        INFO(fmt::format("the mirrored normals are up to {:.2f} degrees from the mirror image", worst));
        CHECK(worst < 0.1f);
    }

    // Where a smooth side creases sharply, as a flattened torus
    // tube does at LOD 0, the double count's weights turn a normal past
    // the triangles on the crease's light side. Equal weights cannot: a
    // crease turns by less than 180 degrees, and its normal by half that
    // from each side.
    TEST_CASE("llvolume: a torus's normals face every triangle at a sharp crease", "[llvolume][!shouldfail]")
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

    // Every side of a closed profile has its first and last columns
    // summed, so on a twisted box or tube each side's two edges take one
    // averaged normal. A half cylinder's side, from one pole of its arc to
    // the other, sums two opposite rims at LOD 2 and 3, as a skewed
    // sphere's sums its poles, and their normals turn over.
    TEST_CASE("llvolume: the S wrap joins only edges that meet", "[llvolume][!shouldfail]")
    {
        Inspect how;
        how.checks = Checks::Oracle | Checks::Inversion;
        how.only_regions = Regions::OpenSWrap;
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

    // The T wrap trusts the path's openness, which ignores revolutions:
    // at one and a half turns the first and last rows are half a turn
    // apart, and their normals are summed. The other way, a skewed circle2
    // path counts as open though its rows meet, and is never wrapped.
    TEST_CASE("llvolume: the T wrap follows whether the first and last rows meet", "[llvolume][!shouldfail]")
    {
        Inspect how;
        how.checks = Checks::Oracle | Checks::Inversion | Checks::SeamsAgree | Checks::HardEdges;
        how.only_regions = Regions::TWrapByOpenness;
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

    // A cap's normal is the first triangle's, with +/-Z when that has
    // no area; the uncut cube cap normalizes a zero cross product. A cap
    // with no area, as on a pyramid or a prism, still faces along the path.
    TEST_CASE("llvolume: a cap with no area still faces away from its wall", "[llvolume][!shouldfail]")
    {
        Inspect how;
        how.checks = Checks::Finite | Checks::CapFacing | Checks::Tangents;
        how.only_regions = Regions::CapNoArea;
        how.cap_facing = 0.5f;
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

    // Lengyel's Gram-Schmidt step runs on the normals before
    // createTangents normalizes them, removing only |n|^2 of the normal's
    // part, so a tangent leans out of the plane its normal makes.
    TEST_CASE("llvolume: every prim tangent is orthogonal to its normal", "[llvolume][!shouldfail]")
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

    // A triangle whose UVs have no area gets an invented ratio of
    // 1024 and outweighs its real neighbours, and one whose positions have
    // no area still adds a tangent. Neither has a tangent to give.
    TEST_CASE("llvolume: a triangle with no area or no UV area adds nothing to a tangent", "[llvolume][!shouldfail]")
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

    // A side's normals are area sums, and are unit length only once
    // createTangents has normalized them.
    TEST_CASE("llvolume: normals are unit length straight out of generation", "[llvolume][!shouldfail]")
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
    // 0.7, and the hole's corners, at 0.707 of the hollow, go through the
    // wall above it: the hole's points must stay inside the wall.
    TEST_CASE("llvolume: a square hole above 0.707 stays inside its wall", "[llvolume][!shouldfail]")
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
        Inspect how;
        how.checks = Checks::Inversion | Checks::CapFacing | Checks::Oracle;
        how.only_regions = Regions::SquareHole;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // Partial builds: a flexible prim's every frame rebuilds its volume in
    // place, and must come out as a full build would, in the buffers it had.
    TEST_CASE("llvolume: a flexible box's partial build equals a full one", "[llvolume][!shouldfail]")
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

    TEST_CASE("llvolume: a flexible cylinder's partial build equals a full one", "[llvolume][!shouldfail]")
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

    TEST_CASE("llvolume: a flexible box with a square hole has a partial build equal to a full one", "[llvolume][!shouldfail]")
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

    // The uncut cube cap is built only on a line path, which flexi never
    // has; its partial build is the one a regen of an unmoved box makes.
    TEST_CASE("llvolume: an uncut cube cap's partial build keeps its buffers", "[llvolume][!shouldfail]")
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

    // createSide takes a side's first column as a pole where it meets the
    // row before the last; on a side of two rows that row is the first, so
    // every closed side of two rows passes, and its seam is never wrapped:
    // a cylinder's seam at LOD 0 and 1 is two one-sided normals.
    TEST_CASE("llvolume: a closed side of two rows has one normal at its seam", "[llvolume][!shouldfail]")
    {
        Inspect how;
        how.checks = Checks::SeamsAgree;
        how.only_regions = Regions::TwoRowSeam;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A general cap's centre vertex takes the middle of the cap's bounding
    // box for its position and the middle of the UVs' box for its UV. A
    // twist rotates the cap, and the two are no longer one point: the
    // texture slides at the centre of every twisted cap.
    TEST_CASE("llvolume: a twisted cap's centre vertex has the UV of its position", "[llvolume][!shouldfail]")
    {
        Inspect how;
        how.checks = Checks::AffineUV;
        how.only_regions = Regions::TwistedCapHub;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A sphere's half disc sweeps about the axis it touches. A twist turns
    // it about its own centre on that axis, to the other side of the axis,
    // and the sphere then leaves the cap the other way; the cap's winding
    // and normal stay, and the cap faces into the sphere.
    TEST_CASE("llvolume: a twisted sphere's caps face out of the sphere", "[llvolume][!shouldfail]")
    {
        Inspect how;
        how.checks = Checks::CapFacing;
        how.only_regions = Regions::InsideOutCap;
        Report report;
        S32 volumes = 0;
        inspectSweep(how, report, volumes);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // The sphere placeholder runs u around along the path and v pole to
    // pole along the profile, the other way round from a sculpt map, but
    // gets a sphere sculpt's normal stitching: the poles' average lands on
    // one meridian, and the S wrap sums the north pole with the south.
    TEST_CASE("llvolume: the sphere placeholder's normals follow its surface", "[llvolume][!shouldfail]")
    {
        Inspect how;
        how.checks = Checks::Inversion | Checks::Oracle;
        Report report;
        inspectPlaceholders(how, true, report);
        INFO(report.text());
        CHECK(report.failures == 0);
    }

    // A vertex whose every triangle has no area sums to a zero normal, and
    // createTangents' normalize3fast makes it NaN: the copied last column
    // of a cut flat inner side, the inner side of a half circle with a
    // square or triangle hole, the middle of a sculpt's pinch, the whole of
    // the empty placeholder.
    TEST_CASE("llvolume: a vertex with only zero-area triangles gets a finite normal", "[llvolume][!shouldfail]")
    {
        Inspect how;
        how.checks = Checks::Tangents;
        how.only_regions = Regions::ZeroNormal;
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
}
