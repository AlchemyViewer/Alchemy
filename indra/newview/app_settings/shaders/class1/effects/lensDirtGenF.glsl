/**
 * @file lensDirtGenF.glsl
 * @brief Generates the lens dirt plate into a texture.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project
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

/*[EXTRA_CODE_HERE]*/

// Muck on the front element: defocused dust, wipe smudges, stray fibres, fine
// grit, and optionally scratches and coating chips. The result is a scalar mask
// that colorCorrect multiplies bloom and flare by, so only relative brightness
// matters and the plate is heavily biased to black.
//
// This runs ONCE into a texture whenever a parameter changes or the window
// resizes -- not per frame. That budget is the whole reason the effect can be
// procedural at all: a few hundred ALU per pixel is unthinkable in the
// per-frame path and unremarkable in a one-off, which is what lets this draw
// discrete features rather than settling for whatever a couple of octaves of
// noise happen to look like.
//
// Generating at the target's own resolution also retires the cover-fit the
// baked plates needed. A square image sampled with screen UV stretches every
// round mote into an ellipse on a wide display; there is no fitting to do when
// the plate is made at the shape it will be read at.

out vec4 frag_color;

in vec2 vary_texcoord0;

uniform vec2  uDirtResolution;   // plate size in pixels; only the ratio is read
uniform float uDirtSeed;         // reshuffles every layer
uniform float uDirtGrime;        // master density
uniform float uDirtMoteScale;    // >1 enlarges the motes and thins them out
uniform float uDirtSmudge;       // wipe-mark strength
uniform int   uDirtScratches;    // 0 for undamaged glass
uniform float uDirtToe;          // tone curve exponent
uniform float uDirtGain;         // tone curve gain

// Upper bound on the segment loops, injected at compile time from
// LENS_DIRT_MAX_LINES so the CPU clamp and the loop bound cannot drift apart.
// The loop is bounded rather than run to a uniform so the compiler can unroll
// it instead of branching per iteration on a value it cannot see.
const int MAX_LINES = DIRT_MAX_LINES;

// ---------------------------------------------------------------- hashes ----

float hash21(vec2 p, float seed)
{
    return fract(sin(p.x * 127.1 + p.y * 311.7 + seed * 74.7) * 43758.5453);
}

vec2 hash22(vec2 p, float seed)
{
    return fract(vec2(sin(p.x * 127.1 + p.y * 311.7 + seed * 74.7),
                      sin(p.x * 269.5 + p.y * 183.3 + seed * 51.3)) * 43758.5453);
}

float screenBlend(float a, float b)
{
    return 1.0 - (1.0 - a) * (1.0 - b);
}

// ------------------------------------------------------------ mote layer ----

// One jittered disc per grid cell, scanned over the 3x3 neighbourhood so a disc
// crossing a cell boundary still registers.
//
// Three details separate this from looking like plain cellular noise, and all
// three were found by comparing bakes against the plates this replaces.
//
// Cells are dropped when their hash exceeds `density`: one feature per cell is
// perfectly even, and dirt is not -- it pools and leaves clean glass between.
// Radii are raised to `bias` so most motes come out small with a few large,
// rather than filling the range uniformly. And the profile is a flat interior
// with a quick rim rather than a Gaussian, because the reference motes were
// drawn as solid shapes and lightly blurred; a Gaussian has no edge anywhere
// and reads as fog the moment neighbouring blobs overlap. `softness` is the
// fraction of the radius the rim occupies.
float dirtMotes(vec2 uv, float cells, float r_min, float r_max, float softness,
                float seed, float aspect, float density, float bias)
{
    float acc = 0.0;
    vec2  cell = floor(uv * cells);

    for (int ox = -1; ox <= 1; ++ox)
    {
        for (int oy = -1; oy <= 1; ++oy)
        {
            vec2  g    = cell + vec2(float(ox), float(oy));
            float keep = step(hash21(g, seed + 57.0), density);
            vec2  f    = (g + hash22(g, seed)) / cells;
            float rad  = r_min + (r_max - r_min) * pow(hash21(g, seed + 19.0), bias);

            vec2  d2 = vec2((uv.x - f.x) * aspect, uv.y - f.y);
            float d  = length(d2);

            float x = clamp((rad - d) / (rad * softness + 1e-9), 0.0, 1.0);
            acc = screenBlend(acc, x * x * (3.0 - 2.0 * x) * keep);
        }
    }
    return acc;
}

// ----------------------------------------------------------- value noise ----

float vnoise(vec2 uv, float freq, float seed)
{
    vec2 p = uv * freq;
    vec2 i = floor(p);
    vec2 f = p - i;
    f = f * f * (3.0 - 2.0 * f);

    float a = hash21(i, seed);
    float b = hash21(i + vec2(1.0, 0.0), seed);
    float c = hash21(i + vec2(0.0, 1.0), seed);
    float d = hash21(i + vec2(1.0, 1.0), seed);

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm3(vec2 uv, float freq, float seed)
{
    float total = 0.0;
    float amp   = 1.0;
    float norm  = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        total += amp * vnoise(uv, freq * exp2(float(i)), seed + float(i) * 13.0);
        norm  += amp;
        amp   *= 0.5;
    }
    return total / norm;
}

// ------------------------------------------------------------- segments -----

float segDist(vec2 uv, vec2 a, vec2 b, float aspect)
{
    vec2 pa = vec2((uv.x - a.x) * aspect, uv.y - a.y);
    vec2 ba = vec2((b.x - a.x) * aspect, b.y - a.y);
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-9), 0.0, 1.0);
    return length(pa - ba * h);
}

// Fibres and scratches. A segment distance field, optionally domain-warped by
// low-frequency noise so a fibre wanders while a scratch stays straight -- the
// only difference between a thread lying on the glass and a wipe with grit in
// the cloth.
float dirtLines(vec2 uv, int count, float length_, float width, float seed,
                float aspect, float wander)
{
    vec2 w = uv;
    if (wander > 0.0)
    {
        w += (vec2(fbm3(uv, 3.0, seed + 7.0), fbm3(uv, 3.0, seed + 31.0)) - 0.5) * wander;
    }

    float acc = 0.0;
    for (int i = 0; i < MAX_LINES; ++i)
    {
        if (i >= count)
        {
            break;
        }
        float fi  = float(i);
        vec2  a   = vec2(hash21(vec2(fi, 1.0), seed), hash21(vec2(fi, 2.0), seed));
        float ang = hash21(vec2(fi, 3.0), seed) * 6.2831853;
        float len = length_ * (0.4 + 0.6 * hash21(vec2(fi, 4.0), seed));
        vec2  b   = a + vec2(cos(ang), sin(ang)) * len;
        float wid = width * (0.5 + 0.5 * hash21(vec2(fi, 5.0), seed));

        float d = segDist(w, a, b, aspect);
        float x = clamp((wid - d) / max(wid, 1e-9), 0.0, 1.0);
        acc = max(acc, x * x * (3.0 - 2.0 * x) * (0.55 + 0.45 * hash21(vec2(fi, 6.0), seed)));
    }
    return acc;
}

// ---------------------------------------------------------------- build -----

void main()
{
    vec2  uv     = vary_texcoord0;
    float aspect = uDirtResolution.x / max(uDirtResolution.y, 1.0);
    float seed   = uDirtSeed;
    float grime  = uDirtGrime;
    float mscale = max(uDirtMoteScale, 0.05);

    // Where the muck gathers. Dirt does not spread evenly over a lens: it pools
    // and leaves other areas nearly clean, and modulating the mote layers by a
    // low-frequency field is what turns an even scatter into something that
    // looks like it settled there.
    //
    // The frequency is load-bearing for a reason that is not obvious. A clump
    // field with only a handful of features across the plate has a spatial mean
    // that swings from seed to seed, and since it multiplies every mote layer
    // that swing becomes the plate's overall density -- at a low frequency the
    // same settings covered anywhere from 19% to 76% of the frame on nothing
    // but the seed. More features average the mean back toward the middle, and
    // the narrow output range keeps what wobble remains away from the result.
    float clump = fbm3(uv, 5.0, seed + 101.0);
    clump = 0.62 + 0.76 * clamp((clump - 0.34) / 0.36, 0.0, 1.0);

    float img = 0.0;

    // Motes, large relative to their spacing so they overlap into a mottled
    // field rather than reading as separate blobs.
    img = screenBlend(img, dirtMotes(uv, 4.5 / mscale, 0.050, 0.140, 0.45,
                                     seed, aspect, 0.80 * grime, 1.7) * 0.46 * clump);
    img = screenBlend(img, dirtMotes(uv, 9.0 / mscale, 0.022, 0.064, 0.42,
                                     seed + 3.0, aspect, 0.75 * grime, 1.9) * 0.50 * clump);
    img = screenBlend(img, dirtMotes(uv, 20.0 / mscale, 0.009, 0.028, 0.40,
                                     seed + 5.0, aspect, 0.60 * grime, 2.1) * 0.44 * clump);

    // Fine grit. Small and sharp, and the layer most easily overdone: too much
    // and the plate reads as a starfield rather than as dirt.
    img = screenBlend(img, dirtMotes(uv, 90.0, 0.0011, 0.0030, 0.55,
                                     seed + 9.0, aspect, 0.34 * grime, 1.5) * 0.75);

    // Wipe smudges: broad and low contrast, a cloth pushed across the glass.
    float sm = fbm3(uv, 1.6, seed + 21.0);
    img = screenBlend(img, pow(clamp((sm - 0.50) / 0.34, 0.0, 1.0), 1.4) * 0.34 * uDirtSmudge);

    // Stray fibres.
    img = screenBlend(img, dirtLines(uv, 12, 0.20, 0.0014, seed + 41.0, aspect, 0.06) * 0.62);

    if (uDirtScratches > 0)
    {
        img = screenBlend(img, dirtLines(uv, uDirtScratches, 0.55, 0.0022,
                                         seed + 57.0, aspect, 0.0));
    }

    // Keep the middle of the frame clearer. The plate multiplies bloom, and the
    // subject usually sits in the centre, so grime there veils exactly what the
    // viewer is looking at. The range is wider than it looks like it needs to
    // be because the clump field above swings harder than this does, and a
    // gentler curve simply disappeared underneath it.
    vec2  c = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
    float r = clamp(length(c) / (0.5 * length(vec2(aspect, 1.0))), 0.0, 1.0);
    img *= 0.22 + 0.78 * pow(r, 0.75);

    // Shape the low end. The layers above are deliberately generous with area,
    // and this is what decides whether the plate reads at all: crushing it too
    // hard removes the midtones dirt actually lives in and leaves a few bright
    // dots that stay invisible however high the strength goes.
    float v = clamp(uDirtGain * pow(clamp(img, 0.0, 1.0), uDirtToe), 0.0, 1.0);

    frag_color = vec4(v, v, v, 1.0);
}
