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

#include "llimagepng.h"
#include "llimagetga.h"
#include "llimagewebp.h"
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
	gSavedSettings.getControl("RenderColorGradeLUT")->getSignal()->connect(boost::bind(&ALRenderUtil::setupColorGrade, this));
	gSavedSettings.getControl("RenderToneMapType")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapExposure")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapLottesA")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapLottesB")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapUchimuraA")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapUchimuraB")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
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

void ALRenderUtil::releaseGLBuffers()
{
	if (mCGLut)
	{
		LLImageGL::deleteTextures(1, &mCGLut);
		mCGLut = 0;
	}
}

void ALRenderUtil::refreshState()
{
	setupTonemap();
	setupColorGrade();
}

bool ALRenderUtil::setupTonemap()
{
	if (LLPipeline::sRenderDeferred)
	{
		mTonemapType = gSavedSettings.getU32("RenderToneMapType");
		if (mTonemapType >= TONEMAP_COUNT)
		{
			mTonemapType = ALTonemap::TONEMAP_NONE;
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
		mTonemapType = ALTonemap::TONEMAP_NONE;
		mTonemapExposure = 1.f;
	}
	return true;
}

bool ALRenderUtil::setupColorGrade()
{
	if (mCGLut)
	{
		LLImageGL::deleteTextures(1, &mCGLut);
		mCGLut = 0;
	}

	if (LLPipeline::sRenderDeferred)
	{
		std::string lut_name = gSavedSettings.getString("RenderColorGradeLUT");
		if (!lut_name.empty())
		{
			enum class ELutExt
			{
				EXT_IMG_TGA = 0,
				EXT_IMG_PNG,
				EXT_IMG_WEBP,
				EXT_NONE
			};
			std::string lut_path = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "colorlut", lut_name);
			if (!lut_path.empty())
			{
				std::string temp_exten = gDirUtilp->getExtension(lut_path);

				ELutExt extension = ELutExt::EXT_NONE;
				if (temp_exten == "tga")
				{
					extension = ELutExt::EXT_IMG_TGA;
				}
				else if (temp_exten == "png")
				{
					extension = ELutExt::EXT_IMG_PNG;
				}
				else if (temp_exten == "webp")
				{
					extension = ELutExt::EXT_IMG_WEBP;
				}

				LLPointer<LLImageRaw> raw_image = new LLImageRaw;
				bool decode_success = false;

				switch (extension)
				{
				default:
					break;
				case ELutExt::EXT_IMG_TGA:
				{
					LLPointer<LLImageTGA> tga_image = new LLImageTGA;
					if (tga_image->load(lut_path) && tga_image->decode(raw_image, 0.0f))
					{
						decode_success = true;
					}
					break;
				}
				case ELutExt::EXT_IMG_PNG:
				{
					LLPointer<LLImagePNG> png_image = new LLImagePNG;
					if (png_image->load(lut_path) && png_image->decode(raw_image, 0.0f))
					{
						decode_success = true;
					}
					break;
				}
				case ELutExt::EXT_IMG_WEBP:
				{
					LLPointer<LLImageWebP> webp_image = new LLImageWebP;
					if (webp_image->load(lut_path) && webp_image->decode(raw_image, 0.0f))
					{
						decode_success = true;
					}
					break;
				}
				}

				if (decode_success)
				{
					U32 primary_format = 0;
					U32 int_format = 0;
					switch (raw_image->getComponents())
					{
					case 3:
					{
						primary_format = GL_RGB;
						int_format = GL_RGB8;
						break;
					}
					case 4:
					{
						primary_format = GL_RGBA;
						int_format = GL_RGBA8;
						break;
					}
					default:
						return true;
					};

					S32 image_height = raw_image->getHeight();
					S32 image_width = raw_image->getWidth();
					if ((image_height > 0 && image_height <= 64)	   // within dimension limit
						&& !(image_height & (image_height - 1))			   // height is power of 2
						&& ((image_height * image_height) == image_width)) // width is height * height
					{
						mCGLutSize = LLVector4(1.f / image_width, 1.f / image_height, (F32)image_width, (F32)image_height);

						LLImageGL::generateTextures(1, &mCGLut);
						gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, mCGLut);
						LLImageGL::setManualImage(LLTexUnit::getInternalType(LLTexUnit::TT_TEXTURE), 0, int_format, image_width,
							image_height, primary_format, GL_UNSIGNED_BYTE, raw_image->getData(), false);
						stop_glerror();
						gGL.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_BILINEAR);
						gGL.getTexUnit(0)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
					}
				}
			}
		}
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
	glClearColor(0, 0, 0, 0);
	dst->clear(GL_COLOR_BUFFER_BIT);

	LLGLSLShader* tone_shader = (mCGLut != 0 ) ? &gDeferredPostColorGradeLUTProgram[mTonemapType] : &gDeferredPostTonemapProgram[mTonemapType];

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
	case ALTonemap::TONEMAP_UCHIMURA:
	{
		tone_shader->uniform3fv(tone_uchimura_a, 1, mToneUchimuraParamA.mV);
		tone_shader->uniform3fv(tone_uchimura_b, 1, mToneUchimuraParamB.mV);
		break;
	}
	case ALTonemap::TONEMAP_LOTTES:
	{
		tone_shader->uniform3fv(tone_lottes_a, 1, mToneLottesParamA.mV);
		tone_shader->uniform3fv(tone_lottes_b, 1, mToneLottesParamB.mV);
		break;
	}
	case ALTonemap::TONEMAP_UNCHARTED:
	{
		tone_shader->uniform3fv(tone_uncharted_a, 1, mToneUnchartedParamA.mV);
		tone_shader->uniform3fv(tone_uncharted_b, 1, mToneUnchartedParamB.mV);
		tone_shader->uniform3fv(tone_uncharted_c, 1, mToneUnchartedParamC.mV);
		break;
	}
	}

	if (mCGLut != 0)
	{
		S32 channel = tone_shader->enableTexture(LLShaderMgr::COLORGRADE_LUT, LLTexUnit::TT_TEXTURE);
		if (channel > -1)
		{
			gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE, mCGLut);
			gGL.getTexUnit(channel)->setTextureFilteringOption(LLTexUnit::TFO_BILINEAR);
			gGL.getTexUnit(channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
		}

		tone_shader->uniform4fv(LLShaderMgr::COLORGRADE_LUT_SIZE, 1, mCGLutSize.mV);
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