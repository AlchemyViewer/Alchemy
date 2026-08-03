/**
 * @file lltexunit.cpp
 * @brief LLTexUnit implementation
 *
 * Split out of llrender.cpp; see lltexunit.h for why.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "linden_common.h"

#include "lltexunit.h"

#include "llcubemap.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "lltexture.h"

static const GLenum sGLTextureType[] =
{
    GL_TEXTURE_2D,
    GL_TEXTURE_RECTANGLE,
    GL_TEXTURE_CUBE_MAP,
    GL_TEXTURE_CUBE_MAP_ARRAY,
    GL_TEXTURE_2D_MULTISAMPLE,
    GL_TEXTURE_3D
};

static const GLint sGLAddressMode[] =
{
    GL_REPEAT,
    GL_MIRRORED_REPEAT,
    GL_CLAMP_TO_EDGE
};

LLTexUnit::LLTexUnit(S32 index)
    : mCurrTexType(TT_NONE),
    mCurrTexture(0),
    mHasMipMaps(false),
    mIndex(index)
{
    llassert_always(index < (S32)LL_NUM_TEXTURE_LAYERS);
}

//static
U32 LLTexUnit::getInternalType(eTextureType type)
{
    return sGLTextureType[type];
}

void LLTexUnit::refreshState(void)
{
    // We set dirty to true so that the tex unit knows to ignore caching
    // and we reset the cached tex unit state

    gGL.flush();

    glActiveTexture(GL_TEXTURE0 + mIndex);

    if (mCurrTexType != TT_NONE)
    {
        glBindTexture(sGLTextureType[mCurrTexType], mCurrTexture);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // The sampler binding is per-unit context state like the texture binding, so it has
    // to be re-asserted here too. Drop to the texture object's own state and let the next
    // bind re-select; mCurrSampler is cleared so that re-selection isn't cached away.
    if (glBindSampler)
    {
        glBindSampler(mIndex, 0);
    }
    mCurrSampler = 0;
}

void LLTexUnit::bindSampler(U32 sampler)
{
    if (mIndex < 0 || mCurrSampler == sampler)
    {
        return;
    }

    // Sampler state is consulted at sample time, not at bind time, so a change has to be
    // ordered against draws the same way a texture change is.
    gGL.flush();

    glBindSampler(mIndex, sampler);
    mCurrSampler = sampler;
}

void LLTexUnit::setSamplerAddressMode(eTextureAddressMode mode)
{
    if (mIndex < 0 || mCurrAddress == mode)
    {
        return;
    }

    mCurrAddress = mode;
    bindSampler(gGL.getSampler(mCurrFilter, mCurrAddress, mHasMipMaps));
}

void LLTexUnit::setSamplerFilteringOption(eTextureFilterOptions option)
{
    if (mIndex < 0 || mCurrFilter == option)
    {
        return;
    }

    mCurrFilter = option;
    bindSampler(gGL.getSampler(mCurrFilter, mCurrAddress, mHasMipMaps));
}

void LLTexUnit::activate(void)
{
    if (mIndex < 0) return;

    if ((S32)gGL.mCurrTextureUnitIndex != mIndex || gGL.mDirty)
    {
        gGL.flush();
        glActiveTexture(GL_TEXTURE0 + mIndex);
        gGL.mCurrTextureUnitIndex = mIndex;
    }
}

void LLTexUnit::enable(eTextureType type)
{
    if (mIndex < 0) return;

    if ( (mCurrTexType != type || gGL.mDirty) && (type != TT_NONE) )
    {
        activate();
        if (mCurrTexType != TT_NONE && !gGL.mDirty)
        {
            disable(); // Force a disable of a previous texture type if it's enabled.
        }
        mCurrTexType = type;

        gGL.flush();
    }
}

void LLTexUnit::disable(void)
{
    if (mIndex < 0) return;

    if (mCurrTexType != TT_NONE)
    {
        unbind(mCurrTexType);
        mCurrTexType = TT_NONE;
    }
}

void LLTexUnit::bindFast(LLTexture* texture)
{
    LLImageGL* gl_tex = texture->getGLTexture();
    texture->setActive();
    glActiveTexture(GL_TEXTURE0 + mIndex);
    gGL.mCurrTextureUnitIndex = mIndex;
    mCurrTexture = gl_tex->getTexName();
    if (!mCurrTexture)
    {
        LL_PROFILE_ZONE_NAMED("MISSING TEXTURE");
        //if deleted, will re-generate it immediately
        texture->forceImmediateUpdate();
        gl_tex->forceUpdateBindStats();
        texture->bindDefaultImage(mIndex);
    }
    // Classic bind, NOT glBindTextureUnit, even where DSA is available.
    //
    // The DSA form issues one GL call here instead of two, but it deliberately leaves the
    // active unit alone -- and this function used to park gGL.mCurrTextureUnitIndex at its
    // own index on every call, which let later activate() calls on the same unit skip their
    // glActiveTexture. Not doing that hands the saved call back at the next activate(), so
    // the win is smaller than it looks and can invert depending on the bind/activate mix.
    //
    // (It also differs on a stale name: glBindTexture silently conjures a fresh empty
    // texture object, glBindTextureUnit raises GL_INVALID_OPERATION.)
    glBindTexture(sGLTextureType[gl_tex->getTarget()], mCurrTexture);

    // Select this texture's sampler. No flush, per this function's contract -- consistent
    // with the texture binding above, which is changed the same way.
    mCurrFilter  = gl_tex->getFilteringOption();
    mCurrAddress = gl_tex->getAddressMode();
    const U32 sampler = gl_tex->getSampler();
    if (mCurrSampler != sampler)
    {
        glBindSampler(mIndex, sampler);
        mCurrSampler = sampler;
    }

    // Track the actual bound target so later callers that read mCurrTexType don't act on a
    // stale target left over from earlier passes. The fast path used to skip this -- the
    // slow enable() path was the only place mCurrTexType was updated.
    mCurrTexType = gl_tex->getTarget();
    mHasMipMaps = gl_tex->mHasMipMaps;
}

bool LLTexUnit::bind(LLTexture* texture, bool for_rendering, bool forceBind)
{
    return bindImpl(texture, forceBind, KEEP_TEXTURE_SAMPLING, KEEP_TEXTURE_SAMPLING);
}

bool LLTexUnit::bindSampled(LLTexture* texture, eTextureAddressMode address, bool forceBind)
{
    return bindImpl(texture, forceBind, KEEP_TEXTURE_SAMPLING, address);
}

bool LLTexUnit::bindSampled(LLTexture* texture, eTextureFilterOptions filter,
                            eTextureAddressMode address, bool forceBind)
{
    return bindImpl(texture, forceBind, filter, address);
}

// filter_override / address_override are KEEP_TEXTURE_SAMPLING to take that half from the
// texture's own mode. Resolving both here means the sampler is selected ONCE: the
// bind-then-setSampler* sequence this replaces could issue two glBindSampler calls and, worse,
// a gGL.flush() between them, splitting the draw batch for a sampler that was about to change
// again.
bool LLTexUnit::bindImpl(LLTexture* texture, bool forceBind, S32 filter_override, S32 address_override)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    stop_glerror();
    if (mIndex >= 0)
    {
        gGL.flush();

        LLImageGL* gl_tex = NULL ;

        if (texture != NULL && (gl_tex = texture->getGLTexture()))
        {
            if (gl_tex->getTexName()) //if texture exists
            {
                //in audit, replace the selected texture by the default one.
                if ((mCurrTexture != gl_tex->getTexName()) || forceBind)
                {
                    activate();
                    enable(gl_tex->getTarget());
                    mCurrTexture = gl_tex->getTexName();
                    glBindTexture(sGLTextureType[gl_tex->getTarget()], mCurrTexture);
                    if(gl_tex->updateBindStats())
                    {
                        texture->setActive();
                    }
                    mHasMipMaps = gl_tex->mHasMipMaps;
                }
                // Outside the redundancy check on purpose: the same texture can be bound
                // again after a pass that left a different sampler on this unit.
                // bindSampler() no-ops when nothing changes.
                mCurrFilter  = (filter_override  == KEEP_TEXTURE_SAMPLING)
                                   ? gl_tex->getFilteringOption()
                                   : (eTextureFilterOptions)filter_override;
                mCurrAddress = (address_override == KEEP_TEXTURE_SAMPLING)
                                   ? gl_tex->getAddressMode()
                                   : (eTextureAddressMode)address_override;

                // The un-overridden case is the hot one (every plain bind), and the texture
                // memoises its own sampler -- so resolve through it rather than re-deriving.
                const bool overridden = (filter_override  != KEEP_TEXTURE_SAMPLING)
                                     || (address_override != KEEP_TEXTURE_SAMPLING);
                bindSampler(overridden
                                ? gGL.getSampler(mCurrFilter, mCurrAddress, gl_tex->mHasMipMaps)
                                : gl_tex->getSampler());
            }
            else
            {
                //if deleted, will re-generate it immediately
                texture->forceImmediateUpdate() ;

                gl_tex->forceUpdateBindStats() ;
                return texture->bindDefaultImage(mIndex);
            }
        }
        else
        {
            if (texture)
            {
                LL_DEBUGS() << "NULL LLTexUnit::bind GL image" << LL_ENDL;
            }
            else
            {
                LL_DEBUGS() << "NULL LLTexUnit::bind texture" << LL_ENDL;
            }
            return false;
        }
    }
    else
    { // mIndex < 0
        return false;
    }

    return true;
}

bool LLTexUnit::bind(LLImageGL* texture, bool for_rendering, bool forceBind, S32 usename)
{
    stop_glerror();
    if (mIndex < 0) return false;

    if(!texture)
    {
        LL_DEBUGS() << "NULL LLTexUnit::bind texture" << LL_ENDL;
        return false;
    }

    // Resolve texname after the null-check; previously this line dereferenced
    // `texture` when usename == 0 and texture was null, crashing before the
    // diagnostic could fire.
    U32 texname = usename ? usename : texture->getTexName();

    if(!texname)
    {
        if(LLImageGL::sDefaultGLTexture && LLImageGL::sDefaultGLTexture->getTexName())
        {
            return bind(LLImageGL::sDefaultGLTexture) ;
        }
        stop_glerror();
        return false ;
    }

    if ((mCurrTexture != texname) || forceBind)
    {
        gGL.flush();
        stop_glerror();
        activate();
        stop_glerror();
        enable(texture->getTarget());
        stop_glerror();
        mCurrTexture = texname;
        glBindTexture(sGLTextureType[texture->getTarget()], mCurrTexture);
        stop_glerror();
        texture->updateBindStats();
        mHasMipMaps = texture->mHasMipMaps;
    }

    // See bind(LLTexture*): selected outside the redundancy check so a sampler left by an
    // earlier pass can't survive on this unit.
    mCurrFilter  = texture->getFilteringOption();
    mCurrAddress = texture->getAddressMode();
    bindSampler(texture->getSampler());

    stop_glerror();

    return true;
}

bool LLTexUnit::bind(LLCubeMap* cubeMap)
{
    if (mIndex < 0) return false;

    gGL.flush();

    if (cubeMap == NULL)
    {
        LL_WARNS() << "NULL LLTexUnit::bind cubemap" << LL_ENDL;
        return false;
    }

    // mImages[0] is normally populated by LLCubeMap::initGL, but a
    // partially-constructed cubemap could have a null face here.
    if (cubeMap->mImages[0].isNull())
    {
        LL_WARNS() << "LLTexUnit::bind cubemap with null face 0" << LL_ENDL;
        return false;
    }

    if (mCurrTexture != cubeMap->mImages[0]->getTexName())
    {
        if (LLCubeMap::sUseCubeMaps)
        {
            activate();
            enable(LLTexUnit::TT_CUBE_MAP);
            mCurrTexture = cubeMap->mImages[0]->getTexName();
            glBindTexture(GL_TEXTURE_CUBE_MAP, mCurrTexture);
            mHasMipMaps = cubeMap->mImages[0]->mHasMipMaps;
            cubeMap->mImages[0]->updateBindStats();
            mCurrFilter  = cubeMap->mImages[0]->getFilteringOption();
            mCurrAddress = cubeMap->mImages[0]->getAddressMode();
            bindSampler(cubeMap->mImages[0]->getSampler());
            return true;
        }
        else
        {
            LL_WARNS() << "Using cube map without extension!" << LL_ENDL;
            return false;
        }
    }
    return true;
}

// LLRenderTarget is unavailible on the mapserver since it uses FBOs.
bool LLTexUnit::bind(LLRenderTarget* renderTarget, bool bindDepth, U32 sampler)
{
    if (mIndex < 0) return false;

    gGL.flush();

    if (bindDepth)
    {
        llassert(renderTarget->getDepth()); // target MUST have a depth buffer attachment

        bindManual(renderTarget->getUsage(), renderTarget->getDepth(), false, sampler);
    }
    else
    {
        bindManual(renderTarget->getUsage(), renderTarget->getTexture(), false, sampler);
    }

    return true;
}

bool LLTexUnit::bindManual(eTextureType type, U32 texture, bool hasMips, U32 sampler)
{
    if (mIndex < 0)
    {
        return false;
    }

    if(mCurrTexture != texture)
    {
        gGL.flush();

        activate();
        enable(type);
        mCurrTexture = texture;
        // Classic bind rather than glBindTextureUnit even under DSA: several callers pass
        // a name straight out of glGenTextures and rely on this call to establish its
        // target (LLRenderTarget::allocateColorTexture, LLCubeMap::initGL, the pipeline
        // noise/SMAA LUTs). glBindTextureUnit rejects a target-less name.
        glBindTexture(sGLTextureType[type], texture);
        mHasMipMaps = hasMips;
    }

    // Outside the redundancy check: re-binding the same texture with a different sampler
    // is exactly what the post-process chain does, so this must not be skipped when the
    // texture happens to match. Multisample textures cannot be sampled through a sampler
    // object at all -- that is a draw-time error, not a silent fallback.
    bindSampler(type == TT_MULTISAMPLE_TEXTURE ? 0 : sampler);

    return true;
}

void LLTexUnit::unbind(eTextureType type)
{
    stop_glerror();

    if (mIndex < 0) return;

    //always flush and activate for consistency
    //   some code paths assume unbind always flushes and sets the active texture
    gGL.flush();
    activate();

    // Disabled caching of binding state.
    if (mCurrTexType == type)
    {
        mCurrTexture = 0;

        if (type == LLTexUnit::TT_TEXTURE)
        {
            glBindTexture(sGLTextureType[type], sWhiteTexture);
        }
        else
        {
            glBindTexture(sGLTextureType[type], 0);
        }

        // The replacement binding (white texture or nothing) has no relationship to the
        // sampler the outgoing texture wanted, so release it here rather than leaving it
        // for whoever binds next to notice.
        bindSampler(0);
        stop_glerror();
    }
}

void LLTexUnit::unbindFast(eTextureType type)
{
    activate();

    // Disabled caching of binding state.
    if (mCurrTexType == type)
    {
        mCurrTexture = 0;

        if (type == LLTexUnit::TT_TEXTURE)
        {
            glBindTexture(sGLTextureType[type], sWhiteTexture);
        }
        else
        {
            glBindTexture(sGLTextureType[type], 0);
        }

        // As unbind(), but without the flush this function exists to avoid.
        if (mCurrSampler)
        {
            glBindSampler(mIndex, 0);
            mCurrSampler = 0;
        }
    }
}

// Writes GL_TEXTURE_WRAP_* onto the TEXTURE OBJECT bound here, which only has an effect
// while this unit samples through sampler 0 -- a bound sampler object overrides wrap state
// wholesale. Textures that carry their own sampler (anything LLImageGL-backed) must go
// through LLImageGL::setAddressMode instead; this is for render targets and raw GL names.
void LLTexUnit::setTextureAddressMode(eTextureAddressMode mode)
{
    if (mIndex < 0 || mCurrTexture == 0) return;

    warnIfSamplerBound("setTextureAddressMode");

    gGL.flush();

    activate();

    setTextureAddressModeFast(mode, mCurrTexType);
}

void LLTexUnit::setTextureAddressModeFast(eTextureAddressMode mode, eTextureType tex_type)
{
    glTexParameteri(sGLTextureType[tex_type], GL_TEXTURE_WRAP_S, sGLAddressMode[mode]);
    glTexParameteri(sGLTextureType[tex_type], GL_TEXTURE_WRAP_T, sGLAddressMode[mode]);
    if (tex_type == TT_CUBE_MAP || tex_type == TT_CUBE_MAP_ARRAY || tex_type == TT_TEXTURE_3D)
    {
        glTexParameteri(sGLTextureType[tex_type], GL_TEXTURE_WRAP_R, sGLAddressMode[mode]);
    }
}

// See setTextureAddressMode: texture-object state, meaningful only under sampler 0.
void LLTexUnit::setTextureFilteringOption(LLTexUnit::eTextureFilterOptions option)
{
    if (mIndex < 0 || mCurrTexture == 0 || mCurrTexType == LLTexUnit::TT_MULTISAMPLE_TEXTURE) return;

    warnIfSamplerBound("setTextureFilteringOption");

    gGL.flush();

    setTextureFilteringOptionFast(option, mCurrTexType);
}

// A sampler object overrides the texture object's filter/wrap/compare state entirely, so
// writing that state while one is bound changes nothing and reports no error. That silence
// is the trap: it already cost the RLV sphere effect its point filter (402770506d). Anything
// landing here with a sampler bound wants the sampler path instead.
void LLTexUnit::warnIfSamplerBound(const char* who) const
{
    if (mCurrSampler != 0)
    {
        LL_WARNS_ONCE("RenderState") << who << " on texture unit " << mIndex
                                     << " while sampler " << mCurrSampler
                                     << " is bound -- the write is a no-op. Set the sampling"
                                        " mode on the texture (LLImageGL::setFilteringOption /"
                                        " setAddressMode) or pass it to the bind call."
                                     << LL_ENDL;
    }
}

void LLTexUnit::setTextureFilteringOptionFast(LLTexUnit::eTextureFilterOptions option, eTextureType tex_type)
{
    if (option == TFO_POINT)
    {
        glTexParameteri(sGLTextureType[tex_type], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    else
    {
        glTexParameteri(sGLTextureType[tex_type], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    if (option >= TFO_TRILINEAR && mHasMipMaps)
    {
        glTexParameteri(sGLTextureType[tex_type], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    else if (option >= TFO_BILINEAR)
    {
        if (mHasMipMaps)
        {
            glTexParameteri(sGLTextureType[tex_type], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
        }
        else
        {
            glTexParameteri(sGLTextureType[tex_type], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
    }
    else
    {
        if (mHasMipMaps)
        {
            glTexParameteri(sGLTextureType[tex_type], GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        }
        else
        {
            glTexParameteri(sGLTextureType[tex_type], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        }
    }

    if (gGLManager.mHasAnisotropic)
    {
        if (option == TFO_ANISOTROPIC && LLRender::sAnisotropicFilteringLevel > 1.f)
        {
            F32 aniso_level = llclamp(LLRender::sAnisotropicFilteringLevel, 1.f, gGLManager.mMaxAnisotropy);
            glTexParameterf(sGLTextureType[tex_type], GL_TEXTURE_MAX_ANISOTROPY, aniso_level);

        }
        else
        {
            glTexParameterf(sGLTextureType[tex_type], GL_TEXTURE_MAX_ANISOTROPY, 1.f);
        }
    }
}

// Useful for debugging that you've manually assigned a texture operation to the correct
// texture unit based on the currently set active texture in opengl.
void LLTexUnit::debugTextureUnit(void)
{
    if (mIndex < 0) return;

    GLint activeTexture;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    if ((GL_TEXTURE0 + mIndex) != activeTexture)
    {
        U32 set_unit = (activeTexture - GL_TEXTURE0);
        LL_WARNS() << "Incorrect Texture Unit!  Expected: " << set_unit << " Actual: " << mIndex << LL_ENDL;
    }
}
