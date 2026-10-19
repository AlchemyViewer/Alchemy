/**
 * @file altransienttargetpool_test.cpp
 * @brief The transient target pool's policy, over a backing that records what GL would be asked: reuse by exact
 *        description, scopes, the trim, the drop on a size change and the counts.
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

#include "llglheaders.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
    using Pool  = ALTransientTargetPool;
    using Depth = ALRenderTargetDesc::Depth;

    constexpr ALRenderTargetDesc LIGHT{ .width = 64, .height = 32, .color = { GL_RGBA16F } };
    constexpr ALRenderTargetDesc POST{ .width = 64, .height = 32, .color = { GL_RGB10_A2 } };
    constexpr ALRenderTargetDesc HALF{ .width = 32, .height = 16, .color = { GL_R11F_G11F_B10F } };
    constexpr ALRenderTargetDesc DEPTH{ .width = 64, .height = 32, .depth = Depth::D32F };
    constexpr ALRenderTargetDesc OWN_STENCIL{ .width = 64, .height = 32, .color = { GL_RGBA8 }, .depth = Depth::D24,
                                              .stencil = true };

    // What GL would have been asked: each build, rename, poison and free, by the target it was asked of.
    struct Recorder final : Pool::Backing
    {
        bool build(LLRenderTarget& target, const Pool::Key& key, std::string_view name) override
        {
            builds.push_back({ &target, key, std::string(name) });
            return !refuse;
        }
        void rename(LLRenderTarget& target, std::string_view name) override { renames[&target] = std::string(name); }
        void poison(LLRenderTarget& target, const Pool::Key& key) override { poisoned.push_back({ &target, key, {} }); }
        void free(LLRenderTarget& target) override { freed.push_back(&target); }
        U64  depthSerial(const LLRenderTarget& owner) const override
        {
            const auto found = serials.find(&owner);
            return found != serials.end() ? found->second : 1;
        }

        struct Call
        {
            LLRenderTarget* target;
            Pool::Key       key;
            std::string     name;
        };
        std::vector<Call>                           builds;
        std::vector<Call>                           poisoned;
        std::vector<LLRenderTarget*>                freed;
        std::map<LLRenderTarget*, std::string>      renames;
        std::map<const LLRenderTarget*, U64>        serials;
        bool                                        refuse = false;
    };

    struct pool_data
    {
        Recorder       backing;
        Pool           pool{ backing };
        LLRenderTarget gbuffer;
        LLRenderTarget other_gbuffer;
    };

    TEST_CASE_METHOD(pool_data, "altransienttargetpool: a released target goes to the next acquisition of its exact description",
                     "[altransienttargetpool]")
    {
        LLRenderTarget* light = pool.acquire(LIGHT, "main.light");
        REQUIRE(light);
        CHECK(pool.release(light));

        UNSCOPED_INFO("the same description, under another name: the same target, built once");
        LLRenderTarget* water = pool.acquire(LIGHT, "main.water.refraction");
        CHECK(water == light);
        CHECK(backing.builds.size() == 1);
        CHECK(pool.stats().mHits == 1);
        CHECK(pool.stats().mMisses == 1);

        UNSCOPED_INFO("while it is held, another of the description is another target");
        LLRenderTarget* scratch = pool.acquire(LIGHT, "main.light.scratch");
        REQUIRE(scratch);
        CHECK(scratch != water);
        CHECK(pool.release(scratch));
        CHECK(pool.release(water));

        UNSCOPED_INFO("a format, a size or a depth apart is never the same target");
        LLRenderTarget* post  = pool.acquire(POST, "main.post.ping");
        LLRenderTarget* half  = pool.acquire(HALF, "main.bloom.mip1");
        LLRenderTarget* depth = pool.acquire(DEPTH, "main.depth.copy");
        for (LLRenderTarget* target : { post, half, depth })
        {
            REQUIRE(target);
            CHECK(target != light);
            CHECK(target != scratch);
        }
        CHECK(post != half);
        CHECK(half != depth);

        UNSCOPED_INFO("nor is a target over another depth, or over none");
        LLRenderTarget* over_gbuffer = pool.acquire(LIGHT, gbuffer, "main.light");
        LLRenderTarget* over_other   = pool.acquire(LIGHT, other_gbuffer, "probe.light");
        REQUIRE(over_gbuffer);
        REQUIRE(over_other);
        CHECK(over_gbuffer != light);
        CHECK(over_gbuffer != scratch);
        CHECK(over_other != over_gbuffer);
        CHECK(backing.builds.back().key.mDepthFrom == &other_gbuffer);

        UNSCOPED_INFO("both held, then both free: the next two of the description take the two there are");
        for (LLRenderTarget* target : { post, half, depth, over_gbuffer, over_other })
        {
            CHECK(pool.release(target));
        }
        const size_t built = backing.builds.size();
        LLRenderTarget* first  = pool.acquire(LIGHT, "main.light");
        LLRenderTarget* second = pool.acquire(LIGHT, "main.light.scratch");
        CHECK(backing.builds.size() == built);
        CHECK(first != second);
        CHECK(((first == light && second == scratch) || (first == scratch && second == light)));
        CHECK(pool.release(first));
        CHECK(pool.release(second));

        UNSCOPED_INFO("a target over a depth built again since is a description no acquisition can match");
        backing.serials[&gbuffer] = 2;
        LLRenderTarget* rebuilt = pool.acquire(LIGHT, gbuffer, "main.light");
        CHECK(rebuilt != over_gbuffer);
        CHECK(backing.builds.size() == built + 1);
        CHECK(backing.builds.back().key.mDepthSerial == 2);
        CHECK(pool.release(rebuilt));
        CHECK(backing.freed.empty());
    }

    TEST_CASE_METHOD(pool_data, "altransienttargetpool: a scope gives back what it took, and a release twice or a leak is caught",
                     "[altransienttargetpool]")
    {
        LLRenderTarget* kept = nullptr;
        {
            ALTransientTargets scope(pool);
            kept = scope.acquire(LIGHT, "main.light");
            LLRenderTarget* post  = scope.acquire(POST, "main.post.ping");
            LLRenderTarget* depth = scope.acquire(DEPTH, gbuffer, "depth.shared");
            REQUIRE(kept);
            REQUIRE(post);
            REQUIRE(depth);
            CHECK(scope.holds(post));

            UNSCOPED_INFO("released early: the pool's again within the scope");
            CHECK(scope.release(post));
            CHECK_FALSE(scope.holds(post));
            CHECK(pool.acquire(POST, "main.post.pong") == post);
            CHECK(pool.release(post));

            UNSCOPED_INFO("released twice through the scope: refused and counted");
            CHECK_FALSE(scope.release(post));
            CHECK(pool.badReleases() == 1);
        }
        UNSCOPED_INFO("the scope's end gave the rest back");
        CHECK(pool.acquire(LIGHT, "main.light") == kept);
        CHECK(pool.release(kept));

        UNSCOPED_INFO("released twice through the pool, and a release of what it never lent: refused and counted");
        CHECK_FALSE(pool.release(kept));
        LLRenderTarget stranger;
        CHECK_FALSE(pool.release(&stranger));
        CHECK_FALSE(pool.release(nullptr));
        CHECK(pool.badReleases() == 4);

        UNSCOPED_INFO("a scope cannot give back what another scope holds");
        {
            ALTransientTargets holder(pool);
            ALTransientTargets other(pool);
            LLRenderTarget* held = holder.acquire(LIGHT, "main.light");
            CHECK_FALSE(other.release(held));
            CHECK(pool.badReleases() == 5);
            CHECK(holder.holds(held));
        }

        UNSCOPED_INFO("a target held past the end of a frame: a leak, counted and taken back at the next");
        LLRenderTarget* leaked = pool.acquire(LIGHT, "main.light");
        REQUIRE(leaked);
        pool.beginFrame();
        CHECK(pool.leaks() == 1);
        CHECK(pool.acquire(LIGHT, "main.light") == leaked);
        CHECK(pool.release(leaked));
        pool.beginFrame();
        CHECK(pool.leaks() == 1);
        CHECK(pool.badReleases() == 5);

        UNSCOPED_INFO("a scope's target taken back as a leak and lent again: the scope's release is refused, and it stays its "
                      "new holder's");
        LLRenderTarget* lost = nullptr;
        {
            ALTransientTargets holder(pool);
            {
                ALTransientTargets leaky(pool);
                lost = leaky.acquire(POST, "main.post.ping");
                REQUIRE(lost);
                pool.beginFrame();
                CHECK(pool.leaks() == 2);
                CHECK(holder.acquire(POST, "main.post.pong") == lost);
            }
            CHECK(pool.badReleases() == 6);
            CHECK(holder.holds(lost));
            LLRenderTarget* other = pool.acquire(POST, "main.post.ping");
            CHECK(other != lost);
            CHECK(pool.release(other));
        }
        UNSCOPED_INFO("given back by its new holder");
        CHECK(pool.badReleases() == 6);
        CHECK(pool.lease(lost) == 0);
    }

    TEST_CASE_METHOD(pool_data, "altransienttargetpool: a target no one acquires for 120 frames is freed", "[altransienttargetpool]")
    {
        REQUIRE(Pool::TRIM_FRAMES == 120);

        LLRenderTarget* idle = pool.acquire(LIGHT, "main.light");
        CHECK(pool.release(idle));
        LLRenderTarget* busy = pool.acquire(POST, "main.post.ping");
        CHECK(pool.release(busy));

        // The frame the two were used in, then 120 more; the busy one acquired in each.
        for (U32 frame = 1; frame <= Pool::TRIM_FRAMES; ++frame)
        {
            pool.beginFrame();
            CHECK(pool.acquire(POST, "main.post.ping") == busy);
            CHECK(pool.release(busy));
        }
        UNSCOPED_INFO("120 whole frames gone by unused: one short of the start of the next");
        CHECK(pool.stats().mTargets == 2);
        CHECK(backing.freed.empty());

        pool.beginFrame();
        CHECK(pool.stats().mTargets == 1);
        CHECK(pool.stats().mTrimmed == 1);
        REQUIRE(backing.freed.size() == 1);
        CHECK(backing.freed[0] == idle);
        CHECK(pool.stats().mBytes == LLRenderTarget::bytesFor(POST));

        UNSCOPED_INFO("two of a description where the frame now needs one: the one left unused goes");
        LLRenderTarget* a = pool.acquire(LIGHT, "main.light");
        LLRenderTarget* b = pool.acquire(LIGHT, "main.light.scratch");
        CHECK(pool.release(a));
        CHECK(pool.release(b));
        LLRenderTarget* used = nullptr;
        for (U32 frame = 0; frame <= Pool::TRIM_FRAMES; ++frame)
        {
            pool.beginFrame();
            used = pool.acquire(LIGHT, "main.light");
            CHECK(pool.release(used));
        }
        UNSCOPED_INFO("the post target, unused since the loop above, went too");
        CHECK(pool.stats().mTargets == 1);
        CHECK(backing.freed.back() == (used == a ? b : a));
        CHECK(pool.acquire(LIGHT, "main.light") == used);
        CHECK(pool.release(used));
    }

    TEST_CASE_METHOD(pool_data, "altransienttargetpool: an acquisition may keep its target unused for longer than 120 frames",
                     "[altransienttargetpool]")
    {
        constexpr U32 KEEP = 300;

        LLRenderTarget* kept = pool.acquire(LIGHT, "preview", KEEP);
        CHECK(pool.release(kept));
        LLRenderTarget* normal = pool.acquire(POST, "main.post.ping");
        CHECK(pool.release(normal));
        UNSCOPED_INFO("an ask under 120 frames keeps it 120");
        LLRenderTarget* short_ask = pool.acquire(HALF, "short", 10);
        CHECK(pool.release(short_ask));

        for (U32 frame = 1; frame <= Pool::TRIM_FRAMES; ++frame)
        {
            pool.beginFrame();
        }
        CHECK(pool.stats().mTargets == 3);

        UNSCOPED_INFO("121 frames unused: the two kept 120 go, the one kept 300 stays");
        pool.beginFrame();
        CHECK(pool.stats().mTrimmed == 2);
        CHECK(pool.stats().mTargets == 1);
        CHECK(std::find(backing.freed.begin(), backing.freed.end(), kept) == backing.freed.end());

        for (U32 frame = Pool::TRIM_FRAMES + 2; frame <= KEEP; ++frame)
        {
            pool.beginFrame();
        }
        UNSCOPED_INFO("300 whole frames unused: still kept, and found without a build");
        CHECK(pool.stats().mTargets == 1);
        CHECK(backing.freed.size() == 2);
        CHECK(pool.acquire(LIGHT, "preview") == kept);
        CHECK(pool.release(kept));
        CHECK(backing.builds.size() == 3);

        UNSCOPED_INFO("taken again without asking: the most any acquisition of it asked still holds");
        for (U32 frame = 1; frame <= KEEP; ++frame)
        {
            pool.beginFrame();
        }
        CHECK(pool.stats().mTargets == 1);
        pool.beginFrame();
        CHECK(pool.stats().mTargets == 0);
        CHECK(pool.stats().mTrimmed == 1);
        CHECK(backing.freed.back() == kept);
    }

    // A Keep makes every acquisition while it lives ask for at least its frames, the asks of callees that know nothing
    // of it included; one inside another keeps the larger, and each puts back what it found.
    TEST_CASE_METHOD(pool_data, "altransienttargetpool: a keep scope keeps what is acquired within it", "[altransienttargetpool]")
    {
        constexpr U32 KEEP = 300;

        LLRenderTarget* kept = nullptr;
        LLRenderTarget* nested = nullptr;
        {
            const Pool::Keep keep(pool, KEEP);
            kept = pool.acquire(LIGHT, "preview.post");
            CHECK(pool.release(kept));
            {
                const Pool::Keep shorter(pool, 10);
                nested = pool.acquire(HALF, "preview.bloom");
                CHECK(pool.release(nested));
            }
        }
        LLRenderTarget* after = pool.acquire(POST, "main.post.ping");
        CHECK(pool.release(after));

        for (U32 frame = 1; frame <= Pool::TRIM_FRAMES + 1; ++frame)
        {
            pool.beginFrame();
        }
        UNSCOPED_INFO("121 frames unused: the one acquired after the scope goes, the two within it stay");
        CHECK(pool.stats().mTargets == 2);
        REQUIRE(backing.freed.size() == 1);
        CHECK(backing.freed[0] == after);

        for (U32 frame = Pool::TRIM_FRAMES + 2; frame <= KEEP + 1; ++frame)
        {
            pool.beginFrame();
        }
        UNSCOPED_INFO("301 frames unused: they go too");
        CHECK(pool.stats().mTargets == 0);
        CHECK(std::find(backing.freed.begin(), backing.freed.end(), kept) != backing.freed.end());
        CHECK(std::find(backing.freed.begin(), backing.freed.end(), nested) != backing.freed.end());
    }

    // The login's frames acquire nothing: held, the pool keeps what was readied through as many of them as there are,
    // and from the hold's end a target goes TRIM_FRAMES whole frames unacquired before it is freed. A leak is still
    // taken back, and a size change still frees everything.
    TEST_CASE_METHOD(pool_data, "altransienttargetpool: nothing is trimmed while trimming is held", "[altransienttargetpool]")
    {
        LLRenderTarget* readied = pool.acquire(LIGHT, "main.light");
        CHECK(pool.release(readied));

        pool.holdTrimming(true);
        for (U32 frame = 1; frame <= 4 * Pool::TRIM_FRAMES; ++frame)
        {
            pool.beginFrame();
        }
        CHECK(pool.stats().mTargets == 1);
        CHECK(backing.freed.empty());

        LLRenderTarget* leaked = pool.acquire(POST, "main.post.ping");
        REQUIRE(leaked);
        pool.beginFrame();
        CHECK(pool.leaks() == 1);
        CHECK(pool.acquire(POST, "main.post.ping") == leaked);
        CHECK(pool.release(leaked));

        pool.holdTrimming(false);
        for (U32 frame = 1; frame <= Pool::TRIM_FRAMES; ++frame)
        {
            pool.beginFrame();
        }
        UNSCOPED_INFO("120 whole frames unused since the hold ended: kept");
        CHECK(pool.stats().mTargets == 2);
        CHECK(backing.freed.empty());
        pool.beginFrame();
        CHECK(pool.stats().mTargets == 0);
        CHECK(backing.freed.size() == 2);

        UNSCOPED_INFO("held, a change of the main size still frees everything");
        pool.holdTrimming(true);
        pool.setMainSize(2560, 1440);
        readied = pool.acquire(LIGHT, "main.light");
        CHECK(pool.release(readied));
        pool.setMainSize(1280, 720);
        pool.beginFrame();
        CHECK(pool.stats().mTargets == 0);
    }

    // A description its users will not ask for again is freed at once: every free target of it, and none held, and none
    // of the same description sharing a depth.
    TEST_CASE_METHOD(pool_data, "altransienttargetpool: trimming a description frees its free targets now", "[altransienttargetpool]")
    {
        LLRenderTarget* free_a = pool.acquire(HALF, "dof.blur");
        LLRenderTarget* free_b = pool.acquire(HALF, "dof.blur");
        LLRenderTarget* held   = pool.acquire(HALF, "dof.blur");
        LLRenderTarget* other  = pool.acquire(POST, "main.post.ping");
        LLRenderTarget* shared = pool.acquire(LIGHT, gbuffer, "main.light");
        REQUIRE((free_a && free_b && held && other && shared));
        for (LLRenderTarget* target : { free_a, free_b, other, shared })
        {
            CHECK(pool.release(target));
        }
        CHECK(pool.stats().mTargets == 5);

        pool.trim(HALF);
        CHECK(pool.stats().mTargets == 3);
        CHECK(pool.stats().mTrimmed == 2);
        CHECK(std::find(backing.freed.begin(), backing.freed.end(), free_a) != backing.freed.end());
        CHECK(std::find(backing.freed.begin(), backing.freed.end(), free_b) != backing.freed.end());
        UNSCOPED_INFO("the held one is its holder's, and is given back as ever");
        CHECK(pool.release(held));
        CHECK(pool.acquire(HALF, "dof.blur") == held);
        CHECK(pool.release(held));

        UNSCOPED_INFO("a description shared over a depth is another key");
        pool.trim(ALRenderTargetDesc{ .width = LIGHT.width, .height = LIGHT.height, .color = LIGHT.color });
        CHECK(pool.stats().mTargets == 3);
    }

    TEST_CASE_METHOD(pool_data, "altransienttargetpool: a change of the main size frees everything", "[altransienttargetpool]")
    {
        pool.setMainSize(2560, 1440);
        LLRenderTarget* light = pool.acquire(LIGHT, "main.light");
        LLRenderTarget* post  = pool.acquire(POST, "main.post.ping");
        CHECK(pool.release(light));
        CHECK(pool.release(post));

        UNSCOPED_INFO("the same size again keeps them");
        pool.setMainSize(2560, 1440);
        CHECK(pool.stats().mTargets == 2);
        CHECK(backing.freed.empty());

        UNSCOPED_INFO("a target held across the change is freed as it is released, and never lent again");
        ALTransientTargets scope(pool);
        LLRenderTarget* held = scope.acquire(HALF, "sky.behind");
        REQUIRE(held);
        pool.setMainSize(1920, 1080);
        CHECK(pool.stats().mTargets == 1);
        CHECK(backing.freed.size() == 2);
        LLRenderTarget* after = pool.acquire(HALF, "main.bloom.mip1");
        CHECK(after != held);
        CHECK(pool.release(after));
        CHECK(scope.release(held));
        CHECK(backing.freed.back() == held);
        CHECK(pool.stats().mTargets == 1);
        CHECK(pool.stats().mBytes == LLRenderTarget::bytesFor(HALF));

        UNSCOPED_INFO("so does a clear");
        pool.clear();
        CHECK(pool.stats().mTargets == 0);
        CHECK(pool.stats().mBytes == 0);
    }

    TEST_CASE_METHOD(pool_data, "altransienttargetpool: only the targets over a depth built again are dropped as stale",
                     "[altransienttargetpool]")
    {
        LLRenderTarget* over  = pool.acquire(LIGHT, gbuffer, "main.light");
        LLRenderTarget* other = pool.acquire(LIGHT, other_gbuffer, "probe.light");
        LLRenderTarget* own   = pool.acquire(LIGHT, "dof.sharp");
        for (LLRenderTarget* target : { over, other, own })
        {
            CHECK(pool.release(target));
        }

        pool.dropStaleDepth();
        CHECK(pool.stats().mTargets == 3);

        backing.serials[&gbuffer] = 7;
        pool.dropStaleDepth();
        CHECK(pool.stats().mTargets == 2);
        REQUIRE(backing.freed.size() == 1);
        CHECK(backing.freed[0] == over);
        CHECK(pool.acquire(LIGHT, other_gbuffer, "probe.light") == other);
        CHECK(pool.acquire(LIGHT, "dof.sharp") == own);
    }

    TEST_CASE_METHOD(pool_data, "altransienttargetpool: a pooled target freed takes the targets sharing its depth with it",
                     "[altransienttargetpool]")
    {
        // SMAA's blend weights share the stencil of its edges, both of them the pool's.
        LLRenderTarget* edges = pool.acquire(OWN_STENCIL, "smaa.edges");
        REQUIRE(edges);
        LLRenderTarget* blend = pool.acquire(POST, *edges, "smaa.blend");
        REQUIRE(blend);
        CHECK(pool.release(blend));
        CHECK(pool.release(edges));

        UNSCOPED_INFO("the blend acquired each frame and the edges not: the edges trimmed, and the blend with them");
        for (U32 frame = 1; frame <= Pool::TRIM_FRAMES; ++frame)
        {
            pool.beginFrame();
            CHECK(pool.acquire(POST, *edges, "smaa.blend") == blend);
            CHECK(pool.release(blend));
        }
        pool.beginFrame();
        CHECK(pool.stats().mTargets == 0);
        CHECK(pool.stats().mTrimmed == 2);
        CHECK(pool.stats().mBytes == 0);
        CHECK(backing.freed.size() == 2);

        UNSCOPED_INFO("a sharer held as its owner goes is freed as it is released, and never matched again");
        edges = pool.acquire(OWN_STENCIL, "smaa.edges");
        REQUIRE(edges);
        CHECK(pool.release(edges));
        blend = pool.acquire(POST, *edges, "smaa.blend");
        REQUIRE(blend);
        pool.clear();
        CHECK(pool.stats().mTargets == 1);
        CHECK(pool.release(blend));
        CHECK(pool.stats().mTargets == 0);
        CHECK(backing.freed.back() == blend);
    }

    TEST_CASE_METHOD(pool_data, "altransienttargetpool: the counts say what the pool holds and what each frame asked of it",
                     "[altransienttargetpool]")
    {
        const U64 light = LLRenderTarget::bytesFor(LIGHT);
        const U64 post  = LLRenderTarget::bytesFor(POST);
        REQUIRE(light == 64 * 32 * 8);
        REQUIRE(post == 64 * 32 * 4);
        UNSCOPED_INFO("a depth of its own counts; a shared one is its owner's");
        CHECK(LLRenderTarget::bytesFor(DEPTH) == 64 * 32 * 4);
        CHECK(LLRenderTarget::bytesFor({ .width = 4, .height = 2, .color = { GL_RGBA8 }, .depth = Depth::D32F, .stencil = true }) ==
              4 * 2 * (4 + 8));

        LLRenderTarget* a = pool.acquire(LIGHT, "main.light");
        LLRenderTarget* b = pool.acquire(LIGHT, gbuffer, "main.water.refraction");
        LLRenderTarget* c = pool.acquire(POST, "main.post.ping");
        CHECK(pool.release(a));
        LLRenderTarget* d = pool.acquire(LIGHT, "main.light.scratch");
        CHECK(d == a);
        Pool::Stats stats = pool.stats();
        CHECK(stats.mBytes == 2 * light + post);
        CHECK(stats.mTargets == 3);
        CHECK(stats.mHits == 1);
        CHECK(stats.mMisses == 3);
        CHECK(stats.mFailures == 0);

        UNSCOPED_INFO("a description the backing cannot build: null, counted, and nothing held");
        backing.refuse = true;
        CHECK(pool.acquire(HALF, "main.bloom.mip1") == nullptr);
        backing.refuse = false;
        CHECK(pool.stats().mFailures == 1);
        CHECK(pool.stats().mTargets == 3);
        CHECK(pool.stats().mBytes == 2 * light + post);

        for (LLRenderTarget* target : { b, c, d })
        {
            CHECK(pool.release(target));
        }

        UNSCOPED_INFO("the next frame starts its counts again, and keeps the last frame's");
        pool.beginFrame();
        CHECK(pool.lastFrame().mHits == 1);
        CHECK(pool.lastFrame().mMisses == 3);
        CHECK(pool.lastFrame().mFailures == 1);
        CHECK(pool.lastFrame().mBytes == 2 * light + post);
        stats = pool.stats();
        CHECK(stats.mHits == 0);
        CHECK(stats.mMisses == 0);
        CHECK(stats.mBytes == 2 * light + post);
        CHECK(pool.frame() == 1);
    }

    TEST_CASE_METHOD(pool_data, "altransienttargetpool: under validation a release poisons the target and an acquisition names it",
                     "[altransienttargetpool]")
    {
        LLRenderTarget* light = pool.acquire(LIGHT, gbuffer, "main.light");
        CHECK(pool.release(light));
        CHECK(backing.poisoned.empty());
        UNSCOPED_INFO("without validation a target keeps the name it was built under");
        CHECK(pool.acquire(LIGHT, gbuffer, "main.water.refraction") == light);
        CHECK(backing.renames.empty());
        CHECK(pool.release(light));

        pool.setValidation(true);
        CHECK(pool.acquire(LIGHT, gbuffer, "main.water.refraction") == light);
        CHECK(backing.renames[light] == "main.water.refraction");
        CHECK(pool.release(light));
        REQUIRE(backing.poisoned.size() == 1);
        CHECK(backing.poisoned[0].target == light);
        UNSCOPED_INFO("the poison is told the depth is shared, so it leaves the owner's alone");
        CHECK(backing.poisoned[0].key.mDepthFrom == &gbuffer);

        UNSCOPED_INFO("a refused release poisons nothing");
        CHECK_FALSE(pool.release(light));
        CHECK(backing.poisoned.size() == 1);
    }

    TEST_CASE_METHOD(pool_data, "altransienttargetpool: a recorded frame lists the names each target served", "[altransienttargetpool]")
    {
        LLRenderTarget* light = pool.acquire(LIGHT, gbuffer, "main.light");
        CHECK(pool.release(light));
        pool.beginFrame();
        CHECK_FALSE(pool.namesRecorded());

        UNSCOPED_INFO("asked for in a frame, the names are recorded over the whole frame after it");
        pool.recordNames();
        CHECK(pool.release(pool.acquire(LIGHT, gbuffer, "probe.light")));
        pool.beginFrame();
        CHECK_FALSE(pool.namesRecorded());
        CHECK(pool.release(pool.acquire(LIGHT, gbuffer, "main.light")));
        CHECK(pool.release(pool.acquire(LIGHT, gbuffer, "main.water.refraction")));
        CHECK(pool.release(pool.acquire(POST, "main.post.ping")));
        pool.beginFrame();
        CHECK(pool.namesRecorded());
        const std::string described = pool.describe();
        CHECK(described.find("64x32, 0.0 MiB, shared depth: main.light, main.water.refraction") != std::string::npos);
        CHECK(described.find("64x32, 0.0 MiB: main.post.ping") != std::string::npos);

        pool.beginFrame();
        CHECK_FALSE(pool.namesRecorded());
    }
}
