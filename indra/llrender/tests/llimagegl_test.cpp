/**
 * @file llimagegl_test.cpp
 * @brief LLImageGL tests that require a real GL context.
 *
 * A GL test: the shared fixture (llheadlessgl_fixture.h) provides
 * the GL context plus LLImageGL/LLFontManager init. Tests cover the
 * core texture lifecycle, the setSubImage bind-preservation
 * invariant, the deprecated-format resolution path, downscaling,
 * the published view of an upload in flight, the bind state
 * edits rely on, and the integer formats.
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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
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

        // The texel at (x, y) of level 0 of an RGBA8 texture, which must lie in the level. The level is read at the
        // size GL holds it at, which need not be the size the image reports. Called inside a check, so it asserts
        // only on failure: an assertion that passed would take the check's UNSCOPED_INFO.
        static U32 readTexelRGBA(const LLImageGL* img, S32 x, S32 y)
        {
            bindForRead(img);
            GLint w = 0;
            GLint h = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
            if (x >= w || y >= h)
            {
                FAIL("texel (" << x << ", " << y << ") is outside the " << w << "x" << h << " level");
            }
            std::vector<U8> px(static_cast<size_t>(w) * h * 4);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            U32 texel = 0;
            std::memcpy(&texel, px.data() + (static_cast<size_t>(y) * w + x) * 4, 4);
            return texel;
        }
    };


    // createGLTexture(LLImageRaw) allocates immutable storage under a
    // fresh GL name. After it succeeds the instance reports
    // getHasGLTexture() and a non-zero texname.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: createGLTexture(LLImageRaw) makes a texture under a fresh GL name", "[LLImageGL]")
    {
        LLPointer<LLImageRaw> raw = makeRaw(16, 16, 4, /*fill=*/0x80);
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);

        CHECK(img->createGLTexture(/*discard_level=*/0, raw.get()));
        CHECK(img->getTexName() != 0);
        CHECK(img->getHasGLTexture());
    }

    // destroyGLTexture must drop the GL name. Subsequent reads see
    // mTexName == 0 and getHasGLTexture() == false; the LLImageGL
    // instance itself remains usable: it takes a new upload.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: destroyGLTexture drops the GL name and the image takes a new upload", "[LLImageGL]")
    {
        LLPointer<LLImageRaw> raw = makeRaw(16, 16, 4, /*fill=*/0x80);
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        CHECK(img->createGLTexture(0, raw.get()));
        CHECK(img->getTexName() != 0);

        img->destroyGLTexture();

        CHECK((S32)img->getTexName() == 0);
        CHECK_FALSE(img->getHasGLTexture());

        // Uploaded again, it holds a texture again.
        CHECK(img->createGLTexture(0, raw.get()));
        UNSCOPED_INFO("re-uploaded");
        CHECK(img->getHasGLTexture());
    }

    // Regression for 8e85c68d69 — the partial-rect setSubImage path
    // used to disable() unit 0 after the upload, leaving the next
    // batch flush bound to nothing (one-frame glyph flicker on cache
    // miss). Verify the GL texture binding survives a setSubImage that
    // skips unbind.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a partial setSubImage leaves the texture bound to unit 0", "[LLImageGL]")
    {
        LLPointer<LLImageRaw> raw = makeRaw(32, 32, 4, /*fill=*/0x80);
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(img->createGLTexture(0, raw.get()));

        // Bind the new texture to unit 0 the same way the renderer
        // would, through its slot, then snapshot what
        // GL_TEXTURE_BINDING_2D points at. createGLTexture ends by
        // unbinding the slot, so the texture is there only if the
        // slot's bind reached GL.
        gGL.getTextureSlot(0)->bind(img.get());
        glActiveTexture(GL_TEXTURE0);

        GLint bound_before = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_before);
        CHECK((S32)bound_before == (S32)img->getTexName());

        // Partial-rect upload — this is the path that used to
        // disable() the unit on its way out. The source raw must
        // span the (x_pos, y_pos, +width, +height) region: setSubImage
        // uses x_pos/y_pos as BOTH the source offset and the dest
        // offset, so a 32x32 source matching the destination is
        // simplest.
        LLPointer<LLImageRaw> patch = makeRaw(32, 32, 4, /*fill=*/0xFF);
        CHECK(img->setSubImage(patch.get(),
                                /*x_pos=*/4, /*y_pos=*/4,
                                /*width=*/8, /*height=*/8,
                                /*force_fast_update=*/true, 0, true));

        glActiveTexture(GL_TEXTURE0);
        GLint bound_after = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_after);
        CHECK((S32)bound_after == (S32)img->getTexName());
    }

    // Regression for 444c29b161 — setExplicitFormat used to leave
    // mFormatPrimary holding deprecated names (GL_LUMINANCE_ALPHA,
    // GL_LUMINANCE, GL_ALPHA) which then leaked into setSubImage /
    // readBackRaw and tripped GL_INVALID_ENUM in core. Now
    // resolveDeprecatedFormat rewrites them at format-set time.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: setExplicitFormat rewrites deprecated formats and keeps modern ones", "[LLImageGL]")
    {
        LLPointer<LLImageGL> la = new LLImageGL(/*usemipmaps=*/false);
        la->setExplicitFormat(GL_LUMINANCE_ALPHA, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE);
        CHECK((S32)la->getPrimaryFormat() == (S32)GL_RG);

        LLPointer<LLImageGL> lum = new LLImageGL(/*usemipmaps=*/false);
        lum->setExplicitFormat(GL_LUMINANCE, GL_LUMINANCE, GL_UNSIGNED_BYTE);
        CHECK((S32)lum->getPrimaryFormat() == (S32)GL_RED);

        LLPointer<LLImageGL> alpha = new LLImageGL(/*usemipmaps=*/false);
        alpha->setExplicitFormat(GL_ALPHA, GL_ALPHA, GL_UNSIGNED_BYTE);
        CHECK((S32)alpha->getPrimaryFormat() == (S32)GL_RED);

        // Modern formats shouldn't be touched.
        LLPointer<LLImageGL> rgba = new LLImageGL(/*usemipmaps=*/false);
        rgba->setExplicitFormat(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        CHECK((S32)rgba->getPrimaryFormat() == (S32)GL_RGBA);
    }

    // Pure-CPU sanity: dataFormatBits/Bytes/Components match the
    // documented sizes and the byte-count math rounds up to a 4-byte
    // multiple. These are static helpers; no GL state involved, but
    // they live here so the table stays close to the formats the
    // upload paths exercise.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: dataFormatBits/Bytes/Components give the documented host and VRAM sizes", "[LLImageGL]")
    {
        CHECK(LLImageGL::dataFormatBits(GL_RGBA8) == 32);
        CHECK(LLImageGL::dataFormatBits(GL_RG8) == 16);
        CHECK(LLImageGL::dataFormatBits(GL_R8) == 8);
        CHECK(LLImageGL::dataFormatBits(GL_RGBA16F) == 64);

        CHECK(LLImageGL::dataFormatComponents(GL_RGBA) == 4);
        CHECK(LLImageGL::dataFormatComponents(GL_LUMINANCE_ALPHA) == 2);
        CHECK(LLImageGL::dataFormatComponents(GL_RED) == 1);

        // 16x16 RGBA8 = 16*16*4 = 1024 bytes, already 4-aligned.
        CHECK((S32)LLImageGL::dataFormatBytes(GL_RGBA8, 16, 16) == 1024);
        // 5x3 R8 = 15 bytes; rounds up to 16 (4-aligned).
        CHECK((S32)LLImageGL::dataFormatBytes(GL_R8, 5, 3) == 16);

        // Host vs. VRAM split: dataFormatBits returns the tight host
        // layout (used for CPU upload-buffer math); dataFormatVRAMBits
        // returns the padded driver allocation (used for VRAM stats).
        // For formats drivers don't pad, the two agree.
        CHECK(LLImageGL::dataFormatBits(GL_RGB8) == 24);
        UNSCOPED_INFO("padded to RGBX");
        CHECK(LLImageGL::dataFormatVRAMBits(GL_RGB8) == 32);
        CHECK(LLImageGL::dataFormatBits(GL_RGB16F) == 48);
        UNSCOPED_INFO("padded to RGBA16F");
        CHECK(LLImageGL::dataFormatVRAMBits(GL_RGB16F) == 64);
        CHECK(LLImageGL::dataFormatBits(GL_RGB32F) == 96);
        UNSCOPED_INFO("padded to RGBA32F");
        CHECK(LLImageGL::dataFormatVRAMBits(GL_RGB32F) == 128);
        CHECK(LLImageGL::dataFormatBits(GL_DEPTH_COMPONENT24) == 24);
        CHECK(LLImageGL::dataFormatVRAMBits(GL_DEPTH_COMPONENT24) == 32);

        // VRAM bytes path applies the same 4-byte alignment as the
        // host bytes path and routes unpadded formats through the
        // host table — so RGBA8 matches between the two.
        UNSCOPED_INFO("matches the host bytes");
        CHECK((S32)LLImageGL::dataFormatVRAMBytes(GL_RGBA8, 16, 16) == 1024);
        // 16x16 RGB8: host 16*16*3 = 768; VRAM 16*16*4 = 1024.
        CHECK((S32)LLImageGL::dataFormatBytes(GL_RGB8, 16, 16) == 768);
        CHECK((S32)LLImageGL::dataFormatVRAMBytes(GL_RGB8, 16, 16) == 1024);
    }

    // When no explicit format is set, createGLTexture derives one from
    // the raw's component count. With deprecated rewrites enabled this
    // means: 1 -> GL_RED, 2 -> GL_RG, 3 -> GL_RGB, 4 -> GL_RGBA.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: createGLTexture picks the primary format from the component count", "[LLImageGL]")
    {
        // 2-component raw — exercises the LUMINANCE_ALPHA -> GL_RG
        // rewrite path. This is the format the greyscale font atlas
        // uses (see commit c99dcaa7d3 — "Restore greyscale font atlas
        // to 2 component").
        LLPointer<LLImageRaw> raw2 = makeRaw(8, 8, 2, /*fill=*/0x80);
        LLPointer<LLImageGL> img2 = new LLImageGL(/*usemipmaps=*/false);
        CHECK(img2->createGLTexture(0, raw2.get()));
        CHECK((S32)img2->getPrimaryFormat() == (S32)GL_RG);

        LLPointer<LLImageRaw> raw4 = makeRaw(8, 8, 4, /*fill=*/0x80);
        LLPointer<LLImageGL> img4 = new LLImageGL(/*usemipmaps=*/false);
        CHECK(img4->createGLTexture(0, raw4.get()));
        CHECK((S32)img4->getPrimaryFormat() == (S32)GL_RGBA);
    }

    // usemipmaps=true must wire the auto-mipmap path through to GL. The
    // storage holds the chain: mip 1 of an 8x8 base is 4x4, where a
    // texture made without mips has no level 1. That alone does not say
    // the mips were generated, since glTexStorage2D allocates every
    // level whether or not anything is written to it; but mip 1 of a
    // flat base holds the base's value only once it has been.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a texture made with usemipmaps has a generated mip chain in GL", "[LLImageGL]")
    {
        LLPointer<LLImageRaw> raw = makeRaw(8, 8, 4, /*fill=*/0x80);
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/true);
        REQUIRE(img->createGLTexture(0, raw.get()));
        CHECK(img->getUseMipMaps());

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, img->getTexName());

        GLint mip1_w = 0;
        GLint mip1_h = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &mip1_w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_HEIGHT, &mip1_h);
        REQUIRE((S32)mip1_w == 4);
        REQUIRE((S32)mip1_h == 4);

        std::vector<U8> mip1((size_t)4 * 4 * 4, 0);
        readTexture(img, GL_RGBA, GL_UNSIGNED_BYTE, mip1.data(), /*level=*/1);
        UNSCOPED_INFO("bytes of mip 1 that are not the base's");
        CHECK(std::count_if(mip1.begin(), mip1.end(), [](U8 byte) { return byte != 0x80; }) == 0);
    }

    // Regression for 2d4e2a60ea — GL_TEXTURE_SWIZZLE_RGBA must be set
    // by createGLTexture for a deprecated-format texture so the shader
    // sees (R, R, R, A) for a 2-component LUMINANCE_ALPHA upload even
    // though the texture is now stored as GL_RG. Verifies the swizzle
    // mask matches applySwizzleForDeprecatedFormat's table.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: createGLTexture sets the RRRA swizzle for luminance-alpha", "[LLImageGL]")
    {
        // 2-component raw goes through LUMINANCE_ALPHA -> GL_RG
        // rewrite, which sets mDeprecatedSourceFormat=LUMINANCE_ALPHA.
        // createGLTexture then applies the matching swizzle.
        LLPointer<LLImageRaw> raw = makeRaw(8, 8, 2, /*fill=*/0x80);
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(img->createGLTexture(0, raw.get()));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, img->getTexName());

        GLint mask[4] = { 0, 0, 0, 0 };
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, mask);

        CHECK((S32)mask[0] == (S32)GL_RED);
        CHECK((S32)mask[1] == (S32)GL_RED);
        CHECK((S32)mask[2] == (S32)GL_RED);
        CHECK((S32)mask[3] == (S32)GL_GREEN);
    }

    // Round-trip: upload a known pattern, glGetTexImage it back, and
    // verify byte-for-byte. Covers the createGLTexture upload path and
    // readBackRaw together. RGBA chosen so no deprecated rewrite kicks
    // in — that path is covered separately by the setExplicitFormat and
    // component-count cases above.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: readBackRaw returns the bytes createGLTexture uploaded", "[LLImageGL]")
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
        REQUIRE(img->createGLTexture(0, src.get()));

        LLPointer<LLImageRaw> dst = new LLImageRaw(W, H, C);
        std::memset(dst->getData(), 0, (size_t)W * H * C);
        CHECK(img->readBackRaw(0, dst.get(), /*compressed_ok=*/false));

        CHECK(std::memcmp(src->getData(), dst->getData(),
                                  (size_t)W * H * C) ==
                      0);
    }

    // A packed pixel type is one value a pixel, whatever the format's component
    // count. GStreamer hands its frames over as GL_BGRA /
    // GL_UNSIGNED_INT_8_8_8_8_REV, and the upload that slices by scanline -- on the
    // main thread on Windows off Intel, on Linux with NVIDIA, on macOS with AMD --
    // stepped through rows by component count times a per-type width that had no
    // entry for it, and died. Where the slicing is off this passes either way.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a packed pixel type uploads and updates as one value a pixel", "[LLImageGL]")
    {
        // 64 rows, a multiple of 32: the full upload takes the batched slices.
        constexpr U16 W = 64, H = 64;
        LLPointer<LLImageRaw> src = new LLImageRaw(W, H, 4);
        U8* sd = src->getData();
        for (size_t i = 0; i < (size_t)W * H * 4; ++i)
            sd[i] = (U8)(i & 0xFF);

        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        img->setExplicitFormat(GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV);
        REQUIRE(img->createGLTexture(0, src.get()));

        std::vector<U8> got((size_t)W * H * 4);
        readTexture(img, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, got.data());
        UNSCOPED_INFO("the full upload");
        CHECK(std::memcmp(got.data(), sd, got.size()) == 0);

        // A partial-width update, which slices one row at a time.
        LLPointer<LLImageRaw> patch = new LLImageRaw(W, H, 4);
        U8* pd = patch->getData();
        for (size_t i = 0; i < (size_t)W * H * 4; ++i)
            pd[i] = (U8)(0xFF - (i & 0xFF));
        CHECK(img->setSubImage(patch.get(), /*x_pos=*/8, /*y_pos=*/4,
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
        UNSCOPED_INFO("the update lands where it was asked and nowhere else");
        CHECK(match);
    }

    // scaleDown builds a new texture object, and texture-object state does not
    // come with it: a deprecated-format texture lost its swizzle, so luminance read
    // back red and luminance-alpha opaque. The FBO method -- the default -- could
    // not have kept the channels either, since it samples THROUGH the swizzle and
    // copies back by position, so these take the PBO copy whatever the setting.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: scaleDown keeps a deprecated-format texture's swizzle and channels", "[LLImageGL]")
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
        REQUIRE(img->createGLTexture(0, raw.get()));

        const U32 method = gGLManager.mDownScaleMethod;
        gGLManager.mDownScaleMethod = 0;
        const bool scaled = img->scaleDown(1);
        gGLManager.mDownScaleMethod = method;
        CHECK(scaled);
        CHECK(img->getDiscardLevel() == 1);

        bindForRead(img);
        GLint w = 0;
        GLint h = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        REQUIRE((S32)w == (S32)W / 2);
        REQUIRE((S32)h == (S32)H / 2);

        GLint mask[4] = { 0, 0, 0, 0 };
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, mask);
        CHECK((S32)mask[0] == (S32)GL_RED);
        CHECK((S32)mask[1] == (S32)GL_RED);
        CHECK((S32)mask[2] == (S32)GL_RED);
        CHECK((S32)mask[3] == (S32)GL_GREEN);

        std::vector<U8> got((size_t)(W / 2) * (H / 2) * 2);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RG, GL_UNSIGNED_BYTE, got.data());
        bool match = true;
        for (size_t i = 0; i < got.size() && match; i += 2)
        {
            match = got[i] == 0x40 && got[i + 1] == 0xC0;
        }
        UNSCOPED_INFO("luminance and alpha both survive the copy");
        CHECK(match);
    }

    // scaleDown waits out an upload in flight: the LLImageGL thread is writing the
    // discard level and storage state it reads and replaces, and that upload's
    // publish would then install its texture under the discard level set here.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: scaleDown waits until an upload in flight has ended", "[LLImageGL]")
    {
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/true);
        REQUIRE(img->createGLTexture(0, makeRaw(16, 16, 4, 0x80).get()));
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

        UNSCOPED_INFO("refused while an upload is in flight");
        CHECK_FALSE(during);
        CHECK((S32)name_during == (S32)name);
        UNSCOPED_INFO("done once the upload has ended");
        CHECK(after);
    }

    // While an upload is in flight the members describe the texture being built.
    // readBackRaw paired the getters' published size with the members' discard
    // level, so it read the old texture's level 0 at the new texture's size.
    // Asset 64x64, on screen at discard 2 (a 16x16 texture), the upload in flight
    // building discard 0.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: readBackRaw during an upload reads the texture on screen", "[LLImageGL]")
    {
        constexpr U16 W = 16, H = 16;
        LLPointer<LLImageRaw> src = new LLImageRaw(W, H, 4);
        U8* sd = src->getData();
        for (size_t i = 0; i < (size_t)W * H * 4; ++i)
            sd[i] = (U8)(i & 0xFF);

        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(img->createGLTexture(2, src.get()));
        CHECK(img->getWidth() == 16);

        img->beginUpload();
        img->setDiscardLevel(0); // what the worker's createGLTexture writes first

        LLPointer<LLImageRaw> dst = new LLImageRaw();
        const bool read = img->readBackRaw(-1, dst.get(), /*compressed_ok=*/false);

        img->setDiscardLevel(2);
        img->endUpload();

        REQUIRE(read);
        {
            INFO("read at the size on screen");
            REQUIRE((S32)dst->getWidth() == (S32)W);
            REQUIRE((S32)dst->getHeight() == (S32)H);
        }
        REQUIRE((S32)dst->getComponents() == 4);
        UNSCOPED_INFO("and reads what is on screen");
        CHECK(std::memcmp(dst->getData(), sd, (size_t)W * H * 4) == 0);
    }

    // An upload of another component count works its format out on the main thread as it
    // begins, and the image goes on answering for the texture on screen until the new one
    // publishes: its format, and the alpha a format without one clears.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: an upload in flight answers with the format on screen", "[LLImageGL]")
    {
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(img->createGLTexture(0, makeRaw(16, 16, 4, 0x00).get()));
        REQUIRE(img->getPrimaryFormat() == (LLGLenum)GL_RGBA);
        REQUIRE(img->getMaxAlpha() == 0);

        img->beginUpload(3);
        UNSCOPED_INFO("the RGBA texture on screen, not the RGB one being built");
        CHECK(img->getPrimaryFormat() == (LLGLenum)GL_RGBA);
        CHECK(img->getMaxAlpha() == 0);
        img->endUpload();
        UNSCOPED_INFO("an upload that never published leaves the alpha alone");
        CHECK(img->getMaxAlpha() == 0);
    }

    // The pick mask of a texture still being uploaded waits for that texture:
    // getMask answers for the one on screen, and off the main thread building the
    // mask in place freed the buffer getMask was reading. It publishes with the
    // texture, and an upload that never publishes leaves it alone.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: the pick mask of an upload in flight publishes with its texture", "[LLImageGL]")
    {
        constexpr U16 W = 16, H = 16;
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(img->createGLTexture(0, makeRaw(W, H, 4, 0xFF).get()));
        const LLVector2 centre(0.5f, 0.5f);
        UNSCOPED_INFO("opaque texture picks");
        CHECK(img->getMask(centre));

        LLPointer<LLImageRaw> clear = makeRaw(W, H, 4, 0x00);

        img->beginUpload();
        img->updatePickMask(W, H, clear->getData());
        UNSCOPED_INFO("the mask on screen answers while the upload is in flight");
        CHECK(img->getMask(centre));
        img->endUpload();
        UNSCOPED_INFO("an upload that never published leaves the mask alone");
        CHECK(img->getMask(centre));

        img->beginUpload();
        img->updatePickMask(W, H, clear->getData());
        img->syncTexName(img->getTexName());
        UNSCOPED_INFO("the new mask publishes with its texture");
        CHECK_FALSE(img->getMask(centre));
    }

    // The alpha analysis reports the highest alpha of the level it read, which the selection outline asks to tell a
    // texture its cut would leave nothing of: every texel counted, the odd row and column the mask histogram leaves
    // out among them; and 255 where there is no alpha to read.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: the alpha analysis reports the highest alpha, odd edges included", "[LLImageGL]")
    {
        // A clear w x h RGBA image with alpha `alpha` at (x, y).
        auto analysed = [](U16 w, U16 h, S32 x, S32 y, U8 alpha)
        {
            LLPointer<LLImageRaw> raw = makeRaw(w, h, 4, 0x00);
            if (x >= 0)
            {
                raw->getData()[((size_t)y * w + x) * 4 + 3] = alpha;
            }
            LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
            REQUIRE(img->createGLTexture(0, raw.get()));
            return img->getMaxAlpha();
        };

        UNSCOPED_INFO("a clear texture");
        CHECK(analysed(16, 16, -1, -1, 0) == 0);
        UNSCOPED_INFO("one texel inside");
        CHECK(analysed(16, 16, 5, 7, 200) == 200);
        UNSCOPED_INFO("a texel in the odd column");
        CHECK(analysed(15, 15, 14, 3, 90) == 90);
        UNSCOPED_INFO("a texel in the odd row");
        CHECK(analysed(15, 15, 6, 14, 60) == 60);
        UNSCOPED_INFO("the odd corner");
        CHECK(analysed(15, 15, 14, 14, 30) == 30);
        UNSCOPED_INFO("a single row");
        CHECK(analysed(9, 1, 8, 0, 120) == 120);

        LLPointer<LLImageGL> rgb = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(rgb->createGLTexture(0, makeRaw(16, 16, 3, 0x00).get()));
        UNSCOPED_INFO("no alpha channel reads as opaque");
        CHECK(rgb->getMaxAlpha() == 255);
    }

    // And the lowest, which the avatar shadow asks to tell a bake that cuts out from one that does not: the odd
    // row and column counted again, 255 where there is no alpha channel, and 0 where nothing was analysed.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: the alpha analysis reports the lowest alpha, odd edges included", "[LLImageGL]")
    {
        // An opaque w x h RGBA image with alpha `alpha` at (x, y).
        auto analysed = [](U16 w, U16 h, S32 x, S32 y, U8 alpha)
        {
            LLPointer<LLImageRaw> raw = makeRaw(w, h, 4, 0xFF);
            if (x >= 0)
            {
                raw->getData()[((size_t)y * w + x) * 4 + 3] = alpha;
            }
            LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
            REQUIRE(img->createGLTexture(0, raw.get()));
            return img->getMinAlpha();
        };

        UNSCOPED_INFO("an opaque texture");
        CHECK(analysed(16, 16, -1, -1, 0) == 255);
        UNSCOPED_INFO("one texel inside");
        CHECK(analysed(16, 16, 5, 7, 40) == 40);
        UNSCOPED_INFO("a texel in the odd column");
        CHECK(analysed(15, 15, 14, 3, 90) == 90);
        UNSCOPED_INFO("a texel in the odd row");
        CHECK(analysed(15, 15, 6, 14, 60) == 60);
        UNSCOPED_INFO("the odd corner");
        CHECK(analysed(15, 15, 14, 14, 30) == 30);
        UNSCOPED_INFO("a single row");
        CHECK(analysed(9, 1, 8, 0, 120) == 120);

        LLPointer<LLImageGL> rgb = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(rgb->createGLTexture(0, makeRaw(16, 16, 3, 0x00).get()));
        UNSCOPED_INFO("no alpha channel reads as opaque");
        CHECK(rgb->getMinAlpha() == 255);

        LLPointer<LLImageGL> unread = new LLImageGL(/*usemipmaps=*/false);
        unread->setNeedsAlphaAndPickMask(false);
        REQUIRE(unread->createGLTexture(0, makeRaw(16, 16, 4, 0xFF).get()));
        UNSCOPED_INFO("an alpha not analysed may be anything");
        CHECK(unread->getMinAlpha() == 0);
    }

    // The alpha layout is worked out with the format, once for a component count. An image
    // that comes to want its mask after an upload works both out again on the next, and
    // reads alpha from the fourth channel rather than the first.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a mask wanted after an upload reads alpha from the alpha channel", "[LLImageGL]")
    {
        constexpr U16 W = 16, H = 16;
        // Red full and alpha clear: read from the first channel, it would look opaque.
        LLPointer<LLImageRaw> raw = makeRaw(W, H, 4, 0x00);
        for (size_t i = 0; i < (size_t)W * H; ++i)
        {
            raw->getData()[i * 4] = 0xFF;
        }
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        img->setNeedsAlphaAndPickMask(false);
        REQUIRE(img->createGLTexture(0, raw.get()));
        UNSCOPED_INFO("no mask wanted reads as opaque");
        CHECK(img->getMaxAlpha() == 255);

        img->setNeedsAlphaAndPickMask(true);
        REQUIRE(img->createGLTexture(0, raw.get()));
        CHECK(img->getMaxAlpha() == 0);
    }

    // An edit writes through the ACTIVE unit, and a bind that finds the texture
    // already cached on slot 0 skips activating it. With another unit left active
    // the write went to whatever that unit held. The glyph atlas does exactly this:
    // still on slot 0 from the text being drawn when a new glyph arrives.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: an edit reaches its own texture while another unit is active", "[LLImageGL]")
    {
        constexpr U16 W = 32, H = 32;
        LLPointer<LLImageGL> a = new LLImageGL(/*usemipmaps=*/false);
        LLPointer<LLImageGL> b = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(a->createGLTexture(0, makeRaw(W, H, 4, 0x10).get()));
        REQUIRE(b->createGLTexture(0, makeRaw(W, H, 4, 0x20).get()));

        // Partial update, as the glyph atlas does it. Forcing the slot 1 bind is what
        // leaves unit 1 active, cached or not.
        gGL.getTextureSlot(0)->bind(a.get());
        gGL.getTextureSlot(1)->bind(b.get(), false, /*forceBind=*/true);
        CHECK(a->setSubImage(makeRaw(W, H, 4, 0xFF).get(), 4, 4, 8, 8,
                              /*force_fast_update=*/true));
        UNSCOPED_INFO("the update reached a");
        CHECK(readTexelRGBA(a, 5, 5) == 0xFFFFFFFFu);
        UNSCOPED_INFO("the update left b alone");
        CHECK(readTexelRGBA(b, 5, 5) == 0x20202020u);

        // Re-upload in place, through setImage.
        gGL.getTextureSlot(0)->bind(a.get());
        gGL.getTextureSlot(1)->bind(b.get(), false, /*forceBind=*/true);
        REQUIRE(a->createGLTexture(0, makeRaw(W, H, 4, 0x77).get()));
        UNSCOPED_INFO("the re-upload reached a");
        CHECK(readTexelRGBA(a, 5, 5) == 0x77777777u);
        UNSCOPED_INFO("the re-upload left b alone");
        CHECK(readTexelRGBA(b, 5, 5) == 0x20202020u);
    }

    // A re-upload at the same discard level wrote into the live texture in place,
    // on the word of the discard level alone. Immutable storage cannot follow a new
    // size or format: the write was rejected, or dropped the channel that did not
    // fit, and the image went on describing a texture GL never had.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a re-upload at the same discard level takes a new size or format", "[LLImageGL]")
    {
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(img->createGLTexture(0, makeRaw(32, 32, 4, 0x11).get()));
        REQUIRE(img->createGLTexture(0, makeRaw(64, 64, 4, 0x22).get()));

        bindForRead(img);
        GLint w = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        UNSCOPED_INFO("the texture took the new size");
        CHECK((S32)w == 64);
        UNSCOPED_INFO("the resized texture holds the new pixels");
        CHECK(readTexelRGBA(img, 40, 40) == 0x22222222u);

        // Same size, an alpha channel the old storage has no room for.
        LLPointer<LLImageGL> rgb = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(rgb->createGLTexture(0, makeRaw(16, 16, 3, 0x33).get()));
        REQUIRE(rgb->createGLTexture(0, makeRaw(16, 16, 4, 0x44).get()));

        bindForRead(rgb);
        GLint format = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        UNSCOPED_INFO("the texture took the new format");
        CHECK((S32)format == (S32)GL_SRGB8_ALPHA8);
        UNSCOPED_INFO("the reformatted texture holds the new pixels");
        CHECK(readTexelRGBA(rgb, 3, 3) == 0x44444444u);
    }

    // A deleted texture's name may come back out of glGenTextures, and a slot whose
    // bind cache still held it would take the new texture for bound already and skip
    // the bind -- sampling texture 0 in its place, since deleting the old one put the
    // binding back to 0. The cache forgets the name when the texture goes.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a slot's bind cache forgets a deleted texture's name", "[LLImageGL]")
    {
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(img->createGLTexture(0, makeRaw(16, 16, 4, 0x80).get()));
        const U32 name = img->getTexName();

        gGL.getTextureSlot(3)->bind(img.get());
        CHECK(gGL.getTextureSlot(3)->getCurrTexture() == name);

        img->destroyGLTexture();
        // However many frames the delete is held back for.
        for (S32 frame = 0; frame < 8; ++frame)
        {
            LLImageGL::updateClass();
        }

        CHECK(gGL.getTextureSlot(3)->getCurrTexture() == 0u);
    }

    // Generated mips fill the whole pyramid the storage holds, and sampling can
    // reach it. MAX_LEVEL stayed at the discard levels -- five below 256x256 -- so
    // glGenerateMipmap stopped there and distant surfaces minified no further.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: generated mips fill the whole pyramid and sampling reaches it", "[LLImageGL]")
    {
        constexpr U16 W = 256, H = 256;
        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/true);
        REQUIRE(img->createGLTexture(0, makeRaw(W, H, 4, 0x80).get()));

        bindForRead(img);
        GLint max_level = 0;
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &max_level);
        const S32 last = LLImageGL::calcMipLevelCount(W, H) - 1;
        UNSCOPED_INFO("sampling reaches the 1x1 level");
        CHECK((S32)max_level == last);

        U8 px[4] = { 0, 0, 0, 0 };
        glGetTexImage(GL_TEXTURE_2D, last, GL_RGBA, GL_UNSIGNED_BYTE, px);
        {
            INFO("the 1x1 level was generated");
            CHECK(px[0] == 0x80);
            CHECK(px[1] == 0x80);
            CHECK(px[2] == 0x80);
            CHECK(px[3] == 0x80);
        }
    }

    // gGL is thread_local and outlives a context, as it does across these tests,
    // and init left the slots' bind caches as the last context had them. A fresh
    // context hands out the same first names, so its first texture read as bound
    // already on a slot that had held its namesake, and the bind was skipped.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a new context starts with clear bind caches and unit 0 active", "[LLImageGL]")
    {
        {
            LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
            REQUIRE(img->createGLTexture(0, makeRaw(16, 16, 4, 0x80).get()));
            gGL.getTextureSlot(2)->bind(img.get());
        }

        // A new context on the same thread.
        gl.reset();
        gl = std::make_unique<ll_test::HeadlessGL>();

        CHECK(gGL.getTextureSlot(2)->getCurrTexture() == 0u);
        CHECK(gGL.getCurrentTexUnitIndex() == 0u);

        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(img->createGLTexture(0, makeRaw(16, 16, 4, 0x40).get()));
        gGL.getTextureSlot(2)->bind(img.get());
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        UNSCOPED_INFO("the bind reached GL");
        CHECK((U32)bound == img->getTexName());
    }

    // The same for the blend and colour-mask caches. A context left blending
    // BT_ALPHA made init's own setSceneBlendType(BT_ALPHA) look redundant, so the
    // next context kept GL's ONE, ZERO while gGL believed it was alpha blending;
    // and a mask left off read as still off on a context writing every channel.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a new context's blend and colour-mask caches match GL", "[LLImageGL]")
    {
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
        gGL.setColorMask(false, false);

        // A new context on the same thread.
        gl.reset();
        gl = std::make_unique<ll_test::HeadlessGL>();

        GLint src = 0;
        GLint dst = 0;
        glGetIntegerv(GL_BLEND_SRC_RGB, &src);
        glGetIntegerv(GL_BLEND_DST_RGB, &dst);
        {
            INFO("init's alpha blending reached GL");
            CHECK((S32)src == (S32)GL_SRC_ALPHA);
            CHECK((S32)dst == (S32)GL_ONE_MINUS_SRC_ALPHA);
        }

        bool mask[4] = { false, false, false, false };
        gGL.getColorMask(mask);
        {
            INFO("the colour mask cache says what GL starts with");
            CHECK(mask[0]);
            CHECK(mask[1]);
            CHECK(mask[2]);
            CHECK(mask[3]);
        }
        GLboolean gl_mask[4] = { GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE };
        glGetBooleanv(GL_COLOR_WRITEMASK, gl_mask);
        {
            INFO("and GL's mask agrees");
            CHECK(gl_mask[0]);
            CHECK(gl_mask[1]);
            CHECK(gl_mask[2]);
            CHECK(gl_mask[3]);
        }
    }

    // And for vertex buffer names, which LLVertexBuffer pools per thread as
    // LLImageGL pools texture names. The pool was never emptied with its context,
    // so the next context bound a name it had never generated -- which a core
    // profile refuses -- and the buffer was never allocated.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a new context binds vertex buffer names it made itself", "[LLImageGL]")
    {
        // A buffer of three vertices, written and unmapped as a caller does. Where the
        // name comes from the pool differs: the default VBO pool draws it when it
        // allocates, and binds it to give it storage; Apple's allocates only memory, and
        // draws and binds the name when the buffer is unmapped. Either way, what is bound
        // after the unmap is the name this buffer drew from the pool.
        auto fill = [](LLVertexBuffer* vb, const std::string& when)
        {
            CAPTURE(when);
            REQUIRE(vb->allocateBuffer(3, 0));
            LLStrider<LLVector3> verts;
            REQUIRE(vb->getVertexStrider(verts));
            for (S32 i = 0; i < 3; ++i)
            {
                verts[i] = LLVector3((F32)i, 0.f, 0.f);
            }
            vb->unmapBuffer();
        };

        {
            // Fill this thread's pool from this context.
            LLPointer<LLVertexBuffer> vb = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            fill(vb, "in the first context");
        }

        // A new context on the same thread.
        gl.reset();
        gl = std::make_unique<ll_test::HeadlessGL>();

        LLPointer<LLVertexBuffer> vb = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
        fill(vb, "in the new context");
        GLint bound = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bound);
        CHECK(bound != 0);
        UNSCOPED_INFO("it is a buffer this context made");
        CHECK(glIsBuffer((GLuint)bound) == GL_TRUE);
        LLVertexBuffer::unbind();
    }

    // An upload on the LLImageGL thread, end to end. The thread no longer waits
    // out the GPU: it fences, hands over, and moves on, and the main thread
    // publishes the texture once the fence has signalled. Until then the image
    // goes on showing the texture it had, and the caller's completion waits.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: an upload on the LLImageGL thread publishes once its fence signals", "[LLImageGL]")
    {
        // The queue the upload thread hands its results back on, as the viewer's.
        // It has to exist before the image, which looks it up when built.
        LL::WorkQueue mainloop("mainloop");
        std::atomic<bool> worker_done{ false };
        std::atomic<bool> worker_ok{ false };
        LLImageGLThread::createInstance(gl->window());
        // The thread ends with the test however the test ends: left running
        // after a failure, it would outlive the window its context shares,
        // and the flags its work writes to.
        struct ThreadEnd
        {
            ~ThreadEnd() { LLImageGLThread::deleteSingleton(); }
        } thread_end;

        LLPointer<LLImageGL> img = new LLImageGL(/*usemipmaps=*/false);
        REQUIRE(img->createGLTexture(0, makeRaw(16, 16, 4, 0x11).get()));
        const U32 old_name = img->getTexName();

        LLPointer<LLImageRaw> raw = makeRaw(32, 32, 4, 0x55);

        img->beginUpload();
        LLImageGLThread::instance().post(
            [img, raw, &worker_done, &worker_ok]()
            {
                worker_ok = img->createGLTexture(0, raw.get());
                worker_done = true;
            });

        // Bounded waits: a hang here should fail the test, not stall the suite.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!worker_done && std::chrono::steady_clock::now() < deadline)
        {
            mainloop.runPending();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // The handover was posted before worker_done was raised.
        mainloop.runPending();
        CHECK(worker_done.load());
        UNSCOPED_INFO("the worker's createGLTexture succeeded");
        CHECK(worker_ok.load());

        bool completed = false;
        U32 name_at_completion = 0;
        img->afterPublish([&]()
        {
            completed = true;
            name_at_completion = img->getTexName();
        });

        UNSCOPED_INFO("not published before its fence is seen");
        CHECK_FALSE(completed);
        UNSCOPED_INFO("the texture on screen is the old one");
        CHECK(img->getTexName() == old_name);
        UNSCOPED_INFO("still at the old size");
        CHECK(img->getWidth() == 16);

        while (LLImageGL::publishUploads() > 0 && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        LLImageGLThread::deleteSingleton();

        UNSCOPED_INFO("the afterPublish completion ran");
        CHECK(completed);
        UNSCOPED_INFO("after the new texture published");
        CHECK(name_at_completion != old_name);
        UNSCOPED_INFO("the image names the published texture");
        CHECK(img->getTexName() == name_at_completion);
        UNSCOPED_INFO("at the new size");
        CHECK(img->getWidth() == 32);
        UNSCOPED_INFO("holding the upload's pixels");
        CHECK(readTexelRGBA(img, 5, 5) == 0x55555555u);
    }

    // And the rest of what LLRender's setters cache. A polygon offset or patch
    // size the last context was left with made the same setting on the next one
    // look redundant, so the fresh context kept GL's initial value instead.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a new context takes the polygon offset and patch size set on it", "[LLImageGL]")
    {
        gGL.setPolygonOffset(1.f, 2.f);
        gGL.setPatchVertices(4);

        // A new context on the same thread.
        gl.reset();
        gl = std::make_unique<ll_test::HeadlessGL>();

        gGL.setPolygonOffset(1.f, 2.f);
        gGL.setPatchVertices(4);

        const F32 sign = LLRender::sReverseZ ? -1.f : 1.f;
        GLfloat factor = 0.f;
        GLfloat units = 0.f;
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &factor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &units);
        CHECK((F32)factor == sign * 1.f);
        CHECK((F32)units == sign * 2.f);

        GLint patch = 0;
        glGetIntegerv(GL_PATCH_VERTICES, &patch);
        CHECK((S32)patch == 4);
    }

    // Which vertex attribute arrays are enabled is vertex-array-object state, and
    // a fresh context's VAO has none. LLVertexBuffer's cache of them outlived the
    // context, so the next context's first draw skipped enabling the arrays it read.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: setupClientArrays enables the position array again on a new context", "[LLImageGL]")
    {
        LLVertexBuffer::setupClientArrays(LLVertexBuffer::MAP_VERTEX);

        // A new context on the same thread.
        gl.reset();
        gl = std::make_unique<ll_test::HeadlessGL>();

        LLVertexBuffer::setupClientArrays(LLVertexBuffer::MAP_VERTEX);
        GLint enabled = GL_FALSE;
        glGetVertexAttribiv(LLVertexBuffer::TYPE_VERTEX, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        LLVertexBuffer::setupClientArrays(0);

        CHECK(enabled == GL_TRUE);
    }

    // A worker ends by destroying its shared context on its own thread, where
    // it is current, as LLImageGLThread does. On EGL that only marked it for
    // destruction, and the context held its display past the window, so the
    // next window was made on the stale display, and failed.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: a window can be made after a worker destroyed its shared context", "[LLImageGL]")
    {
        LLWindow* window = gl->window();
        void* shared = window->createSharedContext();
        REQUIRE(shared != nullptr);
        std::thread worker([window, shared]
        {
            window->makeContextCurrent(shared);
            window->destroySharedContext(shared);
        });
        worker.join();

        gl.reset();
        try
        {
            gl = std::make_unique<ll_test::HeadlessGL>();
        }
        catch (const std::exception& e)
        {
            FAIL(std::string("the next window: ") + e.what());
        }
    }

    // The integer formats through LLImageGL. R32UI, RG16I and RGBA32UI allocate as themselves, hold what was uploaded
    // bit for bit -- past 2^24, and below 0 -- and take a partial update; each is accounted at its texels' size; and
    // the one asked for with mips holds level 0 alone, since GL neither builds nor filters an integer texture's mips.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: integer formats allocate as themselves and hold their texels exactly", "[LLImageGL]")
    {
        constexpr U16 W = 13, H = 7;
        constexpr size_t N = (size_t)W * H;

        std::vector<U32> uints(N);
        for (size_t i = 0; i < N; ++i)
        {
            uints[i] = 0x01000001u + (U32)i * 0x10001u;
        }
        LLPointer<LLImageGL> r32ui = new LLImageGL(W, H, 1, /*usemipmaps=*/true);
        r32ui->setExplicitFormat(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT);
        U64 before = LLImageGL::getTextureBytesAllocated();
        REQUIRE(r32ui->createGLTexture(0, reinterpret_cast<const U8*>(uints.data())));
        UNSCOPED_INFO("R32UI accounted at four bytes a texel");
        CHECK((S64)(LLImageGL::getTextureBytesAllocated() - before) == (S64)N * 4);
        CHECK((S64)r32ui->getMipBytes(0) == (S64)N * 4);
        UNSCOPED_INFO("an integer texture asked for mips takes none");
        CHECK_FALSE(r32ui->getUseMipMaps());

        bindForRead(r32ui);
        GLint format = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        CHECK((S32)format == (S32)GL_R32UI);
        GLint level1_width = -1;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &level1_width);
        UNSCOPED_INFO("no level past the first");
        CHECK((S32)level1_width == 0);

        std::vector<U32> got_uints(N, 0);
        readTexture(r32ui, GL_RED_INTEGER, GL_UNSIGNED_INT, got_uints.data());
        CHECK(got_uints == uints);

        // A partial update, its rows read from a wider source as the glyph atlas updates.
        std::vector<U32> patch(N);
        for (size_t i = 0; i < N; ++i)
        {
            patch[i] = 0xF0000000u + (U32)i;
        }
        CHECK(r32ui->setSubImage(reinterpret_cast<const U8*>(patch.data()), W, H, 2, 1, 4, 3, /*force_fast_update=*/true));
        readTexture(r32ui, GL_RED_INTEGER, GL_UNSIGNED_INT, got_uints.data());
        for (U32 y = 0; y < H; ++y)
        {
            for (U32 x = 0; x < W; ++x)
            {
                const size_t i = (size_t)y * W + x;
                const bool in_patch = x >= 2 && x < 6 && y >= 1 && y < 4;
                CAPTURE(x, y);
                REQUIRE(got_uints[i] ==
                              (in_patch ? patch[i] : uints[i]));
            }
        }

        std::vector<S16> shorts(N * 2);
        for (size_t i = 0; i < N; ++i)
        {
            shorts[i * 2]     = (S16)(-1 - (S32)i * 300);
            shorts[i * 2 + 1] = (S16)((S32)i * 251 - 12000);
        }
        LLPointer<LLImageGL> rg16i = new LLImageGL(W, H, 2, /*usemipmaps=*/false);
        rg16i->setExplicitFormat(GL_RG16I, GL_RG_INTEGER, GL_SHORT);
        before = LLImageGL::getTextureBytesAllocated();
        REQUIRE(rg16i->createGLTexture(0, reinterpret_cast<const U8*>(shorts.data())));
        UNSCOPED_INFO("RG16I accounted at four bytes a texel");
        CHECK((S64)(LLImageGL::getTextureBytesAllocated() - before) == (S64)N * 4);
        bindForRead(rg16i);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        CHECK((S32)format == (S32)GL_RG16I);
        std::vector<S16> got_shorts(N * 2, 0);
        readTexture(rg16i, GL_RG_INTEGER, GL_SHORT, got_shorts.data());
        CHECK(got_shorts == shorts);

        std::vector<U32> wide(N * 4);
        for (size_t i = 0; i < N; ++i)
        {
            wide[i * 4]     = (U32)i;
            wide[i * 4 + 1] = 0x80000000u | (U32)i;
            wide[i * 4 + 2] = ~(U32)i;
            wide[i * 4 + 3] = 0xDEADBEEFu ^ (U32)i;
        }
        LLPointer<LLImageGL> rgba32ui = new LLImageGL(W, H, 4, /*usemipmaps=*/false);
        rgba32ui->setExplicitFormat(GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT);
        before = LLImageGL::getTextureBytesAllocated();
        REQUIRE(rgba32ui->createGLTexture(0, reinterpret_cast<const U8*>(wide.data())));
        UNSCOPED_INFO("RGBA32UI accounted at sixteen bytes a texel");
        CHECK((S64)(LLImageGL::getTextureBytesAllocated() - before) == (S64)N * 16);
        bindForRead(rgba32ui);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        CHECK((S32)format == (S32)GL_RGBA32UI);
        std::vector<U32> got_wide(N * 4, 0);
        readTexture(rgba32ui, GL_RGBA_INTEGER, GL_UNSIGNED_INT, got_wide.data());
        CHECK(got_wide == wide);

        // What an LLImageRaw cannot hold, readBackRaw does not read: it has a byte a component.
        LLPointer<LLImageRaw> raw = new LLImageRaw();
        CHECK_FALSE(r32ui->readBackRaw(0, raw.get(), /*compressed_ok=*/false));
        CHECK_FALSE(r32ui->scaleDown(1));

        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // The integer formats in the size and channel tables: the sized ones at their texels' bits, the three-channel ones
    // padded in VRAM as RGB8 is, and the pixel formats their data comes in at their channel counts.
    TEST_CASE_METHOD(llimagegl_data, "LLImageGL: the size and channel tables cover the integer formats", "[LLImageGL]")
    {
        const std::pair<GLenum, S32> bits[] =
        {
            { GL_R8I, 8 },      { GL_R8UI, 8 },      { GL_R16I, 16 },     { GL_R16UI, 16 },
            { GL_R32I, 32 },    { GL_R32UI, 32 },    { GL_RG8I, 16 },     { GL_RG8UI, 16 },
            { GL_RG16I, 32 },   { GL_RG16UI, 32 },   { GL_RG32I, 64 },    { GL_RG32UI, 64 },
            { GL_RGB8I, 24 },   { GL_RGB8UI, 24 },   { GL_RGB16I, 48 },   { GL_RGB16UI, 48 },
            { GL_RGB32I, 96 },  { GL_RGB32UI, 96 },  { GL_RGBA8I, 32 },   { GL_RGBA8UI, 32 },
            { GL_RGBA16I, 64 }, { GL_RGBA16UI, 64 }, { GL_RGBA32I, 128 }, { GL_RGBA32UI, 128 },
            { GL_RGB10_A2UI, 32 },
        };
        for (const auto& entry : bits)
        {
            INFO("format " << entry.first);
            REQUIRE(LLImageGL::dataFormatBits(entry.first) == entry.second);
            const S32 vram = entry.second == 24 ? 32 : entry.second == 48 ? 64 : entry.second == 96 ? 128 : entry.second;
            REQUIRE(LLImageGL::dataFormatVRAMBits(entry.first) == vram);
        }

        const std::pair<GLenum, S32> components[] =
        {
            { GL_RED_INTEGER, 1 }, { GL_RG_INTEGER, 2 },   { GL_RGB_INTEGER, 3 },
            { GL_BGR_INTEGER, 3 }, { GL_RGBA_INTEGER, 4 }, { GL_BGRA_INTEGER, 4 },
        };
        for (const auto& entry : components)
        {
            INFO("pixel format " << entry.first);
            REQUIRE(LLImageGL::dataFormatComponents(entry.first) == entry.second);
        }
    }
}
