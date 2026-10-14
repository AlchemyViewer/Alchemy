/**
 * @file waterExclusionF.glsl
 * @brief The water exclusion pass: which fragments of a PASS_INVISIBLE prim remove the water in front of what is
 *        behind them.
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

// Nothing is written but the stencil's exclude bit, which the pass sets wherever a fragment survives.
out vec4 frag_color;

uniform vec3 camPosLocal;

in vec3 vary_agent;

float water_height_at(vec2 xy);

void main()
{
    // Along the ray to a fragment the water surface lies in front of it exactly when the fragment and the camera are
    // on opposite sides of the surface. The water hides such a fragment, and it excludes nothing: a hull's keel seen
    // from above, a dome under the sea.
    float h = water_height_at(vary_agent.xy);
    if ((vary_agent.z - h) * (camPosLocal.z - h) < 0.0)
    {
        discard;
    }
    frag_color = vec4(0.0);
}
