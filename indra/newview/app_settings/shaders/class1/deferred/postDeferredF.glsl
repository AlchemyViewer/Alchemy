/**
 * @file postDeferredF.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

uniform sampler2D diffuseRect;

uniform mat4 inv_proj;
uniform vec2 screen_res;
uniform float max_cof;
uniform float res_scale;

// Bokeh aperture shaping, CPU-baked in LLPipeline::renderDoF() via ALDoFBokeh::bakeKernel().
uniform vec4  bokeh_shape;     // (2pi/N, pi/N, cos(pi/N), roundness); N = aperture blade count
uniform vec4  bokeh_lens;      // (rotation_radians, anamorphic_x, anamorphic_y, active flag)
uniform float bokeh_intensity; // highlight-weight scale (1 = classic pop, higher = more defined)

in vec2 vary_fragcoord;

void dofSample(inout vec4 diff, inout float w, float min_sc, vec2 tc)
{
    vec4 s = texture(diffuseRect, tc);

    float sc = abs(s.a*2.0-1.0)*max_cof;

    if (sc > min_sc) //sampled pixel is more "out of focus" than current sample radius
    {
        // de-weight dull areas to make highlights 'pop'; bokeh_intensity scales how
        // strongly bright samples dominate the disc (1.0 = classic behavior).
        float wg = 0.25 + bokeh_intensity*(s.r+s.g+s.b);

        diff += wg*s;

        w += wg;
    }
}

void dofSampleNear(inout vec4 diff, inout float w, float min_sc, vec2 tc)
{
    vec4 s = texture(diffuseRect, tc);

    // de-weight dull areas to make highlights 'pop'; bokeh_intensity scales how
    // strongly bright samples dominate the disc (1.0 = classic behavior).
    float wg = 0.25 + bokeh_intensity*(s.r+s.g+s.b);

    diff += wg*s;

    w += wg;
}

// Aperture-shaped bokeh tap offset. Reshapes WHERE a tap lands (aperture polygon
// and/or anamorphic squeeze); the caller's tap COUNT is unchanged, so this does
// not affect the O(CoC^2) gather cost. When the kernel is inactive it returns the
// exact circular offset the gather used before, so the default look and cost are
// preserved bit-for-bit.
vec2 bokehOffset(float r, float a)
{
    if (bokeh_lens.w < 0.5)
        return vec2(sin(a), cos(a)) * r;               // fast path: unchanged circular bokeh

    // Build the aperture in its own (unrotated) frame: N-gon radius, then the
    // anamorphic per-axis squeeze.
    float shape = 1.0;
    if (bokeh_shape.x > 0.0)                           // polygon active (blades >= 3)
    {
        // Fold the angle into one blade segment and pinch the ring onto the
        // inscribed N-gon edge, then blend back toward a circle by roundness.
        float s = mod(a, bokeh_shape.x) - bokeh_shape.y;
        shape = mix(bokeh_shape.z / cos(s), 1.0, bokeh_shape.w);
    }
    vec2 local = vec2(sin(a), cos(a)) * (r * shape) * bokeh_lens.yz;

    // Rigidly rotate the whole aperture -- polygon AND anamorphic oval -- into
    // screen space. bokeh_lens.x is a uniform, so this cos/sin is loop-invariant
    // and gets hoisted out of the gather loop by the compiler.
    float cs = cos(bokeh_lens.x);
    float sn = sin(bokeh_lens.x);
    return vec2(local.x * cs - local.y * sn, local.x * sn + local.y * cs);
}

vec3 clampHDRRange(vec3 color);

void main()
{
    vec2 tc = vary_fragcoord.xy;

    vec4 diff = texture(diffuseRect, vary_fragcoord.xy);

    {
        float w = 1.0;

        float sc = (diff.a*2.0-1.0)*max_cof;

        float PI = 3.14159265358979323846264;

        // sample quite uniformly spaced points within a circle, for a circular 'bokeh'
#if FRONT_BLUR
        if (sc > 0.5)
        {
            while (sc > 0.5)
            {
                int its = int(max(1.0,(sc*3.7)));
                for (int i=0; i<its; ++i)
                {
                    float ang = sc+i*2*PI/its; // sc is added for rotary perturbance
                    // Aperture-shaped tap offset (polygonal / anamorphic bokeh); circular when off.
                    vec2 samp = bokehOffset(sc, ang);
                    dofSampleNear(diff, w, sc, vary_fragcoord.xy + (samp / screen_res));
                }
                sc -= 1.0;
            }
        }
        else if (sc < -0.5)
#else
        if (sc < -0.5)
#endif
        {
            sc = abs(sc);
            while (sc > 0.5)
            {
                int its = int(max(1.0,(sc*3.7)));
                for (int i=0; i<its; ++i)
                {
                    float ang = sc+i*2*PI/its; // sc is added for rotary perturbance
                    // Aperture-shaped tap offset (polygonal / anamorphic bokeh); circular when off.
                    vec2 samp = bokehOffset(sc, ang);
                    dofSample(diff, w, sc, vary_fragcoord.xy + (samp / screen_res));
                }
                sc -= 1.0;
            }
        }

        diff /= w;
    }

    diff.rgb = clampHDRRange(diff.rgb);
    frag_color = diff;
}
