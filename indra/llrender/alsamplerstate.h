/**
 * @file alsamplerstate.h
 * @brief ALSamplerDesc / ALSamplerCache class definition
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

// llglheaders.h, NOT llgl.h: this header is included by llrender.h, and llgl.h pulls in
// llglstates.h -> llimagegl.h -> llrender.h, which re-enters a half-parsed llrender.h and
// leaves llimagegl.h looking at an undefined slot class. Only the GL enum constants are
// needed here; gGLManager is used from the .cpp.
//
// Nothing else. This header used to include lltexunit.h for the filter/address enums it
// encoded against -- which made the sampler description depend on the binding slot, exactly
// backwards, and forced that header to be split out of llrender.h to break the resulting
// cycle. ALSampler owns its encoding outright now, so this file stands alone and the slot
// header (altextureslot.h) includes IT.
#include "llglheaders.h"

#include <array>
#include <utility>
#include <vector>

// Sampling intent, as a packed bitmask.
//
// This is what a bind site names. It says how the caller wants to READ a texture, and it is
// deliberately not something a texture can carry: two materials can reference one image and
// want different wrap modes (glTF specifies sampler state per texture REFERENCE, not per
// image), so a resource that decided its own filtering would have to pick a winner and be
// wrong for everyone else. Samplers belong to the use, not the data -- which is also the only
// model D3D11/12 and Vulkan offer.
//
// The value IS the table index. Fields sit in disjoint bit ranges, so composing with | lands
// directly on a slot, and a constexpr composition folds to a constant at the call site --
// there is no lookup, no hashing, and nothing to resolve per draw.
//
// LIMIT WORTH KNOWING: packed fields cannot catch two values from the SAME field being ORed
// together. `Bilinear | Trilinear` is 1|2 == 3, which reads as Anisotropic rather than as an
// error. The enum class stops every cross-type mistake (an int, a GL enum, an
// eTextureFilterOptions), which is the class of bug that actually occurred; same-field
// collisions are inherent to flag words and behave the same way in D3D and Vulkan.
enum class ALSampler : U16
{
    // --- minification/magnification, bits 0-1, mutually exclusive ---
    //
    // Ordered weakest-to-strongest on purpose: makeDesc's mip-filter ladder compares against
    // these rather than enumerating, so a value's rank is part of its meaning.
    Point       = 0u,
    Bilinear    = 1u,
    Trilinear   = 2u,
    Anisotropic = 3u,

    // --- addressing, bits 2-3, mutually exclusive ---
    //
    // The numbering here indexes makeDesc's GL address-mode table directly.
    Wrap   = 0u << 2,
    Mirror = 1u << 2,
    Clamp  = 2u << 2,

    // --- depth compare, bit 4. Makes this a shadow sampler. ---
    Compare = 1u << 4,

    // --- bit 5: apply the sRGB transfer function on read (GL_EXT_texture_sRGB_decode) ---
    //
    // OPT IN, which inverts GL's default. GL decodes an sRGB-format texture on every read
    // unless told not to, so the transfer function is applied by the format rather than by
    // anyone's decision, and a pass that wants the stored bits has no say. That is backwards
    // for a renderer that converts explicitly: 26 shader modules import environment.srgb and
    // do the conversion themselves, and hardware decode underneath them is a second,
    // invisible conversion nobody wrote.
    //
    // So every sampler skips the decode unless it names this, and the texture objects skip
    // it too (see LLImageGL::allocateTexture2D) so a bind with no sampler object behaves the
    // same way. A pass that genuinely wants the hardware to linearise says so.
    //
    // No effect on a non-sRGB format, which is most of them -- the bit only matters where
    // the internal format carries a transfer function to begin with.
    SRGBDecode = 1u << 5,

    // There was a fifth field here, HasMips, recording whether the RESOURCE had a mip
    // chain. It existed because GL treats a mipmapped minification filter on a texture with
    // no mip chain as *incomplete*, which samples black -- so the bind had to reconcile the
    // caller's intent against what the texture actually had, doubling the table to carry a
    // fact that was never sampling intent in the first place.
    //
    // Immutable storage removed the rule it was repairing. glTexStorage2D allocates every
    // level it declares, and an immutable texture clamps TEXTURE_MAX_LEVEL to levels-1, so
    // a one-level texture is mipmap-complete under any minification filter and simply
    // samples level 0. That is what D3D11 always did, and now the only kind of texture we
    // allocate. A sampler describes the use again, with nothing of the resource in it.
};

constexpr ALSampler operator|(ALSampler a, ALSampler b)
{
    return static_cast<ALSampler>(static_cast<U16>(a) | static_cast<U16>(b));
}

constexpr ALSampler& operator|=(ALSampler& a, ALSampler b)
{
    a = a | b;
    return a;
}

// Field widths, for the one place that has to take the mask apart again (makeDesc, because GL
// wants the pieces as separate parameters). Everywhere else the mask IS the interface.
//
// These used to be static_asserts tying the layout to LLTexUnit::eTextureFilterOptions and
// eTextureAddressMode. That was the dependency pointing the wrong way: those enums had no
// remaining callers of their own and survived only as the thing this file encoded against, so
// the authoritative layout was being validated against a copy of itself. The fields are
// declared above; these name their extents.
inline constexpr U16 AL_SAMPLER_FILTER_BITS    = 2;
inline constexpr U16 AL_SAMPLER_FILTER_MASK    = (1u << AL_SAMPLER_FILTER_BITS) - 1u;
inline constexpr U16 AL_SAMPLER_ADDRESS_SHIFT  = AL_SAMPLER_FILTER_BITS;
inline constexpr U16 AL_SAMPLER_ADDRESS_MASK   = 0x3u;
// The largest addressing encoding that names a real mode. The fourth is what
// ALSamplers::TargetDefault is built on -- see there.
inline constexpr U16 AL_SAMPLER_ADDRESS_MAX    = static_cast<U16>(ALSampler::Clamp) >> AL_SAMPLER_ADDRESS_SHIFT;

// Filter rank, for the ladder in makeDesc. Returned as a plain integer because it is compared
// by magnitude; the enum class has no ordering operators and should not grow any.
constexpr U16 alSamplerFilter(ALSampler key)
{
    return static_cast<U16>(key) & AL_SAMPLER_FILTER_MASK;
}

// Addressing encoding, already shifted down to index the GL address-mode table.
constexpr U16 alSamplerAddress(ALSampler key)
{
    return (static_cast<U16>(key) >> AL_SAMPLER_ADDRESS_SHIFT) & AL_SAMPLER_ADDRESS_MASK;
}

// Test a single-bit field (Compare, SRGBDecode).
constexpr bool alSamplerHas(ALSampler key, ALSampler bit)
{
    return (static_cast<U16>(key) & static_cast<U16>(bit)) != 0;
}

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
    // GL_SKIP_DECODE_EXT or GL_DECODE_EXT. Defaults to SKIP, which is the opposite of GL's
    // own default -- see ALSampler::SRGBDecode. No D3D11_SAMPLER_DESC equivalent: there the
    // SRV's format decides (DXGI_FORMAT_*_UNORM vs _UNORM_SRGB), which is the same choice
    // expressed at a different bind point.
    U32 mSRGBDecode = GL_SKIP_DECODE_EXT;

    bool operator==(const ALSamplerDesc& rhs) const = default;
};

// Named modes, kept as constants rather than as a struct of resolved handles.
//
// These used to be ALCommonSamplers -- five hand-named U32s that had to be refreshed against
// the cache generation, and that needed a new member (and a naming argument) every time a
// call site wanted a combination nobody had needed yet. The mask makes that unnecessary:
// composition IS the name, so these are just the compositions worth having a word for, and
// anything else spells itself at the call site.
namespace ALSamplers
{
    inline constexpr ALSampler PointWrap      = ALSampler::Point     | ALSampler::Wrap;
    inline constexpr ALSampler PointClamp     = ALSampler::Point     | ALSampler::Clamp;
    inline constexpr ALSampler PointMirror    = ALSampler::Point     | ALSampler::Mirror;
    inline constexpr ALSampler BilinearClamp  = ALSampler::Bilinear  | ALSampler::Clamp;
    inline constexpr ALSampler BilinearWrap   = ALSampler::Bilinear  | ALSampler::Wrap;
    inline constexpr ALSampler BilinearMirror = ALSampler::Bilinear  | ALSampler::Mirror;
    inline constexpr ALSampler TrilinearWrap   = ALSampler::Trilinear | ALSampler::Wrap;
    inline constexpr ALSampler TrilinearClamp  = ALSampler::Trilinear | ALSampler::Clamp;
    inline constexpr ALSampler TrilinearMirror = ALSampler::Trilinear | ALSampler::Mirror;
    inline constexpr ALSampler AnisoWrap      = ALSampler::Anisotropic | ALSampler::Wrap;
    inline constexpr ALSampler AnisoClamp     = ALSampler::Anisotropic | ALSampler::Clamp;

    // AnisoWrap with the hardware sRGB->linear decode: the sampler for any COLOUR texture a
    // linear-shading pass reads (diffuse, legacy spec, glTF base/emissive, projector
    // cookies), so each texel is linearised BEFORE the filter averages. One name so the
    // policy has one spelling; data textures (normal/ORM/masks) never take it.
    inline constexpr ALSampler AnisoWrapSRGB  = AnisoWrap | ALSampler::SRGBDecode;

    // Shadow maps: bilinear + clamp with the depth comparison enabled. The LINEAR filter is
    // what gives the hardware its 2x2 PCF on a compare sampler. Anisotropy was inert here --
    // the depth targets are single-level, so there is no mip chain for it to act on -- and
    // only cost the driver a max-anisotropy it could never use; dropped.
    inline constexpr ALSampler ShadowCompare  = ALSampler::Bilinear | ALSampler::Clamp
                                              | ALSampler::Compare;

    // NOT a sampling mode: "resolve from the render target's per-attachment policy"
    // (getDefaultColorSampler -- bilinear for attachment 0, point for the data attachments).
    // Only LLRenderTarget::bindTexture interprets it; it must never reach getSampler().
    // Built on the addressing field's unused fourth encoding, so if it leaks through anyway
    // the masked slot is one warmup() never fills and the cache's debug assert fires instead
    // of silently sampling wrong.
    inline constexpr ALSampler TargetDefault = static_cast<ALSampler>((3u << 2) | (1u << 15));
}

// Owns the sampler objects belonging to one GL context.
//
// The descriptor space the renderer actually uses is small and enumerable -- filter x
// address mode x compare x srgb-decode -- so the common path is a flat array index rather
// than a hash lookup, which matters because it runs per bind. That table is built up front
// by warmup(), NOT lazily -- the enum get() below only indexes it -- and lives until the
// context goes away. The objects are immutable, so sharing one between unrelated call sites
// is safe by construction. Anything that calls clear() owes a matching warmup() before the
// next bind; see LLRender::clearSamplers().
//
// ONE INSTANCE PER GL CONTEXT, owned by LLRender (which is itself thread_local -- the
// viewer runs a second shared context on the texture upload thread). That is not an
// optimisation, it is the correctness boundary. Shared process-wide storage here means the
// upload thread's gGL.shutdown() deletes the samplers the main thread is still rendering
// with: the contexts share a namespace and the worker's context is still current at that
// point, so the deletes succeed silently and the render thread is left binding freed names.
//
// Keeping the objects at the same scope as the bindings that reference them (ALTextureSlot::
// mCurrSampler, also per-LLRender) is what makes the lifetime work out. Reach it through
// LLRender::getSampler() / clearSamplers() rather than constructing one.
class ALSamplerCache
{
public:
    // Resolve the sampler for a mask. Pure lookup -- warmup() must already have run.
    //
    // The mask is the slot, so there is no arithmetic here at all: a constexpr key at the
    // call site -- which is nearly all of them -- makes this one load and one predictable
    // branch. Everything that can fail (entry-point checks, descriptor construction,
    // glCreateSamplers) lives in createSlot(), which only warmup() reaches.
    //
    // Returns 0 if sampler objects are unavailable, so callers never need to branch.
    U32 get(ALSampler key) const
    {
        const U32 name = mSamplers[static_cast<U16>(key) & KEY_MASK];

        // Zero means either warmup() has not run or the key was built by arithmetic rather
        // than by composing ALSampler values -- the address field has a fourth encoding that
        // no named value produces, and its slots stay empty. Either way the caller would
        // silently get GL's defaults, so say so in debug rather than render it.
        llassert(name != 0);
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

    // Create every reachable sampler up front.
    //
    // The table is 64 slots of which 48 are reachable, and a sampler object is a few words of
    // driver state -- so building them all costs less than the branch that testing for them
    // would cost on every bind, forever. Call once the context is up, and again after clear()
    // drops them (an anisotropy change does that).
    void warmup();

    // Bumped every time clear() drops the objects, so callers that cache a resolved name
    // can tell in one compare whether theirs is still valid. Never 0, so 0 is usable as
    // "never resolved".
    U32 getGeneration() const { return mGeneration; }

    // Build the GL descriptor for a mask. Exposed for tests and for callers that want to
    // inspect what get() will produce; get() is the normal entry point.
    static ALSamplerDesc makeDesc(ALSampler key);

private:
    // Builds the descriptor and the GL object for one slot. Returns 0 if sampler objects are
    // unavailable at all, which only happens on a driver that cannot do them.
    U32 createSlot(ALSampler key);

    static U32 create(const ALSamplerDesc& desc);

    // Six bits: filter(2) + address(2) + compare(1) + srgb-decode(1). 16 of the 64 slots
    // are unreachable (address only uses 3 of its 4 values) and stay 0 -- 64 U32s is 256
    // bytes, and the alternative is arithmetic on every bind to compact them away.
    static constexpr U32 KEY_BITS    = 6;
    static constexpr U32 KEY_MASK    = (1u << KEY_BITS) - 1u;
    static constexpr U32 NUM_SAMPLERS = 1u << KEY_BITS;

    // 0 means "not created yet". GL never hands out 0 as a sampler name.
    std::array<U32, NUM_SAMPLERS> mSamplers = {};

    // Descriptors outside what a mask can express -- notably a mixed min/mag pair such as the
    // deferred lighting LUT's mag=LINEAR + min=NEAREST. Expected to hold one or two entries,
    // so a linear scan beats hashing; clear() empties this alongside mSamplers.
    std::vector<std::pair<ALSamplerDesc, U32>> mCustomSamplers;

    U32 mGeneration = 1;
};
