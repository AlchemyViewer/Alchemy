/**
 * @file llfontcolrv1.h
 * @brief COLRv1 paint-tree rasterizer. Bridges HarfBuzz hb_paint_funcs_t
 *        callbacks to plutovg, then publishes the result as a BGRA bitmap
 *        the existing color-glyph atlas path can consume unchanged.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#ifndef LL_LLFONTCOLRV1_H
#define LL_LLFONTCOLRV1_H

#include "stdtypes.h"
#include "v4coloru.h"

#include <vector>

struct hb_font_t;

class LLFontColrV1Painter
{
public:
    // Result of a paint pass. mBitmap is BGRA in FreeType's draw-row order
    // (top first); mPitch is signed bytes-per-row, may be positive or negative
    // matching FreeType's convention. mBitmap stays valid until the next
    // paintGlyph call on the same painter.
    struct Result
    {
        const U8* mBitmap = nullptr;
        S32       mWidth  = 0;
        S32       mHeight = 0;
        S32       mPitch  = 0;
        S32       mLeft   = 0;  // bitmap_left equivalent
        S32       mTop    = 0;  // bitmap_top equivalent
    };

    LLFontColrV1Painter();
    ~LLFontColrV1Painter();

    // Walk hb_font's paint tree for `glyph_index` and rasterize into the
    // painter's internal staging surface. `foreground` is the active text
    // color used to resolve foreground-color paint slots (COLRv1 references
    // CPAL palette entry 0xFFFF as "use foreground"). `point_size` is the
    // requested type size in pixels — the painter uses it to size the
    // staging surface and to map font-design-unit coordinates to pixels.
    //
    // Returns true on success and populates `out`. Returns false when the
    // glyph has no paint tree, the paint tree uses a feature we don't yet
    // support, or staging allocation failed; the caller falls back to
    // outline rasterization.
    bool paintGlyph(hb_font_t*    hb_font,
                    U32           glyph_index,
                    F32           point_size,
                    const LLColor4U& foreground,
                    Result&       out);

private:
    LLFontColrV1Painter(const LLFontColrV1Painter&) = delete;
    LLFontColrV1Painter& operator=(const LLFontColrV1Painter&) = delete;

    // Staging surface. Reused across paintGlyph calls; resized on demand.
    // BGRA, premultiplied, top-row-first (positive pitch).
    std::vector<U8> mStaging;
    S32             mStagingWidth  = 0;
    S32             mStagingHeight = 0;
};

#endif // LL_LLFONTCOLRV1_H
