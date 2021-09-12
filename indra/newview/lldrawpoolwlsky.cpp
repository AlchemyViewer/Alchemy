/** 
 * @file lldrawpoolwlsky.cpp
 * @brief LLDrawPoolWLSky class implementation
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include "lldrawpoolwlsky.h"

#include "llerror.h"
#include "llface.h"
#include "llimage.h"
#include "llrender.h"
#include "llenvironment.h" 
#include "llglslshader.h"
#include "llgl.h"

#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llviewercamera.h"
#include "pipeline.h"
#include "llsky.h"
#include "llvowlsky.h"
#include "llsettingsvo.h"

static LLStaticHashedString sCamPosLocal("camPosLocal");
static LLStaticHashedString sCustomAlpha("custom_alpha");

static LLGLSLShader* cloud_shader = NULL;
static LLGLSLShader* sky_shader   = NULL;
static LLGLSLShader* sun_shader   = NULL;
static LLGLSLShader* moon_shader  = NULL;
static LLGLSLShader* star_shader = NULL;

LLDrawPoolWLSky::LLDrawPoolWLSky(void) :
	LLDrawPool(POOL_WL_SKY)
{
}

LLDrawPoolWLSky::~LLDrawPoolWLSky()
{
}

LLViewerTexture *LLDrawPoolWLSky::getDebugTexture()
{
	return NULL;
}

void LLDrawPoolWLSky::beginRenderPass( S32 pass )
{
	sky_shader =
		LLPipeline::sUnderWaterRender ?
			&gObjectFullbrightNoColorWaterProgram :
			&gWLSkyProgram;

	cloud_shader =
			LLPipeline::sUnderWaterRender ?
				&gObjectFullbrightNoColorWaterProgram :
				&gWLCloudProgram;

    sun_shader =
			LLPipeline::sUnderWaterRender ?
				&gObjectFullbrightNoColorWaterProgram :
				&gWLSunProgram;

    moon_shader =
			LLPipeline::sUnderWaterRender ?
				&gObjectFullbrightNoColorWaterProgram :
				&gWLMoonProgram;

    star_shader =
			LLPipeline::sUnderWaterRender ?
				&gObjectFullbrightNoColorWaterProgram :
				&gCustomAlphaProgram;

    auto& environment = LLEnvironment::instance();
    mCamHeightLocal = environment.getCamHeight();
    mCameraOrigin = LLViewerCamera::getInstance()->getOrigin();
    mCurrentSky = environment.getCurrentSky();
}

void LLDrawPoolWLSky::endRenderPass( S32 pass )
{
    sky_shader   = nullptr;
    cloud_shader = nullptr;
    sun_shader   = nullptr;
    moon_shader  = nullptr;
    star_shader  = nullptr;
    mCurrentSky = nullptr;
}

void LLDrawPoolWLSky::beginDeferredPass(S32 pass)
{
	sky_shader = &gDeferredWLSkyProgram;
	cloud_shader = &gDeferredWLCloudProgram;

    sun_shader =
			LLPipeline::sUnderWaterRender ?
				&gObjectFullbrightNoColorWaterProgram :
				&gDeferredWLSunProgram;

    moon_shader =
			LLPipeline::sUnderWaterRender ?
				&gObjectFullbrightNoColorWaterProgram :
				&gDeferredWLMoonProgram;

    star_shader =
			LLPipeline::sUnderWaterRender ?
				&gObjectFullbrightNoColorWaterProgram :
				&gDeferredStarProgram;

    auto& environment = LLEnvironment::instance();
    mCamHeightLocal = environment.getCamHeight();
    mCameraOrigin = LLViewerCamera::getInstance()->getOrigin();
    mCurrentSky = environment.getCurrentSky();
}

void LLDrawPoolWLSky::endDeferredPass(S32 pass)
{
    sky_shader   = nullptr;
    cloud_shader = nullptr;
    sun_shader   = nullptr;
    moon_shader  = nullptr;
    star_shader  = nullptr;
    mCurrentSky = nullptr;
}

void LLDrawPoolWLSky::renderDome(LLGLSLShader * shader) const
{
    llassert_always(NULL != shader);

    gGL.matrixMode(LLRender::MM_MODELVIEW);
	gGL.pushMatrix();

	//chop off translation
	if (LLPipeline::sReflectionRender && mCameraOrigin.mV[2] > 256.f)
	{
		gGL.translatef(mCameraOrigin.mV[0], mCameraOrigin.mV[1], 256.f- mCameraOrigin.mV[2]*0.5f);
	}
	else
	{
		gGL.translatef(mCameraOrigin.mV[0], mCameraOrigin.mV[1], mCameraOrigin.mV[2]);
	}
		

	// the windlight sky dome works most conveniently in a coordinate system
	// where Y is up, so permute our basis vectors accordingly.
	gGL.rotatef(120.f, 1.f / F_SQRT3, 1.f / F_SQRT3, 1.f / F_SQRT3);

	gGL.scalef(0.333f, 0.333f, 0.333f);

	gGL.translatef(0.f,-mCamHeightLocal, 0.f);
	
	// Draw WL Sky
	shader->uniform3f(sCamPosLocal, 0.f, mCamHeightLocal, 0.f);

    gSky.mVOWLSkyp->drawDome();

    gGL.matrixMode(LLRender::MM_MODELVIEW);
	gGL.popMatrix();
}

void LLDrawPoolWLSky::renderSkyHaze() const
{
	if (gPipeline.canUseWindLightShaders() && gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_SKY))
	{
        LLGLSPipelineDepthTestSkyBox sky(true, LLPipeline::sRenderDeferred ? true : false);

        sky_shader->bind();

        if (LLPipeline::sRenderDeferred)
        {
            LLViewerTexture* rainbow_tex = gSky.mVOSkyp->getRainbowTex();
            LLViewerTexture* halo_tex = gSky.mVOSkyp->getHaloTex();

            sky_shader->bindTexture(LLShaderMgr::RAINBOW_MAP, rainbow_tex);
            sky_shader->bindTexture(LLShaderMgr::HALO_MAP, halo_tex);

            ((LLSettingsVOSky*)mCurrentSky.get())->updateShader(sky_shader);

            F32 moisture_level = (float)mCurrentSky->getSkyMoistureLevel();
            F32 droplet_radius = (float)mCurrentSky->getSkyDropletRadius();
            F32 ice_level = (float)mCurrentSky->getSkyIceLevel();

            // hobble halos and rainbows when there's no light source to generate them
            if (!mCurrentSky->getIsSunUp() && !mCurrentSky->getIsMoonUp())
            {
                moisture_level = 0.0f;
                ice_level = 0.0f;
            }

            sky_shader->uniform1f(LLShaderMgr::MOISTURE_LEVEL, moisture_level);
            sky_shader->uniform1f(LLShaderMgr::DROPLET_RADIUS, droplet_radius);
            sky_shader->uniform1f(LLShaderMgr::ICE_LEVEL, ice_level);
        }

        sky_shader->uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, mCurrentSky->getSunMoonGlowFactor());

        sky_shader->uniform1i(LLShaderMgr::SUN_UP_FACTOR, mCurrentSky->getIsSunUp() ? 1 : 0);

        /// Render the skydome
        renderDome(sky_shader);

		sky_shader->unbind();
    }
}

void LLDrawPoolWLSky::renderStars() const
{
    LLGLSPipelineBlendSkyBox gls_skybox(true, false);
	
	// *NOTE: have to have bound the cloud noise texture already since register
	// combiners blending below requires something to be bound
	// and we might as well only bind once.
	gGL.getTexUnit(0)->enable(LLTexUnit::TT_TEXTURE);
	
	// *NOTE: we divide by two here and GL_ALPHA_SCALE by two below to avoid
	// clamping and allow the star_alpha param to brighten the stars.
	LLColor4 star_alpha(LLColor4::black);

    star_alpha.mV[3] = mCurrentSky->getStarBrightness() / 512.f;
    
	// If star brightness is not set, exit
	if( star_alpha.mV[3] < 0.001 )
	{
#ifdef SHOW_DEBUG
		LL_DEBUGS("SKY") << "star_brightness below threshold." << LL_ENDL;
#endif
		return;
	}

    LLViewerTexture* tex_a = gSky.mVOSkyp->getBloomTex();
    LLViewerTexture* tex_b = gSky.mVOSkyp->getBloomTexNext();
	
    if (tex_a && (!tex_b || (tex_a == tex_b)))
    {
        // Bind current and next sun textures
		gGL.getTexUnit(0)->bind(tex_a);
    }
    else if (tex_b && !tex_a)
    {
        gGL.getTexUnit(0)->bind(tex_b);
    }
    else if (tex_b != tex_a)
    {
        gGL.getTexUnit(0)->bind(tex_a);
    }

	gGL.pushMatrix();
    gGL.translatef(mCameraOrigin.mV[0], mCameraOrigin.mV[1], mCameraOrigin.mV[2]);
    gGL.rotatef(gFrameTimeSeconds * 0.01f, 0.f, 0.f, 1.f);

	if (LLGLSLShader::sNoFixedFunction)
	{
        star_shader->bind();
        star_shader->uniform1f(sCustomAlpha, star_alpha.mV[3]);
	}
	else
	{
		gGL.getTexUnit(0)->setTextureColorBlend(LLTexUnit::TBO_MULT, LLTexUnit::TBS_TEX_COLOR, LLTexUnit::TBS_VERT_COLOR);
		gGL.getTexUnit(0)->setTextureAlphaBlend(LLTexUnit::TBO_MULT_X2, LLTexUnit::TBS_CONST_ALPHA, LLTexUnit::TBS_TEX_ALPHA);
		glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, star_alpha.mV);
	}

	gSky.mVOWLSkyp->drawStars();

    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

	gGL.popMatrix();

	if (LLGLSLShader::sNoFixedFunction)
	{
        star_shader->unbind();
	}
	else
	{
		// and disable the combiner states
		gGL.getTexUnit(0)->setTextureBlendType(LLTexUnit::TB_MULT);
	}
}

void LLDrawPoolWLSky::renderStarsDeferred() const
{
	LLGLSPipelineBlendSkyBox gls_sky(true, false);

	gGL.setSceneBlendType(LLRender::BT_ADD_WITH_ALPHA);

    F32 star_alpha = mCurrentSky->getStarBrightness() / 500.0f;

	// If start_brightness is not set, exit
	if(star_alpha < 0.001f)
	{
#ifdef SHOW_DEBUG
		LL_DEBUGS("SKY") << "star_brightness below threshold." << LL_ENDL;
#endif
		return;
	}

    LLViewerTexture* tex_a = gSky.mVOSkyp->getBloomTex();
    LLViewerTexture* tex_b = gSky.mVOSkyp->getBloomTexNext();

    F32 blend_factor = mCurrentSky->getBlendFactor();
	
    if (tex_a && (!tex_b || (tex_a == tex_b)))
    {
        // Bind current and next sun textures
		gGL.getTexUnit(0)->bind(tex_a);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
        blend_factor = 0;
    }
    else if (tex_b && !tex_a)
    {
        gGL.getTexUnit(0)->bind(tex_b);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
        blend_factor = 0;
    }
    else if (tex_b != tex_a)
    {
        gGL.getTexUnit(0)->bind(tex_a);
        gGL.getTexUnit(1)->bind(tex_b);
    }

    gGL.pushMatrix();
    gGL.translatef(mCameraOrigin.mV[0], mCameraOrigin.mV[1], mCameraOrigin.mV[2]);

    star_shader->bind();

    star_shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);

    if (LLPipeline::sReflectionRender)
    {
        star_alpha = 1.0f;
    }
    star_shader->uniform1f(sCustomAlpha, star_alpha);

    star_shader->uniform1f(LLShaderMgr::WATER_TIME, (F32)LLFrameTimer::getElapsedSeconds() * 0.5f);

	gSky.mVOWLSkyp->drawStars();

    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

    gGL.popMatrix();

    star_shader->unbind();
}

void LLDrawPoolWLSky::renderSkyClouds() const
{
	if (gPipeline.canUseWindLightShaders() && gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_CLOUDS) && gSky.mVOSkyp->getCloudNoiseTex())
	{
        LLGLSPipelineBlendSkyBox pipeline(true, true);
		
		cloud_shader->bind();

        LLPointer<LLViewerTexture> cloud_noise      = gSky.mVOSkyp->getCloudNoiseTex();
        LLPointer<LLViewerTexture> cloud_noise_next = gSky.mVOSkyp->getCloudNoiseTexNext();

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

        F32 cloud_variance = mCurrentSky ? mCurrentSky->getCloudVariance() : 0.0f;
        F32 blend_factor   = mCurrentSky ? mCurrentSky->getBlendFactor() : 0.0f;

        // if we even have sun disc textures to work with...
        if (cloud_noise || cloud_noise_next)
        {
            if (cloud_noise && (!cloud_noise_next || (cloud_noise == cloud_noise_next)))
            {
                // Bind current and next sun textures
                cloud_shader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise, LLTexUnit::TT_TEXTURE);
                blend_factor = 0;
            }
            else if (cloud_noise_next && !cloud_noise)
            {
                cloud_shader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise_next, LLTexUnit::TT_TEXTURE);
                blend_factor = 0;
            }
            else if (cloud_noise_next != cloud_noise)
            {
                cloud_shader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise, LLTexUnit::TT_TEXTURE);
                cloud_shader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP_NEXT, cloud_noise_next, LLTexUnit::TT_TEXTURE);
            }
        }

        cloud_shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);
        cloud_shader->uniform1f(LLShaderMgr::CLOUD_VARIANCE, cloud_variance);
        cloud_shader->uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, mCurrentSky->getSunMoonGlowFactor());

        ((LLSettingsVOSky*)mCurrentSky.get())->updateShader(cloud_shader);

		/// Render the skydome
        renderDome(cloud_shader);

		cloud_shader->unbind();

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
	}
}

void LLDrawPoolWLSky::renderHeavenlyBodies()
{
	LLGLSPipelineBlendSkyBox gls_skybox(true, false);

	gGL.pushMatrix();
	gGL.translatef(mCameraOrigin.mV[0], mCameraOrigin.mV[1], mCameraOrigin.mV[2]);

	LLFace * face = gSky.mVOSkyp->mFace[LLVOSky::FACE_SUN];

    F32 blend_factor = mCurrentSky->getBlendFactor();
    bool can_use_vertex_shaders = gPipeline.canUseVertexShaders();
    bool can_use_windlight_shaders = gPipeline.canUseWindLightShaders();


	if (gSky.mVOSkyp->getSun().getDraw() && face && face->getGeomCount())
	{
		LLPointer<LLViewerTexture> tex_a = face->getTexture(LLRender::DIFFUSE_MAP);
        LLPointer<LLViewerTexture> tex_b = face->getTexture(LLRender::ALTERNATE_DIFFUSE_MAP);

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

        // if we even have sun disc textures to work with...
        if (tex_a || tex_b)
        {
            // if and only if we have a texture defined, render the sun disc
            if (can_use_vertex_shaders && can_use_windlight_shaders)
            {
                sun_shader->bind();

                if (tex_a && (!tex_b || (tex_a == tex_b)))
                {
                    // Bind current and next sun textures
                    sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                    blend_factor = 0;
                }
                else if (tex_b && !tex_a)
                {
                    sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
                    blend_factor = 0;
                }
                else if (tex_b != tex_a)
                {
                    sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                    sun_shader->bindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
                }

                LLColor4 color(gSky.mVOSkyp->getSun().getInterpColor());

                sun_shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color.mV);
                sun_shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);

                face->renderIndexed();

                gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
                gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

                sun_shader->unbind();
            }
        }
	}

    //blend_factor = LLEnvironment::instance().getCurrentSky()->getBlendFactor();

	face = gSky.mVOSkyp->mFace[LLVOSky::FACE_MOON];

	if (gSky.mVOSkyp->getMoon().getDraw() && face && face->getTexture(LLRender::DIFFUSE_MAP) && face->getGeomCount() && moon_shader)
	{        
        LLViewerTexture* tex_a = face->getTexture(LLRender::DIFFUSE_MAP);
        LLViewerTexture* tex_b = face->getTexture(LLRender::ALTERNATE_DIFFUSE_MAP);

		LLColor4 color(gSky.mVOSkyp->getMoon().getInterpColor());
		
        if (can_use_vertex_shaders && can_use_windlight_shaders && (tex_a || tex_b))
        {
            moon_shader->bind();

            if (tex_a && (!tex_b || (tex_a == tex_b)))
            {
                // Bind current and next sun textures
                moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                //blend_factor = 0;
            }
            else if (tex_b && !tex_a)
            {
                moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
                //blend_factor = 0;
            }
            else if (tex_b != tex_a)
            {
                moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                //moon_shader->bindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
            }

            F32 moon_brightness = (float)mCurrentSky->getMoonBrightness();
            LLColor4 moon_color(gSky.mVOSkyp->getMoon().getColor());
            
            moon_shader->uniform1f(LLShaderMgr::MOON_BRIGHTNESS, moon_brightness);
            moon_shader->uniform4fv(LLShaderMgr::MOONLIGHT_COLOR, 1, moon_color.mV);
            moon_shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color.mV);
            //moon_shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);
            moon_shader->uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, mCurrentSky->getMoonDirection().mV); // shader: moon_dir

            face->renderIndexed();

            gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

            moon_shader->unbind();
        }
    }

    gGL.popMatrix();
}

void LLDrawPoolWLSky::renderDeferred(S32 pass)
{
	if (!gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_SKY))
	{
		return;
	}
	LL_RECORD_BLOCK_TIME(FTM_RENDER_WL_SKY);

	gGL.setColorMask(true, false);

    if (gPipeline.canUseWindLightShaders())
    {
        renderSkyHaze();
        renderStarsDeferred();
        renderHeavenlyBodies();
        renderSkyClouds();
    }
    gGL.setColorMask(true, true);
}

void LLDrawPoolWLSky::render(S32 pass)
{
	if (!gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_SKY))
	{
		return;
	}
	LL_RECORD_BLOCK_TIME(FTM_RENDER_WL_SKY);

    renderSkyHaze();
    renderStars();
    renderHeavenlyBodies();
	renderSkyClouds();

	gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
}

void LLDrawPoolWLSky::prerender()
{
	//LL_INFOS() << "wlsky prerendering pass." << LL_ENDL;
}

LLViewerTexture* LLDrawPoolWLSky::getTexture()
{
	return NULL;
}

void LLDrawPoolWLSky::resetDrawOrders()
{
}

//static
void LLDrawPoolWLSky::cleanupGL()
{
}

//static
void LLDrawPoolWLSky::restoreGL()
{
}
