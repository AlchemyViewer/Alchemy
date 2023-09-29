/**
 * @file encodeNormF.glsl
 *
 * $LicenseInfo:firstyear=2018&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2018, Linden Research, Inc.
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

 // Octahedron normal vector encoding
 // https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
 //

vec2 encode_normal(vec3 n)
{
    n *= 1.0 / max(dot(abs(n), vec3(1.0)), 1e-6);
    float t = clamp(-n.z, 0.0, 1.0);
	n.x += n.x >= 0.0 ? t : -t;
	n.y += n.y >= 0.0 ? t : -t;
    return n.xy * 0.5 + 0.5;
}
