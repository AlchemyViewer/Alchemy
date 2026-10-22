/**
 * @file alrenderresourceplan_test.cpp
 * @brief What each render resource is for a set of inputs, and what each input changes.
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

#include "../alrenderresourceplan.h"
#include "../altransienttargetpool.h"
#include "../altransienttargets.h"

#include "llrendertarget.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using Plan   = ALRenderResourcePlan;
    using Inputs = ALRenderResourceInputs;
    using Id     = Plan::Id;
    using IdSet  = Plan::IdSet;
    using Pack   = Plan::Pack;
    using Slot   = Plan::Slot;
    using Depth  = ALRenderTargetDesc::Depth;

    // The reference configuration: HDR, reverse-Z, the emissive buffer, bloom at full scale with five mips, shadow
    // detail 2, SSAO, transparent water, reflection probes; SSR, FSAA and DoF off. Mirrors on, so the hero pack is there
    // to be changed or left alone.
    Inputs defaults()
    {
        Inputs in;
        in.mWidth   = 2560;
        in.mHeight  = 1440;
        in.mMirrors = true;
        return in;
    }

    // The ids in a set, by name, so a failed comparison says which resources differ.
    std::string names(const IdSet& ids)
    {
        std::string list;
        for (size_t i = 0; i < Plan::ID_COUNT; ++i)
        {
            if (ids.test(i))
            {
                if (!list.empty())
                {
                    list += ", ";
                }
                list += Plan::name((Id)i);
            }
        }
        return list.empty() ? std::string("nothing") : list;
    }

    IdSet of(std::initializer_list<Id> ids)
    {
        IdSet set;
        for (Id id : ids)
        {
            set.set((size_t)id);
        }
        return set;
    }

    IdSet in_packs(std::initializer_list<Pack> packs, std::initializer_list<Slot> slots)
    {
        IdSet set;
        for (Pack pack : packs)
        {
            for (Slot slot : slots)
            {
                set.set((size_t)Plan::inPack(pack, slot));
            }
        }
        return set;
    }

    IdSet sun_shadows(std::initializer_list<Pack> packs)
    {
        return in_packs(packs, { Slot::Sun0, Slot::Sun1, Slot::Sun2, Slot::Sun3 });
    }

    IdSet spot_shadows()
    {
        return of({ Id::SpotShadow0, Id::SpotShadow1 });
    }

    IdSet bloom(std::initializer_list<Pack> packs)
    {
        return in_packs(packs, { Slot::Bloom0, Slot::Bloom1, Slot::Bloom2, Slot::Bloom3, Slot::Bloom4, Slot::Bloom5,
                                 Slot::Bloom6 });
    }

    // Those of @a ids that @a plan wants.
    IdSet wanted(const Plan& plan, const IdSet& ids)
    {
        return Plan::idsWhere([&](Id id) { return ids.test((size_t)id) && plan.wants(id); });
    }

    IdSet render_sized()
    {
        return Plan::idsWhere(Plan::followsRenderSize);
    }

    IdSet preview_targets()
    {
        return Plan::idsWhere(Plan::forMaterialPreview);
    }

    // A shadow map's edge as the shadow blur needs it: the size times its scale plus 16, to a multiple of 16.
    U32 blur_happy(U32 size, F32 scale)
    {
        return (U32)((F32)size * scale + 16.f) & ~0xFu;
    }

    // One setting of plan section 1.2: the configuration it is changed from, the change, and exactly the resources
    // whose description that change must alter, given the plan before it.
    struct Row
    {
        const char*                       setting;
        std::function<void(Inputs&)>      from;
        std::function<void(Inputs&)>      change;
        std::function<IdSet(const Plan&)> expected;
    };

    void no_change(Inputs&) {}

    IdSet nothing(const Plan&)
    {
        return {};
    }

    std::vector<Row> setting_rows()
    {
        const auto hdr_off = [](Inputs& in)
        {
            in.mHDR           = false;
            in.mGBufferNormHDR = false;
        };
        const auto previewed = [](Inputs& in) { in.mMaterialPreview = true; };

        return {
            { "RenderTransparentWater", no_change, [](Inputs& in) { in.mTransparentWater = false; },
              [](const Plan&) { return in_packs({ Pack::Main, Pack::Hero }, { Slot::Water, Slot::DepthCopy }); } },
            // Without a texture barrier the haze passes read the depth copy, so opaque water keeps it.
            { "RenderTransparentWater without a texture barrier", [](Inputs& in) { in.mTextureBarrier = false; },
              [](Inputs& in) { in.mTransparentWater = false; },
              [](const Plan&) { return in_packs({ Pack::Main, Pack::Hero }, { Slot::Water }); } },
            // A probe's water refracts nothing, so its pack takes the copy only for the haze.
            { "a context without a texture barrier", no_change, [](Inputs& in) { in.mTextureBarrier = false; },
              [](const Plan&) { return of({ Id::ProbeDepthCopy }); } },
            { "a context without a texture barrier under opaque water", [](Inputs& in) { in.mTransparentWater = false; },
              [](Inputs& in) { in.mTextureBarrier = false; },
              [](const Plan&) { return in_packs({ Pack::Main, Pack::Probe, Pack::Hero }, { Slot::DepthCopy }); } },
            { "RenderFSAAType to FXAA", no_change, [](Inputs& in) { in.mFSAAType = 1; }, nothing },
            { "RenderFSAAType to SMAA", no_change, [](Inputs& in) { in.mFSAAType = 2; },
              [](const Plan&)
              { return of({ Id::SMAAEdges, Id::SMAABlend, Id::SMAAArea, Id::SMAASearch, Id::SMAASample }); } },
            { "RenderFSAAType from SMAA to FXAA", [](Inputs& in) { in.mFSAAType = 2; },
              [](Inputs& in) { in.mFSAAType = 1; },
              [](const Plan&)
              { return of({ Id::SMAAEdges, Id::SMAABlend, Id::SMAAArea, Id::SMAASearch, Id::SMAASample }); } },
            { "RenderSMAAUseStencil under SMAA", [](Inputs& in) { in.mFSAAType = 2; },
              [](Inputs& in) { in.mSMAAStencil = false; },
              [](const Plan&) { return of({ Id::SMAAEdges, Id::SMAABlend }); } },
            { "RenderSMAAUseStencil without SMAA", no_change, [](Inputs& in) { in.mSMAAStencil = false; }, nothing },
            { "RenderGlowResolutionPow under HDR", no_change, [](Inputs& in) { in.mGlowResolutionPow = 8; }, nothing },
            { "RenderGlowResolutionPow without HDR", hdr_off, [](Inputs& in) { in.mGlowResolutionPow = 8; },
              [](const Plan&) { return of({ Id::Glow0, Id::Glow1, Id::Glow2 }); } },
            { "RenderGlowHDR under HDR", no_change, [](Inputs& in) { in.mGlowHDR = true; }, nothing },
            { "RenderGlowHDR without HDR", hdr_off, [](Inputs& in) { in.mGlowHDR = true; },
              [](const Plan&) { return of({ Id::Glow0, Id::Glow1, Id::Glow2 }); } },
            { "RenderBloomMipCount", no_change, [](Inputs& in) { in.mBloomMips = 4; },
              [](const Plan&) { return of({ Id::MainBloom4 }); } },
            { "RenderBloomMipCount under a material preview", previewed, [](Inputs& in) { in.mBloomMips = 4; },
              [](const Plan&) { return of({ Id::MainBloom4, Id::ProbeBloom4 }); } },
            { "RenderBloomResolutionScale", no_change, [](Inputs& in) { in.mBloomScale = 2; },
              [](const Plan& before) { return wanted(before, bloom({ Pack::Main })); } },
            { "RenderBloomResolutionScale under a material preview", previewed, [](Inputs& in) { in.mBloomScale = 2; },
              [](const Plan& before) { return wanted(before, bloom({ Pack::Main, Pack::Probe })); } },
            { "RenderBloomHalation", no_change, [](Inputs& in) { in.mBloomHalation = true; },
              [](const Plan& before) { return wanted(before, bloom({ Pack::Main })); } },
            { "RenderEnableEmissiveBuffer", no_change, [](Inputs& in) { in.mEmissive = false; },
              [](const Plan&) { return of({ Id::MainGBuffer, Id::ProbeGBuffer, Id::HeroGBuffer, Id::SkyBehind }); } },
            { "RenderHDREnabled", no_change, hdr_off,
              [](const Plan& before)
              {
                  return in_packs({ Pack::Main, Pack::Probe, Pack::Hero }, { Slot::GBuffer }) |
                         in_packs({ Pack::Main, Pack::Hero }, { Slot::Water }) |
                         in_packs({ Pack::Main }, { Slot::PostPing, Slot::PostPong }) |
                         wanted(before, bloom({ Pack::Main })) |
                         of({ Id::SkyBehind, Id::Glow0, Id::Glow1, Id::Glow2 });
              } },
            { "RenderHDREnabled under a material preview", previewed, hdr_off,
              [](const Plan& before)
              {
                  return in_packs({ Pack::Main, Pack::Probe, Pack::Hero }, { Slot::GBuffer }) |
                         in_packs({ Pack::Main, Pack::Hero }, { Slot::Water }) |
                         in_packs({ Pack::Main, Pack::Probe }, { Slot::PostPing, Slot::PostPong }) |
                         wanted(before, bloom({ Pack::Main, Pack::Probe })) |
                         of({ Id::SkyBehind, Id::Glow0, Id::Glow1, Id::Glow2 });
              } },
            { "a material preview made", no_change, previewed,
              [](const Plan&)
              {
                  return in_packs({ Pack::Probe }, { Slot::PostPing, Slot::PostPong }) |
                         of({ Id::ProbeBloom0, Id::ProbeBloom1, Id::ProbeBloom2, Id::ProbeBloom3, Id::ProbeBloom4 });
              } },
            { "AlchemyRenderReverseZ", no_change, [](Inputs& in) { in.mReverseZ = false; },
              [](const Plan& before)
              {
                  return of({ Id::MainGBuffer, Id::ProbeGBuffer, Id::HeroGBuffer, Id::SpotShadow0, Id::SpotShadow1,
                              Id::MainDepthCopy, Id::HeroDepthCopy, Id::Preview }) |
                         wanted(before, sun_shadows({ Pack::Main, Pack::Probe, Pack::Hero }));
              } },
            { "AlchemyRenderReverseZ over 32-bit shadows", [](Inputs& in) { in.mShadowDepth32F = true; },
              [](Inputs& in) { in.mReverseZ = false; },
              [](const Plan&)
              {
                  return of({ Id::MainGBuffer, Id::ProbeGBuffer, Id::HeroGBuffer, Id::MainDepthCopy, Id::HeroDepthCopy,
                              Id::Preview });
              } },
            { "RenderShadowDetail 2 to 1", no_change, [](Inputs& in) { in.mShadowDetail = 1; },
              [](const Plan&) { return of({ Id::SpotShadow0, Id::SpotShadow1 }); } },
            { "RenderShadowDetail 2 to 0", no_change, [](Inputs& in) { in.mShadowDetail = 0; },
              [](const Plan& before)
              {
                  return wanted(before, sun_shadows({ Pack::Main, Pack::Probe, Pack::Hero })) |
                         of({ Id::SpotShadow0, Id::SpotShadow1, Id::ProbeLight, Id::HeroLight });
              } },
            { "RenderDeferredSSAO with shadows", no_change, [](Inputs& in) { in.mSSAO = false; },
              [](const Plan&) { return of({ Id::MainLightBlurred }); } },
            { "RenderDeferredSSAO without shadows", [](Inputs& in) { in.mShadowDetail = 0; },
              [](Inputs& in) { in.mSSAO = false; },
              [](const Plan&) { return of({ Id::MainLight, Id::MainLightBlurred }); } },
            { "RenderDeferredSSAO without compute", [](Inputs& in) { in.mCompute = false; },
              [](Inputs& in) { in.mSSAO = false; }, nothing },
            { "AlchemyRenderDisableCompute", no_change, [](Inputs& in) { in.mCompute = false; },
              [](const Plan&) { return of({ Id::MainLightBlurred }); } },
            { "AlchemyRenderDisableCompute without SSAO", [](Inputs& in) { in.mSSAO = false; },
              [](Inputs& in) { in.mCompute = false; }, nothing },
            { "RenderShadowBlurSize past a dispatch's reach", no_change,
              [](Inputs& in) { in.mSSAOBlurDispatched = false; },
              [](const Plan&) { return of({ Id::MainLightBlurred }); } },
            { "RenderShadowBlurSize past a dispatch's reach without compute", [](Inputs& in) { in.mCompute = false; },
              [](Inputs& in) { in.mSSAOBlurDispatched = false; }, nothing },
            { "RenderShadowResolutionScale", no_change, [](Inputs& in) { in.mShadowScale = 0.5f; },
              [](const Plan&) { return sun_shadows({ Pack::Main }) | of({ Id::SpotShadow0, Id::SpotShadow1 }); } },
            { "AlchemyRenderShadowDepth32F under reverse-Z", no_change, [](Inputs& in) { in.mShadowDepth32F = true; },
              nothing },
            { "AlchemyRenderShadowDepth32F under forward-Z", [](Inputs& in) { in.mReverseZ = false; },
              [](Inputs& in) { in.mShadowDepth32F = true; },
              [](const Plan& before)
              {
                  return wanted(before, sun_shadows({ Pack::Main, Pack::Probe, Pack::Hero })) |
                         of({ Id::SpotShadow0, Id::SpotShadow1 });
              } },
            { "AlchemyRenderProbeAllShadowCascades", no_change, [](Inputs& in) { in.mProbeAllCascades = true; },
              [](const Plan&) { return of({ Id::ProbeSun2, Id::ProbeSun3 }); } },
            { "AlchemyRenderProbeAllShadowCascades without shadows", [](Inputs& in) { in.mShadowDetail = 0; },
              [](Inputs& in) { in.mProbeAllCascades = true; }, nothing },
            { "AlchemyRenderProbeAllShadowCascades without probes", [](Inputs& in) { in.mProbes = false; },
              [](Inputs& in) { in.mProbeAllCascades = true; }, nothing },
            { "ALProbeSuperSample", no_change, [](Inputs& in) { in.mProbeSuperSample = 4; },
              [](const Plan& before)
              {
                  return wanted(before, Plan::idsOf(Pack::Probe) & ~sun_shadows({ Pack::Probe }));
              } },
            { "RenderReflectionProbeResolution", no_change, [](Inputs& in) { in.mProbeResolution = 256; },
              [](const Plan& before) { return wanted(before, Plan::idsOf(Pack::Probe)); } },
            { "RenderReflectionsEnabled", no_change, [](Inputs& in) { in.mProbes = false; },
              [](const Plan& before) { return wanted(before, Plan::idsOf(Pack::Probe) | Plan::idsOf(Pack::Hero)); } },
            { "RenderScreenSpaceReflections", no_change, [](Inputs& in) { in.mSSR = true; },
              [](const Plan&) { return of({ Id::SSRScene }); } },
            { "RenderScreenSpaceReflections without probes", [](Inputs& in) { in.mProbes = false; },
              [](Inputs& in) { in.mSSR = true; }, nothing },
            { "RenderMirrors", no_change, [](Inputs& in) { in.mMirrors = false; },
              [](const Plan& before) { return wanted(before, Plan::idsOf(Pack::Hero)); } },
            { "RenderHeroProbeResolution", no_change, [](Inputs& in) { in.mHeroResolution = 512; },
              [](const Plan& before) { return wanted(before, Plan::idsOf(Pack::Hero)); } },
            { "the window's size", no_change,
              [](Inputs& in)
              {
                  in.mWidth  = 1920;
                  in.mHeight = 1080;
              },
              [](const Plan& before) { return wanted(before, render_sized()); } },
            { "RenderResolutionDivisor", no_change, [](Inputs& in) { in.mResolutionDivisor = 2; },
              [](const Plan& before) { return wanted(before, render_sized()); } },
            // The spot pair stays at its largest edge, which 2560 wide and 3840 wide both pass.
            { "RenderResolutionMultiplier", no_change, [](Inputs& in) { in.mResolutionMultiplier = 1.5f; },
              [](const Plan& before) { return wanted(before, render_sized()) & ~spot_shadows(); } },
            { "RenderResolutionMultiplier below the spot shadows' edge", no_change,
              [](Inputs& in) { in.mResolutionMultiplier = 0.75f; },
              [](const Plan& before) { return wanted(before, render_sized()); } },
            { "AlchemyRenderSpotShadowMaxResolution", no_change, [](Inputs& in) { in.mSpotShadowMaxResolution = 1024; },
              [](const Plan&) { return spot_shadows(); } },
            { "AlchemyRenderSpotShadowMaxResolution to none", no_change, [](Inputs& in) { in.mSpotShadowMaxResolution = 0; },
              [](const Plan&) { return spot_shadows(); } },
            { "AlchemyRenderSpotShadowMaxResolution past the render width", [](Inputs& in) { in.mSpotShadowMaxResolution = 4096; },
              [](Inputs& in) { in.mSpotShadowMaxResolution = 8192; }, nothing },
            { "AlchemyRenderSpotShadowMaxResolution without spot shadows", [](Inputs& in) { in.mShadowDetail = 1; },
              [](Inputs& in) { in.mSpotShadowMaxResolution = 1024; }, nothing },
        };
    }

    // The changes plan section 2.3a says must leave the probe and hero packs alone, other than the screen-only ones the
    // rows already cover. The probe pack's post targets and bloom are the material preview's, there only while one is
    // alive, so with none bloom's settings leave the pack alone too.
    std::vector<std::pair<const char*, std::function<void(Inputs&)>>> not_the_probes()
    {
        return {
            { "the window's size", [](Inputs& in) { in.mWidth = 3840; in.mHeight = 2160; } },
            { "RenderResolutionDivisor", [](Inputs& in) { in.mResolutionDivisor = 2; } },
            { "RenderResolutionMultiplier", [](Inputs& in) { in.mResolutionMultiplier = 0.5f; } },
            { "a snapshot's size", [](Inputs& in) { in.mWidth = 7680; in.mHeight = 4320; } },
            { "FXAA", [](Inputs& in) { in.mFSAAType = 1; } },
            { "SMAA", [](Inputs& in) { in.mFSAAType = 2; } },
            { "SMAA without its stencil", [](Inputs& in) { in.mFSAAType = 2; in.mSMAAStencil = false; } },
            { "RenderBloomMipCount", [](Inputs& in) { in.mBloomMips = 7; } },
            { "RenderBloomResolutionScale", [](Inputs& in) { in.mBloomScale = 3; } },
            { "RenderBloomHalation", [](Inputs& in) { in.mBloomHalation = true; } },
            { "RenderGlowResolutionPow", [](Inputs& in) { in.mGlowResolutionPow = 7; } },
            { "RenderGlowHDR", [](Inputs& in) { in.mGlowHDR = true; } },
            { "RenderScreenSpaceReflections", [](Inputs& in) { in.mSSR = true; } },
            { "RenderDeferredSSAO with shadows", [](Inputs& in) { in.mSSAO = false; } },
            { "RenderShadowResolutionScale", [](Inputs& in) { in.mShadowScale = 2.f; } },
        };
    }

    TEST_CASE("alrenderresourceplan: each setting changes exactly the resources it describes", "[alrenderresourceplan]")
    {
        for (const Row& row : setting_rows())
        {
            INFO(row.setting);
            Inputs from = defaults();
            row.from(from);
            Inputs to = from;
            row.change(to);

            const Plan before = Plan::make(from);
            const Plan after  = Plan::make(to);
            CHECK(names(Plan::changed(before, after)) == names(row.expected(before)));
        }
    }

    TEST_CASE("alrenderresourceplan: the noise strength fills the noise map again and describes nothing",
              "[alrenderresourceplan]")
    {
        const Inputs from = defaults();
        Inputs       to   = from;
        to.mDeferredNoise = from.mDeferredNoise * 2.f;

        CHECK(names(Plan::changed(Plan::make(from), Plan::make(to))) == "nothing");
        CHECK(names(Plan::refilled(from, to)) == names(of({ Id::Noise })));
        CHECK(names(Plan::refilled(from, from)) == "nothing");
    }

    TEST_CASE("alrenderresourceplan: no other setting fills anything again", "[alrenderresourceplan]")
    {
        for (const Row& row : setting_rows())
        {
            INFO(row.setting);
            Inputs from = defaults();
            row.from(from);
            Inputs to = from;
            row.change(to);
            CHECK(names(Plan::refilled(from, to)) == "nothing");
        }
    }

    TEST_CASE("alrenderresourceplan: a resize leaves the probe packs and the fixed-size targets alone", "[alrenderresourceplan]")
    {
        const std::vector<std::pair<const char*, std::function<void(Inputs&)>>> configurations = {
            { "the defaults", no_change },
            { "SMAA and SSR", [](Inputs& in) { in.mFSAAType = 2; in.mSSR = true; } },
            { "no HDR", [](Inputs& in) { in.mHDR = false; in.mGBufferNormHDR = false; } },
            { "no shadows", [](Inputs& in) { in.mShadowDetail = 0; } },
        };
        const std::vector<std::pair<const char*, std::function<void(Inputs&)>>> resizes = {
            { "to 1920x1080", [](Inputs& in) { in.mWidth = 1920; in.mHeight = 1080; } },
            { "to 3840x2160", [](Inputs& in) { in.mWidth = 3840; in.mHeight = 2160; } },
            { "to a snapshot's 6000x4000", [](Inputs& in) { in.mWidth = 6000; in.mHeight = 4000; } },
            { "by a divisor of 2", [](Inputs& in) { in.mResolutionDivisor = 2; } },
            { "by a divisor of 3", [](Inputs& in) { in.mResolutionDivisor = 3; } },
            { "by a multiplier of 0.5", [](Inputs& in) { in.mResolutionMultiplier = 0.5f; } },
            { "by a multiplier of 2", [](Inputs& in) { in.mResolutionMultiplier = 2.f; } },
        };

        for (const auto& [configuration, configure] : configurations)
        {
            for (const auto& [resize, apply] : resizes)
            {
                INFO(configuration << ", resized " << resize);
                Inputs from = defaults();
                configure(from);
                Inputs to = from;
                apply(to);

                const IdSet changed = Plan::changed(Plan::make(from), Plan::make(to));
                CHECK(changed.any());
                CHECK(names(changed & ~render_sized()) == "nothing");
            }
        }
    }

    TEST_CASE("alrenderresourceplan: shadow maps are sized from the size the pack renders at", "[alrenderresourceplan]")
    {
        struct Case
        {
            U32 width;
            U32 height;
            U32 divisor;
            F32 multiplier;
            F32 scale;
        };
        const Case cases[] = {
            { 2560, 1440, 1, 1.f, 1.f },  { 2560, 1440, 2, 1.f, 1.f },   { 2560, 1440, 3, 1.f, 0.75f },
            { 1920, 1080, 1, 1.5f, 1.f }, { 1920, 1080, 1, 0.5f, 2.f },  { 3840, 2160, 4, 1.f, 1.5f },
            { 1366, 768, 1, 2.f, 0.5f },  { 800, 600, 1, 1.f, 0.25f },
        };

        for (const Case& c : cases)
        {
            INFO(c.width << "x" << c.height << ", divisor " << c.divisor << ", multiplier " << c.multiplier << ", scale "
                         << c.scale);
            Inputs in               = defaults();
            in.mWidth               = c.width;
            in.mHeight              = c.height;
            in.mResolutionDivisor   = c.divisor;
            in.mResolutionMultiplier = c.multiplier;
            in.mShadowScale         = c.scale;
            const Plan plan         = Plan::make(in);

            REQUIRE(plan.desc(Id::MainGBuffer));
            const ALRenderTargetDesc& gbuffer = *plan.desc(Id::MainGBuffer);
            U32 width  = c.width;
            U32 height = c.height;
            Inputs::scaleToRenderResolution(width, height, c.divisor, c.multiplier);
            CHECK(gbuffer.width == width);
            CHECK(gbuffer.height == height);

            for (U32 cascade = 0; cascade < Plan::SUN_CASCADES; ++cascade)
            {
                REQUIRE(plan.desc(Plan::sunShadow(Pack::Main, cascade)));
                const ALRenderTargetDesc& sun = *plan.desc(Plan::sunShadow(Pack::Main, cascade));
                CHECK(sun.width == blur_happy(gbuffer.width, c.scale));
                CHECK(sun.height == blur_happy(gbuffer.height, c.scale));
            }
            for (Id spot : { Id::SpotShadow0, Id::SpotShadow1 })
            {
                REQUIRE(plan.desc(spot));
                CHECK(plan.desc(spot)->width == std::min((U32)((F32)gbuffer.width * c.scale), in.mSpotShadowMaxResolution));
                CHECK(plan.desc(spot)->height == plan.desc(spot)->width);
            }

            // The capture packs' shadows never take the scale, the divisor or the multiplier.
            REQUIRE(plan.desc(Id::ProbeSun0));
            CHECK(plan.desc(Id::ProbeSun0)->width == blur_happy(in.mProbeResolution * 4, 1.f));
            REQUIRE(plan.desc(Id::HeroSun0));
            CHECK(plan.desc(Id::HeroSun0)->width == blur_happy(in.mHeroResolution, 1.f));
        }
    }

    TEST_CASE("alrenderresourceplan: the spot shadows stop at their largest edge", "[alrenderresourceplan]")
    {
        struct Case
        {
            U32 width;
            U32 largest;
            F32 scale;
            U32 expected;
        };
        const Case cases[] = {
            { 3440, 2048, 1.f, 2048 }, { 1920, 2048, 1.f, 1920 }, { 3440, 4096, 1.f, 3440 }, { 3440, 0, 1.f, 3440 },
            { 3440, 2048, 0.5f, 1720 }, { 1920, 2048, 1.5f, 2048 }, { 3440, 1, 1.f, 1 },     { 2048, 2048, 1.f, 2048 },
        };

        for (const Case& c : cases)
        {
            INFO(c.width << " wide, largest " << c.largest << ", scale " << c.scale);
            Inputs in                   = defaults();
            in.mWidth                   = c.width;
            in.mHeight                  = 1440;
            in.mSpotShadowMaxResolution = c.largest;
            in.mShadowScale             = c.scale;
            const Plan plan             = Plan::make(in);
            for (Id spot : { Id::SpotShadow0, Id::SpotShadow1 })
            {
                REQUIRE(plan.desc(spot));
                CHECK(plan.desc(spot)->width == c.expected);
                CHECK(plan.desc(spot)->height == c.expected);
            }
            // The sun's cascades follow the render size as they did.
            REQUIRE(plan.desc(Id::MainSun0));
            CHECK(plan.desc(Id::MainSun0)->width == blur_happy(c.width, c.scale));
        }

        // A snapshot past the edge leaves the spot pair as the window had it.
        Inputs window   = defaults();
        window.mWidth   = 3440;
        Inputs snapshot = window;
        snapshot.mWidth  = 6000;
        snapshot.mHeight = 4000;
        CHECK(names(Plan::changed(Plan::make(window), Plan::make(snapshot)) & spot_shadows()) == "nothing");
    }

    TEST_CASE("alrenderresourceplan: every target is planned with a size, whatever the shadow scale", "[alrenderresourceplan]")
    {
        // A target with no size cannot be built, and a target the render size sizes failing to build halves that size.
        for (F32 scale : { 0.f, 0.0001f, 0.01f, 0.25f, 1.f, 4.f })
        {
            for (const auto& [width, height] : { std::pair{ 2560u, 1440u }, std::pair{ 64u, 48u }, std::pair{ 1u, 1u } })
            {
                INFO(width << "x" << height << ", shadow scale " << scale);
                Inputs in       = defaults();
                in.mWidth       = width;
                in.mHeight      = height;
                in.mShadowScale = scale;
                const Plan plan = Plan::make(in);
                for (size_t i = 0; i < Plan::ID_COUNT; ++i)
                {
                    const Id id = (Id)i;
                    if (Plan::isTexture(id) || !plan.wants(id))
                    {
                        continue;
                    }
                    INFO(Plan::name(id));
                    CHECK(plan.desc(id)->width > 0);
                    CHECK(plan.desc(id)->height > 0);
                }
                CHECK(plan.wants(Id::SpotShadow0));
            }
        }
    }

    TEST_CASE("alrenderresourceplan: every pack's shadows take one depth format", "[alrenderresourceplan]")
    {
        for (bool reverse_z : { false, true })
        {
            for (bool depth_32f : { false, true })
            {
                INFO("reverse-Z " << reverse_z << ", 32-bit shadows " << depth_32f);
                Inputs in          = defaults();
                in.mReverseZ       = reverse_z;
                in.mShadowDepth32F = depth_32f;
                const Plan plan    = Plan::make(in);

                const Depth expected = reverse_z || depth_32f ? Depth::D32F : Depth::D24;
                for (Pack pack : { Pack::Main, Pack::Probe, Pack::Hero })
                {
                    REQUIRE(plan.sunCascades(pack) > 0);
                    for (U32 cascade = 0; cascade < plan.sunCascades(pack); ++cascade)
                    {
                        REQUIRE(plan.desc(Plan::sunShadow(pack, cascade)));
                        CHECK(plan.desc(Plan::sunShadow(pack, cascade))->depth == expected);
                    }
                }
                REQUIRE(plan.desc(Id::SpotShadow0));
                CHECK(plan.desc(Id::SpotShadow0)->depth == expected);
            }
        }
    }

    TEST_CASE("alrenderresourceplan: each pack has maps for the sun cascades it renders", "[alrenderresourceplan]")
    {
        struct Case
        {
            const char*                  configuration;
            std::function<void(Inputs&)> configure;
            U32                          main;
            U32                          probe;
            U32                          hero;
        };
        // Two in a capture, as sampleDirectionalShadow reads either all four or the first two.
        const Case cases[] = {
            { "the defaults", no_change, 4, 2, 2 },
            { "all of a probe's cascades", [](Inputs& in) { in.mProbeAllCascades = true; }, 4, 4, 2 },
            { "the sun's shadows alone", [](Inputs& in) { in.mShadowDetail = 1; }, 4, 2, 2 },
            { "no shadows", [](Inputs& in) { in.mShadowDetail = 0; }, 0, 0, 0 },
            { "no shadows, all of a probe's cascades",
              [](Inputs& in)
              {
                  in.mShadowDetail     = 0;
                  in.mProbeAllCascades = true;
              },
              0, 0, 0 },
            { "no probes", [](Inputs& in) { in.mProbes = false; }, 4, 0, 0 },
            { "a material preview without probes",
              [](Inputs& in)
              {
                  in.mProbes          = false;
                  in.mMaterialPreview = true;
              },
              4, 0, 0 },
        };

        for (const Case& c : cases)
        {
            Inputs in = defaults();
            c.configure(in);
            const Plan plan = Plan::make(in);

            for (const auto& [pack, cascades] :
                 { std::pair{ Pack::Main, c.main }, std::pair{ Pack::Probe, c.probe }, std::pair{ Pack::Hero, c.hero } })
            {
                INFO(c.configuration << ", pack " << (U32)pack);
                CHECK(plan.sunCascades(pack) == cascades);
                // From the first, and none past them.
                for (U32 cascade = 0; cascade < Plan::SUN_CASCADES; ++cascade)
                {
                    INFO("cascade " << cascade);
                    CHECK(plan.wants(Plan::sunShadow(pack, cascade)) == (cascade < cascades));
                }
            }
        }
    }

    TEST_CASE("alrenderresourceplan: the probe and hero packs ignore what is the main view's", "[alrenderresourceplan]")
    {
        for (const auto& [change, apply] : not_the_probes())
        {
            for (bool hdr : { true, false })
            {
                INFO(change << (hdr ? " under HDR" : " without HDR"));
                Inputs from          = defaults();
                from.mHDR            = hdr;
                from.mGBufferNormHDR = hdr;
                Inputs to            = from;
                apply(to);

                const Plan before   = Plan::make(from);
                const Plan after    = Plan::make(to);
                const IdSet changed = Plan::changed(before, after);
                CHECK(names(changed & Plan::idsOf(Pack::Hero)) == "nothing");
                CHECK(names(changed & Plan::idsOf(Pack::Probe)) == "nothing");
                CHECK(before.reflectionProbes() == after.reflectionProbes());
                CHECK(before.heroProbes() == after.heroProbes());
            }
        }
    }

    TEST_CASE("alrenderresourceplan: bloom's settings change the material preview's bloom and no other probe target", "[alrenderresourceplan]")
    {
        const std::vector<std::pair<const char*, std::function<void(Inputs&)>>> changes = {
            { "RenderBloomMipCount", [](Inputs& in) { in.mBloomMips = 3; } },
            { "RenderBloomResolutionScale", [](Inputs& in) { in.mBloomScale = 1; } },
            { "RenderBloomHalation", [](Inputs& in) { in.mBloomHalation = true; } },
        };
        for (const auto& [change, apply] : changes)
        {
            for (bool previewed : { true, false })
            {
                INFO(change << (previewed ? " under a material preview" : " with none"));
                Inputs from           = defaults();
                from.mMaterialPreview = previewed;
                Inputs to             = from;
                apply(to);
                const IdSet changed = Plan::changed(Plan::make(from), Plan::make(to));
                CHECK((changed & bloom({ Pack::Probe })).any() == previewed);
                CHECK(names(changed & Plan::idsOf(Pack::Probe) & ~bloom({ Pack::Probe })) == "nothing");
            }
        }
    }

    TEST_CASE("alrenderresourceplan: HDR emissive and reverse-Z rebuild all three G-buffers", "[alrenderresourceplan]")
    {
        const std::vector<std::pair<const char*, std::function<void(Inputs&)>>> changes = {
            { "RenderHDREnabled", [](Inputs& in) { in.mHDR = false; in.mGBufferNormHDR = false; } },
            { "RenderEnableEmissiveBuffer", [](Inputs& in) { in.mEmissive = false; } },
            { "AlchemyRenderReverseZ", [](Inputs& in) { in.mReverseZ = false; } },
        };
        for (const auto& [change, apply] : changes)
        {
            INFO(change);
            Inputs to = defaults();
            apply(to);
            const IdSet changed = Plan::changed(Plan::make(defaults()), Plan::make(to));
            CHECK(changed.test((size_t)Id::MainGBuffer));
            CHECK(changed.test((size_t)Id::ProbeGBuffer));
            CHECK(changed.test((size_t)Id::HeroGBuffer));
        }
    }

    TEST_CASE("alrenderresourceplan: the probe resolution and supersample change the probe pack alone", "[alrenderresourceplan]")
    {
        const std::vector<std::pair<const char*, std::function<void(Inputs&)>>> changes = {
            { "RenderReflectionProbeResolution up", [](Inputs& in) { in.mProbeResolution = 512; } },
            { "RenderReflectionProbeResolution down", [](Inputs& in) { in.mProbeResolution = 64; } },
            { "ALProbeSuperSample up", [](Inputs& in) { in.mProbeSuperSample = 4; } },
            { "ALProbeSuperSample down", [](Inputs& in) { in.mProbeSuperSample = 1; } },
        };
        for (const auto& [change, apply] : changes)
        {
            INFO(change);
            Inputs to = defaults();
            apply(to);
            const Plan before   = Plan::make(defaults());
            const Plan after    = Plan::make(to);
            const IdSet changed = Plan::changed(before, after);
            CHECK(changed.test((size_t)Id::ProbeGBuffer));
            CHECK(names(changed & ~Plan::idsOf(Pack::Probe)) == "nothing");
            CHECK(before.reflectionProbes() != after.reflectionProbes());
            CHECK(before.heroProbes() == after.heroProbes());
        }
    }

    TEST_CASE("alrenderresourceplan: the hero resolution changes the hero pack alone", "[alrenderresourceplan]")
    {
        for (U32 resolution : { 256u, 512u, 2048u })
        {
            INFO("RenderHeroProbeResolution " << resolution);
            Inputs to          = defaults();
            to.mHeroResolution = resolution;
            const Plan before   = Plan::make(defaults());
            const Plan after    = Plan::make(to);
            const IdSet changed = Plan::changed(before, after);
            CHECK(changed.test((size_t)Id::HeroGBuffer));
            CHECK(names(changed & ~Plan::idsOf(Pack::Hero)) == "nothing");
            CHECK(before.heroProbes() != after.heroProbes());
            CHECK(before.reflectionProbes() == after.reflectionProbes());
        }
    }

    TEST_CASE("alrenderresourceplan: with probes off there is no probe pack and no probe manager resources", "[alrenderresourceplan]")
    {
        for (bool mirrors : { false, true })
        {
            INFO("mirrors " << mirrors);
            Inputs in       = defaults();
            in.mProbes      = false;
            in.mMirrors     = mirrors;
            const Plan plan = Plan::make(in);

            CHECK(names(wanted(plan, Plan::idsOf(Pack::Probe) | Plan::idsOf(Pack::Hero))) == "nothing");
            CHECK(!plan.reflectionProbes());
            CHECK(!plan.heroProbes());
        }
    }

    // What depth of field asks of describes() before it frees the last scale's blur: a description is the plan's where a
    // resource it wants has it and shares no depth.
    TEST_CASE("alrenderresourceplan: describes() knows the descriptions of the resources it wants", "[alrenderresourceplan]")
    {
        const Plan plan = Plan::make(defaults());
        const ALRenderTargetDesc& bloom0 = *plan.desc(Plan::bloomMip(Pack::Main, 0));
        CHECK(plan.describes(bloom0));
        ALRenderTargetDesc smaller = bloom0;
        smaller.width /= 2;
        CHECK_FALSE(plan.describes(smaller));
        UNSCOPED_INFO("the screen shares the G-buffer's depth, so its colour alone is no resource's description");
        REQUIRE(plan[Id::MainScreen].mDepthFrom);
        CHECK_FALSE(plan.describes(*plan.desc(Id::MainScreen)));
    }

    TEST_CASE("alrenderresourceplan: a material preview keeps the probe pack and not the probes' own resources", "[alrenderresourceplan]")
    {
        Inputs off           = defaults();
        off.mProbes          = false;
        off.mMaterialPreview = true;
        const Plan plan      = Plan::make(off);
        for (Id id : { Id::ProbeGBuffer, Id::ProbeScreen, Id::ProbePostPing, Id::ProbeBloom0 })
        {
            INFO(Plan::name(id));
            CHECK(plan.wants(id));
        }
        UNSCOPED_INFO("a preview draws without the sun's shadows and draws no water, so the pack has no light map, sun "
                      "shadows or water copies for it");
        CHECK(names(wanted(plan, sun_shadows({ Pack::Probe }) | of({ Id::ProbeLight, Id::ProbeWater, Id::ProbeDepthCopy }))) ==
              "nothing");
        CHECK(!plan.reflectionProbes());
        CHECK(names(wanted(plan, Plan::idsOf(Pack::Hero))) == "nothing");

        // With probes on the pack is there already, and a preview adds the targets only it renders through.
        Inputs on           = defaults();
        Inputs on_previewed = on;
        on_previewed.mMaterialPreview = true;
        const Plan previewed = Plan::make(on_previewed);
        CHECK(names(Plan::changed(Plan::make(on), previewed)) == names(wanted(previewed, preview_targets())));
        CHECK(previewed.wants(Id::ProbePostPing));
    }

    TEST_CASE("alrenderresourceplan: the hero pack and its probes are there only with mirrors and probes both on", "[alrenderresourceplan]")
    {
        for (bool mirrors : { false, true })
        {
            for (bool probes : { false, true })
            {
                INFO("mirrors " << mirrors << ", probes " << probes);
                Inputs in       = defaults();
                in.mMirrors     = mirrors;
                in.mProbes      = probes;
                const Plan plan = Plan::make(in);
                CHECK(plan.wants(Id::HeroGBuffer) == (mirrors && probes));
                CHECK(plan.heroProbes().has_value() == (mirrors && probes));
                CHECK(wanted(plan, Plan::idsOf(Pack::Hero)).any() == (mirrors && probes));
            }
        }
    }

    TEST_CASE("alrenderresourceplan: each probe manager is planned from its own inputs", "[alrenderresourceplan]")
    {
        for (bool hdr : { true, false })
        {
            INFO((hdr ? "under HDR" : "without HDR"));
            Inputs in            = defaults();
            in.mHDR              = hdr;
            in.mGBufferNormHDR   = hdr;
            in.mProbeResolution  = 256;
            in.mProbeSuperSample = 4;
            in.mHeroResolution   = 512;
            const Plan plan      = Plan::make(in);

            REQUIRE(plan.reflectionProbes());
            CHECK(plan.reflectionProbes()->mResolution == 256);
            CHECK(plan.reflectionProbes()->mSuperSample == 4);
            CHECK(plan.reflectionProbes()->mHDR == hdr);
            REQUIRE(plan.heroProbes());
            CHECK(plan.heroProbes()->mResolution == 512);
            CHECK(plan.heroProbes()->mSuperSample == 1);
            CHECK(plan.heroProbes()->mHDR == hdr);

            // A probe face is the probe pack's size, and a mirror's face the hero pack's.
            REQUIRE(plan.desc(Id::ProbeGBuffer));
            CHECK(plan.desc(Id::ProbeGBuffer)->width == 256 * 4);
            REQUIRE(plan.desc(Id::HeroGBuffer));
            CHECK(plan.desc(Id::HeroGBuffer)->width == 512);
        }
    }

    TEST_CASE("alrenderresourceplan: a mirror's water copies the scene at its own pack's size", "[alrenderresourceplan]")
    {
        Inputs in          = defaults();
        in.mHeroResolution = 512;
        const Plan plan    = Plan::make(in);
        for (Pack pack : { Pack::Main, Pack::Hero })
        {
            INFO("pack " << (U32)pack);
            const Plan::Resource& gbuffer = plan[Plan::inPack(pack, Slot::GBuffer)];
            const Plan::Resource& water   = plan[Plan::inPack(pack, Slot::Water)];
            const Plan::Resource& depth   = plan[Plan::inPack(pack, Slot::DepthCopy)];
            REQUIRE(gbuffer.mDesc);
            REQUIRE(water.mDesc);
            REQUIRE(depth.mDesc);
            CHECK(water.mDesc->width == gbuffer.mDesc->width);
            CHECK(water.mDesc->height == gbuffer.mDesc->height);
            CHECK(depth.mDesc->width == gbuffer.mDesc->width);
            CHECK(depth.mDesc->depth == gbuffer.mDesc->depth);
            CHECK(depth.mDesc->colorCount() == 0);
            CHECK_FALSE(depth.mDesc->stencil);
            UNSCOPED_INFO("the colour alone, with no depth: a blit's destination, its alpha read by nothing");
            CHECK(water.mDesc->colorCount() == 1);
            CHECK(water.mDesc->color[0] == (U32)GL_R11F_G11F_B10F);
            CHECK(water.mDesc->depth == Depth::None);
            CHECK(!water.mDepthFrom);
        }
        CHECK(plan.desc(Id::HeroWater)->width == 512);
    }

    // A probe's face clips the scene at the water (LLPipeline::isWaterClip), so its water draws opaque and copies
    // nothing; the main view and a mirror's face refract where the setting asks.
    TEST_CASE("alrenderresourceplan: only the packs whose water refracts have the water's copies", "[alrenderresourceplan]")
    {
        for (bool transparent : { true, false })
        {
            INFO("RenderTransparentWater " << transparent);
            CHECK(Plan::refractsWater(Pack::Main, transparent) == transparent);
            CHECK(Plan::refractsWater(Pack::Hero, transparent) == transparent);
            CHECK_FALSE(Plan::refractsWater(Pack::Probe, transparent));

            for (bool barrier : { true, false })
            {
                for (bool previewed : { false, true })
                {
                    INFO("texture barrier " << barrier << ", material preview " << previewed);
                    Inputs in            = defaults();
                    in.mTransparentWater = transparent;
                    in.mTextureBarrier   = barrier;
                    in.mMaterialPreview  = previewed;
                    const Plan plan      = Plan::make(in);
                    for (Pack pack : { Pack::Main, Pack::Probe, Pack::Hero })
                    {
                        INFO("pack " << (U32)pack);
                        const bool refracts = Plan::refractsWater(pack, transparent);
                        REQUIRE(plan.wants(Plan::inPack(pack, Slot::GBuffer)));
                        CHECK(plan.wants(Plan::inPack(pack, Slot::Water)) == refracts);
                        UNSCOPED_INFO("without a barrier every pack that draws the water copies the depth for its haze");
                        CHECK(plan.wants(Plan::inPack(pack, Slot::DepthCopy)) == (refracts || !barrier));
                    }
                }
            }
        }

        // A pack there for a material preview alone draws no water, nor its haze.
        Inputs preview           = defaults();
        preview.mProbes          = false;
        preview.mMaterialPreview = true;
        preview.mTextureBarrier  = false;
        const Plan plan          = Plan::make(preview);
        REQUIRE(plan.wants(Id::ProbeGBuffer));
        CHECK(names(wanted(plan, of({ Id::ProbeWater, Id::ProbeDepthCopy }))) == "nothing");
    }

    // The water's copy of the screen takes another pass's target: bloom level 0's under HDR, at full scale without
    // halation, and the post chain's without HDR, both drawn after the water's pass.
    TEST_CASE("alrenderresourceplan: the water's copy of the screen is a later pass's description", "[alrenderresourceplan]")
    {
        const Plan hdr = Plan::make(defaults());
        CHECK(hdr[Id::MainWater] == hdr[Plan::bloomMip(Pack::Main, 0)]);

        Inputs ldr          = defaults();
        ldr.mHDR            = false;
        ldr.mGBufferNormHDR = false;
        const Plan plain    = Plan::make(ldr);
        CHECK(plain.desc(Id::MainWater)->color[0] == (U32)GL_RGBA8);
        CHECK(plain[Id::MainWater] == plain[Id::MainPostPing]);
    }

    TEST_CASE("alrenderresourceplan: every light map is 8 bits a channel, whatever HDR says", "[alrenderresourceplan]")
    {
        // Its four channels are factors the sun pass, the blur and the probes' sun pass clamp to [0, 1].
        for (bool hdr : { true, false })
        {
            INFO("HDR " << hdr);
            Inputs in          = defaults();
            in.mHDR            = hdr;
            in.mGBufferNormHDR = hdr;
            const Plan plan    = Plan::make(in);
            for (Id id : { Id::MainLight, Id::ProbeLight, Id::HeroLight })
            {
                INFO(Plan::name(id));
                REQUIRE(plan.wants(id));
                CHECK(plan.desc(id)->colorCount() == 1);
                CHECK(plan.desc(id)->color[0] == (U32)GL_RGBA8);
            }
        }
    }

    TEST_CASE("alrenderresourceplan: a capture pack has a light map exactly when shadows are on", "[alrenderresourceplan]")
    {
        // The probe captures' sun pass writes the light map only for shadows, and their soften pass reads it whatever
        // it holds; SSAO is the main view's alone.
        for (S32 shadow_detail : { 0, 1, 2 })
        {
            for (bool ssao : { false, true })
            {
                INFO("shadow detail " << shadow_detail << ", SSAO " << ssao);
                Inputs in        = defaults();
                in.mShadowDetail = shadow_detail;
                in.mSSAO         = ssao;
                const Plan plan  = Plan::make(in);

                CHECK(plan.wants(Id::ProbeLight) == (shadow_detail > 0));
                CHECK(plan.wants(Id::HeroLight) == (shadow_detail > 0));
                CHECK(plan.wants(Id::MainLight) == (shadow_detail > 0 || ssao));
            }
        }
    }

    TEST_CASE("alrenderresourceplan: the blurred light map is the main view's, with SSAO where the session computes",
              "[alrenderresourceplan]")
    {
        // The dispatch blurs into it in place of a scratch of the light map's description; the fragment passes need
        // the scratch instead, as they do for a kernel wider than a dispatch holds, and nothing blurs without SSAO.
        for (S32 shadow_detail : { 0, 2 })
        {
            for (bool ssao : { false, true })
            {
                for (bool compute : { false, true })
                {
                    for (bool dispatched : { false, true })
                    {
                        INFO("shadow detail " << shadow_detail << ", SSAO " << ssao << ", compute " << compute
                                              << ", dispatched " << dispatched);
                        Inputs in              = defaults();
                        in.mShadowDetail       = shadow_detail;
                        in.mSSAO               = ssao;
                        in.mCompute            = compute;
                        in.mSSAOBlurDispatched = dispatched;
                        const Plan plan        = Plan::make(in);
                        REQUIRE(plan.wants(Id::MainLightBlurred) == (ssao && compute && dispatched));
                        if (plan.wants(Id::MainLightBlurred))
                        {
                            // The light map's size, its 8 bits in two channels, no depth: an image a dispatch stores
                            // to.
                            const ALRenderTargetDesc& blurred = *plan.desc(Id::MainLightBlurred);
                            CHECK(blurred.width == plan.desc(Id::MainLight)->width);
                            CHECK(blurred.height == plan.desc(Id::MainLight)->height);
                            CHECK(blurred.colorCount() == 1);
                            CHECK(blurred.color[0] == (U32)GL_RG8);
                            CHECK(blurred.depth == Depth::None);
                            CHECK(!plan[Id::MainLightBlurred].mDepthFrom);
                        }
                    }
                }
            }
        }
    }

    TEST_CASE("alrenderresourceplan: SSAO alone never touches a capture pack", "[alrenderresourceplan]")
    {
        for (S32 shadow_detail : { 0, 1, 2 })
        {
            INFO("shadow detail " << shadow_detail);
            Inputs from        = defaults();
            from.mShadowDetail = shadow_detail;
            Inputs to          = from;
            to.mSSAO           = !from.mSSAO;
            const IdSet changed = Plan::changed(Plan::make(from), Plan::make(to));
            CHECK(names(changed & (Plan::idsOf(Pack::Probe) | Plan::idsOf(Pack::Hero))) == "nothing");
        }
    }

    TEST_CASE("alrenderresourceplan: the SMAA tables exist only while SMAA is the antialiasing", "[alrenderresourceplan]")
    {
        for (U32 fsaa : { 0u, 1u, 2u })
        {
            for (bool stencil : { false, true })
            {
                INFO("RenderFSAAType " << fsaa << ", stencil " << stencil);
                Inputs in       = defaults();
                in.mFSAAType    = fsaa;
                in.mSMAAStencil = stencil;
                const Plan plan = Plan::make(in);

                for (Id table : { Id::SMAAArea, Id::SMAASearch, Id::SMAASample })
                {
                    INFO(Plan::name(table));
                    CHECK(plan.wants(table) == (fsaa == 2));
                    CHECK(plan.wants(table) == plan.wants(Id::SMAAEdges));
                }
            }
        }
    }

    TEST_CASE("alrenderresourceplan: the reflections' source keeps last frame's colour without its alpha, and its depth",
              "[alrenderresourceplan]")
    {
        for (bool reverse_z : { false, true })
        {
            INFO("reverse-Z " << reverse_z);
            Inputs in    = defaults();
            in.mSSR      = true;
            in.mReverseZ = reverse_z;
            const Plan plan = Plan::make(in);
            REQUIRE(plan.desc(Id::SSRScene));
            const ALRenderTargetDesc& scene = *plan.desc(Id::SSRScene);

            // The reflections read its colour alone, unsigned and float under HDR.
            CHECK(scene.color[0] == (U32)GL_R11F_G11F_B10F);
            CHECK(scene.color[1] == 0u);
            // The scene's depth in its format, without the stencil.
            CHECK(scene.depth == (reverse_z ? Depth::D32F : Depth::D24));
            CHECK_FALSE(scene.stencil);
            CHECK(!plan[Id::SSRScene].mDepthFrom);
            // 8 B/px at the render size.
            CHECK(LLRenderTarget::bytesFor(scene) == 8ull * scene.width * scene.height);
            CHECK(scene.width == plan.desc(Id::MainScreen)->width);
            CHECK(scene.height == plan.desc(Id::MainScreen)->height);
        }
    }

    TEST_CASE("alrenderresourceplan: only the main view and the material preview have post targets", "[alrenderresourceplan]")
    {
        for (bool hdr : { true, false })
        {
            for (bool previewed : { false, true })
            {
                INFO((hdr ? "under HDR" : "without HDR") << (previewed ? ", a material preview alive" : ", none"));
                Inputs in           = defaults();
                in.mHDR             = hdr;
                in.mGBufferNormHDR  = hdr;
                in.mMaterialPreview = previewed;
                const Plan plan     = Plan::make(in);

                const IdSet post = in_packs({ Pack::Main, Pack::Probe, Pack::Hero }, { Slot::PostPing, Slot::PostPong }) |
                                   bloom({ Pack::Main, Pack::Probe, Pack::Hero });
                CHECK(names(wanted(plan, post & Plan::idsOf(Pack::Hero))) == "nothing");
                // The probes' captures never read the probe pack's: they are there for the preview alone.
                CHECK(plan.wants(Id::ProbePostPing) == previewed);
                CHECK(plan.wants(Id::ProbePostPong) == previewed);
                CHECK(plan.wants(Id::ProbeBloom0) == (previewed && hdr));
                CHECK(plan.wants(Id::MainPostPing));
                CHECK(plan.wants(Id::MainPostPong));
            }
        }
    }

    TEST_CASE("alrenderresourceplan: a target shares depth only with an owner of its own size", "[alrenderresourceplan]")
    {
        const std::vector<std::function<void(Inputs&)>> configurations = {
            no_change,
            [](Inputs& in) { in.mFSAAType = 2; },
            [](Inputs& in) { in.mFSAAType = 2; in.mSMAAStencil = false; },
            [](Inputs& in) { in.mShadowDetail = 0; in.mSSAO = false; },
            [](Inputs& in) { in.mResolutionDivisor = 3; in.mProbeSuperSample = 1; },
            [](Inputs& in) { in.mHDR = false; in.mGBufferNormHDR = false; in.mReverseZ = false; },
        };
        for (size_t c = 0; c < configurations.size(); ++c)
        {
            INFO("configuration " << c);
            Inputs in = defaults();
            configurations[c](in);
            const Plan plan = Plan::make(in);

            for (size_t i = 0; i < Plan::ID_COUNT; ++i)
            {
                const Plan::Resource& sharer = plan[(Id)i];
                if (!sharer.mDepthFrom)
                {
                    continue;
                }
                INFO(Plan::name((Id)i) << " from " << Plan::name(*sharer.mDepthFrom));
                REQUIRE(sharer.mDesc);
                const Plan::Resource& owner = plan[*sharer.mDepthFrom];
                REQUIRE(owner.mDesc);
                CHECK(!owner.mDepthFrom);
                CHECK(owner.mDesc->depth != Depth::None);
                CHECK(sharer.mDesc->depth == Depth::None);
                CHECK(!sharer.mDesc->stencil);
                CHECK(sharer.mDesc->width == owner.mDesc->width);
                CHECK(sharer.mDesc->height == owner.mDesc->height);
            }
        }
    }

    TEST_CASE("alrenderresourceplan: only the main view's render-sized targets drive the render size down", "[alrenderresourceplan]")
    {
        const IdSet shadow_maps = sun_shadows({ Pack::Main }) | of({ Id::SpotShadow0, Id::SpotShadow1 });
        for (size_t i = 0; i < Plan::ID_COUNT; ++i)
        {
            const Id id = (Id)i;
            INFO(Plan::name(id));
            const std::optional<Pack> pack = Plan::packOf(id);
            const bool bloom_mip = pack && Plan::slotOf(id) >= Slot::Bloom0;
            // What the render size sizes, but the pyramid, which loses levels instead, and the shadow maps, which are
            // left unbuilt.
            CHECK(Plan::drivesRenderSize(id) == (Plan::followsRenderSize(id) && !bloom_mip && !shadow_maps.test(i)));
        }

        // The fixed-size targets, the capture packs and the textures never do, nor do the shadow maps: halving the
        // view would not make room for a square spot map sized from its width, nor for one with no size.
        for (Id id : { Id::Preview, Id::DownRes, Id::Bake, Id::BrdfLut, Id::Exposure, Id::Glow0, Id::ProbeGBuffer,
                       Id::ProbeSun0, Id::HeroGBuffer, Id::HeroLight, Id::Noise, Id::SMAAArea, Id::MainSun0, Id::MainSun3,
                       Id::SpotShadow0, Id::SpotShadow1 })
        {
            INFO(Plan::name(id));
            CHECK(!Plan::drivesRenderSize(id));
        }
        for (Id id : { Id::MainGBuffer, Id::MainScreen, Id::MainLight, Id::MainPostPing, Id::SMAABlend,
                       Id::MainWater, Id::MainDepthCopy, Id::SkyBehind, Id::SSRScene, Id::MainLightBlurred })
        {
            INFO(Plan::name(id));
            CHECK(Plan::drivesRenderSize(id));
        }

        // The shadow maps are still sized from it, so a lower size builds them again smaller.
        for (Id id : { Id::MainSun0, Id::MainSun3, Id::SpotShadow0, Id::SpotShadow1 })
        {
            INFO(Plan::name(id));
            CHECK(Plan::followsRenderSize(id));
        }

        // Every pack's sun cascades and the spot pair, and nothing else.
        CHECK(names(Plan::idsWhere(Plan::isShadowMap)) ==
              names(sun_shadows({ Pack::Main, Pack::Probe, Pack::Hero }) | of({ Id::SpotShadow0, Id::SpotShadow1 })));
    }

    TEST_CASE("alrenderresourceplan: the passes acquire what lives for one phase of a frame from the pool", "[alrenderresourceplan]")
    {
        const IdSet transient = in_packs({ Pack::Main, Pack::Probe, Pack::Hero },
                                         { Slot::Light, Slot::PostPing, Slot::PostPong, Slot::Water, Slot::DepthCopy }) |
                                bloom({ Pack::Main, Pack::Probe, Pack::Hero }) |
                                of({ Id::SMAAEdges, Id::SMAABlend, Id::Glow0, Id::Glow1, Id::Glow2, Id::SkyBehind,
                                     Id::MainLightBlurred, Id::Preview, Id::Bake });
        CHECK(names(Plan::idsWhere(Plan::isTransient)) == names(transient));

        // What holds anything across frames, or between the phases of one, is built to keep.
        for (Id id : { Id::MainGBuffer, Id::MainScreen, Id::SSRScene, Id::Exposure, Id::ExposureLast, Id::LensFlareA,
                       Id::MainSun0, Id::SpotShadow0, Id::BrdfLut, Id::DownRes, Id::Noise })
        {
            INFO(Plan::name(id));
            CHECK_FALSE(Plan::isTransient(id));
        }
    }

    TEST_CASE("alrenderresourceplan: the reconcile readies every transient target but the previews' and the bakes'",
              "[alrenderresourceplan]")
    {
        // A preview or a bake draws only while one is open or changing, so nothing builds their targets before one
        // does, and a frame without one holds neither.
        const IdSet on_demand = of({ Id::Preview, Id::Bake });
        CHECK(names(Plan::idsWhere(Plan::isPrewarmed)) == names(Plan::idsWhere(Plan::isTransient) & ~on_demand));
        for (Id id : { Id::Preview, Id::Bake })
        {
            INFO(Plan::name(id));
            CHECK(Plan::isTransient(id));
            CHECK_FALSE(Plan::isPrewarmed(id));
        }

        // Neither shares a depth, nor has a depth shared: each is acquired alone, against nothing the reconcile builds.
        const Plan plan = Plan::make(defaults());
        for (Id id : { Id::Preview, Id::Bake })
        {
            INFO(Plan::name(id));
            REQUIRE(plan.wants(id));
            CHECK_FALSE(plan[id].mDepthFrom.has_value());
        }
        for (size_t i = 0; i < Plan::ID_COUNT; ++i)
        {
            const std::optional<Id> owner = plan[(Id)i].mDepthFrom;
            CHECK_FALSE((owner == Id::Preview || owner == Id::Bake));
        }
    }

    // A pool's backing that builds nothing: what a frame holds is worked out from the descriptions alone, as the pool
    // counts them.
    struct CountingBacking final : ALTransientTargetPool::Backing
    {
        bool build(LLRenderTarget&, const ALTransientTargetPool::Key&, std::string_view) override { return true; }
        void rename(LLRenderTarget&, std::string_view) override {}
        void poison(LLRenderTarget&, const ALTransientTargetPool::Key&) override {}
        void free(LLRenderTarget&) override {}
        U64  depthSerial(const LLRenderTarget&) const override { return 1; }
    };

    // The main view's targets through one frame at @a in, acquired and given back in the order the pipeline's passes
    // take them, over a pool that keeps what each frame left it. What the pool holds once a frame finds everything it
    // needs there, and which targets served which resources.
    struct WorkingSet
    {
        U64 mPersistent = 0; ///< the G-buffer and the screen, built to keep
        U64 mPooled     = 0; ///< what the pool holds
        U64 mOneEach    = 0; ///< the same resources, each a target of its own
        U32 mTargets    = 0;
        U32 mLastMisses = 0; ///< acquisitions the last frame had to build for
        bool mWaterIsBloom     = false;
        bool mSkyBehindIsBloom = false;
        bool mDefocusedIsLight = false; ///< depth of field's defocused scene took a target the lighting used
    };

    // CameraDoFResScale's default, the fraction of the screen depth of field's blur covers each way.
    constexpr F32 DOF_RES_SCALE = 0.7f;

    WorkingSet main_view_frame(const Inputs& in, bool dof = false)
    {
        const Plan plan = Plan::make(in);
        CountingBacking       backing;
        ALTransientTargetPool pool(backing);
        LLRenderTarget        gbuffer; // stands in for main.gbuffer, whose depth the light map and the screen's description share

        const auto acquire = [&](ALTransientTargets& scope, Id id, std::string_view name = {}) -> LLRenderTarget*
        {
            const Plan::Resource& resource = plan[id];
            REQUIRE(resource.mDesc);
            const std::string_view label = name.empty() ? Plan::name(id) : name;
            return resource.mDepthFrom ? scope.acquire(*resource.mDesc, gbuffer, label) : scope.acquire(*resource.mDesc, label);
        };

        WorkingSet set;
        for (int frame = 0; frame < 2; ++frame)
        {
            pool.beginFrame();

            // The sky pool draws the sky behind (LLDrawPoolWLSky), held to the end of the deferred lighting.
            ALTransientTargets sky(pool);
            LLRenderTarget*    sky_behind = acquire(sky, Id::SkyBehind);

            LLRenderTarget* light   = nullptr;
            LLRenderTarget* scratch = nullptr;
            {
                // renderDeferredLighting: the light map, and SSAO's blur, by one dispatch into the blurred light map
                // the soften pass reads, or without compute through a scratch of the light map's description.
                ALTransientTargets lighting(pool);
                light = plan.wants(Id::MainLight) ? acquire(lighting, Id::MainLight) : nullptr;
                if (light && plan.wants(Id::MainLightBlurred))
                {
                    acquire(lighting, Id::MainLightBlurred);
                }
                else if (light && in.mSSAO)
                {
                    scratch = acquire(lighting, Id::MainLight, "main.light.scratch");
                    lighting.release(scratch);
                }
            }

            // renderGeomPostDeferred: the water pool's pass with its two copies. The haze passes either side of it read
            // the scene's depth in place.
            LLRenderTarget* water = nullptr;
            {
                ALTransientTargets pass(pool);
                water = acquire(pass, Id::MainWater);
                acquire(pass, Id::MainDepthCopy);
            }
            sky.releaseAll();

            // renderFinalize: depth of field's defocused scene, in the screen's description, and its blur at
            // CameraDoFResScale in bloom level 0's format (the post chain's without HDR), given back before the bloom
            // pyramid; then the pyramid, the post chain's first target, and its other only where a pass after
            // colorCorrect draws into one: antialiasing, or the legacy glow's combine without HDR. At the defaults none
            // does, as the blit sharpens the frame itself. All but the blur held until the frame is presented.
            LLRenderTarget* defocused = nullptr;
            LLRenderTarget* bloom0    = nullptr;
            LLRenderTarget* bloom1    = nullptr;
            {
                ALTransientTargets post(pool);
                if (dof)
                {
                    defocused = acquire(post, Id::MainScreen, "dof.out");
                    const ALRenderTargetDesc& screen = *plan.desc(Id::MainScreen);
                    const ALRenderTargetDesc& like   = *plan.desc(in.mHDR ? Plan::bloomMip(Pack::Main, 0) : Id::MainPostPong);
                    ALTransientTargets        effect(pool);
                    effect.acquire(ALRenderTargetDesc{ .width  = llmax(1u, (U32)(screen.width * DOF_RES_SCALE)),
                                                       .height = llmax(1u, (U32)(screen.height * DOF_RES_SCALE)),
                                                       .color  = { like.color[0] } },
                                   "dof.blur");
                }
                for (U32 mip = 0; mip < Plan::BLOOM_MAX_MIPS && plan.wants(Plan::bloomMip(Pack::Main, mip)); ++mip)
                {
                    LLRenderTarget* level = acquire(post, Plan::bloomMip(Pack::Main, mip));
                    bloom0 = mip == 0 ? level : bloom0;
                    bloom1 = mip == 1 ? level : bloom1;
                }
                acquire(post, Id::MainPostPing);
                if (in.mFSAAType != 0 || !in.mHDR)
                {
                    acquire(post, Id::MainPostPong);
                }
            }

            set.mWaterIsBloom     = water && water == bloom0;
            set.mSkyBehindIsBloom = sky_behind && sky_behind == bloom1;
            set.mDefocusedIsLight = defocused && (defocused == light || defocused == scratch);
            set.mLastMisses       = pool.stats().mMisses;
        }

        set.mPooled  = pool.stats().mBytes;
        set.mTargets = pool.stats().mTargets;
        set.mPersistent = LLRenderTarget::bytesFor(*plan.desc(Id::MainGBuffer)) + LLRenderTarget::bytesFor(*plan.desc(Id::MainScreen));
        set.mOneEach    = set.mPersistent;
        for (Id id : { Id::MainLight, Id::MainLightBlurred, Id::MainWater, Id::MainDepthCopy, Id::MainPostPing,
                       Id::MainPostPong, Id::SkyBehind })
        {
            set.mOneEach += plan.wants(id) ? LLRenderTarget::bytesFor(*plan.desc(id)) : 0;
        }
        for (U32 mip = 0; mip < Plan::BLOOM_MAX_MIPS && plan.wants(Plan::bloomMip(Pack::Main, mip)); ++mip)
        {
            set.mOneEach += LLRenderTarget::bytesFor(*plan.desc(Plan::bloomMip(Pack::Main, mip)));
        }
        return set;
    }

    TEST_CASE("alrenderresourceplan: the main view's working set at 1440p defaults, worked out from the descriptions",
              "[alrenderresourceplan]")
    {
        // 2560x1440 at the defaults: HDR, reverse-Z, the emissive buffer, bloom at full scale with five levels, shadow
        // detail 2, SSAO and transparent water. 36 B/px kept: the G-buffer's 20 and its 8 of depth and stencil, and the
        // screen's 8.
        constexpr U64 PIXELS = 2560ull * 1440ull;
        const WorkingSet set = main_view_frame(defaults());
        CHECK(set.mPersistent == 36 * PIXELS);

        UNSCOPED_INFO("exact-description reuse finds the plan's two matches by itself");
        CHECK(set.mWaterIsBloom);
        CHECK(set.mSkyBehindIsBloom);
        UNSCOPED_INFO("a frame finds what it needs where the last one left it");
        CHECK(set.mLastMisses == 0);

        // The light map, SSAO's blurred light map, the depth copy, bloom mip 0 with the water's copy and mips 2 to
        // 4, mip 1 with the sky behind, and the post chain's first: 4 + 2 + 4 + 4 + 1 + 0.33 + 4 B/px.
        CHECK(set.mTargets == 9);
        CHECK(set.mPooled == 71251200);
        // Each its own target, as each was: 64.3 B/px, 226.2 MiB.
        CHECK(set.mOneEach == 237139200);
        CHECK(set.mPersistent + set.mPooled == 203961600); // 194.5 MiB

        // Without compute, the fragment passes blur through a scratch of the light map's description: two of it
        // through the lighting, 8 B/px where the dispatch takes 6.
        Inputs fragment  = defaults();
        fragment.mCompute = false;
        const WorkingSet passes = main_view_frame(fragment);
        CHECK(passes.mWaterIsBloom);
        CHECK(passes.mSkyBehindIsBloom);
        CHECK(passes.mTargets == 9);
        CHECK(passes.mOneEach == 229766400); // 219.1 MiB
        CHECK(passes.mPersistent + passes.mPooled == 211334400); // 201.5 MiB

        // Without SSAO the lighting needs one target of the light map's description, and no blurred one.
        Inputs no_ssao = defaults();
        no_ssao.mSSAO  = false;
        const WorkingSet without = main_view_frame(no_ssao);
        CHECK(without.mWaterIsBloom);
        CHECK(without.mSkyBehindIsBloom);
        CHECK(without.mOneEach == 229766400);
        CHECK(without.mPersistent + without.mPooled == 196588800); // 187.5 MiB

        // With antialiasing the post chain's other target is drawn into, and pooled beside the first: 4 B/px more.
        Inputs fxaa    = defaults();
        fxaa.mFSAAType = 1;
        const WorkingSet antialiased = main_view_frame(fxaa);
        CHECK(antialiased.mTargets == 10);
        CHECK(antialiased.mPooled - set.mPooled == 4 * PIXELS);
    }

    // Depth of field adds two targets to what the frame holds: its defocused scene, in the screen's description, which
    // no other target of the frame's has, the light map being 8 bits a channel, and its blur, of its own size.
    TEST_CASE("alrenderresourceplan: the main view's working set with depth of field", "[alrenderresourceplan]")
    {
        const WorkingSet off = main_view_frame(defaults());
        const WorkingSet on  = main_view_frame(defaults(), true);
        CHECK_FALSE(on.mDefocusedIsLight);
        CHECK(on.mLastMisses == 0);
        CHECK(on.mTargets == off.mTargets + 2);
        // 2560x1440 of RGBA16F, and 1792x1008 of R11F_G11F_B10F.
        CHECK(on.mPooled - off.mPooled == 29491200 + 7225344);
        CHECK(on.mPersistent + on.mPooled == 240678144); // 229.5 MiB
    }

    TEST_CASE("alrenderresourceplan: every resource has its own debugger label", "[alrenderresourceplan]")
    {
        CHECK(Plan::name(Id::MainGBuffer) == "main.gbuffer");
        CHECK(Plan::name(Id::ProbeSun1) == "probe.shadow.sun1");
        CHECK(Plan::name(Id::HeroBloom6) == "hero.bloom.mip6");
        CHECK(Plan::name(Id::SpotShadow0) == "shadow.spot0");
        CHECK(Plan::name(Id::SMAAEdges) == "smaa.edges");
        CHECK(Plan::name(Id::LensFlareB) == "lens.flare.b");
        CHECK(Plan::name(Id::ToneCurve) == "tone.curve");

        std::set<std::string_view> seen;
        for (size_t i = 0; i < Plan::ID_COUNT; ++i)
        {
            const std::string_view name = Plan::name((Id)i);
            INFO("id " << i);
            CHECK(!name.empty());
            CHECK(seen.insert(name).second);
        }
    }
}
