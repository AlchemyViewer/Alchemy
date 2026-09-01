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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]
uniform vec2 screen_res;
uniform float max_cof;
uniform float res_scale;

in vec2 vary_fragcoord;

// =============================================================================
// Bokeh gather weighting
// =============================================================================
//
// This pass moved ahead of the tonemapper, which changes what the sample
// weighting has to do. The old form was `wg = 0.25 + s.r+s.g+s.b`, applied to
// display-space values already compressed into [0, 1]: there the largest a
// sample could weigh was 3.25x the smallest, a mild nudge that made highlights
// read against a tonemapper that had already crushed them.
//
// On linear HDR the same expression is an accidental max filter. A punctual
// specular peak has no solid angle, so one pixel can carry five figures of
// radiance -- weighted by `r+g+b` it outweighs every other sample in its disc
// combined, and the disc becomes a flat plate of that one colour. So the pop
// that expression was faking now has to be asked for explicitly, and the
// default is a plain energy-conserving average.

uniform float uBokehHighlightThreshold;  // luma where the boost starts; 0 boosts the whole range
uniform float uBokehHighlightGain;       // 0 = plain average (fast path); higher = more highlight pop
uniform float uBokehHighlightClamp;      // per-sample radiance ceiling; <= 0 disables

// Per-sample radiance ceiling -- the DoF-side analogue of the firefly clamp
// bloomExtractF applies for the same reason. Bounding the artifact, not the
// image: the in-focus pixel keeps its full intensity, only what a sample is
// allowed to contribute to a *neighbour's* disc is capped. Scaled by the
// largest channel so a clamped highlight keeps its colour instead of sliding
// toward whichever primary saturated first.
vec3 bokehClamp(vec3 c)
{
    if (uBokehHighlightClamp <= 0.0)
        return c;

    float peak = max(max(c.r, c.g), c.b);
    return (peak > uBokehHighlightClamp) ? c * (uBokehHighlightClamp / peak) : c;
}

// Energy-conserving by default: every accepted sample counts once, so the
// gather is an average and a bright sample contributes its brightness rather
// than extra influence.
float bokehWeight(vec3 c)
{
    if (uBokehHighlightGain <= 0.0)
        return 1.0;

    float luma = max(max(c.r, c.g), c.b);
    return 1.0 + uBokehHighlightGain * max(luma - uBokehHighlightThreshold, 0.0);
}

#if DOF_SHAPED
// =============================================================================
// Shaped aperture, optical vignetting, and defocus fringing
// =============================================================================
//
// Everything in this block is compiled out entirely unless at least one of
// these effects is switched on, because it lives in the innermost sample loop
// where a uniform branch still costs registers. The CPU picks between the
// shaped and unshaped programs; within a shaped one each effect gates on its
// own uniform, the way the lens flare's sub-effects do. The CPU-side `shaped`
// predicate in pipeline.cpp must list every effect that lives here -- one it
// misses becomes a dead control whenever it is the only one active.

uniform int   uBokehBlades;              // 0 = circular; 3..11 = polygon
uniform float uBokehApertureRotation;    // radians, converted from degrees on the CPU
uniform float uBokehApertureCurvature;   // 0 straight blades -> 1 fully round
uniform vec3  uBokehApertureConst;       // (pi/N, 2pi/N, cos(pi/N)) baked on the CPU
uniform vec2  uBokehAnamorphic;          // per-axis sample stretch, area-preserving; (1,1) = spherical
uniform float uBokehCatEye;              // 0 disables; higher clips harder toward the edges
uniform float uBokehFringeAmount;        // 0 disables
uniform vec3  uBokehFringeNearTint;      // applied in front of the focal plane
uniform vec3  uBokehFringeFarTint;       // applied behind it

// 1.0 if this sample falls inside the aperture, 0.0 if a blade or the cat's-eye
// clip excludes it. `radius_norm` is the sample's position across the disc,
// 0 at the centre and 1 at the rim.
float apertureMask(float ang, float radius_norm, vec2 cat_offset)
{
    float edge = 1.0;

    if (uBokehBlades >= 3)
    {
        // Inscribed radius of a regular N-gon at this angle:
        //   cos(pi/N) / cos(mod(theta + rot, 2pi/N) - pi/N)
        // The three constants come pre-baked so the loop does no trig setup,
        // only the one cosine it genuinely needs per sample.
        float half_sector = uBokehApertureConst.x;
        float sector      = uBokehApertureConst.y;
        float apothem     = uBokehApertureConst.z;

        float t    = mod(ang + uBokehApertureRotation, sector) - half_sector;
        float poly = apothem / max(cos(t), 1e-3);

        // Curvature relaxes the straight blades back toward a circle, which is
        // what a rounded diaphragm actually produces.
        edge = mix(poly, 1.0, clamp(uBokehApertureCurvature, 0.0, 1.0));
    }

    if (radius_norm > edge)
    {
        return 0.0;
    }

    if (uBokehCatEye > 0.0)
    {
        // Optical (mechanical) vignetting. The aperture an off-axis ray sees is
        // the intersection of the diaphragm with the lens barrel, and that
        // second opening slides further off-centre the further the ray is from
        // the axis -- so discs near the frame edge are clipped into lens-shaped
        // slivers that lean away from centre, while the middle stays round.
        vec2 p = vec2(sin(ang), cos(ang)) * radius_norm;
        vec2 d = p - cat_offset;
        if (dot(d, d) > 1.0)
        {
            return 0.0;
        }
    }

    return 1.0;
}

// Longitudinal chromatic aberration. Real glass brings different wavelengths to
// focus at slightly different distances, so a defocused edge picks up a colour
// cast whose hue flips either side of the focal plane -- the familiar magenta
// in front, green behind. Approximated by tinting each sample by how far out in
// the disc it sits, which puts the cast on the disc's rim where it belongs, and
// choosing the tint by the sign of the circle of confusion.
//
// Distinct from the lateral chromatic aberration in colorCorrect: that one is a
// whole-frame radial fringe that grows toward the corners and is present
// whether or not anything is defocused.
vec3 bokehFringe(vec3 c, float radius_norm, float cof_sign)
{
    if (uBokehFringeAmount <= 0.0)
    {
        return c;
    }

    vec3 tint = (cof_sign < 0.0) ? uBokehFringeFarTint : uBokehFringeNearTint;
    return c * mix(vec3(1.0), tint, clamp(radius_norm, 0.0, 1.0) * uBokehFringeAmount);
}
#endif

// Note the centre sample in main() is deliberately neither clamped nor
// reweighted. It is this pixel's own value, and when the pixel is in focus the
// gather loops never run at all -- clamping it there would clip in-focus
// highlights, which is the opposite of what the clamp is for.
//
// radius_norm and cof_sign are only read in the shaped build; the unshaped one
// discards them along with the fringe call.
//
// One accumulate body shared by both gathers, so the near and far fields can
// never weight samples differently -- a one-sided edit to the clamp or fringe
// would otherwise show up as a subtle front/back blur mismatch.
void dofAccumulate(inout vec4 diff, inout float w, vec4 s, float radius_norm, float cof_sign)
{
    vec3 c = bokehClamp(s.rgb);
#if DOF_SHAPED
    c = bokehFringe(c, radius_norm, cof_sign);
#endif
    vec4  cs = vec4(c, s.a);
    float wg = bokehWeight(cs.rgb);

    diff += wg*cs;

    w += wg;
}

void dofSample(inout vec4 diff, inout float w, float min_sc, vec2 tc, float radius_norm, float cof_sign)
{
    vec4 s = texture(diffuseRect, tc);

    float sc = abs(s.a*2.0-1.0)*max_cof;

    if (sc > min_sc) //sampled pixel is more "out of focus" than current sample radius
    {
        dofAccumulate(diff, w, s, radius_norm, cof_sign);
    }
}

void dofSampleNear(inout vec4 diff, inout float w, float min_sc, vec2 tc, float radius_norm, float cof_sign)
{
    dofAccumulate(diff, w, texture(diffuseRect, tc), radius_norm, cof_sign);
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

        // Outermost ring radius and which side of focus we are on. The rings
        // walk inward from here, so sc/max_radius is the sample's position
        // across the disc: 1.0 at the rim, approaching 0 at the centre. Both
        // the aperture shape and the fringe are defined in those terms.
        float max_radius = max(abs(sc), 1e-4);
        float cof_sign   = (sc < 0.0) ? -1.0 : 1.0;

#if DOF_SHAPED
        // Offset of the barrel opening for optical vignetting, growing with
        // distance from the optical axis. Constant across the disc, so it is
        // computed once per fragment rather than per sample.
        vec2 cat_offset = (vary_fragcoord.xy - 0.5) * 2.0 * uBokehCatEye;

        // Anamorphic deformation. A cylindrical element squeezes the image on
        // one axis, and out-of-focus highlights inherit that squeeze as ovals
        // -- the format's most recognisable signature. Applied to the sample
        // offsets *after* the aperture test, not to the test itself: the
        // diaphragm is whatever shape it is, and the cylinder stretches the
        // disc that results, so blades and cat's-eye slivers stretch with it.
        //
        // The CPU sends this area-preserving (the two axes multiply to 1), so
        // the control changes the shape of the blur without also changing how
        // much of it there is.
        vec2  anam         = uBokehAnamorphic;
        float ring_density = max(anam.x, anam.y);
#else
        const vec2  anam         = vec2(1.0);
        const float ring_density = 1.0;
#endif

        // sample quite uniformly spaced points within a circle, for a circular 'bokeh'
#if FRONT_BLUR
        if (sc > 0.5)
        {
            while (sc > 0.5)
            {
                int its = int(max(1.0,(sc*3.7*ring_density)));
                for (int i=0; i<its; ++i)
                {
                    float ang = sc+i*2*PI/its; // sc is added for rotary perturbance
                    float rn  = sc / max_radius;
#if DOF_SHAPED
                    if (apertureMask(ang, rn, cat_offset) < 0.5)
                    {
                        continue;   // blade or barrel clips this one
                    }
#endif
                    float samp_x = sc*sin(ang) * anam.x;
                    float samp_y = sc*cos(ang) * anam.y;
                    dofSampleNear(diff, w, sc, vary_fragcoord.xy + (vec2(samp_x,samp_y) / screen_res), rn, cof_sign);
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
                int its = int(max(1.0,(sc*3.7*ring_density)));
                for (int i=0; i<its; ++i)
                {
                    float ang = sc+i*2*PI/its; // sc is added for rotary perturbance
                    float rn  = sc / max_radius;
#if DOF_SHAPED
                    if (apertureMask(ang, rn, cat_offset) < 0.5)
                    {
                        continue;   // blade or barrel clips this one
                    }
#endif
                    float samp_x = sc*sin(ang) * anam.x;
                    float samp_y = sc*cos(ang) * anam.y;
                    dofSample(diff, w, sc, vary_fragcoord.xy + (vec2(samp_x,samp_y) / screen_res), rn, cof_sign);
                }
                sc -= 1.0;
            }
        }

        // A shaped aperture can reject every sample in a ring, and a tight
        // cat's-eye can reject nearly all of them, so the divisor is guarded.
        // The centre sample always contributes 1.0, so this only ever bites in
        // pathological configurations.
        diff /= max(w, 1e-4);
    }

    diff.rgb = clampHDRRange(diff.rgb);
    frag_color = diff;
}
