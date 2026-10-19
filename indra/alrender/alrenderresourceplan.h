/**
 * @file alrenderresourceplan.h
 * @brief What each of the pipeline's render resources is for a set of inputs.
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

#pragma once

#include "alrenderresourceinputs.h"
#include "alrendertargetdesc.h"

#include <array>
#include <bitset>
#include <optional>
#include <string_view>

/// What each of the pipeline's render resources is for a set of inputs: a description where the resource is wanted,
/// nothing where it is not, and for a target that takes its depth from another, which. A pure function of the inputs;
/// LLPipeline::reconcileResources makes the GL objects what the plan says, but for the transient ones (isTransient),
/// which the passes acquire by their descriptions from a pool of targets shared by lifetime (ALTransientTargetPool).
///
/// Three packs of targets render a view each (LLPipeline::RenderTargetPack): Main, the world view at the render size;
/// Probe, a reflection probe's face at the probe resolution times its supersample; Hero, a mirror's face at the hero
/// resolution. Each pack is planned from its own inputs, so what changes one leaves the others alone. The main view
/// has targets of its own beside its pack, and the rest are fixed in size. The two probe managers' own resources are
/// planned beside their packs, from the same inputs.
class ALRenderResourcePlan
{
public:
    enum class Pack : U8
    {
        Main,
        Probe,
        Hero,
        COUNT
    };

    /// A pack's targets, the same in each. A pack plans only those it renders through.
    enum class Slot : U8
    {
        GBuffer,
        Screen,
        Light,
        PostPing,
        PostPong,
        Water,
        DepthCopy,
        Sun0,
        Sun1,
        Sun2,
        Sun3,
        Bloom0,
        Bloom1,
        Bloom2,
        Bloom3,
        Bloom4,
        Bloom5,
        Bloom6,
        COUNT
    };

    static constexpr U32 SUN_CASCADES = 4;
    /// A capture's sun cascades, unless a probe's renders them all (ALRenderResourceInputs::mProbeAllCascades).
    static constexpr U32 CAPTURE_SUN_CASCADES = 2;
    static constexpr U32 BLOOM_MAX_MIPS = 7;

    // The fixed sizes, which the pipeline's passes read from here.
    static constexpr U32 DOWNRES_SIZE = 1024;
    static constexpr U32 PREVIEW_SIZE = 512;
    static constexpr U32 BRDF_LUT_SIZE = 512;
    static constexpr U32 GLOW_WIDTH = 512;
    static constexpr U32 EXPOSURE_METER_WIDTH = 128;
    static constexpr U32 EXPOSURE_METER_HEIGHT = 72;
    static constexpr U32 EXPOSURE_HISTOGRAM_BINS = 64;
    static constexpr U32 NOISE_SIZE = 128;

    /// Every resource, each pack's in Slot order first. The names are the debugger labels (name()).
    enum class Id : U8
    {
        MainGBuffer, MainScreen, MainLight, MainPostPing, MainPostPong, MainWater, MainDepthCopy,
        MainSun0, MainSun1, MainSun2, MainSun3,
        MainBloom0, MainBloom1, MainBloom2, MainBloom3, MainBloom4, MainBloom5, MainBloom6,

        ProbeGBuffer, ProbeScreen, ProbeLight, ProbePostPing, ProbePostPong, ProbeWater, ProbeDepthCopy,
        ProbeSun0, ProbeSun1, ProbeSun2, ProbeSun3,
        ProbeBloom0, ProbeBloom1, ProbeBloom2, ProbeBloom3, ProbeBloom4, ProbeBloom5, ProbeBloom6,

        HeroGBuffer, HeroScreen, HeroLight, HeroPostPing, HeroPostPong, HeroWater, HeroDepthCopy,
        HeroSun0, HeroSun1, HeroSun2, HeroSun3,
        HeroBloom0, HeroBloom1, HeroBloom2, HeroBloom3, HeroBloom4, HeroBloom5, HeroBloom6,

        // The main view's own, at its render size.
        SpotShadow0, SpotShadow1,
        SMAAEdges, SMAABlend,
        SkyBehind,
        SSRScene,
        MainLightBlurred,

        // Fixed in size.
        Glow0, Glow1, Glow2,
        DownRes,
        Preview,
        Bake,
        BrdfLut,
        ExposureMeter, ExposureHistogram, Exposure, ExposureLast,
        LensFlareA, LensFlareB,

        // GL textures of the pipeline's own rather than render targets.
        Noise,
        TrueNoise,
        SMAAArea,
        SMAASearch,
        SMAASample,
        ToneCurve,

        COUNT
    };

    static constexpr size_t ID_COUNT = (size_t)Id::COUNT;
    using IdSet = std::bitset<ID_COUNT>;

    /// What a probe manager's own resources are built for: its cube array, its scratch target and its mip chain.
    struct ProbeManager
    {
        U32  mResolution  = 0; ///< a probe's face edge
        U32  mSuperSample = 1; ///< how much larger than that a face renders each way
        bool mHDR         = false;

        friend bool operator==(const ProbeManager&, const ProbeManager&) = default;
    };

    struct Resource
    {
        /// What the resource is, or nothing where it is not wanted and is freed. A texture's (isTexture()) names only
        /// its format: its size is its contents'.
        std::optional<ALRenderTargetDesc> mDesc;
        /// The resource whose depth this one's framebuffer shares; its own description then has no depth.
        std::optional<Id> mDepthFrom;

        friend bool operator==(const Resource&, const Resource&) = default;
    };

    static ALRenderResourcePlan make(const ALRenderResourceInputs& inputs);

    const Resource& operator[](Id id) const { return mResources[(size_t)id]; }
    bool wants(Id id) const { return mResources[(size_t)id].mDesc.has_value(); }
    const std::optional<ALRenderTargetDesc>& desc(Id id) const { return mResources[(size_t)id].mDesc; }
    /// Whether some resource the plan wants is what `desc` describes, sharing no depth.
    bool describes(const ALRenderTargetDesc& desc) const;

    /// The sun cascades @a pack renders, which are those it has maps for, from the first: SUN_CASCADES in the main view,
    /// CAPTURE_SUN_CASCADES in a capture unless a probe's renders them all, none without sun shadows.
    U32 sunCascades(Pack pack) const
    {
        U32 cascades = 0;
        while (cascades < SUN_CASCADES && wants(sunShadow(pack, cascades)))
        {
            ++cascades;
        }
        return cascades;
    }

    /// The reflection probes' manager's resources, or nothing where they are not wanted and are freed.
    const std::optional<ProbeManager>& reflectionProbes() const { return mReflectionProbes; }
    /// The mirrors' (LLHeroProbeManager's), likewise.
    const std::optional<ProbeManager>& heroProbes() const { return mHeroProbes; }

    /// The resources that differ between two plans: in their description, their depth's owner, or whether they are
    /// wanted at all.
    static IdSet changed(const ALRenderResourcePlan& before, const ALRenderResourcePlan& after);
    /// The resources whose contents, not their descriptions, are made from an input that differs between @a before
    /// and @a after, so that storage kept for them is filled again: the deferred noise map, from RenderDeferredNoise.
    static IdSet refilled(const ALRenderResourceInputs& before, const ALRenderResourceInputs& after);

    static constexpr Id inPack(Pack pack, Slot slot) { return (Id)((U32)pack * (U32)Slot::COUNT + (U32)slot); }
    static constexpr Id sunShadow(Pack pack, U32 cascade) { return inPack(pack, (Slot)((U32)Slot::Sun0 + cascade)); }
    static constexpr Id bloomMip(Pack pack, U32 mip) { return inPack(pack, (Slot)((U32)Slot::Bloom0 + mip)); }

    /// The pack @a id is a target of, or nothing for the main view's own and the fixed ones.
    static std::optional<Pack> packOf(Id id);
    /// Its slot in its pack; only for a pack's targets.
    static Slot slotOf(Id id);
    /// What debuggers call it: main.gbuffer, probe.shadow.sun1, shadow.spot0, smaa.edges, noise.
    static std::string_view name(Id id);
    /// Whether it is sized from the main view's render size, so that a resize, a snapshot and a retry at a lower size
    /// rebuild it, and its failing to build halves that size.
    static bool followsRenderSize(Id id);
    /// Whether failing to build it halves the render size: every target the render size sizes but the bloom pyramid,
    /// which is as deep as the levels that were built, and the shadow maps, which are left unbuilt and their shadows
    /// undrawn. A fixed-size target or a capture pack's failing leaves the size alone, and a retry at a lower size
    /// builds nothing but what the size sizes.
    static bool drivesRenderSize(Id id);
    /// Whether it is a shadow map: a pack's sun cascade or one of the spot lights' pair.
    static bool isShadowMap(Id id);
    /// Whether it is a GL texture of the pipeline's own rather than a render target.
    static bool isTexture(Id id);
    /// Whether the passes that use it acquire it from the transient pool for as long as they need it, rather than the
    /// reconcile building it to keep: a target that lives for one phase of a frame, and holds nothing across frames. A
    /// pack's light map is one, the main view's blurred light map, the water's copies of the scene, the bloom pyramid,
    /// the post chain, SMAA's edges and blend weights, the legacy glow, the sky behind the water, and the targets the
    /// UI previews and the appearance bakes draw into.
    static bool isTransient(Id id);
    /// Whether the reconcile readies it in the pool for the next frame to find: every transient resource but the
    /// previews' and the bakes' targets, which only a dynamic texture with something to draw acquires.
    static bool isPrewarmed(Id id);
    /// Whether only the GLTF material preview renders through it: the probe pack's post targets and bloom, which the
    /// probes' captures never read, and which are planned only while a preview is alive.
    static bool forMaterialPreview(Id id);

    /// Every id of @a pack.
    static IdSet idsOf(Pack pack);
    /// Every id for which @a predicate holds.
    template <typename Predicate> static IdSet idsWhere(Predicate predicate)
    {
        IdSet ids;
        for (size_t i = 0; i < ID_COUNT; ++i)
        {
            ids.set(i, predicate((Id)i));
        }
        return ids;
    }

private:
    Resource& at(Id id) { return mResources[(size_t)id]; }

    std::array<Resource, ID_COUNT> mResources{};
    std::optional<ProbeManager>    mReflectionProbes;
    std::optional<ProbeManager>    mHeroProbes;
};

static_assert(ALRenderResourcePlan::inPack(ALRenderResourcePlan::Pack::Probe, ALRenderResourcePlan::Slot::GBuffer) ==
                  ALRenderResourcePlan::Id::ProbeGBuffer,
              "the probe pack's ids follow the main pack's, in Slot order");
static_assert(ALRenderResourcePlan::inPack(ALRenderResourcePlan::Pack::Hero, ALRenderResourcePlan::Slot::Bloom6) ==
                  ALRenderResourcePlan::Id::HeroBloom6,
              "the hero pack's ids follow the probe pack's, in Slot order");
static_assert(ALRenderResourcePlan::bloomMip(ALRenderResourcePlan::Pack::Hero, ALRenderResourcePlan::BLOOM_MAX_MIPS) ==
                  ALRenderResourcePlan::Id::SpotShadow0,
              "the main view's own ids follow the packs'");
