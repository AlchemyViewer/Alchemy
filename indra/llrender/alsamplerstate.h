/**
 * @file alsamplerstate.h
 * @brief ALSamplerDesc / ALSamplerCache class definition
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

// llglheaders.h, NOT llgl.h: this header is included by llrender.h, and llgl.h pulls in
// llglstates.h -> llimagegl.h -> llrender.h, which re-enters a half-parsed llrender.h and
// leaves llimagegl.h looking at an undefined LLTexUnit. Only the GL enum constants are
// needed here; gGLManager is used from the .cpp.
#include "llglheaders.h"
#include "lltexunit.h"

#include <array>
#include <utility>
#include <vector>

// How a texture is *sampled*, as opposed to what it contains.
//
// GL lets both live on the texture object, which makes filtering a property of the
// resource rather than of the use. That does not survive contact with the renderer: the
// post-process ping-pong target is sampled TFO_POINT by some passes and TFO_BILINEAR by
// others in the same frame, so every bind has to re-issue glTexParameteri to put the
// shared object back into the mode this pass wants. Sampler objects express it correctly
// -- immutable, shared, selected at bind -- and they are the only model D3D11 has
// (ID3D11SamplerState is separate from the SRV).
//
// Mirrors D3D11_SAMPLER_DESC deliberately, so the eventual backend maps field-for-field.
//
// NOT covered here, and deliberately left on the texture object, because GL sampler
// objects don't carry them and neither does D3D11's sampler state:
//   - GL_TEXTURE_SWIZZLE_RGBA
//   - GL_TEXTURE_BASE_LEVEL / GL_TEXTURE_MAX_LEVEL
//   - GL_DEPTH_STENCIL_TEXTURE_MODE
struct ALSamplerDesc
{
    U32 mMinFilter     = GL_LINEAR;
    U32 mMagFilter     = GL_LINEAR;
    U32 mWrapS         = GL_REPEAT;
    U32 mWrapT         = GL_REPEAT;
    U32 mWrapR         = GL_REPEAT;
    F32 mMaxAnisotropy = 1.f;
    // GL_NONE, or GL_COMPARE_REF_TO_TEXTURE for a shadow sampler.
    U32 mCompareMode = GL_NONE;
    U32 mCompareFunc = GL_LEQUAL;

    bool operator==(const ALSamplerDesc& rhs) const = default;
};

// The sampling modes used often enough, and in enough places, to be worth naming.
//
// Reach these through gGL.commonSamplers(). They exist so a call site that wants a fixed,
// well-known mode says which one rather than respelling filter/address/mips arguments --
// the analogue of D3D12 static samplers or Vulkan immutable samplers, and the set the
// eventual backend would declare up front.
//
// Extend as new fixed modes appear. Dynamic modes (LLRenderTarget::bindTexture, which takes
// its filter from the caller) keep using ALSamplerCache::get() directly.
struct ALCommonSamplers
{
    U32 mPointWrap          = 0; // tiled screen-space noise, unfiltered lookups
    U32 mPointClamp         = 0; // data: G-buffer, depth, SMAA predication, impostors
    U32 mBilinearClamp      = 0; // LUTs sampled with interpolation: SMAA area/search
    U32 mTrilinearWrapMips  = 0; // tiling surfaces that recede: the manipulator grid
    U32 mShadowCompare      = 0; // shadow maps; anisotropic + clamp, PCF via depth compare

    // Generation of the cache these were resolved from; 0 = not yet resolved.
    U32 mGeneration = 0;
};

// Owns the sampler objects belonging to one GL context.
//
// The descriptor space the renderer actually uses is small and enumerable -- filter x
// address mode x has-mips x compare -- so the common path is a flat array index rather
// than a hash lookup, which matters because it runs per bind. Objects are created lazily
// on first use and then live until the context goes away; they are immutable, so sharing
// one between unrelated call sites is safe by construction.
//
// ONE INSTANCE PER GL CONTEXT, owned by LLRender (which is itself thread_local -- the
// viewer runs a second shared context on the texture upload thread). That is not an
// optimisation, it is the correctness boundary. Shared process-wide storage here means the
// upload thread's gGL.shutdown() deletes the samplers the main thread is still rendering
// with: the contexts share a namespace and the worker's context is still current at that
// point, so the deletes succeed silently and the render thread is left binding freed names.
//
// Keeping the objects at the same scope as the bindings that reference them (LLTexUnit::
// mCurrSampler, also per-LLRender) is what makes the lifetime work out. Reach it through
// LLRender::getSampler() / clearSamplers() rather than constructing one.
class ALSamplerCache
{
public:
    // Resolve (and create on first use) the sampler for a sampling mode.
    //
    // has_mips must be the truth about the *texture*, not a wish: a min filter with a
    // mipmap mode selects an incomplete texture when no mip chain exists, and an
    // incomplete texture samples black. This is the same trap that recreating a render
    // target used to spring, and it does not get louder just because the state moved
    // into a sampler object.
    //
    // Returns 0 (meaning "no sampler; use the texture object's own state") if sampler
    // objects are unavailable, so callers never need to branch.
    //
    // Inline on purpose: this sits in bind paths, and the resolved case is an index
    // computation, one load and one predictable branch. Everything that can fail -- the
    // entry-point check, descriptor construction, glGenSamplers -- lives in the
    // out-of-line createSlot() taken only on first use of each mode.
    U32 get(LLTexUnit::eTextureFilterOptions filter,
            LLTexUnit::eTextureAddressMode   address,
            bool                             has_mips,
            bool                             compare = false)
    {
        llassert((U32)filter < NUM_FILTERS);
        llassert((U32)address < NUM_ADDRESS);

        U32& name = mSamplers[index(filter, address, has_mips, compare)];
        if (name == 0)
        {
            name = createSlot(filter, address, has_mips, compare);
        }
        return name;
    }

    // Drop this thread's sampler objects. Call while the owning context is still current:
    // from LLRender::shutdown(), or when a global feeding the descriptors changes
    // (currently only LLRender::sAnisotropicFilteringLevel, from graphics preferences).
    void clear();

    // Resolve an arbitrary descriptor, for sampling modes eTextureFilterOptions cannot
    // express -- notably a mixed min/mag pair such as the deferred lighting LUT's
    // mag=LINEAR + min=NEAREST, which is neither TFO_POINT nor TFO_BILINEAR.
    //
    // Backed by a small linear-scanned list rather than the flat array, since the keys are
    // not enumerable. Fine for the handful of per-frame binds that need it; use the enum
    // overload on anything per-draw.
    U32 get(const ALSamplerDesc& desc);

    // The named fixed sampling modes for this context. Lazily resolved, and revalidated
    // against the cache generation so a clear() cannot leave freed names behind.
    const ALCommonSamplers& common()
    {
        if (mCommon.mGeneration != mGeneration)
        {
            refreshCommon();
        }
        return mCommon;
    }

    // Bumped every time clear() drops the objects, so callers that cache a resolved name
    // can tell in one compare whether theirs is still valid. Never 0, so 0 is usable as
    // "never resolved".
    U32 getGeneration() const { return mGeneration; }

    // Build the GL descriptor for a sampling mode. Exposed for tests and for callers
    // that want to inspect what get() will produce; get() is the normal entry point.
    static ALSamplerDesc makeDesc(LLTexUnit::eTextureFilterOptions filter,
                                  LLTexUnit::eTextureAddressMode   address,
                                  bool                             has_mips,
                                  bool                             compare);

private:
    // Cold path of get(): builds the descriptor and the GL object. Returns 0 if sampler
    // objects are unavailable, which leaves the slot at 0 and simply retries next time --
    // acceptable, since that only happens on a driver that cannot do samplers at all.
    U32 createSlot(LLTexUnit::eTextureFilterOptions filter,
                   LLTexUnit::eTextureAddressMode   address,
                   bool                             has_mips,
                   bool                             compare);

    static U32 create(const ALSamplerDesc& desc);

    static constexpr U32 NUM_FILTERS  = 4; // TFO_POINT .. TFO_ANISOTROPIC
    static constexpr U32 NUM_ADDRESS  = 3; // TAM_WRAP .. TAM_CLAMP
    static constexpr U32 NUM_MIP      = 2;
    static constexpr U32 NUM_COMPARE  = 2;
    static constexpr U32 NUM_SAMPLERS = NUM_FILTERS * NUM_ADDRESS * NUM_MIP * NUM_COMPARE;

    static constexpr U32 index(U32 filter, U32 address, bool has_mips, bool compare)
    {
        return ((filter * NUM_ADDRESS + address) * NUM_MIP + (has_mips ? 1 : 0)) * NUM_COMPARE
               + (compare ? 1 : 0);
    }

    // 0 means "not created yet". GL never hands out 0 as a sampler name.
    std::array<U32, NUM_SAMPLERS> mSamplers = {};

    // Descriptors outside the enumerable space. Expected to hold one or two entries, so a
    // linear scan beats hashing; clear() empties this alongside mSamplers.
    std::vector<std::pair<ALSamplerDesc, U32>> mCustomSamplers;

    void refreshCommon();

    ALCommonSamplers mCommon;

    U32 mGeneration = 1;
};
