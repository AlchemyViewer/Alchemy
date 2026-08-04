/**
 * @file alsamplerstate.cpp
 * @brief ALSamplerCache implementation
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

#include "alsamplerstate.h"

#include "llgl.h"     // gGLManager
#include "llrender.h" // LLRender::sAnisotropicFilteringLevel

namespace
{
    // Indexed by the addressing field, already shifted down. The enum's numbering is chosen
    // to index this directly -- see ALSampler::Wrap/Mirror/Clamp.
    constexpr U32 sGLAddressMode[] =
    {
        GL_REPEAT,
        GL_MIRRORED_REPEAT,
        GL_CLAMP_TO_EDGE
    };
}

// static
ALSamplerDesc ALSamplerCache::makeDesc(ALSampler key)
{
    // Unpack the mask back into its fields. This is the only place that does: the mask is the
    // interface everywhere else, and the only reason the pieces exist here is that GL wants
    // them as separate parameters.
    const U16  filter  = alSamplerFilter(key);
    const U16  address = alSamplerAddress(key);
    const bool compare = alSamplerHas(key, ALSampler::Compare);
    const bool decode  = alSamplerHas(key, ALSampler::SRGBDecode);

    // The unreachable quarter of the address field. Reaching it means a mask was built by
    // arithmetic rather than by composing ALSampler values.
    llassert(address <= AL_SAMPLER_ADDRESS_MAX);

    ALSamplerDesc desc;

    desc.mMagFilter = (filter == static_cast<U16>(ALSampler::Point)) ? GL_NEAREST : GL_LINEAR;

    // Trilinear and above get a linear mip filter, Bilinear a nearest one, Point stays nearest
    // in both. The asymmetry at Bilinear is deliberate and long-standing.
    //
    // Unconditional now. These used to collapse to the non-mipmapped filter when the
    // resource had no mip chain, because GL calls that combination incomplete and samples
    // black. Every texture carries immutable storage, which is mipmap-complete by
    // construction, so a mip filter on a one-level texture selects level 0 and needs no
    // reconciling -- see the note where ALSampler::HasMips used to be.
    if (filter >= static_cast<U16>(ALSampler::Trilinear))
    {
        desc.mMinFilter = GL_LINEAR_MIPMAP_LINEAR;
    }
    else if (filter >= static_cast<U16>(ALSampler::Bilinear))
    {
        desc.mMinFilter = GL_LINEAR_MIPMAP_NEAREST;
    }
    else
    {
        desc.mMinFilter = GL_NEAREST_MIPMAP_NEAREST;
    }

    desc.mWrapS = desc.mWrapT = desc.mWrapR = sGLAddressMode[address];

    if (gGLManager.mHasAnisotropic && filter == static_cast<U16>(ALSampler::Anisotropic)
        && LLRender::sAnisotropicFilteringLevel > 1.f)
    {
        desc.mMaxAnisotropy = llclamp(LLRender::sAnisotropicFilteringLevel, 1.f, gGLManager.mMaxAnisotropy);
    }

    if (compare)
    {
        desc.mCompareMode = GL_COMPARE_REF_TO_TEXTURE;
        // Reverse-Z shadow maps store reversed depth, so the shadow compare passes when the
        // receiver is GEQUAL the stored occluder. The latch clears the sampler cache on
        // toggle so this re-derives (see LLPipeline::updateReverseZ).
        desc.mCompareFunc = LLRender::sReverseZ ? GL_GEQUAL : GL_LEQUAL;
    }

    desc.mSRGBDecode = decode ? GL_DECODE_EXT : GL_SKIP_DECODE_EXT;

    return desc;
}

// static
U32 ALSamplerCache::create(const ALSamplerDesc& desc)
{
    U32 name = 0;

    // glSamplerParameter* already takes the object by name, so sampler objects need no
    // bind-to-edit even without DSA. glCreateSamplers only buys the guarantee that the
    // object exists in its default state before the first parameter call, which
    // glGenSamplers leaves until first use.
    if (gGLManager.mHasDirectStateAccess)
    {
        glCreateSamplers(1, &name);
    }
    else
    {
        glGenSamplers(1, &name);
    }

    if (!name)
    {
        return 0;
    }

    glSamplerParameteri(name, GL_TEXTURE_MIN_FILTER, desc.mMinFilter);
    glSamplerParameteri(name, GL_TEXTURE_MAG_FILTER, desc.mMagFilter);
    glSamplerParameteri(name, GL_TEXTURE_WRAP_S, desc.mWrapS);
    glSamplerParameteri(name, GL_TEXTURE_WRAP_T, desc.mWrapT);
    glSamplerParameteri(name, GL_TEXTURE_WRAP_R, desc.mWrapR);

    if (gGLManager.mHasAnisotropic)
    {
        glSamplerParameterf(name, GL_TEXTURE_MAX_ANISOTROPY, desc.mMaxAnisotropy);
    }

    if (desc.mCompareMode != GL_NONE)
    {
        glSamplerParameteri(name, GL_TEXTURE_COMPARE_MODE, desc.mCompareMode);
        glSamplerParameteri(name, GL_TEXTURE_COMPARE_FUNC, desc.mCompareFunc);
    }

    // Written unconditionally when supported, including where it matches GL's default, so a
    // sampler's behaviour is fully described by its descriptor rather than partly inherited.
    if (gGLManager.mHasTextureSRGBDecode)
    {
        glSamplerParameteri(name, GL_TEXTURE_SRGB_DECODE_EXT, desc.mSRGBDecode);
    }

    stop_glerror();

    return name;
}

U32 ALSamplerCache::createSlot(ALSampler key)
{
    // Samplers are GL 3.3 core and the viewer floor is 4.1, so this should never be
    // null -- but a driver that failed to resolve them would otherwise crash here
    // rather than fall back to texture-object state, which still works.
    if (!glGenSamplers || !glSamplerParameteri)
    {
        return 0;
    }

    return create(makeDesc(key));
}

U32 ALSamplerCache::get(const ALSamplerDesc& desc)
{
    if (!glGenSamplers || !glSamplerParameteri)
    {
        return 0;
    }

    for (const auto& entry : mCustomSamplers)
    {
        if (entry.first == desc)
        {
            return entry.second;
        }
    }

    const U32 name = create(desc);
    if (name)
    {
        mCustomSamplers.emplace_back(desc, name);
    }
    return name;
}

void ALSamplerCache::warmup()
{
    // Samplers are GL 3.3 core and the viewer floor is 4.1, so this should never bail -- but
    // a driver that failed to resolve the entry points would otherwise leave every slot at 0
    // and every bind on GL's defaults.
    if (!glGenSamplers || !glSamplerParameteri)
    {
        return;
    }

    for (U32 i = 0; i < NUM_SAMPLERS; ++i)
    {
        // The address field has four encodings and only three meanings. Slots with the fourth
        // are unreachable by composing ALSampler values and stay empty; get() asserts on them,
        // which is how a mask built by arithmetic gets caught.
        if (alSamplerAddress(static_cast<ALSampler>(i)) > AL_SAMPLER_ADDRESS_MAX)
        {
            continue;
        }

        if (mSamplers[i] == 0)
        {
            mSamplers[i] = createSlot(static_cast<ALSampler>(i));
        }
    }
}

void ALSamplerCache::clear()
{
    // Only touch GL while a context is actually current. LLRender::shutdown() reaches here
    // with one current on both threads that have a gGL, but ~LLRender calls shutdown()
    // again during thread_local destruction -- after the context is gone. Normally the
    // explicit gGL.shutdown() left every slot zeroed by then, so there is nothing to do;
    // on an abnormal exit that skipped it, glDeleteSamplers with no current context faults
    // inside the driver rather than failing gracefully.
    //
    // The names are dropped either way: they die with the context, so declining to delete
    // them leaks nothing that outlives the process.
    const bool gl_alive = gGLManager.mInited;

    for (U32& name : mSamplers)
    {
        if (name)
        {
            if (gl_alive)
            {
                // Deleting a bound sampler unbinds it from every unit it was bound to, so
                // no explicit unbind is needed here -- but the per-unit cache still
                // believes it is bound, and GL may hand the same name back for the next
                // one created.
                glDeleteSamplers(1, &name);
            }
            name = 0;
        }
    }

    for (auto& entry : mCustomSamplers)
    {
        if (entry.second && gl_alive)
        {
            glDeleteSamplers(1, &entry.second);
        }
    }
    mCustomSamplers.clear();

    // Invalidate every cached resolution of a name from this cache. Skipping 0 keeps it
    // usable as the "never resolved" marker.
    if (++mGeneration == 0)
    {
        mGeneration = 1;
    }

    // NOTE: any per-unit binding that referenced these names is stale now. Releasing those
    // is LLRender::clearSamplers()' job -- it owns both sides.
}
