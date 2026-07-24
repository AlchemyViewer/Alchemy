/**
 * @file llimagegl_test.cpp
 * @brief LLImageGL tests that require a real GL context.
 *
 * Built only when BUILD_HEADLESS=ON (LL_MESA_HEADLESS=1). The
 * shared OSMesa fixture (llheadlessgl_fixture.h) provides the
 * GL context plus LLImageGL/LLFontManager init. Tests cover the
 * core texture lifecycle, the setSubImage bind-preservation
 * invariant, and the deprecated-format resolution path.
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llimagegl.h"
#include "../llgl.h"
#include "../llrender.h"
#include "../llglheaders.h"

#include "llheadlessgl_fixture.h"

#include "llimage.h"

#include "../test/lltut.h"

#include <cstring>

namespace tut
{
    struct llimagegl_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();

        // Fill an LLImageRaw with a single-byte pattern across all
        // components. Cheap and deterministic — tests that read pixels
        // back compare against the same fill.
        static LLPointer<LLImageRaw> makeRaw(U16 w, U16 h, S8 components, U8 fill)
        {
            LLPointer<LLImageRaw> raw = new LLImageRaw(w, h, components);
            std::memset(raw->getData(), fill,
                        static_cast<size_t>(w) * h * components);
            return raw;
        }
    };

    typedef test_group<llimagegl_data> llimagegl_test;
    typedef llimagegl_test::object     llimagegl_object;
    tut::llimagegl_test llimagegl_testcase("LLImageGL");

    // createGLTexture(LLImageRaw) goes through setManualImage →
    // glTexImage2D under a fresh GL name. After it succeeds the
    // instance reports getHasGLTexture() and a non-zero texname.
    template<> template<>
    void llimagegl_object::test<1>()
    {
        LLPointer<LLImageRaw> raw = makeRaw(16, 16, 4, /*fill=*/0x80);
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);

        ensure("createGLTexture succeeded",
               img->createGLTexture(/*discard_level=*/0, raw.get()));
        ensure("texname allocated",
               img->getTexName() != 0);
        ensure("getHasGLTexture reports true",
               img->getHasGLTexture());
    }

    // destroyGLTexture must drop the GL name. Subsequent reads see
    // mTexName == 0 and getHasGLTexture() == false; the LLImageGL
    // instance itself remains usable (could be re-uploaded).
    template<> template<>
    void llimagegl_object::test<2>()
    {
        LLPointer<LLImageRaw> raw = makeRaw(16, 16, 4, /*fill=*/0x80);
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        ensure("createGLTexture succeeded",
               img->createGLTexture(0, raw.get()));
        ensure("texname allocated before destroy",
               img->getTexName() != 0);

        img->destroyGLTexture();

        ensure_equals("texname cleared after destroy",
                      (S32)img->getTexName(), 0);
        ensure("getHasGLTexture false after destroy",
               !img->getHasGLTexture());
    }

    // Regression for 8e85c68d69 — the partial-rect setSubImage path
    // used to disable() unit 0 after the upload, leaving the next
    // batch flush bound to nothing (one-frame glyph flicker on cache
    // miss). Verify the GL texture binding survives a setSubImage that
    // skips unbind.
    template<> template<>
    void llimagegl_object::test<3>()
    {
        LLPointer<LLImageRaw> raw = makeRaw(32, 32, 4, /*fill=*/0x80);
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        ensure("createGLTexture succeeded",
               img->createGLTexture(0, raw.get()));

        // Bind the new texture to unit 0 the same way the renderer
        // would, then snapshot what GL_TEXTURE_BINDING_2D points at.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, img->getTexName());

        GLint bound_before = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_before);
        ensure_equals("texture is bound to unit 0 before setSubImage",
                      (S32)bound_before, (S32)img->getTexName());

        // Partial-rect upload — this is the path that used to
        // disable() the unit on its way out. The source raw must
        // span the (x_pos, y_pos, +width, +height) region: setSubImage
        // uses x_pos/y_pos as BOTH the source offset and the dest
        // offset, so a 32x32 source matching the destination is
        // simplest.
        LLPointer<LLImageRaw> patch = makeRaw(32, 32, 4, /*fill=*/0xFF);
        ensure("setSubImage succeeded",
               img->setSubImage(patch.get(),
                                /*x_pos=*/4, /*y_pos=*/4,
                                /*width=*/8, /*height=*/8,
                                /*force_fast_update=*/true, 0, true));

        glActiveTexture(GL_TEXTURE0);
        GLint bound_after = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_after);
        ensure_equals("texture still bound to unit 0 after setSubImage",
                      (S32)bound_after, (S32)img->getTexName());
    }

    // Regression for 444c29b161 — setExplicitFormat used to leave
    // mFormatPrimary holding deprecated names (GL_LUMINANCE_ALPHA,
    // GL_LUMINANCE, GL_ALPHA) which then leaked into setSubImage /
    // readBackRaw and tripped GL_INVALID_ENUM in core. Now
    // resolveDeprecatedFormat rewrites them at format-set time.
    template<> template<>
    void llimagegl_object::test<4>()
    {
        if (gGLManager.mGLVersion < 3.29f)
        {
            skip("resolveDeprecatedFormat is gated on GL >= 3.3");
            return;
        }

        LLPointer<LLImageGL> la = new LLImageGL(/*usemipmaps=*/false);
        la->setExplicitFormat(GL_LUMINANCE_ALPHA, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE);
        ensure_equals("LUMINANCE_ALPHA primary rewrites to GL_RG",
                      (S32)la->getPrimaryFormat(), (S32)GL_RG);

        LLPointer<LLImageGL> lum = new LLImageGL(/*usemipmaps=*/false);
        lum->setExplicitFormat(GL_LUMINANCE, GL_LUMINANCE, GL_UNSIGNED_BYTE);
        ensure_equals("LUMINANCE primary rewrites to GL_RED",
                      (S32)lum->getPrimaryFormat(), (S32)GL_RED);

        LLPointer<LLImageGL> alpha = new LLImageGL(/*usemipmaps=*/false);
        alpha->setExplicitFormat(GL_ALPHA, GL_ALPHA, GL_UNSIGNED_BYTE);
        ensure_equals("ALPHA primary rewrites to GL_RED",
                      (S32)alpha->getPrimaryFormat(), (S32)GL_RED);

        // Modern formats shouldn't be touched.
        LLPointer<LLImageGL> rgba = new LLImageGL(/*usemipmaps=*/false);
        rgba->setExplicitFormat(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        ensure_equals("RGBA primary preserved",
                      (S32)rgba->getPrimaryFormat(), (S32)GL_RGBA);
    }

    // Pure-CPU sanity: dataFormatBits/Bytes/Components match the
    // documented sizes and the byte-count math rounds up to a 4-byte
    // multiple. These are static helpers; no GL state involved, but
    // they live here so the table stays close to the formats the
    // upload paths exercise.
    template<> template<>
    void llimagegl_object::test<5>()
    {
        ensure_equals("RGBA8 -> 32 bits",
                      LLImageGL::dataFormatBits(GL_RGBA8), 32);
        ensure_equals("RG8 -> 16 bits",
                      LLImageGL::dataFormatBits(GL_RG8), 16);
        ensure_equals("R8 -> 8 bits",
                      LLImageGL::dataFormatBits(GL_R8), 8);
        ensure_equals("RGBA16F -> 64 bits",
                      LLImageGL::dataFormatBits(GL_RGBA16F), 64);

        ensure_equals("RGBA components == 4",
                      LLImageGL::dataFormatComponents(GL_RGBA), 4);
        ensure_equals("LUMINANCE_ALPHA components == 2",
                      LLImageGL::dataFormatComponents(GL_LUMINANCE_ALPHA), 2);
        ensure_equals("RED components == 1",
                      LLImageGL::dataFormatComponents(GL_RED), 1);

        // 16x16 RGBA8 = 16*16*4 = 1024 bytes, already 4-aligned.
        ensure_equals("16x16 RGBA8 = 1024 bytes",
                      (S32)LLImageGL::dataFormatBytes(GL_RGBA8, 16, 16), 1024);
        // 5x3 R8 = 15 bytes; rounds up to 16 (4-aligned).
        ensure_equals("5x3 R8 byte count rounds to 4-aligned 16",
                      (S32)LLImageGL::dataFormatBytes(GL_R8, 5, 3), 16);

        // Host vs. VRAM split: dataFormatBits returns the tight host
        // layout (used for CPU upload-buffer math); dataFormatVRAMBits
        // returns the padded driver allocation (used for VRAM stats).
        // For formats drivers don't pad, the two agree.
        ensure_equals("RGB8 host -> 24 bits (tight)",
                      LLImageGL::dataFormatBits(GL_RGB8), 24);
        ensure_equals("RGB8 VRAM -> 32 bits (padded to RGBX)",
                      LLImageGL::dataFormatVRAMBits(GL_RGB8), 32);
        ensure_equals("RGB16F host -> 48 bits (tight)",
                      LLImageGL::dataFormatBits(GL_RGB16F), 48);
        ensure_equals("RGB16F VRAM -> 64 bits (padded to RGBA16F)",
                      LLImageGL::dataFormatVRAMBits(GL_RGB16F), 64);
        ensure_equals("RGB32F host -> 96 bits (tight)",
                      LLImageGL::dataFormatBits(GL_RGB32F), 96);
        ensure_equals("RGB32F VRAM -> 128 bits (padded to RGBA32F)",
                      LLImageGL::dataFormatVRAMBits(GL_RGB32F), 128);
        ensure_equals("DEPTH_COMPONENT24 host -> 24 bits (tight)",
                      LLImageGL::dataFormatBits(GL_DEPTH_COMPONENT24), 24);
        ensure_equals("DEPTH_COMPONENT24 VRAM -> 32 bits (padded)",
                      LLImageGL::dataFormatVRAMBits(GL_DEPTH_COMPONENT24), 32);

        // VRAM bytes path applies the same 4-byte alignment as the
        // host bytes path and routes unpadded formats through the
        // host table — so RGBA8 matches between the two.
        ensure_equals("16x16 RGBA8 VRAM = 1024 bytes (matches host)",
                      (S32)LLImageGL::dataFormatVRAMBytes(GL_RGBA8, 16, 16), 1024);
        // 16x16 RGB8: host 16*16*3 = 768; VRAM 16*16*4 = 1024.
        ensure_equals("16x16 RGB8 host = 768 bytes",
                      (S32)LLImageGL::dataFormatBytes(GL_RGB8, 16, 16), 768);
        ensure_equals("16x16 RGB8 VRAM = 1024 bytes (padded)",
                      (S32)LLImageGL::dataFormatVRAMBytes(GL_RGB8, 16, 16), 1024);
    }

    // When no explicit format is set, createGLTexture derives one from
    // the raw's component count. With deprecated rewrites enabled this
    // means: 1 -> GL_RED, 2 -> GL_RG, 3 -> GL_RGB, 4 -> GL_RGBA.
    template<> template<>
    void llimagegl_object::test<6>()
    {
        if (gGLManager.mGLVersion < 3.29f)
        {
            skip("resolveDeprecatedFormat is gated on GL >= 3.3");
            return;
        }

        // 2-component raw — exercises the LUMINANCE_ALPHA -> GL_RG
        // rewrite path. This is the format the greyscale font atlas
        // uses (see commit c99dcaa7d3 — "Restore greyscale font atlas
        // to 2 component").
        LLPointer<LLImageRaw> raw2 = makeRaw(8, 8, 2, /*fill=*/0x80);
        LLPointer<LLImageGL> img2 = new LLImageGL(/*usemipmaps=*/false);
        ensure("createGLTexture 2-component succeeded",
               img2->createGLTexture(0, raw2.get()));
        ensure_equals("2 components -> GL_RG primary",
                      (S32)img2->getPrimaryFormat(), (S32)GL_RG);

        LLPointer<LLImageRaw> raw4 = makeRaw(8, 8, 4, /*fill=*/0x80);
        LLPointer<LLImageGL> img4 = new LLImageGL(/*usemipmaps=*/false);
        ensure("createGLTexture 4-component succeeded",
               img4->createGLTexture(0, raw4.get()));
        ensure_equals("4 components -> GL_RGBA primary",
                      (S32)img4->getPrimaryFormat(), (S32)GL_RGBA);
    }

    // usemipmaps=true must wire the auto-mipmap path through to GL —
    // verify by reading the bound texture's mip 1 dimensions back. Mip
    // 1 of an 8x8 base is 4x4; if mipmaps weren't generated the level
    // would have width 0.
    template<> template<>
    void llimagegl_object::test<7>()
    {
        LLPointer<LLImageRaw> raw = makeRaw(8, 8, 4, /*fill=*/0x80);
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/true);
        ensure("createGLTexture mipmapped succeeded",
               img->createGLTexture(0, raw.get()));
        ensure("getUseMipMaps reports true",
               img->getUseMipMaps());

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, img->getTexName());

        GLint mip1_w = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &mip1_w);
        ensure_equals("mip 1 width == 4 for an 8x8 base", (S32)mip1_w, 4);
    }

    // Regression for 2d4e2a60ea — GL_TEXTURE_SWIZZLE_RGBA must be set
    // by createGLTexture for a deprecated-format texture so the shader
    // sees (R, R, R, A) for a 2-component LUMINANCE_ALPHA upload even
    // though the texture is now stored as GL_RG. Verifies the swizzle
    // mask matches applySwizzleForDeprecatedFormat's table.
    template<> template<>
    void llimagegl_object::test<8>()
    {
        if (gGLManager.mGLVersion < 3.29f)
        {
            skip("swizzle apply is gated on GL >= 3.3");
            return;
        }

        // 2-component raw goes through LUMINANCE_ALPHA -> GL_RG
        // rewrite, which sets mDeprecatedSourceFormat=LUMINANCE_ALPHA.
        // createGLTexture then applies the matching swizzle.
        LLPointer<LLImageRaw> raw = makeRaw(8, 8, 2, /*fill=*/0x80);
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        ensure("createGLTexture 2-component succeeded",
               img->createGLTexture(0, raw.get()));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, img->getTexName());

        GLint mask[4] = { 0, 0, 0, 0 };
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, mask);

        ensure_equals("R swizzle == GL_RED",   (S32)mask[0], (S32)GL_RED);
        ensure_equals("G swizzle == GL_RED",   (S32)mask[1], (S32)GL_RED);
        ensure_equals("B swizzle == GL_RED",   (S32)mask[2], (S32)GL_RED);
        ensure_equals("A swizzle == GL_GREEN", (S32)mask[3], (S32)GL_GREEN);
    }

    // Round-trip: upload a known pattern, glGetTexImage it back, and
    // verify byte-for-byte. Covers the createGLTexture upload path and
    // readBackRaw together. RGBA chosen so no deprecated rewrite kicks
    // in — that path is covered separately by test<4> and test<6>.
    template<> template<>
    void llimagegl_object::test<9>()
    {
        constexpr U16 W = 8, H = 8;
        constexpr S8  C = 4;
        LLPointer<LLImageRaw> src = new LLImageRaw(W, H, C);
        U8* sd = src->getData();
        // Per-pixel pattern: each byte is its linear index (mod 256).
        // Distinguishes any row/col/component swap on readback.
        for (size_t i = 0; i < (size_t)W * H * C; ++i)
            sd[i] = (U8)(i & 0xFF);

        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        ensure("createGLTexture succeeded",
               img->createGLTexture(0, src.get()));

        LLPointer<LLImageRaw> dst = new LLImageRaw(W, H, C);
        std::memset(dst->getData(), 0, (size_t)W * H * C);
        ensure("readBackRaw succeeded",
               img->readBackRaw(0, dst.get(), /*compressed_ok=*/false));

        ensure_equals("readback bytes match upload",
                      std::memcmp(src->getData(), dst->getData(),
                                  (size_t)W * H * C),
                      0);
    }
}
