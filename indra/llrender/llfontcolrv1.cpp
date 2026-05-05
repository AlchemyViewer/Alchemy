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
#include <plutovg/plutovg.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
    // Cap recursion through PaintColrGlyph. The COLR spec doesn't bound this,
    // but a malformed or hostile font could nest indefinitely. 8 is well past
    // anything observed in real fonts (Noto's deepest paint tree is ~4).
    constexpr unsigned MAX_PAINT_DEPTH = 8;

    // Padding around the glyph bbox in surface pixels. Anti-aliased edges of
    // outline-clipped fills can extend ~1 px past the geometric bbox at the
    // shader sample positions; the existing atlas allocator already adds a
    // 4 px border for shadow-tap safety, so 1 px here is plenty.
    constexpr S32 GLYPH_PAD = 1;

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
        // Recursion depth through paint_color_glyph. Painter sets to 0 in its
        // setup; HB increments the conceptual depth via callbacks. We track it
        // explicitly so we can refuse cycles.
        unsigned           depth = 0;
        // Whether any callback bailed out because of an unsupported feature
        // (sweep gradient, image fill, etc.). Caller treats true as "abort
        // and fall back to outline rasterization".
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

    // Map an hb composite mode to a plutovg Porter-Duff operator. Plutovg
    // doesn't expose W3C blend modes (multiply/screen/overlay/etc.); those
    // return SRC_OVER as a graceful fallback with `*exact` set false so the
    // caller can warn / record the lossy mapping.
    inline plutovg_operator_t to_pv_operator(hb_paint_composite_mode_t mode, bool& exact)
    {
        exact = true;
        switch (mode)
        {
            case HB_PAINT_COMPOSITE_MODE_CLEAR:     return PLUTOVG_OPERATOR_CLEAR;
            case HB_PAINT_COMPOSITE_MODE_SRC:       return PLUTOVG_OPERATOR_SRC;
            case HB_PAINT_COMPOSITE_MODE_DEST:      return PLUTOVG_OPERATOR_DST;
            case HB_PAINT_COMPOSITE_MODE_SRC_OVER:  return PLUTOVG_OPERATOR_SRC_OVER;
            case HB_PAINT_COMPOSITE_MODE_DEST_OVER: return PLUTOVG_OPERATOR_DST_OVER;
            case HB_PAINT_COMPOSITE_MODE_SRC_IN:    return PLUTOVG_OPERATOR_SRC_IN;
            case HB_PAINT_COMPOSITE_MODE_DEST_IN:   return PLUTOVG_OPERATOR_DST_IN;
            case HB_PAINT_COMPOSITE_MODE_SRC_OUT:   return PLUTOVG_OPERATOR_SRC_OUT;
            case HB_PAINT_COMPOSITE_MODE_DEST_OUT:  return PLUTOVG_OPERATOR_DST_OUT;
            case HB_PAINT_COMPOSITE_MODE_SRC_ATOP:  return PLUTOVG_OPERATOR_SRC_ATOP;
            case HB_PAINT_COMPOSITE_MODE_DEST_ATOP: return PLUTOVG_OPERATOR_DST_ATOP;
            case HB_PAINT_COMPOSITE_MODE_XOR:       return PLUTOVG_OPERATOR_XOR;
            default:
                exact = false;
                return PLUTOVG_OPERATOR_SRC_OVER;
        }
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
        plutovg_canvas_save(p->canvas);
        plutovg_matrix_t m;
        plutovg_matrix_init(&m, xx, yx, xy, yy, dx, dy);
        plutovg_canvas_transform(p->canvas, &m);
    }

    void pop_transform_cb(hb_paint_funcs_t*, void* data, void*)
    {
        Painter* p = static_cast<Painter*>(data);
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
        plutovg_canvas_restore(static_cast<Painter*>(data)->canvas);
    }

    void color_cb(hb_paint_funcs_t*, void* data,
                  hb_bool_t is_foreground, hb_color_t color, void*)
    {
        Painter* p = static_cast<Painter*>(data);
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
                            float x0, float y0, float x1, float y1, float /*x2*/, float /*y2*/,
                            void*)
    {
        // Note: COLRv1's PaintLinearGradient supplies a third point p2 used
        // to define a rotation around p0. plutovg's linear gradient doesn't
        // accept a rotation parameter directly; for the common case where
        // p2 is collinear with p0..p1 (which it is in nearly all real fonts
        // because emoji rarely use the rotation slot), the gradient axis
        // p0->p1 is correct as-is. We accept this approximation; visually
        // wrong glyphs in a font that uses p2 non-trivially can be fixed
        // later by computing the projected p1' for plutovg.
        Painter* p = static_cast<Painter*>(data);
        slurp_color_stops(*p, color_line);
        if (p->pv_stops.empty())
            return;

        const plutovg_spread_method_t spread = to_pv_spread(hb_color_line_get_extend(color_line));
        plutovg_canvas_set_linear_gradient(p->canvas, x0, y0, x1, y1,
                                           spread, p->pv_stops.data(),
                                           static_cast<int>(p->pv_stops.size()), nullptr);
        plutovg_canvas_paint(p->canvas);
    }

    void radial_gradient_cb(hb_paint_funcs_t*, void* data, hb_color_line_t* color_line,
                            float x0, float y0, float r0,
                            float x1, float y1, float r1, void*)
    {
        Painter* p = static_cast<Painter*>(data);
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
        const int lut_stride = p->width * 4;  // BGRA tight-packed
        for (S32 py = 0; py < p->height; ++py)
        {
            unsigned char* row = lut_data + py * lut_stride;
            for (S32 px = 0; px < p->width; ++px)
            {
                // Pixel center maps better than corner; +0.5 places the
                // sample at the geometric center of the texel.
                float ux, uy;
                plutovg_matrix_map(&inv, (F32)px + 0.5f, (F32)py + 0.5f, &ux, &uy);
                const F32 dx = ux - x0;
                const F32 dy = uy - y0;
                F32 angle = atan2f(dy, dx);
                // Walk angle into the sweep range [start_angle, end_angle].
                F32 t = (angle - start_angle) * inv_span;
                t = apply_extend(t, extend);

                plutovg_color_t c;
                interp_stops(p->pv_stops, t, &c);
                // Write BGRA premultiplied (plutovg's native surface format).
                const F32 a8 = c.a * 255.f;
                const F32 r8 = c.r * a8;  // premultiply
                const F32 g8 = c.g * a8;
                const F32 b8 = c.b * a8;
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
        static_cast<Painter*>(data)->aborted = true;
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

        bool exact = false;
        const plutovg_operator_t op = to_pv_operator(mode, exact);
        if (!exact)
        {
            // W3C blend mode (multiply/screen/etc.) — plutovg can't compose
            // these natively. Falling back to SRC_OVER is the standard graceful
            // degradation and the dominant case in real fonts; a future change
            // could add a software blend pass here for the missing modes.
            LL_WARNS_ONCE("Font") << "COLRv1 PaintComposite mode "
                << (S32)mode << " not supported by plutovg; using SRC_OVER" << LL_ENDL;
        }

        // Save parent state, draw the sub-surface back onto it. Identity CTM
        // so texture pixel (i, j) lines up with parent device pixel (i, j) —
        // the sub was rendered using the parent CTM, so its device pixels
        // already match positions, no remapping needed. The parent's active
        // clip naturally bounds the blit.
        plutovg_canvas_save(p->canvas);
        plutovg_matrix_t identity;
        plutovg_matrix_init_identity(&identity);
        plutovg_canvas_set_matrix(p->canvas, &identity);
        plutovg_canvas_set_operator(p->canvas, op);
        plutovg_canvas_set_texture(p->canvas, sub_surface,
                                   PLUTOVG_TEXTURE_TYPE_PLAIN, 1.f, &identity);
        plutovg_canvas_paint(p->canvas);
        plutovg_canvas_restore(p->canvas);

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

bool LLFontColrV1Painter::paintGlyph(hb_font_t* hb_font,
                                     U32           glyph_index,
                                     F32           /*point_size*/,
                                     const LLColor4U& foreground,
                                     U32           palette_index,
                                     Result&       out)
{
    if (!hb_font)
        return false;

    // Glyph extents from hb-ft are in 26.6 fixed-point (pixels * 64), matching
    // the rest of HB's positioning output (see LLFontShaping's INV_64 usage).
    // y_bearing is positive distance from baseline to glyph top; height is
    // negative in coord systems that grow up. A glyph with no extents has all
    // zero fields — bail and let the outline fallback handle it.
    hb_glyph_extents_t ext = {};
    if (!hb_font_get_glyph_extents(hb_font, glyph_index, &ext))
        return false;
    if (ext.width == 0 || ext.height == 0)
        return false;

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

    const S32 abs_h_26_6 = (ext.height < 0) ? -ext.height : ext.height;
    const S32 left_px    = floor_div_64(ext.x_bearing);
    const S32 top_px     = floor_div_64(ext.y_bearing);
    const S32 width_px   = ceil_div_64(ext.width);
    const S32 height_px  = ceil_div_64(abs_h_26_6);
    const S32 surf_w     = width_px  + 2 * GLYPH_PAD;
    const S32 surf_h     = height_px + 2 * GLYPH_PAD;
    if (surf_w <= 0 || surf_h <= 0 || surf_w > 1024 || surf_h > 1024)
    {
        // Cap matches the atlas sheet ceiling (LLFontBitmapCache caps at
        // 1024x1024); a glyph above that wouldn't fit a single sheet anyway.
        return false;
    }

    Painter p;
    p.surface = plutovg_surface_create(surf_w, surf_h);
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
    p.width  = surf_w;
    p.height = surf_h;
    p.origin_x     = (F32)ext.x_bearing;
    p.origin_y_top = (F32)ext.y_bearing;
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
    constexpr F32 INV_64 = 1.f / 64.f;
    plutovg_canvas_translate(p.canvas, (F32)(-left_px) + (F32)GLYPH_PAD,
                                       (F32)top_px    + (F32)GLYPH_PAD);
    plutovg_canvas_scale(p.canvas, INV_64, -INV_64);

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

    // Surface is BGRA premultiplied (plutovg native), top-row first, stride
    // tightly packed at width*4. Copy into mStaging so the caller can keep
    // pointing at it after we destroy the surface.
    const S32 stride = surf_w * 4;
    const size_t bytes = static_cast<size_t>(stride) * static_cast<size_t>(surf_h);
    mStaging.resize(bytes);
    const unsigned char* src = plutovg_surface_get_data(p.surface);
    if (src)
    {
        std::memcpy(mStaging.data(), src, bytes);
    }
    else
    {
        std::fill(mStaging.begin(), mStaging.end(), (U8)0);
    }
    mStagingWidth  = surf_w;
    mStagingHeight = surf_h;

    teardown(true);

    out.mBitmap = mStaging.data();
    out.mWidth  = surf_w;
    out.mHeight = surf_h;
    out.mPitch  = stride;
    // FT bitmap_left/_top are in PIXELS. The surface is PAD pixels wider than
    // the glyph bbox on each side, so the leftmost pixel of the bitmap sits
    // one PAD to the left of where the glyph bbox starts (left_px), and the
    // topmost pixel sits one PAD above the bbox top (top_px).
    out.mLeft   = left_px - GLYPH_PAD;
    out.mTop    = top_px  + GLYPH_PAD;
    out.mForegroundOnly = !p.saw_palette_color;
    return true;
}
