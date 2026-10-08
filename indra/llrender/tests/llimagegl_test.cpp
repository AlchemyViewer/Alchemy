/**
 * @file llimagegl_test.cpp
 * @brief LLImageGL tests that require a real GL context.
 *
 * A GL test: the shared fixture (llheadlessgl_fixture.h) provides
 * the GL context plus LLImageGL/LLFontManager init. Tests cover the
 * core texture lifecycle, the setSubImage bind-preservation
 * invariant, the deprecated-format resolution path, downscaling,
 * the published view of an upload in flight, and the bind state
 * edits rely on.
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
#include <vector>

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

        // Bind img's texture on unit 0 through the slot, so gGL's bind cache stays
        // truthful for what the test does next. The unbind is what makes unit 0 the
        // ACTIVE unit: a bind that finds the texture already cached does not.
        static void bindForRead(const LLImageGL* img)
        {
            gGL.getTextureSlot(0)->unbind();
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, img->getTexName());
        }

        // Read one level of img's texture straight from GL.
        static void readTexture(const LLImageGL* img, GLenum format, GLenum type, void* out, S32 level = 0)
        {
            bindForRead(img);
            glGetTexImage(GL_TEXTURE_2D, level, format, type, out);
        }

        // The texel at (x, y) of level 0 of an RGBA8 texture.
        static U32 readTexelRGBA(const LLImageGL* img, S32 x, S32 y)
        {
            const S32 w = img->getWidth();
            const S32 h = img->getHeight();
            std::vector<U8> px(static_cast<size_t>(w) * h * 4);
            readTexture(img, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            U32 texel = 0;
            std::memcpy(&texel, px.data() + (static_cast<size_t>(y) * w + x) * 4, 4);
            return texel;
        }
    };

    typedef test_group<llimagegl_data> llimagegl_test;
    typedef llimagegl_test::object     llimagegl_object;
    tut::llimagegl_test llimagegl_testcase("LLImageGL");

    // createGLTexture(LLImageRaw) allocates immutable storage under a
    // fresh GL name. After it succeeds the instance reports
    // getHasGLTexture() and a non-zero texname.
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

    // A packed pixel type is one value a pixel, whatever the format's component
    // count. GStreamer hands its frames over as GL_BGRA /
    // GL_UNSIGNED_INT_8_8_8_8_REV, and the upload that slices by scanline -- on the
    // main thread on Windows off Intel, on Linux with NVIDIA, on macOS with AMD --
    // stepped through rows by component count times a per-type width that had no
    // entry for it, and died. Where the slicing is off this passes either way.
    template<> template<>
    void llimagegl_object::test<10>()
    {
        // 64 rows, a multiple of 32: the full upload takes the batched slices.
        constexpr U16 W = 64, H = 64;
        LLPointer<LLImageRaw> src = new LLImageRaw(W, H, 4);
        U8* sd = src->getData();
        for (size_t i = 0; i < (size_t)W * H * 4; ++i)
            sd[i] = (U8)(i & 0xFF);

        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        img->setExplicitFormat(GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV);
        ensure("createGLTexture succeeded", img->createGLTexture(0, src.get()));

        std::vector<U8> got((size_t)W * H * 4);
        readTexture(img, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, got.data());
        ensure_equals("full upload reads back",
                      std::memcmp(got.data(), sd, got.size()), 0);

        // A partial-width update, which slices one row at a time.
        LLPointer<LLImageRaw> patch = new LLImageRaw(W, H, 4);
        U8* pd = patch->getData();
        for (size_t i = 0; i < (size_t)W * H * 4; ++i)
            pd[i] = (U8)(0xFF - (i & 0xFF));
        ensure("setSubImage succeeded",
               img->setSubImage(patch.get(), /*x_pos=*/8, /*y_pos=*/4,
                                /*width=*/16, /*height=*/8,
                                /*force_fast_update=*/true));

        readTexture(img, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, got.data());
        bool match = true;
        for (U32 y = 0; y < H && match; ++y)
        {
            for (U32 x = 0; x < W && match; ++x)
            {
                const bool in_patch = x >= 8 && x < 24 && y >= 4 && y < 12;
                const size_t at = ((size_t)y * W + x) * 4;
                match = std::memcmp(got.data() + at, (in_patch ? pd : sd) + at, 4) == 0;
            }
        }
        ensure("the update lands where it was asked and nowhere else", match);
    }

    // scaleDown builds a new texture object, and texture-object state does not
    // come with it: a deprecated-format texture lost its swizzle, so luminance read
    // back red and luminance-alpha opaque. The FBO method -- the default -- could
    // not have kept the channels either, since it samples THROUGH the swizzle and
    // copies back by position, so these take the PBO copy whatever the setting.
    template<> template<>
    void llimagegl_object::test<11>()
    {
        constexpr U16 W = 16, H = 16;
        LLPointer<LLImageRaw> raw = new LLImageRaw(W, H, 2);
        U8* d = raw->getData();
        for (size_t i = 0; i < (size_t)W * H; ++i)
        {
            d[i * 2]     = 0x40; // luminance
            d[i * 2 + 1] = 0xC0; // alpha
        }

        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/true);
        ensure("createGLTexture succeeded", img->createGLTexture(0, raw.get()));

        const U32 method = gGLManager.mDownScaleMethod;
        gGLManager.mDownScaleMethod = 0;
        const bool scaled = img->scaleDown(1);
        gGLManager.mDownScaleMethod = method;
        ensure("scaleDown succeeded", scaled);
        ensure_equals("discard level follows", img->getDiscardLevel(), 1);

        bindForRead(img);
        GLint w = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        ensure_equals("the new texture is half size", (S32)w, (S32)W / 2);

        GLint mask[4] = { 0, 0, 0, 0 };
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, mask);
        ensure_equals("R swizzle == GL_RED",   (S32)mask[0], (S32)GL_RED);
        ensure_equals("G swizzle == GL_RED",   (S32)mask[1], (S32)GL_RED);
        ensure_equals("B swizzle == GL_RED",   (S32)mask[2], (S32)GL_RED);
        ensure_equals("A swizzle == GL_GREEN", (S32)mask[3], (S32)GL_GREEN);

        std::vector<U8> got((size_t)(W / 2) * (H / 2) * 2);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RG, GL_UNSIGNED_BYTE, got.data());
        bool match = true;
        for (size_t i = 0; i < got.size() && match; i += 2)
        {
            match = got[i] == 0x40 && got[i + 1] == 0xC0;
        }
        ensure("luminance and alpha both survive the copy", match);
    }

    // scaleDown waits out an upload in flight: the LLImageGL thread is writing the
    // discard level and storage state it reads and replaces, and that upload's
    // publish would then install its texture under the discard level set here.
    template<> template<>
    void llimagegl_object::test<12>()
    {
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/true);
        ensure("createGLTexture succeeded",
               img->createGLTexture(0, makeRaw(16, 16, 4, 0x80).get()));
        const U32 name = img->getTexName();

        // The PBO copy needs no framebuffer of its own.
        const U32 method = gGLManager.mDownScaleMethod;
        gGLManager.mDownScaleMethod = 1;

        img->beginUpload();
        const bool during = img->scaleDown(1);
        const U32 name_during = img->getTexName();
        img->endUpload();
        const bool after = img->scaleDown(1);

        gGLManager.mDownScaleMethod = method;

        ensure("refused while an upload is in flight", !during);
        ensure_equals("texture untouched meanwhile", (S32)name_during, (S32)name);
        ensure("done once the upload has ended", after);
    }

    // While an upload is in flight the members describe the texture being built.
    // readBackRaw paired the getters' published size with the members' discard
    // level, so it read the old texture's level 0 at the new texture's size.
    // Asset 64x64, on screen at discard 2 (a 16x16 texture), the upload in flight
    // building discard 0.
    template<> template<>
    void llimagegl_object::test<13>()
    {
        constexpr U16 W = 16, H = 16;
        LLPointer<LLImageRaw> src = new LLImageRaw(W, H, 4);
        U8* sd = src->getData();
        for (size_t i = 0; i < (size_t)W * H * 4; ++i)
            sd[i] = (U8)(i & 0xFF);

        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        ensure("createGLTexture succeeded", img->createGLTexture(2, src.get()));
        ensure_equals("on screen at 16 wide", img->getWidth(), 16);

        img->beginUpload();
        img->setDiscardLevel(0); // what the worker's createGLTexture writes first

        LLPointer<LLImageRaw> dst = new LLImageRaw();
        const bool read = img->readBackRaw(-1, dst.get(), /*compressed_ok=*/false);

        img->setDiscardLevel(2);
        img->endUpload();

        ensure("readBackRaw succeeded", read);
        ensure_equals("read at the size on screen", (S32)dst->getWidth(), (S32)W);
        ensure_equals("and reads what is on screen",
                      std::memcmp(dst->getData(), sd, (size_t)W * H * 4), 0);
    }

    // The pick mask of a texture still being uploaded waits for that texture:
    // getMask answers for the one on screen, and off the main thread building the
    // mask in place freed the buffer getMask was reading. It publishes with the
    // texture, and an upload that never publishes leaves it alone.
    template<> template<>
    void llimagegl_object::test<14>()
    {
        constexpr U16 W = 16, H = 16;
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        ensure("createGLTexture succeeded",
               img->createGLTexture(0, makeRaw(W, H, 4, 0xFF).get()));
        const LLVector2 centre(0.5f, 0.5f);
        ensure("opaque texture picks", img->getMask(centre));

        LLPointer<LLImageRaw> clear = makeRaw(W, H, 4, 0x00);

        img->beginUpload();
        img->updatePickMask(W, H, clear->getData());
        ensure("the mask on screen answers while the upload is in flight",
               img->getMask(centre));
        img->endUpload();
        ensure("an upload that never published leaves the mask alone",
               img->getMask(centre));

        img->beginUpload();
        img->updatePickMask(W, H, clear->getData());
        img->syncTexName(img->getTexName());
        ensure("the new mask publishes with its texture", !img->getMask(centre));
    }

    // An edit writes through the ACTIVE unit, and a bind that finds the texture
    // already cached on slot 0 skips activating it. With another unit left active
    // the write went to whatever that unit held. The glyph atlas does exactly this:
    // still on slot 0 from the text being drawn when a new glyph arrives.
    template<> template<>
    void llimagegl_object::test<15>()
    {
        constexpr U16 W = 32, H = 32;
        LLPointer<LLImageGL> a = new LLImageGL(/*usemipmaps=*/false);
        LLPointer<LLImageGL> b = new LLImageGL(/*usemipmaps=*/false);
        ensure("create a", a->createGLTexture(0, makeRaw(W, H, 4, 0x10).get()));
        ensure("create b", b->createGLTexture(0, makeRaw(W, H, 4, 0x20).get()));

        // Partial update, as the glyph atlas does it. Forcing the slot 1 bind is what
        // leaves unit 1 active, cached or not.
        gGL.getTextureSlot(0)->bind(a.get());
        gGL.getTextureSlot(1)->bind(b.get(), false, /*forceBind=*/true);
        ensure("setSubImage succeeded",
               a->setSubImage(makeRaw(W, H, 4, 0xFF).get(), 4, 4, 8, 8,
                              /*force_fast_update=*/true));
        ensure_equals("the update reached a", readTexelRGBA(a, 5, 5), 0xFFFFFFFFu);
        ensure_equals("and not b", readTexelRGBA(b, 5, 5), 0x20202020u);

        // Re-upload in place, through setImage.
        gGL.getTextureSlot(0)->bind(a.get());
        gGL.getTextureSlot(1)->bind(b.get(), false, /*forceBind=*/true);
        ensure("re-upload succeeded", a->createGLTexture(0, makeRaw(W, H, 4, 0x77).get()));
        ensure_equals("the re-upload reached a", readTexelRGBA(a, 5, 5), 0x77777777u);
        ensure_equals("and not b", readTexelRGBA(b, 5, 5), 0x20202020u);
    }

    // A re-upload at the same discard level wrote into the live texture in place,
    // on the word of the discard level alone. Immutable storage cannot follow a new
    // size or format: the write was rejected, or dropped the channel that did not
    // fit, and the image went on describing a texture GL never had.
    template<> template<>
    void llimagegl_object::test<16>()
    {
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        ensure("create 32x32", img->createGLTexture(0, makeRaw(32, 32, 4, 0x11).get()));
        ensure("re-create 64x64", img->createGLTexture(0, makeRaw(64, 64, 4, 0x22).get()));

        bindForRead(img);
        GLint w = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        ensure_equals("the texture took the new size", (S32)w, 64);
        ensure_equals("and the new pixels", readTexelRGBA(img, 40, 40), 0x22222222u);

        // Same size, an alpha channel the old storage has no room for.
        LLPointer<LLImageGL> rgb = new LLImageGL(/*usemipmaps=*/false);
        ensure("create RGB", rgb->createGLTexture(0, makeRaw(16, 16, 3, 0x33).get()));
        ensure("re-create RGBA", rgb->createGLTexture(0, makeRaw(16, 16, 4, 0x44).get()));

        bindForRead(rgb);
        GLint format = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        ensure_equals("the texture took the new format", (S32)format, (S32)GL_SRGB8_ALPHA8);
        ensure_equals("and the new pixels", readTexelRGBA(rgb, 3, 3), 0x44444444u);
    }

    // A deleted texture's name may come back out of glGenTextures, and a slot whose
    // bind cache still held it would take the new texture for bound already and skip
    // the bind -- sampling texture 0 in its place, since deleting the old one put the
    // binding back to 0. The cache forgets the name when the texture goes.
    template<> template<>
    void llimagegl_object::test<17>()
    {
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        ensure("createGLTexture succeeded",
               img->createGLTexture(0, makeRaw(16, 16, 4, 0x80).get()));
        const U32 name = img->getTexName();

        gGL.getTextureSlot(3)->bind(img.get());
        ensure_equals("slot 3 caches it", gGL.getTextureSlot(3)->getCurrTexture(), name);

        img->destroyGLTexture();
        // However many frames the delete is held back for.
        for (S32 frame = 0; frame < 8; ++frame)
        {
            LLImageGL::updateClass();
        }

        ensure_equals("the cache let go of the deleted name",
                      gGL.getTextureSlot(3)->getCurrTexture(), 0u);
    }

    // Generated mips fill the whole pyramid the storage holds, and sampling can
    // reach it. MAX_LEVEL stayed at the discard levels -- five below 256x256 -- so
    // glGenerateMipmap stopped there and distant surfaces minified no further.
    template<> template<>
    void llimagegl_object::test<18>()
    {
        constexpr U16 W = 256, H = 256;
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/true);
        ensure("createGLTexture succeeded",
               img->createGLTexture(0, makeRaw(W, H, 4, 0x80).get()));

        bindForRead(img);
        GLint max_level = 0;
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &max_level);
        const S32 last = LLImageGL::calcMipLevelCount(W, H) - 1;
        ensure_equals("sampling reaches the 1x1 level", (S32)max_level, last);

        U8 px[4] = { 0, 0, 0, 0 };
        glGetTexImage(GL_TEXTURE_2D, last, GL_RGBA, GL_UNSIGNED_BYTE, px);
        ensure("and the 1x1 level was generated",
               px[0] == 0x80 && px[1] == 0x80 && px[2] == 0x80 && px[3] == 0x80);
    }

    // gGL is thread_local and outlives a context, as it does across these tests,
    // and init left the slots' bind caches as the last context had them. A fresh
    // context hands out the same first names, so its first texture read as bound
    // already on a slot that had held its namesake, and the bind was skipped.
    template<> template<>
    void llimagegl_object::test<19>()
    {
        {
            LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
            ensure("createGLTexture succeeded",
                   img->createGLTexture(0, makeRaw(16, 16, 4, 0x80).get()));
            gGL.getTextureSlot(2)->bind(img.get());
        }

        // A new context on the same thread.
        gl.reset();
        gl = std::make_unique<ll_test::HeadlessGL>();

        ensure_equals("unit 2's cache starts empty",
                      gGL.getTextureSlot(2)->getCurrTexture(), 0u);
        ensure_equals("and unit 0 is the active one",
                      gGL.getCurrentTexUnitIndex(), 0u);

        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        ensure("createGLTexture succeeded in the new context",
               img->createGLTexture(0, makeRaw(16, 16, 4, 0x40).get()));
        gGL.getTextureSlot(2)->bind(img.get());
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        ensure_equals("the bind reached GL", (U32)bound, img->getTexName());
    }
}
