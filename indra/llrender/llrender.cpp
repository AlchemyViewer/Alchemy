 /** 
 * @file llrender.cpp
 * @brief LLRender implementation
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

#include "llrender.h"

#include "llvertexbuffer.h"
#include "llcubemap.h"
#include "llglslshader.h"
#include "llimagegl.h"
#include "llrendertarget.h"
#include "lltexture.h"
#include "llshadermgr.h"
#include "llmatrix4a.h"
#include "alglmath.h"

LLRender gGL;

// Handy copies of last good GL matrices
LLMatrix4a	gGLModelView;
LLMatrix4a	gGLLastModelView;
LLMatrix4a	gGLLastProjection;
LLMatrix4a	gGLProjection;
S32			gGLViewport[4];

U32 LLRender::sUICalls = 0;
U32 LLRender::sUIVerts = 0;
U32 LLTexUnit::sWhiteTexture = 0;
F32 LLRender::sAnisotropicFilteringLevel = 0.f;
bool LLRender::sGLCoreProfile = false;
bool LLRender::sNsightDebugSupport = false;
LLVector2 LLRender::sUIGLScaleFactor = LLVector2(1.f, 1.f);

static const U32 LL_NUM_TEXTURE_LAYERS = 32; 
static const U32 LL_NUM_LIGHT_UNITS = 8;

static const GLenum sGLTextureType[] =
{
	GL_TEXTURE_2D,
	GL_TEXTURE_RECTANGLE,
	GL_TEXTURE_CUBE_MAP,
	GL_TEXTURE_2D_MULTISAMPLE,
    GL_TEXTURE_3D
};

static const GLint sGLAddressMode[] =
{	
	GL_REPEAT,
	GL_MIRRORED_REPEAT,
	GL_CLAMP_TO_EDGE
};

static const GLenum sGLCompareFunc[] =
{
	GL_NEVER,
	GL_ALWAYS,
	GL_LESS,
	GL_LEQUAL,
	GL_EQUAL,
	GL_NOTEQUAL,
	GL_GEQUAL,
	GL_GREATER
};

const U32 immediate_mask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_COLOR | LLVertexBuffer::MAP_TEXCOORD0;

static const GLenum sGLBlendFactor[] =
{
	GL_ONE,
	GL_ZERO,
	GL_DST_COLOR,
	GL_SRC_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_DST_ALPHA,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,

	GL_ZERO // 'BF_UNDEF'
};

LLTexUnit::LLTexUnit(S32 index)
	: mCurrTexType(TT_NONE), mCurrBlendType(TB_MULT), 
	mCurrColorOp(TBO_MULT), mCurrAlphaOp(TBO_MULT),
	mCurrColorSrc1(TBS_TEX_COLOR), mCurrColorSrc2(TBS_PREV_COLOR),
	mCurrAlphaSrc1(TBS_TEX_ALPHA), mCurrAlphaSrc2(TBS_PREV_ALPHA),
    mCurrColorScale(1), mCurrAlphaScale(1), mCurrTexture(0), mTexColorSpace(TCS_SRGB),
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

	//
	// Per apple spec, don't call glEnable/glDisable when index exceeds max texture units
	// http://www.mailinglistarchive.com/html/mac-opengl@lists.apple.com/2008-07/msg00653.html
	//
	bool enableDisable = !LLGLSLShader::sNoFixedFunction && 
		(mIndex < gGLManager.mNumTextureUnits) && mCurrTexType != LLTexUnit::TT_MULTISAMPLE_TEXTURE;
		
	if (mCurrTexType != TT_NONE)
	{
		if (enableDisable)
		{
			glEnable(sGLTextureType[mCurrTexType]);
		}
		
		glBindTexture(sGLTextureType[mCurrTexType], mCurrTexture);
	}
	else
	{
		if (enableDisable)
		{
			glDisable(GL_TEXTURE_2D);
		}
		
		glBindTexture(GL_TEXTURE_2D, 0);	
	}

	if (mCurrBlendType != TB_COMBINE)
	{
		setTextureBlendType(mCurrBlendType);
	}
	else
	{
		setTextureCombiner(mCurrColorOp, mCurrColorSrc1, mCurrColorSrc2, false);
		setTextureCombiner(mCurrAlphaOp, mCurrAlphaSrc1, mCurrAlphaSrc2, true);
	}

    setTextureColorSpace(mTexColorSpace);
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
		stop_glerror();
		activate();
		stop_glerror();
		if (mCurrTexType != TT_NONE && !gGL.mDirty)
		{
			disable(); // Force a disable of a previous texture type if it's enabled.
			stop_glerror();
		}
		mCurrTexType = type;

		gGL.flush();
		if (!LLGLSLShader::sNoFixedFunction && 
			type != LLTexUnit::TT_MULTISAMPLE_TEXTURE &&
			mIndex < gGLManager.mNumTextureUnits)
		{
			stop_glerror();
			glEnable(sGLTextureType[type]);
			stop_glerror();
		}
	}
}

void LLTexUnit::disable(void)
{
	if (mIndex < 0) return;

	if (mCurrTexType != TT_NONE)
	{
		activate();
		unbind(mCurrTexType);
		gGL.flush();
		if (!LLGLSLShader::sNoFixedFunction &&
			mCurrTexType != LLTexUnit::TT_MULTISAMPLE_TEXTURE &&
			mIndex < gGLManager.mNumTextureUnits)
		{
			glDisable(sGLTextureType[mCurrTexType]);
		}

        setTextureColorSpace(TCS_SRGB);
		
		mCurrTexType = TT_NONE;
	}
}

bool LLTexUnit::bind(LLTexture* texture, bool for_rendering, bool forceBind)
{
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
					if(gl_tex->updateBindStats(gl_tex->mTextureMemory))
					{
						texture->setActive() ;
						texture->updateBindStatsForTester() ;
					}
					mHasMipMaps = gl_tex->mHasMipMaps;
					if (gl_tex->mTexOptionsDirty)
					{
						gl_tex->mTexOptionsDirty = false;
						setTextureAddressMode(gl_tex->mAddressMode);
						setTextureFilteringOption(gl_tex->mFilterOption);
                    }
                    setTextureColorSpace(mTexColorSpace);
				}
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
#if SHOW_DEBUG
			if (texture)
			{
				LL_DEBUGS() << "NULL LLTexUnit::bind GL image" << LL_ENDL;
			}
			else
			{
				LL_DEBUGS() << "NULL LLTexUnit::bind texture" << LL_ENDL;
			}
#endif
			return false;
		}
	}
	else
	{ // mIndex < 0
		return false;
	}

	return true;
}

bool LLTexUnit::bind(LLImageGL* texture, bool for_rendering, bool forceBind)
{
	stop_glerror();
	if (mIndex < 0) return false;

	if(!texture)
	{
#if SHOW_DEBUG
		LL_DEBUGS() << "NULL LLTexUnit::bind texture" << LL_ENDL;
#endif
		return false;
	}

	if(!texture->getTexName())
	{
		if(LLImageGL::sDefaultGLTexture && LLImageGL::sDefaultGLTexture->getTexName())
		{
			return bind(LLImageGL::sDefaultGLTexture) ;
		}
		stop_glerror();
		return false ;
	}

	if ((mCurrTexture != texture->getTexName()) || forceBind)
	{
		gGL.flush();
		stop_glerror();
		activate();
		stop_glerror();
		enable(texture->getTarget());
		stop_glerror();
		mCurrTexture = texture->getTexName();
		glBindTexture(sGLTextureType[texture->getTarget()], mCurrTexture);
		stop_glerror();
		texture->updateBindStats(texture->mTextureMemory);		
		mHasMipMaps = texture->mHasMipMaps;
		if (texture->mTexOptionsDirty)
		{
			stop_glerror();
			texture->mTexOptionsDirty = false;
			setTextureAddressMode(texture->mAddressMode);
			setTextureFilteringOption(texture->mFilterOption);
			stop_glerror();
		}
        setTextureColorSpace(mTexColorSpace);
	}

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

	if (mCurrTexture != cubeMap->mImages[0]->getTexName())
	{
		if (gGLManager.mHasCubeMap && LLCubeMap::sUseCubeMaps)
		{
			activate();
			enable(LLTexUnit::TT_CUBE_MAP);
            mCurrTexture = cubeMap->mImages[0]->getTexName();
			glBindTexture(GL_TEXTURE_CUBE_MAP, mCurrTexture);
			mHasMipMaps = cubeMap->mImages[0]->mHasMipMaps;
			cubeMap->mImages[0]->updateBindStats(cubeMap->mImages[0]->mTextureMemory);
			if (cubeMap->mImages[0]->mTexOptionsDirty)
			{
				cubeMap->mImages[0]->mTexOptionsDirty = false;
				setTextureAddressMode(cubeMap->mImages[0]->mAddressMode);
				setTextureFilteringOption(cubeMap->mImages[0]->mFilterOption);
            }
            setTextureColorSpace(mTexColorSpace);
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
bool LLTexUnit::bind(LLRenderTarget* renderTarget, bool bindDepth)
{
	if (mIndex < 0) return false;

	gGL.flush();

	if (bindDepth)
	{
		if (renderTarget->hasStencil())
		{
			LL_ERRS() << "Cannot bind a render buffer for sampling.  Allocate render target without a stencil buffer if sampling of depth buffer is required." << LL_ENDL;
		}

		bindManual(renderTarget->getUsage(), renderTarget->getDepth());
	}
	else
	{
		bindManual(renderTarget->getUsage(), renderTarget->getTexture());
	}

	return true;
}

bool LLTexUnit::bindManual(eTextureType type, U32 texture, bool hasMips)
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
		glBindTexture(sGLTextureType[type], texture);
        mHasMipMaps = hasMips;
        setTextureColorSpace(mTexColorSpace);
	}
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

        // Always make sure our texture color space is reset to linear.  SRGB sampling should be opt-in in the vast majority of cases.  Also prevents color space "popping".
        mTexColorSpace = TCS_SRGB;
		if (LLGLSLShader::sNoFixedFunction && type == LLTexUnit::TT_TEXTURE)
		{
			glBindTexture(sGLTextureType[type], sWhiteTexture);
		}
		else
		{
			glBindTexture(sGLTextureType[type], 0);
		}
		stop_glerror();
	}
}

void LLTexUnit::setTextureAddressMode(eTextureAddressMode mode)
{
	if (mIndex < 0 || mCurrTexture == 0) return;

	gGL.flush();

	activate();

	glTexParameteri (sGLTextureType[mCurrTexType], GL_TEXTURE_WRAP_S, sGLAddressMode[mode]);
	glTexParameteri (sGLTextureType[mCurrTexType], GL_TEXTURE_WRAP_T, sGLAddressMode[mode]);
	if (mCurrTexType == TT_CUBE_MAP || mCurrTexType == TT_TEXTURE_3D)
	{
		glTexParameteri (sGLTextureType[mCurrTexType], GL_TEXTURE_WRAP_R, sGLAddressMode[mode]);
	}
}

void LLTexUnit::setTextureFilteringOption(LLTexUnit::eTextureFilterOptions option)
{
	if (mIndex < 0 || mCurrTexture == 0 || mCurrTexType == LLTexUnit::TT_MULTISAMPLE_TEXTURE) return;

	gGL.flush();

	if (option == TFO_POINT)
	{
		glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	else
	{
		glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	if (option >= TFO_TRILINEAR && mHasMipMaps)
	{
		glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	} 
	else if (option >= TFO_BILINEAR)
	{
		if (mHasMipMaps)
		{
			glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
		}
		else
		{
			glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		}
	}
	else
	{
		if (mHasMipMaps)
		{
			glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		}
		else
		{
			glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		}
	}

	if (gGLManager.mHasAnisotropic)
	{
		if (option == TFO_ANISOTROPIC && LLRender::sAnisotropicFilteringLevel > 1.f)
		{
			F32 aniso_level = llclamp(LLRender::sAnisotropicFilteringLevel, 1.f, gGLManager.mGLMaxAnisotropy);
			glTexParameterf(sGLTextureType[mCurrTexType], GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso_level);
		}
		else
		{
			glTexParameterf(sGLTextureType[mCurrTexType], GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.f);
		}
	}
}

void LLTexUnit::setTextureBlendType(eTextureBlendType type)
{
	if (LLGLSLShader::sNoFixedFunction)
	{ //texture blend type means nothing when using shaders
		return;
	}

	if (mIndex < 0) return;

	// Do nothing if it's already correctly set.
	if (mCurrBlendType == type && !gGL.mDirty)
	{
		return;
	}

	gGL.flush();

	activate();
	mCurrBlendType = type;
	S32 scale_amount = 1;
	switch (type) 
	{
		case TB_REPLACE:
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			break;
		case TB_ADD:
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
			break;
		case TB_MULT:
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			break;
		case TB_MULT_X2:
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			scale_amount = 2;
			break;
		case TB_ALPHA_BLEND:
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
			break;
		case TB_COMBINE:
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
			break;
		default:
			LL_ERRS() << "Unknown Texture Blend Type: " << type << LL_ENDL;
			break;
	}
	setColorScale(scale_amount);
	setAlphaScale(1);
}

GLint LLTexUnit::getTextureSource(eTextureBlendSrc src)
{
	switch(src)
	{
		// All four cases should return the same value.
		case TBS_PREV_COLOR:
		case TBS_PREV_ALPHA:
		case TBS_ONE_MINUS_PREV_COLOR:
		case TBS_ONE_MINUS_PREV_ALPHA:
			return GL_PREVIOUS;

		// All four cases should return the same value.
		case TBS_TEX_COLOR:
		case TBS_TEX_ALPHA:
		case TBS_ONE_MINUS_TEX_COLOR:
		case TBS_ONE_MINUS_TEX_ALPHA:
			return GL_TEXTURE;

		// All four cases should return the same value.
		case TBS_VERT_COLOR:
		case TBS_VERT_ALPHA:
		case TBS_ONE_MINUS_VERT_COLOR:
		case TBS_ONE_MINUS_VERT_ALPHA:
			return GL_PRIMARY_COLOR;

		// All four cases should return the same value.
		case TBS_CONST_COLOR:
		case TBS_CONST_ALPHA:
		case TBS_ONE_MINUS_CONST_COLOR:
		case TBS_ONE_MINUS_CONST_ALPHA:
			return GL_CONSTANT;

		default:
			LL_WARNS() << "Unknown eTextureBlendSrc: " << src << ".  Using Vertex Color instead." << LL_ENDL;
			return GL_PRIMARY_COLOR;
	}
}

GLint LLTexUnit::getTextureSourceType(eTextureBlendSrc src, bool isAlpha)
{
	switch(src)
	{
		// All four cases should return the same value.
		case TBS_PREV_COLOR:
		case TBS_TEX_COLOR:
		case TBS_VERT_COLOR:
		case TBS_CONST_COLOR:
			return (isAlpha) ? GL_SRC_ALPHA: GL_SRC_COLOR;

		// All four cases should return the same value.
		case TBS_PREV_ALPHA:
		case TBS_TEX_ALPHA:
		case TBS_VERT_ALPHA:
		case TBS_CONST_ALPHA:
			return GL_SRC_ALPHA;

		// All four cases should return the same value.
		case TBS_ONE_MINUS_PREV_COLOR:
		case TBS_ONE_MINUS_TEX_COLOR:
		case TBS_ONE_MINUS_VERT_COLOR:
		case TBS_ONE_MINUS_CONST_COLOR:
			return (isAlpha) ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE_MINUS_SRC_COLOR;

		// All four cases should return the same value.
		case TBS_ONE_MINUS_PREV_ALPHA:
		case TBS_ONE_MINUS_TEX_ALPHA:
		case TBS_ONE_MINUS_VERT_ALPHA:
		case TBS_ONE_MINUS_CONST_ALPHA:
			return GL_ONE_MINUS_SRC_ALPHA;

		default:
			LL_WARNS() << "Unknown eTextureBlendSrc: " << src << ".  Using Source Color or Alpha instead." << LL_ENDL;
			return (isAlpha) ? GL_SRC_ALPHA: GL_SRC_COLOR;
	}
}

void LLTexUnit::setTextureCombiner(eTextureBlendOp op, eTextureBlendSrc src1, eTextureBlendSrc src2, bool isAlpha)
{
	if (LLGLSLShader::sNoFixedFunction)
	{ //register combiners do nothing when not using fixed function
		return;
	}	

	if (mIndex < 0) return;

	activate();
	if (mCurrBlendType != TB_COMBINE || gGL.mDirty)
	{
		mCurrBlendType = TB_COMBINE;
		gGL.flush();
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	}
	
	// We want an early out, because this function does a LOT of stuff.
	if ( ( (isAlpha && (mCurrAlphaOp == op) && (mCurrAlphaSrc1 == src1) && (mCurrAlphaSrc2 == src2))
			|| (!isAlpha && (mCurrColorOp == op) && (mCurrColorSrc1 == src1) && (mCurrColorSrc2 == src2)) ) && !gGL.mDirty)
	{
		return;
	}

	gGL.flush();

	// Get the gl source enums according to the eTextureBlendSrc sources passed in
	GLint source1 = getTextureSource(src1);
	GLint source2 = getTextureSource(src2);
	// Get the gl operand enums according to the eTextureBlendSrc sources passed in
	GLint operand1 = getTextureSourceType(src1, isAlpha);
	GLint operand2 = getTextureSourceType(src2, isAlpha);
	// Default the scale amount to 1
	S32 scale_amount = 1;
	GLenum comb_enum, src0_enum, src1_enum, src2_enum, operand0_enum, operand1_enum, operand2_enum;
	
	if (isAlpha)
	{
		// Set enums to ALPHA ones
		comb_enum = GL_COMBINE_ALPHA;
		src0_enum = GL_SOURCE0_ALPHA;
		src1_enum = GL_SOURCE1_ALPHA;
		src2_enum = GL_SOURCE2_ALPHA;
		operand0_enum = GL_OPERAND0_ALPHA;
		operand1_enum = GL_OPERAND1_ALPHA;
		operand2_enum = GL_OPERAND2_ALPHA;

		// cache current combiner
		mCurrAlphaOp = op;
		mCurrAlphaSrc1 = src1;
		mCurrAlphaSrc2 = src2;
	}
	else 
	{
		// Set enums to RGB ones
		comb_enum = GL_COMBINE_RGB;
		src0_enum = GL_SOURCE0_RGB;
		src1_enum = GL_SOURCE1_RGB;
		src2_enum = GL_SOURCE2_RGB;
		operand0_enum = GL_OPERAND0_RGB;
		operand1_enum = GL_OPERAND1_RGB;
		operand2_enum = GL_OPERAND2_RGB;

		// cache current combiner
		mCurrColorOp = op;
		mCurrColorSrc1 = src1;
		mCurrColorSrc2 = src2;
	}

	switch(op)
	{
		case TBO_REPLACE:
			// Slightly special syntax (no second sources), just set all and return.
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_REPLACE);
			glTexEnvi(GL_TEXTURE_ENV, src0_enum, source1);
			glTexEnvi(GL_TEXTURE_ENV, operand0_enum, operand1);
			(isAlpha) ? setAlphaScale(1) : setColorScale(1);
			return;

		case TBO_MULT:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_MODULATE);
			break;

		case TBO_MULT_X2:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_MODULATE);
			scale_amount = 2;
			break;

		case TBO_MULT_X4:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_MODULATE);
			scale_amount = 4;
			break;

		case TBO_ADD:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_ADD);
			break;

		case TBO_ADD_SIGNED:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_ADD_SIGNED);
			break;

		case TBO_SUBTRACT:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_SUBTRACT);
			break;

		case TBO_LERP_VERT_ALPHA:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_INTERPOLATE);
			glTexEnvi(GL_TEXTURE_ENV, src2_enum, GL_PRIMARY_COLOR);
			glTexEnvi(GL_TEXTURE_ENV, operand2_enum, GL_SRC_ALPHA);
			break;

		case TBO_LERP_TEX_ALPHA:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_INTERPOLATE);
			glTexEnvi(GL_TEXTURE_ENV, src2_enum, GL_TEXTURE);
			glTexEnvi(GL_TEXTURE_ENV, operand2_enum, GL_SRC_ALPHA);
			break;

		case TBO_LERP_PREV_ALPHA:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_INTERPOLATE);
			glTexEnvi(GL_TEXTURE_ENV, src2_enum, GL_PREVIOUS);
			glTexEnvi(GL_TEXTURE_ENV, operand2_enum, GL_SRC_ALPHA);
			break;

		case TBO_LERP_CONST_ALPHA:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_INTERPOLATE);
			glTexEnvi(GL_TEXTURE_ENV, src2_enum, GL_CONSTANT);
			glTexEnvi(GL_TEXTURE_ENV, operand2_enum, GL_SRC_ALPHA);
			break;

		case TBO_LERP_VERT_COLOR:
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_INTERPOLATE);
			glTexEnvi(GL_TEXTURE_ENV, src2_enum, GL_PRIMARY_COLOR);
			glTexEnvi(GL_TEXTURE_ENV, operand2_enum, (isAlpha) ? GL_SRC_ALPHA : GL_SRC_COLOR);
			break;

		default:
			LL_WARNS() << "Unknown eTextureBlendOp: " << op << ".  Setting op to replace." << LL_ENDL;
			// Slightly special syntax (no second sources), just set all and return.
			glTexEnvi(GL_TEXTURE_ENV, comb_enum, GL_REPLACE);
			glTexEnvi(GL_TEXTURE_ENV, src0_enum, source1);
			glTexEnvi(GL_TEXTURE_ENV, operand0_enum, operand1);
			(isAlpha) ? setAlphaScale(1) : setColorScale(1);
			return;
	}

	// Set sources, operands, and scale accordingly
	glTexEnvi(GL_TEXTURE_ENV, src0_enum, source1);
	glTexEnvi(GL_TEXTURE_ENV, operand0_enum, operand1);
	glTexEnvi(GL_TEXTURE_ENV, src1_enum, source2);
	glTexEnvi(GL_TEXTURE_ENV, operand1_enum, operand2);
	(isAlpha) ? setAlphaScale(scale_amount) : setColorScale(scale_amount);
}

void LLTexUnit::setColorScale(S32 scale)
{
	if (mCurrColorScale != scale || gGL.mDirty)
	{
		mCurrColorScale = scale;
		gGL.flush();
		glTexEnvi( GL_TEXTURE_ENV, GL_RGB_SCALE, scale );
	}
}

void LLTexUnit::setAlphaScale(S32 scale)
{
	if (mCurrAlphaScale != scale || gGL.mDirty)
	{
		mCurrAlphaScale = scale;
		gGL.flush();
		glTexEnvi( GL_TEXTURE_ENV, GL_ALPHA_SCALE, scale );
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

void LLTexUnit::setTextureColorSpace(eTextureColorSpace space)
{
    mTexColorSpace = space;

    if (gGLManager.mHasTexturesRGBDecode)
    {
        if (space == TCS_LINEAR)
        {
            glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_SRGB_DECODE_EXT, GL_DECODE_EXT);
        }
        else
        {
            glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_SRGB_DECODE_EXT, GL_SKIP_DECODE_EXT);
        }

        if (gDebugGL)
        {
			stop_glerror();
        }
    }
}

LLLightState::LLLightState(S32 index)
: mIndex(index),
  mEnabled(false),
  mConstantAtten(1.f),
  mLinearAtten(0.f),
  mQuadraticAtten(0.f),
  mSpotExponent(0.f),
  mSpotCutoff(180.f)
{
	if (mIndex == 0)
	{
		mDiffuse.set(1,1,1,1);
        mDiffuseB.set(0,0,0,0);
		mSpecular.set(1,1,1,1);
	}

    mSunIsPrimary = true;

	mAmbient.set(0,0,0,1);
	mPosition.set(0,0,1,0);
	mSpotDirection.set(0,0,-1);
}

void LLLightState::enable()
{
	if (!mEnabled)
	{
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glEnable(GL_LIGHT0+mIndex);
		}
		mEnabled = true;
	}
}

void LLLightState::disable()
{
	if (mEnabled)
	{
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glDisable(GL_LIGHT0+mIndex);
		}
		mEnabled = false;
	}
}

void LLLightState::setDiffuse(const LLColor4& diffuse)
{
	if (mDiffuse != diffuse)
	{
		++gGL.mLightHash;
		mDiffuse = diffuse;
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glLightfv(GL_LIGHT0+mIndex, GL_DIFFUSE, mDiffuse.mV);
		}
	}
}

void LLLightState::setDiffuseB(const LLColor4& diffuse)
{
    if (mDiffuseB != diffuse)
	{
		++gGL.mLightHash;
		mDiffuseB = diffuse;
	}
}

void LLLightState::setSunPrimary(bool v)
{
    if (mSunIsPrimary != v)
    {
        ++gGL.mLightHash;
		mSunIsPrimary = v;
    }
}

void LLLightState::setAmbient(const LLColor4& ambient)
{
	if (mAmbient != ambient)
	{
		++gGL.mLightHash;
		mAmbient = ambient;
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glLightfv(GL_LIGHT0+mIndex, GL_AMBIENT, mAmbient.mV);
		}
	}
}

void LLLightState::setSpecular(const LLColor4& specular)
{
	if (mSpecular != specular)
	{
		++gGL.mLightHash;
		mSpecular = specular;
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glLightfv(GL_LIGHT0+mIndex, GL_SPECULAR, mSpecular.mV);
		}
	}
}

void LLLightState::setPosition(const LLVector4& position)
{
	//always set position because modelview matrix may have changed
	++gGL.mLightHash;
	mPosition = position;
	if (!LLGLSLShader::sNoFixedFunction)
	{
		glLightfv(GL_LIGHT0+mIndex, GL_POSITION, mPosition.mV);
	}
	else
	{ //transform position by current modelview matrix
		LLVector4a pos;
		pos.loadua(position.mV);

		gGL.getModelviewMatrix().rotate4(pos,pos);

		mPosition.set(pos.getF32ptr());
	}

}

void LLLightState::setConstantAttenuation(const F32& atten)
{
	if (mConstantAtten != atten)
	{
		mConstantAtten = atten;
		++gGL.mLightHash;
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glLightf(GL_LIGHT0+mIndex, GL_CONSTANT_ATTENUATION, atten);
		}
	}
}

void LLLightState::setLinearAttenuation(const F32& atten)
{
	if (mLinearAtten != atten)
	{
		++gGL.mLightHash;
		mLinearAtten = atten;
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glLightf(GL_LIGHT0+mIndex, GL_LINEAR_ATTENUATION, atten);
		}
	}
}

void LLLightState::setQuadraticAttenuation(const F32& atten)
{
	if (mQuadraticAtten != atten)
	{
		++gGL.mLightHash;
		mQuadraticAtten = atten;
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glLightf(GL_LIGHT0+mIndex, GL_QUADRATIC_ATTENUATION, atten);
		}
	}
}

void LLLightState::setSpotExponent(const F32& exponent)
{
	if (mSpotExponent != exponent)
	{
		++gGL.mLightHash;
		mSpotExponent = exponent;
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glLightf(GL_LIGHT0+mIndex, GL_SPOT_EXPONENT, exponent);
		}
	}
}

void LLLightState::setSpotCutoff(const F32& cutoff)
{
	if (mSpotCutoff != cutoff)
	{
		++gGL.mLightHash;
		mSpotCutoff = cutoff;
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glLightf(GL_LIGHT0+mIndex, GL_SPOT_CUTOFF, cutoff);
		}
	}
}

void LLLightState::setSpotDirection(const LLVector3& direction)
{
	//always set direction because modelview matrix may have changed
	++gGL.mLightHash;
	mSpotDirection = direction;
	if (!LLGLSLShader::sNoFixedFunction)
	{
		glLightfv(GL_LIGHT0+mIndex, GL_SPOT_DIRECTION, direction.mV);
	}
	else
	{ //transform direction by current modelview matrix
		LLVector4a dir;
		dir.load3(direction.mV);

		gGL.getModelviewMatrix().rotate(dir,dir);

		mSpotDirection.set(dir.getF32ptr());
	}
}

LLRender::LLRender()
  : mDirty(false),
    mCount(0),
    mMode(LLRender::TRIANGLES),
    mCurrTextureUnitIndex(0),
    mLineWidth(1.f),
	mPrimitiveReset(false)
{	
	mTexUnits.reserve(LL_NUM_TEXTURE_LAYERS);
	for (U32 i = 0; i < LL_NUM_TEXTURE_LAYERS; i++)
	{
		mTexUnits.push_back(new LLTexUnit(i));
	}
	mDummyTexUnit = new LLTexUnit(-1);

	for (U32 i = 0; i < LL_NUM_LIGHT_UNITS; ++i)
	{
		mLightState.push_back(new LLLightState(i));
	}

	for (U32 i = 0; i < 4; i++)
	{
		mCurrColorMask[i] = true;
	}

	mCurrAlphaFunc = CF_DEFAULT;
	mCurrAlphaFuncVal = 0.01f;
	mCurrBlendColorSFactor = BF_UNDEF;
	mCurrBlendAlphaSFactor = BF_UNDEF;
	mCurrBlendColorDFactor = BF_UNDEF;
	mCurrBlendAlphaDFactor = BF_UNDEF;

	mMatrixMode = LLRender::MM_MODELVIEW;
	
	for (U32 i = 0; i < NUM_MATRIX_MODES; ++i)
	{
		mMatIdx[i] = 0;
		mMatHash[i] = 0;
		mCurMatHash[i] = 0xFFFFFFFF;
	}

	mLightHash = 0;
	
	//Init base matrix for each mode
	for(S32 i = 0; i < NUM_MATRIX_MODES; ++i)
	{
		mMatrix[i][0].setIdentity();
	}

}

LLRender::~LLRender()
{
	shutdown();
}

void LLRender::init()
{
	restoreVertexBuffers();
}

void LLRender::shutdown()
{
	for (U32 i = 0; i < mTexUnits.size(); i++)
	{
		delete mTexUnits[i];
	}
	mTexUnits.clear();
	delete mDummyTexUnit;
	mDummyTexUnit = NULL;

	for (U32 i = 0; i < mLightState.size(); ++i)
	{
		delete mLightState[i];
	}
	mLightState.clear();

	mBuffer = nullptr;
}

void LLRender::refreshState(void)
{
	mDirty = true;

	U32 active_unit = mCurrTextureUnitIndex;

	for (U32 i = 0; i < mTexUnits.size(); i++)
	{
		mTexUnits[i]->refreshState();
	}
	
	mTexUnits[active_unit]->activate();
	stop_glerror();

	setColorMask(mCurrColorMask[0], mCurrColorMask[1], mCurrColorMask[2], mCurrColorMask[3]);
	stop_glerror();
	
	setAlphaRejectSettings(mCurrAlphaFunc, mCurrAlphaFuncVal);
	stop_glerror();

	mDirty = false;
}

void LLRender::resetVertexBuffers()
{
	mBuffer = nullptr;
}

void LLRender::restoreVertexBuffers()
{
	llassert_always(mBuffer.isNull());
	stop_glerror();
	mBuffer = new LLVertexBuffer(immediate_mask, 0);
	stop_glerror();
	mBuffer->allocateBuffer(4096, 0, TRUE);
	stop_glerror();
	mBuffer->getVertexStrider(mVerticesp);
	stop_glerror();
	mBuffer->getTexCoord0Strider(mTexcoordsp);
	stop_glerror();
	mBuffer->getColorStrider(mColorsp);
	stop_glerror();
}

void LLRender::syncLightState()
{
    LLGLSLShader *shader = LLGLSLShader::sCurBoundShaderPtr;

    if (!shader)
    {
        return;
    }

    if (shader->mLightHash != mLightHash)
    {
        shader->mLightHash = mLightHash;

        LLVector4 position[LL_NUM_LIGHT_UNITS];
        LLVector3 direction[LL_NUM_LIGHT_UNITS];
        LLVector4 attenuation[LL_NUM_LIGHT_UNITS];
		LLVector3 light_diffuse[LL_NUM_LIGHT_UNITS];

        for (U32 i = 0; i < LL_NUM_LIGHT_UNITS; i++)
        {
            LLLightState *light = mLightState[i];

            position[i]  = light->mPosition;
            direction[i] = light->mSpotDirection;
            attenuation[i].set(light->mLinearAtten, light->mQuadraticAtten, light->mSpecular.mV[2], light->mSpecular.mV[3]);
			light_diffuse[i].set(light->mDiffuse.mV);
        }

        shader->uniform4fv(LLShaderMgr::LIGHT_POSITION, LL_NUM_LIGHT_UNITS, position[0].mV);
        shader->uniform3fv(LLShaderMgr::LIGHT_DIRECTION, LL_NUM_LIGHT_UNITS, direction[0].mV);
        shader->uniform4fv(LLShaderMgr::LIGHT_ATTENUATION, LL_NUM_LIGHT_UNITS, attenuation[0].mV);
        shader->uniform3fv(LLShaderMgr::LIGHT_DIFFUSE, LL_NUM_LIGHT_UNITS, light_diffuse[0].mV);
        shader->uniform4fv(LLShaderMgr::LIGHT_AMBIENT, 1, mAmbientLightColor.mV);
        shader->uniform1i(LLShaderMgr::SUN_UP_FACTOR, mLightState[0]->mSunIsPrimary ? 1 : 0);
        shader->uniform4fv(LLShaderMgr::AMBIENT, 1, mAmbientLightColor.mV);
        shader->uniform4fv(LLShaderMgr::SUNLIGHT_COLOR, 1, mLightState[0]->mDiffuse.mV);
        shader->uniform4fv(LLShaderMgr::MOONLIGHT_COLOR, 1, mLightState[0]->mDiffuseB.mV);
    }
}

void LLRender::syncMatrices()
{
	stop_glerror();

	static const U32 name[] = 
	{
		LLShaderMgr::MODELVIEW_MATRIX,
		LLShaderMgr::PROJECTION_MATRIX,
		LLShaderMgr::TEXTURE_MATRIX0,
		LLShaderMgr::TEXTURE_MATRIX1,
		LLShaderMgr::TEXTURE_MATRIX2,
		LLShaderMgr::TEXTURE_MATRIX3,
	};

	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;

	static LLMatrix4a cached_mvp;
	static U32 cached_mvp_mdv_hash = 0xFFFFFFFF;
	static U32 cached_mvp_proj_hash = 0xFFFFFFFF;
	
	static LLMatrix4a cached_normal;
	static U32 cached_normal_hash = 0xFFFFFFFF;

	if (shader)
	{
		//llassert(shader);

		bool mvp_done = false;

		U32 i = MM_MODELVIEW;
		if (mMatHash[MM_MODELVIEW] != shader->mMatHash[MM_MODELVIEW])
		{ //update modelview, normal, and MVP
			const LLMatrix4a& mat = mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]];

			shader->uniformMatrix4fv(name[MM_MODELVIEW], 1, GL_FALSE, mat.getF32ptr());
			shader->mMatHash[MM_MODELVIEW] = mMatHash[MM_MODELVIEW];

			//update normal matrix
			S32 loc = shader->getUniformLocation(LLShaderMgr::NORMAL_MATRIX);
			if (loc > -1)
			{
				if (cached_normal_hash != mMatHash[i])
				{
					cached_normal = mat;
					cached_normal.invert();
					cached_normal.transpose();
					cached_normal_hash = mMatHash[i];
				}

				const LLMatrix4a& norm = cached_normal;

				LLVector3 norms[3];
				norms[0].set(norm.getRow<0>().getF32ptr());
				norms[1].set(norm.getRow<1>().getF32ptr());
				norms[2].set(norm.getRow<2>().getF32ptr());

				shader->uniformMatrix3fv(LLShaderMgr::NORMAL_MATRIX, 1, GL_FALSE, norms[0].mV);
			}

			//update MVP matrix
			mvp_done = true;
			loc = shader->getUniformLocation(LLShaderMgr::MODELVIEW_PROJECTION_MATRIX);
			if (loc > -1)
			{
				if (cached_mvp_mdv_hash != mMatHash[i] || cached_mvp_proj_hash != mMatHash[MM_PROJECTION])
				{
					cached_mvp.setMul(mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]], mat);
					cached_mvp_mdv_hash = mMatHash[i];
					cached_mvp_proj_hash = mMatHash[MM_PROJECTION];
				}

				shader->uniformMatrix4fv(LLShaderMgr::MODELVIEW_PROJECTION_MATRIX, 1, GL_FALSE, cached_mvp.getF32ptr());
			}
		}

		i = MM_PROJECTION;
		if (mMatHash[MM_PROJECTION] != shader->mMatHash[MM_PROJECTION])
		{ //update projection matrix, normal, and MVP
			const LLMatrix4a& mat = mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]];

            // it would be nice to have this automatically track the state of the proj matrix
            // but certain render paths (deferred lighting) require it to be mismatched *sigh*
            //if (shader->getUniformLocation(LLShaderMgr::INVERSE_PROJECTION_MATRIX))
            //{
	        //    LLMatrix4a inv_proj = mat
			//    mat.invert();
	        //    shader->uniformMatrix4fv(LLShaderMgr::INVERSE_PROJECTION_MATRIX, 1, FALSE, inv_proj.getF32ptr());
            //}

			shader->uniformMatrix4fv(name[MM_PROJECTION], 1, GL_FALSE, mat.getF32ptr());
			shader->mMatHash[MM_PROJECTION] = mMatHash[MM_PROJECTION];

			if (!mvp_done)
			{
				//update MVP matrix
				S32 loc = shader->getUniformLocation(LLShaderMgr::MODELVIEW_PROJECTION_MATRIX);
				if (loc > -1)
				{
					if (cached_mvp_mdv_hash != mMatHash[MM_PROJECTION] || cached_mvp_proj_hash != mMatHash[MM_PROJECTION])
					{
						cached_mvp.setMul(mat, mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]]);
						cached_mvp_mdv_hash = mMatHash[MM_MODELVIEW];
						cached_mvp_proj_hash = mMatHash[MM_PROJECTION];
					}
									
					shader->uniformMatrix4fv(LLShaderMgr::MODELVIEW_PROJECTION_MATRIX, 1, GL_FALSE, cached_mvp.getF32ptr());
				}
			}
		}

		for (i = MM_TEXTURE0; i < NUM_MATRIX_MODES; ++i)
		{
			if (mMatHash[i] != shader->mMatHash[i])
			{
				shader->uniformMatrix4fv(name[i], 1, GL_FALSE, mMatrix[i][mMatIdx[i]].getF32ptr());
				shader->mMatHash[i] = mMatHash[i];
			}
		}


		if (shader->mFeatures.hasLighting || shader->mFeatures.calculatesLighting || shader->mFeatures.calculatesAtmospherics)
		{ //also sync light state
			syncLightState();
		}
	}
	else if (!LLGLSLShader::sNoFixedFunction)
	{
		static const GLenum mode[] = 
		{
			GL_MODELVIEW,
			GL_PROJECTION,
			GL_TEXTURE,
			GL_TEXTURE,
			GL_TEXTURE,
			GL_TEXTURE,
		};

		for (U32 i = 0; i < MM_TEXTURE0; ++i)
		{
			if (mMatHash[i] != mCurMatHash[i])
			{
				glMatrixMode(mode[i]);
				glLoadMatrixf(mMatrix[i][mMatIdx[i]].getF32ptr());
				mCurMatHash[i] = mMatHash[i];
			}
		}

		for (U32 i = MM_TEXTURE0; i < NUM_MATRIX_MODES; ++i)
		{
			if (mMatHash[i] != mCurMatHash[i])
			{
				gGL.getTexUnit(i-MM_TEXTURE0)->activate();
				glMatrixMode(mode[i]);
				glLoadMatrixf(mMatrix[i][mMatIdx[i]].getF32ptr());
				mCurMatHash[i] = mMatHash[i];
			}
		}
	}

	stop_glerror();
}

void LLRender::translatef(const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
	if(	llabs(x) < F_APPROXIMATELY_ZERO &&
		llabs(y) < F_APPROXIMATELY_ZERO &&
		llabs(z) < F_APPROXIMATELY_ZERO)
	{
		return;
	}

	flush();

	mMatrix[mMatrixMode][mMatIdx[mMatrixMode]].applyTranslation_affine(x,y,z);
	mMatHash[mMatrixMode]++;

}

void LLRender::scalef(const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
	if(	(llabs(x-1.f)) < F_APPROXIMATELY_ZERO &&
		(llabs(y-1.f)) < F_APPROXIMATELY_ZERO &&
		(llabs(z-1.f)) < F_APPROXIMATELY_ZERO)
	{
		return;
	}
	flush();
	
	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]].applyScale_affine(x,y,z);
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::ortho(F32 left, F32 right, F32 bottom, F32 top, F32 zNear, F32 zFar)
{
	flush();

	{
		LLMatrix4a ortho_mat;
		ortho_mat.setRow<0>(LLVector4a(2.f/(right-left),0,0));
		ortho_mat.setRow<1>(LLVector4a(0,2.f/(top-bottom),0));
		ortho_mat.setRow<2>(LLVector4a(0,0,-2.f/(zFar-zNear)));
		ortho_mat.setRow<3>(LLVector4a(-(right+left)/(right-left),-(top+bottom)/(top-bottom),-(zFar+zNear)/(zFar-zNear),1));	

		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]].mul_affine(ortho_mat);
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::rotatef(const LLMatrix4a& rot)
{
	flush();

	mMatrix[mMatrixMode][mMatIdx[mMatrixMode]].mul_affine(rot);
	mMatHash[mMatrixMode]++;
}

void LLRender::rotatef(const GLfloat& a, const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
	if(	llabs(a) < F_APPROXIMATELY_ZERO ||
		llabs(a-360.f) < F_APPROXIMATELY_ZERO)
	{
		return;
	}
	
	flush();

	rotatef(ALGLMath::genRot(a,x,y,z));
}

void LLRender::pushMatrix()
{
	flush();
	
	{
		if (mMatIdx[mMatrixMode] < LL_MATRIX_STACK_DEPTH-1)
		{
			mMatrix[mMatrixMode][mMatIdx[mMatrixMode]+1] = mMatrix[mMatrixMode][mMatIdx[mMatrixMode]];
			++mMatIdx[mMatrixMode];
		}
		else
		{
			LL_WARNS() << "Matrix stack overflow." << LL_ENDL;
		}
	}
}

void LLRender::popMatrix()
{
	{
		if (mMatIdx[mMatrixMode] > 0)
		{
			if ( memcmp(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]].getF32ptr(), mMatrix[mMatrixMode][mMatIdx[mMatrixMode] - 1].getF32ptr(), sizeof(LLMatrix4a)) )
			{
				flush();
			}
			--mMatIdx[mMatrixMode];
			mMatHash[mMatrixMode]++;
		}
		else
		{
			flush();
			LL_WARNS() << "Matrix stack underflow." << LL_ENDL;
		}
	}
}

void LLRender::loadMatrix(const LLMatrix4a& mat)
{
	flush();
	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = mat;
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::loadMatrix(const F32* mat)
{
	flush();
	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]].loadu(mat);
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::multMatrix(const LLMatrix4a& mat)
{
	flush();
	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]].mul_affine(mat); 
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::matrixMode(eMatrixMode mode)
{
	if (mode == MM_TEXTURE)
	{
        U32 tex_index = gGL.getCurrentTexUnitIndex();
        // the shaders don't actually reference anything beyond texture_matrix0/1 outside of terrain rendering
        llassert(tex_index <= 3);
        mode = eMatrixMode(MM_TEXTURE0 + tex_index);
        if (mode > MM_TEXTURE3)
        {
            // getCurrentTexUnitIndex() can go as high as 32 (LL_NUM_TEXTURE_LAYERS)
            // Large value will result in a crash at mMatrix
            LL_WARNS_ONCE() << "Attempted to assign matrix mode out of bounds: " << mode << LL_ENDL;
            mode = MM_TEXTURE0;
        }
	}

	mMatrixMode = mode;
}

LLRender::eMatrixMode LLRender::getMatrixMode()
{
	if (mMatrixMode >= MM_TEXTURE0 && mMatrixMode <= MM_TEXTURE3)
	{ //always return MM_TEXTURE if current matrix mode points at any texture matrix
		return MM_TEXTURE;
	}

	return mMatrixMode;
}


void LLRender::loadIdentity()
{
	flush();

	{
		llassert_always(mMatrixMode < NUM_MATRIX_MODES) ;

		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]].setIdentity();
		mMatHash[mMatrixMode]++;
	}
}

const LLMatrix4a& LLRender::getModelviewMatrix()
{
	return mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]];
}

const LLMatrix4a& LLRender::getProjectionMatrix()
{
	return mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]];
}

void LLRender::translateUI(F32 x, F32 y, F32 z)
{
	if (mUIOffset.empty())
	{
		LL_ERRS() << "Need to push a UI translation frame before offsetting" << LL_ENDL;
	}

	LLVector4a add(x,y,z);
	mUIOffset.back().add(add);
}

void LLRender::scaleUI(F32 x, F32 y, F32 z)
{
	if (mUIScale.empty())
	{
		LL_ERRS() << "Need to push a UI transformation frame before scaling." << LL_ENDL;
	}

	LLVector4a scale(x,y,z);
	mUIScale.back().mul(scale);
}

void LLRender::rotateUI(LLQuaternion& rot)
{
	if (mUIRotation.empty())
	{
		mUIRotation.push_back(rot);
	}
	else
	{
		mUIRotation.push_back(mUIRotation.back()*rot);
	}
}

void LLRender::pushUIMatrix()
{
	if (mUIOffset.empty())
	{
		mUIOffset.emplace_back(LLVector4a::getZero());
	}
	else
	{
		mUIOffset.push_back(mUIOffset.back());
	}
	
	if (mUIScale.empty())
	{
		mUIScale.emplace_back(LLVector4a(1.f));
	}
	else
	{
		mUIScale.push_back(mUIScale.back());
	}
	if (!mUIRotation.empty())
	{
		mUIRotation.push_back(mUIRotation.back());
	}
}

void LLRender::popUIMatrix()
{
	if (mUIOffset.empty() || mUIScale.empty())
	{
		LL_ERRS() << "UI offset or scale stack blown." << LL_ENDL;
	}

	mUIOffset.pop_back();
	mUIScale.pop_back();
	if (!mUIRotation.empty())
	{
		mUIRotation.pop_back();
	}
}

LLVector3 LLRender::getUITranslation()
{
	if (mUIOffset.empty())
	{
		return LLVector3(0,0,0);
	}
	return LLVector3(mUIOffset.back().getF32ptr());
}

LLVector3 LLRender::getUIScale()
{
	if (mUIScale.empty())
	{
		return LLVector3(1,1,1);
	}
	return LLVector3(mUIScale.back().getF32ptr());
}


void LLRender::loadUIIdentity()
{
	if (mUIOffset.empty() || mUIScale.empty())
	{
		LL_ERRS() << "Need to push UI translation frame before clearing offset." << LL_ENDL;
	}
	mUIOffset.back().splat(0.f);
	mUIScale.back().splat(1.f);
	if (!mUIRotation.empty())
		mUIRotation.push_back(LLQuaternion());
}

void LLRender::setColorMask(bool writeColor, bool writeAlpha)
{
	setColorMask(writeColor, writeColor, writeColor, writeAlpha);
}

void LLRender::setColorMask(bool writeColorR, bool writeColorG, bool writeColorB, bool writeAlpha)
{
	flush();

	if (mCurrColorMask[0] != writeColorR ||
		mCurrColorMask[1] != writeColorG ||
		mCurrColorMask[2] != writeColorB ||
		mCurrColorMask[3] != writeAlpha)
	{
		mCurrColorMask[0] = writeColorR;
		mCurrColorMask[1] = writeColorG;
		mCurrColorMask[2] = writeColorB;
		mCurrColorMask[3] = writeAlpha;

		glColorMask(writeColorR ? GL_TRUE : GL_FALSE, 
					writeColorG ? GL_TRUE : GL_FALSE,
					writeColorB ? GL_TRUE : GL_FALSE,
					writeAlpha ? GL_TRUE : GL_FALSE);
	}
}

void LLRender::setSceneBlendType(eBlendType type)
{
	switch (type) 
	{
		case BT_ALPHA:
			blendFunc(BF_SOURCE_ALPHA, BF_ONE_MINUS_SOURCE_ALPHA);
			break;
		case BT_ADD:
			blendFunc(BF_ONE, BF_ONE);
			break;
		case BT_ADD_WITH_ALPHA:
			blendFunc(BF_SOURCE_ALPHA, BF_ONE);
			break;
		case BT_MULT:
			blendFunc(BF_DEST_COLOR, BF_ZERO);
			break;
		case BT_MULT_ALPHA:
			blendFunc(BF_DEST_ALPHA, BF_ZERO);
			break;
		case BT_MULT_X2:
			blendFunc(BF_DEST_COLOR, BF_SOURCE_COLOR);
			break;
		case BT_REPLACE:
			blendFunc(BF_ONE, BF_ZERO);
			break;
		default:
			LL_ERRS() << "Unknown Scene Blend Type: " << type << LL_ENDL;
			break;
	}
}

void LLRender::setAlphaRejectSettings(eCompareFunc func, F32 value)
{
	flush();

	if (LLGLSLShader::sNoFixedFunction)
	{ //glAlphaFunc is deprecated in OpenGL 3.3
		return;
	}

	if (mCurrAlphaFunc != func ||
		mCurrAlphaFuncVal != value)
	{
		mCurrAlphaFunc = func;
		mCurrAlphaFuncVal = value;
		if (func == CF_DEFAULT)
		{
			glAlphaFunc(GL_GREATER, 0.01f);
		} 
		else
		{
			glAlphaFunc(sGLCompareFunc[func], value);
		}
	}

	if (gDebugGL)
	{ //make sure cached state is correct
		GLint cur_func = 0;
		glGetIntegerv(GL_ALPHA_TEST_FUNC, &cur_func);

		if (func == CF_DEFAULT)
		{
			func = CF_GREATER;
		}

		if (cur_func != sGLCompareFunc[func])
		{
			LL_ERRS() << "Alpha test function corrupted!" << LL_ENDL;
		}

		F32 ref = 0.f;
		glGetFloatv(GL_ALPHA_TEST_REF, &ref);

		if (ref != value)
		{
			LL_ERRS() << "Alpha test value corrupted!" << LL_ENDL;
		}
	}
}

void LLRender::blendFunc(eBlendFactor sfactor, eBlendFactor dfactor)
{
	llassert(sfactor < BF_UNDEF);
	llassert(dfactor < BF_UNDEF);
	if (mCurrBlendColorSFactor != sfactor || mCurrBlendColorDFactor != dfactor ||
	    mCurrBlendAlphaSFactor != sfactor || mCurrBlendAlphaDFactor != dfactor)
	{
		mCurrBlendColorSFactor = sfactor;
		mCurrBlendAlphaSFactor = sfactor;
		mCurrBlendColorDFactor = dfactor;
		mCurrBlendAlphaDFactor = dfactor;
		flush();
		glBlendFunc(sGLBlendFactor[sfactor], sGLBlendFactor[dfactor]);
	}
}

void LLRender::blendFunc(eBlendFactor color_sfactor, eBlendFactor color_dfactor,
			 eBlendFactor alpha_sfactor, eBlendFactor alpha_dfactor)
{
	llassert(color_sfactor < BF_UNDEF);
	llassert(color_dfactor < BF_UNDEF);
	llassert(alpha_sfactor < BF_UNDEF);
	llassert(alpha_dfactor < BF_UNDEF);
	if (!gGLManager.mHasBlendFuncSeparate)
	{
		LL_WARNS_ONCE("render") << "no glBlendFuncSeparate(), using color-only blend func" << LL_ENDL;
		blendFunc(color_sfactor, color_dfactor);
		return;
	}
	if (mCurrBlendColorSFactor != color_sfactor || mCurrBlendColorDFactor != color_dfactor ||
	    mCurrBlendAlphaSFactor != alpha_sfactor || mCurrBlendAlphaDFactor != alpha_dfactor)
	{
		mCurrBlendColorSFactor = color_sfactor;
		mCurrBlendAlphaSFactor = alpha_sfactor;
		mCurrBlendColorDFactor = color_dfactor;
		mCurrBlendAlphaDFactor = alpha_dfactor;
		flush();
		glBlendFuncSeparate(sGLBlendFactor[color_sfactor], sGLBlendFactor[color_dfactor],
				       sGLBlendFactor[alpha_sfactor], sGLBlendFactor[alpha_dfactor]);
	}
}

LLTexUnit* LLRender::getTexUnit(U32 index)
{
	if (index < mTexUnits.size())
	{
		return mTexUnits[index];
	}
	else 
	{
#ifdef SHOW_DEBUG
		LL_DEBUGS() << "Non-existing texture unit layer requested: " << index << LL_ENDL;
#endif
		return mDummyTexUnit;
	}
}

LLLightState* LLRender::getLight(U32 index)
{
	if (index < mLightState.size())
	{
		return mLightState[index];
	}
	
	return NULL;
}

void LLRender::setAmbientLightColor(const LLColor4& color)
{
	if (color != mAmbientLightColor)
	{
		++mLightHash;
		mAmbientLightColor = color;
		if (!LLGLSLShader::sNoFixedFunction)
		{
			glLightModelfv(GL_LIGHT_MODEL_AMBIENT, color.mV);
		}
	}
}
void LLRender::setLineWidth(F32 line_width)
{
	if (LLRender::sGLCoreProfile)
	{
		line_width = 1.f;
	}
	if (mLineWidth != line_width || mDirty)
	{
		if (mMode == LLRender::LINES || mMode == LLRender::LINE_STRIP)
		{
			flush();
		}
		mLineWidth = line_width;
		glLineWidth(line_width);
	}
}

bool LLRender::verifyTexUnitActive(U32 unitToVerify)
{
	if (mCurrTextureUnitIndex == unitToVerify)
	{
		return true;
	}
	else 
	{
		LL_WARNS() << "TexUnit currently active: " << mCurrTextureUnitIndex << " (expecting " << unitToVerify << ")" << LL_ENDL;
		return false;
	}
}

void LLRender::clearErrors()
{
	while (glGetError())
	{
		//loop until no more error flags left
	}
}

void LLRender::begin(const GLuint& mode)
{
	if (mode != mMode)
	{
		if (mMode == LLRender::LINES ||
			mMode == LLRender::TRIANGLES ||
			mMode == LLRender::POINTS ||
			mMode == LLRender::TRIANGLE_STRIP )
		{
			flush();
		}
		else if (mCount != 0)
		{
			LL_ERRS() << "gGL.begin() called redundantly." << LL_ENDL;
		}
		
		mMode = mode;
	}
}

void LLRender::end()
{ 
	if (mCount == 0)
	{
		return;
		//IMM_ERRS << "GL begin and end called with no vertices specified." << LL_ENDL;
	}

	if ((mMode != LLRender::LINES &&
		mMode != LLRender::TRIANGLES &&
		mMode != LLRender::POINTS &&
		mMode != LLRender::TRIANGLE_STRIP) ||
		mCount > 2048)
	{
		flush();
	}
	else if (mMode == LLRender::TRIANGLE_STRIP)
	{
		mPrimitiveReset = true;
	}
}

void LLRender::flush()
{
	if (mCount > 0)
	{
		if (!mUIOffset.empty())
		{
			sUICalls++;
			sUIVerts += mCount;
		}
		
		//store mCount in a local variable to avoid re-entrance (drawArrays may call flush)
		U32 count = mCount;

			if (mMode == LLRender::TRIANGLES)
			{
				if (mCount%3 != 0)
				{
				count -= (mCount % 3);
				LL_WARNS() << "Incomplete triangle requested." << LL_ENDL;
				}
			}
			
			if (mMode == LLRender::LINES)
			{
				if (mCount%2 != 0)
				{
				count -= (mCount % 2);
				LL_WARNS() << "Incomplete line requested." << LL_ENDL;
			}
		}

		mCount = 0;

		if (mBuffer->useVBOs() && !mBuffer->isLocked())
		{ //hack to only flush the part of the buffer that was updated (relies on stream draw using buffersubdata)
			mBuffer->getVertexStrider(mVerticesp, 0, count);
			mBuffer->getTexCoord0Strider(mTexcoordsp, 0, count);
			mBuffer->getColorStrider(mColorsp, 0, count);
		}
		
		mBuffer->flush();
		mBuffer->setBuffer(immediate_mask);
		mBuffer->drawArrays(mMode, 0, count);
		
		mVerticesp[0] = mVerticesp[count];
		mTexcoordsp[0] = mTexcoordsp[count];
		mColorsp[0] = mColorsp[count];
		
		mCount = 0;
		mPrimitiveReset = false;
	}
}

void LLRender::vertex4a(const LLVector4a& vertex)
{ 
	//the range of mVerticesp, mColorsp and mTexcoordsp is [0, 4095]
	if (mCount > 2048)
	{ //break when buffer gets reasonably full to keep GL command buffers happy and avoid overflow below
		switch (mMode)
		{
			case LLRender::POINTS: flush(); break;
			case LLRender::TRIANGLES: if (mCount%3==0) flush(); break;
			case LLRender::LINES: if (mCount%2 == 0) flush(); break;
			case LLRender::TRIANGLE_STRIP:
			{
				LLVector4a vert[] = { mVerticesp[mCount - 2], mVerticesp[mCount - 1], mVerticesp[mCount] };
				LLColor4U col[] = { mColorsp[mCount - 2], mColorsp[mCount - 1], mColorsp[mCount] };
				LLVector2 tc[] = { mTexcoordsp[mCount - 2], mTexcoordsp[mCount - 1], mTexcoordsp[mCount] };
				flush();
				for (int i = 0; i < LL_ARRAY_SIZE(vert); ++i)
				{
					mVerticesp[i] = vert[i];
					mColorsp[i] = col[i];
					mTexcoordsp[i] = tc[i];
				}
				mCount = 2;
				break;
			}
		}
	}
			
	if (mCount > 4094)
	{
	//	LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
		return;
	}

	if (mPrimitiveReset && mCount)
	{
		// Insert degenerate
		++mCount;
		mVerticesp[mCount] = mVerticesp[mCount - 1];
		mColorsp[mCount] = mColorsp[mCount - 1];
		mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
		mVerticesp[mCount - 1] = mVerticesp[mCount - 2];
		mColorsp[mCount - 1] = mColorsp[mCount - 2];
		mTexcoordsp[mCount - 1] = mTexcoordsp[mCount - 2];
	}

	if (mUIOffset.empty())
	{
		if (!mUIRotation.empty() && mUIRotation.back().isNotIdentity())
		{
			LLVector4 vert(vertex.getF32ptr());
			mVerticesp[mCount].loadua((vert*mUIRotation.back()).mV);
		}
		else
		{
		mVerticesp[mCount] = vertex;
		}
	}
	else
	{
		if (!mUIRotation.empty() && mUIRotation.back().isNotIdentity())
		{
			LLVector4 vert(vertex.getF32ptr());
			vert = vert * mUIRotation.back();
			LLVector4a postrot_vert;
			postrot_vert.loadua(vert.mV);
			mVerticesp[mCount].setAdd(postrot_vert, mUIOffset.back());
			mVerticesp[mCount].mul(mUIScale.back());
		}
		else
		{
			mVerticesp[mCount].setAdd(vertex, mUIOffset.back());
			mVerticesp[mCount].mul(mUIScale.back());
		}
	}

	mCount++;
	mVerticesp[mCount] = mVerticesp[mCount-1];
	mColorsp[mCount] = mColorsp[mCount-1];
	mTexcoordsp[mCount] = mTexcoordsp[mCount-1];	

	if (mPrimitiveReset && mCount)
	{
		mCount++;
		mVerticesp[mCount] = mVerticesp[mCount - 1];
		mColorsp[mCount] = mColorsp[mCount - 1];
		mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
	}

	mPrimitiveReset = false;
}

void LLRender::vertexBatchPreTransformed(LLVector4a* verts, S32 vert_count)
{
	if (mCount + vert_count > 4094)
	{
		//	LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
		return;
	}

	{
		if (mPrimitiveReset && mCount)
		{
			// Insert degenerate
			++mCount;
			mVerticesp[mCount] = verts[0];
			mColorsp[mCount] = mColorsp[mCount - 1];
			mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
			mVerticesp[mCount - 1] = mVerticesp[mCount - 2];
			mColorsp[mCount - 1] = mColorsp[mCount - 2];
			mTexcoordsp[mCount - 1] = mTexcoordsp[mCount - 2];
			++mCount;
			mColorsp[mCount] = mColorsp[mCount - 1];
			mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
		}
	
		mVerticesp.copyArray(mCount, verts, vert_count);
		for (S32 i = 0; i < vert_count; i++)
		{

			mCount++;
			mTexcoordsp[mCount] = mTexcoordsp[mCount-1];
			mColorsp[mCount] = mColorsp[mCount-1];
		}
	}

	if( mCount > 0 ) // ND: Guard against crashes if mCount is zero, yes it can happen
		mVerticesp[mCount] = mVerticesp[mCount-1];
	mPrimitiveReset = false;
}

void LLRender::vertexBatchPreTransformed(LLVector4a* verts, LLVector2* uvs, S32 vert_count)
{
	if (mCount + vert_count > 4094)
	{
		//	LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
		return;
	}

	{
		if (mPrimitiveReset && mCount)
		{
			// Insert degenerate
			++mCount;
			mVerticesp[mCount] = verts[0];
			mColorsp[mCount] = mColorsp[mCount - 1];
			mTexcoordsp[mCount] = uvs[0];
			mVerticesp[mCount - 1] = mVerticesp[mCount - 2];
			mColorsp[mCount - 1] = mColorsp[mCount - 2];
			mTexcoordsp[mCount - 1] = mTexcoordsp[mCount - 2];
			++mCount;
			mColorsp[mCount] = mColorsp[mCount - 1];
			mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
		}
	
		mVerticesp.copyArray(mCount, verts, vert_count);
		mTexcoordsp.copyArray(mCount, uvs, vert_count);
	
		for (S32 i = 0; i < vert_count; i++)
		{
			mCount++;
			mColorsp[mCount] = mColorsp[mCount-1];
		}
	}

	if (mCount > 0)
	{
		mVerticesp[mCount] = mVerticesp[mCount - 1];
		mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
	}

	mPrimitiveReset = false;
}

void LLRender::vertexBatchPreTransformed(LLVector4a* verts, LLVector2* uvs, LLColor4U* colors, S32 vert_count)
{
	if (mCount + vert_count > 4094)
	{
		//	LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
		return;
	}

	{
		if (mPrimitiveReset && mCount)
		{
			// Insert degenerate
			++mCount;
			mVerticesp[mCount] = verts[0];
			mColorsp[mCount] = colors[mCount - 1];
			mTexcoordsp[mCount] = uvs[0];
			mVerticesp[mCount - 1] = mVerticesp[mCount - 2];
			mColorsp[mCount - 1] = mColorsp[mCount - 2];
			mTexcoordsp[mCount - 1] = mTexcoordsp[mCount - 2];
			++mCount;
			mColorsp[mCount] = mColorsp[mCount - 1];
			mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
		}
	
		// Note: Batch copies instead of iterating.
		mVerticesp.copyArray(mCount, verts, vert_count);
		mTexcoordsp.copyArray(mCount, uvs, vert_count);
		mColorsp.copyArray(mCount, colors, vert_count);
		mCount += vert_count;
	}

	if (mCount > 0)
	{
		mVerticesp[mCount] = mVerticesp[mCount - 1];
		mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
		mColorsp[mCount] = mColorsp[mCount - 1];
	}

	mPrimitiveReset = false;
}

void LLRender::texCoord2f(const GLfloat& x, const GLfloat& y)
{ 
	mTexcoordsp[mCount] = LLVector2(x,y);
}

void LLRender::texCoord2i(const GLint& x, const GLint& y)
{ 
	texCoord2f((GLfloat) x, (GLfloat) y);
}

void LLRender::texCoord2fv(const GLfloat* tc)
{ 
	texCoord2f(tc[0], tc[1]);
}

void LLRender::color4ub(const GLubyte& r, const GLubyte& g, const GLubyte& b, const GLubyte& a)
{
	if (!LLGLSLShader::sCurBoundShaderPtr ||
		LLGLSLShader::sCurBoundShaderPtr->mAttributeMask & LLVertexBuffer::MAP_COLOR)
	{
		mColorsp[mCount] = LLColor4U(r,g,b,a);
	}
	else
	{ //not using shaders or shader reads color from a uniform
		diffuseColor4ub(r,g,b,a);
	}
}
void LLRender::color4ubv(const GLubyte* c)
{
	color4ub(c[0], c[1], c[2], c[3]);
}

void LLRender::color4f(const GLfloat& r, const GLfloat& g, const GLfloat& b, const GLfloat& a)
{
	color4ub((GLubyte) (llclamp(r, 0.f, 1.f)*255),
		(GLubyte) (llclamp(g, 0.f, 1.f)*255),
		(GLubyte) (llclamp(b, 0.f, 1.f)*255),
		(GLubyte) (llclamp(a, 0.f, 1.f)*255));
}

void LLRender::color4fv(const GLfloat* c)
{ 
	color4f(c[0],c[1],c[2],c[3]);
}

void LLRender::color3f(const GLfloat& r, const GLfloat& g, const GLfloat& b)
{ 
	color4f(r,g,b,1);
}

void LLRender::color3fv(const GLfloat* c)
{ 
	color4f(c[0],c[1],c[2],1);
}

void LLRender::diffuseColor3f(F32 r, F32 g, F32 b)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(!LLGLSLShader::sNoFixedFunction || shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r,g,b,1.f);
	}
	else
	{
		glColor3f(r,g,b);
	}
}

void LLRender::diffuseColor3fv(const F32* c)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(!LLGLSLShader::sNoFixedFunction || shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, c[0], c[1], c[2], 1.f);
	}
	else
	{
		glColor3fv(c);
	}
}

void LLRender::diffuseColor4f(F32 r, F32 g, F32 b, F32 a)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(!LLGLSLShader::sNoFixedFunction || shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r,g,b,a);
	}
	else
	{
		glColor4f(r,g,b,a);
	}
}

void LLRender::diffuseColor4fv(const F32* c)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(!LLGLSLShader::sNoFixedFunction || shader != NULL);

	if (shader)
	{
		shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, c);
	}
	else
	{
		glColor4fv(c);
	}
}

void LLRender::diffuseColor4ubv(const U8* c)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(!LLGLSLShader::sNoFixedFunction || shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, c[0]/255.f, c[1]/255.f, c[2]/255.f, c[3]/255.f);
	}
	else
	{
		glColor4ubv(c);
	}
}

void LLRender::diffuseColor4ub(U8 r, U8 g, U8 b, U8 a)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(!LLGLSLShader::sNoFixedFunction || shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r/255.f, g/255.f, b/255.f, a/255.f);
	}
	else
	{
		glColor4ub(r,g,b,a);
	}
}


void LLRender::debugTexUnits(void)
{
	LL_INFOS("TextureUnit") << "Active TexUnit: " << mCurrTextureUnitIndex << LL_ENDL;
	std::string active_enabled = "false";
	for (U32 i = 0; i < mTexUnits.size(); i++)
	{
		if (getTexUnit(i)->mCurrTexType != LLTexUnit::TT_NONE)
		{
			if (i == mCurrTextureUnitIndex) active_enabled = "true";
			LL_INFOS("TextureUnit") << "TexUnit: " << i << " Enabled" << LL_ENDL;
			LL_INFOS("TextureUnit") << "Enabled As: " ;
			switch (getTexUnit(i)->mCurrTexType)
			{
				case LLTexUnit::TT_TEXTURE:
					LL_CONT << "Texture 2D";
					break;
				case LLTexUnit::TT_RECT_TEXTURE:
					LL_CONT << "Texture Rectangle";
					break;
				case LLTexUnit::TT_CUBE_MAP:
					LL_CONT << "Cube Map";
					break;
				case LLTexUnit::TT_TEXTURE_3D:
					LL_CONT << "Texture 3D";
					break;
				default:
					LL_CONT << "ARGH!!! NONE!";
					break;
			}
			LL_CONT << ", Texture Bound: " << getTexUnit(i)->mCurrTexture << LL_ENDL;
		}
	}
	LL_INFOS("TextureUnit") << "Active TexUnit Enabled : " << active_enabled << LL_ENDL;
}

const LLMatrix4a& get_current_modelview()
{
	return gGLModelView;
}

const LLMatrix4a& get_current_projection()
{
	return gGLProjection;
}

const LLMatrix4a& get_last_modelview()
{
	return gGLLastModelView;
}

const LLMatrix4a& get_last_projection()
{
	return gGLLastProjection;
}

void set_current_modelview(const LLMatrix4a& mat)
{
	gGLModelView = mat;
}

void set_current_projection(const LLMatrix4a& mat)
{
	gGLProjection = mat;
}

void set_last_modelview(const LLMatrix4a& mat)
{
	gGLLastModelView = mat;
}

void set_last_projection(const LLMatrix4a& mat)
{
	gGLLastProjection = mat;
}
