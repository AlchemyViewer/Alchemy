/**
 * @file raytrace.h
 * @brief Ray intersection tests for primitives.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_RAYTRACE_H
#define LL_RAYTRACE_H

class LLVector3;
class LLQuaternion;

// All functions produce results in the same reference frame as the arguments.
//
// Any arguments of the form "foo_direction" or "foo_normal" are assumed to
// be normalized, or normalized vectors are stored in them.
//
// Vector arguments of the form "shape_scale" represent the scale of the
// object along the three axes.
//
// All functions return the expected true or false, unless otherwise noted.
// When false is returned, any resulting values that might have been stored
// are undefined.
//
// Rays are defined by a "ray_point" and a "ray_direction" (unit).
//
// Lines are defined by a "line_point" and a "line_direction" (unit).
//
// Line segements are defined by "point_a" and "point_b", and for intersection
// purposes are assumed to point from "point_a" to "point_b".
//
// A ray is different from a line in that it starts at a point and extends
// in only one direction.
//
// Intersection normals always point outside the object, normal to the object's
// surface at the point of intersection.
//
// Object rotations passed as quaternions are expected to rotate from the
// object's local frame to the absolute frame.  So, if "foo" is a vector in
// the object's local frame, then "foo * object_rotation" is in the absolute
// frame.


// returns true if line is not parallel to plane.
bool line_plane(const LLVector3 &line_point, const LLVector3 &line_direction,
                const LLVector3 &plane_point, const LLVector3 plane_normal,
                LLVector3 &intersection);


// returns true if ray is not parallel to plane and does not point away from it.
bool ray_plane(const LLVector3 &ray_point, const LLVector3 &ray_direction,
               const LLVector3 &plane_point, const LLVector3 plane_normal,
               LLVector3 &intersection);


// point_0 through point_2 define the plane_normal via the right-hand rule:
// circle from point_0 to point_2 with fingers ==> thumb points in direction of normal
bool ray_triangle(const LLVector3 &ray_point, const LLVector3 &ray_direction,
                  const LLVector3 &point_0, const LLVector3 &point_1, const LLVector3 &point_2,
                  LLVector3 &intersection, LLVector3 &intersection_normal);


bool ray_sphere(const LLVector3 &ray_point, const LLVector3 &ray_direction,
                const LLVector3 &sphere_center, F32 sphere_radius,
                LLVector3 &intersection, LLVector3 &intersection_normal);


bool ray_tetrahedron(const LLVector3 &ray_point, const LLVector3 &ray_direction,
                     const LLVector3 &t_center, const LLVector3 &t_scale, const LLQuaternion &t_rotation,
                     LLVector3 &intersection, LLVector3 &intersection_normal);


bool linesegment_sphere(const LLVector3 &point_a, const LLVector3 &point_b,
                const LLVector3 &sphere_center, F32 sphere_radius,
                LLVector3 &intersection, LLVector3 &intersection_normal);


bool linesegment_tetrahedron(const LLVector3 &point_a, const LLVector3 &point_b,
                             const LLVector3 &t_center, const LLVector3 &t_scale, const LLQuaternion &t_rotation,
                             LLVector3 &intersection, LLVector3 &intersection_normal);

#endif
