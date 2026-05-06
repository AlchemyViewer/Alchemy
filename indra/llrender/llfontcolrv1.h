/**
 * @file llfontcolrv1.h
 * @brief COLRv1 paint-tree rasterizer. Bridges HarfBuzz hb_paint_funcs_t
 *        callbacks to plutovg, then publishes the result as either a BGRA
 *        bitmap (the existing color-glyph atlas path can consume unchanged)
 *        or a single-channel grayscale (luminance-shaded) bitmap that the
 *        Grayscale atlas can consume for monochrome rendering.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#ifndef LL_LLFONTCOLRV1_H
#define LL_LLFONTCOLRV1_H

#include "stdtypes.h"
#include "v4coloru.h"

#include <hb.h>  // hb_paint_composite_mode_t for testSoftwareBlendPixel

#include <vector>

struct hb_font_t;

class LLFontColrV1Painter
{
public:
    // Output pixel format requested by the caller. BGRA is the painter's
    // native plutovg-backed format; Gray is a hybrid luminance/alpha fold
    // suitable for the existing Grayscale atlas (luminance-shaded silhouette
    // that tints with text_color at draw time).
    enum class OutputFormat
    {
        BGRA,
        Gray,
    };

    // Result of a paint pass. mBitmap layout depends on mFormat:
    //   - BGRA: 4 bytes per pixel, premultiplied, top-row-first.
    //   - Gray: 1 byte per pixel (coverage = max(Rec.709 luminance,
    //           alpha * 0.15)), top-row-first.
    // mPitch is bytes per row and is ALWAYS POSITIVE: surf_w*4 for BGRA,
    // surf_w for Gray. mBitmap stays valid until the next paintGlyph call
    // on the same painter.
    struct Result
    {
        const U8*    mBitmap = nullptr;
        S32          mWidth  = 0;
        S32          mHeight = 0;
        S32          mPitch  = 0;
        S32          mLeft   = 0;  // bitmap_left equivalent
        S32          mTop    = 0;  // bitmap_top equivalent
        // True iff every fill in the paint tree was the foreground slot —
        // i.e. no CPAL palette colors were ever referenced. Caller can use
        // this to decide whether to tint the rasterized BGRA glyph with the
        // active text color at draw time (which preserves the white-baked
        // bitmap as a luminance mask) or with the emoji-fixed white (which
        // preserves CPAL palette colors verbatim). Always false on abort.
        // Meaningless for Gray output (the Grayscale atlas always tints
        // with text_color regardless).
        bool         mForegroundOnly = false;
        OutputFormat mFormat = OutputFormat::BGRA;
    };

    LLFontColrV1Painter();
    ~LLFontColrV1Painter();

    // Walk hb_font's paint tree for `glyph_index` and rasterize into the
    // painter's internal staging surface. `foreground` is the active text
    // color used to resolve foreground-color paint slots (COLRv1 references
    // CPAL palette entry 0xFFFF as "use foreground"). `palette_index`
    // selects which CPAL palette HB resolves color refs against; 0 is the
    // default palette. The painter clamps oversized indices to [0, count).
    // `format` selects the output pixel layout (see OutputFormat above).
    // `fallback_point_size` is the requested type size in pixels; only used
    // when hb_font_get_ppem returns zero (the painter normally pulls ppem
    // off the hb_font).
    //
    // Returns true on success and populates `out`. Returns false when the
    // glyph has no paint tree, the paint tree uses a feature we don't yet
    // support, or staging allocation failed; the caller falls back to
    // outline rasterization.
    bool paintGlyph(hb_font_t*       hb_font,
                    U32              glyph_index,
                    F32              fallback_point_size,
                    const LLColor4U& foreground,
                    U32              palette_index,
                    OutputFormat     format,
                    Result&          out);

    // Source-compat overload for callers that don't care about output
    // format and just want the painter's native BGRA result. Forwards to
    // the format-aware overload.
    bool paintGlyph(hb_font_t*       hb_font,
                    U32              glyph_index,
                    F32              fallback_point_size,
                    const LLColor4U& foreground,
                    U32              palette_index,
                    Result&          out)
    {
        return paintGlyph(hb_font, glyph_index, fallback_point_size,
                          foreground, palette_index, OutputFormat::BGRA, out);
    }

    // Test/debug accessor: true if the most recent paintGlyph used 2×
    // supersampling (low-ppem path). Atlas output is unaffected — the
    // staging buffer is always at the final per-glyph dimensions — but
    // tests want to assert SS kicks in below the threshold and skips
    // above without inferring it from output bytes.
    bool lastUsedSupersampling() const { return mLastUsedSupersampling; }

    // Test entry point: composes one pair of BGRA-premultiplied pixels under
    // the given hb composite mode using the same software blend path
    // pop_group_cb invokes for W3C / HSL / PLUS modes. Exposed because the
    // per-mode blend math is otherwise reachable only via a font that ships
    // glyphs using that mode, which not all test corpora cover. Production
    // callers go through paintGlyph; this is for unit tests only.
    static void testSoftwareBlendPixel(hb_paint_composite_mode_t mode,
                                       const U8 src[4],
                                       const U8 dst[4],
                                       U8 out[4]);

private:
    LLFontColrV1Painter(const LLFontColrV1Painter&) = delete;
    LLFontColrV1Painter& operator=(const LLFontColrV1Painter&) = delete;

    // Staging surface. Reused across paintGlyph calls; resized on demand.
    // For BGRA output: premultiplied, top-row-first, surf_w*4 bytes/row.
    // For Gray output: 8-bit coverage, top-row-first, surf_w bytes/row.
    std::vector<U8> mStaging;
    S32             mStagingWidth  = 0;
    S32             mStagingHeight = 0;

    // Tracks whether the last paintGlyph call took the supersampling path.
    // Exposed via lastUsedSupersampling() for tests; not used for rendering
    // decisions.
    bool            mLastUsedSupersampling = false;
};

#endif // LL_LLFONTCOLRV1_H
