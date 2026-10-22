/**
 * @file lldynamictexture.cpp
 * @brief Implementation of LLViewerDynamicTexture class
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

#include "llviewerprecompiledheaders.h"

#include "lldynamictexture.h"

// Linden library includes
#include "llglheaders.h"
#include "llwindow.h"           // getPosition()

// Viewer includes
#include "llviewerwindow.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewertexture.h"
#include "llvertexbuffer.h"
#include "llviewerdisplay.h"
#include "llrender.h"
#include "pipeline.h"
#include "llglslshader.h"

#include "alondemandtarget.h"

// static
LLViewerDynamicTexture::instance_list_t LLViewerDynamicTexture::sInstances[ LLViewerDynamicTexture::ORDER_COUNT ];
S32 LLViewerDynamicTexture::sNumRenders = 0;

//-----------------------------------------------------------------------------
// LLViewerDynamicTexture()
//-----------------------------------------------------------------------------
LLViewerDynamicTexture::LLViewerDynamicTexture(S32 width, S32 height, S32 components, EOrder order) :
    LLViewerTexture(width, height, components, false)
{
    llassert((1 <= components) && (components <= 4));

    generateGLTexture();

    llassert( 0 <= order && order < ORDER_COUNT );
    LLViewerDynamicTexture::sInstances[ order ].insert(this);
}

//-----------------------------------------------------------------------------
// LLViewerDynamicTexture()
//-----------------------------------------------------------------------------
LLViewerDynamicTexture::~LLViewerDynamicTexture()
{
    for( S32 order = 0; order < ORDER_COUNT; order++ )
    {
        LLViewerDynamicTexture::sInstances[order].erase(this);  // will fail in all but one case.
    }
}

//virtual
S8 LLViewerDynamicTexture::getType() const
{
    return LLViewerTexture::DYNAMIC_TEXTURE ;
}

//-----------------------------------------------------------------------------
// generateGLTexture()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::generateGLTexture()
{
    LLViewerTexture::generateGLTexture() ;
    generateGLTexture(-1, 0, 0, false);
}

void LLViewerDynamicTexture::generateGLTexture(LLGLint internal_format, LLGLenum primary_format, LLGLenum type_format, bool swap_bytes)
{
    if (mComponents < 1 || mComponents > 4)
    {
        LL_ERRS() << "Bad number of components in dynamic texture: " << mComponents << LL_ENDL;
    }

    LLPointer<LLImageRaw> raw_image = new LLImageRaw(mFullWidth, mFullHeight, mComponents);
    if (internal_format >= 0)
    {
        setExplicitFormat(internal_format, primary_format, type_format, swap_bytes);
    }
    createGLTexture(0, raw_image, 0, true, LLGLTexture::DYNAMIC_TEX);
    mGLTexturep->setGLTextureCreated(false);
}

//-----------------------------------------------------------------------------
// render()
//-----------------------------------------------------------------------------
bool LLViewerDynamicTexture::render()
{
    return false;
}

//-----------------------------------------------------------------------------
// preRender()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::preRender(bool clear_depth)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

     //use the bottom left corner
    mOrigin.set(0, 0);

    gGL.getTextureSlot(0)->unbind();
    // Set up camera
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    mCamera.setOrigin(*camera);
    mCamera.setAxes(*camera);
    mCamera.setAspect(camera->getAspect());
    mCamera.setView(camera->getView());
    mCamera.setNear(camera->getNear());

    gGL.setViewport(mOrigin.mX, mOrigin.mY, mFullWidth, mFullHeight);
    if (clear_depth)
    {
        gGL.clear(GL_DEPTH_BUFFER_BIT);
    }
}

//-----------------------------------------------------------------------------
// postRender()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::postRender(bool success)
{
    {
        if (success)
        {
            if(mGLTexturep.isNull())
            {
                generateGLTexture() ;
            }
            else if(!mGLTexturep->getHasGLTexture())
            {
                generateGLTexture() ;
            }
            else if(mGLTexturep->getDiscardLevel() != 0)//do not know how it happens, but regenerate one if it does.
            {
                generateGLTexture() ;
            }

            success = mGLTexturep->setSubImageFromFrameBuffer(0, 0, mOrigin.mX, mOrigin.mY, mFullWidth, mFullHeight);
        }
    }

    // restore viewport
    gViewerWindow->setup2DViewport();

    // restore camera
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    camera->setOrigin(mCamera);
    camera->setAxes(mCamera);
    camera->setAspect(mCamera.getAspect());
    camera->setViewNoBroadcast(mCamera.getView());
    camera->setNear(mCamera.getNear());
}

//-----------------------------------------------------------------------------
// static
// updateDynamicTextures()
// Calls update on each dynamic texture.  Calls each group in order: "first," then "middle," then "last."
//-----------------------------------------------------------------------------
bool LLViewerDynamicTexture::updateAllInstances()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    sNumRenders = 0;
    if (gGLManager.mIsDisabled)
    {
        return true;
    }

    LLGLSLShader::unbind();
    LLVertexBuffer::unbind();

    bool result = false;
    bool ret = false ;
    auto update_func = [&](LLViewerDynamicTexture* dynamicTexture, LLRenderTarget& renderTarget)
        {
            llassert(dynamicTexture->getFullWidth() <= (S32)renderTarget.getWidth());
            llassert(dynamicTexture->getFullHeight() <= (S32)renderTarget.getHeight());

            gGL.clear(GL_DEPTH_BUFFER_BIT);

            gGL.color4f(1.f, 1.f, 1.f, 1.f);
            dynamicTexture->setBoundTarget(&renderTarget);
            dynamicTexture->preRender();    // Must be called outside of startRender()
            result = false;
            if (dynamicTexture->render())
            {
                ret = true;
                result = true;
                sNumRenders++;
            }
            gGL.flush();
            LLVertexBuffer::unbind();
            dynamicTexture->setBoundTarget(nullptr);
            dynamicTexture->postRender(result);
        };
    const auto needs_render = [](LLViewerDynamicTexture* dynamicTexture) { return dynamicTexture->needsRender(); };

    // Each range of orders draws into a target of its own from the pool, acquired, bound and cleared for the first of
    // its instances that needs a render, and given back once they are drawn: a frame with nothing to draw takes, binds
    // and clears neither. Where one cannot be had, the range's instances wait for a later frame. A preview or a bake
    // can go seconds between draws, as a slider waits on the hand dragging it, and building its target again costs
    // the frame that draws next around half a millisecond of CPU and more of the GPU's: the pool keeps each through
    // 1800 frames unacquired, 30 seconds at 60 fps, and with it whatever a draw takes for itself, as a GLTF material
    // preview takes the probe pack's post target and bloom.
    using Plan = ALRenderResourcePlan;
    constexpr U32 KEEP_FRAMES = 1800;
    const auto update_orders = [&](S32 first, S32 last, Plan::Id id)
        {
            const ALTransientTargetPool::Keep keep(gPipeline.mTransientTargets, KEEP_FRAMES);
            ALOnDemandTarget target(gPipeline.mTransientTargets, gPipeline.resourcePlan().desc(id), Plan::name(id),
                                    KEEP_FRAMES);
            bool drawn = true;
            for (S32 order = first; drawn && order < last; ++order)
            {
                drawn = target.drawEach(LLViewerDynamicTexture::sInstances[order], needs_render, update_func);
            }
            if (!drawn)
            {
                LL_WARNS_ONCE("RenderResources") << "No " << Plan::name(id)
                                                 << " target to draw dynamic textures into; they wait for one" << LL_ENDL;
            }
        };

    // ORDER_FIRST is unused, ORDER_MIDDLE is various ui preview
    update_orders(0, ORDER_LAST, Plan::Id::Preview);

    // ORDER_LAST is baked skin preview, ORDER_RESET resets appearance parameters and does not render.
    result = false;
    ret = false;
    update_orders(ORDER_LAST, ORDER_COUNT, Plan::Id::Bake);

    gGL.flush();

    return ret;
}

//-----------------------------------------------------------------------------
// static
// destroyGL()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::destroyGL()
{
    for( S32 order = 0; order < ORDER_COUNT; order++ )
    {
        for (instance_list_t::iterator iter = LLViewerDynamicTexture::sInstances[order].begin();
             iter != LLViewerDynamicTexture::sInstances[order].end(); ++iter)
        {
            LLViewerDynamicTexture *dynamicTexture = *iter;
            dynamicTexture->destroyGLTexture() ;
        }
    }
}

//-----------------------------------------------------------------------------
// static
// restoreGL()
//-----------------------------------------------------------------------------
void LLViewerDynamicTexture::restoreGL()
{
    if (gGLManager.mIsDisabled)
    {
        return ;
    }

    for( S32 order = 0; order < ORDER_COUNT; order++ )
    {
        for (instance_list_t::iterator iter = LLViewerDynamicTexture::sInstances[order].begin();
             iter != LLViewerDynamicTexture::sInstances[order].end(); ++iter)
        {
            LLViewerDynamicTexture *dynamicTexture = *iter;
            dynamicTexture->restoreGLTexture() ;
        }
    }
}
