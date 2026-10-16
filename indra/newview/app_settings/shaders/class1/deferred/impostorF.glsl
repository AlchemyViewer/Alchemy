/**
 * @file impostorF.glsl
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
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

out vec4 frag_data[4];

uniform float minimum_alpha;


uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
uniform sampler2D specularMap;
#if defined(HAS_EMISSIVE)
uniform sampler2D emissiveMap;
#endif

// Rotation taking a baked normal from the impostor's view basis into the current one --
// main_view * inverse(bake_view), rebuilt per frame by LLVOAvatar::renderImpostor.
uniform mat3 impostor_norm_rot;

in vec2 vary_texcoord0;

vec3 linear_to_srgb(vec3 c);
vec3 srgb_to_linear(vec3 c);

vec4 encodeNormal(vec3 n, float env, float gbuffer_flag);
vec4 decodeNormal(vec4 norm);

void main()
{
    vec4 col = texture(diffuseMap, vary_texcoord0.xy);

    if (col.a < minimum_alpha)
    {
        discard;
    }

    // The bake stores colour PREMULTIPLIED by coverage: translucent geometry was blended over
    // a cleared black background, so a half-covered hair texel holds half its own colour.
    // Dividing the coverage back out recovers the surface colour, which is what this opaque
    // G-buffer replay needs -- without it, every partially covered texel was handed to the
    // lighting pass darkened toward black and then declared fully opaque, which is the dark
    // halo around hair and translucent edges.
    //
    // Opaque texels carry coverage 1 from the stamp pass, so this is a no-op for them.
    //
    // The division has to happen in LINEAR. The bake premultiplied in linear -- the target is
    // sRGB and GL_FRAMEBUFFER_SRGB is on for it, so the blender decodes, blends and re-encodes
    // -- while this shader samples with skip-decode and therefore holds the ENCODED value.
    // Dividing encoded bits by a linear coverage is not the inverse of anything: encode() is
    // concave, so encode(a*s)/a overshoots encode(s), by more than 2x at half coverage.
    col.rgb = linear_to_srgb(srgb_to_linear(col.rgb) / max(col.a, minimum_alpha));

    vec4 norm = texture(normalMap,   vary_texcoord0.xy);
    vec4 spec = texture(specularMap, vary_texcoord0.xy);

    // Did any G-buffer writer actually cover this texel?
    //
    // Forward alpha writes ONLY the albedo attachment -- it declares a single output, and the
    // bake narrows the draw buffers to match -- so wherever translucent geometry drew over
    // background rather than over the avatar's body, the normal, ORM and flag attachments
    // still hold the clear. A cleared normal texel decodes to (0, 0, -1), pointing directly
    // AWAY from the viewer, and the lighting pass duly shades it as a back face.
    //
    // That has always been wrong. It went unnoticed because the answer was at least constant,
    // so it read as flat and slightly dark; rebasing it into the current view basis makes it
    // swing as the camera pans, which is what turns a static error into an obvious one.
    //
    // The flag discriminates: every real writer sets one (HAS_ATMOS, 0.34, is the lowest) and
    // the clear is 0. The threshold sits between them and survives the 2-bit alpha of the
    // RGB10_A2 normal target used when HDR is off.
    bool has_gbuffer = norm.w > 0.17;

    // Carry the bake's material flag through for PBR, and ONLY for PBR.
    //
    // The flag lives in the normal buffer's .w, which forward alpha never writes -- so where
    // blended geometry drew over background it reads back as the clear, 0, which is
    // GBUFFER_FLAG_SKIP_ATMOS. Passing that straight through would route hair fringes into
    // softenLight's WL-sky branch and multiply them by sky_hdr_scale. Anything that is not
    // recognisably PBR therefore keeps the legacy flag this shader always wrote, which is
    // what those pixels were already being lit as.
    //
    // For real PBR surfaces this is the difference between being lit as PBR -- metal,
    // roughness, IBL -- and being reinterpreted as legacy blinn-phong, which read the baked
    // ORM as an sRGB specular colour and produced no highlight at all, because pbropaque
    // writes spec alpha 0. The 0.1 tolerance matches GET_GBUFFER_FLAG().
    float material_flag = (has_gbuffer && abs(norm.w - GBUFFER_FLAG_HAS_PBR) < 0.1)
                              ? GBUFFER_FLAG_HAS_PBR
                              : GBUFFER_FLAG_HAS_ATMOS;

    // Rebase into the current view basis before handing the normal to the G-buffer. The bake
    // camera was aimed at the avatar, so its basis differs from the main camera's by the
    // avatar's angular offset from screen centre -- up to half the field of view. Replayed
    // verbatim, the lighting pass shaded impostors from a direction that drifted as they
    // moved across the screen, and disagreed with the live avatars beside them.
    //
    // .z is the environment intensity and .w the material flag; only .xy carry the direction,
    // so decode/rotate/re-encode rather than touching the packed bits.
    //
    // Texels the bake never wrote a normal for face the viewer instead. For a billboard that
    // is the only defensible answer -- there is no surface orientation to recover -- and it
    // at least gives translucent geometry a sane diffuse response rather than a back face.
    vec3 view_normal = has_gbuffer
                           ? normalize(impostor_norm_rot * decodeNormal(norm).xyz)
                           : vec3(0.0, 0.0, 1.0);

    frag_data[0] = vec4(col.rgb, 0.0);
    frag_data[1] = spec;
    frag_data[2] = encodeNormal(view_normal, norm.z, material_flag);

#if defined(HAS_EMISSIVE)
    // The bake has a real emissive attachment (addDeferredAttachments gives the impostor the
    // same set as deferredScreen), and this used to throw it away. That is what made an
    // emissive attachment go dark, and contribute nothing to bloom, the instant its avatar
    // impostored. It is also what the PBR branch above reads as colorEmissive.
    frag_data[3] = vec4(texture(emissiveMap, vary_texcoord0.xy).rgb, 0.0);
#endif
}
