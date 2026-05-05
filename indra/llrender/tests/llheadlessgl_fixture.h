/**
 * @file llheadlessgl_fixture.h
 * @brief One-shot OSMesa GL bring-up shared across the GL-backed
 *        llrender integration tests.
 *
 * The OSMesa context is the expensive part — TUT spins a fresh
 * fixture per test method, so we hide a static-local instance
 * behind getHeadlessGl() and let every fixture struct embed a
 * reference. That keeps each test isolated against its own state
 * (font registry, atlas pages) while reusing the GL context.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#ifndef LL_LLHEADLESSGL_FIXTURE_H
#define LL_LLHEADLESSGL_FIXTURE_H

#include "linden_common.h"

#include "../llgltexture.h"
#include "../llimagegl.h"
#include "../llfontfreetype.h"
#include "../llfontshaping.h"
#include "../llvertexbuffer.h"

#include "llwindowcallbacks.h"
#include "llwindowmesaheadless.h"

namespace ll_test
{
    // Drop transient font caches that would otherwise leak between tests
    // sharing this binary's HeadlessGl singleton. Safe to call repeatedly;
    // does NOT touch LLFontManager / LLImageGL / GL state — those stay
    // alive for the whole binary because LLFontGL's static fontp caches
    // (getFontSansSerif et al.) dangle across a re-init cycle (see
    // llfontgl_test.cpp:64-74). The shape LRU is the only piece we can
    // safely flush mid-binary, since its entries hold raw face* and would
    // re-populate on the next shape call regardless.
    inline void resetFontState()
    {
        LLFontShaping::clearCache();
    }

    // RAII scope — calls resetFontState() in dtor. Embed in a TUT fixture
    // struct to guarantee the shape cache is empty after every test,
    // regardless of how the test exited.
    struct FontStateScope
    {
        FontStateScope() = default;
        ~FontStateScope() { resetFontState(); }

        FontStateScope(const FontStateScope&) = delete;
        FontStateScope& operator=(const FontStateScope&) = delete;
    };

    // Owns the OSMesa context plus the GL-side singletons that the
    // llrender code paths assume are alive. Construction order is
    // window → LLImageGL → LLFontManager; teardown is the reverse.
    struct HeadlessGL : public LLWindowCallbacks
    {
        HeadlessGL(bool needs_vbos = true, bool needs_imagegl = true, bool needs_llrender = true)
        {
            const S32  WIDTH                 = 256;
            const S32  HEIGHT                = 256;
            const S32  WINDOW_ORIGIN_X       = 0;
            const S32  WINDOW_ORIGIN_Y       = 0;
            const U32  FLAGS                 = 32; // *TODO: Why did mapserver use this?  mFlags looks unused.
            const bool NO_FULLSCREEN         = false;
            const bool NO_CLEAR_BG           = false;
            const bool ENABLE_VSYNC          = false;
            const bool IGNORE_PIXEL_DEPTH    = false;
            const bool USE_GL                = true;
            mWindow = LLWindowManager::createWindow(this, "llrender-headless-test", "llrender-headless-test", WINDOW_ORIGIN_X, WINDOW_ORIGIN_Y,
                                                    WIDTH, HEIGHT, FLAGS, NO_FULLSCREEN, NO_CLEAR_BG,
                                                    ENABLE_VSYNC,           // gSavedSettings.getBOOL("DisableVerticalSync"),
                                                    USE_GL,                 // not headless
                                                    IGNORE_PIXEL_DEPTH);    // gIgnorePixelDepth = false

            if (nullptr == mWindow)
            {
                throw std::runtime_error("Failed to init window bailing");
            }

            if (needs_vbos)
            {
                LLVertexBuffer::initClass(mWindow);
            }

            if (needs_llrender)
            {
                gGL.init(needs_vbos);
            }

            if (needs_imagegl)
            {
                // num_catagories=1 mirrors the smallest sane init the viewer uses;
                // the test never tags textures so the count is moot beyond ≥1.
                // thread_texture_loads/thread_media_updates default to false so no
                // worker threads are spun up — keeps teardown simple.
                LLImageGL::initClass(mWindow, LLGLTexture::MAX_GL_IMAGE_CATEGORY, false, false, false);
            }

            LLFontManager::initClass();
        }

        ~HeadlessGL()
        {
            LLFontManager::cleanupClass();
            LLImageGL::cleanupClass();
            gGL.shutdown();
            LLVertexBuffer::cleanupClass();
            LLWindowManager::destroyWindow(mWindow);
        }

        void swapBuffer()
        {
            mWindow -> swapBuffers();
        }

        HeadlessGL(const HeadlessGL&)            = delete;
        HeadlessGL& operator=(const HeadlessGL&) = delete;

    private:
        LLWindowCallbacks    mCallbacks;
        LLWindow* mWindow;
    };
}

#endif // LL_LLHEADLESSGL_FIXTURE_H
