/**
 * @file uiF.glsl
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

out vec4 frag_color;

uniform sampler2D diffuseMap;

// Shadow shader path. The default value (shadowMode == 0) makes every
// non-text UI surface and every NO_SHADOW text glyph hit the early-return
// branch below — bytewise equivalent to the pre-change shader. Only text
// rendering with active shadow geometry sets shadowMode > 0 before its
// draw range. shadowMode is deliberately the ONLY shadow uniform: per-pass
// constant, so captured vertex buffers (which replay texture binds but not
// uniforms) stay correct. Everything that varies per batch — atlas texel
// size, alpha channel layout — derives from the bound texture itself.
uniform int  shadowMode;           // 0 = passthrough, 1 = drop, 2 = soft

in vec2 vary_texcoord0;
in vec4 vertex_color;

#ifdef HAS_FONT_GPU
// Analytic (hb-gpu) font path, decided PER VERTEX from glyph_loc (rides gGL's
// batched stream): GLYPH_LOC_QUAD (0xFFFFFFFF) -> ordinary quad/atlas path below;
// COLOR bit (0x80000000) set -> COLR color glyph (hb_gpu_paint, premultiplied);
// otherwise -> monochrome coverage (hb_gpu_draw). vary_texcoord0 is the em-space
// renderCoord for glyphs. hb_gpu_draw/paint/ppem/stem_darken are spliced in ahead
// of main() by the runtime lib injection.
flat in uint vary_glyphLoc;
#endif

float sampleAtlasAlpha(vec2 uv)
{
    // Both atlas types route alpha through .a in the shader: the grayscale
    // font atlas is uploaded as GL_LUMINANCE_ALPHA, which LLImageGL converts
    // to GL_RG with TEXTURE_SWIZZLE (R, R, R, G) (see llimagegl.cpp's
    // setManualImage), so .a maps to the rasterized alpha byte. RGBA emoji
    // atlases natively store alpha in .a. Reading .g for grayscale would
    // pick up the post-swizzle R channel (255 in cleared regions), which
    // produced solid 83%-black boxes wherever the shader sampled outside a
    // glyph.
    return texture(diffuseMap, uv).a;
}

void main()
{
#ifdef HAS_FONT_GPU
    if (vary_glyphLoc != 0xFFFFFFFFu)   // GLYPH_LOC_QUAD
    {
        if ((vary_glyphLoc & 0x80000000u) != 0u)   // GLYPH_LOC_COLOR
        {
            // COLR(v1) color glyph: hb_gpu_paint returns PREMULTIPLIED RGBA
            // (coverage folded in). vertex_color.rgb is the foreground (used only
            // by foreground-referencing layers); .a is overall opacity. Un-premul
            // and output straight-alpha so this composites with the SAME
            // straight-alpha blend as mono glyphs and quads -- no separate blend
            // state to bracket or to capture for cached replay. (un-premul +
            // straight-alpha == premultiplied output + premultiplied blend.)
            float cov;
            vec4 premul = hb_gpu_paint(vary_texcoord0, vary_glyphLoc & 0x7FFFFFFFu,
                                       vec4(vertex_color.rgb, 1.0), cov);
            frag_color = vec4(premul.rgb / max(premul.a, 1e-5), premul.a * vertex_color.a);
            return;
        }
        // Monochrome analytic glyph coverage, composited into the text color's
        // alpha — same color * coverage semantics as the atlas grayscale path.
        float coverage = hb_gpu_draw(vary_texcoord0, vary_glyphLoc);
        float ppem = hb_gpu_ppem(vary_texcoord0, vary_glyphLoc);
        float brightness = dot(vertex_color.rgb, vec3(0.299, 0.587, 0.114));
        //coverage = hb_gpu_stem_darken(coverage, brightness, ppem);
        frag_color = vec4(vertex_color.rgb, vertex_color.a * coverage);
        return;
    }
#endif
    if (shadowMode == 0)
    {
        // Default path. Byte-identical to pre-change uiF.glsl.
        frag_color = vertex_color*texture(diffuseMap, vary_texcoord0.xy);
        return;
    }

    // Shadow path: emulate the multi-quad emission with a single dilated
    // quad. Each multi-quad tap with screen offset Δ translates the quad
    // and, at fragment S, samples the glyph at base_uv − Δ * uv_per_px.
    // The shader's equivalent sample offset is therefore the *negative* of
    // the multi-quad's screen offset (with atlasTexelSize ≈ uv_per_px for
    // 1:1 crisp glyph mapping). Symmetric-corner taps look identical after
    // mirroring, but the asymmetric (0,−2) tap MUST flip to (0,+2) to
    // preserve the soft shadow's drop direction.
    //
    // Combining the taps: standard alpha blending of N quads with per-pixel
    // alpha α_i = vertex_color.a * sample_alpha_i over a transparent
    // background gives total_alpha = 1 − Π(1 − α_i). Using max() instead
    // collapses to uniform vertex_color.a wherever any sample hits, which
    // produces a blocky outline-shaped shadow with no gradient.
    //
    // Atlas texel size comes from the bound texture, not a uniform: one
    // shadowed string can batch glyphs from differently-sized atlases (the
    // head face's sheets vs a fallback emoji face's), and a per-string
    // uniform sized off the head atlas put the fallback glyphs' taps at the
    // wrong distance. textureSize always reflects the atlas this draw
    // actually samples — including on captured-buffer replay, which rebinds
    // textures per batch.
    vec2 atlasTexelSize = 1.0 / vec2(textureSize(diffuseMap, 0));
    float vc_a = vertex_color.a;
    float p;
    if (shadowMode == 1)
    {
        // DROP_SHADOW: single tap. Multi-quad screen offset (1, −1)
        // → shader offset (−1, 1).
        p = 1.0 - vc_a * sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2(-1.0, 1.0));
    }
    else
    {
        // DROP_SHADOW_SOFT: 5 taps. Multi-quad offsets
        // (−1,−1)(1,−1)(1,1)(−1,1)(0,−2) → shader offsets negated.
        p  = (1.0 - vc_a * sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2( 1.0,  1.0)));
        p *= (1.0 - vc_a * sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2(-1.0,  1.0)));
        p *= (1.0 - vc_a * sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2(-1.0, -1.0)));
        p *= (1.0 - vc_a * sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2( 1.0, -1.0)));
        p *= (1.0 - vc_a * sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2( 0.0,  2.0)));
    }
    frag_color = vec4(vertex_color.rgb, 1.0 - p);
}
