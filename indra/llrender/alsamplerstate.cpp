/**
 * @file alsamplerstate.cpp
 * @brief ALSamplerCache implementation
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

#include "linden_common.h"

#include "alsamplerstate.h"

#include "llgl.h"     // gGLManager
#include "llrender.h" // LLRender::sAnisotropicFilteringLevel

namespace
{
    // Same table LLTexUnit used before samplers existed; kept in the same order as
    // LLTexUnit::eTextureAddressMode.
    constexpr U32 sGLAddressMode[] =
    {
        GL_REPEAT,
        GL_MIRRORED_REPEAT,
        GL_CLAMP_TO_EDGE
    };
}

// static
ALSamplerDesc ALSamplerCache::makeDesc(LLTexUnit::eTextureFilterOptions filter,
                                       LLTexUnit::eTextureAddressMode   address,
                                       bool                             has_mips,
                                       bool                             compare)
{
    ALSamplerDesc desc;

    desc.mMagFilter = (filter == LLTexUnit::TFO_POINT) ? GL_NEAREST : GL_LINEAR;

    // Mirrors the old LLTexUnit::setTextureFilteringOptionFast ladder exactly, including
    // its asymmetry: TRILINEAR and above get a linear mip filter, BILINEAR gets a nearest
    // one, and POINT stays nearest in both. Without a mip chain every case collapses to
    // the non-mipmapped filter -- selecting a mipmap mode there would make the texture
    // incomplete and sample black.
    if (filter >= LLTexUnit::TFO_TRILINEAR && has_mips)
    {
        desc.mMinFilter = GL_LINEAR_MIPMAP_LINEAR;
    }
    else if (filter >= LLTexUnit::TFO_BILINEAR)
    {
        desc.mMinFilter = has_mips ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR;
    }
    else
    {
        desc.mMinFilter = has_mips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
    }

    desc.mWrapS = desc.mWrapT = desc.mWrapR = sGLAddressMode[address];

    if (gGLManager.mHasAnisotropic && filter == LLTexUnit::TFO_ANISOTROPIC
        && LLRender::sAnisotropicFilteringLevel > 1.f)
    {
        desc.mMaxAnisotropy = llclamp(LLRender::sAnisotropicFilteringLevel, 1.f, gGLManager.mMaxAnisotropy);
    }

    if (compare)
    {
        desc.mCompareMode = GL_COMPARE_REF_TO_TEXTURE;
        desc.mCompareFunc = GL_LEQUAL;
    }

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

    stop_glerror();

    return name;
}

U32 ALSamplerCache::createSlot(LLTexUnit::eTextureFilterOptions filter,
                               LLTexUnit::eTextureAddressMode   address,
                               bool                             has_mips,
                               bool                             compare)
{
    // Samplers are GL 3.3 core and the viewer floor is 4.1, so this should never be
    // null -- but a driver that failed to resolve them would otherwise crash here
    // rather than fall back to texture-object state, which still works.
    if (!glGenSamplers || !glSamplerParameteri)
    {
        return 0;
    }

    return create(makeDesc(filter, address, has_mips, compare));
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

void ALSamplerCache::refreshCommon()
{
    mCommon.mPointWrap  = get(LLTexUnit::TFO_POINT, LLTexUnit::TAM_WRAP, false);
    mCommon.mPointClamp = get(LLTexUnit::TFO_POINT, LLTexUnit::TAM_CLAMP, false);
    mCommon.mBilinearClamp = get(LLTexUnit::TFO_BILINEAR, LLTexUnit::TAM_CLAMP, false);
    mCommon.mTrilinearWrapMips = get(LLTexUnit::TFO_TRILINEAR, LLTexUnit::TAM_WRAP, true);
    // No mip chain, so the filter resolves to GL_LINEAR -- which is what turns the depth
    // comparison into 2x2 PCF rather than a hard test.
    mCommon.mShadowCompare = get(LLTexUnit::TFO_ANISOTROPIC, LLTexUnit::TAM_CLAMP, false, true);

    mCommon.mGeneration = mGeneration;
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
