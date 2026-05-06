/**
 * @file llfontcolrv1.cpp
 * @brief COLRv1 paint-tree rasterizer using HarfBuzz callbacks → plutovg.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llfontcolrv1.h"

#include "llerror.h"

#include <hb.h>
#include <hb-ot.h>
#include <hb-ft.h>
#include <plutovg/plutovg.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
    // PaintColrGlyph recursion is bounded by HB_MAX_NESTING_LEVEL inside
    // HarfBuzz (currently 64), which is the only depth check that actually
    // applies here — our callbacks don't recurse on their own.

    // Padding around the glyph bbox in surface pixels. Anti-aliased edges of
    // outline-clipped fills, supersampled gradients, and rotated transforms
    // can spread coverage 1–2 px past the geometric bbox; clipping that
    // spread to a tight surface produces visible 1-px fringes (especially
    // on flag emoji built from stacked clip rects). 2 px gives both AA and
    // post-transform paint commands enough slack without meaningful atlas
    // cost — the existing 4-px shadow border in LLFontBitmapCache already
    // subsumes this spread, so atlas footprint is unchanged.
    constexpr S32 GLYPH_PAD = 2;

    // Floor (×256, fixed-point) applied to the alpha channel when folding
    // BGRA premul → Gray. Keeps fully-saturated palette black visible as a
    // ~15% silhouette rather than collapsing to zero, while leaving the
    // luminance-dominant majority of the glyph unchanged. 0.15 is large
    // enough to register on light backgrounds and small enough not to wash
    // out the brightness gradation in lighter regions.
    constexpr U32 GRAY_ALPHA_FLOOR_256 = 38;  // ≈ 0.15 * 256

    // mStaging high-water cap: keep at most 4× the most recent glyph or
    // 256 KB, whichever is larger, so a single huge glyph doesn't strand
    // multiple MB of per-thread memory for the rest of the process.
    constexpr size_t STAGING_HIGH_WATER_BYTES = 256 * 1024;

    // Supersample factor below this ppem. Small glyphs have no inherent AA
    // budget — gradient transitions and clip edges are 1 px wide, so any
    // sub-pixel positioning issue surfaces as visible jaggies. 2× SS gives
    // them 4× the rasterization area to absorb spread without changing the
    // output (atlas) size. Above the threshold, glyphs already AA naturally
    // and SS is wasted work.
    constexpr unsigned SUPERSAMPLE_PPEM_THRESHOLD = 32u;

    struct Painter
    {
        plutovg_surface_t* surface = nullptr;
        plutovg_canvas_t*  canvas  = nullptr;
        S32                width   = 0;
        S32                height  = 0;
        // (xmin, ymin) of the glyph bbox in HarfBuzz paint coordinates (pixels
        // with ppem set). Used to compose the surface origin transform.
        F32                origin_x = 0.f;
        F32                origin_y_top = 0.f;  // y_bearing in HB (Y-up) coords
        // Foreground color used for COLRv1 foreground-slot fills. Unpremultiplied.
        hb_color_t         foreground = 0;
        // Whether any callback bailed out because of an unsupported feature
        // (image fill, group surface alloc fail, etc.). Caller treats true
        // as "abort and fall back to outline rasterization". Once set, every
        // callback below is a no-op — the latch ensures a failed push_group
        // doesn't leak subsequent paint commands into the parent surface or
        // throw plutovg's clip/transform stack out of sync.
        bool               aborted = false;
        // Tracks whether any non-foreground (palette / explicit RGBA) color
        // was emitted during the walk. Drives the foreground-only tint flag
        // on the resulting Result so the draw path can pick text_color over
        // emoji-white for glyphs whose paint tree references only foreground.
        bool               saw_palette_color = false;

        // Reusable scratch buffer for color-line stop slurps.
        std::vector<hb_color_stop_t>      stop_buf;
        std::vector<plutovg_gradient_stop_t> pv_stops;

        // PaintComposite group stack: each entry is the (surface, canvas) we
        // were drawing into before push_group redirected output to a fresh
        // sub-surface. pop_group blits the current sub-surface back onto the
        // popped parent under the requested compositing mode and frees the
        // sub-surface. Empty during ordinary (non-grouped) painting.
        struct GroupFrame
        {
            plutovg_surface_t* surface;
            plutovg_canvas_t*  canvas;
        };
        std::vector<GroupFrame> group_stack;
    };

    // Map an hb composite mode to a plutovg Porter-Duff operator if the mode
    // is one of the Porter-Duff set plutovg natively supports. Returns true
    // and sets *op for native-supported modes; returns false for the W3C
    // separable blend modes, HSL non-separable blend modes, and PLUS, all
    // of which run through the software blend path in pop_group_cb.
    inline bool try_to_pv_operator(hb_paint_composite_mode_t mode, plutovg_operator_t* op)
    {
        switch (mode)
        {
            case HB_PAINT_COMPOSITE_MODE_CLEAR:     *op = PLUTOVG_OPERATOR_CLEAR;     return true;
            case HB_PAINT_COMPOSITE_MODE_SRC:       *op = PLUTOVG_OPERATOR_SRC;       return true;
            case HB_PAINT_COMPOSITE_MODE_DEST:      *op = PLUTOVG_OPERATOR_DST;       return true;
            case HB_PAINT_COMPOSITE_MODE_SRC_OVER:  *op = PLUTOVG_OPERATOR_SRC_OVER;  return true;
            case HB_PAINT_COMPOSITE_MODE_DEST_OVER: *op = PLUTOVG_OPERATOR_DST_OVER;  return true;
            case HB_PAINT_COMPOSITE_MODE_SRC_IN:    *op = PLUTOVG_OPERATOR_SRC_IN;    return true;
            case HB_PAINT_COMPOSITE_MODE_DEST_IN:   *op = PLUTOVG_OPERATOR_DST_IN;    return true;
            case HB_PAINT_COMPOSITE_MODE_SRC_OUT:   *op = PLUTOVG_OPERATOR_SRC_OUT;   return true;
            case HB_PAINT_COMPOSITE_MODE_DEST_OUT:  *op = PLUTOVG_OPERATOR_DST_OUT;   return true;
            case HB_PAINT_COMPOSITE_MODE_SRC_ATOP:  *op = PLUTOVG_OPERATOR_SRC_ATOP;  return true;
            case HB_PAINT_COMPOSITE_MODE_DEST_ATOP: *op = PLUTOVG_OPERATOR_DST_ATOP;  return true;
            case HB_PAINT_COMPOSITE_MODE_XOR:       *op = PLUTOVG_OPERATOR_XOR;       return true;
            default:
                return false;
        }
    }

    // ---- W3C separable blend functions on un-premultiplied [0..1] components ----
    // Per W3C Compositing and Blending Level 2, §10.2 (separable blend modes).
    // Each function returns the blended channel B(Cb, Cs).
    inline F32 blend_multiply   (F32 cb, F32 cs) { return cb * cs; }
    inline F32 blend_screen     (F32 cb, F32 cs) { return cb + cs - cb * cs; }
    inline F32 blend_darken     (F32 cb, F32 cs) { return cb < cs ? cb : cs; }
    inline F32 blend_lighten    (F32 cb, F32 cs) { return cb > cs ? cb : cs; }
    inline F32 blend_difference (F32 cb, F32 cs) { return cb > cs ? cb - cs : cs - cb; }
    inline F32 blend_exclusion  (F32 cb, F32 cs) { return cb + cs - 2.f * cb * cs; }
    inline F32 blend_hard_light (F32 cb, F32 cs)
    {
        return (cs <= 0.5f) ? 2.f * cb * cs
                            : 1.f - 2.f * (1.f - cb) * (1.f - cs);
    }
    inline F32 blend_overlay    (F32 cb, F32 cs) { return blend_hard_light(cs, cb); }
    inline F32 blend_color_dodge(F32 cb, F32 cs)
    {
        if (cb <= 0.f) return 0.f;
        if (cs >= 1.f) return 1.f;
        const F32 d = cb / (1.f - cs);
        return d < 1.f ? d : 1.f;
    }
    inline F32 blend_color_burn (F32 cb, F32 cs)
    {
        if (cb >= 1.f) return 1.f;
        if (cs <= 0.f) return 0.f;
        const F32 d = (1.f - cb) / cs;
        return 1.f - (d < 1.f ? d : 1.f);
    }
    inline F32 blend_soft_light (F32 cb, F32 cs)
    {
        // Per W3C §10.2:
        // if cs <= 0.5:               cb - (1 - 2*cs) * cb * (1 - cb)
        // if cs >  0.5 and cb <= 0.25: cb + (2*cs - 1) * (((16*cb - 12) * cb + 4) * cb - cb)
        // if cs >  0.5 and cb >  0.25: cb + (2*cs - 1) * (sqrt(cb) - cb)
        if (cs <= 0.5f)
            return cb - (1.f - 2.f * cs) * cb * (1.f - cb);
        const F32 d = (cb <= 0.25f)
            ? (((16.f * cb - 12.f) * cb + 4.f) * cb - cb)
            : (sqrtf(cb) - cb);
        return cb + (2.f * cs - 1.f) * d;
    }

    // ---- W3C non-separable HSL helpers (operate on RGB triple, [0..1]) ----
    // Per W3C §10.3. lum() weights are baked into the spec; sat() is the
    // simple max-min range. clip_color reins in any out-of-gamut channel
    // back into [0,1] while preserving luminosity.
    inline F32 lum_w3c(F32 r, F32 g, F32 b) { return 0.3f * r + 0.59f * g + 0.11f * b; }
    inline F32 sat_w3c(F32 r, F32 g, F32 b)
    {
        const F32 mx = (r > g ? (r > b ? r : b) : (g > b ? g : b));
        const F32 mn = (r < g ? (r < b ? r : b) : (g < b ? g : b));
        return mx - mn;
    }
    inline void clip_color(F32& r, F32& g, F32& b)
    {
        const F32 l  = lum_w3c(r, g, b);
        const F32 mn = (r < g ? (r < b ? r : b) : (g < b ? g : b));
        const F32 mx = (r > g ? (r > b ? r : b) : (g > b ? g : b));
        if (mn < 0.f && (l - mn) > 0.f)
        {
            const F32 k = l / (l - mn);
            r = l + (r - l) * k;
            g = l + (g - l) * k;
            b = l + (b - l) * k;
        }
        if (mx > 1.f && (mx - l) > 0.f)
        {
            const F32 k = (1.f - l) / (mx - l);
            r = l + (r - l) * k;
            g = l + (g - l) * k;
            b = l + (b - l) * k;
        }
    }
    inline void set_lum(F32& r, F32& g, F32& b, F32 target_l)
    {
        const F32 d = target_l - lum_w3c(r, g, b);
        r += d; g += d; b += d;
        clip_color(r, g, b);
    }
    inline void set_sat(F32& r, F32& g, F32& b, F32 target_s)
    {
        // Per W3C §10.3: rank channels by value, scale mid/max so range
        // equals target_s, set min to zero. The pointer-juggling sorts
        // (mn, md, mx) by current value without copying.
        F32* mn = &r;
        F32* md = &g;
        F32* mx = &b;
        if (*mn > *md) std::swap(mn, md);
        if (*mn > *mx) std::swap(mn, mx);
        if (*md > *mx) std::swap(md, mx);
        if (*mx > *mn)
        {
            *md = (*md - *mn) * target_s / (*mx - *mn);
            *mx = target_s;
        }
        else
        {
            *md = 0.f;
            *mx = 0.f;
        }
        *mn = 0.f;
    }

    // ---- The compose_pixel core ----
    // Applies a W3C / PLUS composite mode to a single (src, dst) pair of
    // BGRA-premultiplied bytes and writes the result. Used by both the
    // pop_group_cb software blend pass and the public testSoftwareBlendPixel
    // entry point.
    inline void compose_pixel_w3c(hb_paint_composite_mode_t mode,
                                  const U8 src[4], const U8 dst[4], U8 out[4])
    {
        constexpr F32 INV_255 = 1.f / 255.f;
        const F32 sa = src[3] * INV_255;
        const F32 da = dst[3] * INV_255;

        // PLUS is a compositing op, not a blend mode: out = src + dst per
        // channel (premultiplied), clamped. Handle separately because it
        // doesn't go through the un-premul → blend → premul path.
        if (mode == HB_PAINT_COMPOSITE_MODE_PLUS)
        {
            const F32 ob = (F32)src[0] + (F32)dst[0];
            const F32 og = (F32)src[1] + (F32)dst[1];
            const F32 or_ = (F32)src[2] + (F32)dst[2];
            const F32 oa = (F32)src[3] + (F32)dst[3];
            out[0] = (U8)(ob > 255.f ? 255.f : ob);
            out[1] = (U8)(og > 255.f ? 255.f : og);
            out[2] = (U8)(or_ > 255.f ? 255.f : or_);
            out[3] = (U8)(oa > 255.f ? 255.f : oa);
            return;
        }

        // sa == 0 means the source contributes nothing to the result. The
        // W3C composite reduces to "destination unchanged"; short-circuit
        // both the divide-by-zero un-premul and the per-channel blend math.
        if (sa <= 0.f)
        {
            out[0] = dst[0];
            out[1] = dst[1];
            out[2] = dst[2];
            out[3] = dst[3];
            return;
        }

        // Un-premultiply both sides for the blend computation. Source has
        // sa > 0 here; destination may have da == 0, in which case the
        // un-premul value is undefined but the blend formula's da term is
        // zero so the value drops out — safe to leave as 0.
        const F32 inv_sa = 1.f / sa;
        const F32 sb = src[0] * INV_255 * inv_sa;
        const F32 sg = src[1] * INV_255 * inv_sa;
        const F32 sr = src[2] * INV_255 * inv_sa;
        F32 db = 0.f, dg = 0.f, dr = 0.f;
        if (da > 0.f)
        {
            const F32 inv_da = 1.f / da;
            db = dst[0] * INV_255 * inv_da;
            dg = dst[1] * INV_255 * inv_da;
            dr = dst[2] * INV_255 * inv_da;
        }

        // Compute the blended source color B(Cb, Cs) per the requested mode.
        // Separable modes apply the same scalar function per channel; HSL
        // modes operate on the RGB triple jointly.
        F32 br = sr, bg = sg, bb = sb;
        switch (mode)
        {
            case HB_PAINT_COMPOSITE_MODE_MULTIPLY:
                br = blend_multiply  (dr, sr); bg = blend_multiply  (dg, sg); bb = blend_multiply  (db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_SCREEN:
                br = blend_screen    (dr, sr); bg = blend_screen    (dg, sg); bb = blend_screen    (db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_OVERLAY:
                br = blend_overlay   (dr, sr); bg = blend_overlay   (dg, sg); bb = blend_overlay   (db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_DARKEN:
                br = blend_darken    (dr, sr); bg = blend_darken    (dg, sg); bb = blend_darken    (db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_LIGHTEN:
                br = blend_lighten   (dr, sr); bg = blend_lighten   (dg, sg); bb = blend_lighten   (db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_COLOR_DODGE:
                br = blend_color_dodge(dr, sr); bg = blend_color_dodge(dg, sg); bb = blend_color_dodge(db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_COLOR_BURN:
                br = blend_color_burn(dr, sr); bg = blend_color_burn(dg, sg); bb = blend_color_burn(db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_HARD_LIGHT:
                br = blend_hard_light(dr, sr); bg = blend_hard_light(dg, sg); bb = blend_hard_light(db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_SOFT_LIGHT:
                br = blend_soft_light(dr, sr); bg = blend_soft_light(dg, sg); bb = blend_soft_light(db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_DIFFERENCE:
                br = blend_difference(dr, sr); bg = blend_difference(dg, sg); bb = blend_difference(db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_EXCLUSION:
                br = blend_exclusion (dr, sr); bg = blend_exclusion (dg, sg); bb = blend_exclusion (db, sb); break;
            case HB_PAINT_COMPOSITE_MODE_HSL_HUE:
            {
                // Hue of source, saturation of source's hue projected onto
                // the destination's saturation, luminosity of destination.
                F32 r = sr, g = sg, b = sb;
                set_sat(r, g, b, sat_w3c(dr, dg, db));
                set_lum(r, g, b, lum_w3c(dr, dg, db));
                br = r; bg = g; bb = b;
                break;
            }
            case HB_PAINT_COMPOSITE_MODE_HSL_SATURATION:
            {
                // Saturation of source, hue and luminosity of destination.
                F32 r = dr, g = dg, b = db;
                set_sat(r, g, b, sat_w3c(sr, sg, sb));
                set_lum(r, g, b, lum_w3c(dr, dg, db));
                br = r; bg = g; bb = b;
                break;
            }
            case HB_PAINT_COMPOSITE_MODE_HSL_COLOR:
            {
                // Hue and saturation of source, luminosity of destination.
                F32 r = sr, g = sg, b = sb;
                set_lum(r, g, b, lum_w3c(dr, dg, db));
                br = r; bg = g; bb = b;
                break;
            }
            case HB_PAINT_COMPOSITE_MODE_HSL_LUMINOSITY:
            {
                // Luminosity of source, hue and saturation of destination.
                F32 r = dr, g = dg, b = db;
                set_lum(r, g, b, lum_w3c(sr, sg, sb));
                br = r; bg = g; bb = b;
                break;
            }
            default:
                // Unknown / unsupported mode that snuck past try_to_pv_operator.
                // Behavior matches the old SRC_OVER fallback so we never
                // produce worse output than before this code existed.
                br = sr; bg = sg; bb = sb;
                break;
        }

        // Mix the blended source back with the un-blended source via
        // destination alpha (W3C §10.1):
        //   Cs' = (1 - αb) * Cs + αb * B(Cb, Cs)
        // When αb == 0 this is just Cs; when αb == 1 it's B().
        const F32 inv_da = 1.f - da;
        const F32 mr = inv_da * sr + da * br;
        const F32 mg = inv_da * sg + da * bg;
        const F32 mb = inv_da * sb + da * bb;

        // Source-over composite of (mixed_color_unpremul, sa) onto destination
        // in premultiplied form. (1 - sa) * dst.rgb_premul = (1 - sa) * dst[k].
        const F32 inv_sa_term = 1.f - sa;
        const F32 oa = sa + da * inv_sa_term;
        const F32 ob_p = sa * mb * 255.f + inv_sa_term * (F32)dst[0];
        const F32 og_p = sa * mg * 255.f + inv_sa_term * (F32)dst[1];
        const F32 or_p = sa * mr * 255.f + inv_sa_term * (F32)dst[2];

        auto clamp8 = [](F32 v) -> U8 {
            if (v <= 0.f)   return (U8)0;
            if (v >= 255.f) return (U8)255;
            return (U8)(v + 0.5f);
        };
        out[0] = clamp8(ob_p);
        out[1] = clamp8(og_p);
        out[2] = clamp8(or_p);
        out[3] = clamp8(oa * 255.f);
    }

    // ---- Utility: hb_color_t → plutovg_color_t (unpremultiplied float) ----
    inline void hb_to_pv_color(hb_color_t in, hb_color_t foreground,
                               bool is_foreground, plutovg_color_t* out)
    {
        const hb_color_t c = is_foreground ? foreground : in;
        plutovg_color_init_rgba8(out,
                                 hb_color_get_red(c),
                                 hb_color_get_green(c),
                                 hb_color_get_blue(c),
                                 hb_color_get_alpha(c));
    }

    inline plutovg_spread_method_t to_pv_spread(hb_paint_extend_t extend)
    {
        switch (extend)
        {
            case HB_PAINT_EXTEND_REPEAT:  return PLUTOVG_SPREAD_METHOD_REPEAT;
            case HB_PAINT_EXTEND_REFLECT: return PLUTOVG_SPREAD_METHOD_REFLECT;
            case HB_PAINT_EXTEND_PAD:
            default:                      return PLUTOVG_SPREAD_METHOD_PAD;
        }
    }

    // ---------- hb_paint_funcs_t callbacks ----------

    void push_transform_cb(hb_paint_funcs_t*, void* data,
                           float xx, float yx, float xy, float yy,
                           float dx, float dy, void*)
    {
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        plutovg_canvas_save(p->canvas);
        plutovg_matrix_t m;
        plutovg_matrix_init(&m, xx, yx, xy, yy, dx, dy);
        plutovg_canvas_transform(p->canvas, &m);
    }

    void pop_transform_cb(hb_paint_funcs_t*, void* data, void*)
    {
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        plutovg_canvas_restore(p->canvas);
    }

    // Build a plutovg path from a glyph's outline by driving an hb_draw_funcs
    // bridge. Returns true if the outline emitted at least one move.
    struct DrawCtx
    {
        plutovg_canvas_t* canvas;
        bool any = false;
    };
    void draw_move_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*,
                      float x, float y, void*)
    {
        auto* d = static_cast<DrawCtx*>(data);
        plutovg_canvas_move_to(d->canvas, x, y);
        d->any = true;
    }
    void draw_line_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*,
                      float x, float y, void*)
    {
        plutovg_canvas_line_to(static_cast<DrawCtx*>(data)->canvas, x, y);
    }
    void draw_quad_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*,
                      float c1x, float c1y, float x, float y, void*)
    {
        plutovg_canvas_quad_to(static_cast<DrawCtx*>(data)->canvas, c1x, c1y, x, y);
    }
    void draw_cubic_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*,
                       float c1x, float c1y, float c2x, float c2y,
                       float x, float y, void*)
    {
        plutovg_canvas_cubic_to(static_cast<DrawCtx*>(data)->canvas, c1x, c1y, c2x, c2y, x, y);
    }
    void draw_close_path(hb_draw_funcs_t*, void* data, hb_draw_state_t*, void*)
    {
        plutovg_canvas_close_path(static_cast<DrawCtx*>(data)->canvas);
    }

    hb_draw_funcs_t* get_outline_draw_funcs()
    {
        // One process-wide instance — hb_draw_funcs_t is immutable after
        // setup, and the callbacks key on user_data passed at draw time.
        static hb_draw_funcs_t* funcs = []
        {
            hb_draw_funcs_t* f = hb_draw_funcs_create();
            hb_draw_funcs_set_move_to_func(f, draw_move_to, nullptr, nullptr);
            hb_draw_funcs_set_line_to_func(f, draw_line_to, nullptr, nullptr);
            hb_draw_funcs_set_quadratic_to_func(f, draw_quad_to, nullptr, nullptr);
            hb_draw_funcs_set_cubic_to_func(f, draw_cubic_to, nullptr, nullptr);
            hb_draw_funcs_set_close_path_func(f, draw_close_path, nullptr, nullptr);
            hb_draw_funcs_make_immutable(f);
            return f;
        }();
        return funcs;
    }

    void push_clip_glyph_cb(hb_paint_funcs_t*, void* data,
                            hb_codepoint_t glyph, hb_font_t* font, void*)
    {
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        plutovg_canvas_save(p->canvas);
        plutovg_canvas_new_path(p->canvas);

        DrawCtx ctx{ p->canvas, false };
        hb_font_draw_glyph(font, glyph, get_outline_draw_funcs(), &ctx);
        if (ctx.any)
        {
            plutovg_canvas_clip(p->canvas);
        }
        // If the glyph is empty (no path), we still pushed a save() — pop_clip
        // will restore it. Subsequent fills emit nothing because the clip is
        // empty, which is the spec-correct behavior.
    }

    void push_clip_rectangle_cb(hb_paint_funcs_t*, void* data,
                                float xmin, float ymin, float xmax, float ymax, void*)
    {
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        plutovg_canvas_save(p->canvas);
        plutovg_canvas_new_path(p->canvas);
        plutovg_canvas_move_to(p->canvas, xmin, ymin);
        plutovg_canvas_line_to(p->canvas, xmax, ymin);
        plutovg_canvas_line_to(p->canvas, xmax, ymax);
        plutovg_canvas_line_to(p->canvas, xmin, ymax);
        plutovg_canvas_close_path(p->canvas);
        plutovg_canvas_clip(p->canvas);
    }

    void pop_clip_cb(hb_paint_funcs_t*, void* data, void*)
    {
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        plutovg_canvas_restore(p->canvas);
    }

    void color_cb(hb_paint_funcs_t*, void* data,
                  hb_bool_t is_foreground, hb_color_t color, void*)
    {
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        if (!is_foreground)
            p->saw_palette_color = true;
        plutovg_color_t pc;
        hb_to_pv_color(color, p->foreground, is_foreground, &pc);
        plutovg_canvas_set_color(p->canvas, &pc);
        // Fills the entire active clip with the current paint. The clip was
        // built up under prior push_clip_* calls and lives in device space,
        // so this respects whatever shape we're masked to.
        plutovg_canvas_paint(p->canvas);
    }

    // Slurp every stop from an hb_color_line into pv_stops. Filters out NaN
    // offsets which some malformed fonts produce.
    void slurp_color_stops(Painter& p, hb_color_line_t* color_line)
    {
        p.stop_buf.clear();
        p.pv_stops.clear();

        unsigned start = 0;
        for (;;)
        {
            unsigned chunk = 16;
            hb_color_stop_t buf[16];
            hb_color_line_get_color_stops(color_line, start, &chunk, buf);
            if (chunk == 0)
                break;
            for (unsigned i = 0; i < chunk; ++i)
                p.stop_buf.push_back(buf[i]);
            start += chunk;
            if (chunk < 16)
                break;
        }

        p.pv_stops.reserve(p.stop_buf.size());
        for (const auto& s : p.stop_buf)
        {
            if (!(s.offset >= 0.f) && !(s.offset <= 1.f))
                continue; // NaN guard; either of those is true for any real number
            if (!s.is_foreground)
                p.saw_palette_color = true;
            plutovg_gradient_stop_t out;
            out.offset = s.offset;
            hb_to_pv_color(s.color, p.foreground, s.is_foreground, &out.color);
            p.pv_stops.push_back(out);
        }
    }

    void linear_gradient_cb(hb_paint_funcs_t*, void* data, hb_color_line_t* color_line,
                            float x0, float y0, float x1, float y1, float x2, float y2,
                            void*)
    {
        // COLRv1's PaintLinearGradient supplies a third point p2; the gradient
        // line is the line through p0 perpendicular to (p2 - p0), and the
        // gradient parameter t at a point P is the signed projection of (P - p0)
        // onto p1' = p0 + proj(p1-p0, perp_of(p2-p0)). plutovg's linear gradient
        // takes (start, end) along the axis directly, so we project p1 onto
        // that axis to get an effective endpoint that produces the same field.
        // When p2 is collinear with p0..p1 (the common case), perp_of(p2-p0)
        // points along p0..p1 and the projection is the identity — falls back
        // to the current p1 endpoint without a special case.
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        slurp_color_stops(*p, color_line);
        if (p->pv_stops.empty())
            return;

        // perp = 90° rotation of (p2 - p0). Either rotation direction works
        // since we end up dotting p1-p0 against it; chose CCW (-vy, vx).
        const float vx = x2 - x0;
        const float vy = y2 - y0;
        const float perp_x = -vy;
        const float perp_y =  vx;
        const float perp_len_sq = perp_x * perp_x + perp_y * perp_y;
        float p1x = x1, p1y = y1;
        if (perp_len_sq > 1e-6f)
        {
            const float dx = x1 - x0;
            const float dy = y1 - y0;
            const float t  = (dx * perp_x + dy * perp_y) / perp_len_sq;
            p1x = x0 + t * perp_x;
            p1y = y0 + t * perp_y;
        }

        const plutovg_spread_method_t spread = to_pv_spread(hb_color_line_get_extend(color_line));
        plutovg_canvas_set_linear_gradient(p->canvas, x0, y0, p1x, p1y,
                                           spread, p->pv_stops.data(),
                                           static_cast<int>(p->pv_stops.size()), nullptr);
        plutovg_canvas_paint(p->canvas);
    }

    void radial_gradient_cb(hb_paint_funcs_t*, void* data, hb_color_line_t* color_line,
                            float x0, float y0, float r0,
                            float x1, float y1, float r1, void*)
    {
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        slurp_color_stops(*p, color_line);
        if (p->pv_stops.empty())
            return;

        const plutovg_spread_method_t spread = to_pv_spread(hb_color_line_get_extend(color_line));
        // COLRv1's two-circle gradient maps directly onto plutovg's two-point
        // conic radial gradient. HB hands us p0=(x0,y0,r0) the start (focal)
        // circle and p1=(x1,y1,r1) the end circle. plutovg's signature is
        // (cx,cy,cr, fx,fy,fr) where (cx,cy,cr) is the end circle and
        // (fx,fy,fr) is the focal — so we pass p1 first, p0 second.
        plutovg_canvas_set_radial_gradient(p->canvas,
                                           x1, y1, r1,
                                           x0, y0, r0,
                                           spread, p->pv_stops.data(),
                                           static_cast<int>(p->pv_stops.size()), nullptr);
        plutovg_canvas_paint(p->canvas);
    }

    // Linearly interpolate the color at parameter `t` (already wrapped/clamped
    // into [0, 1] by the spread mode) across the painter's current pv_stops.
    // Stops are assumed in offset-ascending order (the order HB produces).
    void interp_stops(const std::vector<plutovg_gradient_stop_t>& stops,
                      F32 t, plutovg_color_t* out)
    {
        if (stops.empty()) { out->r = out->g = out->b = out->a = 0.f; return; }
        if (t <= stops.front().offset) { *out = stops.front().color; return; }
        if (t >= stops.back().offset)  { *out = stops.back().color;  return; }
        for (size_t i = 1; i < stops.size(); ++i)
        {
            const auto& a = stops[i - 1];
            const auto& b = stops[i];
            if (t <= b.offset)
            {
                const F32 span = b.offset - a.offset;
                const F32 u = (span > 0.f) ? (t - a.offset) / span : 0.f;
                out->r = a.color.r + (b.color.r - a.color.r) * u;
                out->g = a.color.g + (b.color.g - a.color.g) * u;
                out->b = a.color.b + (b.color.b - a.color.b) * u;
                out->a = a.color.a + (b.color.a - a.color.a) * u;
                return;
            }
        }
        *out = stops.back().color;
    }

    // Map a parameter t through the gradient extend mode into [0, 1]. The
    // semantics match COLRv1 / SVG: PAD clamps; REPEAT wraps; REFLECT
    // mirrors at every odd integer.
    inline F32 apply_extend(F32 t, hb_paint_extend_t extend)
    {
        switch (extend)
        {
            case HB_PAINT_EXTEND_REPEAT:
            {
                F32 frac = t - floorf(t);
                if (frac < 0.f) frac += 1.f;
                return frac;
            }
            case HB_PAINT_EXTEND_REFLECT:
            {
                F32 wrapped = t - 2.f * floorf(t * 0.5f);
                return (wrapped > 1.f) ? (2.f - wrapped) : wrapped;
            }
            case HB_PAINT_EXTEND_PAD:
            default:
                return (t < 0.f) ? 0.f : (t > 1.f) ? 1.f : t;
        }
    }

    void sweep_gradient_cb(hb_paint_funcs_t*, void* data,
                           hb_color_line_t* color_line,
                           float x0, float y0,
                           float start_angle, float end_angle, void*)
    {
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        slurp_color_stops(*p, color_line);
        if (p->pv_stops.empty())
            return;

        // Allocate a per-call lookup surface sized to the painter's main
        // surface in device pixels. The fill loop walks every device pixel,
        // maps it back to HB user-space (where the gradient is defined),
        // computes the angle from the gradient center, and writes the
        // sampled color. Then plutovg_canvas_paint with the LUT as the
        // active texture stamps it into the active clip.
        plutovg_surface_t* lut = plutovg_surface_create(p->width, p->height);
        if (!lut)
        {
            p->aborted = true;
            return;
        }

        // Compute the inverse of the current CTM so we can map device pixels
        // back into HB user-space — that's the space (x0, y0) and the start/
        // end angles are defined in. The CTM already includes the surface
        // origin translate and the 1/64 paint-coord scale.
        plutovg_matrix_t ctm;
        plutovg_canvas_get_matrix(p->canvas, &ctm);
        plutovg_matrix_t inv;
        if (!plutovg_matrix_invert(&ctm, &inv))
        {
            // Singular CTM (zero-determinant scale). Shouldn't happen with
            // our setup, but fall back gracefully if it does.
            plutovg_surface_destroy(lut);
            p->aborted = true;
            return;
        }

        const hb_paint_extend_t extend = hb_color_line_get_extend(color_line);
        const F32 angle_span = end_angle - start_angle;
        if (angle_span == 0.f)
        {
            // Zero-width sweep — degenerate; the COLRv1 spec says fall back
            // to a solid fill with the first stop's color.
            plutovg_canvas_set_color(p->canvas, &p->pv_stops.front().color);
            plutovg_canvas_paint(p->canvas);
            plutovg_surface_destroy(lut);
            return;
        }
        const F32 inv_span = 1.f / angle_span;

        unsigned char* lut_data = plutovg_surface_get_data(lut);
        const int lut_stride = plutovg_surface_get_stride(lut);
        if (!lut_data || lut_stride < p->width * 4)
        {
            plutovg_surface_destroy(lut);
            p->aborted = true;
            return;
        }
        // 4-rook supersampling pattern. Sample offsets within the pixel are
        // (1/8, 3/8), (3/8, 7/8), (5/8, 1/8), (7/8, 5/8) — distributes
        // along both axes, no two samples share an x or y, gives 4× AA on
        // the angular-jump boundaries that single-sample sweep produces.
        // 4× the sweep cost in this loop only; bounded by surf_w*surf_h
        // (≤ 1024×1024) and runs once per glyph, atlas-cached afterward.
        constexpr int  SS_TAPS = 4;
        constexpr float SS_OFFSETS[SS_TAPS][2] = {
            {0.125f, 0.375f}, {0.375f, 0.875f},
            {0.625f, 0.125f}, {0.875f, 0.625f},
        };
        for (S32 py = 0; py < p->height; ++py)
        {
            unsigned char* row = lut_data + py * lut_stride;
            for (S32 px = 0; px < p->width; ++px)
            {
                F32 r_acc = 0.f, g_acc = 0.f, b_acc = 0.f, a_acc = 0.f;
                for (int t_i = 0; t_i < SS_TAPS; ++t_i)
                {
                    float ux, uy;
                    plutovg_matrix_map(&inv,
                        (F32)px + SS_OFFSETS[t_i][0],
                        (F32)py + SS_OFFSETS[t_i][1],
                        &ux, &uy);
                    const F32 dx = ux - x0;
                    const F32 dy = uy - y0;
                    F32 angle = atan2f(dy, dx);
                    F32 t = (angle - start_angle) * inv_span;
                    t = apply_extend(t, extend);
                    plutovg_color_t c;
                    interp_stops(p->pv_stops, t, &c);
                    r_acc += c.r;
                    g_acc += c.g;
                    b_acc += c.b;
                    a_acc += c.a;
                }
                constexpr F32 INV_TAPS = 1.f / (F32)SS_TAPS;
                const F32 r = r_acc * INV_TAPS;
                const F32 g = g_acc * INV_TAPS;
                const F32 b = b_acc * INV_TAPS;
                const F32 a = a_acc * INV_TAPS;
                // Write BGRA premultiplied (plutovg's native surface format).
                // Average colors first, then premultiply — the alternative
                // (premultiply each tap, then average) gives the same result
                // because premultiplication and linear averaging commute, but
                // averaging once at the end keeps rounding error to a single
                // step per channel.
                const F32 a8 = a * 255.f;
                const F32 r8 = r * a8;
                const F32 g8 = g * a8;
                const F32 b8 = b * a8;
                row[px * 4 + 0] = (unsigned char)(b8 + 0.5f);
                row[px * 4 + 1] = (unsigned char)(g8 + 0.5f);
                row[px * 4 + 2] = (unsigned char)(r8 + 0.5f);
                row[px * 4 + 3] = (unsigned char)(a8 + 0.5f);
            }
        }

        // Stamp the LUT into the active clip. The plain-texture paint type
        // samples once (no tiling) at user coords; reset the canvas matrix
        // to identity for this paint so texture pixel (i, j) lands at
        // device pixel (i, j). Save/restore preserves the outer CTM.
        plutovg_canvas_save(p->canvas);
        plutovg_matrix_t identity;
        plutovg_matrix_init_identity(&identity);
        plutovg_canvas_set_matrix(p->canvas, &identity);
        plutovg_canvas_set_texture(p->canvas, lut, PLUTOVG_TEXTURE_TYPE_PLAIN,
                                   1.0f, &identity);
        plutovg_canvas_paint(p->canvas);
        plutovg_canvas_restore(p->canvas);

        plutovg_surface_destroy(lut);

        // Sweep colors are gradient stops; track palette-color use the same
        // way slurp_color_stops does for linear/radial. (slurp already ran
        // above, so the flag is set if any stop was non-foreground.)
    }

    hb_bool_t image_cb(hb_paint_funcs_t*, void* data,
                       hb_blob_t* /*image*/, unsigned /*width*/, unsigned /*height*/,
                       hb_tag_t /*format*/, float /*slant*/,
                       hb_glyph_extents_t* /*extents*/, void*)
    {
        // sbix/CBDT image blobs aren't on the COLRv1 path we're targeting.
        // If a font ships them anyway, mark aborted; FreeType's existing
        // FT_LOAD_COLOR path (already invoked for the base color render
        // attempt earlier) handles those formats natively.
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return false;
        p->aborted = true;
        return false;
    }

    void push_group_cb(hb_paint_funcs_t*, void* data, void*)
    {
        // Allocate a transparent sub-surface the same size as the main one
        // and redirect drawing to it. The new canvas inherits the current
        // CTM so paint commands during the group land at the same device
        // pixels they would on the parent. The clip is intentionally NOT
        // copied — the COLRv1 spec composites the group through the parent
        // clip at pop_group time, not during the group itself.
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        plutovg_surface_t* sub_surface = plutovg_surface_create(p->width, p->height);
        if (!sub_surface)
        {
            p->aborted = true;
            return;
        }
        plutovg_color_t transparent = { 0.f, 0.f, 0.f, 0.f };
        plutovg_surface_clear(sub_surface, &transparent);
        plutovg_canvas_t* sub_canvas = plutovg_canvas_create(sub_surface);
        if (!sub_canvas)
        {
            plutovg_surface_destroy(sub_surface);
            p->aborted = true;
            return;
        }
        // Copy the parent CTM so subsequent transforms / paths produce the
        // same device coords on the sub-canvas as they would on the parent.
        plutovg_matrix_t parent_ctm;
        plutovg_canvas_get_matrix(p->canvas, &parent_ctm);
        plutovg_canvas_set_matrix(sub_canvas, &parent_ctm);

        p->group_stack.push_back({ p->surface, p->canvas });
        p->surface = sub_surface;
        p->canvas  = sub_canvas;
    }

    void pop_group_cb(hb_paint_funcs_t*, void* data,
                      hb_paint_composite_mode_t mode, void*)
    {
        Painter* p = static_cast<Painter*>(data);
        if (p->aborted) return;
        if (p->group_stack.empty())
        {
            // pop without matching push — malformed paint tree. HB shouldn't
            // hand us this, but bail rather than crash if it does.
            p->aborted = true;
            return;
        }

        // Restore the parent canvas/surface; the current sub gets blitted
        // back as a texture under the requested composite mode.
        plutovg_surface_t* sub_surface = p->surface;
        plutovg_canvas_t*  sub_canvas  = p->canvas;
        const Painter::GroupFrame parent = p->group_stack.back();
        p->group_stack.pop_back();
        p->surface = parent.surface;
        p->canvas  = parent.canvas;

        plutovg_operator_t op = PLUTOVG_OPERATOR_SRC_OVER;
        const bool hardware = try_to_pv_operator(mode, &op);

        if (hardware)
        {
            // Native plutovg operator (Porter-Duff). Save parent state, draw
            // the sub-surface back onto it. Identity CTM so texture pixel
            // (i, j) lines up with parent device pixel (i, j) — the sub was
            // rendered using the parent CTM, so its device pixels already
            // match positions. The parent's active clip naturally bounds
            // the blit.
            plutovg_canvas_save(p->canvas);
            plutovg_matrix_t identity;
            plutovg_matrix_init_identity(&identity);
            plutovg_canvas_set_matrix(p->canvas, &identity);
            plutovg_canvas_set_operator(p->canvas, op);
            plutovg_canvas_set_texture(p->canvas, sub_surface,
                                       PLUTOVG_TEXTURE_TYPE_PLAIN, 1.f, &identity);
            plutovg_canvas_paint(p->canvas);
            plutovg_canvas_restore(p->canvas);
        }
        else
        {
            // W3C blend mode (multiply/screen/overlay/etc.), HSL non-separable
            // mode, or PLUS — plutovg can't compose these natively. Allocate
            // a temp surface, run the per-pixel blend over (parent, sub) →
            // temp using the same compose_pixel_w3c path the test entry
            // point exercises, then SRC-paint temp back onto the parent so
            // the parent's active clip masks where the result lands. Pixels
            // outside the clip stay untouched; pixels inside the clip but
            // where sub.alpha == 0 round-trip through the formula and write
            // back the unchanged parent bytes.
            plutovg_surface_t* temp = plutovg_surface_create(p->width, p->height);
            if (!temp)
            {
                // Out-of-memory on the temp allocation — fall back to the
                // pre-W3C SRC_OVER behavior so the glyph still produces
                // *something* rather than aborting the paint.
                LL_WARNS_ONCE("Font")
                    << "COLRv1 PaintComposite temp surface alloc failed for mode "
                    << (S32)mode << "; using SRC_OVER" << LL_ENDL;
                plutovg_canvas_save(p->canvas);
                plutovg_matrix_t identity;
                plutovg_matrix_init_identity(&identity);
                plutovg_canvas_set_matrix(p->canvas, &identity);
                plutovg_canvas_set_operator(p->canvas, PLUTOVG_OPERATOR_SRC_OVER);
                plutovg_canvas_set_texture(p->canvas, sub_surface,
                                           PLUTOVG_TEXTURE_TYPE_PLAIN, 1.f, &identity);
                plutovg_canvas_paint(p->canvas);
                plutovg_canvas_restore(p->canvas);
            }
            else
            {
                const int sub_stride = plutovg_surface_get_stride(sub_surface);
                const int parent_stride = plutovg_surface_get_stride(p->surface);
                const int temp_stride = plutovg_surface_get_stride(temp);
                const unsigned char* sub_data    = plutovg_surface_get_data(sub_surface);
                const unsigned char* parent_data = plutovg_surface_get_data(p->surface);
                unsigned char* temp_data         = plutovg_surface_get_data(temp);
                if (!sub_data || !parent_data || !temp_data ||
                    sub_stride < p->width * 4 ||
                    parent_stride < p->width * 4 ||
                    temp_stride < p->width * 4)
                {
                    plutovg_surface_destroy(temp);
                    p->aborted = true;
                    plutovg_canvas_destroy(sub_canvas);
                    plutovg_surface_destroy(sub_surface);
                    return;
                }

                for (S32 y = 0; y < p->height; ++y)
                {
                    const unsigned char* srow = sub_data    + (ptrdiff_t)y * sub_stride;
                    const unsigned char* prow = parent_data + (ptrdiff_t)y * parent_stride;
                    unsigned char* trow       = temp_data   + (ptrdiff_t)y * temp_stride;
                    for (S32 x = 0; x < p->width; ++x)
                    {
                        compose_pixel_w3c(mode, srow + x*4, prow + x*4, trow + x*4);
                    }
                }

                // SRC paint of the blended temp back onto the parent. The
                // parent's active clip masks which pixels are overwritten;
                // pixels outside the clip stay as they were. Pixels where
                // sub.alpha == 0 produce trow == prow, so SRC-overwriting
                // those bytes is a no-op.
                plutovg_canvas_save(p->canvas);
                plutovg_matrix_t identity;
                plutovg_matrix_init_identity(&identity);
                plutovg_canvas_set_matrix(p->canvas, &identity);
                plutovg_canvas_set_operator(p->canvas, PLUTOVG_OPERATOR_SRC);
                plutovg_canvas_set_texture(p->canvas, temp,
                                           PLUTOVG_TEXTURE_TYPE_PLAIN, 1.f, &identity);
                plutovg_canvas_paint(p->canvas);
                plutovg_canvas_restore(p->canvas);

                plutovg_surface_destroy(temp);
            }
        }

        plutovg_canvas_destroy(sub_canvas);
        plutovg_surface_destroy(sub_surface);
    }

    hb_paint_funcs_t* get_paint_funcs()
    {
        static hb_paint_funcs_t* funcs = []
        {
            hb_paint_funcs_t* f = hb_paint_funcs_create();
            hb_paint_funcs_set_push_transform_func     (f, push_transform_cb,       nullptr, nullptr);
            hb_paint_funcs_set_pop_transform_func      (f, pop_transform_cb,        nullptr, nullptr);
            hb_paint_funcs_set_push_clip_glyph_func    (f, push_clip_glyph_cb,      nullptr, nullptr);
            hb_paint_funcs_set_push_clip_rectangle_func(f, push_clip_rectangle_cb,  nullptr, nullptr);
            hb_paint_funcs_set_pop_clip_func           (f, pop_clip_cb,             nullptr, nullptr);
            hb_paint_funcs_set_color_func              (f, color_cb,                nullptr, nullptr);
            hb_paint_funcs_set_image_func              (f, image_cb,                nullptr, nullptr);
            hb_paint_funcs_set_linear_gradient_func    (f, linear_gradient_cb,      nullptr, nullptr);
            hb_paint_funcs_set_radial_gradient_func    (f, radial_gradient_cb,      nullptr, nullptr);
            hb_paint_funcs_set_sweep_gradient_func     (f, sweep_gradient_cb,       nullptr, nullptr);
            hb_paint_funcs_set_push_group_func         (f, push_group_cb,           nullptr, nullptr);
            hb_paint_funcs_set_pop_group_func          (f, pop_group_cb,            nullptr, nullptr);
            // No color_glyph_func override: leaving it default makes HB walk
            // the nested COLR glyph paint tree itself and emit per-paint
            // callbacks here, which is exactly what we want.
            // No custom_palette_color_func: HB pulls palette colors from the
            // CPAL table directly. We pass palette_index in hb_font_paint_glyph.
            hb_paint_funcs_make_immutable(f);
            return f;
        }();
        return funcs;
    }
}

LLFontColrV1Painter::LLFontColrV1Painter() = default;
LLFontColrV1Painter::~LLFontColrV1Painter() = default;

void LLFontColrV1Painter::testSoftwareBlendPixel(hb_paint_composite_mode_t mode,
                                                 const U8 src[4],
                                                 const U8 dst[4],
                                                 U8       out[4])
{
    compose_pixel_w3c(mode, src, dst, out);
}

bool LLFontColrV1Painter::paintGlyph(hb_font_t*       hb_font,
                                     U32              glyph_index,
                                     F32              fallback_point_size,
                                     const LLColor4U& foreground,
                                     U32              palette_index,
                                     OutputFormat     format,
                                     Result&          out)
{
    if (!hb_font)
        return false;

    hb_face_t* hb_face = hb_font_get_face(hb_font);
    if (!hb_face)
        return false;

    // Reject out-of-range glyph indices up front. hb_font_paint_glyph silently
    // succeeds (no callbacks fire) for unknown glyphs, and the empty-bbox
    // fallback below would then synthesize a 2-em-square surface and the
    // walker would hand back a transparent bitmap as if it had rendered a
    // valid glyph. Caller can't distinguish "no paint tree" from "empty
    // glyph" on the success path, so fail cleanly here instead.
    if (glyph_index >= hb_face_get_glyph_count(hb_face))
        return false;

    // Clamp the requested palette index to what the font actually carries.
    // hb_font_paint_glyph silently does *something* on out-of-range indices
    // (implementation-defined); pinning to the [0, count) range gives us a
    // deterministic fallback to palette 0 for fonts without the requested
    // palette while still honoring valid alternate-palette requests.
    {
        const unsigned palette_count = hb_ot_color_palette_get_count(hb_face);
        if (palette_count == 0u)
            palette_index = 0u;
        else if (palette_index >= palette_count)
            palette_index = 0u;
    }

    // Surface-sizing bbox in HB paint coordinates (pixels * 64, the same
    // 26.6 convention HB uses everywhere). x_bearing is the leftmost coord;
    // y_bearing is the topmost in HB's Y-up coord system. height is stored
    // negative for Y-up glyphs.
    S32 bbox_x_bearing = 0;
    S32 bbox_y_bearing = 0;
    S32 bbox_width     = 0;
    S32 bbox_height    = 0;

    // Prefer FT's COLRv1 clip box when present — that's the spec-authoritative
    // visual extent of the painted glyph. hb_font_get_glyph_extents returns
    // the underlying outline glyph's bbox, which for many COLRv1 glyphs (esp.
    // flag and family emoji built from many nested PaintColrGlyph layers)
    // is zero or wildly different from the painted output. Fall back to the
    // outline bbox if the font has no clip box for this glyph.
    FT_Face ft_face = hb_ft_font_get_ft_face(hb_font);
    FT_ClipBox clip = {};
    if (ft_face && FT_Get_Color_Glyph_ClipBox(ft_face, glyph_index, &clip))
    {
        // The clip box gives 4 corners in 26.6 (font-design coords scaled by
        // ppem * 64 — same units HB paint uses). For axis-aligned boxes the
        // corners line up; rotated boxes are rare but supported by computing
        // the AABB. Use the AABB unconditionally; the worst case is a slightly
        // looser bound.
        const FT_Pos xs[4] = { clip.bottom_left.x, clip.top_left.x,
                               clip.top_right.x,   clip.bottom_right.x };
        const FT_Pos ys[4] = { clip.bottom_left.y, clip.top_left.y,
                               clip.top_right.y,   clip.bottom_right.y };
        FT_Pos xmin = xs[0], xmax = xs[0], ymin = ys[0], ymax = ys[0];
        for (int i = 1; i < 4; ++i)
        {
            if (xs[i] < xmin) xmin = xs[i];
            if (xs[i] > xmax) xmax = xs[i];
            if (ys[i] < ymin) ymin = ys[i];
            if (ys[i] > ymax) ymax = ys[i];
        }
        bbox_x_bearing = (S32)xmin;
        bbox_y_bearing = (S32)ymax;          // HB y_bearing convention: top-most Y, Y-up
        bbox_width     = (S32)(xmax - xmin);
        bbox_height    = (S32)(ymin - ymax); // negative, matching HB convention
    }
    else
    {
        hb_glyph_extents_t ext = {};
        if (hb_font_get_glyph_extents(hb_font, glyph_index, &ext))
        {
            bbox_x_bearing = ext.x_bearing;
            bbox_y_bearing = ext.y_bearing;
            bbox_width     = ext.width;
            bbox_height    = ext.height;
        }
    }

    // Resolve ppem early — used for both the empty-bbox fallback below and
    // the supersampling threshold further down. Falls back to the caller-
    // supplied point_size when the hb_font has no scale set; that lets
    // paintGlyph still produce a sized surface for misconfigured faces.
    unsigned x_ppem = 0, y_ppem = 0;
    hb_font_get_ppem(hb_font, &x_ppem, &y_ppem);
    if (x_ppem == 0 && fallback_point_size > 0.f)
        x_ppem = static_cast<unsigned>(fallback_point_size + 0.5f);
    if (y_ppem == 0 && fallback_point_size > 0.f)
        y_ppem = static_cast<unsigned>(fallback_point_size + 0.5f);

    // Empty bbox happens for COLRv1 ligature glyphs (notably ZWJ emoji like
    // family-of-three or flags) whose underlying outline glyph is a stub —
    // the paint tree carries the visual content but the cmap/glyf entry has
    // zero extents. Without a clip box (which Noto-COLRv1 doesn't always
    // ship), neither FT nor HB can size the surface. Fall back to a generous
    // 2-em-square box centered on the baseline so HB's paint walk has space
    // to draw into; paint commands that go outside still get plutovg-clipped
    // to the surface, which is acceptable for the rare-edge case.
    if (bbox_width == 0 || bbox_height == 0)
    {
        if (x_ppem == 0 || y_ppem == 0)
            return false;
        const S32 ppem_x_64 = static_cast<S32>(x_ppem) * 64;
        const S32 ppem_y_64 = static_cast<S32>(y_ppem) * 64;
        // Center horizontally on the origin (-em..+em); vertically span from
        // 1.5em above baseline down to 0.5em below — covers descender-heavy
        // emoji while keeping the ascender at typical-cap-height bias.
        bbox_x_bearing = -ppem_x_64;
        bbox_y_bearing =  ppem_y_64 + (ppem_y_64 / 2);  // 1.5 em above baseline
        bbox_width     =  2 * ppem_x_64;
        bbox_height    = -2 * ppem_y_64;
    }

    // Convert 26.6 extents to integer pixels for the surface allocation.
    // Floor the bearings (rounded outward to the leftmost / topmost pixel
    // any paint command can possibly touch) and ceil the dimensions so the
    // surface fully covers the glyph bbox even when bearings have fractional
    // parts. The +PAD on each side absorbs AA edge spread.
    auto floor_div_64 = [](S32 v) -> S32 {
        // C++ truncates toward zero; for negative values that floors *toward
        // zero* not toward -infinity. Adjust so we always floor downward.
        return (v >= 0) ? (v >> 6) : -((-v + 63) >> 6);
    };
    auto ceil_div_64  = [](S32 v) -> S32 { return (v + 63) >> 6; };

    const S32 abs_h_26_6 = (bbox_height < 0) ? -bbox_height : bbox_height;
    const S32 left_px    = floor_div_64(bbox_x_bearing);
    const S32 top_px     = floor_div_64(bbox_y_bearing);
    const S32 width_px   = ceil_div_64(bbox_width);
    const S32 height_px  = ceil_div_64(abs_h_26_6);
    const S32 surf_w     = width_px  + 2 * GLYPH_PAD;
    const S32 surf_h     = height_px + 2 * GLYPH_PAD;
    if (surf_w <= 0 || surf_h <= 0 || surf_w > 1024 || surf_h > 1024)
    {
        // Cap matches the atlas sheet ceiling (LLFontBitmapCache caps at
        // 1024x1024); a glyph above that wouldn't fit a single sheet anyway.
        return false;
    }

    // Supersample factor: 2 below the threshold, 1 above. Output (atlas)
    // dims stay (surf_w, surf_h); the plutovg surface is allocated at
    // (surf_w * ss_factor, surf_h * ss_factor) and box-downsampled into
    // mStaging post-paint. Threshold gates on BOTH ppem axes so a font
    // with anisotropic scale doesn't half-supersample.
    const int ss_factor = (x_ppem > 0u && x_ppem <= SUPERSAMPLE_PPEM_THRESHOLD &&
                           y_ppem > 0u && y_ppem <= SUPERSAMPLE_PPEM_THRESHOLD)
                          ? 2 : 1;
    mLastUsedSupersampling = (ss_factor > 1);
    const S32 ss_w = surf_w * ss_factor;
    const S32 ss_h = surf_h * ss_factor;

    Painter p;
    p.surface = plutovg_surface_create(ss_w, ss_h);
    if (!p.surface)
        return false;
    // Plutovg doc doesn't explicitly guarantee zero-init; do it ourselves so
    // pixels outside any paint-tree clip stay transparent rather than carrying
    // arbitrary heap content. Cheap (one memset over a few KB).
    {
        plutovg_color_t transparent = { 0.f, 0.f, 0.f, 0.f };
        plutovg_surface_clear(p.surface, &transparent);
    }
    p.canvas = plutovg_canvas_create(p.surface);
    if (!p.canvas)
    {
        plutovg_surface_destroy(p.surface);
        return false;
    }
    // Painter sees the full SS surface dimensions. push_group_cb /
    // sweep_gradient_cb allocate sub-surfaces against p.width/p.height,
    // so they need to match the plutovg surface, not the final atlas size.
    p.width  = ss_w;
    p.height = ss_h;
    p.origin_x     = (F32)bbox_x_bearing;
    p.origin_y_top = (F32)bbox_y_bearing;
    // hb_color_t is little-endian uint32 layout: blue=byte3, green=byte2,
    // red=byte1, alpha=byte0 per the HB_COLOR macro convention. Build the
    // foreground value matching that bit layout from our LLColor4U (RGBA bytes).
    p.foreground = ((hb_color_t)foreground.mV[VBLUE] << 24)
                 | ((hb_color_t)foreground.mV[VGREEN] << 16)
                 | ((hb_color_t)foreground.mV[VRED]  << 8)
                 | ((hb_color_t)foreground.mV[VALPHA]);

    // Set up the surface transform: paint coords come in 26.6 (pixels * 64,
    // Y-up, glyph origin at HB(0,0)); we want surface pixel coords (Y-down,
    // glyph top-left at (PAD, PAD)). Apply translate-then-scale so user-coord
    // P maps to (-left_px+PAD + (1/64)*P.x, top_px+PAD + (-1/64)*P.y) device.
    // Test: HB top-left (x_bearing, y_bearing) -> (PAD, PAD); HB origin (0,0)
    //       -> (-left_px+PAD, top_px+PAD). Both match the surface layout.
    //
    // For supersampling, both the translate and the scale are multiplied
    // by ss_factor so HB(x_bearing, y_bearing) lands at (PAD*ss, PAD*ss)
    // on the SS surface — the entire device coord system scales linearly,
    // and a 2×2 box downsample post-paint lines back up exactly with the
    // atlas dims.
    constexpr F32 INV_64 = 1.f / 64.f;
    const F32 ss_f = (F32)ss_factor;
    plutovg_canvas_translate(p.canvas,
        ((F32)(-left_px) + (F32)GLYPH_PAD) * ss_f,
        ((F32)top_px    + (F32)GLYPH_PAD) * ss_f);
    plutovg_canvas_scale(p.canvas, INV_64 * ss_f, -INV_64 * ss_f);

    // Walk the paint tree. Palette index comes from the caller (typically
    // computed by LLFontFace::load from the EmojiUseDarkPalette setting and
    // the face's CPAL flags); foreground pre-resolved.
    hb_font_paint_glyph(hb_font, glyph_index, get_paint_funcs(), &p,
                        palette_index, p.foreground);

    // Defensive cleanup: a malformed paint tree (or HB bug) could leave the
    // group stack non-empty. Each frame holds owning surface+canvas pointers.
    // Free everything the painter is currently sitting on (the redirected
    // sub-surface) plus all parent frames before returning to the caller.
    auto teardown = [&](bool ok)
    {
        if (p.canvas)  plutovg_canvas_destroy(p.canvas);
        if (p.surface) plutovg_surface_destroy(p.surface);
        for (const auto& frame : p.group_stack)
        {
            if (frame.canvas)  plutovg_canvas_destroy(frame.canvas);
            if (frame.surface) plutovg_surface_destroy(frame.surface);
        }
        p.group_stack.clear();
        return ok;
    };

    if (p.aborted)
    {
        return teardown(false);
    }

    // Surface is BGRA premultiplied (plutovg native), top-row first, at
    // (ss_w, ss_h). Query the stride rather than assuming tight packing —
    // plutovg's documented contract is bytes-per-row and may include
    // padding. The Gray output path collapses each pixel to 1 byte; the
    // BGRA path is a row-by-row copy. Both paths box-downsample by 2×2
    // when ss_factor == 2.
    const S32 src_stride = plutovg_surface_get_stride(p.surface);
    const unsigned char* src = plutovg_surface_get_data(p.surface);
    if (src_stride < ss_w * 4 || !src)
    {
        // Sanity: a sane plutovg surface has stride >= width*4 bytes. If
        // we ever see less, the build has changed pixel format under us
        // and the BGRA->X fold below would walk off the end of the row.
        // Fail loudly into the caller's outline-fallback path.
        return teardown(false);
    }

    // Per-source-pixel hybrid fold for the Gray path. Lifted into a lambda
    // so the SS box-downsample can call it on each of the four source
    // pixels in a 2×2 block before averaging — `max` doesn't commute with
    // averaging, so the floor must apply per source pixel, not per block.
    auto fold_gray = [](U8 b, U8 g, U8 r, U8 a) -> U32 {
        const U32 y_256     = (U32)r * 54u + (U32)g * 183u + (U32)b * 18u;
        const U32 floor_256 = (U32)a * GRAY_ALPHA_FLOOR_256;
        const U32 cov_256   = (y_256 > floor_256) ? y_256 : floor_256;
        const U32 cov       = cov_256 >> 8;
        return cov > 255u ? 255u : cov;
    };

    if (format == OutputFormat::Gray)
    {
        // Hybrid coverage fold: max(Rec.709 luminance, alpha * floor).
        // Foreground-only glyphs (white premul: r=g=b=a) collapse to alpha
        // exactly because Y = (54+183+18)*a/256 ≈ a > a*0.15. Palette-painted
        // pixels keep luminance gradation where it dominates and fall back
        // to the alpha floor for fully-saturated dark colors so they still
        // register as a silhouette against light text colors.
        const size_t bytes = static_cast<size_t>(surf_w) * static_cast<size_t>(surf_h);
        mStaging.resize(bytes);
        if (ss_factor == 1)
        {
            for (S32 y = 0; y < surf_h; ++y)
            {
                const unsigned char* row = src + (ptrdiff_t)y * src_stride;
                U8* dst = mStaging.data() + (ptrdiff_t)y * surf_w;
                for (S32 x = 0; x < surf_w; ++x)
                {
                    dst[x] = (U8)fold_gray(row[x*4+0], row[x*4+1],
                                           row[x*4+2], row[x*4+3]);
                }
            }
        }
        else
        {
            // 2×2 box average of per-source-pixel folds. Round-to-nearest
            // via (sum + 2) >> 2 keeps the destination centered between
            // the four taps without cumulative bias.
            for (S32 dy = 0; dy < surf_h; ++dy)
            {
                U8* dst = mStaging.data() + (ptrdiff_t)dy * surf_w;
                for (S32 dx = 0; dx < surf_w; ++dx)
                {
                    U32 sum = 0;
                    for (int oy = 0; oy < 2; ++oy)
                    {
                        const unsigned char* row =
                            src + (ptrdiff_t)(dy*2 + oy) * src_stride;
                        for (int ox = 0; ox < 2; ++ox)
                        {
                            const S32 sx = dx*2 + ox;
                            sum += fold_gray(row[sx*4+0], row[sx*4+1],
                                             row[sx*4+2], row[sx*4+3]);
                        }
                    }
                    dst[dx] = (U8)((sum + 2u) >> 2);
                }
            }
        }
        out.mPitch  = surf_w;
        out.mFormat = OutputFormat::Gray;
    }
    else
    {
        // BGRA copy. Honor source stride: when src_stride > ss_w*4 the
        // padding bytes are skipped, and the destination is written tightly
        // packed (out.mPitch == surf_w*4) so downstream consumers can
        // continue to assume tight rows.
        const S32 dst_stride = surf_w * 4;
        const size_t bytes = static_cast<size_t>(dst_stride) * static_cast<size_t>(surf_h);
        mStaging.resize(bytes);
        if (ss_factor == 1)
        {
            for (S32 y = 0; y < surf_h; ++y)
            {
                std::memcpy(mStaging.data() + (ptrdiff_t)y * dst_stride,
                            src + (ptrdiff_t)y * src_stride,
                            (size_t)dst_stride);
            }
        }
        else
        {
            // 2×2 box average per channel. Premultiplied BGRA averages
            // linearly — both luminance and alpha scale linearly with the
            // mixing ratio, so straight per-channel averaging is correct.
            for (S32 dy = 0; dy < surf_h; ++dy)
            {
                U8* drow = mStaging.data() + (ptrdiff_t)dy * dst_stride;
                for (S32 dx = 0; dx < surf_w; ++dx)
                {
                    U32 sb = 0, sg = 0, sr = 0, sa = 0;
                    for (int oy = 0; oy < 2; ++oy)
                    {
                        const unsigned char* srow =
                            src + (ptrdiff_t)(dy*2 + oy) * src_stride;
                        for (int ox = 0; ox < 2; ++ox)
                        {
                            const unsigned char* sp = srow + (dx*2 + ox)*4;
                            sb += sp[0]; sg += sp[1]; sr += sp[2]; sa += sp[3];
                        }
                    }
                    U8* dp = drow + dx*4;
                    dp[0] = (U8)((sb + 2u) >> 2);
                    dp[1] = (U8)((sg + 2u) >> 2);
                    dp[2] = (U8)((sr + 2u) >> 2);
                    dp[3] = (U8)((sa + 2u) >> 2);
                }
            }
        }
        out.mPitch  = dst_stride;
        out.mFormat = OutputFormat::BGRA;
    }
    mStagingWidth  = surf_w;
    mStagingHeight = surf_h;

    teardown(true);

    // Cap the per-thread staging high-water mark. Once we've held more than
    // 4× the current glyph and the buffer exceeds STAGING_HIGH_WATER_BYTES,
    // shrink. Bounded by max(current, threshold) so a steady stream of
    // similarly-sized glyphs doesn't thrash through reallocations.
    if (mStaging.capacity() > mStaging.size() * 4
        && mStaging.size() < STAGING_HIGH_WATER_BYTES)
    {
        mStaging.shrink_to_fit();
    }

    out.mBitmap = mStaging.data();
    out.mWidth  = surf_w;
    out.mHeight = surf_h;
    // FT bitmap_left/_top are in PIXELS. The surface is PAD pixels wider than
    // the glyph bbox on each side, so the leftmost pixel of the bitmap sits
    // one PAD to the left of where the glyph bbox starts (left_px), and the
    // topmost pixel sits one PAD above the bbox top (top_px).
    out.mLeft   = left_px - GLYPH_PAD;
    out.mTop    = top_px  + GLYPH_PAD;
    out.mForegroundOnly = !p.saw_palette_color;
    return true;
}
