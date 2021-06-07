/** 
 * @file llgl.cpp
 * @brief LLGL implementation
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

// This file sets some global GL parameters, and implements some 
// useful functions for GL operations.

#include "linden_common.h"

#include "boost/tokenizer.hpp"

#include "llsys.h"

#include "llgl.h"
#include "llglstates.h"
#include "llrender.h"

#include "llerror.h"
#include "llerrorcontrol.h"
#include "llquaternion.h"
#include "llmath.h"
#include "m4math.h"
#include "llstring.h"
#include "llstacktrace.h"

#include "llglheaders.h"
#include "llglslshader.h"

#if LL_WINDOWS
#include "lldxhardware.h"
#endif

BOOL gDebugSession = FALSE;
BOOL gHeadlessClient = FALSE;
BOOL gGLActive = FALSE;
BOOL gGLDebugLoggingEnabled = TRUE;

static const std::string HEADLESS_VENDOR_STRING("Linden Lab");
static const std::string HEADLESS_RENDERER_STRING("Headless");
static const std::string HEADLESS_VERSION_STRING("1.0");

llofstream gFailLog;

#ifndef APIENTRY
#define APIENTRY
#endif

void APIENTRY gl_debug_callback(GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar* message,
	GLvoid* userParam)
{
	if (gGLDebugLoggingEnabled)
	{
		if (severity == GL_DEBUG_SEVERITY_HIGH)
		{
			LL_WARNS() << "----- GL ERROR --------" << LL_ENDL;
		}
		else
		{
			LL_WARNS() << "----- GL WARNING -------" << LL_ENDL;
		}
		LL_WARNS() << "Type: " << std::hex << type << LL_ENDL;
		LL_WARNS() << "ID: " << std::hex << id << LL_ENDL;
		LL_WARNS() << "Severity: " << std::hex << severity << LL_ENDL;
		LL_WARNS() << "Message: " << message << LL_ENDL;
		LL_WARNS() << "-----------------------" << LL_ENDL;
		if (severity == GL_DEBUG_SEVERITY_HIGH)
		{
			LL_ERRS() << "Halting on GL Error" << LL_ENDL;
		}
	}
}

void parse_glsl_version(S32& major, S32& minor);

void ll_init_fail_log(std::string filename)
{
	gFailLog.open(filename.c_str());
}


void ll_fail(std::string msg)
{
	
	if (gDebugSession)
	{
		std::vector<std::string> lines;

		gFailLog << LLError::utcTime() << " " << msg << std::endl;

		gFailLog << "Stack Trace:" << std::endl;

		ll_get_stack_trace(lines);
		
		for(size_t i = 0; i < lines.size(); ++i)
		{
			gFailLog << lines[i] << std::endl;
		}

		gFailLog << "End of Stack Trace." << std::endl << std::endl;

		gFailLog.flush();
	}
};

void ll_close_fail_log()
{
	gFailLog.close();
}

std::list<LLGLUpdate*> LLGLUpdate::sGLQ;

LLGLManager gGLManager;

LLGLManager::LLGLManager() :
	mInited(FALSE),
	mIsDisabled(FALSE),

	mHasMultitexture(FALSE),
	mHasATIMemInfo(FALSE),
	mHasAMDAssociations(FALSE),
	mHasNVXMemInfo(FALSE),
	mNumTextureUnits(1),
	mHasMipMapGeneration(FALSE),
	mHasCompressedTextures(FALSE),
	mHasFramebufferObject(FALSE),
	mMaxSamples(0),
	mHasBlendFuncSeparate(FALSE),
	mHasSync(FALSE),
	mHasVertexBufferObject(FALSE),
	mHasVertexArrayObject(FALSE),
	mHasMapBufferRange(FALSE),
	mHasFlushBufferRange(FALSE),
	mHasPBuffer(FALSE),
	mHasShaderObjects(FALSE),
	mHasVertexShader(FALSE),
	mHasFragmentShader(FALSE),
	mNumTextureImageUnits(0),
	mHasOcclusionQuery(FALSE),
	mHasTimerQuery(FALSE),
	mHasOcclusionQuery2(FALSE),
	mHasPointParameters(FALSE),
	mHasDrawBuffers(FALSE),
	mHasTextureRectangle(FALSE),
	mHasTextureMultisample(FALSE),
	mHasTransformFeedback(FALSE),
	mMaxSampleMaskWords(0),
	mMaxColorTextureSamples(0),
	mMaxDepthTextureSamples(0),
	mMaxIntegerSamples(0),

	mHasAnisotropic(FALSE),
	mHasARBEnvCombine(FALSE),
	mHasCubeMap(FALSE),
	mHasDebugOutput(FALSE),

	mHasTextureSwizzle(false),
	mIsATI(FALSE),
	mIsNVIDIA(FALSE),
	mIsIntel(FALSE),
	mIsGF2or4MX(FALSE),
	mIsGF3(FALSE),
	mIsGFFX(FALSE),
	mATIOffsetVerticalLines(FALSE),
	mATIOldDriver(FALSE),
#if LL_DARWIN
	mIsMobileGF(FALSE),
#endif
	mHasRequirements(TRUE),

	mHasSeparateSpecularColor(FALSE),

	mDriverVersionMajor(1),
	mDriverVersionMinor(0),
	mDriverVersionRelease(0),
	mGLVersion(1.0f),
	mGLSLVersionMajor(0),
	mGLSLVersionMinor(0),		
	mVRAM(0),
	mGLMaxVertexRange(0),
	mGLMaxIndexRange(0)
{
}

//---------------------------------------------------------------------
// Global initialization for GL
//---------------------------------------------------------------------
#if LL_WINDOWS && !LL_MESA_HEADLESS
void LLGLManager::initWGL(HDC dc)
{
	mHasPBuffer = FALSE;

	if (!epoxy_has_wgl_extension(dc, "WGL_ARB_pixel_format"))
	{
		LL_WARNS("RenderInit") << "No ARB pixel format extensions" << LL_ENDL;
	}

	if (epoxy_has_wgl_extension(dc, "WGL_ARB_create_context"))
	{
		//GLH_EXT_NAME(wglCreateContextAttribsARB) = (PFNWGLCREATECONTEXTATTRIBSARBPROC)GLH_EXT_GET_PROC_ADDRESS("wglCreateContextAttribsARB");
	}
	else
	{
		LL_WARNS("RenderInit") << "No ARB create context extensions" << LL_ENDL;
	}

	// For retreiving information per AMD adapter, 
	// because we can't trust curently selected/default one when there are multiple
	mHasAMDAssociations = epoxy_has_wgl_extension(dc, "WGL_AMD_gpu_association");
	if (mHasAMDAssociations)
	{
		//GLH_EXT_NAME(wglGetGPUIDsAMD) = (PFNWGLGETGPUIDSAMDPROC)GLH_EXT_GET_PROC_ADDRESS("wglGetGPUIDsAMD");
		//GLH_EXT_NAME(wglGetGPUInfoAMD) = (PFNWGLGETGPUINFOAMDPROC)GLH_EXT_GET_PROC_ADDRESS("wglGetGPUInfoAMD");
	}

	if (epoxy_has_wgl_extension(dc, "WGL_EXT_swap_control"))
	{
        //GLH_EXT_NAME(wglSwapIntervalEXT) = (PFNWGLSWAPINTERVALEXTPROC)GLH_EXT_GET_PROC_ADDRESS("wglSwapIntervalEXT");
	}

	if( !epoxy_has_wgl_extension(dc, "WGL_ARB_pbuffer") )
	{
		LL_WARNS("RenderInit") << "No ARB WGL PBuffer extensions" << LL_ENDL;
	}

	if( !epoxy_has_wgl_extension(dc, "WGL_ARB_render_texture") )
	{
		LL_WARNS("RenderInit") << "No ARB WGL render texture extensions" << LL_ENDL;
	}

	mHasPBuffer = epoxy_has_wgl_extension(dc, "WGL_ARB_pbuffer") &&
					epoxy_has_wgl_extension(dc, "WGL_ARB_render_texture") &&
					epoxy_has_wgl_extension(dc, "WGL_ARB_pixel_format");
}
#endif

// return false if unable (or unwilling due to old drivers) to init GL
bool LLGLManager::initGL()
{
	if (mInited)
	{
		LL_ERRS("RenderInit") << "Calling init on LLGLManager after already initialized!" << LL_ENDL;
	}
	
	stop_glerror();

	// Extract video card strings and convert to upper case to
	// work around driver-to-driver variation in capitalization.
	mGLVendor = ll_safe_string((const char *)glGetString(GL_VENDOR));
	LLStringUtil::toUpper(mGLVendor);

	mGLRenderer = ll_safe_string((const char *)glGetString(GL_RENDERER));
	LLStringUtil::toUpper(mGLRenderer);

	parse_gl_version( &mDriverVersionMajor, 
		&mDriverVersionMinor, 
		&mDriverVersionRelease, 
		&mDriverVersionVendorString,
		&mGLVersionString);

	mGLVersion = mDriverVersionMajor + mDriverVersionMinor * .1f;

	if (mGLVersion >= 2.f)
	{
		parse_glsl_version(mGLSLVersionMajor, mGLSLVersionMinor);

#if LL_DARWIN
		//never use GLSL greater than 1.20 on OSX
		if (mGLSLVersionMajor > 1 || mGLSLVersionMinor >= 30)
		{
			mGLSLVersionMajor = 1;
			mGLSLVersionMinor = 20;
		}
#endif
	}

	if (mGLVersion >= 2.1f && LLImageGL::sCompressTextures)
	{ //use texture compression
		glHint(GL_TEXTURE_COMPRESSION_HINT, GL_NICEST);
	}
	else
	{ //GL version is < 3.0, always disable texture compression
		LLImageGL::sCompressTextures = false;
	}
	
	// Trailing space necessary to keep "nVidia Corpor_ati_on" cards
	// from being recognized as ATI.
	if (mGLVendor.substr(0,4) == "ATI ")
	{
		mGLVendorShort = "ATI";
		// *TODO: Fix this?
		mIsATI = TRUE;

#if LL_WINDOWS && !LL_MESA_HEADLESS
		if (mDriverVersionRelease < 3842)
		{
			mATIOffsetVerticalLines = TRUE;
		}
#endif // LL_WINDOWS

#if (LL_WINDOWS || LL_LINUX) && !LL_MESA_HEADLESS
		// count any pre OpenGL 3.0 implementation as an old driver
		if (mGLVersion < 3.f) 
		{
			mATIOldDriver = TRUE;
		}
#endif // (LL_WINDOWS || LL_LINUX) && !LL_MESA_HEADLESS
	}
	else if (mGLVendor.find("NVIDIA ") != std::string::npos)
	{
		mGLVendorShort = "NVIDIA";
		mIsNVIDIA = TRUE;
		if (   mGLRenderer.find("GEFORCE4 MX") != std::string::npos
			|| mGLRenderer.find("GEFORCE2") != std::string::npos
			|| mGLRenderer.find("GEFORCE 2") != std::string::npos
			|| mGLRenderer.find("GEFORCE4 460 GO") != std::string::npos
			|| mGLRenderer.find("GEFORCE4 440 GO") != std::string::npos
			|| mGLRenderer.find("GEFORCE4 420 GO") != std::string::npos)
		{
			mIsGF2or4MX = TRUE;
		}
		else if (mGLRenderer.find("GEFORCE FX") != std::string::npos
				 || mGLRenderer.find("QUADRO FX") != std::string::npos
				 || mGLRenderer.find("NV34") != std::string::npos)
		{
			mIsGFFX = TRUE;
		}
		else if(mGLRenderer.find("GEFORCE3") != std::string::npos)
		{
			mIsGF3 = TRUE;
		}
#if LL_DARWIN
		else if ((mGLRenderer.find("9400M") != std::string::npos)
			  || (mGLRenderer.find("9600M") != std::string::npos)
			  || (mGLRenderer.find("9800M") != std::string::npos))
		{
			mIsMobileGF = TRUE;
		}
#endif

	}
	else if (mGLVendor.find("INTEL") != std::string::npos
#if LL_LINUX
		 // The Mesa-based drivers put this in the Renderer string,
		 // not the Vendor string.
		 || mGLRenderer.find("INTEL") != std::string::npos
#endif //LL_LINUX
		 )
	{
		mGLVendorShort = "INTEL";
		mIsIntel = TRUE;
	}
	else
	{
		mGLVendorShort = "MISC";
	}
	
	stop_glerror();
	// This is called here because it depends on the setting of mIsGF2or4MX, and sets up mHasMultitexture.
	initExtensions();
	stop_glerror();

	if (mVRAM == 0)
	{
		S32 old_vram = mVRAM;
	mVRAM = 0;

#if LL_WINDOWS
	if (mHasAMDAssociations)
	{
		GLuint gl_gpus_count = wglGetGPUIDsAMD(0, 0);
		if (gl_gpus_count > 0)
		{
			GLuint* ids = new GLuint[gl_gpus_count];
			wglGetGPUIDsAMD(gl_gpus_count, ids);

			GLuint mem_mb = 0;
			for (U32 i = 0; i < gl_gpus_count; i++)
			{
				wglGetGPUInfoAMD(ids[i],
					WGL_GPU_RAM_AMD,
					GL_UNSIGNED_INT,
					sizeof(GLuint),
					&mem_mb);
				if (mVRAM < mem_mb)
				{
					// basically pick the best AMD and trust driver/OS to know to switch
					mVRAM = mem_mb;
				}
			}
		}
		if (mVRAM != 0)
		{
			LL_INFOS("RenderInit") << "VRAM Detected (AMDAssociations):" << mVRAM << LL_ENDL;
		}
	}
#endif

	if (mHasATIMemInfo && mVRAM == 0)
	{ //ask the gl how much vram is free at startup and attempt to use no more than half of that
		S32 meminfo[4];
		glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, meminfo);

		mVRAM = meminfo[0] / 1024;
		LL_INFOS("RenderInit") << "VRAM Detected (ATIMemInfo):" << mVRAM << LL_ENDL;
	}

	if (mHasNVXMemInfo && mVRAM == 0)
	{
		S32 dedicated_memory;
		glGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &dedicated_memory);
		mVRAM = dedicated_memory/1024;
		LL_INFOS("RenderInit") << "VRAM Detected (NVXMemInfo):" << mVRAM << LL_ENDL;
	}

#if LL_WINDOWS
	if (mVRAM < 256)
	{
		// Something likely went wrong using the above extensions
		// try WMI first and fall back to old method (from dxdiag) if all else fails
		// Function will check all GPUs WMI knows of and will pick up the one with most
		// memory. We need to check all GPUs because system can switch active GPU to
		// weaker one, to preserve power when not under load.
		S32 mem = LLDXHardware::getMBVideoMemoryViaDXGI();
		if (mem != 0)
		{
			mVRAM = mem;
			LL_INFOS("RenderInit") << "VRAM Detected (WMI):" << mVRAM<< LL_ENDL;
		}
	}
#endif

	if (mVRAM < 256 && old_vram > 0)
	{
		// fall back to old method
		// Note: on Windows value will be from LLDXHardware.
		// Either received via dxdiag or via WMI by id from dxdiag.
		mVRAM = old_vram;
	}

		else
		{
			LL_INFOS("RenderInit") << "Detected VRAM from GL: " << mVRAM << LL_ENDL;
		}
	}
	else
	{
		LL_INFOS("RenderInit") << "Using VRAM value from OS: " << mVRAM << LL_ENDL;
	}

	stop_glerror();

	if (mHasFragmentShader)
	{
		GLint num_tex_image_units;
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &num_tex_image_units);
		mNumTextureImageUnits = llmin(num_tex_image_units, 32);
	}

	LL_INFOS() << "NUM TEX IMAGE UNITS: " << mNumTextureImageUnits << LL_ENDL;

	if (LLRender::sGLCoreProfile)
	{
		mNumTextureUnits = llmin(mNumTextureImageUnits, MAX_GL_TEXTURE_UNITS);
	}
	else if (mHasMultitexture)
	{
		GLint num_tex_units;		
		glGetIntegerv(GL_MAX_TEXTURE_UNITS, &num_tex_units);
		mNumTextureUnits = llmin(num_tex_units, (GLint)MAX_GL_TEXTURE_UNITS);
		if (mIsIntel)
		{
			mNumTextureUnits = llmin(mNumTextureUnits, 2);
		}
	}
	else
	{
		mHasRequirements = FALSE;

		// We don't support cards that don't support the GL_ARB_multitexture extension
		LL_WARNS("RenderInit") << "GL Drivers do not support GL_ARB_multitexture" << LL_ENDL;
		return false;
	}
	
	stop_glerror();

	if (mHasTextureMultisample)
	{
		glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &mMaxColorTextureSamples);
		glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &mMaxDepthTextureSamples);
		glGetIntegerv(GL_MAX_INTEGER_SAMPLES, &mMaxIntegerSamples);
		glGetIntegerv(GL_MAX_SAMPLE_MASK_WORDS, &mMaxSampleMaskWords);
	}

	stop_glerror();

	if (mHasDebugOutput && gDebugGL)
	{ //setup debug output callback
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW, 0, NULL, GL_TRUE);
		glDebugMessageCallback((GLDEBUGPROC) gl_debug_callback, NULL);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	}

	stop_glerror();

	//HACK always disable texture multisample, use FXAA instead
	mHasTextureMultisample = FALSE;
#if LL_WINDOWS
	if (mIsATI)
	{ //using multisample textures on ATI results in black screen for some reason
		mHasTextureMultisample = FALSE;
	}


	if (mIsIntel && mGLVersion <= 3.f)
	{ //never try to use framebuffer objects on older intel drivers (crashy)
		mHasFramebufferObject = FALSE;
	}
#endif

	if (mHasFramebufferObject)
	{
		glGetIntegerv(GL_MAX_SAMPLES, &mMaxSamples);
	}

	stop_glerror();
	
	initGLStates();

	stop_glerror();

	return true;
}

void LLGLManager::getGLInfo(LLSD& info)
{
	if (gHeadlessClient)
	{
		info["GLInfo"]["GLVendor"] = HEADLESS_VENDOR_STRING;
		info["GLInfo"]["GLRenderer"] = HEADLESS_RENDERER_STRING;
		info["GLInfo"]["GLVersion"] = HEADLESS_VERSION_STRING;
		return;
	}
	else
	{
		info["GLInfo"]["GLVendor"] = ll_safe_string((const char *)glGetString(GL_VENDOR));
		info["GLInfo"]["GLRenderer"] = ll_safe_string((const char *)glGetString(GL_RENDERER));
		info["GLInfo"]["GLVersion"] = ll_safe_string((const char *)glGetString(GL_VERSION));
	}

#if !LL_MESA_HEADLESS
#if !LL_DARWIN
    if (mGLVersion >= 3.0f && glGetStringi) {
        std::vector<std::string> gl_extension_str_vec;
        GLint n, i;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (i = 0; i < n; i++) {
            std::string exten = ll_safe_string((const char *) glGetStringi(GL_EXTENSIONS, i));
            if (!exten.empty())
                info["GLInfo"]["GLExtensions"].append(exten);
        }
    } else
#endif
    {
        std::string all_exts = ll_safe_string((const char *) glGetString(GL_EXTENSIONS));
        boost::char_separator<char> sep(" ");
        boost::tokenizer<boost::char_separator<char> > tok(all_exts, sep);
        for (boost::tokenizer<boost::char_separator<char> >::iterator i = tok.begin(); i != tok.end(); ++i) {
            info["GLInfo"]["GLExtensions"].append(*i);
        }
    }
#endif
}

std::string LLGLManager::getGLInfoString()
{
	std::string info_str;

	if (gHeadlessClient)
	{
		info_str += std::string("GL_VENDOR      ") + HEADLESS_VENDOR_STRING + std::string("\n");
		info_str += std::string("GL_RENDERER    ") + HEADLESS_RENDERER_STRING + std::string("\n");
		info_str += std::string("GL_VERSION     ") + HEADLESS_VERSION_STRING + std::string("\n");
	}
	else
	{
		info_str += std::string("GL_VENDOR      ") + ll_safe_string((const char *)glGetString(GL_VENDOR)) + std::string("\n");
		info_str += std::string("GL_RENDERER    ") + ll_safe_string((const char *)glGetString(GL_RENDERER)) + std::string("\n");
		info_str += std::string("GL_VERSION     ") + ll_safe_string((const char *)glGetString(GL_VERSION)) + std::string("\n");
	}

#if !LL_MESA_HEADLESS
#if !LL_DARWIN
    if (mGLVersion >= 3.0f && glGetStringi) {
        std::stringstream gl_extension_strstrm;
        GLint n, i;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (i = 0; i < n; i++) {
            std::string exten = ll_safe_string((const char *) glGetStringi(GL_EXTENSIONS, i));
            if (!exten.empty() && i != n)
                gl_extension_strstrm << exten << "\n";
        }
        info_str += std::string("GL_EXTENSIONS:\n") + gl_extension_strstrm.str() + std::string("\n");
    } else
#endif
    {
        std::string all_exts = ll_safe_string((const char *) glGetString(GL_EXTENSIONS));
        LLStringUtil::replaceChar(all_exts, ' ', '\n');
        info_str += std::string("GL_EXTENSIONS:\n") + all_exts + std::string("\n");
    }
#endif

    return info_str;
}

void LLGLManager::printGLInfoString()
{
	if (gHeadlessClient)
	{
		LL_INFOS("RenderInit") << "GL_VENDOR:     " << HEADLESS_VENDOR_STRING << LL_ENDL;
		LL_INFOS("RenderInit") << "GL_RENDERER:   " << HEADLESS_RENDERER_STRING << LL_ENDL;
		LL_INFOS("RenderInit") << "GL_VERSION:    " << HEADLESS_VERSION_STRING << LL_ENDL;
	}
	else
	{
		LL_INFOS("RenderInit") << "GL_VENDOR:     " << ll_safe_string((const char *)glGetString(GL_VENDOR)) << LL_ENDL;
		LL_INFOS("RenderInit") << "GL_RENDERER:   " << ll_safe_string((const char *)glGetString(GL_RENDERER)) << LL_ENDL;
		LL_INFOS("RenderInit") << "GL_VERSION:    " << ll_safe_string((const char *)glGetString(GL_VERSION)) << LL_ENDL;
	}

#if !LL_MESA_HEADLESS
#if !LL_DARWIN
    if (mGLVersion >= 3.0f && glGetStringi) {
        std::stringstream gl_extension_strstrm;
        GLint n, i;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (i = 0; i < n; i++) {
            std::string exten = ll_safe_string((const char *) glGetStringi(GL_EXTENSIONS, i));
            if (!exten.empty() && i != n)
                gl_extension_strstrm << exten << "\n";
        }
        LL_INFOS("RenderInit") << "GL_EXTENSIONS:\n" << gl_extension_strstrm.str() << LL_ENDL;
    }
    else
#endif
    {
        std::string all_exts = ll_safe_string((const char *) glGetString(GL_EXTENSIONS));
        LLStringUtil::replaceChar(all_exts, ' ', '\n');
        LL_INFOS("RenderInit") << "GL_EXTENSIONS:\n" << all_exts << LL_ENDL;
    }
#endif
}

std::string LLGLManager::getRawGLString()
{
	std::string gl_string;
	if (gHeadlessClient)
	{
		gl_string = HEADLESS_VENDOR_STRING + " " + HEADLESS_RENDERER_STRING;
	}
	else
	{
		gl_string = ll_safe_string((char*)glGetString(GL_VENDOR)) + " " + ll_safe_string((char*)glGetString(GL_RENDERER));
	}
	return gl_string;
}

void LLGLManager::asLLSD(LLSD& info)
{
	// Currently these are duplicates of fields in "system".
	info["gpu_vendor"] = mGLVendorShort;
	info["gpu_version"] = mDriverVersionVendorString;
	info["opengl_version"] = mGLVersionString;

	info["vram"] = mVRAM;

	// Extensions used by everyone
	info["has_multitexture"] = mHasMultitexture;
	info["has_ati_mem_info"] = mHasATIMemInfo;
	info["has_nvx_mem_info"] = mHasNVXMemInfo;
	info["num_texture_units"] = mNumTextureUnits;
	info["has_mip_map_generation"] = mHasMipMapGeneration;
	info["has_compressed_textures"] = mHasCompressedTextures;
	info["has_framebuffer_object"] = mHasFramebufferObject;
	info["max_samples"] = mMaxSamples;
	info["has_blend_func_separate"] = mHasBlendFuncSeparate;

	// ARB Extensions
	info["has_vertex_buffer_object"] = mHasVertexBufferObject;
	info["has_vertex_array_object"] = mHasVertexArrayObject;
	info["has_sync"] = mHasSync;
	info["has_map_buffer_range"] = mHasMapBufferRange;
	info["has_flush_buffer_range"] = mHasFlushBufferRange;
	info["has_pbuffer"] = mHasPBuffer;
	info["has_shader_objects"] = mHasShaderObjects;
	info["has_vertex_shader"] = mHasVertexShader;
	info["has_fragment_shader"] = mHasFragmentShader;
	info["num_texture_image_units"] =  mNumTextureImageUnits;
	info["has_occlusion_query"] = mHasOcclusionQuery;
	info["has_timer_query"] = mHasTimerQuery;
	info["has_occlusion_query2"] = mHasOcclusionQuery2;
	info["has_point_parameters"] = mHasPointParameters;
	info["has_draw_buffers"] = mHasDrawBuffers;
	info["has_depth_clamp"] = mHasDepthClamp;
	info["has_texture_rectangle"] = mHasTextureRectangle;
	info["has_texture_multisample"] = mHasTextureMultisample;
	info["has_transform_feedback"] = mHasTransformFeedback;
	info["max_sample_mask_words"] = mMaxSampleMaskWords;
	info["max_color_texture_samples"] = mMaxColorTextureSamples;
	info["max_depth_texture_samples"] = mMaxDepthTextureSamples;
	info["max_integer_samples"] = mMaxIntegerSamples;

	// Other extensions.
	info["has_anisotropic"] = mHasAnisotropic;
	info["has_arb_env_combine"] = mHasARBEnvCombine;
	info["has_cube_map"] = mHasCubeMap;
	info["has_debug_output"] = mHasDebugOutput;
	info["has_srgb_texture"] = mHassRGBTexture;
	info["has_srgb_framebuffer"] = mHassRGBFramebuffer;
    info["has_texture_srgb_decode"] = mHasTexturesRGBDecode;

	// Vendor-specific extensions
	info["is_ati"] = mIsATI;
	info["is_nvidia"] = mIsNVIDIA;
	info["is_intel"] = mIsIntel;
	info["is_gf2or4mx"] = mIsGF2or4MX;
	info["is_gf3"] = mIsGF3;
	info["is_gf_gfx"] = mIsGFFX;
	info["ati_offset_vertical_lines"] = mATIOffsetVerticalLines;
	info["ati_old_driver"] = mATIOldDriver;

	// Other fields
	info["has_requirements"] = mHasRequirements;
	info["has_separate_specular_color"] = mHasSeparateSpecularColor;
	info["max_vertex_range"] = mGLMaxVertexRange;
	info["max_index_range"] = mGLMaxIndexRange;
	info["max_texture_size"] = mGLMaxTextureSize;
	info["gl_renderer"] = mGLRenderer;
}

void LLGLManager::shutdownGL()
{
	if (mInited)
	{
		glFinish();
		stop_glerror();
		mInited = FALSE;
	}
}

// these are used to turn software blending on. They appear in the Debug/Avatar menu
// presence of vertex skinning/blending or vertex programs will set these to FALSE by default.

void LLGLManager::initExtensions()
{
#if LL_MESA_HEADLESS
# ifdef GL_ARB_multitexture
	mHasMultitexture = TRUE;
# else
	mHasMultitexture = FALSE;
# endif // GL_ARB_multitexture
# ifdef GL_ARB_texture_env_combine
	mHasARBEnvCombine = TRUE;	
# else
	mHasARBEnvCombine = FALSE;
# endif // GL_ARB_texture_env_combine
# ifdef GL_ARB_texture_compression
	mHasCompressedTextures = TRUE;
# else
	mHasCompressedTextures = FALSE;
# endif // GL_ARB_texture_compression
# ifdef GL_ARB_vertex_buffer_object
	mHasVertexBufferObject = TRUE;
# else
	mHasVertexBufferObject = FALSE;
# endif // GL_ARB_vertex_buffer_object
# ifdef GL_EXT_framebuffer_object
	mHasFramebufferObject = TRUE;
# else
	mHasFramebufferObject = FALSE;
# endif // GL_EXT_framebuffer_object
# ifdef GL_ARB_draw_buffers
	mHasDrawBuffers = TRUE;
#else
	mHasDrawBuffers = FALSE;
# endif // GL_ARB_draw_buffers
# if defined(GL_NV_depth_clamp) || defined(GL_ARB_depth_clamp)
	mHasDepthClamp = TRUE;
#else
	mHasDepthClamp = FALSE;
#endif // defined(GL_NV_depth_clamp) || defined(GL_ARB_depth_clamp)
# if GL_EXT_blend_func_separate
	mHasBlendFuncSeparate = TRUE;
#else
	mHasBlendFuncSeparate = FALSE;
# endif // GL_EXT_blend_func_separate
	mHasMipMapGeneration = FALSE;
	mHasSeparateSpecularColor = FALSE;
	mHasAnisotropic = FALSE;
	mHasCubeMap = FALSE;
	mHasOcclusionQuery = FALSE;
	mHasPointParameters = FALSE;
	mHasShaderObjects = FALSE;
	mHasVertexShader = FALSE;
	mHasFragmentShader = FALSE;
	mHasTextureRectangle = FALSE;
#else // LL_MESA_HEADLESS
	mHasMultitexture = mGLVersion >= 1.3f || epoxy_has_gl_extension("GL_ARB_multitexture");
	mHasATIMemInfo = epoxy_has_gl_extension("GL_ATI_meminfo"); //Basic AMD method, also see mHasAMDAssociations
	mHasNVXMemInfo = epoxy_has_gl_extension("GL_NVX_gpu_memory_info");
	mHasSeparateSpecularColor = mGLVersion >= 1.2f || epoxy_has_gl_extension("GL_EXT_separate_specular_color");
	mHasAnisotropic = mGLVersion >= 4.6f || epoxy_has_gl_extension("GL_EXT_texture_filter_anisotropic");

	mHasCubeMap = mGLVersion >= 1.3f || epoxy_has_gl_extension("GL_ARB_texture_cube_map");
	mHasARBEnvCombine = mGLVersion >= 1.3f || epoxy_has_gl_extension("GL_ARB_texture_env_combine");
	mHasCompressedTextures = mGLVersion >= 1.3f || epoxy_has_gl_extension("GL_ARB_texture_compression");
	mHasOcclusionQuery = mGLVersion >= 1.5f || epoxy_has_gl_extension("GL_ARB_occlusion_query");
	mHasTimerQuery = mGLVersion >= 3.3f || epoxy_has_gl_extension("GL_ARB_timer_query");
	mHasOcclusionQuery2 = mGLVersion >= 3.3f || epoxy_has_gl_extension("GL_ARB_occlusion_query2");
	mHasVertexBufferObject = mGLVersion >= 1.5f || epoxy_has_gl_extension("GL_ARB_vertex_buffer_object");
	mHasVertexArrayObject = mGLVersion >= 3.0f || epoxy_has_gl_extension("GL_ARB_vertex_array_object");
	mHasSync = mGLVersion >= 3.2f || epoxy_has_gl_extension("GL_ARB_sync");
	mHasMapBufferRange = mGLVersion >= 3.0f || epoxy_has_gl_extension("GL_ARB_map_buffer_range");
	mHasFlushBufferRange = epoxy_has_gl_extension("GL_APPLE_flush_buffer_range");
	mHasDepthClamp = mGLVersion >= 3.2f || (epoxy_has_gl_extension("GL_ARB_depth_clamp") || epoxy_has_gl_extension("GL_NV_depth_clamp"));
	// mask out FBO support when packed_depth_stencil isn't there 'cause we need it for LLRenderTarget -Brad
#ifdef GL_ARB_framebuffer_object
	mHasFramebufferObject = mGLVersion >= 3.0f || epoxy_has_gl_extension("GL_ARB_framebuffer_object");
#else
	mHasFramebufferObject = mGLVersion >= 3.0f || (epoxy_has_gl_extension("GL_EXT_framebuffer_object") &&
		epoxy_has_gl_extension("GL_EXT_framebuffer_blit") &&
		epoxy_has_gl_extension("GL_EXT_framebuffer_multisample") &&
		epoxy_has_gl_extension("GL_EXT_packed_depth_stencil"));
#endif
#ifdef GL_EXT_texture_sRGB
	mHassRGBTexture = mGLVersion >= 2.1f || epoxy_has_gl_extension("GL_EXT_texture_sRGB");
#endif

#ifdef GL_ARB_framebuffer_sRGB
	mHassRGBFramebuffer = mGLVersion >= 3.0f || epoxy_has_gl_extension("GL_ARB_framebuffer_sRGB");
#else
	mHassRGBFramebuffer = mGLVersion >= 3.0f || epoxy_has_gl_extension("GL_EXT_framebuffer_sRGB");
#endif

#ifdef GL_EXT_texture_sRGB_decode
	mHasTexturesRGBDecode = epoxy_has_gl_extension("GL_EXT_texture_sRGB_decode");
#else
	mHasTexturesRGBDecode = epoxy_has_gl_extension("GL_ARB_texture_sRGB_decode");
#endif

	mHasMipMapGeneration = mHasFramebufferObject || mGLVersion >= 1.4f;

	mHasDrawBuffers = mGLVersion >= 2.0f || epoxy_has_gl_extension("GL_ARB_draw_buffers");
	mHasBlendFuncSeparate = mGLVersion >= 1.4f || epoxy_has_gl_extension("GL_EXT_blend_func_separate");
	mHasTextureRectangle = mGLVersion >= 3.1f || epoxy_has_gl_extension("GL_ARB_texture_rectangle");
	mHasTextureMultisample = mGLVersion >= 3.2f || epoxy_has_gl_extension("GL_ARB_texture_multisample");
	mHasDebugOutput = mGLVersion >= 4.3f || epoxy_has_gl_extension("GL_ARB_debug_output");
	mHasTransformFeedback = mGLVersion >= 4.0f ? TRUE : FALSE;
#if !LL_DARWIN
	mHasPointParameters = !mIsATI && (mGLVersion >= 1.4f || epoxy_has_gl_extension("GL_ARB_point_parameters"));
#endif
	mHasShaderObjects = mGLVersion >= 2.0f || (epoxy_has_gl_extension("GL_ARB_shader_objects") && (LLRender::sGLCoreProfile || epoxy_has_gl_extension("GL_ARB_shading_language_100")));
	mHasVertexShader = mGLVersion >= 2.0f || (epoxy_has_gl_extension("GL_ARB_vertex_program") && epoxy_has_gl_extension("GL_ARB_vertex_shader")
		&& (LLRender::sGLCoreProfile || epoxy_has_gl_extension("GL_ARB_shading_language_100")));
	mHasFragmentShader = mGLVersion >= 2.0f || (epoxy_has_gl_extension("GL_ARB_fragment_shader") && (LLRender::sGLCoreProfile || epoxy_has_gl_extension("GL_ARB_shading_language_100")));

	mHasTextureSwizzle = mGLVersion >= 3.3f || epoxy_has_gl_extension("GL_ARB_texture_swizzle");
#endif

#if LL_LINUX
	LL_INFOS() << "initExtensions() checking shell variables to adjust features..." << LL_ENDL;
	// Our extension support for the Linux Client is very young with some
	// potential driver gotchas, so offer a semi-secret way to turn it off.
	if (getenv("LL_GL_NOEXT"))
	{
		//mHasMultitexture = FALSE; // NEEDED!
		mHasDepthClamp = FALSE;
		mHasARBEnvCombine = FALSE;
		mHasCompressedTextures = FALSE;
		mHasVertexBufferObject = FALSE;
		mHasFramebufferObject = FALSE;
		mHasDrawBuffers = FALSE;
		mHasBlendFuncSeparate = FALSE;
		mHasMipMapGeneration = FALSE;
		mHasSeparateSpecularColor = FALSE;
		mHasAnisotropic = FALSE;
		mHasCubeMap = FALSE;
		mHasOcclusionQuery = FALSE;
		mHasPointParameters = FALSE;
		mHasShaderObjects = FALSE;
		mHasVertexShader = FALSE;
		mHasFragmentShader = FALSE;
		mHasTextureSwizzle = FALSE;
		LL_WARNS("RenderInit") << "GL extension support DISABLED via LL_GL_NOEXT" << LL_ENDL;
	}
	else if (getenv("LL_GL_BASICEXT"))	/* Flawfinder: ignore */
	{
		// This switch attempts to turn off all support for exotic
		// extensions which I believe correspond to fatal driver
		// bug reports.  This should be the default until we get a
		// proper blacklist/whitelist on Linux.
		mHasMipMapGeneration = FALSE;
		mHasAnisotropic = FALSE;
		//mHasCubeMap = FALSE; // apparently fatal on Intel 915 & similar
		//mHasOcclusionQuery = FALSE; // source of many ATI system hangs
		mHasShaderObjects = FALSE;
		mHasVertexShader = FALSE;
		mHasFragmentShader = FALSE;
		mHasBlendFuncSeparate = FALSE;
		LL_WARNS("RenderInit") << "GL extension support forced to SIMPLE level via LL_GL_BASICEXT" << LL_ENDL;
	}
	if (getenv("LL_GL_BLACKLIST"))	/* Flawfinder: ignore */
	{
		// This lets advanced troubleshooters disable specific
		// GL extensions to isolate problems with their hardware.
		// SL-28126
		const char *const blacklist = getenv("LL_GL_BLACKLIST");	/* Flawfinder: ignore */
		LL_WARNS("RenderInit") << "GL extension support partially disabled via LL_GL_BLACKLIST: " << blacklist << LL_ENDL;
		if (strchr(blacklist,'a')) mHasARBEnvCombine = FALSE;
		if (strchr(blacklist,'b')) mHasCompressedTextures = FALSE;
		if (strchr(blacklist,'c')) mHasVertexBufferObject = FALSE;
		if (strchr(blacklist,'d')) mHasMipMapGeneration = FALSE;//S
// 		if (strchr(blacklist,'f')) mHasNVVertexArrayRange = FALSE;//S
// 		if (strchr(blacklist,'g')) mHasNVFence = FALSE;//S
		if (strchr(blacklist,'h')) mHasSeparateSpecularColor = FALSE;
		if (strchr(blacklist,'i')) mHasAnisotropic = FALSE;//S
		if (strchr(blacklist,'j')) mHasCubeMap = FALSE;//S
// 		if (strchr(blacklist,'k')) mHasATIVAO = FALSE;//S
		if (strchr(blacklist,'l')) mHasOcclusionQuery = FALSE;
		if (strchr(blacklist,'m')) mHasShaderObjects = FALSE;//S
		if (strchr(blacklist,'n')) mHasVertexShader = FALSE;//S
		if (strchr(blacklist,'o')) mHasFragmentShader = FALSE;//S
		if (strchr(blacklist,'p')) mHasPointParameters = FALSE;//S
		if (strchr(blacklist,'q')) mHasFramebufferObject = FALSE;//S
		if (strchr(blacklist,'r')) mHasDrawBuffers = FALSE;//S
		if (strchr(blacklist,'s')) mHasTextureRectangle = FALSE;
		if (strchr(blacklist,'t')) mHasBlendFuncSeparate = FALSE;//S
		if (strchr(blacklist,'u')) mHasDepthClamp = FALSE;
		
	}
#endif // LL_LINUX
	
	if (!mHasMultitexture)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize multitexturing" << LL_ENDL;
	}
	if (!mHasMipMapGeneration)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize mipmap generation" << LL_ENDL;
	}
	if (!mHasARBEnvCombine)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize GL_ARB_texture_env_combine" << LL_ENDL;
	}
	if (!mHasSeparateSpecularColor)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize separate specular color" << LL_ENDL;
	}
	if (!mHasAnisotropic)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize anisotropic filtering" << LL_ENDL;
	}
	if (!mHasCompressedTextures)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize GL_ARB_texture_compression" << LL_ENDL;
	}
	if (!mHasOcclusionQuery)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize GL_ARB_occlusion_query" << LL_ENDL;
	}
	if (!mHasOcclusionQuery2)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize GL_ARB_occlusion_query2" << LL_ENDL;
	}
	if (!mHasPointParameters)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize GL_ARB_point_parameters" << LL_ENDL;
	}
	if (!mHasShaderObjects)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize GL_ARB_shader_objects" << LL_ENDL;
	}
	if (!mHasVertexShader)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize GL_ARB_vertex_shader" << LL_ENDL;
	}
	if (!mHasFragmentShader)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize GL_ARB_fragment_shader" << LL_ENDL;
	}
	if (!mHasBlendFuncSeparate)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize GL_EXT_blend_func_separate" << LL_ENDL;
	}
	if (!mHasDrawBuffers)
	{
		LL_INFOS("RenderInit") << "Couldn't initialize GL_ARB_draw_buffers" << LL_ENDL;
	}

	// Disable certain things due to known bugs
//	if (mIsIntel && mHasMipMapGeneration)
//	{
//		LL_INFOS("RenderInit") << "Disabling mip-map generation for Intel GPUs" << LL_ENDL;
//		mHasMipMapGeneration = FALSE;
//	}
	
	// Misc
	glGetIntegerv(GL_MAX_ELEMENTS_VERTICES, (GLint*) &mGLMaxVertexRange);
	glGetIntegerv(GL_MAX_ELEMENTS_INDICES, (GLint*) &mGLMaxIndexRange);
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, (GLint*) &mGLMaxTextureSize);


	mInited = TRUE;
}

void rotate_quat(LLQuaternion& rotation)
{
	F32 angle_radians, x, y, z;
	rotation.getAngleAxis(&angle_radians, &x, &y, &z);
	gGL.rotatef(angle_radians * RAD_TO_DEG, x, y, z);
}

void flush_glerror()
{
	glGetError();
}

const std::string getGLErrorString(GLenum error)
{
	switch(error)
	{
	case GL_NO_ERROR:
		return "No Error";
	case GL_INVALID_ENUM:
		return "Invalid Enum";
	case GL_INVALID_VALUE:
		return "Invalid Value";
	case GL_INVALID_OPERATION:
		return "Invalid Operation";
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		return "Invalid Framebuffer Operation";
	case GL_OUT_OF_MEMORY:
		return "Out of Memory";
	case GL_STACK_UNDERFLOW:
		return "Stack Underflow";
	case GL_STACK_OVERFLOW:
		return "Stack Overflow";
#ifdef GL_TABLE_TOO_LARGE
	case GL_TABLE_TOO_LARGE:
		return "Table too large";
#endif
	default:
		return "UNKNOWN ERROR";
	}
}

//this function outputs gl error to the log file, does not crash the code.
void log_glerror()
{
	if (LL_UNLIKELY(!gGLManager.mInited))
	{
		return ;
	}
	//  Create or update texture to be used with this data 
	GLenum error = glGetError();
	while (LL_UNLIKELY(error))
	{
		std::string gl_error_msg = getGLErrorString(error);
		LL_WARNS() << "GL Error: 0x" << std::hex << error << std::dec << " GL Error String: " << gl_error_msg << LL_ENDL;			
		error = glGetError();
	}
}

void do_assert_glerror()
{
	//  Create or update texture to be used with this data 
	GLenum error = glGetError();
	BOOL quit = FALSE;
	while (LL_UNLIKELY(error))
	{
		quit = TRUE;
		
		std::string gl_error_msg = getGLErrorString(error);
		LL_WARNS("RenderState") << "GL Error: 0x" << std::hex << error << std::dec << LL_ENDL;		
		LL_WARNS("RenderState") << "GL Error String: " << gl_error_msg << LL_ENDL;
		if (gDebugSession)
		{
			gFailLog << "GL Error: 0x" << std::hex << error << std::dec << " GL Error String: " << gl_error_msg << std::endl;
		}
		error = glGetError();
	}

	if (quit)
	{
		if (gDebugSession)
		{
			ll_fail("assert_glerror failed");
		}
		else
		{
			LL_ERRS() << "One or more unhandled GL errors." << LL_ENDL;
		}
	}
}

void assert_glerror()
{
/*	if (!gGLActive)
	{
		//LL_WARNS() << "GL used while not active!" << LL_ENDL;

		if (gDebugSession)
		{
			//ll_fail("GL used while not active");
		}
	}
*/

	if (!gDebugGL) 
	{
		//funny looking if for branch prediction -- gDebugGL is almost always false and assert_glerror is called often
	}
	else
	{
		do_assert_glerror();
	}
}
	

void clear_glerror()
{
	glGetError();
	glGetError();
}

///////////////////////////////////////////////////////////////
//
// LLGLState
//

// Static members
absl::flat_hash_map<LLGLenum, LLGLboolean> LLGLState::sStateMap;

GLboolean LLGLDepthTest::sDepthEnabled = GL_FALSE; // OpenGL default
GLenum LLGLDepthTest::sDepthFunc = GL_LESS; // OpenGL default
GLboolean LLGLDepthTest::sWriteEnabled = GL_TRUE; // OpenGL default

//static
void LLGLState::initClass() 
{
	sStateMap[GL_DITHER] = GL_TRUE;
	// sStateMap[GL_TEXTURE_2D] = GL_TRUE;
	
	//make sure multisample defaults to disabled
	sStateMap[GL_MULTISAMPLE] = GL_FALSE;
	glDisable(GL_MULTISAMPLE);
}

//static
void LLGLState::restoreGL()
{
	sStateMap.clear();
	initClass();
}

//static
// Really shouldn't be needed, but seems we sometimes do.
void LLGLState::resetTextureStates()
{
	gGL.flush();
	GLint maxTextureUnits;
	
	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &maxTextureUnits);
	for (S32 j = maxTextureUnits-1; j >=0; j--)
	{
		gGL.getTexUnit(j)->activate();
		glClientActiveTexture(GL_TEXTURE0+j);
		j == 0 ? gGL.getTexUnit(j)->enable(LLTexUnit::TT_TEXTURE) : gGL.getTexUnit(j)->disable();
	}
}

void LLGLState::dumpStates() 
{
	LL_INFOS("RenderState") << "GL States:" << LL_ENDL;
	for (const auto& state_pair : sStateMap)
	{
		LL_INFOS("RenderState") << llformat(" 0x%04x : %s",(S32)state_pair.first, state_pair.second?"TRUE":"FALSE") << LL_ENDL;
	}
}

void LLGLState::checkStates(const std::string& msg)  
{
	if (!gDebugGL)
	{
		return;
	}

	stop_glerror();

	GLint src;
	GLint dst;
	glGetIntegerv(GL_BLEND_SRC, &src);
	glGetIntegerv(GL_BLEND_DST, &dst);
	
	stop_glerror();

	BOOL error = FALSE;

	if (src != GL_SRC_ALPHA || dst != GL_ONE_MINUS_SRC_ALPHA)
	{
		if (gDebugSession)
		{
			gFailLog << "Blend function corrupted: " << std::hex << src << " " << std::hex << dst << "  " << msg << std::dec << std::endl;
			error = TRUE;
		}
		else
		{
			LL_GL_ERRS << "Blend function corrupted: " << std::hex << src << " " << std::hex << dst << "  " << msg << std::dec << LL_ENDL;
		}
	}
	
	for (auto iter = sStateMap.begin();
		 iter != sStateMap.end(); ++iter)
	{
		LLGLenum state = iter->first;
		LLGLboolean cur_state = iter->second;
		stop_glerror();
		LLGLboolean gl_state = glIsEnabled(state);
		stop_glerror();
		if(cur_state != gl_state)
		{
			dumpStates();
			if (gDebugSession)
			{
				gFailLog << llformat("LLGLState error. State: 0x%04x",state) << std::endl;
				error = TRUE;
			}
			else
			{
				LL_GL_ERRS << llformat("LLGLState error. State: 0x%04x",state) << LL_ENDL;
			}
		}
	}
	
	if (error)
	{
		ll_fail("LLGLState::checkStates failed.");
	}
	stop_glerror();
}

void LLGLState::checkTextureChannels(const std::string& msg)
{
#if 0
	if (!gDebugGL)
	{
		return;
	}
	stop_glerror();

	GLint activeTexture;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
	stop_glerror();

	BOOL error = FALSE;

	if (activeTexture == GL_TEXTURE0)
	{
		GLint tex_env_mode = 0;

		glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &tex_env_mode);
		stop_glerror();

		if (tex_env_mode != GL_MODULATE)
		{
			error = TRUE;
			LL_WARNS("RenderState") << "GL_TEXTURE_ENV_MODE invalid: " << std::hex << tex_env_mode << std::dec << LL_ENDL;
			if (gDebugSession)
			{
				gFailLog << "GL_TEXTURE_ENV_MODE invalid: " << std::hex << tex_env_mode << std::dec << std::endl;
			}
		}
	}

	static const char* label[] =
	{
		"GL_TEXTURE_2D",
		"GL_TEXTURE_COORD_ARRAY",
		"GL_TEXTURE_1D",
		"GL_TEXTURE_CUBE_MAP",
		"GL_TEXTURE_GEN_S",
		"GL_TEXTURE_GEN_T",
		"GL_TEXTURE_GEN_Q",
		"GL_TEXTURE_GEN_R",
		"GL_TEXTURE_RECTANGLE",
		"GL_TEXTURE_2D_MULTISAMPLE"
	};

	static GLint value[] =
	{
		GL_TEXTURE_2D,
		GL_TEXTURE_COORD_ARRAY,
		GL_TEXTURE_1D,
		GL_TEXTURE_CUBE_MAP,
		GL_TEXTURE_GEN_S,
		GL_TEXTURE_GEN_T,
		GL_TEXTURE_GEN_Q,
		GL_TEXTURE_GEN_R,
		GL_TEXTURE_RECTANGLE,
		GL_TEXTURE_2D_MULTISAMPLE
	};

	GLint stackDepth = 0;

	glh::matrix4f mat;
	glh::matrix4f identity;
	identity.identity();

	for (GLint i = 1; i < gGLManager.mNumTextureUnits; i++)
	{
		gGL.getTexUnit(i)->activate();

		if (i < gGLManager.mNumTextureUnits)
		{
			glClientActiveTexture(GL_TEXTURE0+i);
			stop_glerror();
			glGetIntegerv(GL_TEXTURE_STACK_DEPTH, &stackDepth);
			stop_glerror();

			if (stackDepth != 1)
			{
				error = TRUE;
				LL_WARNS("RenderState") << "Texture matrix stack corrupted." << LL_ENDL;

				if (gDebugSession)
				{
					gFailLog << "Texture matrix stack corrupted." << std::endl;
				}
			}

			glGetFloatv(GL_TEXTURE_MATRIX, (GLfloat*) mat.m);
			stop_glerror();

			if (mat != identity)
			{
				error = TRUE;
				LL_WARNS("RenderState") << "Texture matrix in channel " << i << " corrupt." << LL_ENDL;
				if (gDebugSession)
				{
					gFailLog << "Texture matrix in channel " << i << " corrupt." << std::endl;
				}
			}
				
			for (S32 j = (i == 0 ? 1 : 0); 
				j < 9; j++)
			{
				if (j == 8 && !gGLManager.mHasTextureRectangle ||
					j == 9 && !gGLManager.mHasTextureMultisample)
				{
					continue;
				}
				
				if (glIsEnabled(value[j]))
				{
					error = TRUE;
					LL_WARNS("RenderState") << "Texture channel " << i << " still has " << label[j] << " enabled." << LL_ENDL;
					if (gDebugSession)
					{
						gFailLog << "Texture channel " << i << " still has " << label[j] << " enabled." << std::endl;
					}
				}
				stop_glerror();
			}

			glGetFloatv(GL_TEXTURE_MATRIX, mat.m);
			stop_glerror();

			if (mat != identity)
			{
				error = TRUE;
				LL_WARNS("RenderState") << "Texture matrix " << i << " is not identity." << LL_ENDL;
				if (gDebugSession)
				{
					gFailLog << "Texture matrix " << i << " is not identity." << std::endl;
				}
			}
		}

		{
			GLint tex = 0;
			stop_glerror();
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
			stop_glerror();

			if (tex != 0)
			{
				error = TRUE;
				LL_WARNS("RenderState") << "Texture channel " << i << " still has texture " << tex << " bound." << LL_ENDL;

				if (gDebugSession)
				{
					gFailLog << "Texture channel " << i << " still has texture " << tex << " bound." << std::endl;
				}
			}
		}
	}

	stop_glerror();
	gGL.getTexUnit(0)->activate();
	glClientActiveTexture(GL_TEXTURE0);
	stop_glerror();

	if (error)
	{
		if (gDebugSession)
		{
			ll_fail("LLGLState::checkTextureChannels failed.");
		}
		else
		{
			LL_GL_ERRS << "GL texture state corruption detected.  " << msg << LL_ENDL;
		}
	}
#endif
}

void LLGLState::checkClientArrays(const std::string& msg, U32 data_mask)
{
	if (!gDebugGL || LLGLSLShader::sNoFixedFunction)
	{
		return;
	}

	stop_glerror();
	BOOL error = FALSE;

	GLint active_texture;
	glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &active_texture);

	if (active_texture != GL_TEXTURE0)
	{
		LL_WARNS() << "Client active texture corrupted: " << active_texture << LL_ENDL;
		if (gDebugSession)
		{
			gFailLog << "Client active texture corrupted: " << active_texture << std::endl;
		}
		error = TRUE;
	}

	/*glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
	if (active_texture != GL_TEXTURE0)
	{
		LL_WARNS() << "Active texture corrupted: " << active_texture << LL_ENDL;
		if (gDebugSession)
		{
			gFailLog << "Active texture corrupted: " << active_texture << std::endl;
		}
		error = TRUE;
	}*/

	static const char* label[] =
	{
		"GL_VERTEX_ARRAY",
		"GL_NORMAL_ARRAY",
		"GL_COLOR_ARRAY",
		"GL_TEXTURE_COORD_ARRAY"
	};

	static GLint value[] =
	{
		GL_VERTEX_ARRAY,
		GL_NORMAL_ARRAY,
		GL_COLOR_ARRAY,
		GL_TEXTURE_COORD_ARRAY
	};

	static const U32 mask[] = 
	{ //copied from llvertexbuffer.h
		0x0001, //MAP_VERTEX,
		0x0002, //MAP_NORMAL,
		0x0010, //MAP_COLOR,
		0x0004, //MAP_TEXCOORD
	};


	for (S32 j = 1; j < 4; j++)
	{
		if (glIsEnabled(value[j]))
		{
			if (!(mask[j] & data_mask))
			{
				error = TRUE;
				LL_WARNS("RenderState") << "GL still has " << label[j] << " enabled." << LL_ENDL;
				if (gDebugSession)
				{
					gFailLog << "GL still has " << label[j] << " enabled." << std::endl;
				}
			}
		}
		else
		{
			if (mask[j] & data_mask)
			{
				error = TRUE;
				LL_WARNS("RenderState") << "GL does not have " << label[j] << " enabled." << LL_ENDL;
				if (gDebugSession)
				{
					gFailLog << "GL does not have " << label[j] << " enabled." << std::endl;
				}
			}
		}
	}

	glClientActiveTexture(GL_TEXTURE1);
	gGL.getTexUnit(1)->activate();
	if (glIsEnabled(GL_TEXTURE_COORD_ARRAY))
	{
		if (!(data_mask & 0x0008))
		{
			error = TRUE;
			LL_WARNS("RenderState") << "GL still has GL_TEXTURE_COORD_ARRAY enabled on channel 1." << LL_ENDL;
			if (gDebugSession)
			{
				gFailLog << "GL still has GL_TEXTURE_COORD_ARRAY enabled on channel 1." << std::endl;
			}
		}
	}
	else
	{
		if (data_mask & 0x0008)
		{
			error = TRUE;
			LL_WARNS("RenderState") << "GL does not have GL_TEXTURE_COORD_ARRAY enabled on channel 1." << LL_ENDL;
			if (gDebugSession)
			{
				gFailLog << "GL does not have GL_TEXTURE_COORD_ARRAY enabled on channel 1." << std::endl;
			}
		}
	}

	/*if (glIsEnabled(GL_TEXTURE_2D))
	{
		if (!(data_mask & 0x0008))
		{
			error = TRUE;
			LL_WARNS("RenderState") << "GL still has GL_TEXTURE_2D enabled on channel 1." << LL_ENDL;
			if (gDebugSession)
			{
				gFailLog << "GL still has GL_TEXTURE_2D enabled on channel 1." << std::endl;
			}
		}
	}
	else
	{
		if (data_mask & 0x0008)
		{
			error = TRUE;
			LL_WARNS("RenderState") << "GL does not have GL_TEXTURE_2D enabled on channel 1." << LL_ENDL;
			if (gDebugSession)
			{
				gFailLog << "GL does not have GL_TEXTURE_2D enabled on channel 1." << std::endl;
			}
		}
	}*/

	glClientActiveTexture(GL_TEXTURE0);
	gGL.getTexUnit(0)->activate();

	if (gGLManager.mHasVertexShader && LLGLSLShader::sNoFixedFunction)
	{	//make sure vertex attribs are all disabled
		GLint count;
		glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &count);
		for (GLint i = 0; i < count; i++)
		{
			GLint enabled;
			glGetVertexAttribiv((GLuint) i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
			if (enabled)
			{
				error = TRUE;
				LL_WARNS("RenderState") << "GL still has vertex attrib array " << i << " enabled." << LL_ENDL;
				if (gDebugSession)
				{
					gFailLog <<  "GL still has vertex attrib array " << i << " enabled." << std::endl;
				}
			}
		}
	}

	if (error)
	{
		if (gDebugSession)
		{
			ll_fail("LLGLState::checkClientArrays failed.");
		}
		else
		{
			LL_GL_ERRS << "GL client array corruption detected.  " << msg << LL_ENDL;
		}
	}
}

///////////////////////////////////////////////////////////////////////

LLGLState::LLGLState(LLGLenum state, S32 enabled) :
	mState(state), mWasEnabled(FALSE), mIsEnabled(FALSE)
{
	if (LLGLSLShader::sNoFixedFunction)
	{ //always ignore state that's deprecated post GL 3.0
		switch (state)
		{
			case GL_ALPHA_TEST:
			case GL_NORMALIZE:
			case GL_TEXTURE_GEN_R:
			case GL_TEXTURE_GEN_S:
			case GL_TEXTURE_GEN_T:
			case GL_TEXTURE_GEN_Q:
			case GL_LIGHTING:
			case GL_COLOR_MATERIAL:
			case GL_FOG:
			case GL_LINE_STIPPLE:
			case GL_POLYGON_STIPPLE:
				mState = 0;
				break;
		}
	}

	stop_glerror();
	if (mState)
	{
		mWasEnabled = sStateMap[state];
        // we can't actually assert on this as queued changes to state are not reflected by glIsEnabled
		//llassert(mWasEnabled == glIsEnabled(state));
		setEnabled(enabled);
		stop_glerror();
	}
}

void LLGLState::setEnabled(S32 enabled)
{
	if (!mState)
	{
		return;
	}
	if (enabled == CURRENT_STATE)
	{
		enabled = sStateMap[mState] == GL_TRUE ? TRUE : FALSE;
	}
	else if (enabled == TRUE && sStateMap[mState] != GL_TRUE)
	{
		gGL.flush();
		glEnable(mState);
		sStateMap[mState] = GL_TRUE;
	}
	else if (enabled == FALSE && sStateMap[mState] != GL_FALSE)
	{
		gGL.flush();
		glDisable(mState);
		sStateMap[mState] = GL_FALSE;
	}
	mIsEnabled = enabled;
}

LLGLState::~LLGLState() 
{
	stop_glerror();
	if (mState)
	{
		if (gDebugGL)
		{
			if (!gDebugSession)
			{
				llassert_always(sStateMap[mState] == glIsEnabled(mState));
			}
			else
			{
				if (sStateMap[mState] != glIsEnabled(mState))
				{
					ll_fail("GL enabled state does not match expected");
				}
			}
		}

		if (mIsEnabled != mWasEnabled)
		{
			gGL.flush();
			if (mWasEnabled)
			{
				glEnable(mState);
				sStateMap[mState] = GL_TRUE;
			}
			else
			{
				glDisable(mState);
				sStateMap[mState] = GL_FALSE;
			}
		}
	}
	stop_glerror();
}

////////////////////////////////////////////////////////////////////////////////

void LLGLManager::initGLStates()
{
	//gl states moved to classes in llglstates.h
	LLGLState::initClass();
}

////////////////////////////////////////////////////////////////////////////////

void parse_gl_version( S32* major, S32* minor, S32* release, std::string* vendor_specific, std::string* version_string )
{
	// GL_VERSION returns a null-terminated string with the format: 
	// <major>.<minor>[.<release>] [<vendor specific>]

	const char* version = (const char*) glGetString(GL_VERSION);
	*major = 0;
	*minor = 0;
	*release = 0;
	vendor_specific->assign("");

	if( !version )
	{
		return;
	}

	version_string->assign(version);

	std::string ver_copy( version );
	S32 len = (S32)strlen( version );	/* Flawfinder: ignore */
	S32 i = 0;
	S32 start;
	// Find the major version
	start = i;
	for( ; i < len; i++ )
	{
		if( '.' == version[i] )
		{
			break;
		}
	}
	std::string major_str = ver_copy.substr(start,i-start);
	LLStringUtil::convertToS32(major_str, *major);

	if( '.' == version[i] )
	{
		i++;
	}

	// Find the minor version
	start = i;
	for( ; i < len; i++ )
	{
		if( ('.' == version[i]) || isspace(version[i]) )
		{
			break;
		}
	}
	std::string minor_str = ver_copy.substr(start,i-start);
	LLStringUtil::convertToS32(minor_str, *minor);

	// Find the release number (optional)
	if( '.' == version[i] )
	{
		i++;

		start = i;
		for( ; i < len; i++ )
		{
			if( isspace(version[i]) )
			{
				break;
			}
		}

		std::string release_str = ver_copy.substr(start,i-start);
		LLStringUtil::convertToS32(release_str, *release);
	}

	// Skip over any white space
	while( version[i] && isspace( version[i] ) )
	{
		i++;
	}

	// Copy the vendor-specific string (optional)
	if( version[i] )
	{
		vendor_specific->assign( version + i );
	}
}


void parse_glsl_version(S32& major, S32& minor)
{
	// GL_SHADING_LANGUAGE_VERSION returns a null-terminated string with the format: 
	// <major>.<minor>[.<release>] [<vendor specific>]

	const char* version = (const char*) glGetString(GL_SHADING_LANGUAGE_VERSION);
	major = 0;
	minor = 0;
	
	if( !version )
	{
		return;
	}

	std::string ver_copy( version );
	S32 len = (S32)strlen( version );	/* Flawfinder: ignore */
	S32 i = 0;
	S32 start;
	// Find the major version
	start = i;
	for( ; i < len; i++ )
	{
		if( '.' == version[i] )
		{
			break;
		}
	}
	std::string major_str = ver_copy.substr(start,i-start);
	LLStringUtil::convertToS32(major_str, major);

	if( '.' == version[i] )
	{
		i++;
	}

	// Find the minor version
	start = i;
	for( ; i < len; i++ )
	{
		if( ('.' == version[i]) || isspace(version[i]) )
		{
			break;
		}
	}
	std::string minor_str = ver_copy.substr(start,i-start);
	LLStringUtil::convertToS32(minor_str, minor);
}

LLGLUserClipPlane::LLGLUserClipPlane(const LLPlane& p, const glh::matrix4f& modelview, const glh::matrix4f& projection, bool apply)
{
	mApply = apply;

	if (mApply)
	{
		mModelview = modelview;
		mProjection = projection;

		setPlane(p[0], p[1], p[2], p[3]);
	}
}

void LLGLUserClipPlane::disable()
{
    if (mApply)
	{
		gGL.matrixMode(LLRender::MM_PROJECTION);
		gGL.popMatrix();
		gGL.matrixMode(LLRender::MM_MODELVIEW);
	}
    mApply = false;
}

void LLGLUserClipPlane::setPlane(F32 a, F32 b, F32 c, F32 d)
{
	glh::matrix4f& P = mProjection;
	glh::matrix4f& M = mModelview;
    
	glh::matrix4f invtrans_MVP = (P * M).inverse().transpose();
    glh::vec4f oplane(a,b,c,d);
    glh::vec4f cplane;
    invtrans_MVP.mult_matrix_vec(oplane, cplane);

    cplane /= fabs(cplane[2]); // normalize such that depth is not scaled
    cplane[3] -= 1;

    if(cplane[2] < 0)
        cplane *= -1;

    glh::matrix4f suffix;
    suffix.set_row(2, cplane);
    glh::matrix4f newP = suffix * P;
    gGL.matrixMode(LLRender::MM_PROJECTION);
	gGL.pushMatrix();
    gGL.loadMatrix(newP.m);
    gGL.matrixMode(LLRender::MM_MODELVIEW);
}

LLGLUserClipPlane::~LLGLUserClipPlane()
{
	disable();
}

LLGLDepthTest::LLGLDepthTest(GLboolean depth_enabled, GLboolean write_enabled, GLenum depth_func)
: mPrevDepthEnabled(sDepthEnabled), mPrevDepthFunc(sDepthFunc), mPrevWriteEnabled(sWriteEnabled)
{
	stop_glerror();
	
	checkState();

	if (!depth_enabled)
	{ // always disable depth writes if depth testing is disabled
	  // GL spec defines this as a requirement, but some implementations allow depth writes with testing disabled
	  // The proper way to write to depth buffer with testing disabled is to enable testing and use a depth_func of GL_ALWAYS
		write_enabled = FALSE;
	}

	if (depth_enabled != sDepthEnabled)
	{
		gGL.flush();
		if (depth_enabled) glEnable(GL_DEPTH_TEST);
		else glDisable(GL_DEPTH_TEST);
		sDepthEnabled = depth_enabled;
	}
	if (depth_func != sDepthFunc)
	{
		gGL.flush();
		glDepthFunc(depth_func);
		sDepthFunc = depth_func;
	}
	if (write_enabled != sWriteEnabled)
	{
		gGL.flush();
		glDepthMask(write_enabled);
		sWriteEnabled = write_enabled;
	}
}

LLGLDepthTest::~LLGLDepthTest()
{
	checkState();
	if (sDepthEnabled != mPrevDepthEnabled )
	{
		gGL.flush();
		if (mPrevDepthEnabled) glEnable(GL_DEPTH_TEST);
		else glDisable(GL_DEPTH_TEST);
		sDepthEnabled = mPrevDepthEnabled;
	}
	if (sDepthFunc != mPrevDepthFunc)
	{
		gGL.flush();
		glDepthFunc(mPrevDepthFunc);
		sDepthFunc = mPrevDepthFunc;
	}
	if (sWriteEnabled != mPrevWriteEnabled )
	{
		gGL.flush();
		glDepthMask(mPrevWriteEnabled);
		sWriteEnabled = mPrevWriteEnabled;
	}
}

void LLGLDepthTest::checkState()
{
	if (gDebugGL)
	{
		GLint func = 0;
		GLboolean mask = FALSE;

		glGetIntegerv(GL_DEPTH_FUNC, &func);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);

		if (glIsEnabled(GL_DEPTH_TEST) != sDepthEnabled ||
			sWriteEnabled != mask ||
			sDepthFunc != func)
		{
			if (gDebugSession)
			{
				gFailLog << "Unexpected depth testing state." << std::endl;
			}
			else
			{
				LL_GL_ERRS << "Unexpected depth testing state." << LL_ENDL;
			}
		}
	}
}

LLGLSquashToFarClip::LLGLSquashToFarClip()
{
    glh::matrix4f proj = get_current_projection();
    setProjectionMatrix(proj, 0);
}

LLGLSquashToFarClip::LLGLSquashToFarClip(glh::matrix4f& P, U32 layer)
{
    setProjectionMatrix(P, layer);
}


void LLGLSquashToFarClip::setProjectionMatrix(glh::matrix4f& projection, U32 layer)
{

	F32 depth = 0.99999f - 0.0001f * layer;

	for (U32 i = 0; i < 4; i++)
	{
		projection.element(2, i) = projection.element(3, i) * depth;
	}

    LLRender::eMatrixMode last_matrix_mode = gGL.getMatrixMode();

	gGL.matrixMode(LLRender::MM_PROJECTION);
	gGL.pushMatrix();
	gGL.loadMatrix(projection.m);

	gGL.matrixMode(last_matrix_mode);
}

LLGLSquashToFarClip::~LLGLSquashToFarClip()
{
    LLRender::eMatrixMode last_matrix_mode = gGL.getMatrixMode();

	gGL.matrixMode(LLRender::MM_PROJECTION);
	gGL.popMatrix();

	gGL.matrixMode(last_matrix_mode);
}


	
LLGLSyncFence::LLGLSyncFence()
{
#ifdef GL_ARB_sync
	mSync = 0;
#endif
}

LLGLSyncFence::~LLGLSyncFence()
{
#ifdef GL_ARB_sync
	if (mSync)
	{
		glDeleteSync(mSync);
	}
#endif
}

void LLGLSyncFence::placeFence()
{
#ifdef GL_ARB_sync
	if (mSync)
	{
		glDeleteSync(mSync);
	}
	mSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
#endif
}

bool LLGLSyncFence::isCompleted()
{
	bool ret = true;
#ifdef GL_ARB_sync
	if (mSync)
	{
		GLenum status = glClientWaitSync(mSync, 0, 1);
		if (status == GL_TIMEOUT_EXPIRED)
		{
			ret = false;
		}
	}
#endif
	return ret;
}

void LLGLSyncFence::wait()
{
#ifdef GL_ARB_sync
	if (mSync)
	{
		while (glClientWaitSync(mSync, 0, FENCE_WAIT_TIME_NANOSECONDS) == GL_TIMEOUT_EXPIRED)
		{ //track the number of times we've waited here
			static S32 waits = 0;
			waits++;
		}
	}
#endif
}

LLGLSPipelineSkyBox::LLGLSPipelineSkyBox()
: mAlphaTest(GL_ALPHA_TEST)
, mCullFace(GL_CULL_FACE)
, mSquashClip()
{ 
    if (!LLGLSLShader::sNoFixedFunction)
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_FOG);
        glDisable(GL_CLIP_PLANE0);
    }
}

LLGLSPipelineSkyBox::~LLGLSPipelineSkyBox()
{
    if (!LLGLSLShader::sNoFixedFunction)
    {
        glEnable(GL_LIGHTING);
        glEnable(GL_FOG);
        glEnable(GL_CLIP_PLANE0);
    }
}

LLGLSPipelineDepthTestSkyBox::LLGLSPipelineDepthTestSkyBox(bool depth_test, bool depth_write)
: LLGLSPipelineSkyBox()
, mDepth(depth_test ? GL_TRUE : GL_FALSE, depth_write ? GL_TRUE : GL_FALSE, GL_LEQUAL)
{

}

LLGLSPipelineBlendSkyBox::LLGLSPipelineBlendSkyBox(bool depth_test, bool depth_write)
: LLGLSPipelineDepthTestSkyBox(depth_test, depth_write)    
, mBlend(GL_BLEND)
{ 
    gGL.setSceneBlendType(LLRender::BT_ALPHA);
}

#if LL_WINDOWS
// Expose desired use of high-performance graphics processor to Optimus driver and to AMD driver
// https://docs.nvidia.com/gameworks/content/technologies/desktop/optimus.htm
extern "C" 
{ 
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

