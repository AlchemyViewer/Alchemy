/**
 * @file llshadermgr.cpp
 * @brief Shader manager implementation.
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
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
#include "llshadermgr.h"
#include "llrender.h"
#include "llfile.h"
#include "lldir.h"
#include "llsdutil.h"
#include "llsdserialize.h"

#if LL_DARWIN
#include "OpenGL/OpenGL.h"
#endif

// Lots of STL stuff in here, using namespace std to keep things more readable
using std::vector;
using std::pair;
using std::make_pair;
using std::string;

LLShaderMgr * LLShaderMgr::sInstance = NULL;

LLShaderMgr::LLShaderMgr()
{
}


LLShaderMgr::~LLShaderMgr()
{
}

// static
LLShaderMgr * LLShaderMgr::instance()
{
    if(NULL == sInstance)
    {
        LL_ERRS("Shaders") << "LLShaderMgr should already have been instantiated by the application!" << LL_ENDL;
    }

    return sInstance;
}

std::string LLShaderMgr::variantObjectKey(const std::string& path, U32 axes, const LLGLSLShader* shader, GLenum stage) const
{
    const char* suffix = nullptr;
    if ((axes & OBJ_AXIS_CLASSIC) && shader->mDefines.count("CLASSIC_MODE"))
    {
        suffix = CLASSIC_OBJECT_SUFFIX;
    }
    else if ((axes & OBJ_AXIS_MIRROR) && shader->mDefines.count("MIRROR_CLIP"))
    {
        suffix = MIRROR_OBJECT_SUFFIX;
    }

    if (!suffix)
    {
        return path;
    }

    // No object under the variant key means the loader decided this source could not vary on
    // the axis at the class level it resolved to -- deferred/reflectionProbeF.glsl reads
    // CLASSIC_MODE at class3 and not at class2 -- so the base object already IS the right one
    // and no second copy was compiled. Absence is always that decision and never a failure:
    // loadBasicShaders aborts the whole load if a copy it did want fails to build.
    const std::string key = path + suffix;
    const auto& objects = (stage == GL_VERTEX_SHADER) ? mVertexShaderObjects : mFragmentShaderObjects;
    return objects.count(key) ? key : path;
}

bool LLShaderMgr::attachShaderFeatures(LLGLSLShader * shader)
{
    llassert_always(shader != NULL);
    LLShaderFeatures *features = & shader->mFeatures;

    if (features->attachNothing)
    {
        return true;
    }
    //////////////////////////////////////
    // Attach Vertex Shader Features First
    //////////////////////////////////////

    // NOTE order of shader object attaching is VERY IMPORTANT!!!
    if (features->calculatesAtmospherics || features->hasGamma || features->isDeferred)
    {
        if (!shader->attachVertexObject("windlight/atmosphericsVarsV.glsl"))
        {
            return false;
        }
    }

    if (features->calculatesLighting || features->calculatesAtmospherics)
    {
        if (!shader->attachVertexObject("windlight/atmosphericsHelpersV.glsl"))
        {
            return false;
        }
    }

    if (features->calculatesLighting)
    {
        if (features->isSpecular)
        {
            if (!shader->attachVertexObject("lighting/lightFuncSpecularV.glsl"))
            {
                return false;
            }

            if (!features->isAlphaLighting)
            {
                if (!shader->attachVertexObject("lighting/sumLightsSpecularV.glsl"))
                {
                    return false;
                }
            }

            if (!shader->attachVertexObject("lighting/lightSpecularV.glsl"))
            {
                return false;
            }
        }
        else
        {
            if (!shader->attachVertexObject("lighting/lightFuncV.glsl"))
            {
                return false;
            }

            if (!features->isAlphaLighting)
            {
                if (!shader->attachVertexObject("lighting/sumLightsV.glsl"))
                {
                    return false;
                }
            }

            if (!shader->attachVertexObject("lighting/lightV.glsl"))
            {
                return false;
            }
        }
    }

    // NOTE order of shader object attaching is VERY IMPORTANT!!!
    if (features->calculatesAtmospherics)
    {
        if (!shader->attachVertexObject("environment/srgbF.glsl")) // NOTE -- "F" suffix is superfluous here, there is nothing fragment specific in srgbF
        {
            return false;
        }

        if (!shader->attachVertexObject(variantObjectKey("windlight/atmosphericsFuncs.glsl", OBJ_AXIS_CLASSIC, shader, GL_VERTEX_SHADER))) {
            return false;
        }

        if (!shader->attachVertexObject("windlight/atmosphericsV.glsl"))
        {
            return false;
        }
    }

    if (features->hasSkinning)
    {
        if (!shader->attachVertexObject("avatar/avatarSkinV.glsl"))
        {
            return false;
        }
    }

    if (features->hasObjectSkinning)
    {
        // NOTE: the matching `mRiggedVariant = this` self-edge is set by createShader().
        if (!shader->attachVertexObject("avatar/objectSkinV.glsl"))
        {
            return false;
        }
    }

    if (!shader->attachVertexObject("deferred/textureUtilV.glsl"))
    {
        return false;
    }

    ///////////////////////////////////////
    // Attach Fragment Shader Features Next
    ///////////////////////////////////////

    // NOTE order of shader object attaching is VERY IMPORTANT!!!

    if (!shader->attachFragmentObject(variantObjectKey("deferred/globalF.glsl", OBJ_AXIS_MIRROR, shader, GL_FRAGMENT_SHADER)))
    {
        return false;
    }

    if (features->hasSrgb || features->hasAtmospherics || features->calculatesAtmospherics || features->isDeferred)
    {
        if (!shader->attachFragmentObject("environment/srgbF.glsl"))
        {
            return false;
        }
    }

    if(features->calculatesAtmospherics || features->hasGamma || features->isDeferred)
    {
        if (!shader->attachFragmentObject("windlight/atmosphericsVarsF.glsl"))
        {
            return false;
        }
    }

    if (features->calculatesLighting || features->calculatesAtmospherics)
    {
        if (!shader->attachFragmentObject("windlight/atmosphericsHelpersF.glsl"))
        {
            return false;
        }
    }

    // we want this BEFORE shadows and AO because those facilities use pos/norm access
    if (features->isDeferred || features->hasReflectionProbes)
    {
        if (!shader->attachFragmentObject(variantObjectKey("deferred/deferredUtil.glsl", OBJ_AXIS_CLASSIC, shader, GL_FRAGMENT_SHADER)))
        {
            return false;
        }
    }

    if (features->hasFullGBuffer)
    {
        if (!shader->attachFragmentObject("deferred/gbufferUtil.glsl"))
        {
            return false;
        }
    }

    if (features->hasScreenSpaceReflections || features->hasReflectionProbes)
    {
        if (!shader->attachFragmentObject("deferred/screenSpaceReflUtil.glsl"))
        {
            return false;
        }
    }

    if (features->hasShadows)
    {
        if (!shader->attachFragmentObject("deferred/shadowUtil.glsl"))
        {
            return false;
        }
    }

    if (features->hasReflectionProbes)
    {
        if (!shader->attachFragmentObject(variantObjectKey("deferred/reflectionProbeF.glsl", OBJ_AXIS_CLASSIC, shader, GL_FRAGMENT_SHADER)))
        {
            return false;
        }
    }

    if (features->hasAmbientOcclusion)
    {
        if (!shader->attachFragmentObject("deferred/aoUtil.glsl"))
        {
            return false;
        }
    }

    if (features->hasAtmospherics || features->isDeferred)
    {
        if (!shader->attachFragmentObject(variantObjectKey("windlight/atmosphericsFuncs.glsl", OBJ_AXIS_CLASSIC, shader, GL_FRAGMENT_SHADER))) {
            return false;
        }

        if (!shader->attachFragmentObject("windlight/atmosphericsF.glsl"))
        {
            return false;
        }
    }

    if (features->isPBRTerrain)
    {
        if (!shader->attachFragmentObject("deferred/pbrterrainUtilF.glsl"))
        {
            return false;
        }
    }

    if (features->hasTonemap)
    {
        if (!shader->attachFragmentObject("deferred/tonemapUtilF.glsl"))
        {
            return false;
        }
    }

    if (features->hasColorGrade)
    {
        if (!shader->attachFragmentObject("alchemy/colorGradeUtilF.glsl"))
        {
            return false;
        }
    }

    if (features->hasPostEffects)
    {
        if (!shader->attachFragmentObject("alchemy/postEffectUtilsF.glsl"))
        {
            return false;
        }
    }

    // NOTE order of shader object attaching is VERY IMPORTANT!!!
    if (features->hasAtmospherics)
    {
        if (!shader->attachFragmentObject("environment/waterFogF.glsl"))
        {
            return false;
        }
    }

    if (features->hasLighting)
    {
        if (features->mIndexedTextureChannels <= 1)
        {
            if (features->hasAlphaMask)
            {
                if (!shader->attachFragmentObject("lighting/lightAlphaMaskNonIndexedF.glsl"))
                {
                    return false;
                }
            }
            else
            {
                if (!shader->attachFragmentObject("lighting/lightNonIndexedF.glsl"))
                {
                    return false;
                }
            }
        }
        else
        {
            if (features->hasAlphaMask)
            {
                if (!shader->attachFragmentObject("lighting/lightAlphaMaskF.glsl"))
                {
                    return false;
                }
            }
            else
            {
                if (!shader->attachFragmentObject("lighting/lightF.glsl"))
                {
                    return false;
                }
            }
            shader->mFeatures.mIndexedTextureChannels = llmax(LLGLSLShader::sIndexedTextureChannels, 1);
        }
    }

    if (features->mIndexedTextureChannels <= 1)
    {
        if (!shader->attachVertexObject("objects/nonindexedTextureV.glsl"))
        {
            return false;
        }
    }
    else
    {
        if (!shader->attachVertexObject("objects/indexedTextureV.glsl"))
        {
            return false;
        }
    }

    return true;
}

//============================================================================
// Load Shader

static std::string get_shader_log(GLuint ret)
{
    std::string res;

    //get log length
    GLint length;
    glGetShaderiv(ret, GL_INFO_LOG_LENGTH, &length);
    if (length > 0)
    {
        //the log could be any size, so allocate appropriately
        GLchar* log = new GLchar[length];
        glGetShaderInfoLog(ret, length, &length, log);
        res = std::string((char *)log);
        delete[] log;
    }
    return res;
}

static std::string get_program_log(GLuint ret)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    std::string res;

    //get log length
    GLint length;
    glGetProgramiv(ret, GL_INFO_LOG_LENGTH, &length);
    if (length > 0)
    {
        //the log could be any size, so allocate appropriately
        GLchar* log = new GLchar[length];
        glGetProgramInfoLog(ret, length, &length, log);
        res = std::string((char*)log);
        delete[] log;
    }
    return res;
}

// get the info log for the given object, be it a shader or program object
// NOTE: ret MUST be a shader OR a program object
static std::string get_object_log(GLuint ret)
{
    if (glIsProgram(ret))
    {
        return get_program_log(ret);
    }
    else
    {
        llassert(glIsShader(ret));
        return get_shader_log(ret);
    }
}

//dump shader source for debugging
void LLShaderMgr::dumpShaderSource(U32 shader_code_count, GLchar** shader_code_text)
{
    char num_str[16]; // U32 = max 10 digits

    LL_SHADER_LOADING_WARNS() << "\n";

    for (U32 i = 0; i < shader_code_count; i++)
    {
        snprintf(num_str, sizeof(num_str), "%4d: ", i+1);
        std::string line_number(num_str);
        LL_CONT << line_number << shader_code_text[i];
    }
    LL_CONT << LL_ENDL;
}

void LLShaderMgr::dumpObjectLog(GLuint ret, bool warns, const std::string& filename)
{
    std::string log = get_object_log(ret);
    if (log.empty())
    {
        // Nothing the driver wanted to tell us -- a clean compile/link on a modern
        // core-profile driver returns an empty info log, so there is nothing to show.
        return;
    }

    const std::string fname = filename.empty() ? "unknown shader file" : filename;

    // warns == true  -> the compile / link / validate FAILED: this is the error log.
    // warns == false -> the object built successfully but the driver still emitted a
    //                   non-empty info log, i.e. these are compiler *warnings*. Surface
    //                   them either way (both at WARNS so they are actually visible)
    //                   but frame them so a warning isn't mistaken for a hard failure.
    if (warns)
    {
        LL_SHADER_LOADING_WARNS() << "Shader compiler error log for " << fname << ":\n" << log << LL_ENDL;
    }
    else
    {
        LL_WARNS("ShaderLoading") << "Shader compiler warning log for " << fname << ":\n" << log << LL_ENDL;
    }
}

GLuint LLShaderMgr::loadShaderFile(const std::string& filename, S32 & shader_level, GLenum type, std::map<std::string, std::string>* defines, S32 texture_index_channels, const std::string& cache_key)
{
    GLenum error = GL_NO_ERROR;

    error = glGetError();
    if (error != GL_NO_ERROR)
    {
        LL_SHADER_LOADING_WARNS() << "GL ERROR entering loadShaderFile(): " << error << " for file: " << filename << LL_ENDL;
    }

    if (filename.empty())
    {
        LL_WARNS("ShaderLoading") << "tried loading empty filename" << LL_ENDL;
        return 0;
    }


    //read in from file
    LLFILE* file = NULL;

    S32 try_gpu_class = shader_level;
    S32 gpu_class;

    std::string open_file_name;

#if 0  // WIP -- try to come up with a way to fallback to an error shader without needing debug stubs all over the place in the shader tree
    if (shader_level == -1)
    {
        // use "error" fallback
        if (type == GL_VERTEX_SHADER)
        {
            open_file_name = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "shaders/errorV.glsl");
        }
        else
        {
            llassert(type == GL_FRAGMENT_SHADER);  // type must be vertex or fragment shader
            open_file_name = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "shaders/errorF.glsl");
        }

        file = LLFile::fopen(open_file_name, LLFILE_MODE("r"));
    }
    else
#endif
    {
        //find the most relevant file
        for (gpu_class = try_gpu_class; gpu_class > 0; gpu_class--)
        {   //search from the current gpu class down to class 1 to find the most relevant shader
            std::stringstream fname;
            fname << getShaderDirPrefix();
            fname << gpu_class << gDirUtilp->getDirDelimiter() << filename;

            open_file_name = fname.str();

            /*
            Would be awesome, if we didn't have shaders that re-use files
            with different environments to say, add skinning, etc
            can't depend on cached version to have evaluate ifdefs identically...
            if we can define a deterministic hash for the shader based on
            all the inputs, maybe we can save some time here.
            if (mShaderObjects.count(filename) > 0)
            {
                return mShaderObjects[filename];
            }

            */

            LL_DEBUGS("ShaderLoading") << "Looking in " << open_file_name << LL_ENDL;
            file = LLFile::fopen(open_file_name, LLFILE_MODE("r")); /* Flawfinder: ignore */
            if (file)
            {
                LL_DEBUGS("ShaderLoading") << "Loading file: " << open_file_name << " (Want class " << gpu_class << ")" << LL_ENDL;
                break; // done
            }
        }
    }

    if (file == NULL)
    {
        if (gDirUtilp->fileExists(open_file_name))
        {
            LL_WARNS("ShaderLoading") << "GLSL Shader file failed to open: " << open_file_name << LL_ENDL;
        }
        else
        {
            LL_WARNS("ShaderLoading") << "GLSL Shader file not found: " << open_file_name << LL_ENDL;
        }
        return 0;
    }

    //we can't have any lines longer than 1024 characters
    //or any shaders longer than 4096 lines... deal - DaveP
    GLchar buff[1024];
    GLchar *extra_code_text[1024];
    GLchar *shader_code_text[4096 + LL_ARRAY_SIZE(extra_code_text)] = { NULL };
    GLuint extra_code_count = 0, shader_code_count = 0;
    BOOST_STATIC_ASSERT(LL_ARRAY_SIZE(extra_code_text) < LL_ARRAY_SIZE(shader_code_text));


    S32 major_version = gGLManager.mGLSLVersionMajor;
    S32 minor_version = gGLManager.mGLSLVersionMinor;

    if (major_version == 1 && minor_version < 30)
    {
        llassert(false); // GL 3.1 or later required
    }
    else
    {
        if (major_version >= 4)
        {
            //set version to 400 or 420
            if (minor_version >= 20)
            {
                shader_code_text[shader_code_count++] = strdup("#version 420\n");
            }
            else
            {
                shader_code_text[shader_code_count++] = strdup("#version 400\n");
            }
        }
        else if (major_version == 3)
        {
            if (minor_version <= 29)
            {
                // OpenGL 3.2 had GLSL version 1.50.  anything after that the version numbers match.
                // https://www.khronos.org/opengl/wiki/Core_Language_(GLSL)#OpenGL_and_GLSL_versions
                shader_code_text[shader_code_count++] = strdup("#version 150\n");
            }
            else
            {
                shader_code_text[shader_code_count++] = strdup("#version 330\n");
            }
        }
        else
        {
            // OpenGL 3.2 had GLSL version 1.50.  anything after that the version numbers match.
            if (type == GL_GEOMETRY_SHADER || minor_version >= 50)
            {
                //set version to 1.50
                shader_code_text[shader_code_count++] = strdup("#version 150\n");
                //some implementations of GLSL 1.30 require integer precision be explicitly declared
                extra_code_text[extra_code_count++] = strdup("precision mediump int;\n");
                extra_code_text[extra_code_count++] = strdup("precision highp float;\n");
            }
            else
            {
                //set version to 1.40
                shader_code_text[shader_code_count++] = strdup("#version 140\n");
                //some implementations of GLSL 1.30 require integer precision be explicitly declared
                extra_code_text[extra_code_count++] = strdup("precision mediump int;\n");
                extra_code_text[extra_code_count++] = strdup("precision highp float;\n");
            }
        }
    }

    if (type == GL_FRAGMENT_SHADER)
    {
        extra_code_text[extra_code_count++] = strdup("#define FRAGMENT_SHADER 1\n");
    }
    else
    {
        extra_code_text[extra_code_count++] = strdup("#define VERTEX_SHADER 1\n");
    }

    // Use alpha float to store bit flags
    // See: C++: addDeferredAttachment(), shader: frag_data[2]
    extra_code_text[extra_code_count++] = strdup("#define GBUFFER_FLAG_SKIP_ATMOS   0.0 \n"); // atmo kill
    extra_code_text[extra_code_count++] = strdup("#define GBUFFER_FLAG_HAS_ATMOS    0.34\n"); // bit 0
    extra_code_text[extra_code_count++] = strdup("#define GBUFFER_FLAG_HAS_PBR      0.67\n"); // bit 1
    extra_code_text[extra_code_count++] = strdup("#define GBUFFER_FLAG_HAS_HDRI      1.0\n");  // bit 2
    extra_code_text[extra_code_count++] = strdup("#define GET_GBUFFER_FLAG(data, flag)    (abs(data-flag)< 0.1)\n");

    // Bits available in the normal attachment's blue channel, which carries envIntensity for a
    // legacy fragment and a packed geometric normal for a PBR one -- 16 under GL_RGBA16, 10
    // under GL_RGB10_A2. Same reason as REVERSE_Z below: a property of the buffer the whole tree
    // writes, not of any one program, and a shader compiled against the wrong assumption
    // unpacks noise rather than degrading.
    if (LLRender::sGBufferNormHDR)
    {
        extra_code_text[extra_code_count++] = strdup("#define GBUFFER_NORM_HDR 1\n");
    }

    // Balances punctual lights against the legacy Blinn-Phong response, and has to be one value
    // across every program that lights a PBR surface -- the deferred local lights, the forward
    // alpha path and the sun all accumulate into the same frame. It lived as a literal in four
    // separate compilation units, where nothing tied the copies together and they drifted: the
    // three deferred light shaders carried 3.25 while deferredUtil carried 3.0, so a lamp lit an
    // alpha-blended surface and the opaque one behind it about 8% apart. A global const cannot
    // fix that -- GLSL globals do not link across objects -- so it is injected here, where the
    // GBuffer flags already are, for the same reason.
    //
    // 3.0 is the value to converge on, not 3.25, because deferredUtil's copy also scales the SUN
    // in pbrBaseLight -- which is what softenLightF lights every opaque surface with. Taking the
    // local lights' 3.25 would have moved the sun across the whole opaque scene in order to
    // settle a disagreement between two local-light paths. Only the deferred local lights move.
    extra_code_text[extra_code_count++] = strdup("#define PUNCTUAL_LIGHT_SCALE      3.0\n");
    // A guard against a divide that ran away, not a look control. At 10.0 it was neither: the
    // peak of F*Vis*D at the roughness floor is about 776 for a dielectric and 19400 for a
    // white metal, so a ceiling of 10 flat-topped the highlight on every dielectric below
    // roughness 0.13 and every metal below 0.30 -- most polished material in a scene -- and did
    // it in linear space ahead of exposure, pre-empting the tonemapper that exists to compress
    // exactly these values. This sits above anything the roughness floor permits, so it now
    // only catches a NaN or an infinity. Bloom's exposure to sub-pixel glints is bounded at
    // bloomExtractF instead, which is where that artifact is actually made.
    extra_code_text[extra_code_count++] = strdup("#define MAX_PUNCTUAL_RADIANCE     65504.0\n");

    // Reverse-Z is a property of the depth convention the whole tree rasterizes against, not of
    // any one program, so it is injected here instead of riding a defines map. The map
    // loadBasicShaders() passes reaches only the shared objects it compiles; a program's own
    // mShaderFiles are compiled with nothing but their permutations, so a source like
    // environment/waterV.glsl would otherwise take its forward branch against a reversed buffer
    // while the deferredUtil.glsl it links against took the reversed one.
    if (LLRender::sReverseZ)
    {
        extra_code_text[extra_code_count++] = strdup("#define REVERSE_Z 1\n");
    }

    if (defines)
    {
        for (auto iter = defines->begin(); iter != defines->end(); ++iter)
        {
            std::string define = "#define " + iter->first + " " + iter->second + "\n";
            extra_code_text[extra_code_count++] = (GLchar *) strdup(define.c_str());
        }
    }

    if( gGLManager.mIsAMD )
    {
        extra_code_text[extra_code_count++] = strdup( "#define IS_AMD_CARD 1\n" );
    }

    if (texture_index_channels > 0 && type == GL_FRAGMENT_SHADER)
    {
        //use specified number of texture channels for indexed texture rendering

        /* prepend shader code that looks like this:

        uniform sampler2D tex0;
        uniform sampler2D tex1;
        uniform sampler2D tex2;
        .
        .
        .
        uniform sampler2D texN;

        flat in int vary_texture_index;

        vec4 ret = vec4(1,0,1,1);

        vec4 diffuseLookup(vec2 texcoord)
        {
            switch (vary_texture_index)
            {
                case 0: ret = texture(tex0, texcoord); break;
                case 1: ret = texture(tex1, texcoord); break;
                case 2: ret = texture(tex2, texcoord); break;
                .
                .
                .
                case N: return texture(texN, texcoord); break;
            }

            return ret;
        }
        */

        extra_code_text[extra_code_count++] = strdup("#define HAS_DIFFUSE_LOOKUP\n");

        //uniform declartion
        for (S32 i = 0; i < texture_index_channels; ++i)
        {
            std::string decl = llformat("uniform sampler2D tex%d;\n", i);
            extra_code_text[extra_code_count++] = strdup(decl.c_str());
        }

        if (texture_index_channels > 1)
        {
            extra_code_text[extra_code_count++] = strdup("flat in int vary_texture_index;\n");
        }

        extra_code_text[extra_code_count++] = strdup("vec4 diffuseLookup(vec2 texcoord)\n");
        extra_code_text[extra_code_count++] = strdup("{\n");


        if (texture_index_channels == 1)
        { //don't use flow control, that's silly
            extra_code_text[extra_code_count++] = strdup("return texture(tex0, texcoord);\n");
            extra_code_text[extra_code_count++] = strdup("}\n");
        }
        else if (major_version > 1 || minor_version >= 30)
        {  //switches are supported in GLSL 1.30 and later
            if (gGLManager.mIsNVIDIA)
            { //switches are unreliable on some NVIDIA drivers
                for (S32 i = 0; i < texture_index_channels; ++i)
                {
                    std::string if_string = llformat("\t%sif (vary_texture_index == %d) { return texture(tex%d, texcoord); }\n", i > 0 ? "else " : "", i, i);
                    extra_code_text[extra_code_count++] = strdup(if_string.c_str());
                }
                extra_code_text[extra_code_count++] = strdup("\treturn vec4(1,0,1,1);\n");
                extra_code_text[extra_code_count++] = strdup("}\n");
            }
            else
            {
                extra_code_text[extra_code_count++] = strdup("\tvec4 ret = vec4(1,0,1,1);\n");
                extra_code_text[extra_code_count++] = strdup("\tswitch (vary_texture_index)\n");
                extra_code_text[extra_code_count++] = strdup("\t{\n");

                //switch body
                for (S32 i = 0; i < texture_index_channels; ++i)
                {
                    std::string case_str = llformat("\t\tcase %d: return texture(tex%d, texcoord);\n", i, i);
                    extra_code_text[extra_code_count++] = strdup(case_str.c_str());
                }

                extra_code_text[extra_code_count++] = strdup("\t}\n");
                extra_code_text[extra_code_count++] = strdup("\treturn ret;\n");
                extra_code_text[extra_code_count++] = strdup("}\n");
            }
        }
        else
        { //should never get here.  Indexed texture rendering requires GLSL 1.30 or later
            // (for passing integers between vertex and fragment shaders)
            LL_ERRS() << "Indexed texture rendering requires GLSL 1.30 or later." << LL_ENDL;
        }
    }

    // Master definition can be found in deferredUtil.glsl
    extra_code_text[extra_code_count++] = strdup("struct GBufferInfo { vec4 albedo; vec4 specular; vec3 normal; vec3 geoNormal; vec4 emissive; float gbufferFlag; float envIntensity; };\n");

    //copy file into memory
    enum {
          flag_write_to_out_of_extra_block_area = 0x01
        , flag_extra_block_marker_was_found = 0x02
    };

    unsigned char flags = flag_write_to_out_of_extra_block_area;

    GLuint out_of_extra_block_counter = 0, start_shader_code = shader_code_count, file_lines_count = 0;

#define TOUCH_SHADERS 0

#if TOUCH_SHADERS
    const char* marker = "// touched";
    bool touched = false;
#endif

    auto append_line = [&](const char* line)
    {
        if (shader_code_count >= (LL_ARRAY_SIZE(shader_code_text) - LL_ARRAY_SIZE(extra_code_text)))
        {
            return;
        }

        shader_code_text[shader_code_count] = (GLchar *)strdup(line);

        if(flag_write_to_out_of_extra_block_area & flags)
        {
            shader_code_text[extra_code_count + start_shader_code + out_of_extra_block_counter]
                = shader_code_text[shader_code_count];
            out_of_extra_block_counter++;

            if(out_of_extra_block_counter == extra_code_count)
            {
                shader_code_count += extra_code_count;
                flags &= ~flag_write_to_out_of_extra_block_area;
            }
        }

        ++shader_code_count;
    };

    while(NULL != fgets((char *)buff, 1024, file)
          && shader_code_count < (LL_ARRAY_SIZE(shader_code_text) - LL_ARRAY_SIZE(extra_code_text)))
    {
        file_lines_count++;

        bool extra_block_area_found = NULL != strstr((const char*)buff, "[EXTRA_CODE_HERE]");
        const char* engine_block_marker = strstr((const char*)buff, "[ENGINE_BLOCK ");

#if TOUCH_SHADERS
        if (NULL != strstr((const char*)buff, marker))
        {
            touched = true;
        }
#endif

        if(extra_block_area_found && !(flag_extra_block_marker_was_found & flags))
        {
            if(!(flag_write_to_out_of_extra_block_area & flags))
            {
                //shift
                for(GLuint to = start_shader_code, from = extra_code_count + start_shader_code;
                    from < shader_code_count; ++to, ++from)
                {
                    shader_code_text[to] = shader_code_text[from];
                }

                shader_code_count -= extra_code_count;
            }

            //copy extra code
            for(GLuint n = 0; n < extra_code_count
                && shader_code_count < (LL_ARRAY_SIZE(shader_code_text) - LL_ARRAY_SIZE(extra_code_text)); ++n)
            {
                shader_code_text[shader_code_count++] = extra_code_text[n];
            }

            extra_code_count = 0;

            flags &= ~flag_write_to_out_of_extra_block_area;
            flags |= flag_extra_block_marker_was_found;
        }
        else if (engine_block_marker)
        {
            // Shared engine-block declaration splice: a "//[ENGINE_BLOCK <Name>]" line
            // expands to the ONE canonical std140 declaration for that block
            // (class1/deferred/<name>Block.glsl). Every file that reads the block gets a
            // byte-identical copy -- required, since helper sources are separate compilation
            // units whose matching blocks must unify at link -- and none of them can drift
            // from each other or from the C++ mirror struct.
            std::string block_name(engine_block_marker + strlen("[ENGINE_BLOCK "));
            const size_t close = block_name.find(']');
            std::string snippet;
            if (close != std::string::npos)
            {
                block_name.resize(close);
                std::string snippet_rel = block_name;
                if (!snippet_rel.empty())
                {
                    snippet_rel[0] = (char)tolower((unsigned char)snippet_rel[0]);
                }
                const std::string snippet_path = getShaderDirPrefix() + "1" + gDirUtilp->getDirDelimiter()
                    + "deferred" + gDirUtilp->getDirDelimiter() + snippet_rel + "Block.glsl";
                std::error_code ec;
                snippet = LLFile::getContents(snippet_path, ec);
                if (ec)
                {
                    snippet.clear();
                }
            }

            if (snippet.empty())
            {
                // Loud but non-fatal: the compile that follows fails with
                // undefined-member errors right next to this warning.
                LL_WARNS("ShaderLoading") << "Engine block snippet missing for marker '"
                                          << (const char*)buff << "' in " << open_file_name << LL_ENDL;
                append_line((const char*)buff);
            }
            else
            {
                size_t start = 0;
                while (start < snippet.size())
                {
                    size_t nl = snippet.find('\n', start);
                    std::string line = (nl == std::string::npos) ? snippet.substr(start) + "\n"
                                                                 : snippet.substr(start, nl - start + 1);
                    append_line(line.c_str());
                    if (nl == std::string::npos)
                    {
                        break;
                    }
                    start = nl + 1;
                }
            }
        }
        else
        {
            append_line((const char*)buff);
        }
    } //while

    if(!(flag_extra_block_marker_was_found & flags))
    {
        for(GLuint n = start_shader_code; n < extra_code_count + start_shader_code; ++n)
        {
            shader_code_text[n] = extra_code_text[n - start_shader_code];
        }

        if (file_lines_count < extra_code_count)
        {
            shader_code_count += extra_code_count;
        }

        extra_code_count = 0;
    }

#if TOUCH_SHADERS
    if (!touched)
    {
        fprintf(file, "\n%s\n", marker);
    }
#endif

    fclose(file);

    //create shader object
    GLuint ret = glCreateShader(type);

    error = glGetError();
    if (error != GL_NO_ERROR)
    {
        LL_WARNS("ShaderLoading") << "GL ERROR in glCreateShader: " << error << " for file: " << open_file_name << LL_ENDL;
        if (ret)
        {
            glDeleteShader(ret); //no longer need handle
            ret = 0;
        }
    }

    //load source
    if (ret)
    {
        LL_DEBUGS("ShaderLoading") << "glCreateShader done" << LL_ENDL;
        glShaderSource(ret, shader_code_count, (const GLchar**)shader_code_text, NULL);

        error = glGetError();
        if (error != GL_NO_ERROR)
        {
            LL_WARNS("ShaderLoading") << "GL ERROR in glShaderSource: " << error << " for file: " << open_file_name << LL_ENDL;
            glDeleteShader(ret); //no longer need handle
            ret = 0;
        }
    }

    //compile source
    if (ret)
    {
        LL_DEBUGS("ShaderLoading") << "glShaderSource done" << U32(ret) << LL_ENDL;
        glCompileShader(ret);

        error = glGetError();
        if (error != GL_NO_ERROR)
        {
            LL_WARNS("ShaderLoading") << "GL ERROR in glCompileShader: " << error << " for file: " << open_file_name << LL_ENDL;
            glDeleteShader(ret); //no longer need handle
            ret = 0;
        }
    }

    if (error == GL_NO_ERROR)
    {
        //check for errors
        LL_DEBUGS("ShaderLoading") << "glCompileShader done" << U32(ret) << LL_ENDL;
        GLint success = GL_TRUE;
        glGetShaderiv(ret, GL_COMPILE_STATUS, &success);

        error = glGetError();
        if (error != GL_NO_ERROR || success == GL_FALSE)
        {
            //an error occured, print log
            LL_WARNS("ShaderLoading") << "GLSL Compilation Error:" << LL_ENDL;
            dumpObjectLog(ret, true, open_file_name);
            dumpShaderSource(shader_code_count, shader_code_text);
            glDeleteShader(ret); //no longer need handle
            ret = 0;
        }
        else
        {
            // Compiled OK, but the driver may still have emitted warnings into the
            // info log (deprecated usage, precision mismatches, unused varyings...).
            // dumpObjectLog() no-ops on an empty log, so this is free on clean builds.
            dumpObjectLog(ret, false, open_file_name);
        }
    }
    else
    {
        LL_DEBUGS("ShaderLoading") << "loadShaderFile() completed, ret: " << U32(ret) << LL_ENDL;
        ret = 0;
    }
    stop_glerror();

    //free memory
    for (GLuint i = 0; i < shader_code_count; i++)
    {
        free(shader_code_text[i]);
    }

    //successfully loaded, save results
    if (ret)
    {
        // Add shader file to map. The key is the path unless the caller asked for a distinct
        // one -- a shared object compiled a second time under different defines needs its own
        // entry, since attach is by key (see variantObjectKey).
        const std::string& key = cache_key.empty() ? filename : cache_key;
        if (type == GL_VERTEX_SHADER) {
            mVertexShaderObjects[key] = ret;
        }
        else if (type == GL_FRAGMENT_SHADER) {
            mFragmentShaderObjects[key] = ret;
        }
        shader_level = try_gpu_class;
    }
    else
    {
        if (shader_level > 1)
        {
            shader_level--;
            return loadShaderFile(filename, shader_level, type, defines, texture_index_channels, cache_key);
        }
        LL_WARNS("ShaderLoading") << "Failed to load " << filename << LL_ENDL;
    }

    LL_DEBUGS("ShaderLoading") << "loadShaderFile() completed, ret: " << U32(ret) << LL_ENDL;
    return ret;
}

bool LLShaderMgr::linkProgramObject(GLuint obj, bool suppress_errors, const std::string& shader_name)
{
    //check for errors
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_SHADER("glLinkProgram");
        glLinkProgram(obj);
    }

    GLint success = GL_TRUE;

    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_SHADER("glsl check link status");
        glGetProgramiv(obj, GL_LINK_STATUS, &success);
        if (!suppress_errors && success == GL_FALSE)
        {
            //an error occured, print log
            LL_SHADER_LOADING_WARNS() << "GLSL Linker Error:" << LL_ENDL;
            dumpObjectLog(obj, true, "linker: " + shader_name);
            return success;
        }
        else if (success == GL_TRUE)
        {
            // Linked OK -- surface any non-fatal linker warnings the driver produced.
            // Skipped while suppress_errors is set (a probe/fallback link the caller
            // expects may fail and doesn't want narrated). No-ops on an empty log.
            if (!suppress_errors)
            {
                dumpObjectLog(obj, false, "linker: " + shader_name);
            }
        }
    }

    return success;
}

bool LLShaderMgr::validateProgramObject(GLuint obj)
{
    //check program validity against current GL
    glValidateProgram(obj);
    GLint success = GL_TRUE;
    glGetProgramiv(obj, GL_VALIDATE_STATUS, &success);
    if (success == GL_FALSE)
    {
        LL_SHADER_LOADING_WARNS() << "GLSL program not valid: " << LL_ENDL;
        dumpObjectLog(obj);
    }
    else
    {
        dumpObjectLog(obj, false);
    }

    return success;
}

void LLShaderMgr::initShaderCache(bool enabled, const LLUUID& old_cache_version, const LLUUID& current_cache_version, bool second_instance)
{
    LL_PROFILE_ZONE_SCOPED;
    LL_INFOS("ShaderMgr") << "Initializing shader cache" << LL_ENDL;

    mShaderCacheEnabled = gGLManager.mGLVersion >= 4.09 && enabled;

    // A 4.1 version string (or ARB_get_program_binary) is necessary but NOT
    // sufficient to actually retrieve/restore program binaries, and assuming it is
    // is a crash: the entry points have to have resolved AND the driver has to
    // expose at least one program binary format. Real drivers violate this --
    // Apple's GL reports GL_NUM_PROGRAM_BINARY_FORMATS == 0, and some virtualized /
    // remote GPUs advertise 4.1 yet return null entry points -- in which case
    // glProgramParameteri / glGetProgramBinary / glProgramBinary are unusable and
    // touching them (as loadCachedProgramBinary / saveCachedProgramBinary do on
    // every program) faults. Verify capability once here and disable the cache
    // rather than crash later. glGetProgramiv stays valid regardless, but with no
    // format it can only ever report a 0-length binary, so the cache is pointless.
    if (mShaderCacheEnabled)
    {
        GLint num_binary_formats = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &num_binary_formats);
        const bool have_entry_points = glProgramParameteri && glGetProgramBinary && glProgramBinary;
        if (!have_entry_points || num_binary_formats <= 0)
        {
            LL_INFOS("ShaderMgr") << "Program binary cache disabled: "
                                  << (have_entry_points ? "driver exposes " : "missing GL entry points; ")
                                  << num_binary_formats << " program binary format(s)." << LL_ENDL;
            mShaderCacheEnabled = false;
        }
    }

    if(!mShaderCacheEnabled || mShaderCacheVersion.notNull())
        return;

    mShaderCacheVersion = current_cache_version;

    mShaderCacheDir = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "shader_cache");
    LLFile::mkdir(mShaderCacheDir);
    {
        std::string meta_out_path = gDirUtilp->add(mShaderCacheDir, "shaderdata.llsd");
        if (gDirUtilp->fileExists(meta_out_path))
        {
            LL_PROFILE_ZONE_NAMED("shader_cache");
            LL_INFOS("ShaderMgr") << "Loading shader cache metadata" << LL_ENDL;

            llifstream instream(meta_out_path, std::ifstream::in | std::ifstream::binary);
            LLSD in_data;
            try
            {
                LLSDSerialize::fromBinary(in_data, instream, LLSDSerialize::SIZE_UNLIMITED);
            }
            catch( std::bad_alloc& )
            {
                // Try to get a bit more memory back before we try to clear the cache.
                in_data.clear();
                // Just in case it was somehow the cause, clear cache.
                clearShaderCache();
                // If user run out of memory this early in init,
                // we don't want to keep going just to crash again.
                // Notify user and close.
                LLError::LLUserWarningMsg::showOutOfMemory();
                LL_ERRS("ShaderMgr") << "Failed to parse shader cache metadata, potentially due to size. Purged cache." << LL_ENDL;
                return;
            }
            instream.close();

            if (old_cache_version == current_cache_version
                && in_data["version"].asUUID() == current_cache_version)
            {
                for (const auto& data_pair : llsd::inMap(in_data["shaders"]))
                {
                    ProgramBinaryData binary_info = ProgramBinaryData();
                    binary_info.mBinaryFormat = data_pair.second["binary_format"].asInteger();
                    binary_info.mBinaryLength = data_pair.second["binary_size"].asInteger();
                    binary_info.mLastUsedTime = (F32)data_pair.second["last_used"].asReal();
                    mShaderBinaryCache.insert_or_assign(LLUUID(data_pair.first), binary_info);
                }
            }
            else if (!second_instance)
            {
                LL_INFOS("ShaderMgr") << "Shader cache version mismatch detected. Purging." << LL_ENDL;
                clearShaderCache();
            }
            else
            {
                LL_INFOS("ShaderMgr") << "Shader cache version mismatch detected." << LL_ENDL;
            }
        }
    }
}

void LLShaderMgr::clearShaderCache()
{
    std::string shader_cache = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "shader_cache");
    LL_INFOS("ShaderMgr") << "Removing shader cache at " << shader_cache << LL_ENDL;
    const std::string mask = "*";
    gDirUtilp->deleteFilesInDir(shader_cache, mask);
    mShaderBinaryCache.clear();
}

void LLShaderMgr::persistShaderCacheMetadata()
{
    if(!mShaderCacheEnabled) return;
    if (mShaderCacheVersion.isNull())
    {
        LL_WARNS("ShaderMgr") << "Attempted to save shader cache with no version set" << LL_ENDL;
        return;
    }

    LL_INFOS("ShaderMgr") << "Persisting shader cache metadata to disk" << LL_ENDL;

    LLSD out;
    // Settings and shader cache get saved at different time, thus making
    // RenderShaderCacheVersion unreliable when running multiple viewer
    // instances, or for cases where viewer crashes before saving settings.
    // Duplicate version to the cache itself.
    out["version"] = mShaderCacheVersion;
    out["shaders"] = LLSD::emptyMap();
    LLSD &shaders = out["shaders"];

    static const F32 LRU_TIME = (60.f * 60.f) * 24.f * 7.f; // 14 days
    const F32 current_time = (F32)LLTimer::getTotalSeconds();
    for (auto it = mShaderBinaryCache.begin(); it != mShaderBinaryCache.end();)
    {
        const ProgramBinaryData& shader_metadata = it->second;
        if ((shader_metadata.mLastUsedTime + LRU_TIME) < current_time)
        {
            std::string shader_path = gDirUtilp->add(mShaderCacheDir, it->first.asString() + ".shaderbin");
            LLFile::remove(shader_path);
            it = mShaderBinaryCache.erase(it);
        }
        else
        {
            LLSD data = LLSD::emptyMap();
            data["binary_format"] = LLSD::Integer(shader_metadata.mBinaryFormat);
            data["binary_size"] = LLSD::Integer(shader_metadata.mBinaryLength);
            data["last_used"] = LLSD::Real(shader_metadata.mLastUsedTime);
            shaders[it->first.asString()] = data;
            ++it;
        }
    }

    std::string meta_out_path = gDirUtilp->add(mShaderCacheDir, "shaderdata.llsd");
    llofstream outstream(meta_out_path, std::ios_base::out | std::ios_base::binary);
    if (!outstream.is_open())
    {
        LL_WARNS("ShaderMgr") << "Failed to open file. Unable to save shader cache to: " << mShaderCacheDir << LL_ENDL;
        return;
    }
    LLSDSerialize::toBinary(out, outstream);
    outstream.close();
}

bool LLShaderMgr::loadCachedProgramBinary(LLGLSLShader* shader)
{
    if (!mShaderCacheEnabled) return false;

    // Don't touch a program that failed to allocate: the recreate paths in
    // createShader() call glCreateProgram() and land here without re-checking for 0,
    // and glProgramParameteri on a non-program name is undefined.
    if (shader->mProgramObject == 0)
    {
        return false;
    }

    glProgramParameteri(shader->mProgramObject, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);

    auto binary_iter = mShaderBinaryCache.find(shader->mShaderHash);
    if (binary_iter != mShaderBinaryCache.end())
    {
        std::string in_path = gDirUtilp->add(mShaderCacheDir, shader->mShaderHash.asString() + ".shaderbin");
        auto& shader_info = binary_iter->second;
        if (shader_info.mBinaryLength > 0)
        {
            std::vector<U8> in_data;
            in_data.resize(shader_info.mBinaryLength);
            std::error_code ec;
            LLFile filep = LLFile(in_path, LLFile::in | LLFile::binary, ec);
            if (!ec && (bool)filep)
            {
                size_t result = filep.read(in_data.data(), in_data.size(), ec);
                filep.close();

                if (result == in_data.size())
                {
                    GLenum error = glGetError(); // Clear current error
                    glProgramBinary(shader->mProgramObject, shader_info.mBinaryFormat, in_data.data(), shader_info.mBinaryLength);

                    error = glGetError();
                    GLint success = GL_TRUE;
                    glGetProgramiv(shader->mProgramObject, GL_LINK_STATUS, &success);
                    if (error == GL_NO_ERROR && success == GL_TRUE)
                    {
                        binary_iter->second.mLastUsedTime = (F32)LLTimer::getTotalSeconds();
                        LL_INFOS() << "Loaded cached binary for shader: " << shader->mName << LL_ENDL;
                        return true;
                    }
                }
            }
        }
        //an error occured, normally we would print log but in this case it means the shader needs recompiling.
        LL_INFOS() << "Failed to load cached binary for shader: " << shader->mName << " falling back to compilation" << LL_ENDL;
        LLFile::remove(in_path);
        mShaderBinaryCache.erase(binary_iter);
    }
    return false;
}

bool LLShaderMgr::saveCachedProgramBinary(LLGLSLShader* shader)
{
    if (!mShaderCacheEnabled) return true;

    // mShaderCacheEnabled guarantees the entry points resolved and the driver
    // reports >=1 binary format (see initShaderCache). A program we never created /
    // linked is still off-limits: glGetProgramiv on 0 or a non-program name is
    // undefined and faults on some drivers, which is the crash this guard prevents.
    if (shader->mProgramObject == 0)
    {
        return false;
    }

    ProgramBinaryData binary_info = ProgramBinaryData();

    glGetError(); // flush any stale error so the check below reflects this query
    glGetProgramiv(shader->mProgramObject, GL_PROGRAM_BINARY_LENGTH, &binary_info.mBinaryLength);
    if (glGetError() != GL_NO_ERROR || binary_info.mBinaryLength <= 0)
    {
        // Query failed (e.g. the retrievable hint wasn't honored) or the driver has
        // no binary to hand back -- either way there's nothing safe to store.
        return false;
    }

    std::vector<U8> program_binary;
    program_binary.resize(binary_info.mBinaryLength);

    glGetProgramBinary(shader->mProgramObject, static_cast<GLsizei>(program_binary.size() * sizeof(U8)), nullptr, &binary_info.mBinaryFormat, program_binary.data());
    if (glGetError() == GL_NO_ERROR)
    {
        std::string out_path = gDirUtilp->add(mShaderCacheDir, shader->mShaderHash.asString() + ".shaderbin");
        std::error_code ec;
        LLFile filep = LLFile(out_path, LLFile::out | LLFile::binary, ec);
        if (filep)
        {
            filep.write(program_binary.data(), program_binary.size(), ec);
            filep.close();

            binary_info.mLastUsedTime = (F32)LLTimer::getTotalSeconds();

            mShaderBinaryCache.insert_or_assign(shader->mShaderHash, binary_info);
            return true;
        }
    }
    return false;
}

//virtual
void LLShaderMgr::initAttribsAndUniforms()
{
    //MUST match order of enum in LLVertexBuffer.h
    mReservedAttribs.push_back("position");
    mReservedAttribs.push_back("normal");
    mReservedAttribs.push_back("texcoord0");
    mReservedAttribs.push_back("texcoord1");
    mReservedAttribs.push_back("texcoord2");
    mReservedAttribs.push_back("texcoord3");
    mReservedAttribs.push_back("diffuse_color");
    mReservedAttribs.push_back("emissive");
    mReservedAttribs.push_back("tangent");
    mReservedAttribs.push_back("weight");
    mReservedAttribs.push_back("weight4");
    mReservedAttribs.push_back("clothing");
    mReservedAttribs.push_back("joint");
    mReservedAttribs.push_back("texture_index");

    //matrix state
    mReservedUniforms.push_back("modelview_matrix");
    mReservedUniforms.push_back("projection_matrix");
    mReservedUniforms.push_back("inv_proj");
    mReservedUniforms.push_back("modelview_projection_matrix");
    mReservedUniforms.push_back("inv_modelview");
    mReservedUniforms.push_back("identity_matrix");
    mReservedUniforms.push_back("normal_matrix");
    mReservedUniforms.push_back("texture_matrix0");
    mReservedUniforms.push_back("texture_matrix1");
    mReservedUniforms.push_back("object_plane_s");
    mReservedUniforms.push_back("object_plane_t");

    mReservedUniforms.push_back("texture_base_color_transform"); // (GLTF)
    mReservedUniforms.push_back("texture_normal_transform"); // (GLTF)
    mReservedUniforms.push_back("texture_metallic_roughness_transform"); // (GLTF)
    mReservedUniforms.push_back("texture_emissive_transform"); // (GLTF)
    mReservedUniforms.push_back("base_color_texcoord"); // (GLTF)
    mReservedUniforms.push_back("emissive_texcoord"); // (GLTF)
    mReservedUniforms.push_back("normal_texcoord"); // (GLTF)
    mReservedUniforms.push_back("metallic_roughness_texcoord"); // (GLTF)

    mReservedUniforms.push_back("terrain_texture_transforms"); // (GLTF)

    llassert(mReservedUniforms.size() == LLShaderMgr::TERRAIN_TEXTURE_TRANSFORMS +1);

    mReservedUniforms.push_back("viewport");

    mReservedUniforms.push_back("light_position");
    mReservedUniforms.push_back("light_direction");
    mReservedUniforms.push_back("light_attenuation");
    mReservedUniforms.push_back("light_deferred_attenuation");
    mReservedUniforms.push_back("light_diffuse");
    mReservedUniforms.push_back("light_ambient");
    mReservedUniforms.push_back("light_count");
    mReservedUniforms.push_back("light");
    mReservedUniforms.push_back("light_col");
    mReservedUniforms.push_back("far_z");

    llassert(mReservedUniforms.size() == LLShaderMgr::MULTI_LIGHT_FAR_Z+1);

    //NOTE: MUST match order in eGLSLReservedUniforms
    mReservedUniforms.push_back("proj_mat");
    mReservedUniforms.push_back("proj_near");
    mReservedUniforms.push_back("proj_p");
    mReservedUniforms.push_back("proj_n");
    mReservedUniforms.push_back("proj_origin");
    mReservedUniforms.push_back("proj_range");
    mReservedUniforms.push_back("proj_ambiance");
    mReservedUniforms.push_back("proj_shadow_idx");
    mReservedUniforms.push_back("shadow_fade");
    mReservedUniforms.push_back("proj_focus");
    mReservedUniforms.push_back("proj_lod");
    mReservedUniforms.push_back("proj_ambient_lod");

    llassert(mReservedUniforms.size() == LLShaderMgr::PROJECTOR_AMBIENT_LOD+1);

    mReservedUniforms.push_back("color");
    mReservedUniforms.push_back("emissiveColor");
    mReservedUniforms.push_back("metallicFactor");
    mReservedUniforms.push_back("roughnessFactor");
    mReservedUniforms.push_back("clipPlane");
    mReservedUniforms.push_back("clipSign");

    mReservedUniforms.push_back("diffuseMap");
    mReservedUniforms.push_back("altDiffuseMap");
    mReservedUniforms.push_back("specularMap");
    mReservedUniforms.push_back("metallicRoughnessMap");
    mReservedUniforms.push_back("normalMap");
    mReservedUniforms.push_back("emissiveMap");
    mReservedUniforms.push_back("bumpMap");
    mReservedUniforms.push_back("bumpMap2");
    mReservedUniforms.push_back("environmentMap");
    mReservedUniforms.push_back("sceneMap");
    mReservedUniforms.push_back("sceneDepth");
    mReservedUniforms.push_back("reflectionProbes");
    mReservedUniforms.push_back("shCoeffs");
    mReservedUniforms.push_back("heroProbes");
    mReservedUniforms.push_back("cloud_noise_texture");
    mReservedUniforms.push_back("cloud_noise_texture_next");
    mReservedUniforms.push_back("lightnorm");
    mReservedUniforms.push_back("sunlight_color");
    mReservedUniforms.push_back("ambient_color");
    mReservedUniforms.push_back("sky_hdr_scale");
    mReservedUniforms.push_back("sky_sunlight_scale");
    mReservedUniforms.push_back("sky_ambient_scale");
    mReservedUniforms.push_back("blue_horizon");
    mReservedUniforms.push_back("blue_density");
    mReservedUniforms.push_back("haze_horizon");
    mReservedUniforms.push_back("haze_density");
    mReservedUniforms.push_back("cloud_shadow");
    mReservedUniforms.push_back("density_multiplier");
    mReservedUniforms.push_back("distance_multiplier");
    mReservedUniforms.push_back("max_y");
    mReservedUniforms.push_back("glow");
    mReservedUniforms.push_back("cloud_color");
    mReservedUniforms.push_back("cloud_pos_density1");
    mReservedUniforms.push_back("cloud_pos_density2");
    mReservedUniforms.push_back("cloud_scale");
    mReservedUniforms.push_back("gamma");
    mReservedUniforms.push_back("scene_light_strength");

    llassert(mReservedUniforms.size() == LLShaderMgr::SCENE_LIGHT_STRENGTH+1);

    mReservedUniforms.push_back("center");
    mReservedUniforms.push_back("size");
    mReservedUniforms.push_back("falloff");

    mReservedUniforms.push_back("box_center");
    mReservedUniforms.push_back("box_size");


    mReservedUniforms.push_back("minLuminance");
    mReservedUniforms.push_back("maxExtractAlpha");
    mReservedUniforms.push_back("lumWeights");
    mReservedUniforms.push_back("warmthWeights");
    mReservedUniforms.push_back("warmthAmount");
    mReservedUniforms.push_back("glowStrength");
    mReservedUniforms.push_back("glowDelta");
    mReservedUniforms.push_back("glowNoiseMap");

    llassert(mReservedUniforms.size() == LLShaderMgr::GLOW_NOISE_MAP+1);

    mReservedUniforms.push_back("bloom_threshold");
    mReservedUniforms.push_back("bloom_knee");
    mReservedUniforms.push_back("bloom_firefly_clamp");
    mReservedUniforms.push_back("bloom_texel_size");
    mReservedUniforms.push_back("bloom_scatter");
    mReservedUniforms.push_back("bloom_strength");
    mReservedUniforms.push_back("alpha_glow_boost");
    mReservedUniforms.push_back("bloomMap");
    mReservedUniforms.push_back("halation_strength");
    mReservedUniforms.push_back("halation_tint");
    mReservedUniforms.push_back("halation_lum_weights");

    llassert(mReservedUniforms.size() == LLShaderMgr::HALATION_LUM_WEIGHTS+1);


    mReservedUniforms.push_back("minimum_alpha");
    mReservedUniforms.push_back("emissive_brightness");

    // Deferred
    mReservedUniforms.push_back("shadow_matrix");
    mReservedUniforms.push_back("env_mat");
    mReservedUniforms.push_back("shadow_clip");
    mReservedUniforms.push_back("ssao_radius");
    mReservedUniforms.push_back("ssao_max_radius");
    mReservedUniforms.push_back("ssao_factor");
    mReservedUniforms.push_back("ssao_factor_inv");
    mReservedUniforms.push_back("ssao_effect_mat");
    mReservedUniforms.push_back("ssao_irradiance_scale");
    mReservedUniforms.push_back("ssao_irradiance_max");
    mReservedUniforms.push_back("screen_res");
    mReservedUniforms.push_back("near_clip");
    mReservedUniforms.push_back("shadow_offset");
    mReservedUniforms.push_back("shadow_bias");
    mReservedUniforms.push_back("spot_shadow_bias");
    mReservedUniforms.push_back("spot_shadow_offset");
    mReservedUniforms.push_back("sun_dir");
    mReservedUniforms.push_back("moon_dir");
    mReservedUniforms.push_back("shadow_res");
    mReservedUniforms.push_back("proj_shadow_res");
    mReservedUniforms.push_back("depth_cutoff");
    mReservedUniforms.push_back("norm_cutoff");
    mReservedUniforms.push_back("shadow_target_width");

    llassert(mReservedUniforms.size() == LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH + 1);

    mReservedUniforms.push_back("iterationCount");
    mReservedUniforms.push_back("rayStep");
    mReservedUniforms.push_back("distanceBias");
    mReservedUniforms.push_back("depthRejectBias");
    mReservedUniforms.push_back("glossySampleCount");
    mReservedUniforms.push_back("noiseSine");
    mReservedUniforms.push_back("adaptiveStepMultiplier");

    mReservedUniforms.push_back("modelview_delta");
    mReservedUniforms.push_back("inv_modelview_delta");
    mReservedUniforms.push_back("cube_snapshot");

    mReservedUniforms.push_back("tc_scale");
    mReservedUniforms.push_back("rcp_screen_res");
    mReservedUniforms.push_back("rcp_frame_opt");
    mReservedUniforms.push_back("rcp_frame_opt2");

    mReservedUniforms.push_back("focal_distance");
    mReservedUniforms.push_back("blur_constant");
    mReservedUniforms.push_back("tan_pixel_angle");
    mReservedUniforms.push_back("magnification");
    mReservedUniforms.push_back("max_cof");
    mReservedUniforms.push_back("res_scale");
    mReservedUniforms.push_back("dof_width");
    mReservedUniforms.push_back("dof_height");

    mReservedUniforms.push_back("depthMap");
    mReservedUniforms.push_back("shadowMap0");
    mReservedUniforms.push_back("shadowMap1");
    mReservedUniforms.push_back("shadowMap2");
    mReservedUniforms.push_back("shadowMap3");
    mReservedUniforms.push_back("shadowMap4");
    mReservedUniforms.push_back("shadowMap5");

    llassert(mReservedUniforms.size() == LLShaderMgr::DEFERRED_SHADOW5+1);

    mReservedUniforms.push_back("diffuseRect");
    mReservedUniforms.push_back("specularRect");
    mReservedUniforms.push_back("emissiveRect");
    mReservedUniforms.push_back("exposureMap");
    mReservedUniforms.push_back("brdfLut");
    mReservedUniforms.push_back("noiseMap");
    mReservedUniforms.push_back("lightMap");
    mReservedUniforms.push_back("projectionMap");
    mReservedUniforms.push_back("norm_mat");
    mReservedUniforms.push_back("impostor_norm_rot");

    mReservedUniforms.push_back("specular_color");
    mReservedUniforms.push_back("env_intensity");

    mReservedUniforms.push_back("matrixPalette");
    mReservedUniforms.push_back("skin_origin");

    mReservedUniforms.push_back("screenTex");
    mReservedUniforms.push_back("exclusionTex");
    mReservedUniforms.push_back("eyeVec");
    mReservedUniforms.push_back("time");
    mReservedUniforms.push_back("waveDir1");
    mReservedUniforms.push_back("waveDir2");
    mReservedUniforms.push_back("lightDir");
    mReservedUniforms.push_back("specular");
    mReservedUniforms.push_back("lightExp");
    mReservedUniforms.push_back("waterFogColor");
    mReservedUniforms.push_back("waterFogColorLinear");
    mReservedUniforms.push_back("waterFogDensity");
    mReservedUniforms.push_back("waterFogKS");
    mReservedUniforms.push_back("refScale");
    mReservedUniforms.push_back("waterHeight");
    mReservedUniforms.push_back("waterPlane");
    mReservedUniforms.push_back("waterSign");
    mReservedUniforms.push_back("normScale");
    mReservedUniforms.push_back("fresnelScale");
    mReservedUniforms.push_back("fresnelOffset");
    mReservedUniforms.push_back("blurMultiplier");
    mReservedUniforms.push_back("above_water");
    llassert(mReservedUniforms.size() == LLShaderMgr::WATER_ABOVE_WATER + 1);

    mReservedUniforms.push_back("camPosLocal");
// [RLVa:KB] - @setsphere
    mReservedUniforms.push_back("rlvEffectMode");
    mReservedUniforms.push_back("rlvEffectParam1");
    mReservedUniforms.push_back("rlvEffectParam2");
    mReservedUniforms.push_back("rlvEffectParam3");
    mReservedUniforms.push_back("rlvEffectParam4");
    mReservedUniforms.push_back("rlvEffectParam5");
// [/RLV:KB]

    mReservedUniforms.push_back("gWindDir");
    mReservedUniforms.push_back("gSinWaveParams");
    mReservedUniforms.push_back("gGravity");

    mReservedUniforms.push_back("detail_0");
    mReservedUniforms.push_back("detail_1");
    mReservedUniforms.push_back("detail_2");
    mReservedUniforms.push_back("detail_3");

    mReservedUniforms.push_back("alpha_ramp");
    mReservedUniforms.push_back("paint_map");

    mReservedUniforms.push_back("detail_0_base_color");
    mReservedUniforms.push_back("detail_1_base_color");
    mReservedUniforms.push_back("detail_2_base_color");
    mReservedUniforms.push_back("detail_3_base_color");
    mReservedUniforms.push_back("detail_0_normal");
    mReservedUniforms.push_back("detail_1_normal");
    mReservedUniforms.push_back("detail_2_normal");
    mReservedUniforms.push_back("detail_3_normal");
    mReservedUniforms.push_back("detail_0_metallic_roughness");
    mReservedUniforms.push_back("detail_1_metallic_roughness");
    mReservedUniforms.push_back("detail_2_metallic_roughness");
    mReservedUniforms.push_back("detail_3_metallic_roughness");
    mReservedUniforms.push_back("detail_0_emissive");
    mReservedUniforms.push_back("detail_1_emissive");
    mReservedUniforms.push_back("detail_2_emissive");
    mReservedUniforms.push_back("detail_3_emissive");

    mReservedUniforms.push_back("baseColorFactors");
    mReservedUniforms.push_back("metallicFactors");
    mReservedUniforms.push_back("roughnessFactors");
    mReservedUniforms.push_back("emissiveColors");
    mReservedUniforms.push_back("minimum_alphas");

    mReservedUniforms.push_back("region_scale");
    llassert(mReservedUniforms.size() == LLShaderMgr::REGION_SCALE + 1);

    mReservedUniforms.push_back("gltf_minimum_alpha");
    mReservedUniforms.push_back("gltf_basecolor_transform");
    mReservedUniforms.push_back("gltf_emissive_color");
    mReservedUniforms.push_back("gltf_emissive_transform");
    mReservedUniforms.push_back("gltf_roughness_factor");
    mReservedUniforms.push_back("gltf_metallic_factor");
    mReservedUniforms.push_back("gltf_normal_transform");
    mReservedUniforms.push_back("gltf_mr_transform");

    mReservedUniforms.push_back("mat_specular_color");
    mReservedUniforms.push_back("mat_env_intensity");
    mReservedUniforms.push_back("mat_minimum_alpha");
    mReservedUniforms.push_back("mat_emissive_brightness");

    mReservedUniforms.push_back("origin");
    mReservedUniforms.push_back("display_gamma");


    mReservedUniforms.push_back("blend_factor");
    mReservedUniforms.push_back("moisture_level");
    mReservedUniforms.push_back("droplet_radius");
    mReservedUniforms.push_back("ice_level");
    mReservedUniforms.push_back("rainbow_map");
    mReservedUniforms.push_back("halo_map");
    mReservedUniforms.push_back("moon_brightness");
    mReservedUniforms.push_back("cloud_variance");
    mReservedUniforms.push_back("reflection_probe_ambiance");
    mReservedUniforms.push_back("max_probe_lod");
    mReservedUniforms.push_back("probe_strength");

    mReservedUniforms.push_back("resScale");
    mReservedUniforms.push_back("direction");
    mReservedUniforms.push_back("znear");
    mReservedUniforms.push_back("zfar");
    mReservedUniforms.push_back("sourceIdx");
    mReservedUniforms.push_back("mipLevel");
    mReservedUniforms.push_back("roughness");
    mReservedUniforms.push_back("u_width");


    mReservedUniforms.push_back("sun_moon_glow_factor");
    mReservedUniforms.push_back("sun_up_factor");
    mReservedUniforms.push_back("moonlight_color");

    mReservedUniforms.push_back("debug_normal_draw_length");
    llassert(mReservedUniforms.size() == LLShaderMgr::DEBUG_NORMAL_DRAW_LENGTH + 1);

    // Pathfinding Debug
    mReservedUniforms.push_back("tint");
    mReservedUniforms.push_back("ambiance");
    mReservedUniforms.push_back("alpha_scale");

    // Bump mapping
    mReservedUniforms.push_back("norm_scale");
    mReservedUniforms.push_back("stepX");
    mReservedUniforms.push_back("stepY");
    mReservedUniforms.push_back("bump_code");

    mReservedUniforms.push_back("delta");
    mReservedUniforms.push_back("dist_factor");
    mReservedUniforms.push_back("kern");
    mReservedUniforms.push_back("kern_scale");

    // Debug
    mReservedUniforms.push_back("tolerance");
    mReservedUniforms.push_back("dither_scale");
    mReservedUniforms.push_back("dither_scale_s");
    mReservedUniforms.push_back("dither_scale_t");

    // SMAA
    mReservedUniforms.push_back("edgesTex");
    mReservedUniforms.push_back("areaTex");
    mReservedUniforms.push_back("searchTex");
    mReservedUniforms.push_back("blendTex");
    mReservedUniforms.push_back("predicationTex");
    mReservedUniforms.push_back("SMAA_RT_METRICS");

    // CAS
    mReservedUniforms.push_back("cas_param_0");
    mReservedUniforms.push_back("cas_param_1");
    mReservedUniforms.push_back("out_screen_res");

    // Tonemapping + Exposure
    mReservedUniforms.push_back("dt");
    mReservedUniforms.push_back("noiseVec");
    mReservedUniforms.push_back("dynamic_exposure_params");
    mReservedUniforms.push_back("dynamic_exposure_params2");

    mReservedUniforms.push_back("exposure");
    mReservedUniforms.push_back("tonemap_type");
    mReservedUniforms.push_back("tonemap_mix");
    mReservedUniforms.push_back("tonemap_params");
    mReservedUniforms.push_back("hdri_split_screen");
    mReservedUniforms.push_back("diffuse_luminance_scale");

    // Stars/Aurora/Meteors
    mReservedUniforms.push_back("custom_alpha");
    mReservedUniforms.push_back("meteor_width_pixels");
    mReservedUniforms.push_back("aurora_intensity");
    mReservedUniforms.push_back("aurora_time");
    llassert(mReservedUniforms.size() == LLShaderMgr::AURORA_TIME + 1);

    // Alchemy Effects Stack
    mReservedUniforms.push_back("uFrameId");
    mReservedUniforms.push_back("uResolution");

    // Chromatic Aberration
    mReservedUniforms.push_back("uCAAmount");
    mReservedUniforms.push_back("uCAFalloff");
    mReservedUniforms.push_back("uCAAngleSinCos");
    mReservedUniforms.push_back("uCAOffsetR");
    mReservedUniforms.push_back("uCAOffsetB");
    mReservedUniforms.push_back("uCAAnisotropy");

    // Lens Flare
    mReservedUniforms.push_back("uLensFlareStrength");
    mReservedUniforms.push_back("uLensFlareSunPos");
    mReservedUniforms.push_back("uLensFlareSunVisibility");
    mReservedUniforms.push_back("uLensFlareStreakLength");
    mReservedUniforms.push_back("uLensFlareStreakFalloff");
    mReservedUniforms.push_back("uLensFlareStreakWidth");
    mReservedUniforms.push_back("uLensFlareStreakIntensity");
    mReservedUniforms.push_back("uLensFlareStreakTint");
    mReservedUniforms.push_back("uLensFlareChromaticSpread");
    mReservedUniforms.push_back("uLensFlareGlowRadius");
    mReservedUniforms.push_back("uLensFlareGlowFalloff");
    mReservedUniforms.push_back("uLensFlareGlow");
    mReservedUniforms.push_back("uLensFlareGhostCount");
    mReservedUniforms.push_back("uLensFlareGhostSpacing");
    mReservedUniforms.push_back("uLensFlareGhost");
    mReservedUniforms.push_back("uLensFlareHaloRadius");
    mReservedUniforms.push_back("uLensFlareHaloWidth");
    mReservedUniforms.push_back("uLensFlareHalo");
    mReservedUniforms.push_back("uLensFlareOcclusionRadius");
    mReservedUniforms.push_back("uLensFlareStarburst");
    mReservedUniforms.push_back("uLensFlareStarburstSpikes");
    mReservedUniforms.push_back("uLensFlareStarburstSharpness");
    mReservedUniforms.push_back("uLensFlareStarburstFalloff");
    mReservedUniforms.push_back("uLensFlareOcclusionTaps");
    mReservedUniforms.push_back("uLensFlareLightColor");

    // Color Correction LUT
    mReservedUniforms.push_back("uColorGradeLut");
    mReservedUniforms.push_back("uColorGradeLutSize");
    mReservedUniforms.push_back("uColorGradeLutStrength");

    // Linear-space grading (pre-tonemap)
    mReservedUniforms.push_back("uWhiteBalanceGain");
    mReservedUniforms.push_back("uLift");
    mReservedUniforms.push_back("uInvGammaCC");
    mReservedUniforms.push_back("uGain");

    // Split toning
    mReservedUniforms.push_back("uShadowRatio");
    mReservedUniforms.push_back("uHighlightRatio");
    mReservedUniforms.push_back("uMidtoneRatio");
    mReservedUniforms.push_back("uMidtoneAmount");
    mReservedUniforms.push_back("uSplitToneMid");
    mReservedUniforms.push_back("uToneAmount");

    // Display-space grading
    mReservedUniforms.push_back("uBWPScale");
    mReservedUniforms.push_back("uBWPBias");
    mReservedUniforms.push_back("uBCScale");
    mReservedUniforms.push_back("uBCBias");
    mReservedUniforms.push_back("uHighlightsScaled");
    mReservedUniforms.push_back("uShadowsScaled");
    mReservedUniforms.push_back("uSaturation");
    mReservedUniforms.push_back("uVibrance");
    mReservedUniforms.push_back("uHueShiftNorm");

    // Per-channel filmic curves
    mReservedUniforms.push_back("uCurveToe");
    mReservedUniforms.push_back("uCurveInvRange");
    mReservedUniforms.push_back("uCurveStrength");

    // Vignette
    mReservedUniforms.push_back("uVignetteAmount");
    mReservedUniforms.push_back("uVignetteRadius");
    mReservedUniforms.push_back("uVignetteSoft");
    mReservedUniforms.push_back("uVignetteShape");
    mReservedUniforms.push_back("uVignetteColor");
    mReservedUniforms.push_back("uVignetteMidColor");
    mReservedUniforms.push_back("uVignetteMidPoint");
    mReservedUniforms.push_back("uVignetteCenter");
    mReservedUniforms.push_back("uVignetteAspect");
    mReservedUniforms.push_back("uVignetteFeather");

    // CVD
    mReservedUniforms.push_back("uCompensateMode");
    mReservedUniforms.push_back("uCompensateAmount");

    // Film Grain
    mReservedUniforms.push_back("uGrainAmount");
    mReservedUniforms.push_back("uGrainStyle");
    mReservedUniforms.push_back("uGrainSize");
    mReservedUniforms.push_back("uGrainRange");
    mReservedUniforms.push_back("uGrainTint");
    mReservedUniforms.push_back("uGrainAnimate");

    // Dithering
    mReservedUniforms.push_back("uDitherAmount");
    mReservedUniforms.push_back("uDitherBits");
    mReservedUniforms.push_back("uDitherAnimate");

    // Previews
    mReservedUniforms.push_back("uPreviewMode");

    // Reference still
    mReservedUniforms.push_back("uReferenceStill");
    mReservedUniforms.push_back("uRefWipeMode");
    mReservedUniforms.push_back("uRefWipePos");

    // Text Shadow
    mReservedUniforms.push_back("textShadowMode");

    // End Alchemy Effects Stack

    // The enum and this list are parallel, and an entry added or removed on one side only
    // shifts every later uniform index for every shader in the viewer -- silently, since a
    // wrong index still resolves to some other real uniform. Fatal, like the duplicate check
    // below: there is no partial recovery from it.
    if (mReservedUniforms.size() != END_RESERVED_UNIFORMS)
    {
        LL_ERRS() << "Reserved uniform table has " << mReservedUniforms.size()
                  << " entries but eGLSLReservedUniforms declares " << (U32)END_RESERVED_UNIFORMS
                  << " -- the enum and the table are out of sync" << LL_ENDL;
    }

    std::set<std::string> dupe_check;

    for (U32 i = 0; i < mReservedUniforms.size(); ++i)
    {
        if (dupe_check.find(mReservedUniforms[i]) != dupe_check.end())
        {
            LL_ERRS() << "Duplicate reserved uniform name found: " << mReservedUniforms[i] << LL_ENDL;
        }
        dupe_check.insert(mReservedUniforms[i]);

        // An array uniform belongs here under its bare name. LLGLSLShader::mapUniform
        // chops the "[0]" off whatever GL reports before matching against this table,
        // so a subscript here can never match anything -- and nothing complains. The
        // location is never recorded, every upload to it silently does nothing, and the
        // shader reads the array as all zeroes, which surfaces as a rendering fault a
        // long way from the cause. Fatal for the same reason as the two checks above.
        if (mReservedUniforms[i].find('[') != std::string::npos)
        {
            LL_ERRS() << "Reserved uniform '" << mReservedUniforms[i] << "' carries a subscript; "
                      << "array uniforms are declared here by their bare name" << LL_ENDL;
        }
    }
}

