/**
 * @file llglslshader.cpp
 * @brief GLSL helper functions and state.
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
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

#include "llglslshader.h"

#include "llshadermgr.h"
#include "llfile.h"
#include "alsamplerstate.h"
#include "llrender.h"
#include "llvertexbuffer.h"
#include "llrendertarget.h"

#include "hbxxh.h"
#include "llsdserialize.h"

#if LL_DARWIN
#include "OpenGL/OpenGL.h"
#endif

 // Print-print list of shader included source files that are linked together via glAttachShader()
 // i.e. On macOS / OSX the AMD GLSL linker will display an error if a varying is left in an undefined state.
#define DEBUG_SHADER_INCLUDES 0

// Lots of STL stuff in here, using namespace std to keep things more readable
using std::vector;
using std::pair;
using std::make_pair;
using std::string;

GLuint LLGLSLShader::sCurBoundShader = 0;
LLGLSLShader* LLGLSLShader::sCurBoundShaderPtr = NULL;
S32 LLGLSLShader::sIndexedTextureChannels = 0;
U32 LLGLSLShader::sCompareSamplerUnits = 0;
S32 LLGLSLShader::sIndexedGLTFChannels = 0;
bool LLGLSLShader::sIndexedLegacyMaterials = false;
U32 LLGLSLShader::sMaxGLTFMaterials = 0;
U32 LLGLSLShader::sMaxGLTFNodes = 0;
bool LLGLSLShader::sProfileEnabled = false;
bool LLGLSLShader::sCanProfile = true;
std::set<LLGLSLShader*>& LLGLSLShader::sInstances = *(new std::set<LLGLSLShader*>());
LLGLSLShader::defines_map_t LLGLSLShader::sGlobalDefines;
// Starts at 1, and a default-constructed program starts level with it (see the member's
// initialiser) rather than behind: a program must not apply the environment uniform set before
// there is an environment. Wrapping is harmless -- this is only ever compared for equality.
U32 LLGLSLShader::sEnvironmentGeneration = 1;
U64 LLGLSLShader::sTotalTimeElapsed = 0;
U32 LLGLSLShader::sTotalTrianglesDrawn = 0;
U64 LLGLSLShader::sTotalSamplesDrawn = 0;
U32 LLGLSLShader::sTotalBinds = 0;
LLSD LLGLSLShader::sDefaultStats;

//UI shader -- declared here so llui_libtest will link properly
LLGLSLShader    gUIProgram;
LLGLSLShader    gSolidColorProgram;

// NOTE: Keep gShaderConsts* and LLGLSLShader::ShaderConsts_e in sync!
const std::string gShaderConstsKey[LLGLSLShader::NUM_SHADER_CONSTS] =
{
      "LL_SHADER_CONST_CLOUD_MOON_DEPTH"
    , "LL_SHADER_CONST_STAR_DEPTH"
};

// NOTE: Keep gShaderConsts* and LLGLSLShader::ShaderConsts_e in sync!
const std::string gShaderConstsVal[LLGLSLShader::NUM_SHADER_CONSTS] =
{
      "0.99998" // SHADER_CONST_CLOUD_MOON_DEPTH // SL-14113
    , "0.99999" // SHADER_CONST_STAR_DEPTH       // SL-14113
};


bool shouldChange(const LLVector4& v1, const LLVector4& v2)
{
    return v1 != v2;
}

#if !LL_RELEASE_FOR_DOWNLOAD
namespace
{
    // Expected engine-block layouts, registered by the modules that own the C++ mirror
    // structs (offsetof-derived -- see registerEngineBlockLayout in llglslshader.h).
    // Construct-on-first-use so cross-TU static-initializer registration is order-safe.
    std::map<std::string, std::vector<LLGLSLShader::EngineBlockLayoutMember>>& engine_block_layouts()
    {
        static std::map<std::string, std::vector<LLGLSLShader::EngineBlockLayoutMember>> s_layouts;
        return s_layouts;
    }
}

// static
void LLGLSLShader::registerEngineBlockLayout(const char* block_name, std::vector<EngineBlockLayoutMember> members)
{
    engine_block_layouts()[block_name] = std::move(members);
}

// Debug-only: assert the driver laid each registered engine block out at the std140 byte
// offsets the C++ upload expects and, where flagged, that matrix members introspect
// row-major (a pack that uploads transposed to match a row-major block would silently
// transpose every lookup against a column-major one). Expected offsets come from
// registerEngineBlockLayout -- offsetof() on the very structs the pack code writes -- so
// there is no hand-copied table here to drift. A mismatch means std140 drift (a member
// reordered in one of the C++/GLSL mirrors) or a driver packing bug -- classically Apple
// with vec3 members -- and is caught at shader load instead of surfacing as silently wrong
// shading.
//
// Blocks and members are matched by NAME: GLSL is compiled from source here, so the names
// the shader declares are the names GL introspection reports.
static void validateEngineBlockLayouts(GLuint program)
{
    const auto& reserved = LLShaderMgr::instance()->mReservedUniforms;
    const auto& layouts = engine_block_layouts();

    GLint block_count = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &block_count);
    for (GLint b = 0; b < block_count; ++b)
    {
        char block_name_buf[256] = {0};
        glGetActiveUniformBlockName(program, (GLuint)b, sizeof(block_name_buf) - 1, nullptr, block_name_buf);

        auto it = layouts.find(block_name_buf);
        if (it == layouts.end())
        {
            continue; // engine block with no registered C++ mirror (probes, GLTF*), or not one
        }
        const std::string& block_name = it->first;
        const auto& members = it->second;

        // Walk the block's ACTIVE members (a member GL eliminated keeps its std140 offset,
        // so skipping it loses nothing) and validate every one we can identify.
        GLint member_count = 0;
        glGetActiveUniformBlockiv(program, (GLuint)b, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &member_count);
        std::vector<GLint> indices((size_t)llmax(member_count, 0));
        if (indices.empty())
        {
            continue;
        }
        glGetActiveUniformBlockiv(program, (GLuint)b, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES, indices.data());

        for (GLint raw_idx : indices)
        {
            GLuint idx = (GLuint)raw_idx;
            char name_buf[256] = {0};
            GLint size = 0;
            GLenum type = 0;
            glGetActiveUniform(program, idx, sizeof(name_buf) - 1, nullptr, &size, &type, name_buf);
            const std::string name = name_buf;

            const LLGLSLShader::EngineBlockLayoutMember* expected = nullptr;
            for (const auto& m : members)
            {
                const char* nm = m.mName;
                if (!nm)
                {
                    if (m.mReservedUniform < 0 || m.mReservedUniform >= (S32)reserved.size())
                    {
                        continue;
                    }
                    nm = reserved[m.mReservedUniform].c_str();
                }
                if (name == nm)
                {
                    expected = &m;
                    break;
                }
            }
            if (!expected)
            {
                continue; // member this block's registration doesn't track
            }

            GLint off = -1;
            glGetActiveUniformsiv(program, 1, &idx, GL_UNIFORM_OFFSET, &off);
            if (off != (GLint)expected->mOffset)
            {
                LL_ERRS() << block_name << " UBO std140 layout mismatch: '" << name
                          << "' at offset " << off << ", expected " << expected->mOffset
                          << " -- C++ mirror struct drift or driver std140 packing bug." << LL_ENDL;
            }
            if (expected->mMatrix)
            {
                GLint row_major = 0;
                glGetActiveUniformsiv(program, 1, &idx, GL_UNIFORM_IS_ROW_MAJOR, &row_major);
                if (row_major)
                {
                    LL_ERRS() << block_name << " UBO matrix '" << name
                              << "' introspects row-major; the C++ upload writes column-major"
                              << " (std140's default), so every read would transpose." << LL_ENDL;
                }
            }
        }
    }
}
#endif // !LL_RELEASE_FOR_DOWNLOAD

//===============================
// LLGLSL Shader implementation
//===============================

//static
void LLGLSLShader::initProfile()
{
    sProfileEnabled = true;
    sTotalTimeElapsed = 0;
    sTotalTrianglesDrawn = 0;
    sTotalSamplesDrawn = 0;
    sTotalBinds = 0;

    for (auto ptr : sInstances)
    {
        ptr->clearStats();
    }
}


struct LLGLSLShaderCompareTimeElapsed
{
    bool operator()(const LLGLSLShader* const& lhs, const LLGLSLShader* const& rhs)
    {
        return lhs->mTimeElapsed < rhs->mTimeElapsed;
    }
};

//static
void LLGLSLShader::finishProfile(LLSD& stats)
{
    sProfileEnabled = false;

    if (stats.isDefined())
    {
        std::vector<LLGLSLShader*> sorted(sInstances.begin(), sInstances.end());
        std::sort(sorted.begin(), sorted.end(), LLGLSLShaderCompareTimeElapsed());

        LLSD& shaders = stats["shaders"];
        shaders = LLSD::emptyArray();
        bool unbound = false;
        for (auto ptr : sorted)
        {
            if (ptr->mBinds == 0)
            {
                unbound = true;
            }
            else
            {
                LLSD& shaderit = shaders.append(LLSD::emptyMap());
                ptr->dumpStats(shaderit);
            }
        }

        constexpr float mega = 1'000'000.f;
        float totalTimeMs = sTotalTimeElapsed / mega;
        LL_INFOS() << "-----------------------------------" << LL_ENDL;
        LL_INFOS() << "Total rendering time: " << llformat("%.4f ms", totalTimeMs) << LL_ENDL;
        LL_INFOS() << "Total samples drawn: " << llformat("%.4f million", sTotalSamplesDrawn / mega) << LL_ENDL;
        LL_INFOS() << "Total triangles drawn: " << llformat("%.3f million", sTotalTrianglesDrawn / mega) << LL_ENDL;
        LL_INFOS() << "-----------------------------------" << LL_ENDL;
        LLSD& totals = stats["totals"];
        totals = LLSD::emptyMap();
        totals["time"] = totalTimeMs / 1000.0;
        totals["binds"] = LLSD::Integer(sTotalBinds);
        // sample counters are 64-bit; store as Real to avoid S32 overflow
        totals["samples"] = F64(sTotalSamplesDrawn);
        totals["triangles"] = LLSD::Integer(sTotalTrianglesDrawn);

        LLSD& unused = stats["unused"];
        unused = LLSD::emptyArray();
        if (unbound)
        {
            LL_INFOS() << "The following shaders were unused: " << LL_ENDL;
            for (auto ptr : sorted)
            {
                if (ptr->mBinds == 0)
                {
                    LL_INFOS() << ptr->mName << LL_ENDL;
                    unused.append(ptr->mName);
                }
            }
        }
    }
}

void LLGLSLShader::clearStats()
{
    mTrianglesDrawn = 0;
    mTimeElapsed = 0;
    mSamplesDrawn = 0;
    mBinds = 0;
}

void LLGLSLShader::dumpStats(LLSD& stats)
{
    stats["name"] = mName;
    LLSD& files = stats["files"];
    files = LLSD::emptyArray();
    LL_INFOS() << "=============================================" << LL_ENDL;
    LL_INFOS() << mName << LL_ENDL;
    for (U32 i = 0; i < mShaderFiles.size(); ++i)
    {
        LL_INFOS() << mShaderFiles[i].first << LL_ENDL;
        files.append(mShaderFiles[i].first);
    }
    LL_INFOS() << "=============================================" << LL_ENDL;

    constexpr float  mega = 1'000'000.f;
    constexpr double giga = 1'000'000'000.0;
    F32 ms = mTimeElapsed / mega;
    F32 seconds = ms / 1000.f;

    F32 pct_tris = (F32)mTrianglesDrawn / (F32)sTotalTrianglesDrawn * 100.f;
    F32 tris_sec = (F32)(mTrianglesDrawn / mega);
    tris_sec /= seconds;

    F32 pct_samples = (F32)((F64)mSamplesDrawn / (F64)sTotalSamplesDrawn) * 100.f;
    F32 samples_sec = (F32)(mSamplesDrawn / giga);
    samples_sec /= seconds;

    F32 pct_binds = (F32)mBinds / (F32)sTotalBinds * 100.f;

    LL_INFOS() << "Triangles Drawn: " << mTrianglesDrawn << " " << llformat("(%.2f pct of total, %.3f million/sec)", pct_tris, tris_sec) << LL_ENDL;
    LL_INFOS() << "Binds: " << mBinds << " " << llformat("(%.2f pct of total)", pct_binds) << LL_ENDL;
    LL_INFOS() << "SamplesDrawn: " << mSamplesDrawn << " " << llformat("(%.2f pct of total, %.3f billion/sec)", pct_samples, samples_sec) << LL_ENDL;
    LL_INFOS() << "Time Elapsed: " << mTimeElapsed << " " << llformat("(%.2f pct of total, %.5f ms)\n", (F32)((F64)mTimeElapsed / (F64)sTotalTimeElapsed) * 100.f, ms) << LL_ENDL;
    stats["time"] = seconds;
    stats["binds"] = LLSD::Integer(mBinds);
    // sample counters are 64-bit; store as Real to avoid S32 overflow
    stats["samples"] = F64(mSamplesDrawn);
    stats["triangles"] = LLSD::Integer(mTrianglesDrawn);
}

//static
void LLGLSLShader::startProfile()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    if (sProfileEnabled && sCurBoundShaderPtr)
    {
        sCurBoundShaderPtr->placeProfileQuery();
    }

}

//static
void LLGLSLShader::stopProfile()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    if (sProfileEnabled && sCurBoundShaderPtr)
    {
        sCurBoundShaderPtr->unbind();
    }
}

void LLGLSLShader::placeProfileQuery(bool for_runtime)
{
    if (sProfileEnabled || for_runtime)
    {
        if (mTimerQuery == 0)
        {
            glGenQueries(1, &mSamplesQuery);
            glGenQueries(1, &mTimerQuery);
            glGenQueries(1, &mPrimitivesQuery);
        }

        glBeginQuery(GL_TIME_ELAPSED, mTimerQuery);

        if (!for_runtime)
        {
            glBeginQuery(GL_SAMPLES_PASSED, mSamplesQuery);
            glBeginQuery(GL_PRIMITIVES_GENERATED, mPrimitivesQuery);
        }
    }
}

bool LLGLSLShader::readProfileQuery(bool for_runtime, bool force_read)
{
    if ((sProfileEnabled || for_runtime) && sCanProfile)
    {
        if (!mProfilePending)
        {
            glEndQuery(GL_TIME_ELAPSED);
            if (!for_runtime)
            {
                glEndQuery(GL_SAMPLES_PASSED);
                glEndQuery(GL_PRIMITIVES_GENERATED);
            }
            mProfilePending = for_runtime;
        }

        if (mProfilePending && for_runtime && !force_read)
        {
            GLuint64 result = 0;
            glGetQueryObjectui64v(mTimerQuery, GL_QUERY_RESULT_AVAILABLE, &result);

            if (result != GL_TRUE)
            {
                return false;
            }
        }

        GLuint64 time_elapsed = 0;
        glGetQueryObjectui64v(mTimerQuery, GL_QUERY_RESULT, &time_elapsed);
        mTimeElapsed += time_elapsed;
        mProfilePending = false;

        if (!for_runtime)
        {
            GLuint64 samples_passed = 0;
            glGetQueryObjectui64v(mSamplesQuery, GL_QUERY_RESULT, &samples_passed);

            GLuint64 primitives_generated = 0;
            glGetQueryObjectui64v(mPrimitivesQuery, GL_QUERY_RESULT, &primitives_generated);
            sTotalTimeElapsed += time_elapsed;

            sTotalSamplesDrawn += samples_passed;
            mSamplesDrawn += samples_passed;

            U32 tri_count = (U32)primitives_generated / 3;

            mTrianglesDrawn += tri_count;
            sTotalTrianglesDrawn += tri_count;

            sTotalBinds++;
            mBinds++;
        }
    }

    return true;
}



LLGLSLShader::LLGLSLShader()
    : mProgramObject(0),
    mAttributeMask(0),
    mTotalUniformSize(0),
    mActiveTextureChannels(0),
    mShaderLevel(0),
    mShaderGroup(SG_DEFAULT),
    mFeatures(),
    mTimerQuery(0),
    mSamplesQuery(0),
    mPrimitivesQuery(0)
{

}

LLGLSLShader::~LLGLSLShader()
{
    // Leave the registry, which needs no GL context. unloadInternal() is the usual way out of
    // it, but a program can be destroyed without ever being unloaded -- and this one is about
    // to delete its children the same way -- so every pointer sInstances holds would otherwise
    // dangle. It is walked for name uniqueness, so a stale entry is a use-after-free rather
    // than a leak. Recursion covers the subtree: each child erases itself on the way out.
    sInstances.erase(this);

    // Free the owned subtree's OBJECTS, so a program destroyed without unload() does not leak
    // them. Deliberately not unload(): a global program's destructor runs at static destruction
    // with no GL context, where deleting program objects is undefined -- and the driver reclaims
    // them with the context anyway. unload() remains the way to release GL.
    for (LLGLSLShader** v : { &mRiggedVariant, &mClassicVariant, &mMirrorVariant })
    {
        if (*v && *v != this && (*v)->mOwnedVariant)
        {
            delete *v;  // recurses through the subtree; touches no GL
        }
        *v = nullptr;
    }
}

// static
// Invalidate the shared environment (sky/water) uniform set for EVERY program: each re-applies it
// on its next bind, when it notices its own generation is behind. Bumping one counter keeps this
// O(1) instead of walking every live program every frame, and it reaches programs that no list
// happens to hold -- the indexed writers, avatar rigid and the deferred lighting programs were
// never in mShaderList, so they never saw a change of sky at all. A program with no environment
// uniforms resolves them to -1 and skips, so re-applying is cheap.
void LLGLSLShader::dirtyEnvironmentUniforms()
{
    ++sEnvironmentGeneration;
}

void LLGLSLShader::freeVariant(LLGLSLShader*& variant)
{
    if (variant)
    {
        // Two things must never be freed through:
        //  - a SELF-edge: a skinned program is its own rigged variant (see createShader), so
        //    freeing here would recurse forever and delete this mid-unload;
        //  - a pointer aimed at a global rather than a createShader() allocation, which would
        //    free a static object (mOwnedVariant marks the ones we allocated).
        if (variant != this && variant->mOwnedVariant)
        {
            variant->unload();
            delete variant;
        }
        variant = nullptr;
    }
}

void LLGLSLShader::freeOwnedVariants()
{
    freeVariant(mRiggedVariant);
    freeVariant(mClassicVariant);
    freeVariant(mMirrorVariant);
}

void LLGLSLShader::configureVariantClone(LLGLSLShader& dst, const std::string& name) const
{
    dst.mName        = name;
    dst.mFeatures    = mFeatures;
    dst.mDefines     = mDefines;    // NOTE: must come before the caller's addPermutation()s
    dst.mShaderFiles = mShaderFiles;
    dst.mShaderLevel = mShaderLevel;
    dst.mShaderGroup = mShaderGroup;
}

// Build one corner: this program's config plus the requested defines, compiled. Deriving every
// corner from this program (rather than from a hand-configured sibling) is what makes
// construction independent of the order a caller happens to build its programs in, and it puts
// the axis defines after every addPermutation() the caller made -- the ordering that hand-wiring
// got wrong whenever a define was added after the clone was configured.
LLGLSLShader* LLGLSLShader::makeVariantCorner(const std::string& name, const char* perm_key,
                                             bool add_classic, bool add_rigged) const
{
    LLGLSLShader* corner = new LLGLSLShader();
    corner->mOwnedVariant = true;   // parent frees it in unload(); see freeVariant()
    configureVariantClone(*corner, name);

    if (perm_key)
    {
        corner->addPermutation(perm_key, "1");
    }
    if (add_classic)
    {
        corner->addPermutation("CLASSIC_MODE", "1");
    }
    if (add_rigged)
    {   // HAS_SKIN=1 selects the skinned path in the vertex source; hasObjectSkinning is the
        // matching feature flag, which is what attaches the objectSkin module.
        corner->addPermutation("HAS_SKIN", "1");
        corner->mFeatures.hasObjectSkinning = true;
    }

    if (corner->createShader())
    {
        return corner;
    }
    delete corner;
    return nullptr;
}

// Build `axis`'s corner set into the matching member. RIGGED is the innermost axis: one corner,
// nothing to compose over. Each per-PASS axis gets one corner per rigged x classic combination
// already present, so a program asking for all three ends up with every corner reachable by
// selectVariant()->bind(rigged). On any corner failure the partial subtree is dropped so the
// base is used.
bool LLGLSLShader::createVariant(EVariant axis)
{
    LLGLSLShader* LLGLSLShader::* member =
        (axis == VARIANT_RIGGED)  ? &LLGLSLShader::mRiggedVariant  :
        (axis == VARIANT_CLASSIC) ? &LLGLSLShader::mClassicVariant :
                                    &LLGLSLShader::mMirrorVariant;
    freeVariant(this->*member);

    if (axis == VARIANT_RIGGED)
    {
        mRiggedVariant = makeVariantCorner(llformat("Skinned %s", mName.c_str()), nullptr, false, true);
        return mRiggedVariant != nullptr;
    }

    const char* perm   = (axis == VARIANT_CLASSIC) ? "CLASSIC_MODE" : "MIRROR_CLIP";
    const char* suffix = (axis == VARIANT_CLASSIC) ? "(Classic)"    : "(Mirror)";

    // classic never composes over itself (the member was just cleared, and classic is built
    // before mirror), so a classic sibling only exists when the axis being added is mirror.
    const bool has_rigged  = mRiggedVariant  != nullptr;
    const bool has_classic = mClassicVariant != nullptr;

    LLGLSLShader* v = makeVariantCorner(llformat("%s %s", mName.c_str(), suffix), perm, false, false);
    if (!v)
    {
        return false;
    }

    bool ok = true;
    if (has_rigged)
    {
        v->mRiggedVariant = makeVariantCorner(llformat("Skinned %s %s", mName.c_str(), suffix), perm, false, true);
        ok = ok && v->mRiggedVariant != nullptr;
    }
    if (ok && has_classic)
    {
        v->mClassicVariant = makeVariantCorner(llformat("%s (Classic) %s", mName.c_str(), suffix), perm, true, false);
        ok = ok && v->mClassicVariant != nullptr;

        if (ok && mClassicVariant->mRiggedVariant)
        {
            v->mClassicVariant->mRiggedVariant =
                makeVariantCorner(llformat("Skinned %s (Classic) %s", mName.c_str(), suffix), perm, true, true);
            ok = ok && v->mClassicVariant->mRiggedVariant != nullptr;
        }
    }

    if (!ok)
    {   // drop the partial subtree; selectVariant() falls back to this program
        freeVariant(v);
        return false;
    }

    this->*member = v;
    return true;
}

void LLGLSLShader::unload()
{
    mShaderFiles.clear();
    mDefines.clear();
    mPermutationsAdded = false;
    mFeatures = LLShaderFeatures();

    freeOwnedVariants();

    unloadInternal();
}

void LLGLSLShader::unloadInternal()
{
    sInstances.erase(this);

    stop_glerror();
    mAttribute.clear();
    mTexture.clear();
    mUniform.clear();
    mMinimumAlpha = MINIMUM_ALPHA_UNSET; // program state is going away with the program

    if (mProgramObject)
    {
        GLuint obj[1024];
        GLsizei count = 0;
        glGetAttachedShaders(mProgramObject, 1024, &count, obj);

        for (GLsizei i = 0; i < count; i++)
        {
            glDetachShader(mProgramObject, obj[i]);
        }

        for (GLsizei i = 0; i < count; i++)
        {
            if (glIsShader(obj[i]))
            {
                glDeleteShader(obj[i]);
#if LL_DARWIN
                // Apple's GL 4.1 core driver spuriously raises
                // GL_INVALID_OPERATION on glDeleteShader of a just-detached,
                // valid shader (already vetted by glIsShader above). Drain
                // it. Commit 7e9bf817a7 removed the original sweep on the
                // belief that the driver had been fixed; empirically it has
                // not been.
                flush_glerror();
#endif
            }
        }

        // GL may hand this name straight back out, and the debug validator's per-program
        // sampler cache revalidates only on active-uniform count -- a recreated program
        // matching both would be checked against this one's samplers.
        forget_program_samplers(mProgramObject);

        glDeleteProgram(mProgramObject);

        mProgramObject = 0;
    }

    if (mTimerQuery)
    {
        glDeleteQueries(1, &mTimerQuery);
        mTimerQuery = 0;
    }

    if (mSamplesQuery)
    {
        glDeleteQueries(1, &mSamplesQuery);
        mSamplesQuery = 0;
    }

    stop_glerror();
}

bool LLGLSLShader::createShader(U32 variants)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    // Recreating the program invalidates any variants built from the previous configuration.
    freeOwnedVariants();

    // Start level with the current generation rather than behind it: a program must not apply
    // the environment uniform set before there IS an environment. gpu_benchmark() builds and
    // binds a program during feature detection, which runs from LLViewerWindow's constructor,
    // long before LLEnvironment exists. Nothing is lost by waiting -- LLEnvironment::update()
    // bumps the generation every frame, so a program built mid-session applies on the next one.
    mEnvUniformsGeneration = sEnvironmentGeneration;

    unloadInternal();

    sInstances.insert(this);

    //reloading, reset light hash value
    mLightHash = 0xFFFFFFFF;

    // Derived, never hand-set: the LINEAR_DIFFUSE permutation is what the shader source
    // itself keys its colour-space behaviour on, so deriving the engine-side flag from the
    // same define means the two cannot disagree -- the same reason rigged-ness is expressed
    // as the HAS_SKIN permutation rather than a parallel bool.
    mLinearDiffuse = mDefines.contains("LINEAR_DIFFUSE");

    llassert_always(!mShaderFiles.empty());

    mShaderHash = hash();

    // this program's configuration is settled; a permutation added after this point belongs to
    // the NEXT build, and clearPermutations() will say so if one is then discarded
    mPermutationsAdded = false;

    // Create program
    mProgramObject = glCreateProgram();
    if (mProgramObject == 0)
    {
        // Shouldn't happen if shader related extensions, like ARB_vertex_shader, exist.
        LL_SHADER_LOADING_WARNS() << "Failed to create handle for shader: " << mName << LL_ENDL;
        unloadInternal();
        return false;
    }

    bool success = true;

    mUsingBinaryProgram =  LLShaderMgr::instance()->loadCachedProgramBinary(this);

    if (!mUsingBinaryProgram)
    {
#if DEBUG_SHADER_INCLUDES
        fprintf(stderr, "--- %s ---\n", mName.c_str());
#endif // DEBUG_SHADER_INCLUDES

        //compile new source
        vector< pair<string, GLenum> >::iterator fileIter = mShaderFiles.begin();
        for (; fileIter != mShaderFiles.end(); fileIter++)
        {
            GLuint shaderhandle = LLShaderMgr::instance()->loadShaderFile((*fileIter).first, mShaderLevel, (*fileIter).second, &mDefines, mFeatures.mIndexedTextureChannels);
            LL_DEBUGS("ShaderLoading") << "SHADER FILE: " << (*fileIter).first << " mShaderLevel=" << mShaderLevel << LL_ENDL;
            if (shaderhandle)
            {
                attachObject(shaderhandle);
            }
            else
            {
                success = false;
            }
        }
    }

    // Attach existing objects
    if (!LLShaderMgr::instance()->attachShaderFeatures(this))
    {
        unloadInternal();
        return false;
    }
    // Map attributes and uniforms
    if (success)
    {
        success = mapAttributes();
    }
    if (success)
    {
        success = mapUniforms();
    }
    if (!success)
    {
        LL_SHADER_LOADING_WARNS() << "Failed to link shader: " << mName << LL_ENDL;

        // Try again using a lower shader level;
        if (mShaderLevel > 0)
        {
            LL_SHADER_LOADING_WARNS() << "Failed to link using shader level " << mShaderLevel << " trying again using shader level " << (mShaderLevel - 1) << LL_ENDL;
            mShaderLevel--;
            return createShader(variants);
        }
        else
        {
            // Give up and unload shader.
            unloadInternal();
        }
    }
    // NOTE: indexed texture channels (tex0..texN) are assigned the first texture
    // units directly in mapUniforms() via texunit_priority -- no post-pass needed.

    LL_DEBUGS("GLSLTextureChannels") << mName << " has " << mActiveTextureChannels << " active texture channels" << LL_ENDL;

    for (U32 i = 0; i < mTexture.size(); i++)
    {
        if (mTexture[i] > -1)
        {
            LL_DEBUGS("GLSLTextureChannels") << "Texture " << LLShaderMgr::instance()->mReservedUniforms[i] << " assigned to channel " << mTexture[i] << LL_ENDL;
        }
    }

#if LL_PROFILER_ENABLE_RENDER_DOC
    setLabel(mName.c_str());
#endif

    // A skinned program IS its own rigged variant, so bind(true) on one returns itself rather
    // than asserting. Set here rather than in attachShaderFeatures() so createShader() is the
    // one owner of every mRiggedVariant edge. NB this makes mRiggedVariant a self-edge --
    // freeVariant()/forEachVariant() must not traverse it -- and a program that also asks for
    // VARIANT_RIGGED overwrites it with the real corner just below.
    if (success && mFeatures.hasObjectSkinning)
    {
        mRiggedVariant = this;
    }

    for (EVariant axis : { VARIANT_RIGGED, VARIANT_CLASSIC, VARIANT_MIRROR })
    {
        if (success && (variants & axis))
        {
            success = createVariant(axis);
        }
    }

    return success;
}

#if DEBUG_SHADER_INCLUDES
void dumpAttachObject(const char* func_name, GLuint program_object, const std::string& object_path)
{
    GLchar* info_log;
    GLint      info_len_expect = 0;
    GLint      info_len_actual = 0;

    glGetShaderiv(program_object, GL_INFO_LOG_LENGTH, , &info_len_expect);
    fprintf(stderr, " * %-20s(), log size: %d, %s\n", func_name, info_len_expect, object_path.c_str());

    if (info_len_expect > 0)
    {
        fprintf(stderr, " ========== %s() ========== \n", func_name);
        info_log = new GLchar[info_len_expect];
        glGetProgramInfoLog(program_object, info_len_expect, &info_len_actual, info_log);
        fprintf(stderr, "%s\n", info_log);
        delete[] info_log;
    }
}
#endif // DEBUG_SHADER_INCLUDES

bool LLGLSLShader::attachVertexObject(std::string object_path)
{
    if (LLShaderMgr::instance()->mVertexShaderObjects.count(object_path) > 0)
    {
        stop_glerror();
        glAttachShader(mProgramObject, LLShaderMgr::instance()->mVertexShaderObjects[object_path]);
#if DEBUG_SHADER_INCLUDES
        dumpAttachObject("attachVertexObject", mProgramObject, object_path);
#endif // DEBUG_SHADER_INCLUDES
        stop_glerror();
        return true;
    }
    else
    {
        LL_SHADER_LOADING_WARNS() << "Attempting to attach shader object: '" << object_path << "' that hasn't been compiled." << LL_ENDL;
        return false;
    }
}

bool LLGLSLShader::attachFragmentObject(std::string object_path)
{
    if(mUsingBinaryProgram)
        return true;

    if (LLShaderMgr::instance()->mFragmentShaderObjects.count(object_path) > 0)
    {
        stop_glerror();
        glAttachShader(mProgramObject, LLShaderMgr::instance()->mFragmentShaderObjects[object_path]);
#if DEBUG_SHADER_INCLUDES
        dumpAttachObject("attachFragmentObject", mProgramObject, object_path);
#endif // DEBUG_SHADER_INCLUDES
        stop_glerror();
        return true;
    }
    else
    {
        LL_SHADER_LOADING_WARNS() << "Attempting to attach shader object: '" << object_path << "' that hasn't been compiled." << LL_ENDL;
        return false;
    }
}

void LLGLSLShader::attachObject(GLuint object)
{
    if(mUsingBinaryProgram)
        return;

    if (object != 0)
    {
        stop_glerror();
        glAttachShader(mProgramObject, object);
#if DEBUG_SHADER_INCLUDES
        std::string object_path("???");
        dumpAttachObject("attachObject", mProgramObject, object_path);
#endif // DEBUG_SHADER_INCLUDES
        stop_glerror();
    }
    else
    {
        LL_SHADER_LOADING_WARNS() << "Attempting to attach non existing shader object. " << LL_ENDL;
    }
}

void LLGLSLShader::attachObjects(GLuint* objects, S32 count)
{
    if(mUsingBinaryProgram)
        return;

    for (S32 i = 0; i < count; i++)
    {
        attachObject(objects[i]);
    }
}

bool LLGLSLShader::mapAttributes()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    bool res = true;
    if (!mUsingBinaryProgram)
    {
        //before linking, make sure reserved attributes always have consistent locations
        for (U32 i = 0; i < LLShaderMgr::instance()->mReservedAttribs.size(); i++)
        {
            const char* name = LLShaderMgr::instance()->mReservedAttribs[i].c_str();
            glBindAttribLocation(mProgramObject, i, (const GLchar*)name);
        }

        //link the program
        res = link();
    }

    mAttribute.clear();
#if LL_DEBUG || LL_RELEASE_WITH_DEBUG_INFO
    mAttribute.resize(LLShaderMgr::instance()->mReservedAttribs.size(), { -1, NULL });
#else
    mAttribute.resize(LLShaderMgr::instance()->mReservedAttribs.size(), -1);
#endif

    if (res)
    { //read back channel locations

        mAttributeMask = 0;

        //read back reserved channels first
        for (U32 i = 0; i < LLShaderMgr::instance()->mReservedAttribs.size(); i++)
        {
            const char* name = LLShaderMgr::instance()->mReservedAttribs[i].c_str();
            S32 index = glGetAttribLocation(mProgramObject, (const GLchar*)name);
            if (index != -1)
            {
#if LL_DEBUG || LL_RELEASE_WITH_DEBUG_INFO
                mAttribute[i] = { index, name };
#else
                mAttribute[i] = index;
#endif
                mAttributeMask |= 1 << i;
                LL_DEBUGS("ShaderUniform") << "Attribute " << name << " assigned to channel " << index << LL_ENDL;
            }
        }

        return true;
    }

    return false;
}

void LLGLSLShader::mapUniform(const gl_uniform_data_t& gl_uniform)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    GLenum type = gl_uniform.type;
    GLint size = gl_uniform.size;
    char name[1024];        /* Flawfinder: ignore */
    strncpy(name, gl_uniform.name.c_str(), sizeof(name) - 1); /* Flawfinder: ignore */
    name[sizeof(name) - 1] = 0;

    if (size > 0)
    {
        switch (type)
        {
        case GL_FLOAT_VEC2: size *= 2; break;
        case GL_FLOAT_VEC3: size *= 3; break;
        case GL_FLOAT_VEC4: size *= 4; break;
        // Doubles occupy two 4-byte words per component (component_count * 2).
        case GL_DOUBLE: size *= 2; break;
        case GL_DOUBLE_VEC2: size *= 4; break;
        case GL_DOUBLE_VEC3: size *= 6; break;
        case GL_DOUBLE_VEC4: size *= 8; break;
        case GL_INT_VEC2: size *= 2; break;
        case GL_INT_VEC3: size *= 3; break;
        case GL_INT_VEC4: size *= 4; break;
        case GL_UNSIGNED_INT_VEC2: size *= 2; break;
        case GL_UNSIGNED_INT_VEC3: size *= 3; break;
        case GL_UNSIGNED_INT_VEC4: size *= 4; break;
        // 64-bit integers (ARB_gpu_shader_int64 / bindless texture handles) are two
        // 4-byte words per component, same sizing as GL_DOUBLE. (NV enums alias the
        // ARB values, so listing only the ARB names covers both.)
        case GL_INT64_ARB: size *= 2; break;
        case GL_INT64_VEC2_ARB: size *= 4; break;
        case GL_INT64_VEC3_ARB: size *= 6; break;
        case GL_INT64_VEC4_ARB: size *= 8; break;
        case GL_UNSIGNED_INT64_ARB: size *= 2; break;
        case GL_UNSIGNED_INT64_VEC2_ARB: size *= 4; break;
        case GL_UNSIGNED_INT64_VEC3_ARB: size *= 6; break;
        case GL_UNSIGNED_INT64_VEC4_ARB: size *= 8; break;
        case GL_BOOL_VEC2: size *= 2; break;
        case GL_BOOL_VEC3: size *= 3; break;
        case GL_BOOL_VEC4: size *= 4; break;
        case GL_FLOAT_MAT2: size *= 4; break;
        case GL_FLOAT_MAT3: size *= 9; break;
        case GL_FLOAT_MAT4: size *= 16; break;
        case GL_FLOAT_MAT2x3: size *= 6; break;
        case GL_FLOAT_MAT2x4: size *= 8; break;
        case GL_FLOAT_MAT3x2: size *= 6; break;
        case GL_FLOAT_MAT3x4: size *= 12; break;
        case GL_FLOAT_MAT4x2: size *= 8; break;
        case GL_FLOAT_MAT4x3: size *= 12; break;
        case GL_DOUBLE_MAT2: size *= 8; break;
        case GL_DOUBLE_MAT3: size *= 18; break;
        case GL_DOUBLE_MAT4: size *= 32; break;
        case GL_DOUBLE_MAT2x3: size *= 12; break;
        case GL_DOUBLE_MAT2x4: size *= 16; break;
        case GL_DOUBLE_MAT3x2: size *= 12; break;
        case GL_DOUBLE_MAT3x4: size *= 24; break;
        case GL_DOUBLE_MAT4x2: size *= 16; break;
        case GL_DOUBLE_MAT4x3: size *= 24; break;
        }
        mTotalUniformSize += size;
    }

    S32 location = glGetUniformLocation(mProgramObject, name);
    if (location != -1)
    {
        //chop off "[0]" so we can always access the first element
        //of an array by the array name
        char* is_array = strstr(name, "[0]");
        if (is_array)
        {
            is_array[0] = 0;
        }

        LLStaticHashedString hashedName(name);
        mUniformMap[hashedName] = location;

        LL_DEBUGS("ShaderUniform") << "Uniform " << name << " is at location " << location << LL_ENDL;

        // Indexed textures (tex0..texN) are referenced by hardcoded texture-unit
        // index, not through a reserved-uniform enum. The priority sort in
        // mapUniforms() guarantees they are mapped first, so just bind the
        // texture-unit => sampler-location mapping here and skip mUniform/mTexture.
        if (gl_uniform.texunit_priority < (U32)mFeatures.mIndexedTextureChannels)
        {
            mapUniformTextureChannel(location, type, size);
            return;
        }

        //find the index of this uniform
        for (S32 i = 0; i < (S32)LLShaderMgr::instance()->mReservedUniforms.size(); i++)
        {
            if ((mUniform[i] == -1)
                && (LLShaderMgr::instance()->mReservedUniforms[i] == name))
            {
                //found it
                mUniform[i] = location;
                mTexture[i] = mapUniformTextureChannel(location, type, size);
                if (mTexture[i] != -1)
                {
                    LL_DEBUGS("GLSLTextureChannels") << name << " assigned to texture channel " << mTexture[i] << LL_ENDL;
                }
                return;
            }
        }
    }
}

void LLGLSLShader::clearPermutations()
{
    // Clearing permutations the caller just added silently drops them: the define never reaches
    // the compiled program, and anything derived from it (mLinearDiffuse) reads false. Three
    // programs lost LINEAR_DIFFUSE exactly this way. Configuration must clear first, then add.
    if (mPermutationsAdded)
    {
        LL_WARNS("Shader") << "clearPermutations() on " << mName
                           << " discarded permutations added since the last createShader()" << LL_ENDL;
    }
    mDefines.clear();
    mPermutationsAdded = false;
}

void LLGLSLShader::addPermutation(std::string name, std::string value)
{
    mDefines[name] = value;
    mPermutationsAdded = true;
}

void LLGLSLShader::addConstant(const LLGLSLShader::eShaderConsts shader_const)
{
    addPermutation(gShaderConstsKey[shader_const], gShaderConstsVal[shader_const]);
}

void LLGLSLShader::removePermutation(std::string name)
{
    mDefines.erase(name);
}

GLint LLGLSLShader::mapUniformTextureChannel(GLint location, GLenum type, GLint size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    if ((type >= GL_SAMPLER_1D && type <= GL_SAMPLER_2D_RECT_SHADOW) ||
        type == GL_SAMPLER_2D_MULTISAMPLE ||
        type == GL_SAMPLER_CUBE_MAP_ARRAY)
    {   //this here is a texture
        GLint ret = mActiveTextureChannels;
        if (size == 1)
        {
            glUniform1i(location, mActiveTextureChannels);
            mActiveTextureChannels++;
        }
        else
        {
            //is array of textures, make sequential after this texture
            GLint channel[32]; // <=== only support up to 32 texture channels
            llassert(size <= 32);
            size = llmin(size, 32);
            for (int i = 0; i < size; ++i)
            {
                channel[i] = mActiveTextureChannels++;
            }
            glUniform1iv(location, size, channel);
        }

        return ret;
    }
    return -1;
}

bool LLGLSLShader::mapUniforms()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    bool res = true;

    mTotalUniformSize = 0;
    mActiveTextureChannels = 0;
    mUniform.clear();
    mUniformMap.clear();
    mTexture.clear();
    mValue.clear();
    mMinimumAlpha = MINIMUM_ALPHA_UNSET; // fresh mapping: GPU-side value is the default again
    //initialize arrays
    mUniform.resize(LLShaderMgr::instance()->mReservedUniforms.size(), -1);
    mTexture.resize(LLShaderMgr::instance()->mReservedUniforms.size(), -1);

    bind();

    //get the number of active uniforms
    GLint activeCount;
    glGetProgramiv(mProgramObject, GL_ACTIVE_UNIFORMS, &activeCount);

    // Texture channels are assigned in the order samplers are mapped (see
    // mapUniformTextureChannel / mActiveTextureChannels), and the engine is
    // sensitive to that order -- e.g. "diffuseMap" must win channel 0 so the
    // texture matrix is applied to the right unit. The GLSL compiler does not
    // guarantee any particular ordering of glGetActiveUniform() indices, so we
    // The analytic font glyph buffer (isamplerBuffer) is now an auto-channeled
    // diffuseMap still wins texture channel 0 if the compiler orders the buffer
    // sampler first (it is declared earlier, in the injected lib).
    //   [mIndexedTextureChannels, ...) -> reserved uniforms, in mReservedUniforms order
    //   UINT_MAX                       -> everything else (order irrelevant; non-samplers)
    const auto& reservedUniforms = LLShaderMgr::instance()->mReservedUniforms;
    const U32 max_index = (U32)mFeatures.mIndexedTextureChannels;
    llassert(max_index == 0 || mFeatures.mIndexedTextureChannels == LLGLSLShader::sIndexedTextureChannels);

    std::vector<gl_uniform_data_t> gl_uniforms;
    gl_uniforms.reserve(activeCount);

    bool has_diffuse = false;
    for (S32 i = 0; i < activeCount; i++)
    {
        // Fetch name, type and size from OpenGL.
        char name[1024];        /* Flawfinder: ignore */
        gl_uniform_data_t gl_uniform;
        GLsizei length = 0;
        glGetActiveUniform(mProgramObject, i, sizeof(name), &length, &gl_uniform.size, &gl_uniform.type, (GLchar*)name);
        if (length && name[length - 1] == '\0')
        {
            --length; // some drivers include the null terminator in the length, some don't
        }
        if (gl_uniform.size < 0 || length <= 0)
            continue;
        gl_uniform.name.assign(name, length);

        // Track whether diffuseMap is present so we can assert it is never mixed
        // with indexed textures (they share texture channel 0).
        has_diffuse |= gl_uniform.name == "diffuseMap";

        // Reserved uniforms keep their relative order, offset past the indexed range.
        auto it = std::find(reservedUniforms.cbegin(), reservedUniforms.cend(), gl_uniform.name);
        if (it != reservedUniforms.cend())
        {
            gl_uniform.texunit_priority = max_index + (U32)std::distance(reservedUniforms.cbegin(), it);
        }
        else
        {
            // Indexed textures tex0..texN must always take the first channels, so
            // give tex<idx> priority <idx>. (Breaks if a tex# index is skipped.)
            S32 idx;
            if (sscanf(gl_uniform.name.c_str(), "tex%d", &idx) == 1 && idx >= 0 && idx < (S32)max_index)
            {
                gl_uniform.texunit_priority = (U32)idx;
            }
        }
        gl_uniforms.push_back(std::move(gl_uniform));
    }

    // Stable sort so equal-priority (non-reserved) uniforms keep their GL order.
    std::stable_sort(gl_uniforms.begin(), gl_uniforms.end(),
        [](const gl_uniform_data_t& lhs, const gl_uniform_data_t& rhs)
        {
            return lhs.texunit_priority < rhs.texunit_priority;
        });

    // Indexed textures and diffuseMap both want texture channel 0 -- they must never coexist.
    if (max_index > 0)
    {
        llassert_always_msg(!has_diffuse, "Indexed textures and diffuseMap are incompatible!");
    }

    for (const auto& gl_uniform : gl_uniforms)
    {
        mapUniform(gl_uniform);
    }

    // when indexed texture channels are used, enforce an upper limit of 32; this
    // acts as a canary for adding textures and breaking machines limited to 32.
    llassert(max_index == 0 || mActiveTextureChannels <= 32);

    // Set up block binding, in a way supported by Apple (rather than binding = 1 in .glsl).
    // See slide 35 and more of https://docs.huihoo.com/apple/wwdc/2011/session_420__advances_in_opengl_for_mac_os_x_lion.pdf
    for (U32 i = 0; i < NUM_UNIFORM_BLOCKS; ++i)
    {
        GLuint UBOBlockIndex = glGetUniformBlockIndex(mProgramObject, UNIFORM_BLOCK_NAMES[i]);
        if (UBOBlockIndex != GL_INVALID_INDEX)
        {
            glUniformBlockBinding(mProgramObject, UBOBlockIndex, i);
        }
    }

#if !LL_RELEASE_FOR_DOWNLOAD
    // Bindings are live now, so the block a mismatch would be reported against is the one
    // the engine will actually upload to.
    validateEngineBlockLayouts(mProgramObject);
#endif

    unbind();

    // Cached here rather than recomputed per bind: this is read on every program switch for as
    // long as the shadow maps are bound, and mTexture only changes at link.
    mDeclaresShadowSamplers = false;
    if (mTexture.size() > (size_t)LLShaderMgr::DEFERRED_SHADOW5)
    {
        for (S32 i = LLShaderMgr::DEFERRED_SHADOW0; i <= LLShaderMgr::DEFERRED_SHADOW5; ++i)
        {
            if (mTexture[i] > -1)
            {
                mDeclaresShadowSamplers = true;
                break;
            }
        }
    }

    LL_DEBUGS("ShaderUniform") << "Total Uniform Size: " << mTotalUniformSize << LL_ENDL;
    return res;
}

bool LLGLSLShader::hasUniform(U32 index) const
{
    return (index < mUniform.size() && mUniform[index] >= 0);
}

bool LLGLSLShader::link(bool suppress_errors)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    bool success = LLShaderMgr::instance()->linkProgramObject(mProgramObject, suppress_errors, mName);

    if (!success && !suppress_errors)
    {
        LLShaderMgr::instance()->dumpObjectLog(mProgramObject, !success, mName);
    }

    if (success)
    {
        LLShaderMgr::instance()->saveCachedProgramBinary(this);
    }

    return success;
}

// static
void LLGLSLShader::releaseCompareSamplerUnits()
{
    const U32 binds_before = ALTextureSlot::sSamplerBinds;
    U32 units = sCompareSamplerUnits;
    sCompareSamplerUnits = 0;
    for (S32 unit = 0; units != 0; ++unit, units >>= 1)
    {
        if (units & 1u)
        {
            gGL.getTextureSlot(unit)->unbind();
            gGL.getTextureSlot(unit)->bindSampler(0);
        }
    }
    ALTextureSlot::sSamplerBindsShadowCycle += ALTextureSlot::sSamplerBinds - binds_before;
}

void LLGLSLShader::bind()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    llassert_always(mProgramObject != 0);

    gGL.flush();

    if (sCurBoundShader != mProgramObject)  // Don't re-bind current shader
    {
        if (sCurBoundShaderPtr)
        {
            sCurBoundShaderPtr->readProfileQuery();
        }
        LLVertexBuffer::unbind();
        glUseProgram(mProgramObject);
        sCurBoundShader = mProgramObject;
        sCurBoundShaderPtr = this;
        placeProfileQuery();
        LLVertexBuffer::setupClientArrays(mAttributeMask);

        // Shadow maps ride compare samplers on units this program may map to ordinary
        // sampler2Ds -- a pairing GL calls undefined even where the shader never reads the
        // unit. Release them here, at the one place every program switch passes through,
        // rather than by ritual unbind calls at each pass that interleaves shader families.
        // Declaring programs skip this: bindShadowMaps relayouts their units itself.
        if (sCompareSamplerUnits != 0 && !declaresShadowSamplers())
        {
            releaseCompareSamplerUnits();
        }
    }

    if (mEnvUniformsGeneration != sEnvironmentGeneration)
    {
        LLShaderMgr::instance()->updateShaderUniforms(this);
        mEnvUniformsGeneration = sEnvironmentGeneration;
    }

    warnIfVariantMissed();

    llassert_always(sCurBoundShaderPtr != nullptr);
    llassert_always(sCurBoundShader == mProgramObject);
}

// A per-PASS axis only applies if the caller routed through selectVariant() before binding.
// Holding a variant for an axis that is ACTIVE means this program is the base and its corner was
// never selected -- the pass silently runs at the wrong gamma, or stops clipping. Both are
// invisible in a normal frame, so this names the program instead of leaving it to be noticed. A
// corner has no variant of its own axis, so it passes. Gated on gDebugGL rather than SHOW_ASSERT:
// the builds that ship are the ones where a missed site would go unseen.
void LLGLSLShader::warnIfVariantMissed() const
{
    if (LL_LIKELY(!gDebugGL))
    {
        return;
    }

    if (LLRender::sMirrorPass && mMirrorVariant)
    {
        LL_WARNS("Shader") << mName << " bound during the mirror pass without selectVariant()" << LL_ENDL;
    }
    if (LLRender::sClassicMode && mClassicVariant)
    {
        LL_WARNS("Shader") << mName << " bound under classic lighting without selectVariant()" << LL_ENDL;
    }
}

void LLGLSLShader::bind(bool rigged)
{
    if (rigged)
    {
        llassert_always(mRiggedVariant);
        // Checked on THIS program, not on the corner about to be bound. A rigged corner carries
        // no per-pass variants of its own, so the check inside its bind() can never fire -- which
        // would let base.bind(true) skip a classic or mirror corner silently, the one route the
        // check would otherwise miss entirely.
        warnIfVariantMissed();
        mRiggedVariant->bind();
    }
    else
    {
        bind();
    }
}

void LLGLSLShader::unbind(void)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    gGL.flush();
    LLVertexBuffer::unbind();

    if (sCurBoundShaderPtr)
    {
        sCurBoundShaderPtr->readProfileQuery();
    }

    glUseProgram(0);
    sCurBoundShader = 0;
    sCurBoundShaderPtr = NULL;
}

S32 LLGLSLShader::bindTexture(S32 uniform, LLTexture* texture, ALSampler key)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    if (uniform < 0 || uniform >= (S32)mTexture.size())
    {
        LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << uniform << LL_ENDL;
        llassert(false);
        return -1;
    }

    uniform = mTexture[uniform];

    if (uniform > -1)
    {
        gGL.getTextureSlot(uniform)->bindFast(texture, key);
    }

    return uniform;
}

S32 LLGLSLShader::bindTexture(S32 uniform, LLRenderTarget* texture, ALSampler key, U32 index)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    if (uniform < 0 || uniform >= (S32)mTexture.size())
    {
        LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << uniform << LL_ENDL;
        llassert(false);
        return -1;
    }

    uniform = getTextureChannel(uniform);

    if (uniform > -1)
    {
        texture->bindTexture(index, uniform, key);
    }

    return uniform;
}

S32 LLGLSLShader::bindDepthTexture(S32 uniform, LLRenderTarget* texture, ALSampler key)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    if (uniform < 0 || uniform >= (S32)mTexture.size())
    {
        LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << uniform << LL_ENDL;
        llassert(false);
        return -1;
    }

    uniform = getTextureChannel(uniform);

    if (uniform > -1)
    {
        // Clamp by default, and a shadow-compare sampler is never wanted here -- this is a
        // plain depth fetch. A repeat wrap on depth returns the opposite edge of the screen
        // for any fetch that strays outside [0,1], which is geometry from the wrong place
        // rather than a merely inexact sample. These textures carried GL_REPEAT only because
        // allocateDepth never set an address mode. See LLRenderTarget::getDefaultDepthSampler.
        gGL.getTextureSlot(uniform)->bind(texture, true, gGL.getSampler(key));
    }

    return uniform;
}

S32 LLGLSLShader::unbindTexture(S32 uniform)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    if (uniform < 0 || uniform >= (S32)mTexture.size())
    {
        LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << uniform << LL_ENDL;
        llassert(false);
        return -1;
    }

    uniform = mTexture[uniform];

    if (uniform > -1)
    {
        gGL.getTextureSlot(uniform)->unbindFast();
    }

    return uniform;
}

S32 LLGLSLShader::getTextureChannel(S32 uniform) const
{
    return mTexture[uniform];
}

// Resolve the texture channel a uniform is bound to. Nothing more: this used to also activate
// the slot and stamp the expected target onto it, which existed only so disableTexture's check
// below had something to compare against. A slot learns its target from whatever actually gets
// bound, and reads TT_NONE when nothing was -- so the prediction is both unnecessary and less
// truthful than the thing it was predicting.
//
// NO MODE PARAMETER for the same reason. disableTexture keeps its one, because there it names
// the caller's expectation and is checked rather than written.
S32 LLGLSLShader::enableTexture(S32 uniform)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    if (uniform < 0 || uniform >= (S32)mTexture.size())
    {
        LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << uniform << LL_ENDL;
        llassert(false);
        return -1;
    }

    return mTexture[uniform];
}

S32 LLGLSLShader::disableTexture(S32 uniform, ALTextureSlot::eTextureType mode)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    if (uniform < 0 || uniform >= (S32)mTexture.size())
    {
        LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << uniform << LL_ENDL;
        llassert(false);
        return -1;
    }

    S32 index = mTexture[uniform];
    if (index < 0)
    {
        // Invalid texture index - nothing to disable
        return index;
    }

    ALTextureSlot* tex_unit = gGL.getTextureSlot(index);
    if (!tex_unit)
    {
        // Invalid texture unit
        LL_WARNS_ONCE("Shader") << "Invalid texture unit at index: " << index << LL_ENDL;
        return index;
    }

    // TT_NONE means nothing was ever bound here (or it has already been released), so there is
    // no target to disagree with `mode`. Everything else was put there by an actual bind, which
    // makes this comparison a real check on what the shader is about to read rather than a
    // check on bookkeeping the channel setup wrote itself.
    ALTextureSlot::eTextureType curr_type = tex_unit->getCurrType();
    if (curr_type != ALTextureSlot::TT_NONE)
    {
        if (gDebugGL && curr_type != mode)
        {
            if (gDebugSession)
            {
                gFailLog << "Texture channel " << index << " texture type corrupted. Expected: " << mode << ", Found: " << curr_type << std::endl;
                ll_fail("LLGLSLShader::disableTexture failed");
            }
            else
            {
                LL_ERRS() << "Texture channel " << index << " texture type corrupted. Expected: " << mode << ", Found: " << curr_type << LL_ENDL;
            }
        }
        tex_unit->unbind();
    }

    return index;
}

void LLGLSLShader::uniform1i(U32 index, GLint x)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);
    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            if (iter == mValue.end() || iter->second.mV[0] != x)
            {
                glUniform1i(mUniform[index], x);
                mValue[mUniform[index]] = LLVector4((F32)x, 0.f, 0.f, 0.f);
            }
        }
    }
}

void LLGLSLShader::uniform1f(U32 index, GLfloat x)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            if (iter == mValue.end() || iter->second.mV[0] != x)
            {
                glUniform1f(mUniform[index], x);
                mValue[mUniform[index]] = LLVector4(x, 0.f, 0.f, 0.f);
            }
        }
    }
}

void LLGLSLShader::fastUniform1f(U32 index, GLfloat x)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);
    llassert(mProgramObject);
    llassert(index < mUniform.size());
    llassert(mUniform[index] >= 0);
    glUniform1f(mUniform[index], x);
}

void LLGLSLShader::uniform1ui(U32 index, GLuint x)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);
    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            if (iter == mValue.end() || iter->second.mV[0] != (F32)x)
            {
                glUniform1ui(mUniform[index], x);
                mValue[mUniform[index]] = LLVector4((F32)x, 0.f, 0.f, 0.f);
            }
        }
    }
}

void LLGLSLShader::uniform2f(U32 index, GLfloat x, GLfloat y)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            LLVector4 vec(x, y, 0.f, 0.f);
            if (iter == mValue.end() || shouldChange(iter->second, vec))
            {
                glUniform2f(mUniform[index], x, y);
                mValue[mUniform[index]] = vec;
            }
        }
    }
}

void LLGLSLShader::uniform3f(U32 index, GLfloat x, GLfloat y, GLfloat z)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            LLVector4 vec(x, y, z, 0.f);
            if (iter == mValue.end() || shouldChange(iter->second, vec))
            {
                glUniform3f(mUniform[index], x, y, z);
                mValue[mUniform[index]] = vec;
            }
        }
    }
}

void LLGLSLShader::uniform4f(U32 index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            LLVector4 vec(x, y, z, w);
            if (iter == mValue.end() || shouldChange(iter->second, vec))
            {
                glUniform4f(mUniform[index], x, y, z, w);
                mValue[mUniform[index]] = vec;
            }
        }
    }
}

void LLGLSLShader::uniform1iv(U32 index, U32 count, const GLint* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            LLVector4 vec((F32)v[0], 0.f, 0.f, 0.f);
            if (iter == mValue.end() || shouldChange(iter->second, vec) || count != 1)
            {
                glUniform1iv(mUniform[index], count, v);
                mValue[mUniform[index]] = vec;
            }
        }
    }
}

void LLGLSLShader::uniform4iv(U32 index, U32 count, const GLint* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            LLVector4 vec((F32)v[0], (F32)v[1], (F32)v[2], (F32)v[3]);
            if (iter == mValue.end() || shouldChange(iter->second, vec) || count != 1)
            {
                glUniform1iv(mUniform[index], count, v);
                mValue[mUniform[index]] = vec;
            }
        }
    }
}


void LLGLSLShader::uniform1fv(U32 index, U32 count, const GLfloat* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            LLVector4 vec(v[0], 0.f, 0.f, 0.f);
            if (iter == mValue.end() || shouldChange(iter->second, vec) || count != 1)
            {
                glUniform1fv(mUniform[index], count, v);
                mValue[mUniform[index]] = vec;
            }
        }
    }
}

void LLGLSLShader::uniform2fv(U32 index, U32 count, const GLfloat* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            LLVector4 vec(v[0], v[1], 0.f, 0.f);
            if (iter == mValue.end() || shouldChange(iter->second, vec) || count != 1)
            {
                glUniform2fv(mUniform[index], count, v);
                mValue[mUniform[index]] = vec;
            }
        }
    }
}

void LLGLSLShader::uniform3fv(U32 index, U32 count, const GLfloat* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            LLVector4 vec(v[0], v[1], v[2], 0.f);
            if (iter == mValue.end() || shouldChange(iter->second, vec) || count != 1)
            {
                glUniform3fv(mUniform[index], count, v);
                mValue[mUniform[index]] = vec;
            }
        }
    }
}

void LLGLSLShader::uniform4fv(U32 index, U32 count, const GLfloat* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            LLVector4 vec(v[0], v[1], v[2], v[3]);
            if (iter == mValue.end() || shouldChange(iter->second, vec) || count != 1)
            {
                LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
                glUniform4fv(mUniform[index], count, v);
                mValue[mUniform[index]] = vec;
            }
        }
    }
}

void LLGLSLShader::fastUniform4fv(U32 index, U32 count, const GLfloat* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);
    llassert(mProgramObject);
    llassert(index < mUniform.size());
    llassert(mUniform[index] >= 0);
    glUniform4fv(mUniform[index], count, v);
}

void LLGLSLShader::uniform4uiv(U32 index, U32 count, const GLuint* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            const auto& iter = mValue.find(mUniform[index]);
            LLVector4 vec((F32)v[0], (F32)v[1], (F32)v[2], (F32)v[3]);
            if (iter == mValue.end() || shouldChange(iter->second, vec) || count != 1)
            {
                LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
                glUniform4uiv(mUniform[index], count, v);
                mValue[mUniform[index]] = vec;
            }
        }
    }
}

void LLGLSLShader::uniformMatrix2fv(U32 index, U32 count, GLboolean transpose, const GLfloat* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            glUniformMatrix2fv(mUniform[index], count, transpose, v);
        }
    }
}

void LLGLSLShader::uniformMatrix3fv(U32 index, U32 count, GLboolean transpose, const GLfloat* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            glUniformMatrix3fv(mUniform[index], count, transpose, v);
        }
    }
}

void LLGLSLShader::uniformMatrix3x4fv(U32 index, U32 count, GLboolean transpose, const GLfloat* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            glUniformMatrix3x4fv(mUniform[index], count, transpose, v);
        }
    }
}

void LLGLSLShader::uniformMatrix4fv(U32 index, U32 count, GLboolean transpose, const GLfloat* v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    llassert(sCurBoundShaderPtr == this);

    if (mProgramObject)
    {
        if (mUniform.size() <= index)
        {
            LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
            llassert(false);
            return;
        }

        if (mUniform[index] >= 0)
        {
            glUniformMatrix4fv(mUniform[index], count, transpose, v);
        }
    }
}

GLint LLGLSLShader::getUniformLocation(const LLStaticHashedString& uniform)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    GLint ret = -1;
    if (mProgramObject)
    {
        LLStaticStringTable<GLint>::iterator iter = mUniformMap.find(uniform);
        if (iter != mUniformMap.end())
        {
            if (gDebugGL)
            {
                stop_glerror();
                if (iter->second != glGetUniformLocation(mProgramObject, uniform.String().c_str()))
                {
                    LL_ERRS() << "Uniform does not match." << LL_ENDL;
                }
                stop_glerror();
            }
            ret = iter->second;
        }
    }

    return ret;
}

GLint LLGLSLShader::getUniformLocation(U32 index)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    GLint ret = -1;
    if (mProgramObject)
    {
        if (index >= mUniform.size())
        {
            LL_WARNS_ONCE("Shader") << "Uniform index " << index << " out of bounds " << (S32)mUniform.size() << LL_ENDL;
            return ret;
        }
        return mUniform[index];
    }

    return ret;
}

GLint LLGLSLShader::getAttribLocation(U32 attrib)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

    if (attrib < mAttribute.size())
    {
        return mAttribute[attrib];
    }
    else
    {
        return -1;
    }
}

void LLGLSLShader::uniform1i(const LLStaticHashedString& uniform, GLint v)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    GLint location = getUniformLocation(uniform);

    if (location >= 0)
    {
        const auto& iter = mValue.find(location);
        LLVector4 vec((F32)v, 0.f, 0.f, 0.f);
        if (iter == mValue.end() || shouldChange(iter->second, vec))
        {
            glUniform1i(location, v);
            mValue[location] = vec;
        }
    }
}
void LLGLSLShader::vertexAttrib4f(U32 index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (mAttribute[index] > 0)
    {
        glVertexAttrib4f(mAttribute[index], x, y, z, w);
    }
}

void LLGLSLShader::vertexAttrib4fv(U32 index, GLfloat* v)
{
    if (mAttribute[index] > 0)
    {
        glVertexAttrib4fv(mAttribute[index], v);
    }
}

void LLGLSLShader::setMinimumAlpha(F32 minimum)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    if (mMinimumAlpha == minimum)
    {
        // Unchanged: skip the immediate-mode flush (which would pointlessly split any
        // pending batch) and the whole uniform routing below. mMinimumAlpha is the
        // single authority because every writer of MINIMUM_ALPHA comes through here
        // (see its declaration).
        return;
    }
    gGL.flush();
    uniform1f(LLShaderMgr::MINIMUM_ALPHA, minimum);
    mMinimumAlpha = minimum;
}

void LLShaderUniforms::apply(LLGLSLShader* shader)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
    for (auto& uniform : mIntegers)
    {
        shader->uniform1i(uniform.mUniform, uniform.mValue);
    }

    for (auto& uniform : mFloats)
    {
        shader->uniform1f(uniform.mUniform, uniform.mValue);
    }

    for (auto& uniform : mVectors)
    {
        shader->uniform4fv(uniform.mUniform, 1, uniform.mValue.mV);
    }

    for (auto& uniform : mVector3s)
    {
        shader->uniform3fv(uniform.mUniform, 1, uniform.mValue.mV);
    }
}

LLUUID LLGLSLShader::hash()
{
    HBXXH128 hash_obj;
    hash_obj.update(mName);
    hash_obj.update(&mShaderGroup, sizeof(mShaderGroup));
    hash_obj.update(&mShaderLevel, sizeof(mShaderLevel));
    for (const auto& shdr_pair : mShaderFiles)
    {
        hash_obj.update(shdr_pair.first);
        hash_obj.update(&shdr_pair.second, sizeof(GLenum));
    }
    for (const auto& define_pair : mDefines)
    {
        hash_obj.update(define_pair.first);
        hash_obj.update(define_pair.second);

    }
    for (const auto& define_pair : LLGLSLShader::sGlobalDefines)
    {
        hash_obj.update(define_pair.first);
        hash_obj.update(define_pair.second);

    }
    // Injected by loadShaderFile() rather than carried in either defines map, so it has to be
    // folded in by hand -- otherwise a reverse-Z toggle would hand back binaries compiled under
    // the other depth convention.
    hash_obj.update(&LLRender::sReverseZ, sizeof(LLRender::sReverseZ));
    hash_obj.update(&mFeatures, sizeof(LLShaderFeatures));
    hash_obj.update(gGLManager.mGLVendor);
    hash_obj.update(gGLManager.mGLRenderer);
    hash_obj.update(gGLManager.mGLVersionString);
    return hash_obj.digest();
}

#if LL_PROFILER_ENABLE_RENDER_DOC
void LLGLSLShader::setLabel(const char* label) {
    LL_LABEL_OBJECT_GL(GL_PROGRAM, mProgramObject, strlen(label), label);
}
#endif
