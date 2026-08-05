/**
 * @file class1/deferred/shadowUtil.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

/**
 * FILTERING. The shadow term is a Gaussian-weighted PCF:
 *   - Default path (SHADOW_PCF_GATHER): one hardware gather-compare per 2x2 texel block
 *     (textureGather(sampler2DShadow, uv, dref), core since GLSL 4.00). A K x K kernel
 *     costs (K/2)^2 fetches, each returning four comparisons we weight individually --
 *     a true separable Gaussian, not a box.
 *   - Fallback path: the "optimized PCF" of Castano/MJP -- fixed depth-compare taps whose
 *     bilinear footprints synthesise the same window with 4 (3x3) or 9 (5x5) fetches. Used
 *     wherever a gather-compare is unavailable or undesirable, including any context that
 *     compiles below #version 400 (see the __VERSION__ guard below).
 *   - SHADOW_PCSS: contact-hardening soft shadows. A blocker search sizes the penumbra per
 *     pixel, so a shadow is sharp where its caster meets the receiver and softens with the
 *     gap. Sampled on rotated Vogel discs. Needs raw depth, so it swaps the sampler type.
 * All replace the old fixed 5-tap-with-4x-centre-plus-Y-jitter kernel, which was really a
 * box with dithered snapping (hence the characteristic blocky edge).
 *
 * BIAS. Receiver-plane depth bias (SHADOW_RPDB): the per-pixel depth slope in shadow space,
 * solved from the screen-space derivatives of the receiver position, so a tap offset by N
 * texels compares against the depth the receiver plane actually has there. This is what lets
 * the constant depth bias stay small without acne, and removes the acne/peter-panning
 * tradeoff a single global bias forces. The derivatives are taken ONCE at the top of the
 * public samplers, in uniform control flow -- dFdx/dFdy inside the divergent cascade `if`s
 * would be undefined -- then transformed into each cascade's light space by plain matrix
 * math (legal in a branch; it is not a derivative). The normal-offset push and constant bias
 * are retained; with RPDB active the constant (RenderShadowBias) can be tuned down.
 *
 * DEPTH CONVENTION. The hardware-compare paths (gather and SampleCmp) are byte-identical
 * across conventions: under reverse-Z the C++ side (a) sets the shadow sampler compare func
 * to GEQUAL, (b) feeds reversed zero-to-one shadow_matrix (its bias sub-matrix drops the z
 * remap since ZO clip already yields [0,1] shadow-map z), and (c) NEGATES shadow_bias /
 * spot_shadow_bias at their single assembly site (pipeline.cpp), so `uvz.z += const_bias`
 * needs no #ifdef -- the sign rides in on the uniform. Cascade selection (spos.z vs
 * shadow_clip) is view-space and convention-independent, and RPDB's dz_texel is measured in
 * whichever z space shadow_matrix produced, so it follows automatically. Only PCSS, which
 * reads raw depth and does its own comparisons, has to branch: see compareDepth4,
 * findBlockerDepth and the penumbra-radius gap below.
 */

// ---- filter configuration (override from the global defines to retune without editing) ----
#ifndef SHADOW_PCF_GATHER
#define SHADOW_PCF_GATHER 1     // 1: gather-compare Gaussian PCF (default). 0: optimized compare taps.
#endif
#ifndef SHADOW_PCF_KERNEL
#define SHADOW_PCF_KERNEL 4     // kernel width in texels. Gather: even (4 => 4 fetches/16 texels).
                                // Compare-tap fallback: 3 or 5.
#endif
#ifndef SHADOW_RPDB
#define SHADOW_RPDB 1           // receiver-plane depth bias
#endif
#ifndef SHADOW_RPDB_MAXBIAS
#define SHADOW_RPDB_MAXBIAS 0.01 // cap on |per-texel z bias| (normalized depth) -- silhouette guard
#endif

// ---- PCSS (contact-hardening) configuration ----
//
// PCSS needs the BLOCKER DEPTH, which a depth-compare sampler cannot return -- it only ever
// yields the comparison result. So the PCSS path declares the maps as ordinary sampler2D and
// does the depth compare in the shader. That costs no extra texture units (the alternative,
// binding all six maps a second time through a non-compare sampler, would have taken four to
// six more and shrunk the indexed-batch ladder for the whole renderer) and no extra fetches:
// one textureGather returns four raw depths, and four `step`s reproduce exactly what a
// gather-compare would have returned. LLPipeline::bindShadowMaps selects the matching sampler
// (compare vs plain) from the same quality setting that sets this define -- the two MUST agree,
// which is why both read AlchemyRenderShadowFilterQuality and nothing else.
#ifndef SHADOW_PCSS
#define SHADOW_PCSS 0
#endif
#ifndef SHADOW_PCSS_SCALE
#define SHADOW_PCSS_SCALE 200.0 // receiver-blocker depth delta -> penumbra radius, in texels
#endif
#ifndef SHADOW_PCSS_MAX_RADIUS
#define SHADOW_PCSS_MAX_RADIUS 12.0 // penumbra clamp, texels (bounds the sparse-tap spread)
#endif
#ifndef SHADOW_PCSS_SEARCH
#define SHADOW_PCSS_SEARCH 6.0  // blocker-search radius, texels
#endif

// textureGather on a shadow sampler is core in GLSL 4.00. The viewer's floor is GL 4.1, but
// llshadermgr still emits #version 330/150 for a 3.x context, and a gather there would fail
// to compile rather than fall back -- so pin both gather paths to 400+. PCSS has no
// non-gather implementation, so below 400 it degrades to the fixed-kernel filter entirely.
#if __VERSION__ < 400
#undef SHADOW_PCF_GATHER
#define SHADOW_PCF_GATHER 0
#undef SHADOW_PCSS
#define SHADOW_PCSS 0
#endif

#if SHADOW_PCSS
#define AL_SHADOW_SAMPLER sampler2D        // raw depth; compared in-shader
#else
#define AL_SHADOW_SAMPLER sampler2DShadow  // hardware depth compare
#endif

uniform sampler2D   normalMap;

#if defined(SUN_SHADOW)
uniform AL_SHADOW_SAMPLER shadowMap0;
uniform AL_SHADOW_SAMPLER shadowMap1;
uniform AL_SHADOW_SAMPLER shadowMap2;
uniform AL_SHADOW_SAMPLER shadowMap3;
#endif

#if defined(SPOT_SHADOW)
uniform AL_SHADOW_SAMPLER shadowMap4;
uniform AL_SHADOW_SAMPLER shadowMap5;
#endif

uniform vec3 sun_dir;
uniform vec3 moon_dir;
// Shared shadow/SSAO constants, spliced from class1/deferred/deferredBlock.glsl and
// bound at UB_DEFERRED. Members are read by bare name.
//[ENGINE_BLOCK Deferred]
// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]
uniform vec2 screen_res;
uniform int sun_up_factor;

#if defined(SUN_SHADOW) || defined(SPOT_SHADOW)

// Solve the shadow-space depth gradient from the receiver's screen-space derivatives and
// return it as a per-texel (du,dv) depth step, clamped so a runaway gradient at a depth
// discontinuity cannot punch a light-leak hole. duv*/dz* are already post-perspective-divide.
vec2 receiverPlaneDepthBias(vec2 duvdx, vec2 duvdy, float dzdx, float dzdy, vec2 texel)
{
    // [ du/dx  dv/dx ] (dz/du)   (dz/dx)
    // [ du/dy  dv/dy ] (dz/dv) = (dz/dy)
    float det = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    vec2 dz_duv = vec2(0.0, 0.0);
    if (abs(det) > 1e-8)
    {
        float inv = 1.0 / det;
        dz_duv.x = ( duvdy.y * dzdx - duvdx.y * dzdy) * inv;
        dz_duv.y = (-duvdy.x * dzdx + duvdx.x * dzdy) * inv;
    }
    vec2 dz_texel = dz_duv * texel;
    return clamp(dz_texel, vec2(-SHADOW_RPDB_MAXBIAS), vec2(SHADOW_RPDB_MAXBIAS));
}

#if SHADOW_PCSS

// Vogel (golden-angle) discs: near-uniform area coverage, unlike a couple of concentric rings
// at fixed angles, which leaves wedge-shaped gaps that alias into visible structure once the
// disc is scaled up to a wide penumbra. Generated as r=sqrt((i+0.5)/n), theta=i*2.39996.
// Precomputed rather than evaluated per tap so the per-pixel rotation below costs ONE sincos
// instead of one per sample.
const vec2 kPCSSBlockerDisc[8] = vec2[8](
    vec2(+0.250000, +0.000000), vec2(-0.319290, +0.292496),
    vec2(+0.048872, -0.556877), vec2(+0.402444, +0.524918),
    vec2(-0.738535, -0.130636), vec2(+0.699605, -0.445031),
    vec2(-0.234004, +0.870484), vec2(-0.446271, -0.859268)
);

const vec2 kPCSSFilterDisc[12] = vec2[12](
    vec2(+0.204124, +0.000000), vec2(-0.260699, +0.238822),
    vec2(+0.039904, -0.454688), vec2(+0.328595, +0.428593),
    vec2(-0.603011, -0.106664), vec2(+0.571225, -0.363367),
    vec2(-0.191064, +0.710747), vec2(-0.364379, -0.701590),
    vec2(+0.790557, +0.288710), vec2(-0.822442, +0.339492),
    vec2(+0.396472, -0.847237), vec2(+0.292982, +0.934074)
);

// Interleaved gradient noise -- the per-pixel rotation angle for the discs above.
//
// A PCSS disc is necessarily a SPARSE sample of the penumbra (12 gathers cover ~48 of the
// several hundred texels a wide penumbra spans). With the same pattern on every pixel that
// undersampling is correlated between neighbours and reads as blocks and bands; rotating each
// pixel's disc independently turns the identical error budget into high-frequency noise, which
// the eye integrates across a soft edge. Screen space is the right domain for the hash: hashing
// shadow-map position would hand neighbouring screen pixels the SAME rotation exactly where the
// map is magnified, which is the case that blocks in the first place.
float pcssRotation(vec2 pos_screen)
{
    // Scaled into a pixel-ish domain; only needs to decorrelate adjacent pixels.
    vec2 p = pos_screen * 2048.0;
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))) * 6.2831853;
}

// In-shader replacement for the hardware depth compare: lit where ref <= depth (GL_LEQUAL),
// or ref >= depth under reverse-Z (GL_GEQUAL -- the func ALSamplerCache::makeDesc selects for
// the compare samplers the non-PCSS paths use, mirrored here so both tiers agree).
// The ref is saturated so a receiver pushed past either end of the depth range by its bias
// resolves to a definite answer instead of drifting outside what the map can store. Do not
// justify this by the fixed-point hardware clamp: reverse-Z forces DEPTH_FMT_32F, where the
// hardware compare does not clamp at all. The saturation is symmetric under both conventions
// (forward pushes ref past 1 near the far plane, reverse-Z past 0), so the two tiers agree.
vec4 compareDepth4(vec4 depth, float ref)
{
#ifdef REVERSE_Z
    return step(depth, vec4(clamp(ref, 0.0, 1.0)));
#else
    return step(vec4(clamp(ref, 0.0, 1.0)), depth);
#endif
}

// Rotate a disc offset by the per-pixel angle. cs = (cos phi, sin phi).
vec2 pcssRotate(vec2 v, vec2 cs)
{
    return vec2(v.x * cs.x - v.y * cs.y, v.x * cs.y + v.y * cs.x);
}

// Blocker search: mean depth of the texels nearer the light than the receiver. Returns a
// negative value when nothing occludes this pixel, which the caller treats as fully lit --
// the early-out that keeps PCSS affordable over open ground.
float findBlockerDepth(sampler2D shadowMap, vec3 uvz, vec2 texel, vec2 dz_texel, vec2 cs)
{
    float sum   = 0.0;
    float count = 0.0;

    for (int i = 0; i < 8; ++i)
    {
        vec2 off = pcssRotate(kPCSSBlockerDisc[i], cs) * SHADOW_PCSS_SEARCH;
        vec4 d   = textureGather(shadowMap, uvz.xy + off * texel, 0);
        // Follow the receiver plane out to the tap, exactly as the filter does, so a sloped
        // receiver is not mistaken for its own blocker.
        float ref = uvz.z + dot(off, dz_texel);

        // 1 where the texel is closer to the light: smaller depth forward, larger under
        // reverse-Z (near=1, far=0).
#ifdef REVERSE_Z
        vec4 isBlocker = step(vec4(ref), d);
#else
        vec4 isBlocker = step(d, vec4(ref));
#endif
        sum   += dot(d, isBlocker);
        count += dot(isBlocker, vec4(1.0));
    }

    return (count > 0.0) ? (sum / count) : -1.0;
}

// Percentage-closer soft shadows: estimate the penumbra from the receiver-blocker depth gap,
// then run a variable-radius gather PCF. Contact regions have a small gap -> tight radius ->
// a sharp shadow; distant occluders give a large gap -> wide radius -> a soft one.
float filterShadowPCSS(sampler2D shadowMap, vec3 uvz, vec2 res, vec2 dz_texel, vec2 pos_screen)
{
    vec2 texel = 1.0 / res;

    float phi = pcssRotation(pos_screen);
    vec2  cs  = vec2(cos(phi), sin(phi));

    float blocker = findBlockerDepth(shadowMap, uvz, texel, dz_texel, cs);
    if (blocker < 0.0)
    {
        return 1.0; // no occluder found -- fully lit
    }

    // Penumbra radius in texels, from the receiver-blocker depth gap. The scale folds the
    // light's angular size and the cascade's depth range into one tunable; clamped below at
    // one texel so the filter never degenerates to a single point sample, and above to bound
    // the sparse taps' spacing. The receiver is the FARTHER of the two, so it holds the larger
    // depth forward and the smaller one under reverse-Z -- swap the operands so the gap stays
    // positive (a negative gap would clamp to the 1-texel floor and kill contact hardening).
#ifdef REVERSE_Z
    float radius = clamp((blocker - uvz.z) * SHADOW_PCSS_SCALE, 1.0, SHADOW_PCSS_MAX_RADIUS);
#else
    float radius = clamp((uvz.z - blocker) * SHADOW_PCSS_SCALE, 1.0, SHADOW_PCSS_MAX_RADIUS);
#endif

    float shadow = 0.0;
    float wsum   = 0.0;

    for (int i = 0; i < 12; ++i)
    {
        vec2 unit = kPCSSFilterDisc[i];
        vec2 off  = pcssRotate(unit, cs) * radius;
        vec4 d    = textureGather(shadowMap, uvz.xy + off * texel, 0);
        float ref = uvz.z + dot(off, dz_texel);

        // Smooth radial falloff, zero at the disc edge. The Vogel points are area-uniform, so
        // this is the whole kernel shape -- and because it reaches zero, a tap does not pop in
        // or out as `radius` changes between neighbouring pixels (same reasoning as the
        // fixed-kernel path's apodization).
        float w = 1.0 - dot(unit, unit);
        shadow += dot(compareDepth4(d, ref), vec4(0.25)) * w;
        wsum   += w;
    }

    return shadow / wsum;
}

#else // !SHADOW_PCSS -- hardware depth-compare paths

#if SHADOW_PCF_GATHER

// Gather-compare Gaussian PCF. uvz = post-divide (u, v, z); res = shadow map resolution;
// dz_texel = receiver-plane depth step per texel (0 when RPDB is off).
float filterShadowGather(sampler2DShadow shadowMap, vec3 uvz, vec2 res, vec2 dz_texel)
{
    const int K = SHADOW_PCF_KERNEL;
    vec2 texel = 1.0 / res;
    vec2 c = uvz.xy * res;                    // receiver in texel space
    vec2 base = floor(c + 0.5) - float(K / 2); // lower-left kernel texel index

    // Separable Gaussian weights, sigma ~ quarter of the kernel width. Apodized: the Gaussian's
    // value at the window edge (K/2 texels from centre) is subtracted off, so a texel entering
    // or leaving the fixed K-texel window carries ~zero weight. Without this the edge texels pop
    // between 0 and exp(-2) as the window slides across shadow texels -- invisible when texels
    // are sub-pixel on screen, but banding/blockiness on the penumbra up close where a texel
    // spans many pixels (the artefact the old per-pixel jitter masked, now removed at the root).
    const float sigma    = float(K) * 0.25;
    const float inv2s2   = 1.0 / (2.0 * sigma * sigma);
    const float edge     = float(K) * 0.5;
    const float pedestal = exp(-edge * edge * inv2s2);
    float wx[K];
    float wy[K];
    for (int i = 0; i < K; ++i)
    {
        float dx = (base.x + float(i) + 0.5) - c.x;
        float dy = (base.y + float(i) + 0.5) - c.y;
        wx[i] = max(0.0, exp(-dx * dx * inv2s2) - pedestal);
        wy[i] = max(0.0, exp(-dy * dy * inv2s2) - pedestal);
    }

    float shadow = 0.0;
    float wsum   = 0.0;
    for (int by = 0; by < K; by += 2)
    {
        for (int bx = 0; bx < K; bx += 2)
        {
            // Gather the 2x2 block whose lower-left texel index is (base+bx, base+by): the
            // gather point is that block's centre, so floor(P*res - 0.5) lands on it.
            vec2 P = (base + vec2(float(bx), float(by)) + 1.0) * texel;
            // One compare ref per block, RPDB-shifted by the block centre's texel offset.
            vec2 blockOff = (base + vec2(float(bx), float(by)) + 1.0) - c;
            float ref = uvz.z + dot(blockOff, dz_texel);
            vec4 g = textureGather(shadowMap, P, ref);
            // textureGather order: .x=(0,1) .y=(1,1) .z=(1,0) .w=(0,0), relative to the block.
            shadow += g.w * (wx[bx]     * wy[by]);
            shadow += g.z * (wx[bx + 1] * wy[by]);
            shadow += g.x * (wx[bx]     * wy[by + 1]);
            shadow += g.y * (wx[bx + 1] * wy[by + 1]);
            wsum   += (wx[bx] + wx[bx + 1]) * (wy[by] + wy[by + 1]);
        }
    }
    return shadow / wsum;
}

#else // !SHADOW_PCF_GATHER

// Depth-compare tap with the receiver-plane bias folded into the compare ref. off is the
// tap's texel offset from the receiver.
float cmpTap(sampler2DShadow shadowMap, vec2 uv, float z, vec2 off, vec2 dz_texel)
{
    return texture(shadowMap, vec3(uv, z + dot(off, dz_texel)));
}

// Optimized bilinear PCF (Castano/MJP): fixed compare taps whose hardware 2x2 footprints
// reconstruct a KxK tent. Fallback for SHADOW_PCF_GATHER == 0.
float filterShadowSampleCmp(sampler2DShadow shadowMap, vec3 uvz, vec2 res, vec2 dz_texel)
{
    vec2  texel   = 1.0 / res;
    vec2  uv      = uvz.xy * res;
    vec2  base_uv = floor(uv + 0.5);
    float s       = uv.x + 0.5 - base_uv.x;
    float t       = uv.y + 0.5 - base_uv.y;
    base_uv       = (base_uv - 0.5) * texel;
    float z       = uvz.z;

#if SHADOW_PCF_KERNEL <= 3
    // 3x3, four weighted taps.
    float uw0 = (3.0 - 2.0 * s), uw1 = (1.0 + 2.0 * s);
    float u0  = (2.0 - s) / uw0 - 1.0, u1 = s / uw1 + 1.0;
    float vw0 = (3.0 - 2.0 * t), vw1 = (1.0 + 2.0 * t);
    float v0  = (2.0 - t) / vw0 - 1.0, v1 = t / vw1 + 1.0;

    float sum = 0.0;
    sum += uw0 * vw0 * cmpTap(shadowMap, base_uv + vec2(u0, v0) * texel, z, vec2(u0 - s, v0 - t), dz_texel);
    sum += uw1 * vw0 * cmpTap(shadowMap, base_uv + vec2(u1, v0) * texel, z, vec2(u1 - s, v0 - t), dz_texel);
    sum += uw0 * vw1 * cmpTap(shadowMap, base_uv + vec2(u0, v1) * texel, z, vec2(u0 - s, v1 - t), dz_texel);
    sum += uw1 * vw1 * cmpTap(shadowMap, base_uv + vec2(u1, v1) * texel, z, vec2(u1 - s, v1 - t), dz_texel);
    return sum * (1.0 / 16.0);
#else
    // 5x5, nine weighted taps.
    float uw0 = (4.0 - 3.0 * s), uw1 = 7.0, uw2 = (1.0 + 3.0 * s);
    float u0  = (3.0 - 2.0 * s) / uw0 - 2.0, u1 = (3.0 + s) / uw1, u2 = s / uw2 + 2.0;
    float vw0 = (4.0 - 3.0 * t), vw1 = 7.0, vw2 = (1.0 + 3.0 * t);
    float v0  = (3.0 - 2.0 * t) / vw0 - 2.0, v1 = (3.0 + t) / vw1, v2 = t / vw2 + 2.0;

    float sum = 0.0;
    sum += uw0 * vw0 * cmpTap(shadowMap, base_uv + vec2(u0, v0) * texel, z, vec2(u0 - s, v0 - t), dz_texel);
    sum += uw1 * vw0 * cmpTap(shadowMap, base_uv + vec2(u1, v0) * texel, z, vec2(u1 - s, v0 - t), dz_texel);
    sum += uw2 * vw0 * cmpTap(shadowMap, base_uv + vec2(u2, v0) * texel, z, vec2(u2 - s, v0 - t), dz_texel);
    sum += uw0 * vw1 * cmpTap(shadowMap, base_uv + vec2(u0, v1) * texel, z, vec2(u0 - s, v1 - t), dz_texel);
    sum += uw1 * vw1 * cmpTap(shadowMap, base_uv + vec2(u1, v1) * texel, z, vec2(u1 - s, v1 - t), dz_texel);
    sum += uw2 * vw1 * cmpTap(shadowMap, base_uv + vec2(u2, v1) * texel, z, vec2(u2 - s, v1 - t), dz_texel);
    sum += uw0 * vw2 * cmpTap(shadowMap, base_uv + vec2(u0, v2) * texel, z, vec2(u0 - s, v2 - t), dz_texel);
    sum += uw1 * vw2 * cmpTap(shadowMap, base_uv + vec2(u1, v2) * texel, z, vec2(u1 - s, v2 - t), dz_texel);
    sum += uw2 * vw2 * cmpTap(shadowMap, base_uv + vec2(u2, v2) * texel, z, vec2(u2 - s, v2 - t), dz_texel);
    return sum * (1.0 / 144.0);
#endif
}

#endif // SHADOW_PCF_GATHER

#endif // SHADOW_PCSS

// Common core: post-divide, apply constant + receiver-plane bias, run the selected filter.
// lpos is the pre-divide light-clip position; dLdx/dLdy its screen-space derivatives
// (already transformed into this map's light space by the caller, in uniform control flow).
float filterShadow(AL_SHADOW_SAMPLER shadowMap, vec4 lpos, vec4 dLdx, vec4 dLdy,
                   vec2 res, float const_bias, vec2 pos_screen)
{
    float w   = lpos.w;
    vec3  uvz = lpos.xyz / w;
    uvz.z += const_bias;

    vec2 dz_texel = vec2(0.0, 0.0);
#if SHADOW_RPDB
    // post-perspective-divide derivatives of (u, v, z)
    vec2  duvdx = (dLdx.xy - uvz.xy * dLdx.w) / w;
    vec2  duvdy = (dLdy.xy - uvz.xy * dLdy.w) / w;
    float dzdx  = (dLdx.z  - uvz.z  * dLdx.w) / w;
    float dzdy  = (dLdy.z  - uvz.z  * dLdy.w) / w;
    dz_texel = receiverPlaneDepthBias(duvdx, duvdy, dzdx, dzdy, 1.0 / res);
#endif

#if SHADOW_PCSS
    return filterShadowPCSS(shadowMap, uvz, res, dz_texel, pos_screen);
#elif SHADOW_PCF_GATHER
    return filterShadowGather(shadowMap, uvz, res, dz_texel);
#else
    return filterShadowSampleCmp(shadowMap, uvz, res, dz_texel);
#endif
}

#endif // SUN_SHADOW || SPOT_SHADOW

float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen)
{
#if defined(SUN_SHADOW)
    // Receiver derivatives, taken here in uniform control flow (before the cascade branches
    // below diverge) so the per-cascade receiver-plane bias is well-defined.
    vec3 dpos_dx = dFdx(pos);
    vec3 dpos_dy = dFdy(pos);

    float shadow = 0.0;
    vec3 light_dir = normalize((sun_up_factor == 1) ? sun_dir : moon_dir);

    float dp_directional_light = max(0.0, dot(norm.xyz, light_dir));
    dp_directional_light = clamp(dp_directional_light, 0.0, 1.0);

    vec3 shadow_pos = pos.xyz;
    vec3 offset = light_dir.xyz * (1.0 - dp_directional_light);
    shadow_pos += offset * shadow_offset * 2.0;

    vec4 spos = vec4(shadow_pos.xyz, 1.0);
    vec4 ddx4 = vec4(dpos_dx, 0.0);
    vec4 ddy4 = vec4(dpos_dy, 0.0);
    float const_bias = shadow_bias * 2.0;

    if (spos.z > -shadow_clip.w)
    {
        vec4 lpos;
        vec4 near_split = shadow_clip * -0.75;
        vec4 far_split = shadow_clip * -1.25;
        vec4 transition_domain = near_split - far_split;
        float weight = 0.0;

        if (spos.z < near_split.z)
        {
            lpos = shadow_matrix[3] * spos;
            vec4 dLdx = shadow_matrix[3] * ddx4;
            vec4 dLdy = shadow_matrix[3] * ddy4;

            float w = 1.0;
            w -= max(spos.z - far_split.z, 0.0) / transition_domain.z;
            shadow += filterShadow(shadowMap3, lpos, dLdx, dLdy, shadow_res, const_bias, pos_screen) * w;
            weight += w;
            shadow += max((pos.z + shadow_clip.z) / (shadow_clip.z - shadow_clip.w) * 2.0 - 1.0, 0.0);
        }

        if (spos.z < near_split.y && spos.z > far_split.z)
        {
            lpos = shadow_matrix[2] * spos;
            vec4 dLdx = shadow_matrix[2] * ddx4;
            vec4 dLdy = shadow_matrix[2] * ddy4;

            float w = 1.0;
            w -= max(spos.z - far_split.y, 0.0) / transition_domain.y;
            w -= max(near_split.z - spos.z, 0.0) / transition_domain.z;
            shadow += filterShadow(shadowMap2, lpos, dLdx, dLdy, shadow_res, const_bias, pos_screen) * w;
            weight += w;
        }

        if (spos.z < near_split.x && spos.z > far_split.y)
        {
            lpos = shadow_matrix[1] * spos;
            vec4 dLdx = shadow_matrix[1] * ddx4;
            vec4 dLdy = shadow_matrix[1] * ddy4;

            float w = 1.0;
            w -= max(spos.z - far_split.x, 0.0) / transition_domain.x;
            w -= max(near_split.y - spos.z, 0.0) / transition_domain.y;
            shadow += filterShadow(shadowMap1, lpos, dLdx, dLdy, shadow_res, const_bias, pos_screen) * w;
            weight += w;
        }

        if (spos.z > far_split.x)
        {
            lpos = shadow_matrix[0] * spos;
            vec4 dLdx = shadow_matrix[0] * ddx4;
            vec4 dLdy = shadow_matrix[0] * ddy4;

            float w = 1.0;
            w -= max(near_split.x - spos.z, 0.0) / transition_domain.x;
            shadow += filterShadow(shadowMap0, lpos, dLdx, dLdy, shadow_res, const_bias, pos_screen) * w;
            weight += w;
        }

        shadow /= weight;
    }
    else
    {
        return 1.0; // lit beyond the far split...
    }
    return shadow;
#else
    return 1.0;
#endif
}

float sampleSpotShadow(vec3 pos, vec3 norm, int index, vec2 pos_screen)
{
#if defined(SPOT_SHADOW)
    vec3 dpos_dx = dFdx(pos);
    vec3 dpos_dy = dFdy(pos);

    float shadow = 0.0;
    pos += norm * spot_shadow_offset;

    vec4 spos = vec4(pos, 1.0);
    vec4 ddx4 = vec4(dpos_dx, 0.0);
    vec4 ddy4 = vec4(dpos_dy, 0.0);
    float const_bias = spot_shadow_bias * 0.8;

    if (spos.z > -shadow_clip.w)
    {
        vec4 lpos;

        vec4 near_split = shadow_clip * -0.75;
        vec4 far_split = shadow_clip * -1.25;
        vec4 transition_domain = near_split - far_split;
        float weight = 0.0;

        {
            float w = 1.0;
            w -= max(spos.z - far_split.z, 0.0) / transition_domain.z;

            // A sampler2DShadow cannot live in a local or a ternary (GLSL opaque type), so pick
            // the map by branch and pass it straight in.
            if (index == 0)
            {
                lpos = shadow_matrix[4] * spos;
                vec4 dLdx = shadow_matrix[4] * ddx4;
                vec4 dLdy = shadow_matrix[4] * ddy4;
                shadow += filterShadow(shadowMap4, lpos, dLdx, dLdy, proj_shadow_res, const_bias, pos_screen) * w;
            }
            else
            {
                lpos = shadow_matrix[5] * spos;
                vec4 dLdx = shadow_matrix[5] * ddx4;
                vec4 dLdy = shadow_matrix[5] * ddy4;
                shadow += filterShadow(shadowMap5, lpos, dLdx, dLdy, proj_shadow_res, const_bias, pos_screen) * w;
            }

            weight += w;
            shadow += max((pos.z + shadow_clip.z) / (shadow_clip.z - shadow_clip.w) * 2.0 - 1.0, 0.0);
        }

        shadow /= weight;
    }
    else
    {
        shadow = 1.0;
    }
    return shadow;
#else
    return 1.0;
#endif
}
