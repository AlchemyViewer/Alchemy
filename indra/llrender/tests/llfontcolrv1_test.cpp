/**
 * @file llfontcolrv1_test.cpp
 * @brief Unit tests for COLRv1 detection and the paint walker that
 *        rasterises Noto-COLRv1 glyphs.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llfontcolrv1.h"
#include "../llfontface.h"
#include "../llfontfreetype.h"
#include "../llfontregistry.h"

#include "../test/lltut.h"

#include <hb.h>
#include <hb-ft.h>

#include <cstdio>

namespace
{
    // Resolved at test time, points at the source-tree font directory so the
    // tests don't depend on whatever the runtime install staged. Set via the
    // LLFONT_TEST_DATA_DIR compile-time define from CMake.
#ifndef LLFONT_TEST_DATA_DIR
#  define LLFONT_TEST_DATA_DIR ""
#endif

    constexpr const char* kFontDir = LLFONT_TEST_DATA_DIR;

    bool fileExists(const std::string& path)
    {
        if (FILE* f = std::fopen(path.c_str(), "rb"))
        {
            std::fclose(f);
            return true;
        }
        return false;
    }

    // Construct a face key with reasonable defaults. point_size 14 / DPI 96
    // is a typical UI-text size; the painter doesn't care what we pick as
    // long as ppem is non-zero.
    LLFontFaceKey makeKey(const std::string& filename)
    {
        return LLFontFaceKey{
            filename,
            /*face_index=*/0,
            /*point_size=*/14.f,
            /*vert_dpi=*/96.f,
            /*horz_dpi=*/96.f,
            /*weight=*/-1,
            EFontHinting::DEFAULT,
            /*flags=*/0
        };
    }

    // Walks a putative BGRA buffer (top-row first, tightly packed) and
    // returns true if any pixel has non-zero alpha. Used to confirm the
    // painter actually drew something into the surface.
    bool anyAlpha(const U8* bgra, S32 w, S32 h, S32 pitch)
    {
        for (S32 y = 0; y < h; ++y)
        {
            const U8* row = bgra + (ptrdiff_t)y * pitch;
            for (S32 x = 0; x < w; ++x)
            {
                if (row[x * 4 + 3] != 0)
                    return true;
            }
        }
        return false;
    }
}

namespace tut
{
    // Each test_group<>::object spins up a fresh fixture for every test<N>,
    // so init/cleanup the font manager once per test rather than once per
    // process. Cheaper than tracking init state and matches the test-
    // isolation model TUT expects.
    struct llfontcolrv1_data
    {
        llfontcolrv1_data()
        {
            LLFontManager::initClass();
        }
        ~llfontcolrv1_data()
        {
            LLFontManager::cleanupClass();
        }
    };

    typedef test_group<llfontcolrv1_data> llfontcolrv1_test;
    typedef llfontcolrv1_test::object     llfontcolrv1_object;
    tut::llfontcolrv1_test llfontcolrv1_testcase("LLFontColrV1");

    // Painter input validation: null hb_font fails cleanly without a crash.
    // Doesn't need a real font, so this test runs even when the test data
    // dir is empty (e.g., a stripped CI checkout).
    template<> template<>
    void llfontcolrv1_object::test<1>()
    {
        LLFontColrV1Painter painter;
        LLFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        ensure("paintGlyph(null hb_font) returns false",
               !painter.paintGlyph(nullptr, /*glyph_index=*/0,
                                   /*point_size=*/14.f, fg,
                                   /*palette_index=*/0, result));
        ensure("Result.mBitmap stays null on failure",
               result.mBitmap == nullptr);
        ensure_equals("Result.mWidth stays zero on failure",
                      result.mWidth, 0);
    }

    // hasColrV1 is the in-tree probe for "FreeType's FT_LOAD_COLOR can't
    // rasterise this — route through the paint walker." Verify it fires
    // for Noto-COLRv1.ttf, which ships a COLRv1 paint table.
    template<> template<>
    void llfontcolrv1_object::test<2>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            skip("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<LLFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("Noto-COLRv1 loaded", face.notNull() && face->isValid());
        ensure("Noto-COLRv1 hasColrV1 == true", face->hasColrV1());
        ensure("Noto-COLRv1 hasColor == true (general flag)", face->hasColor());
    }

    // Negative: InterVariable.woff2 is a plain text font with no COLR
    // table, so hasColrV1 must stay false. Guards against the probe
    // false-positiving on any FT_HAS_COLOR-y face.
    template<> template<>
    void llfontcolrv1_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
        {
            skip("InterVariable.woff2 not present in test data dir");
        }

        LLPointer<LLFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("Inter loaded", face.notNull() && face->isValid());
        ensure("Inter hasColrV1 == false", !face->hasColrV1());
    }

    // Painter end-to-end: feed a known emoji glyph (fire, U+1F525) through
    // hb_font_paint_glyph and verify the resulting BGRA buffer has at least
    // one non-zero-alpha pixel. Also exercises the FT_Get_Color_Glyph_ClipBox
    // path (Noto-COLRv1 ships clip boxes for its glyphs) and the BGRA atlas
    // packing logic in paintGlyph.
    template<> template<>
    void llfontcolrv1_object::test<4>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            skip("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<LLFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("face loaded",     face.notNull() && face->isValid());
        ensure("face has COLRv1", face->hasColrV1());

        hb_font_t* hb_font = face->getHbFont();
        ensure("hb_font available", hb_font != nullptr);

        // Resolve the fire emoji's glyph index via the cmap. Avoids hardcoding
        // a glyph_id that could shift across font versions.
        const hb_codepoint_t fire_cp = 0x1F525;
        hb_codepoint_t       fire_gid = 0;
        ensure("cmap maps U+1F525 to a glyph",
               hb_font_get_nominal_glyph(hb_font, fire_cp, &fire_gid)
               && fire_gid != 0);

        LLFontColrV1Painter painter;
        LLFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        const bool ok = painter.paintGlyph(hb_font, fire_gid, /*point_size=*/14.f,
                                           fg, /*palette_index=*/0, result);
        ensure("paintGlyph succeeded", ok);
        ensure("painter produced bitmap",
               result.mBitmap != nullptr
            && result.mWidth > 0 && result.mHeight > 0
            && result.mPitch != 0);
        ensure("painter drew non-zero alpha somewhere",
               anyAlpha(result.mBitmap, result.mWidth, result.mHeight, result.mPitch));
    }

    // VS-16 cmap absence is the trigger for shape_sub_run's stripping path.
    // Verify Noto-COLRv1 actually has the shape we expect: ZWJ in cmap but
    // VS-16 not. Other emoji fonts that ship VS-16 (e.g. Twemoji) take the
    // no-strip path; the shape pipeline branches on this flag per-face.
    template<> template<>
    void llfontcolrv1_object::test<5>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            skip("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<LLFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("face loaded", face.notNull() && face->isValid());

        // ZWJ should resolve to a real glyph (cmap entry present).
        ensure("Noto cmap has ZWJ glyph",
               face->getCharGlyphIndex(0x200D) != 0u);
        // VS-16 should NOT — shape_sub_run keys VS-16 stripping on this.
        ensure_equals("Noto cmap has no VS-16 glyph",
                      face->getCharGlyphIndex(0xFE0F), 0u);
    }
}
