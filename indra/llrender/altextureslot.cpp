/**
 * @file altextureslot.cpp
 * @brief ALTextureSlot implementation
 *
 * Split out of llrender.cpp; see altextureslot.h for what this models.
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

#include "linden_common.h"

#include "altextureslot.h"

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

U32 ALTextureSlot::sSamplerBinds  = 0;
U32 ALTextureSlot::sSamplerSkips  = 0;
U32 ALTextureSlot::sTextureBinds  = 0;
U32 ALTextureSlot::sSamplerBindsFlushed = 0;
U32 ALTextureSlot::sSamplerBindsSplitBatch = 0;
U32 ALTextureSlot::sSamplerBindsShadowCycle = 0;

ALTextureSlot::ALTextureSlot(S32 index)
    : mCurrTexType(TT_NONE),
    mCurrTexture(0),
    mIndex(index)
{
    llassert_always(index < (S32)AL_NUM_TEXTURE_SLOTS);
}

//static
U32 ALTextureSlot::getInternalType(eTextureType type)
{
    return sGLTextureType[type];
}

void ALTextureSlot::refreshState(void)
{
    // We set dirty to true so that the slot knows to ignore caching
    // and we reset the cached slot state

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

    // The sampler binding is per-slot context state like the texture binding, so it has
    // to be re-asserted here too. Drop to the texture object's own state and let the next
    // bind re-select; mCurrSampler is cleared so that re-selection isn't cached away.
    if (glBindSampler)
    {
        glBindSampler(mIndex, 0);
    }
    mCurrSampler = 0;
}

void ALTextureSlot::bindSampler(U32 sampler)
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

    // Zone only on the ISSUED path (a couple thousand per frame, not tens of thousands of
    // skips), so a Tracy capture attributes the standing sampler-bind total by enclosing
    // zone -- which pass, which pool, which bind chain -- without per-suspect counters.
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_ZONE_NUM(mIndex);

    ++sSamplerBinds;
    ++sSamplerBindsFlushed;
    if (gGL.mCount > 0)
    {
        // The flush below will emit a draw: this sampler change genuinely split a batch.
        ++sSamplerBindsSplitBatch;
    }

    // Sampler state is consulted at sample time, not at bind time, so a change has to be
    // ordered against draws the same way a texture change is.
    gGL.flush();

    glBindSampler(mIndex, sampler);
    mCurrSampler = sampler;
}

void ALTextureSlot::activate(void)
{
    if (mIndex < 0) return;

    if ((S32)gGL.mCurrTextureUnitIndex != mIndex || gGL.mDirty)
    {
        gGL.flush();
        glActiveTexture(GL_TEXTURE0 + mIndex);
        gGL.mCurrTextureUnitIndex = mIndex;
    }
}

void ALTextureSlot::setTarget(eTextureType type)
{
    if (mIndex < 0) return;

    if ( (mCurrTexType != type || gGL.mDirty) && (type != TT_NONE) )
    {
        activate();
        if (mCurrTexType != TT_NONE && !gGL.mDirty)
        {
            unbind(); // Release a resource of a different target first.
        }
        mCurrTexType = type;

        gGL.flush();
    }
}

// Sampler named by the CALLER: the bind site says how it wants to read, and the texture
// supplies only its data.
void ALTextureSlot::bindFast(LLTexture* texture, ALSampler key)
{
    // getGLTexture() is virtual and this is the per-draw path, so it is resolved once and the
    // sampler is chosen from the result rather than re-fetching.
    LLImageGL* gl_tex = texture->getGLTexture();
    bindFastImpl(texture, gl_tex, gGL.getSampler(key));
}

void ALTextureSlot::bindFastImpl(LLTexture* texture, LLImageGL* gl_tex, U32 sampler)
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
    // own index on every call, which let later activate() calls on the same slot skip their
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
    // slow setTarget() path was the only place mCurrTexType was updated.
    mCurrTexType = gl_tex->getTarget();
}

// The sampler is named by the caller and the texture's own mode is not consulted at all --
// it has none to consult.
bool ALTextureSlot::bindSampled(LLTexture* texture, ALSampler key, bool forceBind)
{
    return bindImpl(texture, forceBind, key);
}

// Same for a raw LLImageGL -- the font atlas is one, since glyph pages are not LLTextures.
bool ALTextureSlot::bindSampled(LLImageGL* texture, ALSampler key, bool forceBind)
{
    if (!bind(texture, false, forceBind))
    {
        return false;
    }
    bindSampler(gGL.getSampler(key));
    return true;
}

// The sampler is selected ONCE here, from the caller's mask. The bind-then-setSampler*
// sequence this replaces could issue two glBindSampler calls and, worse, a gGL.flush()
// between them, splitting the draw batch for a sampler about to change again.
bool ALTextureSlot::bindImpl(LLTexture* texture, bool forceBind, ALSampler key)
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
                    setTarget(gl_tex->getTarget());
                    mCurrTexture = gl_tex->getTexName();
                    glBindTexture(sGLTextureType[gl_tex->getTarget()], mCurrTexture);
                    if(gl_tex->updateBindStats())
                    {
                        texture->setActive();
                    }
                }
                // Outside the redundancy check on purpose: the same texture can be bound
                // again after a pass that left a different sampler on this slot.
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
                LL_DEBUGS() << "NULL ALTextureSlot::bind GL image" << LL_ENDL;
            }
            else
            {
                LL_DEBUGS() << "NULL ALTextureSlot::bind texture" << LL_ENDL;
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

bool ALTextureSlot::bind(LLImageGL* texture, bool for_rendering, bool forceBind, S32 usename)
{
    stop_glerror();
    if (mIndex < 0) return false;

    if(!texture)
    {
        LL_DEBUGS() << "NULL ALTextureSlot::bind texture" << LL_ENDL;
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
        setTarget(texture->getTarget());
        stop_glerror();
        mCurrTexture = texname;
        glBindTexture(sGLTextureType[texture->getTarget()], mCurrTexture);
        stop_glerror();
        texture->updateBindStats();
    }

    // The sampler is deliberately LEFT ALONE.
    //
    // This overload is bind-to-edit -- allocate, upload, generate mips -- and self-binds.
    // Nothing samples through it, so whatever sampler the slot happens to be holding is
    // irrelevant, and dropping it to 0 would cost a glBindSampler now plus another when the
    // next draw restores one. bindSampler() also flushes, so that pair would break the batch
    // every time a texture is uploaded mid-frame.
    //
    // Safe because no bind can sample without naming a sampler, and this path binds nothing
    // sampleable in the first place. See unbind() for the case where that is not enough.
    stop_glerror();

    return true;
}

bool ALTextureSlot::bind(LLCubeMap* cubeMap, ALSampler key)
{
    if (mIndex < 0) return false;

    gGL.flush();

    if (cubeMap == NULL)
    {
        LL_WARNS() << "NULL ALTextureSlot::bind cubemap" << LL_ENDL;
        return false;
    }

    // mImages[0] is normally populated by LLCubeMap::initGL, but a
    // partially-constructed cubemap could have a null face here.
    if (cubeMap->mImages[0].isNull())
    {
        LL_WARNS() << "ALTextureSlot::bind cubemap with null face 0" << LL_ENDL;
        return false;
    }

    if (mCurrTexture != cubeMap->mImages[0]->getTexName())
    {
        if (LLCubeMap::sUseCubeMaps)
        {
            activate();
            setTarget(ALTextureSlot::TT_CUBE_MAP);
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
bool ALTextureSlot::bind(LLRenderTarget* renderTarget, bool bindDepth, U32 sampler)
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

bool ALTextureSlot::bindManual(eTextureType type, U32 texture, U32 sampler)
{
    if (mIndex < 0)
    {
        return false;
    }

    if(mCurrTexture != texture)
    {
        gGL.flush();

        activate();
        setTarget(type);
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

void ALTextureSlot::unbind()
{
    stop_glerror();

    if (mIndex < 0) return;

    //always flush and activate for consistency
    //   some code paths assume unbind always flushes and sets the active texture
    gGL.flush();
    activate();

    if (mCurrTexType != TT_NONE)
    {
        mCurrTexture = 0;

        if (mCurrTexType == TT_TEXTURE)
        {
            glBindTexture(sGLTextureType[mCurrTexType], sWhiteTexture);
        }
        else
        {
            glBindTexture(sGLTextureType[mCurrTexType], 0);
        }

        // TT_NONE even where the white placeholder is now bound. It means "this slot holds
        // nothing a caller should reason about", which is true either way, and it is what
        // makes LLGLSLShader::disableTexture's target check meaningful: a slot that was
        // enabled but never bound reads TT_NONE and is skipped, so the check only ever
        // compares a target something actually put there.
        mCurrTexType = TT_NONE;

        // Sampler left in place. Whoever binds here next names their own, so releasing it
        // would just be a glBindSampler now and another one then -- with a flush attached.
        //
        // ONE EXCEPTION, and it is not theoretical -- it shipped as a driver warning before
        // being caught. This leaves the WHITE PLACEHOLDER on the slot, which is sampleable,
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

void ALTextureSlot::unbindFast()
{
    activate();

    if (mCurrTexType != TT_NONE)
    {
        mCurrTexture = 0;

        if (mCurrTexType == TT_TEXTURE)
        {
            glBindTexture(sGLTextureType[mCurrTexType], sWhiteTexture);
        }
        else
        {
            glBindTexture(sGLTextureType[mCurrTexType], 0);
        }

        mCurrTexType = TT_NONE;

        // Sampler left in place -- see unbind(), including the compare-sampler exception. This
        // is the per-draw unbind, so the pair of glBindSampler calls it used to cost (release
        // here, restore at the next bind) landed squarely in the hot loop.
    }
}


// Useful for debugging that you've manually assigned a texture operation to the correct
// slot based on the currently set active texture in opengl.
void ALTextureSlot::debugTextureUnit(void)
{
    if (mIndex < 0) return;

    GLint activeTexture;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    if ((GL_TEXTURE0 + mIndex) != activeTexture)
    {
        U32 set_unit = (activeTexture - GL_TEXTURE0);
        LL_WARNS() << "Incorrect texture slot!  Expected: " << set_unit << " Actual: " << mIndex << LL_ENDL;
    }
}
