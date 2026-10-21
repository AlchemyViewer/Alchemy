/**
 * @file altransienttargetpool_gl_test.cpp
 * @brief The transient target pool over LLRenderTarget on the hidden window: a pooled target's GL objects kept across
 *        acquisitions, a shared depth kept attached, and the poison a release leaves under validation.
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

#include "../altransienttargetpool.h"
#include "../altransienttargets.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llglstates.h"
#include "llrender.h"
#include "llrendertarget.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace
{
    using Pool  = ALTransientTargetPool;
    using Depth = ALRenderTargetDesc::Depth;

    constexpr U32 W = 6;
    constexpr U32 H = 5;

    // The scene's G-buffer, as the light map and the water's copy share its depth.
    constexpr ALRenderTargetDesc GBUFFER{ .width = W, .height = H, .color = { GL_RGBA8, GL_RGBA16F },
                                          .depth = Depth::D32F, .stencil = true };
    constexpr ALRenderTargetDesc LIGHT{ .width = W, .height = H, .color = { GL_RGBA16F } };
    // The depth snapshot: depth of its own and no colour.
    constexpr ALRenderTargetDesc DEPTH_COPY{ .width = W, .height = H, .depth = Depth::D32F };
    // Colour and a depth of its own, as SMAA's edges with their stencil.
    constexpr ALRenderTargetDesc OWN_DEPTH{ .width = W, .height = H, .color = { GL_RGBA8 }, .depth = Depth::D24,
                                            .stencil = true };

    struct Names
    {
        U32              framebuffer = 0;
        std::vector<U32> color;
        U32              depth = 0;

        bool operator==(const Names&) const = default;
    };

    Names names_of(LLRenderTarget& target)
    {
        Names names;
        target.bindTarget();
        GLint framebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        target.flush();
        names.framebuffer = (U32)framebuffer;
        for (U32 attachment = 0; attachment < target.getNumTextures(); ++attachment)
        {
            names.color.push_back(target.getTexture(attachment));
        }
        names.depth = target.getDepth();
        return names;
    }

    // What the target's framebuffer has attached at `point`, 0 for nothing.
    U32 attached_at(LLRenderTarget& target, GLenum point)
    {
        target.bindTarget();
        GLint name = -1;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, point, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
        target.flush();
        return (U32)name;
    }

    // Clear what `target` holds of its own: its colour to `colour`, and its depth, shared or not, to `depth`.
    void fill(LLRenderTarget& target, const LLColor4& colour, F64 depth)
    {
        LLGLDepthTest write(GL_TRUE, GL_TRUE);
        LLGLSColorMask mask(true, true);
        const LLColor4 previous = gGL.getClearColor();
        gGL.setClearColor(colour);
        glClearDepth(depth);
        glClearStencil(0);
        target.bindTarget();
        target.clear();
        target.flush();
        gGL.setClearColor(previous);
        glClearDepth(1.0);
    }

    // The colour of attachment 0 at the target's middle, as floats.
    LLColor4 colour_of(LLRenderTarget& target)
    {
        target.bindTarget();
        F32 pixel[4] = { -1.f, -1.f, -1.f, -1.f };
        glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_FLOAT, pixel);
        target.flush();
        return LLColor4(pixel);
    }

    // The depth at the target's middle, read through its own framebuffer: a shared depth reads as its owner's.
    F32 depth_of(LLRenderTarget& target)
    {
        target.bindTarget();
        F32 depth = -1.f;
        glReadPixels(W / 2, H / 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
        target.flush();
        return depth;
    }

    bool close_to(F32 a, F32 b)
    {
        return std::abs(a - b) < 1.e-3f;
    }

    bool is_poison(const LLColor4& colour)
    {
        return close_to(colour.mV[0], Pool::POISON_COLOUR[0]) && close_to(colour.mV[1], Pool::POISON_COLOUR[1]) &&
               close_to(colour.mV[2], Pool::POISON_COLOUR[2]) && close_to(colour.mV[3], Pool::POISON_COLOUR[3]);
    }

    // Puts glClearTexImage back, however the case ends.
    struct ClearTexImageRestore
    {
        PFNGLCLEARTEXIMAGEPROC mSaved = glClearTexImage;
        ~ClearTexImageRestore() { glClearTexImage = mSaved; }
    };

    struct pool_gl_data
    {
        ~pool_gl_data()
        {
            while (LLRenderTarget::getCurrentBoundTarget())
            {
                LLRenderTarget::getCurrentBoundTarget()->flush();
            }
            pool.clear();
            owner.release();
        }

        ll_test::SharedGLScope mGL;
        LLRenderTarget         owner;
        Pool                   pool;
    };

    TEST_CASE_METHOD(pool_gl_data, "altransienttargetpool_gl: a pooled target is the same GL objects at each acquisition",
                     "[altransienttargetpool_gl]")
    {
        for (const ALRenderTargetDesc& desc : { LIGHT, DEPTH_COPY, OWN_DEPTH })
        {
            CAPTURE(desc.colorCount(), (U32)desc.depth);
            LLRenderTarget* first = pool.acquire(desc, "first");
            REQUIRE(first);
            REQUIRE(first->isComplete());
            CHECK(first->desc() == desc);
            const Names names = names_of(*first);
            CHECK(names.framebuffer != 0u);
            CHECK(pool.release(first));

            const U32 bytes = LLRenderTarget::sBytesAllocated;
            LLRenderTarget* again = pool.acquire(desc, "again");
            REQUIRE(again == first);
            UNSCOPED_INFO("nothing built: the names it had, and no byte more");
            CHECK(names_of(*again) == names);
            CHECK(LLRenderTarget::sBytesAllocated == bytes);
            CHECK(again->desc() == desc);
            CHECK(pool.release(again));
        }
        CHECK(pool.stats().mMisses == 3);
        CHECK(pool.stats().mHits == 3);
        CHECK(pool.stats().mBytes ==
              LLRenderTarget::bytesFor(LIGHT) + LLRenderTarget::bytesFor(DEPTH_COPY) + LLRenderTarget::bytesFor(OWN_DEPTH));
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    TEST_CASE_METHOD(pool_gl_data, "altransienttargetpool_gl: a pooled target keeps the depth it shares attached",
                     "[altransienttargetpool_gl]")
    {
        REQUIRE(owner.ensure(GBUFFER) == LLRenderTarget::EnsureResult::Built);

        LLRenderTarget* light = nullptr;
        {
            ALTransientTargets scope(pool);
            light = scope.acquire(LIGHT, owner, "main.light");
            REQUIRE(light);
            CHECK(light->sharesDepth());
            CHECK(attached_at(*light, GL_DEPTH_ATTACHMENT) == owner.getDepth());
            CHECK(attached_at(*light, GL_STENCIL_ATTACHMENT) == owner.getDepth());
            fill(owner, LLColor4::black, 0.25);
            CHECK(close_to(depth_of(*light), 0.25f));
        }

        UNSCOPED_INFO("acquired again, under another name: still attached, and what the owner writes reads through it");
        const Names names = names_of(*light);
        LLRenderTarget* water = pool.acquire(LIGHT, owner, "main.water.refraction");
        REQUIRE(water == light);
        CHECK(names_of(*water) == names);
        CHECK(attached_at(*water, GL_DEPTH_ATTACHMENT) == owner.getDepth());
        fill(owner, LLColor4::black, 0.625);
        CHECK(close_to(depth_of(*water), 0.625f));
        CHECK(pool.release(water));

        UNSCOPED_INFO("the owner's depth built again: the old sharer dropped, and the next attaches the new depth");
        REQUIRE(owner.ensure({ .width = W, .height = H, .color = { GL_RGBA8, GL_RGBA16F }, .depth = Depth::D24,
                               .stencil = true }) == LLRenderTarget::EnsureResult::Built);
        pool.dropStaleDepth();
        CHECK(pool.stats().mTargets == 0);
        LLRenderTarget* rebuilt = pool.acquire(LIGHT, owner, "main.light");
        REQUIRE(rebuilt);
        CHECK(attached_at(*rebuilt, GL_DEPTH_ATTACHMENT) == owner.getDepth());
        fill(owner, LLColor4::black, 0.375);
        CHECK(close_to(depth_of(*rebuilt), 0.375f));
        CHECK(pool.release(rebuilt));
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    TEST_CASE_METHOD(pool_gl_data, "altransienttargetpool_gl: under validation a release poisons what the target holds of its own",
                     "[altransienttargetpool_gl]")
    {
        REQUIRE(owner.ensure(GBUFFER) == LLRenderTarget::EnsureResult::Built);
        pool.setValidation(true);

        ClearTexImageRestore restore;
        for (bool clear_tex_image : { true, false })
        {
            CAPTURE(clear_tex_image);
            if (!clear_tex_image)
            {
                UNSCOPED_INFO("without GL 4.4's glClearTexImage, through each target's framebuffer");
                glClearTexImage = nullptr;
            }

            LLRenderTarget* sharer = pool.acquire(LIGHT, owner, "main.light");
            LLRenderTarget* own    = pool.acquire(OWN_DEPTH, "smaa.edges");
            LLRenderTarget* depth  = pool.acquire(DEPTH_COPY, "main.depth.copy");
            REQUIRE(sharer);
            REQUIRE(own);
            REQUIRE(depth);
            fill(owner, LLColor4::black, 0.25);
            fill(*sharer, LLColor4(0.f, 1.f, 0.f, 1.f), 0.25);
            fill(*own, LLColor4(0.f, 0.f, 1.f, 1.f), 0.75);
            fill(*depth, LLColor4::black, 0.875);
            REQUIRE(!is_poison(colour_of(*sharer)));

            const bool scissor = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
            bool mask[4];
            gGL.getColorMask(mask);
            for (LLRenderTarget* target : { sharer, own, depth })
            {
                CHECK(pool.release(target));
            }

            CHECK(is_poison(colour_of(*sharer)));
            CHECK(is_poison(colour_of(*own)));
            CHECK(close_to(depth_of(*own), Pool::POISON_DEPTH));
            CHECK(close_to(depth_of(*depth), Pool::POISON_DEPTH));
            UNSCOPED_INFO("the depth the sharer shares is the owner's, and keeps what the owner wrote");
            CHECK(close_to(depth_of(owner), 0.25f));

            UNSCOPED_INFO("the state the poison changes is put back");
            CHECK((glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE) == scissor);
            bool after[4];
            gGL.getColorMask(after);
            CHECK(std::equal(std::begin(mask), std::end(mask), std::begin(after)));
            CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
        }

        UNSCOPED_INFO("without validation a release leaves the contents be");
        pool.setValidation(false);
        LLRenderTarget* kept = pool.acquire(OWN_DEPTH, "smaa.edges");
        REQUIRE(kept);
        fill(*kept, LLColor4(0.f, 0.f, 1.f, 1.f), 0.75);
        CHECK(pool.release(kept));
        CHECK(!is_poison(colour_of(*kept)));
        CHECK(close_to(depth_of(*kept), 0.75f));
    }
}
