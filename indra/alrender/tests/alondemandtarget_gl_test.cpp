/**
 * @file alondemandtarget_gl_test.cpp
 * @brief An on-demand target on the hidden window: a run with nothing to draw takes, binds and clears nothing, a run
 *        that draws takes its target once, the pool keeps it as long as the run asked, and the previews and the bakes
 *        draw through the plan's descriptions and copy out what they drew, as LLViewerDynamicTexture::updateAllInstances
 *        has them.
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

#include "../alondemandtarget.h"
#include "../alrenderresourceplan.h"
#include "../altransienttargetpool.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llglstates.h"
#include "llgltexture.h"
#include "llimage.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llrendertarget.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

namespace
{
    using Pool = ALTransientTargetPool;
    using Plan = ALRenderResourcePlan;
    using Id   = Plan::Id;
    using RGBA = std::array<U8, 4>;

    const RGBA BLACK{ 0, 0, 0, 0 };
    const RGBA RED{ 255, 0, 0, 255 };
    const RGBA BLUE{ 0, 0, 255, 255 };
    const RGBA SENTINEL{ 51, 102, 153, 255 };

    // The plan's descriptions at the defaults the viewer starts with: the preview's own depth is the scene's format.
    const Plan& plan()
    {
        static const Plan plan = []()
        {
            ALRenderResourceInputs in;
            in.mWidth  = 2560;
            in.mHeight = 1440;
            return Plan::make(in);
        }();
        return plan;
    }

    LLColor4 colour(const RGBA& c)
    {
        return LLColor4(c[0] / 255.f, c[1] / 255.f, c[2] / 255.f, c[3] / 255.f);
    }

    // A dynamic texture, as updateAllInstances sees one: whether it needs a render this frame, its size and
    // components, and the GL texture it copies itself into.
    struct Item
    {
        bool                 mNeeds = false;
        S32                  mWidth = 0;
        S32                  mHeight = 0;
        S32                  mComponents = 0;
        LLPointer<LLImageGL> mImage;
        U32                  mAsked = 0;
        U32                  mDrawn = 0;
    };

    // LLViewerDynamicTexture::generateGLTexture's texture: the instance's size and components, nothing copied in yet.
    LLPointer<LLImageGL> dynamic_texture(S32 width, S32 height, S32 components)
    {
        LLPointer<LLImageGL>  image = new LLImageGL(width, height, (U8)components, /*usemipmaps=*/false);
        LLPointer<LLImageRaw> raw   = new LLImageRaw((U16)width, (U16)height, (S8)components);
        REQUIRE(image->createGLTexture(0, raw.get(), 0, true, LLGLTexture::DYNAMIC_TEX));
        image->setGLTextureCreated(false);
        return image;
    }

    // Fill [x, x + w) x [y, y + h) of the bound target with `c`, as a draw does.
    void fill_rect(S32 x, S32 y, S32 w, S32 h, const RGBA& c)
    {
        LLGLEnable     scissor(GL_SCISSOR_TEST);
        LLGLSColorMask mask(true, true);
        const LLColor4 previous = gGL.getClearColor();
        glScissor(x, y, w, h);
        gGL.setClearColor(colour(c));
        glClear(GL_COLOR_BUFFER_BIT);
        gGL.setClearColor(previous);
    }

    // What the texture holds, row by row from the bottom, as RGBA.
    std::vector<RGBA> pixels_of(LLImageGL& image)
    {
        std::vector<RGBA> pixels((size_t)image.getWidth() * image.getHeight());
        gGL.getTextureSlot(0)->bind(&image);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        gGL.getTextureSlot(0)->unbind();
        return pixels;
    }

    // What a preview or a bake does once its target is bound, as update_func has it: its own viewport at the corner,
    // depth cleared, its draw, and the copy into its texture before the next draws (postRender).
    template <typename Draw> void draw_item(Item& item, LLRenderTarget& target, Draw&& draw)
    {
        ++item.mDrawn;
        CHECK(LLRenderTarget::getCurrentBoundTarget() == &target);
        gGL.setViewport(0, 0, item.mWidth, item.mHeight);
        gGL.clear(GL_DEPTH_BUFFER_BIT);
        draw(target);
        REQUIRE(item.mImage->setSubImageFromFrameBuffer(0, 0, 0, 0, item.mWidth, item.mHeight));
    }

    U32 draw_framebuffer()
    {
        GLint framebuffer = -1;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        return (U32)framebuffer;
    }

    RGBA centre_of(LLRenderTarget& target)
    {
        RGBA pixel{};
        target.bindTarget();
        glReadPixels(target.getWidth() / 2, target.getHeight() / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        target.flush();
        return pixel;
    }

    std::string str(const RGBA& c)
    {
        return std::to_string(c[0]) + "," + std::to_string(c[1]) + "," + std::to_string(c[2]) + "," + std::to_string(c[3]);
    }

    // Each pixel of `pixels` (width `w`) that differs from `inside` within the rectangle or from `outside` beyond it.
    U32 mismatches(const std::vector<RGBA>& pixels, S32 w, S32 x0, S32 y0, S32 rw, S32 rh, const RGBA& inside,
                   const RGBA& outside, std::string& first)
    {
        U32 count = 0;
        for (size_t i = 0; i < pixels.size(); ++i)
        {
            const S32  x    = (S32)(i % (size_t)w);
            const S32  y    = (S32)(i / (size_t)w);
            const bool in   = x >= x0 && x < x0 + rw && y >= y0 && y < y0 + rh;
            const RGBA want = in ? inside : outside;
            if (pixels[i] != want)
            {
                if (count++ == 0)
                {
                    first = "(" + std::to_string(x) + "," + std::to_string(y) + ") is " + str(pixels[i]) + ", not " +
                            str(want);
                }
            }
        }
        return count;
    }

    struct on_demand_gl_data
    {
        on_demand_gl_data()
        {
            gGL.setClearColor(colour(BLACK));
            REQUIRE(sentinel.ensure({ .width = 4, .height = 4, .color = { GL_RGBA8 } }) == LLRenderTarget::EnsureResult::Built);
            gGL.setClearColor(colour(SENTINEL));
            sentinel.bindTarget();
            sentinel.clear();
            sentinel.flush();
            gGL.setClearColor(colour(BLACK));
        }

        ~on_demand_gl_data()
        {
            while (LLRenderTarget::getCurrentBoundTarget())
            {
                LLRenderTarget::getCurrentBoundTarget()->flush();
            }
            pool.clear();
            sentinel.release();
        }

        ll_test::SharedGLScope mGL;
        LLRenderTarget         sentinel;
        Pool                   pool;
    };

    // A backing whose every build fails, as a target the GL cannot allocate.
    struct FailingBacking final : Pool::Backing
    {
        bool build(LLRenderTarget&, const Pool::Key&, std::string_view) override { return false; }
        void rename(LLRenderTarget&, std::string_view) override {}
        void poison(LLRenderTarget&, const Pool::Key&) override {}
        void free(LLRenderTarget&) override {}
        U64  depthSerial(const LLRenderTarget&) const override { return 1; }
    };

    TEST_CASE_METHOD(on_demand_gl_data, "alondemandtarget_gl: a run with nothing to draw takes, binds and clears nothing",
                     "[alondemandtarget_gl]")
    {
        std::vector<Item> previews(3);
        std::vector<Item> bakes(2);

        // What the frame had bound before the dynamic textures, still bound through them and after.
        sentinel.bindTarget();
        const U32 framebuffer = draw_framebuffer();
        for (auto* run : { &previews, &bakes })
        {
            ALOnDemandTarget target(pool, plan().desc(run == &previews ? Id::Preview : Id::Bake), "run");
            CHECK(target.drawEach(*run, [](Item& item) { return ++item.mAsked, item.mNeeds; },
                                  [](Item& item, LLRenderTarget&) { ++item.mDrawn; }));
            CHECK(LLRenderTarget::getCurrentBoundTarget() == &sentinel);
            CHECK(draw_framebuffer() == framebuffer);
        }
        CHECK(LLRenderTarget::getCurrentBoundTarget() == &sentinel);
        CHECK(draw_framebuffer() == framebuffer);
        sentinel.flush();

        for (const auto* run : { &previews, &bakes })
        {
            for (const Item& item : *run)
            {
                CHECK(item.mAsked == 1u);
                CHECK(item.mDrawn == 0u);
            }
        }
        UNSCOPED_INFO("nothing acquired, built or held");
        CHECK(pool.stats().mTargets == 0u);
        CHECK(pool.stats().mMisses == 0u);
        CHECK(pool.stats().mHits == 0u);
        CHECK(pool.stats().mFailures == 0u);
        CHECK(pool.stats().mBytes == 0u);
        UNSCOPED_INFO("nothing cleared: what was bound holds what it held");
        CHECK(str(centre_of(sentinel)) == str(SENTINEL));
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    TEST_CASE_METHOD(on_demand_gl_data, "alondemandtarget_gl: a run takes its target once, for the first that draws, and gives it back",
                     "[alondemandtarget_gl]")
    {
        std::vector<Item> items(4);
        items[1].mNeeds = true;
        items[3].mNeeds = true;

        sentinel.bindTarget();
        std::vector<size_t> order;
        LLRenderTarget*     drawn_into = nullptr;
        {
            ALOnDemandTarget target(pool, plan().desc(Id::Preview), "preview");
            CHECK(target.drawEach(items,
                                  [&](Item& item)
                                  {
                                      order.push_back((size_t)(&item - items.data()));
                                      ++item.mAsked;
                                      return item.mNeeds;
                                  },
                                  [&](Item& item, LLRenderTarget& bound)
                                  {
                                      ++item.mDrawn;
                                      CHECK(LLRenderTarget::getCurrentBoundTarget() == &bound);
                                      CHECK((!drawn_into || drawn_into == &bound));
                                      drawn_into = &bound;
                                  }));
            REQUIRE(drawn_into);
            CHECK(drawn_into->desc() == *plan().desc(Id::Preview));
            CHECK(pool.stats().mMisses == 1u);
            CHECK(pool.stats().mHits == 0u);

            target.end();
            UNSCOPED_INFO("flushed: the frame's own target bound again");
            CHECK(LLRenderTarget::getCurrentBoundTarget() == &sentinel);
            UNSCOPED_INFO("given back as the run ends: the next acquisition finds it");
            LLRenderTarget* again = pool.acquire(*plan().desc(Id::Preview), "preview");
            CHECK(again == drawn_into);
            CHECK(pool.stats().mHits == 1u);
            CHECK(pool.release(again));
            CHECK(target.bind() == nullptr);
        }
        CHECK(LLRenderTarget::getCurrentBoundTarget() == &sentinel);
        sentinel.flush();

        CHECK(order == std::vector<size_t>{ 0, 1, 2, 3 });
        CHECK(items[0].mDrawn == 0u);
        CHECK(items[1].mDrawn == 1u);
        CHECK(items[2].mDrawn == 0u);
        CHECK(items[3].mDrawn == 1u);
        CHECK(pool.stats().mTargets == 1u);
        CHECK(pool.badReleases() == 0u);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A preview drawn seconds apart, as updateAllInstances asks for its targets: the pool keeps the target through the
    // frames the run asked for, and the next draw finds it without a build.
    TEST_CASE_METHOD(on_demand_gl_data, "alondemandtarget_gl: the pool keeps a run's target as long as the run asked",
                     "[alondemandtarget_gl]")
    {
        constexpr U32 KEEP = 300;
        const auto run = [&]() -> LLRenderTarget*
        {
            ALOnDemandTarget target(pool, plan().desc(Id::Preview), "preview", KEEP);
            return target.bind();
        };

        LLRenderTarget* first = run();
        REQUIRE(first);
        for (U32 frame = 1; frame <= KEEP; ++frame)
        {
            pool.beginFrame();
        }
        UNSCOPED_INFO("unused for 300 whole frames, past the pool's 120: still there, and the next run's");
        CHECK(pool.stats().mTargets == 1u);
        CHECK(run() == first);
        CHECK(pool.stats().mMisses == 0u);
        CHECK(pool.stats().mHits == 1u);

        for (U32 frame = 0; frame <= KEEP; ++frame)
        {
            pool.beginFrame();
        }
        UNSCOPED_INFO("301 unused: freed");
        CHECK(pool.stats().mTargets == 0u);
        CHECK(pool.stats().mTrimmed == 1u);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    TEST_CASE_METHOD(on_demand_gl_data, "alondemandtarget_gl: where no target can be had, the rest wait for another run",
                     "[alondemandtarget_gl]")
    {
        FailingBacking failing;
        Pool           unbuildable(failing);
        for (const bool described : { false, true })
        {
            CAPTURE(described);
            std::vector<Item> items(4);
            items[1].mNeeds = true;
            items[2].mNeeds = true;

            ALOnDemandTarget target(described ? unbuildable : pool,
                                    described ? plan().desc(Id::Bake) : std::optional<ALRenderTargetDesc>(), "bake");
            const auto asked = [](Item& item) { return ++item.mAsked, item.mNeeds; };
            const auto draw  = [](Item& item, LLRenderTarget&) { ++item.mDrawn; };
            CHECK_FALSE(target.drawEach(items, asked, draw));
            UNSCOPED_INFO("asked up to the first that would draw, and none drawn");
            CHECK(items[0].mAsked == 1u);
            CHECK(items[1].mAsked == 1u);
            CHECK(items[2].mAsked == 0u);
            CHECK(items[3].mAsked == 0u);
            for (const Item& item : items)
            {
                CHECK(item.mDrawn == 0u);
            }
            CHECK(target.bind() == nullptr);
            CHECK(LLRenderTarget::getCurrentBoundTarget() == nullptr);
        }
        CHECK(unbuildable.stats().mFailures == 1u);
        CHECK(pool.stats().mTargets == 0u);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // Two frames over one pool that poisons what is released: the second takes the target the first gave back, which
    // the bind's clear empties before the preview draws a different rectangle.
    TEST_CASE_METHOD(on_demand_gl_data, "alondemandtarget_gl: a preview copies out what it drew through the pool's target",
                     "[alondemandtarget_gl]")
    {
        pool.setValidation(true);

        // An image preview's avatar (LLImagePreviewAvatar): 256x256, RGB.
        Item preview{ .mNeeds = true, .mWidth = 256, .mHeight = 256, .mComponents = 3 };
        preview.mImage = dynamic_texture(preview.mWidth, preview.mHeight, preview.mComponents);
        std::vector<Item*> previews{ &preview };

        struct Frame
        {
            S32  x, y, w, h;
            RGBA c;
        };
        LLRenderTarget* first = nullptr;
        for (const Frame& frame : { Frame{ 16, 32, 100, 60, RED }, Frame{ 128, 8, 40, 200, BLUE } })
        {
            CAPTURE(frame.x, frame.y);
            {
                ALOnDemandTarget target(pool, plan().desc(Id::Preview), "preview");
                CHECK(target.drawEach(previews, [](Item* item) { return item->mNeeds; },
                                      [&](Item* item, LLRenderTarget& bound)
                                      {
                                          first = first ? first : &bound;
                                          CHECK(&bound == first);
                                          draw_item(*item, bound, [&](LLRenderTarget&)
                                                    { fill_rect(frame.x, frame.y, frame.w, frame.h, frame.c); });
                                      }));
            }
            // The texture is RGB: what the clear left reads back opaque.
            std::string where;
            const RGBA  opaque_black{ 0, 0, 0, 255 };
            const U32   wrong = mismatches(pixels_of(*preview.mImage), preview.mWidth, frame.x, frame.y, frame.w, frame.h,
                                           frame.c, opaque_black, where);
            INFO(where);
            CHECK(wrong == 0u);
        }
        CHECK(pool.stats().mTargets == 1u);
        CHECK(pool.stats().mHits == 1u);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // The bake's morph masks read the bake target back mid-draw where a driver cannot read its alpha alone
    // (LLTexLayer::renderMorphMasks): what was drawn is there while the target is held, and copies out after.
    TEST_CASE_METHOD(on_demand_gl_data, "alondemandtarget_gl: a bake reads its own target mid-draw and copies out what it drew",
                     "[alondemandtarget_gl]")
    {
        pool.setValidation(true);

        // A baked layer set's composite (LLViewerTexLayerSetBuffer): 1024x1024, RGBA.
        Item bake{ .mNeeds = true, .mWidth = 1024, .mHeight = 1024, .mComponents = 4 };
        bake.mImage = dynamic_texture(bake.mWidth, bake.mHeight, bake.mComponents);
        std::vector<Item*> bakes{ &bake };

        const RGBA mask{ 0, 255, 0, 0 };
        for (const RGBA& c : { mask, RED })
        {
            CAPTURE(str(c));
            {
                ALOnDemandTarget target(pool, plan().desc(Id::Bake), "bake");
                CHECK(target.drawEach(bakes, [](Item* item) { return item->mNeeds; },
                                      [&](Item* item, LLRenderTarget& bound)
                                      {
                                          draw_item(*item, bound,
                                                    [&](LLRenderTarget& held)
                                                    {
                                                        fill_rect(100, 200, 300, 400, c);

                                                        std::vector<RGBA> read((size_t)held.getWidth() * held.getHeight());
                                                        gGL.getTextureSlot(0)->bind(&held);
                                                        glPixelStorei(GL_PACK_ALIGNMENT, 1);
                                                        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, read.data());
                                                        gGL.getTextureSlot(0)->unbind();
                                                        CHECK(str(read[(size_t)300 * held.getWidth() + 200]) == str(c));
                                                        CHECK(str(read[(size_t)10 * held.getWidth() + 10]) == str(BLACK));
                                                    });
                                      }));
            }
            std::string where;
            const U32   wrong = mismatches(pixels_of(*bake.mImage), bake.mWidth, 100, 200, 300, 400, c, BLACK, where);
            INFO(where);
            CHECK(wrong == 0u);
        }
        CHECK(pool.stats().mTargets == 1u);
        CHECK(pool.stats().mHits == 1u);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }
}
