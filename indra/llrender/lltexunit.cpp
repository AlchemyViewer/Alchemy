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

U32 LLTexUnit::sSamplerBinds  = 0;
U32 LLTexUnit::sSamplerSkips  = 0;
U32 LLTexUnit::sTextureBinds  = 0;
U32 LLTexUnit::sSamplerBindsFlushed = 0;

LLTexUnit::LLTexUnit(S32 index)
    : mCurrTexType(TT_NONE),
    mCurrTexture(0),
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
    if (mIndex < 0)
    {
        return;
    }

    if (mCurrSampler == sampler)
    {
        ++sSamplerSkips;
        return;
    }

    ++sSamplerBinds;
    ++sSamplerBindsFlushed;

    // Sampler state is consulted at sample time, not at bind time, so a change has to be
    // ordered against draws the same way a texture change is.
    gGL.flush();

    glBindSampler(mIndex, sampler);
    mCurrSampler = sampler;
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

// Sampler named by the CALLER. This is the shape the migration is heading for: the bind site
// says how it wants to read, and the texture supplies only its data (and whether it has a mip
// chain, which GL needs to keep a mipmapped filter from selecting an incomplete texture).
void LLTexUnit::bindFast(LLTexture* texture, ALSampler key)
{
    // getGLTexture() is virtual and this is the per-draw path, so it is resolved once and the
    // sampler is chosen from the result rather than re-fetching. LLTexUnit is a friend of
    // LLImageGL, so the mip truth is read directly.
    LLImageGL* gl_tex = texture->getGLTexture();
    bindFastImpl(texture, gl_tex, gGL.getSampler(key));
}

void LLTexUnit::bindFastImpl(LLTexture* texture, LLImageGL* gl_tex, U32 sampler)
{
    ++sTextureBinds;
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

    // Select the sampler. No flush, per this function's contract -- consistent with the
    // texture binding above, which is changed the same way.
    if (mCurrSampler != sampler)
    {
        ++sSamplerBinds;
        glBindSampler(mIndex, sampler);
        mCurrSampler = sampler;
    }
    else
    {
        ++sSamplerSkips;
    }

    // Track the actual bound target so later callers that read mCurrTexType don't act on a
    // stale target left over from earlier passes. The fast path used to skip this -- the
    // slow enable() path was the only place mCurrTexType was updated.
    mCurrTexType = gl_tex->getTarget();
}

// The sampler is named by the caller and the texture's own mode is not consulted at all --
// it has none to consult.
bool LLTexUnit::bindSampled(LLTexture* texture, ALSampler key, bool forceBind)
{
    return bindImpl(texture, forceBind, key);
}

// Same for a raw LLImageGL -- the font atlas is one, since glyph pages are not LLTextures.
bool LLTexUnit::bindSampled(LLImageGL* texture, ALSampler key, bool forceBind)
{
    if (!bind(texture, false, forceBind))
    {
        return false;
    }
    bindSampler(gGL.getSampler(key));
    return true;
}

// The sampler is selected ONCE here, from the caller's mask plus the resource's mip truth. The
// bind-then-setSampler* sequence this replaces could issue two glBindSampler calls and, worse,
// a gGL.flush() between them, splitting the draw batch for a sampler about to change again.
bool LLTexUnit::bindImpl(LLTexture* texture, bool forceBind, ALSampler key)
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
                            }
                // Outside the redundancy check on purpose: the same texture can be bound
                // again after a pass that left a different sampler on this unit.
                // bindSampler() no-ops when nothing changes.
                bindSampler(gGL.getSampler(key));
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
    }

    // The sampler is deliberately LEFT ALONE.
    //
    // This overload is bind-to-edit -- allocate, upload, generate mips -- and self-binds.
    // Nothing samples through it, so whatever sampler the unit happens to be holding is
    // irrelevant, and dropping it to 0 would cost a glBindSampler now plus another when the
    // next draw restores one. bindSampler() also flushes, so that pair would break the batch
    // every time a texture is uploaded mid-frame.
    //
    // Safe because no bind can sample without naming a sampler, and this path binds nothing
    // sampleable in the first place. See unbind() for the case where that is not enough.
    stop_glerror();

    return true;
}

bool LLTexUnit::bind(LLCubeMap* cubeMap, ALSampler key)
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
            cubeMap->mImages[0]->updateBindStats();
            // Named by the caller, not read off face 0. A cube map is as much a shared
            // resource as any other texture -- the environment map is sampled by every
            // shiny surface in the scene -- so which sampler a pass wants is the pass's
            // business.
            bindSampler(gGL.getSampler(key));
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

        // Sampler 0 means "sample through the texture object's own state", and render target
        // textures no longer carry any -- allocation expresses it as a sampler instead. So an
        // unspecified sampler resolves to the target's default rather than to nothing, which
        // would otherwise leave these binds on GL's defaults (nearest, repeat) and quietly
        // unfilter every pass that reads a target without naming a sampler.
        bindManual(renderTarget->getUsage(), renderTarget->getDepth(),
                   sampler ? sampler : renderTarget->getDefaultDepthSampler());
    }
    else
    {
        bindManual(renderTarget->getUsage(), renderTarget->getTexture(),
                   sampler ? sampler : renderTarget->getDefaultColorSampler(0));
    }

    return true;
}

bool LLTexUnit::bindManual(eTextureType type, U32 texture, U32 sampler)
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

        // Sampler left in place. Whoever binds here next names their own, so releasing it
        // would just be a glBindSampler now and another one then -- with a flush attached.
        //
        // ONE EXCEPTION, and it is not theoretical -- it shipped as a driver warning before
        // being caught. This leaves the WHITE PLACEHOLDER on the unit, which is sampleable,
        // so a leftover COMPARE sampler is read through depth comparison on a non-depth
        // texture: undefined. Whoever selects a compare sampler must therefore release it
        // itself rather than relying on this; LLPipeline::bindShadowMaps and unbindShadowMaps
        // call bindSampler(0) on the channels they give up.
        //
        // It is not enough to track the channels and unbind the TEXTURE. That is what those
        // functions already did, and it is exactly what stopped being sufficient the moment
        // this stopped clearing the sampler as a side effect.
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

        // Sampler left in place -- see unbind(), including the compare-sampler exception. This
        // is the per-draw unbind, so the pair of glBindSampler calls it used to cost (release
        // here, restore at the next bind) landed squarely in the hot loop.
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
