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
#include "llviewershadermgr.h"

const U32 ALRENDER_BUFFER_MASK = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_TEXCOORD1;

static LLStaticHashedString al_exposure("exposure");

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
	LLGLSLShader* shader;

	switch (ALControlCache::RenderToneMapType)
	{
	default:
	case ALTonemap::NONE:
		shader = &gPostTonemapProgram[NONE];
		break;
	case ALTonemap::LINEAR:
		shader = &gPostTonemapProgram[LINEAR];
		break;
	case ALTonemap::REINHARD:
		shader = &gPostTonemapProgram[REINHARD];
		break;
	case ALTonemap::REINHARD2:
		shader = &gPostTonemapProgram[REINHARD2];
		break;
	case ALTonemap::FILMIC:
		shader = &gPostTonemapProgram[FILMIC];
		break;
	case ALTonemap::UNREAL:
		shader = &gPostTonemapProgram[UNREAL];
		break;
	case ALTonemap::ACES:
		shader = &gPostTonemapProgram[ACES];
		break;
	case ALTonemap::UCHIMURA:
		shader = &gPostTonemapProgram[UCHIMURA];
		break;
	case ALTonemap::LOTTES:
		shader = &gPostTonemapProgram[LOTTES];
		break;
	}

	shader->bind();

	S32 channel = shader->enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, src->getUsage());
	if (channel > -1)
	{
		src->bindTexture(0, channel, LLTexUnit::TFO_POINT);
	}
	shader->uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, dst->getWidth(), dst->getHeight());


	F32 exposure = llclamp(ALControlCache::RenderToneMapExposure, 0.1f, 8.f);
	shader->uniform1f(al_exposure, exposure);

	mRenderBuffer->setBuffer(LLVertexBuffer::MAP_VERTEX);
	mRenderBuffer->drawArrays(LLRender::TRIANGLES, 0, 3);
	stop_glerror();

	shader->unbind();
	dst->flush();

	gGL.matrixMode(LLRender::MM_PROJECTION);
	gGL.popMatrix();
	gGL.matrixMode(LLRender::MM_MODELVIEW);
	gGL.popMatrix();
}