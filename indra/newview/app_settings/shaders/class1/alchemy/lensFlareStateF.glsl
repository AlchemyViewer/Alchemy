/**
 * @file lensFlareStateF.glsl
 * @brief Per-frame lens flare state: how much of the sun is unoccluded, what
 *        colour it is, and a temporally filtered "drive" the colour-correct
 *        pass multiplies its flare by. Written to a 2x1 target; read back next
 *        frame as its own history, exactly like the exposure map.
 *
 * Why a separate pass: the occlusion test used to run inside computeLensFlare,
 * per fragment and from scratch every frame. Its answer is the same for every
 * pixel on screen, and it was binary in time -- the sun's centre pixel gets
 * covered, the whole flare (a streak across the frame, halo, ghosts) is gone
 * in one frame and back the next. A moving camera behind fence posts or
 * foliage turned that into a strobe. Here the visibility can only change at
 * a bounded rate, whatever the geometry does.
 *
 * Layout (texelFetch, no filtering):
 *   texel 0: rgb = drive, linear HDR: sun colour x HDR gate x coverage x edge
 *                  fade, filtered. Premultiplied on purpose -- fading out to
 *                  black holds the last sun colour for free.
 *            a   = instability score, sign-packed with the direction of the
 *                  last significant move of the raw target.
 *   texel 1: r = raw target luminance, g = reference luminance (a decaying
 *            running maximum the slew limit is relative to), b = raw coverage,
 *            a = the dt this frame used. b and a are for inspection only.
 *
 * Every constant below was chosen by scripts/content_tools/check_lens_flare_state.py,
 * which mirrors this file statement for statement. Change them together.
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
 *
 * $/LicenseInfo$
 */

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

uniform sampler2D diffuseRect;          // linear HDR scene
uniform sampler2D depthMap;             // final scene depth (after alpha)
uniform sampler2D uLensFlareStateMap;   // this target, last frame

uniform float dt;                       // frame interval, seconds
uniform uint  uFrameId;
uniform vec2  uResolution;              // framebuffer size in pixels
uniform vec2  uLensFlareSunPos;         // sun (or moon) centre in UV
uniform float uLensFlareSunVisibility;  // CPU screen-edge fade, 0..1
uniform float uLensFlareOcclusionRadius;// probe radius as a fraction of screen height
uniform vec4  uLensFlareFadeParams;     // x tau_in, y tau_out (s); z slew_in, w slew_out (1/s)

const vec3  LUMA         = vec3(0.2126, 0.7152, 0.0722);
const float GOLDEN_ANGLE = 2.39996322972865332;

// ---- Spatial kernel ---------------------------------------------------------
// A Fermat spiral of 48 taps: uniform area density with no angular clustering,
// so a straight edge through the centre reads within 0.02 of half at every
// angle, and no single tap carries more than 3.3% of the weight. The ad-hoc
// Poisson table it replaces swung 0.45..0.65 with the edge angle.
const int   TAP_COUNT = 48;
const float K_COVER   = 1.0;    // coverage weight  exp(-K_COVER * r^2)
const float K_COLOR   = 8.0;    // colour weight    exp(-K_COLOR * r^2): tight, so the
                                // unoccluded mean stays within 2% of the centre texel

// ---- Temporal filter --------------------------------------------------------
const float DT_MAX      = 0.25; // a stall converges, it does not teleport
const float TAU_REF     = 2.0;  // reference luminance decay, seconds
const float STEP_THRESH = 0.1;  // raw-target move that counts as a step, relative to the reference
const float GAIN        = 0.35; // instability added per direction reversal
const float TAU_I       = 1.0;  // instability decay, seconds
const float I_DEADZONE  = 0.30; // one reversal (an ordinary reveal) slows nothing
const float K_DAMP      = 20.0; // tau multiplier slope above the dead zone

float skyOf(float d)
{
    // Only the far layers count as unoccluded: the sun disc is pinned at
    // 0.999999 and the sky dome at 0.99999 (forward); mirrored under reverse-Z.
#ifdef REVERSE_Z
    return smoothstep(0.9999, 1.0, 1.0 - d);
#else
    return smoothstep(0.9999, 1.0, d);
#endif
}

void main()
{
    vec2  sun_uv    = uLensFlareSunPos;
    vec2  radius_uv = uLensFlareOcclusionRadius * vec2(uResolution.y / max(uResolution.x, 1.0), 1.0);
    // Rotate the whole pattern by the golden angle each frame: the residual
    // angular bias becomes zero-mean noise (std 0.017) the filter removes.
    float rot = float(uFrameId & 1023u) * GOLDEN_ANGLE;

    float w_sum = 0.0;
    float cover = 0.0;
    vec3  col   = vec3(0.0);
    float col_w = 0.0;
    for (int i = 0; i < TAP_COUNT; i++)
    {
        float r  = sqrt((float(i) + 0.5) / float(TAP_COUNT));
        float a  = float(i) * GOLDEN_ANGLE + rot;
        vec2  t  = vec2(cos(a), sin(a)) * r;
        float r2 = dot(t, t);
        float w  = exp(-K_COVER * r2);
        vec2  uv = sun_uv + t * radius_uv;
        w_sum += w;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        {
            // Off screen: nothing known, and the edge fade already handles a
            // sun leaving the frame. Count it as sky, keep it out of the colour.
            cover += w;
            continue;
        }
        float sky = skyOf(texture(depthMap, uv).r);
        cover += w * sky;
        float wc = exp(-K_COLOR * r2) * sky;
        col   += wc * texture(diffuseRect, uv).rgb;
        col_w += wc;
    }
    cover /= w_sum;
    col = (col_w > 0.0) ? col / col_w : vec3(0.0);

    // Only an HDR-bright sun drives the flare -- a bright diffuse wall must not.
    float sun_lum = dot(col, LUMA);
    float bright  = max(sun_lum - 2.0, 0.0) / max(sun_lum, 1e-4);
    vec3  target  = col * bright * cover * uLensFlareSunVisibility;

    // ---- Temporal filter, in luminance, RGB following ------------------------
    vec4  prev0 = texelFetch(uLensFlareStateMap, ivec2(0, 0), 0);
    vec4  prev1 = texelFetch(uLensFlareStateMap, ivec2(1, 0), 0);
    float dtc   = clamp(dt, 0.0, DT_MAX);
    float Lp    = dot(prev0.rgb, LUMA);
    float Lt    = dot(target, LUMA);
    float L_ref = max(max(prev1.g * exp(-dtc / TAU_REF), Lt), Lp);

    // Instability: direction reversals of the raw target within about a second.
    // A fence post train reverses every crossing; a single reveal does not.
    float packed = prev0.a;
    float I      = abs(packed);
    float s_prev = sign(packed);
    float delta  = (Lt - prev1.r) / max(L_ref, 1e-6);
    float s_now  = (abs(delta) > STEP_THRESH) ? sign(delta) : 0.0;
    bool  reversal = (s_now != 0.0) && (s_prev != 0.0) && (s_now != s_prev);
    I = min(I * exp(-dtc / TAU_I) + (reversal ? GAIN : 0.0), 1.0);
    float s_store = (s_now != 0.0) ? s_now : s_prev;
    packed = (s_store != 0.0) ? s_store * max(I, 1e-3) : 0.0;

    // Fade: asymmetric time constants, slowed while unstable, and a slew cap
    // relative to the reference so no frame moves the drive by more than a
    // bounded fraction of the sun's recent brightness.
    bool  rising = Lt > Lp;
    float tau    = rising ? uLensFlareFadeParams.x : uLensFlareFadeParams.y;
    tau *= 1.0 + K_DAMP * max(I - I_DEADZONE, 0.0);
    float alpha  = 1.0 - exp(-dtc / tau);
    float slew   = rising ? uLensFlareFadeParams.z : uLensFlareFadeParams.w;
    float cap    = slew * dtc * L_ref;
    float diff   = abs(Lt - Lp);
    if (diff > 1e-9)
    {
        alpha = min(alpha, cap / diff);
    }
    vec3 drive = max(mix(prev0.rgb, target, alpha), vec3(0.0));

    if (gl_FragCoord.x < 1.0)
    {
        frag_color = vec4(drive, packed);
    }
    else
    {
        frag_color = vec4(Lt, L_ref, cover, dtc);
    }
}
