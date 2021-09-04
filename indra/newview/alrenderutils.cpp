/**
* @file alrenderutils.cpp
* @brief Alchemy Render Utility
*
* $LicenseInfo:firstyear=2021&license=viewerlgpl$
* Alchemy Viewer Source Code
* Copyright (C) 2021, Alchemy Viewer Project.
* Copyright (C) 2021, Rye Mutt <rye@alchemyviewer.org>
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
* $/LicenseInfo$
*/

#include "llviewerprecompiledheaders.h"

#include "alrenderutils.h"

#include "llrendertarget.h"
#include "llvertexbuffer.h"

#include "alcontrolcache.h"
#include "llviewercontrol.h"
#include "llviewershadermgr.h"
#include "pipeline.h"

const U32 ALRENDER_BUFFER_MASK = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_TEXCOORD1;

static LLStaticHashedString al_exposure("exposure");
static LLStaticHashedString tone_uchimura_a("tone_uchimura_a");
static LLStaticHashedString tone_uchimura_b("tone_uchimura_b");
static LLStaticHashedString tone_lottes_a("tone_lottes_a");
static LLStaticHashedString tone_lottes_b("tone_lottes_b");
static LLStaticHashedString tone_uncharted_a("tone_uncharted_a");
static LLStaticHashedString tone_uncharted_b("tone_uncharted_b");
static LLStaticHashedString tone_uncharted_c("tone_uncharted_c");

ALRenderUtil::ALRenderUtil()
{
	// Connect settings
	gSavedSettings.getControl("RenderToneMapType")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapExposure")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapLottesA")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapLottesB")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapUchimuraA")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapUchimuraB")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));

	// Shader setup
	refreshState();
}

void ALRenderUtil::restoreVertexBuffers()
{
	mRenderBuffer = new LLVertexBuffer(ALRENDER_BUFFER_MASK, 0);
	mRenderBuffer->allocateBuffer(3, 0, true);

	LLStrider<LLVector3> vert;
	LLStrider<LLVector2> tc0;
	LLStrider<LLVector2> tc1;
	mRenderBuffer->getVertexStrider(vert);
	mRenderBuffer->getTexCoord0Strider(tc0);
	mRenderBuffer->getTexCoord1Strider(tc1);

	vert[0].set(-1.f, -1.f, 0.f);
	vert[1].set(3.f, -1.f, 0.f);
	vert[2].set(-1.f, 3.f, 0.f);

	mRenderBuffer->flush();
}

void ALRenderUtil::resetVertexBuffers()
{
	mRenderBuffer = nullptr;
}

void ALRenderUtil::refreshState()
{
	setupTonemap();
}

bool ALRenderUtil::setupTonemap()
{
	if (LLPipeline::sRenderDeferred)
	{
		mTonemapType = gSavedSettings.getU32("RenderToneMapType");
		if (mTonemapType >= TONEMAP_COUNT)
		{
			mTonemapType = ALTonemap::NONE;
		}

		mTonemapExposure = llclamp(gSavedSettings.getF32("RenderToneMapExposure"), 0.1f, 16.f);

		mToneLottesParamA = gSavedSettings.getVector3("RenderToneMapLottesA");
		mToneLottesParamB = gSavedSettings.getVector3("RenderToneMapLottesB");
		mToneUchimuraParamA = gSavedSettings.getVector3("RenderToneMapUchimuraA");
		mToneUchimuraParamB = gSavedSettings.getVector3("RenderToneMapUchimuraB");
		mToneUnchartedParamA = gSavedSettings.getVector3("RenderToneMapUnchartedA");
		mToneUnchartedParamB = gSavedSettings.getVector3("RenderToneMapUnchartedB");
		mToneUnchartedParamC = gSavedSettings.getVector3("RenderToneMapUnchartedC");
	}
	else
	{
		mTonemapType = ALTonemap::NONE;
		mTonemapExposure = 1.f;
	}
	return true;
}

void ALRenderUtil::renderTonemap(LLRenderTarget* src, LLRenderTarget* dst)
{
	gGL.matrixMode(LLRender::MM_PROJECTION);
	gGL.pushMatrix();
	gGL.loadIdentity();
	gGL.matrixMode(LLRender::MM_MODELVIEW);
	gGL.pushMatrix();
	gGL.loadIdentity();

	LLGLDepthTest depth(GL_FALSE, GL_FALSE);

	dst->bindTarget();

	LLGLSLShader* tone_shader = &gDeferredPostTonemapProgram[mTonemapType];

	tone_shader->bind();

	S32 channel = tone_shader->enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, src->getUsage());
	if (channel > -1)
	{
		src->bindTexture(0, channel, LLTexUnit::TFO_POINT);
	}
	tone_shader->uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, dst->getWidth(), dst->getHeight());
	tone_shader->uniform1f(al_exposure, mTonemapExposure);

	switch (mTonemapType)
	{
	default:
		break;
	case ALTonemap::UCHIMURA:
	{
		tone_shader->uniform3fv(tone_uchimura_a, 1, mToneUchimuraParamA.mV);
		tone_shader->uniform3fv(tone_uchimura_b, 1, mToneUchimuraParamB.mV);
		break;
	}
	case ALTonemap::LOTTES:
	{
		tone_shader->uniform3fv(tone_lottes_a, 1, mToneLottesParamA.mV);
		tone_shader->uniform3fv(tone_lottes_b, 1, mToneLottesParamB.mV);
		break;
	}
	case ALTonemap::UNCHARTED:
	{
		tone_shader->uniform3fv(tone_uncharted_a, 1, mToneUnchartedParamA.mV);
		tone_shader->uniform3fv(tone_uncharted_b, 1, mToneUnchartedParamB.mV);
		tone_shader->uniform3fv(tone_uncharted_c, 1, mToneUnchartedParamC.mV);
		break;
	}
	}

	mRenderBuffer->setBuffer(LLVertexBuffer::MAP_VERTEX);
	mRenderBuffer->drawArrays(LLRender::TRIANGLES, 0, 3);
	stop_glerror();

	tone_shader->unbind();
	dst->flush();

	gGL.matrixMode(LLRender::MM_PROJECTION);
	gGL.popMatrix();
	gGL.matrixMode(LLRender::MM_MODELVIEW);
	gGL.popMatrix();
}