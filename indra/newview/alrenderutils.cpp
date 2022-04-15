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

#include "llimagebmp.h"
#include "llimagejpeg.h"
#include "llimagepng.h"
#include "llimagetga.h"
#include "llimagewebp.h"
#include "llrendertarget.h"
#include "llvertexbuffer.h"

#include "alcontrolcache.h"
#include "llviewercontrol.h"
#include "llviewershadermgr.h"
#include "pipeline.h"

#include "absl/strings/numbers.h"

const U32 ALRENDER_BUFFER_MASK = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_TEXCOORD1;

static LLStaticHashedString al_exposure("exposure");
static LLStaticHashedString tone_uchimura_a("tone_uchimura_a");
static LLStaticHashedString tone_uchimura_b("tone_uchimura_b");
static LLStaticHashedString tone_lottes_a("tone_lottes_a");
static LLStaticHashedString tone_lottes_b("tone_lottes_b");
static LLStaticHashedString tone_uncharted_a("tone_uncharted_a");
static LLStaticHashedString tone_uncharted_b("tone_uncharted_b");
static LLStaticHashedString tone_uncharted_c("tone_uncharted_c");
static LLStaticHashedString sharpen_params("sharpen_params");

// BEGIN ZLIB LICENSE

/*
Copyright (c) 2019 - 2020 Georg Lehmann

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
*/
 
/*
   reads .cube files
   returns a vector of bytes
   one byte is one color value
   4 bytes stand for rgba
   the alpha value is always 255
   size will be set according to the size in the file, which can be in [2,256]
   the cube will have the dimentions size * size * size
   so the vector will have a length of size*size*size*4
   See: https://wwwimages2.adobe.com/content/dam/acom/en/products/speedgrade/cc/pdfs/cube-lut-specification-1.0.pdf
*/

class LutCube
{
public:
	std::vector<unsigned char> colorCube;
	int                        size = 0;

	LutCube(const std::string& file);
	LutCube() = default;

private:
	float minX = 0.0f;
	float minY = 0.0f;
	float minZ = 0.0f;

	float maxX = 1.0f;
	float maxY = 1.0f;
	float maxZ = 1.0f;

	int currentX = 0;
	int currentY = 0;
	int currentZ = 0;

	void writeColor(int x, int y, int z, unsigned char r, unsigned char g, unsigned char b);

	void parseLine(std::string line);

	// splits a tripel of floats
	void splitTripel(std::string tripel, float& x, float& y, float& z);

	void clampTripel(float x, float y, float z, unsigned char& outX, unsigned char& outY, unsigned char& outZ);

	// returns the text without leading whitespace
	std::string skipWhiteSpace(std::string text);
};

LutCube::LutCube(const std::string& file)
{
	llifstream cubeStream(file);
	if (!cubeStream.good())
	{
		LL_WARNS() << "lut cube file does not exist" << LL_ENDL;
		return;
	}

	std::string line;

	while (std::getline(cubeStream, line))
	{
		parseLine(line);
	}
}
void LutCube::parseLine(std::string line)
{
	if (line.length() == 0)
	{
		return;
	}
	if (line[0] == '#')
	{
		return;
	}
	if (line.find("LUT_3D_SIZE") != std::string::npos)
	{
		line = line.substr(line.find("LUT_3D_SIZE") + 11);
		line = skipWhiteSpace(line);
		size = std::stoi(line);

		colorCube = std::vector<unsigned char>(size * size * size * 4, 255);
		return;
	}
	if (line.find("DOMAIN_MIN") != std::string::npos)
	{
		line = line.substr(line.find("DOMAIN_MIN") + 10);
		splitTripel(line, minX, minY, minZ);
		return;
	}
	if (line.find("DOMAIN_MAX") != std::string::npos)
	{
		line = line.substr(line.find("DOMAIN_MAX") + 10);
		splitTripel(line, maxX, maxY, maxZ);
		return;
	}
	if (line.find_first_of("0123456789") == 0)
	{
		float         x, y, z;
		unsigned char outX, outY, outZ;
		splitTripel(line, x, y, z);
		clampTripel(x, y, z, outX, outY, outZ);
		writeColor(currentX, currentY, currentZ, outX, outY, outZ);
		if (currentX != size - 1)
		{
			currentX++;
		}
		else if (currentY != size - 1)
		{
			currentY++;
			currentX = 0;
		}
		else if (currentZ != size - 1)
		{
			currentZ++;
			currentX = 0;
			currentY = 0;
		}
		return;
	}
}

std::string LutCube::skipWhiteSpace(std::string text)
{
	while (text.size() > 0 && (text[0] == ' ' || text[0] == '\t'))
	{
		text = text.substr(1);
	}
	return text;
}

void LutCube::splitTripel(std::string tripel, float& x, float& y, float& z)
{
	tripel = skipWhiteSpace(tripel);
	size_t after = tripel.find_first_of(" \n");
	x = std::stof(tripel.substr(0, after));
	tripel = tripel.substr(after);

	tripel = skipWhiteSpace(tripel);
	after = tripel.find_first_of(" \n");
	y = std::stof(tripel.substr(0, after));
	tripel = tripel.substr(after);

	tripel = skipWhiteSpace(tripel);
	z = std::stof(tripel);
}

void LutCube::clampTripel(float x, float y, float z, unsigned char& outX, unsigned char& outY, unsigned char& outZ)
{
	outX = (unsigned char)255 * (x / (maxX - minX));
	outY = (unsigned char)255 * (y / (maxY - minY));
	outZ = (unsigned char)255 * (z / (maxZ - minZ));
}

void LutCube::writeColor(int x, int y, int z, unsigned char r, unsigned char g, unsigned char b)
{
	static const int colorSize = 4; // 4 bytes per point in the cube, rgba

	int locationR = (((z * size) + y) * size + x) * colorSize;

	colorCube[locationR + 0] = r;
	colorCube[locationR + 1] = g;
	colorCube[locationR + 2] = b;
}

// END ZLIB LICENSE

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
	gSavedSettings.getControl("RenderToneMapUnchartedA")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapUnchartedB")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderToneMapUnchartedC")->getSignal()->connect(boost::bind(&ALRenderUtil::setupTonemap, this));
	gSavedSettings.getControl("RenderSharpenMethod")->getSignal()->connect(boost::bind(&ALRenderUtil::setupSharpen, this));
	gSavedSettings.getControl("RenderSharpenCASParams")->getSignal()->connect(boost::bind(&ALRenderUtil::setupSharpen, this));
	gSavedSettings.getControl("RenderSharpenDLSParams")->getSignal()->connect(boost::bind(&ALRenderUtil::setupSharpen, this));
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
	setupSharpen();
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
			std::string lut_path = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "colorlut", lut_name);
			if (!lut_path.empty())
			{
				std::string temp_exten = gDirUtilp->getExtension(lut_path);
				bool decode_success = false;
				LLPointer<LLImageRaw> raw_image;
				bool flip_green = true;
				bool swap_bluegreen = true;
				if (temp_exten == "cube")
				{
					LutCube  lutCube(lut_path);
					if (!lutCube.colorCube.empty())
					{
						try
						{
							raw_image = new LLImageRaw(lutCube.colorCube.data(), lutCube.size * lutCube.size, lutCube.size, 4);
						}
						catch (const std::bad_alloc&)
						{
							return true;
						}
						flip_green = false;
						swap_bluegreen = false;
						decode_success = true;
					}
				}
				else
				{
					enum class ELutExt
					{
						EXT_IMG_TGA = 0,
						EXT_IMG_PNG,
						EXT_IMG_JPEG,
						EXT_IMG_BMP,
						EXT_IMG_WEBP,
						EXT_NONE
					};

					ELutExt extension = ELutExt::EXT_NONE;
					if (temp_exten == "tga")
					{
						extension = ELutExt::EXT_IMG_TGA;
					}
					else if (temp_exten == "png")
					{
						extension = ELutExt::EXT_IMG_PNG;
					}
					else if (temp_exten == "jpg" || temp_exten == "jpeg")
					{
						extension = ELutExt::EXT_IMG_JPEG;
					}
					else if (temp_exten == "bmp")
					{
						extension = ELutExt::EXT_IMG_BMP;
					}
					else if (temp_exten == "webp")
					{
						extension = ELutExt::EXT_IMG_WEBP;
					}

					raw_image = new LLImageRaw;

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
					case ELutExt::EXT_IMG_JPEG:
					{
						LLPointer<LLImageJPEG> jpg_image = new LLImageJPEG;
						if (jpg_image->load(lut_path) && jpg_image->decode(raw_image, 0.0f))
						{
							decode_success = true;
						}
						break;
					}
					case ELutExt::EXT_IMG_BMP:
					{
						LLPointer<LLImageBMP> bmp_image = new LLImageBMP;
						if (bmp_image->load(lut_path) && bmp_image->decode(raw_image, 0.0f))
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
				}

				if (decode_success && raw_image)
				{
					U32 primary_format = 0;
					U32 int_format = 0;
					switch (raw_image->getComponents())
					{
					case 3:
					{
						primary_format = GL_RGB;
						int_format = GL_SRGB8;
						break;
					}
					case 4:
					{
						primary_format = GL_RGBA;
						int_format = GL_SRGB8_ALPHA8;
						break;
					}
					default:
					{
						LL_WARNS() << "Color LUT has invalid number of color components: " << raw_image->getComponents() << LL_ENDL;
						return true;
					}
					};

					S32 image_height = raw_image->getHeight();
					S32 image_width = raw_image->getWidth();
					if ((image_height > 0 && image_height <= gGLManager.mGLMaxTextureSize)		   // within dimension limit
						&& ((image_height * image_height) == image_width)) // width is height * height
					{
						mCGLutSize = LLVector4(image_height, (float)flip_green, (float)swap_bluegreen);

						LLImageGL::generateTextures(1, &mCGLut);
						gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE_3D, mCGLut);
						LLImageGL::setManualImage3D(LLTexUnit::getInternalType(LLTexUnit::TT_TEXTURE_3D), 0, int_format, image_height,
							image_height, image_height, primary_format, GL_UNSIGNED_BYTE, raw_image->getData(), false);
						stop_glerror();
						gGL.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_BILINEAR);
						gGL.getTexUnit(0)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
						gGL.getTexUnit(0)->setTextureColorSpace(LLTexUnit::TCS_LINEAR);
						gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE_3D);
					}
					else
					{
						LL_WARNS() << "Color LUT is invalid width or height: " << image_height << " x " << image_width << LL_ENDL;
					}
				}
				else
				{
					LL_WARNS() << "Failed to decode color grading LUT with extension: " << temp_exten << LL_ENDL;
				}
			}
		}
	}
	return true;
}

void ALRenderUtil::renderTonemap(LLRenderTarget* src, LLRenderTarget* dst, LLRenderTarget* bloom)
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

	channel = tone_shader->enableTexture(LLShaderMgr::DEFERRED_BLOOM, bloom->getUsage());
	if (channel > -1)
	{
		bloom->bindTexture(0, channel);
	}

	tone_shader->uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, src->getWidth(), src->getHeight());
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
		S32 channel = tone_shader->enableTexture(LLShaderMgr::COLORGRADE_LUT, LLTexUnit::TT_TEXTURE_3D);
		if (channel > -1)
		{
			gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE_3D, mCGLut);
			gGL.getTexUnit(channel)->setTextureFilteringOption(LLTexUnit::TFO_BILINEAR);
			gGL.getTexUnit(channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
			gGL.getTexUnit(channel)->setTextureColorSpace(LLTexUnit::TCS_LINEAR);
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

bool ALRenderUtil::setupSharpen()
{
	if (LLPipeline::sRenderDeferred)
	{
		mSharpenMethod = gSavedSettings.getU32("RenderSharpenMethod");
		if (mSharpenMethod >= SHARPEN_COUNT)
		{
			mSharpenMethod = ALSharpen::SHARPEN_NONE;
		}

		LLGLSLShader* sharpen_shader = nullptr;
		switch (mSharpenMethod)
		{
		case ALSharpen::SHARPEN_CAS:
		{
			sharpen_shader = &gDeferredPostCASProgram;
			sharpen_shader->bind();
			LLVector3 params = gSavedSettings.getVector3("RenderSharpenCASParams");
			params.clamp(LLVector3::zero, LLVector3::all_one);
			sharpen_shader->uniform3fv(sharpen_params, 1, params.mV);
			sharpen_shader->unbind();
			break;
		}
		case ALSharpen::SHARPEN_DLS:
		{
			sharpen_shader = &gDeferredPostDLSProgram;
			sharpen_shader->bind();
			LLVector3 params = gSavedSettings.getVector3("RenderSharpenDLSParams");
			params.clamp(LLVector3::zero, LLVector3::all_one);
			sharpen_shader->uniform3fv(sharpen_params, 1, params.mV);
			sharpen_shader->unbind();
			break;
		}
		default:
		case ALSharpen::SHARPEN_NONE:
			break;
		}
	}
	else
	{
		mSharpenMethod = ALSharpen::SHARPEN_NONE;
	}
	return true;
}

void ALRenderUtil::renderSharpen(LLRenderTarget* src, LLRenderTarget* dst)
{
	if (mSharpenMethod == ALSharpen::SHARPEN_NONE)
	{
		return;
	}

	LLGLSLShader* sharpen_shader = nullptr;
	switch (mSharpenMethod)
	{
	case ALSharpen::SHARPEN_CAS:
		sharpen_shader = &gDeferredPostCASProgram;
		break;
	case ALSharpen::SHARPEN_DLS:
		sharpen_shader = &gDeferredPostDLSProgram;
		break;
	default:
	case ALSharpen::SHARPEN_NONE:
		break;
	}

	LLGLDepthTest depth(GL_FALSE, GL_FALSE);
	LLGLDisable srgb(GL_FRAMEBUFFER_SRGB);

	// Bind setup:
	if (dst)
	{
		dst->bindTarget();
	}

	sharpen_shader->bind();

	// Draw
	src->bindTexture(0, 0, LLTexUnit::TFO_POINT, LLTexUnit::TCS_LINEAR);

	mRenderBuffer->setBuffer(LLVertexBuffer::MAP_VERTEX);
	mRenderBuffer->drawArrays(LLRender::TRIANGLES, 0, 3);

	if (dst)
	{
		dst->flush();
	}

	sharpen_shader->unbind();
	gGL.getTexUnit(0)->disable();
}