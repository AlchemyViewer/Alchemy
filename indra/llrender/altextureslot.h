/**
 * @file altextureslot.h
 * @brief ALTextureSlot definition
 *
 * One texture binding point: what resource it holds, what target that resource has, and
 * which sampler object it is read through. Nothing else.
 *
 * This was LLTexUnit, and the rename is the point. A GL "texture unit" was a bundle of state
 * -- an enable bit per target, a texenv combiner, filtering and wrap parameters -- that a
 * fixed-function pipeline configured and then drew through. None of that survives: filtering
 * and wrapping moved to sampler objects (alsamplerstate.h), the combiners went with the
 * fragment shader, and the enable bits went with them. What is left is a slot, which is also
 * what D3D11 and Vulkan model -- a resource view bound at an index, plus a sampler bound at
 * the same index.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#pragma once

#include "alsamplerstate.h"
#include "llglheaders.h"

class LLCubeMap;
class LLImageGL;
class LLRender;
class LLRenderTarget;
class LLTexture;

constexpr U32 AL_NUM_TEXTURE_SLOTS = 32;

class ALTextureSlot
{
    friend class LLRender;
public:
    static U32 sWhiteTexture;

    // --- sampler traffic, per frame ---------------------------------------------------
    //
    // Sampler objects cost a glBindSampler that baked texture state did not: the mode used to
    // ride along with the texture. Whether that matters depends entirely on how often the
    // sampler actually CHANGES, which is what these measure. Reset in LLImageGL::updateStats.
    //
    // sSamplerBinds / sTextureBinds is the number to look at. Near zero means the redundancy
    // check is doing its job and samplers are close to free; near one means nearly every
    // texture bind drags a sampler bind with it.
    static U32 sSamplerBinds;   // glBindSampler calls ISSUED
    static U32 sSamplerSkips;   // suppressed by the mCurrSampler check
    static U32 sTextureBinds;   // texture binds through bindFast/bindImpl, for the ratio

    // ... of sSamplerBinds, the ones that went through bindSampler() and therefore forced a
    // gGL.flush(). Those are the expensive ones by a wide margin: the call itself is a state
    // change, but the flush SPLITS THE DRAW BATCH. bindFastImpl deliberately skips the flush,
    // so a high total with a low flushed count is cheap and the reverse is not.
    static U32 sSamplerBindsFlushed;

    // ... of sSamplerBindsFlushed, the ones where the flush found geometry actually queued
    // (gGL.mCount > 0) and emitted a draw. An empty flush is a branch and a return; THIS is
    // the real batch-split count, and where it comes from is the question that matters --
    // immediate-mode UI alternating glyph-atlas and image samplers on one slot is the usual
    // suspect, since scene geometry draws from VBOs with nothing queued in gGL.
    static U32 sSamplerBindsSplitBatch;

    // ... of sSamplerBinds, the ones issued by the shadow-map compare-sampler cycle:
    // releaseCompareSamplerUnits dropping slots to sampler 0 plus bindShadowMaps putting the
    // compare sampler back. Scene-dependent through the alpha pass's emissive groups (each
    // costs a release + restore), which makes it the prime suspect for a large standing
    // total that UI toggles barely move.
    static U32 sSamplerBindsShadowCycle;

    // What KIND of resource a slot holds. A property of the resource, not of the slot -- this
    // is D3D11's SRV dimension, and it lives here only because the slot has to name a target
    // to issue a classic glBindTexture.
    //
    // The four other enums that used to sit alongside it are gone: eTextureBlendType,
    // eTextureBlendOp and eTextureBlendSrc described the fixed-function texture combiner and
    // had no references anywhere in the tree, and eTextureColorSpace was likewise unused
    // (sRGB is a format property now, plus ALSampler::SRGBDecode at the read).
    // eTextureFilterOptions and eTextureAddressMode moved into ALSampler, which is what
    // actually decides filtering and wrapping; eTextureMipGeneration moved to LLRenderTarget,
    // its only consumer.
    typedef enum
    {
        TT_TEXTURE = 0,         // Standard 2D Texture
        TT_RECT_TEXTURE,        // Non power of 2 texture
        TT_CUBE_MAP,            // 6-sided cube map texture
        TT_CUBE_MAP_ARRAY,      // Array of cube maps
        TT_MULTISAMPLE_TEXTURE, // see GL_ARB_texture_multisample
        TT_TEXTURE_3D,          // standard 3D Texture
        TT_NONE,                // Slot holds nothing
    } eTextureType;

    ALTextureSlot(S32 index = -1);

    // Refreshes renderer state of the slot to the cached values
    // Needed when the render context has changed and invalidated the current state
    void refreshState(void);

    // returns the index of this slot
    S32 getIndex(void) const { return mIndex; }

    // Binds the LLImageGL to this slot WITHOUT selecting a sampler -- the slot keeps whatever
    // sampler the previous bind left. For bind-to-edit (allocation, upload, glGenerateMipmap),
    // where nothing samples through the slot; a bind that will be sampled goes through
    // bindSampled/bindFast so the caller names how.
    bool bind(LLImageGL* texture, bool for_rendering = false, bool forceBind = false, S32 usename = 0);

    // Bind AND select the sampler in one step. The caller names the whole sampling mode;
    // the texture contributes nothing -- it carries no sampling state to contribute.
    bool bindSampled(LLTexture* texture, ALSampler key, bool forceBind = false);
    bool bindSampled(LLImageGL* texture, ALSampler key, bool forceBind = false);

    // bind implementation for inner loops
    // makes the following assumptions:
    //  - No need for gGL.flush()
    //  - texture is not null
    //  - gl_tex->getTexName() is not zero
    //  - This texture is not being bound redundantly
    // The CALLER names how it wants to sample; the resource contributes nothing.
    void bindFast(LLTexture* texture, ALSampler key);

    // Binds a cubemap to this slot
    bool bind(LLCubeMap* cubeMap, ALSampler key);

    // Binds a render target to this slot
    // sampler is an ALSamplerCache name, or 0 for the target's own per-attachment default.
    bool bind(LLRenderTarget * renderTarget, bool bindDepth = false, U32 sampler = 0);

    // Manually binds a texture to the slot
    // sampler is an ALSamplerCache name, or 0 to sample through the texture object's own
    // state. Applied even when the texture binding is unchanged -- see bindSampler.
    bool bindManual(eTextureType type, U32 texture, U32 sampler = 0);

    // Release whatever this slot holds.
    //
    // NO TYPE ARGUMENT: the slot knows its own target. The pair this replaces --
    // unbind(eTextureType) plus disable() -- made the caller state the target and then did
    // nothing at all if it guessed wrong, so an unbind(TT_TEXTURE) aimed at a slot still
    // holding a cube map silently left it bound. Callers were starting to work around it by
    // hand (unbind(getCurrType()) guarded on TT_NONE, which is exactly this function).
    void unbind();

    // Fast but unsafe version of unbind
    void unbindFast();

    // NO setTextureFilteringOption / setTextureAddressMode here.
    //
    // Filtering and addressing are properties of a SAMPLER, not of a texture and not of a
    // slot, which is the model D3D11/12 and Vulkan have and the one this renderer now uses
    // throughout. Write the sampler to the bind call. There is no way to put sampling state
    // on a texture at all -- LLImageGL stopped carrying any, which is what makes that
    // impossible rather than merely discouraged.
    //
    // The removed pair wrote glTexParameteri onto whatever texture object was bound, and a
    // bound sampler object overrides that state wholesale -- so the call did nothing and
    // reported nothing. That silence already cost the RLV sphere effect its point filter
    // (402770506d). They also read almost identically to the LLImageGL setters that ARE
    // correct, which made reaching for the wrong one easy. Reintroducing either means
    // reintroducing both problems.

    // Select the sampler object this slot samples through, from ALSamplerCache.
    //
    // Pass 0 to fall back to the bound texture object's own filter/wrap/compare state.
    // A sampler object OVERRIDES that state wholesale while bound, so a leftover
    // glTexParameteri on a slot with a non-zero sampler is a silent no-op -- when
    // converting a bind path, convert every filtering call that follows it.
    //
    // Never selects a sampler for TT_MULTISAMPLE_TEXTURE: sampling a multisample
    // texture through a sampler object is an error at draw time.
    void bindSampler(U32 sampler);

    static U32 getInternalType(eTextureType type);

    U32 getCurrTexture(void) const { return mCurrTexture; }

    eTextureType getCurrType(void) const { return mCurrTexType; }

protected:
    friend class LLRender;

    S32                 mIndex;
    U32                 mCurrTexture;
    eTextureType        mCurrTexType;
    // The sampler object currently bound to this slot, so a redundant selection can be
    // skipped. Reset by clearSamplers() when the objects behind the names are dropped.
    U32                 mCurrSampler = 0;

    // Make this the active GL texture unit, so the classic glBindTexture calls below land on
    // it. Internal only: it is a consequence of not using glBindTextureUnit, not something a
    // caller has a reason to ask for.
    //
    // It was public for a long time and had 54 external callers, none of which needed it --
    // every bind and unbind path here activates for itself, and the rest were guarding
    // gGL.matrixMode(MM_TEXTURE0) back when MM_TEXTURE resolved through the active unit. It
    // stopped doing that (see LLRender::eMatrixMode) and the guards became ceremony.
    void activate(void);

    // Record the target this slot's binds will use, activating it first. Called by the bind
    // paths; not an "enable" in any sense GL still has.
    void setTarget(eTextureType type);

    void debugTextureUnit(void);

    void bindFastImpl(LLTexture* texture, LLImageGL* gl_tex, U32 sampler);

    bool bindImpl(LLTexture* texture, bool forceBind, ALSampler key);
};
