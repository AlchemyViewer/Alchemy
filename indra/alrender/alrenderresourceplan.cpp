/**
 * @file alrenderresourceplan.cpp
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

#include "linden_common.h"

#include "alrenderresourceplan.h"

#include "llglheaders.h"

#include <algorithm>

namespace
{
    using Plan  = ALRenderResourcePlan;
    using Id    = ALRenderResourcePlan::Id;
    using Pack  = ALRenderResourcePlan::Pack;
    using Slot  = ALRenderResourcePlan::Slot;
    using Depth = ALRenderTargetDesc::Depth;

    // A shadow map's edge: the size times its scale, plus 16 and rounded down to a multiple of 16, so it is even and
    // the horizontal shadow blur leaves no stripe.
    constexpr U32 blur_happy_size(U32 size, F32 scale)
    {
        return (U32)((F32)size * scale + 16.f) & ~0xFu;
    }

    // Each pack's slot names under its own prefix, laid out as Id lays the packs out.
    constexpr std::string_view PACK_NAMES[(size_t)Pack::COUNT][(size_t)Slot::COUNT] = {
        { "main.gbuffer", "main.screen", "main.light", "main.post.ping", "main.post.pong", "main.water.refraction",
          "main.depth.copy", "main.shadow.sun0",
          "main.shadow.sun1", "main.shadow.sun2", "main.shadow.sun3", "main.bloom.mip0", "main.bloom.mip1",
          "main.bloom.mip2", "main.bloom.mip3", "main.bloom.mip4", "main.bloom.mip5", "main.bloom.mip6" },
        { "probe.gbuffer", "probe.screen", "probe.light", "probe.post.ping", "probe.post.pong", "probe.water.refraction",
          "probe.depth.copy", "probe.shadow.sun0",
          "probe.shadow.sun1", "probe.shadow.sun2", "probe.shadow.sun3", "probe.bloom.mip0", "probe.bloom.mip1",
          "probe.bloom.mip2", "probe.bloom.mip3", "probe.bloom.mip4", "probe.bloom.mip5", "probe.bloom.mip6" },
        { "hero.gbuffer", "hero.screen", "hero.light", "hero.post.ping", "hero.post.pong", "hero.water.refraction",
          "hero.depth.copy", "hero.shadow.sun0",
          "hero.shadow.sun1", "hero.shadow.sun2", "hero.shadow.sun3", "hero.bloom.mip0", "hero.bloom.mip1",
          "hero.bloom.mip2", "hero.bloom.mip3", "hero.bloom.mip4", "hero.bloom.mip5", "hero.bloom.mip6" },
    };

    constexpr size_t PACK_IDS = (size_t)Pack::COUNT * (size_t)Slot::COUNT;

    // Everything after the packs, in Id order.
    constexpr std::string_view OWN_NAMES[Plan::ID_COUNT - PACK_IDS] = {
        "shadow.spot0", "shadow.spot1", "smaa.edges", "smaa.blend", "sky.behind", "ssr.scene", "main.light.blurred",
        "glow0", "glow1", "glow2", "downres", "preview", "bake", "brdf.lut", "exposure.meter", "exposure.histogram",
        "exposure", "exposure.last", "lens.flare.a", "lens.flare.b", "noise", "noise.true", "smaa.area", "smaa.search",
        "smaa.sample", "tone.curve",
    };

    static_assert(OWN_NAMES[(size_t)Id::ToneCurve - PACK_IDS] == "tone.curve", "a name for every id after the packs");
}

// static
ALRenderResourcePlan ALRenderResourcePlan::make(const ALRenderResourceInputs& in)
{
    ALRenderResourcePlan plan;

    const bool hdr = in.mHDR;

    // The scene's depth carries stencil, where the water keeps its per-pixel facts (alwaterstencil.h), and is float
    // under reverse-Z. The shadow maps are float under reverse-Z whatever AlchemyRenderShadowDepth32F says: reversed
    // depth puts the float mantissa's dense end where the perspective spot cascades need it, and DEPTH24 pads to the
    // same four bytes.
    const Depth main_depth   = in.mReverseZ ? Depth::D32F : Depth::D24;
    const Depth shadow_depth = (in.mShadowDepth32F || in.mReverseZ) ? Depth::D32F : Depth::D24;

    // The scene's colour in a target of its own: float with HDR, 8 bits without.
    const U32 screen_format = hdr ? GL_RGBA16F : GL_RGBA8;
    // The tonemapped image the post chain carries.
    const U32 post_format = hdr ? GL_RGB10_A2 : GL_RGBA8;

    // Albedo, ORM or specular, the normal and the emissive. The normal's layout is the one the programs were compiled
    // against (GBUFFER_NORM_HDR), which setShaders latches from RenderHDREnabled before it compiles; nothing compiles
    // against the emissive's, which follows HDR as the other HDR targets do. R11F_G11F_B10F rather than RGB16F, which
    // pads to RGBA16F in memory: half the bandwidth, still float so it filters linearly and holds the sky writers'
    // boosts past 1, and unsigned is enough since the writers clamp emissive at zero. Its 6/6/5 mantissa bits band in
    // smooth gradients, which the sky, cloud and aurora writers' ditherEmissive covers.
    const std::array<U32, ALRenderTargetDesc::MAX_COLOR> gbuffer_color = {
        GL_SRGB8_ALPHA8, GL_RGBA8, in.mGBufferNormHDR ? (U32)GL_RGBA16 : (U32)GL_RGB10_A2,
        in.mEmissive ? (hdr ? (U32)GL_R11F_G11F_B10F : (U32)GL_RGB8) : 0u
    };

    // The HDR bloom pyramid's base is the pack's size times one of five fractions, each level half the one before, to a
    // single texel at most. Halation carries its warmth in alpha, which needs RGBA16F; without it the pyramid takes the
    // half-bandwidth R11F_G11F_B10F.
    static constexpr U32 BLOOM_SCALE_NUM[5] = { 1, 3, 1, 1, 1 };
    static constexpr U32 BLOOM_SCALE_DEN[5] = { 1, 4, 2, 4, 8 };
    const U32 bloom_mips   = (U32)std::clamp(in.mBloomMips, 3, (S32)BLOOM_MAX_MIPS);
    const U32 bloom_scale  = (U32)std::clamp(in.mBloomScale, 0, 4);
    const U32 bloom_format = in.mBloomHalation ? GL_RGBA16F : GL_R11F_G11F_B10F;

    struct PackShape
    {
        U32  width;
        U32  height;
        U32  shadow_width;
        U32  shadow_height;
        U32  sun_cascades;
        bool light;
        bool post;
        bool water;
    };

    const auto plan_pack = [&](Pack pack, const PackShape& shape)
    {
        const Id gbuffer = inPack(pack, Slot::GBuffer);
        plan.at(gbuffer).mDesc = ALRenderTargetDesc{ .width   = shape.width,
                                                     .height  = shape.height,
                                                     .color   = gbuffer_color,
                                                     .depth   = main_depth,
                                                     .stencil = true };
        // Always RGBA16F: its alpha carries glow through the post-deferred passes.
        plan.at(inPack(pack, Slot::Screen)) = {
            ALRenderTargetDesc{ .width = shape.width, .height = shape.height, .color = { GL_RGBA16F } }, gbuffer
        };
        // The light map shares the scene's depth for the far-plane test that keeps its passes off the sky. Its four
        // channels are the sun's shadow, the occlusion and the spot lights' two shadows, each a factor its writers clamp
        // to [0, 1], so 8 bits a channel whatever HDR says.
        if (shape.light)
        {
            plan.at(inPack(pack, Slot::Light)) = {
                ALRenderTargetDesc{ .width = shape.width, .height = shape.height, .color = { GL_RGBA8 } }, gbuffer
            };
        }

        // The scene as it stood before the water, which the water reads under its surface: the screen's colour,
        // copied where the water is transparent over the part of the screen the surface can read it at, and the
        // scene's depth, whole, which the surface writes over while it reads it. The colour is a blit's destination
        // and nothing reads its alpha (waterF.glsl and underWaterF.glsl take the colour alone), so under HDR it is
        // R11F_G11F_B10F, half the screen's bandwidth, and with no depth of its own or the scene's: bloom level 0's
        // description at full scale without halation, whose target the pool hands it, bloom's pass coming after the
        // water's. Without HDR 8 bits, the post chain's description. Water that does not refract reads neither.
        const bool refracts = shape.water && refractsWater(pack, in.mTransparentWater);
        if (refracts)
        {
            plan.at(inPack(pack, Slot::Water)).mDesc = ALRenderTargetDesc{
                .width = shape.width, .height = shape.height, .color = { hdr ? (U32)GL_R11F_G11F_B10F : screen_format }
            };
        }
        // The depth copy serves the haze passes too where the context has no texture barrier, whether the water
        // refracts or not: each copies the scene's depth before it reads it (LLPipeline::hazeDepth), the draws before it
        // in its binding having written the scene's own.
        if (refracts || (shape.water && !in.mTextureBarrier))
        {
            plan.at(inPack(pack, Slot::DepthCopy)).mDesc =
                ALRenderTargetDesc{ .width = shape.width, .height = shape.height, .depth = main_depth };
        }

        if (shape.post)
        {
            const ALRenderTargetDesc post{ .width = shape.width, .height = shape.height, .color = { post_format } };
            plan.at(inPack(pack, Slot::PostPing)).mDesc = post;
            plan.at(inPack(pack, Slot::PostPong)).mDesc = post;

            if (hdr)
            {
                const U32 base_width  = std::max(1u, shape.width * BLOOM_SCALE_NUM[bloom_scale] / BLOOM_SCALE_DEN[bloom_scale]);
                const U32 base_height = std::max(1u, shape.height * BLOOM_SCALE_NUM[bloom_scale] / BLOOM_SCALE_DEN[bloom_scale]);
                for (U32 mip = 0; mip < bloom_mips; ++mip)
                {
                    const U32 width  = std::max(1u, base_width >> mip);
                    const U32 height = std::max(1u, base_height >> mip);
                    plan.at(bloomMip(pack, mip)).mDesc =
                        ALRenderTargetDesc{ .width = width, .height = height, .color = { bloom_format } };
                    if (width == 1 && height == 1)
                    {
                        break;
                    }
                }
            }
        }

        if (in.mShadowDetail > 0)
        {
            for (U32 cascade = 0; cascade < shape.sun_cascades; ++cascade)
            {
                plan.at(sunShadow(pack, cascade)).mDesc = ALRenderTargetDesc{ .width  = shape.shadow_width,
                                                                              .height = shape.shadow_height,
                                                                              .depth  = shadow_depth };
            }
        }
    };

    // The main view, at the render size: its light map for shadows or SSAO, its shadows scaled by
    // RenderShadowResolutionScale.
    U32 width  = 0;
    U32 height = 0;
    in.renderSize(width, height);
    width  = std::max(width, 1u);
    height = std::max(height, 1u);
    const F32 shadow_scale = std::max(0.f, in.mShadowScale);
    const bool light = in.mShadowDetail > 0 || in.mSSAO;
    plan_pack(Pack::Main, { width, height, blur_happy_size(width, shadow_scale), blur_happy_size(height, shadow_scale),
                            SUN_CASCADES, light, true, true });

    // A capture's light map is written by its sun pass, which runs for shadows alone: SSAO is the main view's, and a
    // capture's soften pass reads the light map whatever it holds (renderDeferredLighting), so without shadows it reads
    // the white a missing map binds.
    const bool capture_light = in.mShadowDetail > 0;

    // A probe face renders at the probe resolution times its supersample, which the manager downsamples to its probe,
    // and its shadow maps at the size the full supersample of four gave them: a coarser capture is no reason for coarser
    // shadows in it. A capture renders two sun cascades, or a probe's all four where AlchemyRenderProbeAllShadowCascades
    // asks (generateSunShadow), and has maps for those alone. Its post targets and bloom are the GLTF material preview's,
    // which the probes' captures never read, so they are there only while a preview is alive. The pack is there while
    // probes are on or a preview is alive, and the manager's own resources while probes are. Its sun shadows, light
    // map and depth copy are the captures' alone: a preview draws without the sun's shadows and draws no water
    // (LLGLTFPreviewTexture::render). A capture's water does not refract (refractsWater), so the copy is the haze's,
    // without a texture barrier, and the pack has no copy of the screen.
    if (in.mProbes || in.mMaterialPreview)
    {
        const U32 probe_size     = in.mProbeResolution * in.mProbeSuperSample;
        const U32 probe_shadow   = blur_happy_size(in.mProbeResolution * 4, 1.f);
        const U32 probe_cascades = !in.mProbes ? 0 : in.mProbeAllCascades ? SUN_CASCADES : CAPTURE_SUN_CASCADES;
        plan_pack(Pack::Probe, { probe_size, probe_size, probe_shadow, probe_shadow, probe_cascades,
                                 capture_light && in.mProbes, in.mMaterialPreview, in.mProbes });
    }
    if (in.mProbes)
    {
        plan.mReflectionProbes = ProbeManager{ .mResolution = in.mProbeResolution,
                                               .mSuperSample = in.mProbeSuperSample,
                                               .mHDR = hdr };
    }

    // A mirror's face renders at the hero resolution, never supersampled, with two sun cascades, and runs no post chain.
    // The mirrors render only with reflection probes on (LLHeroProbeManager::update), so the pack and the manager's
    // resources are there only then.
    if (in.mMirrors && in.mProbes)
    {
        const U32 hero_size   = std::max(in.mHeroResolution, 1u);
        const U32 hero_shadow = blur_happy_size(hero_size, 1.f);
        plan_pack(Pack::Hero,
                  { hero_size, hero_size, hero_shadow, hero_shadow, CAPTURE_SUN_CASCADES, capture_light, false, true });
        plan.mHeroProbes = ProbeManager{ .mResolution = hero_size, .mSuperSample = 1, .mHDR = hdr };
    }

    // The spot lights' shadows are the main view's alone, square at its width times the shadow scale, and a texel at
    // the least: a scale near 0 takes the shadows' detail, not the maps.
    if (in.mShadowDetail > 1)
    {
        const U32 spot = std::max((U32)((F32)width * shadow_scale), 1u);
        for (Id id : { Id::SpotShadow0, Id::SpotShadow1 })
        {
            plan.at(id).mDesc = ALRenderTargetDesc{ .width = spot, .height = spot, .depth = shadow_depth };
        }
    }

    // SMAA's edges and blend weights. RGBA8 whatever the post format: the neighbourhood blend reads the right edge's
    // weight from the blend buffer's alpha, which RGB10_A2 would leave four levels. The stencil the blend-weights pass
    // skips non-edges by is a pure stencil carrier, never scene depth, so DEPTH24 even under reverse-Z.
    if (in.mFSAAType == 2)
    {
        plan.at(Id::SMAAEdges).mDesc = ALRenderTargetDesc{ .width   = width,
                                                           .height  = height,
                                                           .color   = { GL_RGBA8 },
                                                           .depth   = in.mSMAAStencil ? Depth::D24 : Depth::None,
                                                           .stencil = in.mSMAAStencil };
        plan.at(Id::SMAABlend) = { ALRenderTargetDesc{ .width = width, .height = height, .color = { GL_RGBA8 } },
                                   in.mSMAAStencil ? std::optional<Id>(Id::SMAAEdges) : std::nullopt };
    }

    // Where the session computes, and SSAO's kernel reaches no further than a dispatch holds, SSAO's blur writes the
    // sun's shadow and the blurred occlusion here, which the soften pass reads in place of the light map
    // (ALLightMapBlur): it takes no scratch, and no depth, as a dispatch's destination. The light map's 8 bits a
    // channel, the format blurLightC.glsl stores.
    if (in.mSSAO && in.mCompute && in.mSSAOBlurDispatched)
    {
        plan.at(Id::MainLightBlurred).mDesc = ALRenderTargetDesc{ .width = width, .height = height, .color = { GL_RG8 } };
    }

    // The reflections' source: last frame's scene, without the alpha the reflections drop, so R11F_G11F_B10F under HDR
    // (unsigned, which the lit scene is), and the scene's depth without its stencil, in its format. They trace it
    // only through the probes (bindReflectionProbes): without them no program reads it.
    if (in.mSSR && in.mProbes)
    {
        plan.at(Id::SSRScene).mDesc = ALRenderTargetDesc{ .width  = width,
                                                          .height = height,
                                                          .color  = { hdr ? (U32)GL_R11F_G11F_B10F : screen_format },
                                                          .depth  = main_depth };
    }

    // The sky behind everything at half size, in the format the sky's colour lands in: the emissive attachment's float
    // with HDR, 8 bits otherwise.
    plan.at(Id::SkyBehind).mDesc = ALRenderTargetDesc{ .width  = std::max(width / 2, 1u),
                                                       .height = std::max(height / 2, 1u),
                                                       .color  = { hdr && in.mEmissive ? (U32)GL_R11F_G11F_B10F
                                                                                       : (U32)GL_RGBA8 } };

    // The legacy glow, which HDR's bloom replaces.
    if (!hdr)
    {
        const U32 glow_height = 1u << std::clamp(in.mGlowResolutionPow, 0, 9);
        for (Id id : { Id::Glow0, Id::Glow1, Id::Glow2 })
        {
            plan.at(id).mDesc = ALRenderTargetDesc{ .width  = GLOW_WIDTH,
                                                    .height = glow_height,
                                                    .color  = { in.mGlowHDR ? (U32)GL_RGBA16F : (U32)GL_RGBA8 } };
        }
    }

    // The texture downscaler's scratch (LLImageGL::scaleDown), every UI preview's target, and the appearance bake's.
    plan.at(Id::DownRes).mDesc = ALRenderTargetDesc{ .width = DOWNRES_SIZE, .height = DOWNRES_SIZE, .color = { GL_RGBA8 } };
    plan.at(Id::Preview).mDesc = ALRenderTargetDesc{ .width  = PREVIEW_SIZE,
                                                     .height = PREVIEW_SIZE,
                                                     .color  = { GL_SRGB8_ALPHA8 },
                                                     .depth  = main_depth };
    plan.at(Id::Bake).mDesc = ALRenderTargetDesc{ .width = in.mBakeWidth, .height = in.mBakeHeight, .color = { GL_RGBA8 } };

    plan.at(Id::BrdfLut).mDesc = ALRenderTargetDesc{ .width = BRDF_LUT_SIZE, .height = BRDF_LUT_SIZE, .color = { GL_RG16F } };

    // The auto-exposure: the meter, its histogram (RGBA32F: a bin sums thousands of points, past what a half float
    // counts exactly), the exposure and last frame's; and the lens flare's sun state, two swapped each frame.
    plan.at(Id::ExposureMeter).mDesc =
        ALRenderTargetDesc{ .width = EXPOSURE_METER_WIDTH, .height = EXPOSURE_METER_HEIGHT, .color = { GL_RGBA16F } };
    plan.at(Id::ExposureHistogram).mDesc =
        ALRenderTargetDesc{ .width = EXPOSURE_HISTOGRAM_BINS, .height = 1, .color = { GL_RGBA32F } };
    plan.at(Id::Exposure).mDesc     = ALRenderTargetDesc{ .width = 1, .height = 1, .color = { GL_RGBA16F } };
    plan.at(Id::ExposureLast).mDesc = ALRenderTargetDesc{ .width = 1, .height = 1, .color = { GL_RGBA16F } };
    plan.at(Id::LensFlareA).mDesc   = ALRenderTargetDesc{ .width = 2, .height = 1, .color = { GL_RGBA16F } };
    plan.at(Id::LensFlareB).mDesc   = ALRenderTargetDesc{ .width = 2, .height = 1, .color = { GL_RGBA16F } };

    plan.at(Id::Noise).mDesc     = ALRenderTargetDesc{ .color = { GL_RGB16F } };
    plan.at(Id::TrueNoise).mDesc = ALRenderTargetDesc{ .color = { GL_RGB16F } };
    plan.at(Id::ToneCurve).mDesc = ALRenderTargetDesc{ .color = { GL_RGBA16 } };

    // SMAA's area and search tables, and the debug image it can blend, for as long as SMAA is what antialiases, as its
    // edges and blend weights are.
    if (in.mFSAAType == 2)
    {
        plan.at(Id::SMAAArea).mDesc   = ALRenderTargetDesc{ .color = { GL_RG8 } };
        plan.at(Id::SMAASearch).mDesc = ALRenderTargetDesc{ .color = { GL_R8 } };
        plan.at(Id::SMAASample).mDesc = ALRenderTargetDesc{ .color = { GL_RGB8 } };
    }

    return plan;
}

// static
ALRenderResourcePlan::IdSet ALRenderResourcePlan::changed(const ALRenderResourcePlan& before, const ALRenderResourcePlan& after)
{
    IdSet ids;
    for (size_t i = 0; i < ID_COUNT; ++i)
    {
        ids.set(i, before.mResources[i] != after.mResources[i]);
    }
    return ids;
}

// static
ALRenderResourcePlan::IdSet ALRenderResourcePlan::refilled(const ALRenderResourceInputs& before,
                                                           const ALRenderResourceInputs& after)
{
    IdSet ids;
    ids.set((size_t)Id::Noise, before.mDeferredNoise != after.mDeferredNoise);
    return ids;
}

// static
std::optional<ALRenderResourcePlan::Pack> ALRenderResourcePlan::packOf(Id id)
{
    if ((size_t)id >= PACK_IDS)
    {
        return std::nullopt;
    }
    return (Pack)((size_t)id / (size_t)Slot::COUNT);
}

// static
ALRenderResourcePlan::Slot ALRenderResourcePlan::slotOf(Id id)
{
    llassert((size_t)id < PACK_IDS);
    return (Slot)((size_t)id % (size_t)Slot::COUNT);
}

// static
std::string_view ALRenderResourcePlan::name(Id id)
{
    if ((size_t)id < PACK_IDS)
    {
        return PACK_NAMES[(size_t)id / (size_t)Slot::COUNT][(size_t)id % (size_t)Slot::COUNT];
    }
    if (id < Id::COUNT)
    {
        return OWN_NAMES[(size_t)id - PACK_IDS];
    }
    return {};
}

// static
bool ALRenderResourcePlan::followsRenderSize(Id id)
{
    if (const std::optional<Pack> pack = packOf(id))
    {
        return *pack == Pack::Main;
    }
    switch (id)
    {
        case Id::SpotShadow0:
        case Id::SpotShadow1:
        case Id::SMAAEdges:
        case Id::SMAABlend:
        case Id::SkyBehind:
        case Id::SSRScene:
        case Id::MainLightBlurred:
            return true;
        default:
            return false;
    }
}

// static
bool ALRenderResourcePlan::drivesRenderSize(Id id)
{
    if (!followsRenderSize(id) || isShadowMap(id))
    {
        return false;
    }
    const std::optional<Pack> pack = packOf(id);
    return !pack || slotOf(id) < Slot::Bloom0;
}

// static
bool ALRenderResourcePlan::isShadowMap(Id id)
{
    if (packOf(id))
    {
        const Slot slot = slotOf(id);
        return slot >= Slot::Sun0 && slot <= Slot::Sun3;
    }
    return id == Id::SpotShadow0 || id == Id::SpotShadow1;
}

// static
bool ALRenderResourcePlan::isTexture(Id id)
{
    return id >= Id::Noise && id < Id::COUNT;
}

// static
bool ALRenderResourcePlan::isTransient(Id id)
{
    if (packOf(id))
    {
        const Slot slot = slotOf(id);
        return slot == Slot::Light || slot == Slot::PostPing || slot == Slot::PostPong || slot == Slot::Water ||
               slot == Slot::DepthCopy || slot >= Slot::Bloom0;
    }
    return id == Id::SMAAEdges || id == Id::SMAABlend || id == Id::Glow0 || id == Id::Glow1 || id == Id::Glow2 ||
           id == Id::SkyBehind || id == Id::MainLightBlurred || id == Id::Preview || id == Id::Bake;
}

// static
bool ALRenderResourcePlan::describes(const ALRenderTargetDesc& desc) const
{
    return std::any_of(mResources.begin(), mResources.end(),
                       [&desc](const Resource& resource) { return resource.mDesc == desc && !resource.mDepthFrom; });
}

bool ALRenderResourcePlan::isPrewarmed(Id id)
{
    return isTransient(id) && id != Id::Preview && id != Id::Bake;
}

// static
bool ALRenderResourcePlan::forMaterialPreview(Id id)
{
    if (packOf(id) != Pack::Probe)
    {
        return false;
    }
    const Slot slot = slotOf(id);
    return slot == Slot::PostPing || slot == Slot::PostPong || slot >= Slot::Bloom0;
}

// static
ALRenderResourcePlan::IdSet ALRenderResourcePlan::idsOf(Pack pack)
{
    return idsWhere([pack](Id id) { return packOf(id) == pack; });
}
