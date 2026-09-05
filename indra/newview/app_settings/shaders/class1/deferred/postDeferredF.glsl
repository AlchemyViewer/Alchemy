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

    float peak = max(max(c.r, c.g), c.b);
    return 1.0 + uBokehHighlightGain * max(peak - uBokehHighlightThreshold, 0.0);
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
uniform float uBokehSpherical;           // -1 creamy .. 0 flat disc .. +1 soap bubble
uniform float uBokehFieldStretch;        // 0 disables; + tangential (swirl), - radial (coma)
uniform float uBokehFieldFalloff;        // how fast the stretch grows toward the corners
uniform float uBokehComaAsymmetry;       // 0 disables; ramps with field radius

// The floor keeps the shape weight strictly positive. Two reasons, both
// measured rather than defensive. A rim-bright profile drives the inner disc
// toward zero, and near a depth edge the outer rings are all rejected by the
// `sc > min_sc` test -- so the surviving samples would carry almost no weight
// and the pixel would fall back to its own colour while its neighbour blurred
// normally, which reads as speckle along every defocus transition. And a
// creamy profile lands exactly 0.0 on the outermost ring, the largest one, so
// its samples were fetched, tinted and multiplied away: 40% of all taps at 4px
// of blur. At 0.15 the centre-tap share tracks the unaberrated baseline to
// within a percent at every blur size, and both profiles still read correctly.
const float BOKEH_SHAPE_FLOOR = 0.15;

// How much this sample counts, before its radiance is weighed. Both aberrations
// are per-sample scalars on the same accumulation, so they combine into one
// branchless expression -- cheaper than gating each, and it avoids a divergent
// branch on `apod`, which varies per fragment through the blur-size fade.
//
// Spherical aberration redistributes weight across the disc. The gather is
// close to area-uniform -- ring sample counts grow with radius while rings stay
// one pixel apart -- so a per-sample weight is very nearly the bokeh's radial
// profile. Not exactly: int(sc*3.7) truncates a fraction of a sample from every
// ring, which biases density by up to 5% at small radii, and the outermost ring
// sits at radius_norm 1.0 where a continuous integral would half-weight it. The
// continuous form of (2r^2 - 1) has an area-weighted mean of zero, so the mean
// weight is 1 in the limit; the discrete walk deviates by up to a quarter at
// 3-4px of blur. It does not matter while the gather normalises by `w`, and it
// would matter a great deal if that normalisation were ever removed.
//
// `apod` arrives multiplied by -cof_sign and by the blur-size fade. `coma_vec`
// carries the comatic bias as one vector: its direction is the axis and its
// length is the strength, both baked per fragment.
float bokehShapeWeight(float radius_norm, vec2 samp_dir, float apod, vec2 coma_vec)
{
    float sw = 1.0
             + apod * (2.0 * radius_norm * radius_norm - 1.0)
             + dot(samp_dir, coma_vec) * radius_norm;

    return max(sw, BOKEH_SHAPE_FLOOR);
}

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
// radiance-weighted. It is this pixel's own value, and when the pixel is in
// focus the gather loops never run at all -- clamping it there would clip
// in-focus highlights, which is the opposite of what the clamp is for. It IS
// shape-weighted, so a rim-bright profile does not leave the sharp image
// bleeding through the middle of the disc; see the centre tap in main().
//
// radius_norm and cof_sign are only read in the shaped build; the unshaped one
// discards them along with the fringe call.
//
// One accumulate body shared by both gathers, so the near and far fields can
// never weight samples differently -- a one-sided edit to the clamp or fringe
// would otherwise show up as a subtle front/back blur mismatch.
void dofAccumulate(inout vec4 diff, inout float w, vec4 s, float radius_norm, float cof_sign, float shape_w)
{
    vec3 c = bokehClamp(s.rgb);
#if DOF_SHAPED
    c = bokehFringe(c, radius_norm, cof_sign);
#endif
    vec4  cs = vec4(c, s.a);
    float wg = bokehWeight(cs.rgb) * shape_w;

    diff += wg*cs;

    w += wg;
}

void dofSample(inout vec4 diff, inout float w, float min_sc, vec2 tc, float radius_norm, float cof_sign, float shape_w)
{
    vec4 s = texture(diffuseRect, tc);

    float sc = abs(s.a*2.0-1.0)*max_cof;

    if (sc > min_sc) //sampled pixel is more "out of focus" than current sample radius
    {
        dofAccumulate(diff, w, s, radius_norm, cof_sign, shape_w);
    }
}

void dofSampleNear(inout vec4 diff, inout float w, vec2 tc, float radius_norm, float cof_sign, float shape_w)
{
    dofAccumulate(diff, w, texture(diffuseRect, tc), radius_norm, cof_sign, shape_w);
}

vec3 clampHDRRange(vec3 color);

void main()
{
    vec2 tc = vary_fragcoord.xy;

    vec4 diff = texture(diffuseRect, vary_fragcoord.xy);

    {
        float sc = (diff.a*2.0-1.0)*max_cof;

        float PI = 3.14159265358979323846264;

        // Outermost ring radius and which side of focus we are on. The rings
        // walk inward from here, so sc/max_radius is the sample's position
        // across the disc: 1.0 at the rim, approaching 0 at the centre. Both
        // the aperture shape and the fringe are defined in those terms.
        float max_radius = max(abs(sc), 1e-4);
        float cof_sign   = (sc < 0.0) ? -1.0 : 1.0;

#if DOF_SHAPED
        vec2  cat_offset   = vec2(0.0);
        vec2  ax           = vec2(1.0, 0.0);
        vec2  ay           = vec2(0.0, 1.0);
        vec2  coma_vec     = vec2(0.0);
        float apod_signed  = 0.0;
        float ring_density = 1.0;

        // Everything below is only read by the gather loops, so fragments that
        // are in focus -- most of the frame at a mild setting -- skip the lot.
        // It matters more than it looks: the block carries several divides, two
        // square roots and a smoothstep, where before these effects existed it
        // was two scalar assignments.
        if (abs(sc) > 0.5)
        {
            // Where this fragment sits in the frame, measured the way every
            // other radial effect in the stack measures it: aspect-corrected,
            // then normalised over the half-diagonal so the corner reads 1.0 on
            // any viewport shape. Measured in raw UV instead, "distance from
            // the optical axis" reaches 1.0 at the left edge of a 21:9 frame
            // and 1.0 at its top edge, which are nowhere near the same distance
            // -- and the effects keyed off it then follow the viewport
            // rectangle rather than the lens's image circle.
            float dof_aspect = screen_res.x / max(screen_res.y, 1.0);
            vec2  dof_ascale = max(vec2(dof_aspect, 1.0 / max(dof_aspect, 1e-4)), 1.0);
            vec2  field_vec  = (vary_fragcoord.xy - 0.5) * dof_ascale;
            float field_len  = length(field_vec);
            vec2  field_dir  = (field_len > 1e-5) ? (field_vec / field_len) : vec2(0.0);
            float field_r    = clamp(field_len / (0.5 * length(dof_ascale)), 0.0, 1.0);

            // Aberrations fade in with blur size. One ring cannot carry a
            // radial profile: below about 1.5px the disc is a single ring
            // sitting at radius_norm 1.0, so a shaped weight there is applied
            // to every surviving sample at once and the blur either collapses
            // or goes one-sided. Fading to zero leaves those pixels behaving
            // exactly as they do without the effect.
            float shape_fade = smoothstep(1.5, 4.0, max_radius);

            // Offset of the barrel opening for optical vignetting, growing with
            // distance from the optical axis.
            cat_offset = field_dir * (field_r * uBokehCatEye);

        // Anamorphic deformation. A cylindrical element squeezes the image on
        // one axis, and out-of-focus highlights inherit that squeeze as ovals
        // -- the format's most recognisable signature. Applied to the sample
        // offsets *after* the aperture test, not to the test itself: the
        // diaphragm is whatever shape it is, and the cylinder stretches the
        // disc that results, so blades and cat's-eye slivers stretch with it.
        //
            // The CPU sends the anamorphic squeeze area-preserving (the two
            // axes multiply to 1), so the control changes the shape of the blur
            // without also changing how much of it there is.
            //
            // Anamorphic alone is a diagonal matrix, which is what this used to
            // be as two scalars. Field stretch adds a second squeeze on the
            // radial axis, so the pair becomes a general 2x2 carried as its two
            // column vectors. With field stretch off it reduces to (anam.x, 0)
            // and (0, anam.y) -- the old behaviour, bit for bit.
            if (uBokehFieldStretch != 0.0 && field_len > 1e-5)
            {
                // Stretch across the radius for swirl, along it for coma. The
                // sqrt makes the axes s and 1/s, so the deformation is
                // area-preserving and a highlight keeps its brightness as it
                // deforms. max() on the falloff because a zero exponent would
                // make pow() return 1.0 everywhere and stretch the on-axis disc
                // as hard as the corners.
                vec2  u = (uBokehFieldStretch > 0.0)
                        ? vec2(-field_dir.y, field_dir.x)   // across the radius
                        : field_dir;                        // along it
                float s = sqrt(1.0 + abs(uBokehFieldStretch)
                                    * pow(field_r, max(uBokehFieldFalloff, 1.0)));

                // A symmetric stretch by s along u is (1/s)I + (s - 1/s)uu^T.
                // field_dir is already the unit vector the rotation would have
                // rebuilt, so there is no angle to recover and no trig here.
                float k = s - 1.0 / s;
                ax = vec2(1.0 / s + k * u.x * u.x, k * u.x * u.y);
                ay = vec2(k * u.x * u.y, 1.0 / s + k * u.y * u.y);
            }

            // Anamorphic applied outermost: the cylindrical element squeezes
            // the whole image, including whatever shape the field aberration
            // has already produced.
            ax *= uBokehAnamorphic;
            ay *= uBokehAnamorphic;

            // Largest singular value of the basis -- how far its widest axis
            // has been stretched. Ring sample counts scale by it so a deformed
            // disc does not thin out into visible rings. This reduces exactly
            // to max(anam.x, anam.y) when there is no field stretch, which is
            // what the line used to be, and it cannot be replaced by
            // max(old, new) once field stretch is live: the two stretch axes
            // can oppose, and the true maximum then sits *below* max(anam).
            // Capped because it multiplies the tap count directly, and the only
            // other bound on it is a pair of CPU clamps two files away.
            float bF = dot(ax, ax) + dot(ay, ay);
            float bD = ax.x * ay.y - ax.y * ay.x;
            ring_density = min(sqrt(max(0.5 * (bF + sqrt(max(bF * bF - 4.0 * bD * bD, 0.0))), 1e-4)), 3.0);

            // Spherical aberration. Multiplied by -cof_sign, not cof_sign:
            // cof_sign is +1 in front of the focal plane, so anchoring the
            // control to the foreground would invert it for the background --
            // and the background is the only field the default build renders,
            // since RenderDepthOfFieldNearBlur defaults off and compiles the
            // near gather out. Positive now means a bright rim behind focus,
            // which is what the setting says it means.
            apod_signed = uBokehSpherical * shape_fade * -cof_sign;

            // Comatic asymmetry, as one vector: direction is the bias axis,
            // length is the strength.
            //
            // Two things here are easy to get backwards, and both were.
            //
            // This pass is a *gather*: a fragment reads its neighbours, so a
            // point source renders as the weight function mirrored through the
            // origin. Favouring outward samples therefore deposits light on the
            // inward side and the comet points at the frame centre. The axis is
            // negated so the rendered flare runs outward, the way real coma and
            // this setting's own description both say it should.
            //
            // And the axis has to be measured in the disc's *parameter* space,
            // because that is where the samples are chosen. The screen-space
            // centroid is M times the parameter-space centroid, so biasing
            // along M^T(field) lands the comet along M M^T(field) -- 41 degrees
            // off with a strong anamorphic squeeze. Biasing along the inverse
            // instead puts it back exactly on the field direction. det(M) is
            // the anamorphic product, which the CPU sends as 1 and which is
            // positive regardless, so the adjugate serves and the 1/det drops
            // out in the normalise.
            if (uBokehComaAsymmetry != 0.0 && field_len > 1e-5)
            {
                vec2  g  = vec2(ay.y * field_dir.x - ay.x * field_dir.y,
                                ax.x * field_dir.y - ax.y * field_dir.x);
                float gl = length(g);
                if (gl > 1e-5)
                {
                    coma_vec = -(g / gl)
                             * (uBokehComaAsymmetry * field_r * shape_fade);
                }
            }
        }
#else
        const float ring_density = 1.0;
#endif

        // The centre tap is the sample at radius_norm 0, so it carries that
        // position's weight rather than a bare 1.0. Leaving it unweighted is
        // what let a rim-bright profile bleed the sharp image through wherever
        // the surrounding ring samples were rejected.
#if DOF_SHAPED
        float w = bokehShapeWeight(0.0, vec2(0.0), apod_signed, coma_vec);
#else
        float w = 1.0;
#endif
        diff *= w;

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
                    // Below the reject, not above it: a clipped sample should
                    // not pay for trig it never uses, and apertureMask derives
                    // its own sin/cos only on the cat's-eye path.
                    float sa = sin(ang), ca = cos(ang);
#if DOF_SHAPED
                    vec2  samp    = sc * (sa * ax + ca * ay);
                    float shape_w = bokehShapeWeight(rn, vec2(sa, ca), apod_signed, coma_vec);
#else
                    // Deliberately the plain axis-aligned form rather than the
                    // basis above: this is the path every DoF user without a
                    // shaped effect takes, and it should not pay for a general
                    // 2x2 on the strength of the compiler folding it back down.
                    vec2  samp    = vec2(sc * sa, sc * ca);
                    float shape_w = 1.0;
#endif
                    dofSampleNear(diff, w, vary_fragcoord.xy + (samp / screen_res), rn, cof_sign, shape_w);
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
                    // Below the reject, not above it: a clipped sample should
                    // not pay for trig it never uses, and apertureMask derives
                    // its own sin/cos only on the cat's-eye path.
                    float sa = sin(ang), ca = cos(ang);
#if DOF_SHAPED
                    vec2  samp    = sc * (sa * ax + ca * ay);
                    float shape_w = bokehShapeWeight(rn, vec2(sa, ca), apod_signed, coma_vec);
#else
                    // Deliberately the plain axis-aligned form rather than the
                    // basis above: this is the path every DoF user without a
                    // shaped effect takes, and it should not pay for a general
                    // 2x2 on the strength of the compiler folding it back down.
                    vec2  samp    = vec2(sc * sa, sc * ca);
                    float shape_w = 1.0;
#endif
                    dofSample(diff, w, sc, vary_fragcoord.xy + (samp / screen_res), rn, cof_sign, shape_w);
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
