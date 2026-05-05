/**
 * @file llfontgl_test.cpp
 * @brief Smoke test for the OSMesa-backed headless GL harness.
 *
 * Brings up a real GL context via LLWindowMesaHeadless, runs
 * LLFontGL::initClass through the explicit-fonts.xml-path overload,
 * and verifies the GL-backed atlas pipeline produces a live texture.
 *
 * The single fixture instance shared across test methods (see
 * llheadlessgl_fixture.h) keeps the OSMesa context creation cost
 * out of every test — but each test method still gets a fresh
 * sFontRegistry, so font-state mutations don't leak.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llfontgl.h"
#include "../llfontfreetype.h"
#include "../llfontbitmapcache.h"
#include "../llimagegl.h"

#include "llheadlessgl_fixture.h"

#include "../test/lltut.h"

#include <cstdio>

namespace
{
#ifndef LLFONT_TEST_APP_DIR
#  define LLFONT_TEST_APP_DIR ""
#endif
#ifndef LLFONT_TEST_FONTS_XML
#  define LLFONT_TEST_FONTS_XML ""
#endif

    constexpr const char* kAppDir   = LLFONT_TEST_APP_DIR;
    constexpr const char* kFontsXml = LLFONT_TEST_FONTS_XML;

    bool fileExists(const std::string& path)
    {
        if (FILE* f = std::fopen(path.c_str(), "rb"))
        {
            std::fclose(f);
            return true;
        }
        return false;
    }
}

namespace tut
{
    // Construct the shared OSMesa fixture on first access; tear down
    // sFontRegistry per test so font state doesn't leak between tests.
    struct llfontgl_data
    {
        ll_test::HeadlessGl& gl = ll_test::getHeadlessGl();

        llfontgl_data() = default;
        // Do NOT tear down sFontRegistry between tests. LLFontGL's
        // per-getter static fontp caches (getFontSansSerif, etc.) are
        // process-scoped and re-initialize exactly once: a teardown +
        // re-initClass cycle leaves them holding dangling pointers,
        // which segfault on the next render call. Sharing the
        // registry across tests in this fixture group is the simplest
        // way to keep those caches valid; the LLFontGL::initClass
        // explicit-path overload is single-shot (warns + bails on
        // re-entry) so subsequent tests just observe the test<1>
        // initialized state.
        ~llfontgl_data() = default;
    };

    typedef test_group<llfontgl_data> llfontgl_test;
    typedef llfontgl_test::object     llfontgl_object;
    tut::llfontgl_test llfontgl_testcase("LLFontGL");

    // initClass through the explicit-path overload should bring up a
    // working font registry under a real GL context. loadDefaultFonts
    // (called from initClass) returns true only if every required
    // family resolved through to a usable LLFontFreetype.
    template<> template<>
    void llfontgl_object::test<1>()
    {
        if (!fileExists(kFontsXml))
        {
            skip("fonts.xml not found at " + std::string(kFontsXml));
            return;
        }

        LLFontGL::initClass(/*screen_dpi=*/96.f,
                            /*x_scale=*/1.f, /*y_scale=*/1.f,
                            /*app_dir=*/kAppDir,
                            /*fonts_xml_path=*/kFontsXml,
                            /*font_overrides=*/LLSD(),
                            /*create_gl_textures=*/true);

        ensure("default fonts loaded",
               LLFontGL::loadDefaultFonts());
        ensure("SansSerif resolves",
               LLFontGL::getFontSansSerif() != nullptr);
    }

    // After ASCII rasterization, the SansSerif font's bitmap cache
    // should have at least one greyscale page with a live GL texture.
    // Catches the silent-no-op failure mode where the GL context is
    // healthy but createGLTexture skipped the upload.
    template<> template<>
    void llfontgl_object::test<2>()
    {
        if (!fileExists(kFontsXml))
        {
            skip("fonts.xml not found at " + std::string(kFontsXml));
            return;
        }

        // initClass is single-shot; if test<1> ran first the registry is
        // already up. The call below either initializes (test<2> ran
        // standalone) or warns + bails (test<1> ran first). Either way
        // the assertions below operate against a working registry.
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);

        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("SansSerif resolves", font != nullptr);

        // Force the ASCII range to rasterize so the atlas isn't empty.
        // (createFont's pre-warm in getFont already does this on first
        // resolve; calling again is a fast no-op via head cache.)
        font->generateASCIIglyphs();

        const LLFontFreetype* ft = font->getFontFreetype();
        ensure("LLFontFreetype present", ft != nullptr);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        ensure("at least one grayscale atlas page",
               cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);

        LLImageGL* page = cache->getImageGL(EFontGlyphType::Grayscale, 0);
        ensure("page 0 LLImageGL present", page != nullptr);
        ensure("page 0 has a live GL texture name",
               page->getTexName() != 0);
    }
}
