/**
 * @file llglslshader_gl_test.cpp
 * @brief LLGLSLShader's uniform value cache on the hidden window: what a program holds, read back from GL, after
 *        each kind of set, across a relink, and where two setters write one location.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llgl.h"
#include "../llglheaders.h"
#include "../llglslshader.h"
#include "../llshadermgr.h"

#include "llheadlessgl_fixture.h"

#include "../test/lltut.h"

#include <array>
#include <bit>
#include <climits>
#include <initializer_list>
#include <string>

namespace tut
{
    namespace
    {
        const char* kVertex =
            "#version 330\n"
            "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";

        // One uniform of each type a setter writes, under reserved names, all folded into the output so each stays
        // active. uGrainAnimate is a bool, which takes the int setters and the float ones alike. The types are this
        // program's own, as the shader manager matches a reserved uniform by name alone: sun_up_factor, an int in the
        // viewer's shaders, is the ivec4 here.
        const char* kFragment =
            "#version 330\n"
            "uniform int light_count;\n"
            "uniform ivec4 sun_up_factor;\n"
            "uniform uint uFrameId;\n"
            "uniform uvec4 cas_param_0;\n"
            "uniform float env_intensity;\n"
            "uniform float minimum_alpha;\n"
            "uniform vec2 screen_res;\n"
            "uniform vec3 sun_dir;\n"
            "uniform vec4 color;\n"
            "uniform vec4 specular_color;\n"
            "uniform vec4 light_diffuse[4];\n"
            "uniform bool uGrainAnimate;\n"
            "out vec4 frag_color;\n"
            "void main()\n"
            "{\n"
            "    vec4 sum = vec4(float(light_count)) + vec4(sun_up_factor) + vec4(float(uFrameId)) + vec4(cas_param_0);\n"
            "    sum += vec4(env_intensity, minimum_alpha, screen_res) + vec4(sun_dir, 0.0) + color + specular_color;\n"
            "    for (int i = 0; i < 4; ++i)\n"
            "    {\n"
            "        sum += light_diffuse[i];\n"
            "    }\n"
            "    frag_color = uGrainAnimate ? sum : -sum;\n"
            "}\n";

        // 2^24: past it a float cannot hold every int, and BIG + 1 rounds to BIG.
        constexpr GLint BIG = 1 << 24;

    // The program the test binds goes back with mGL, and the test's shader manager hands back the shared context's.
    struct llglslshader_data
    {
        ~llglslshader_data()
        {
            LLGLSLShader::unbind();
            mShader.unload();
        }

        // The test program, compiled and linked through LLGLSLShader's own path, mapped and bound.
        void setUp()
        {
            // An error an earlier test left on the shared context is its own: each test ends asserting none of its
            // calls raised one.
            while (glGetError() != GL_NO_ERROR)
            {
            }

            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            ensure("shaders compile", vs && fs);
            mShader.mProgramObject = glCreateProgram();
            mShader.mName = "uniform cache (test)";
            mShader.attachObject(vs);
            mShader.attachObject(fs);
            const bool linked = mShader.mapAttributes();
            glDeleteShader(vs);
            glDeleteShader(fs);
            ensure("program links", linked);
            ensure("program maps", mShader.mapUniforms());
            mShader.bind();
        }

        // The same program linked by GL alone, never through LLGLSLShader::link. 0 if it does not link.
        static GLuint linkRaw()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            if (!vs || !fs)
            {
                if (vs) glDeleteShader(vs);
                if (fs) glDeleteShader(fs);
                return 0;
            }
            const GLuint program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked != GL_TRUE)
            {
                glDeleteProgram(program);
                return 0;
            }
            return program;
        }

        GLint location(const char* name) const
        {
            return glGetUniformLocation(mShader.mProgramObject, name);
        }

        // What the program holds at a uniform, as GL reads it back, against what a check expects of its first
        // components. Floats are compared by their bits, so a check is exact.
        void ensureInts(const std::string& what, const char* name, std::initializer_list<GLint> expected) const
        {
            std::array<GLint, 4> held = {};
            glGetUniformiv(mShader.mProgramObject, location(name), held.data());
            size_t i = 0;
            for (const GLint value : expected)
            {
                ensure_equals(what + ", component " + std::to_string(i), held[i], value);
                ++i;
            }
        }

        void ensureUints(const std::string& what, const char* name, std::initializer_list<GLuint> expected) const
        {
            std::array<GLuint, 4> held = {};
            glGetUniformuiv(mShader.mProgramObject, location(name), held.data());
            size_t i = 0;
            for (const GLuint value : expected)
            {
                ensure_equals(what + ", component " + std::to_string(i), held[i], value);
                ++i;
            }
        }

        void ensureFloats(const std::string& what, const char* name, std::initializer_list<F32> expected) const
        {
            std::array<F32, 4> held = {};
            glGetUniformfv(mShader.mProgramObject, location(name), held.data());
            size_t i = 0;
            for (const F32 value : expected)
            {
                ensure_equals(what + ", component " + std::to_string(i) + " bits",
                              std::bit_cast<U32>(held[i]), std::bit_cast<U32>(value));
                ++i;
            }
        }

        ll_test::SharedGLScope mGL;
        // Made before the program and gone after it: mapUniforms reads its reserved names.
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mShader;
    };

    typedef test_group<llglslshader_data> llglslshader_t;
    typedef llglslshader_t::object llglslshader_object_t;
    tut::llglslshader_t tut_llglslshader("llglslshader_gl");
    }

    // Ints and uints arrive exactly past 2^24: each set below differs from the one before only in values that
    // convert to the same float, and only in some of its components.
    template<> template<>
    void llglslshader_object_t::test<1>()
    {
        setUp();

        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, BIG);
        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, BIG + 1);
        ensureInts("int", "light_count", { BIG + 1 });

        const GLint first[4]  = { BIG,     -BIG,     BIG, BIG };
        const GLint second[4] = { BIG,     -BIG,     BIG, BIG + 1 };
        const GLint third[4]  = { BIG + 1, -BIG - 1, BIG, BIG + 1 };
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, first);
        ensureInts("ivec4", "sun_up_factor", { BIG, -BIG, BIG, BIG });
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, second);
        ensureInts("ivec4, w alone changed", "sun_up_factor", { BIG, -BIG, BIG, BIG + 1 });
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, third);
        ensureInts("ivec4, x and y changed", "sun_up_factor", { BIG + 1, -BIG - 1, BIG, BIG + 1 });

        mShader.uniform1ui(LLShaderMgr::FRAME_ID, (GLuint)BIG);
        mShader.uniform1ui(LLShaderMgr::FRAME_ID, (GLuint)BIG + 1);
        ensureUints("uint", "uFrameId", { (GLuint)BIG + 1 });

        // The sharpening constants are floats' bits. 1.0's and the next float up's are both 1065353216 as floats.
        const GLuint one[4]  = { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u };
        const GLuint next[4] = { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800001u };
        mShader.uniform4uiv(LLShaderMgr::CAS_PARAM_0, 1, one);
        mShader.uniform4uiv(LLShaderMgr::CAS_PARAM_0, 1, next);
        ensureUints("uvec4, w alone changed", "cas_param_0", { 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800001u });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A, then B, then A again holds A, for every kind of set: each upload replaces what its location's slot holds,
    // so the third set is compared with B. The pairs mix a setter's value and array forms, which share a kind.
    template<> template<>
    void llglslshader_object_t::test<2>()
    {
        setUp();

        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 0.5f);
        ensureFloats("vec4 B", "color", { 0.25f, 0.5f, 0.75f, 0.5f });
        const F32 color[4] = { 0.25f, 0.5f, 0.75f, 1.f };
        mShader.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color);
        ensureFloats("vec4 A again", "color", { 0.25f, 0.5f, 0.75f, 1.f });

        mShader.uniform3f(LLShaderMgr::DEFERRED_SUN_DIR, 0.f, 0.5f, 1.f);
        const F32 sun[3] = { 0.f, 0.5f, 0.25f };
        mShader.uniform3fv(LLShaderMgr::DEFERRED_SUN_DIR, 1, sun);
        ensureFloats("vec3 B", "sun_dir", { 0.f, 0.5f, 0.25f });
        mShader.uniform3f(LLShaderMgr::DEFERRED_SUN_DIR, 0.f, 0.5f, 1.f);
        ensureFloats("vec3 A again", "sun_dir", { 0.f, 0.5f, 1.f });

        mShader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, 640.f, 480.f);
        const F32 res[2] = { 640.f, 360.f };
        mShader.uniform2fv(LLShaderMgr::DEFERRED_SCREEN_RES, 1, res);
        ensureFloats("vec2 B", "screen_res", { 640.f, 360.f });
        mShader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, 640.f, 480.f);
        ensureFloats("vec2 A again", "screen_res", { 640.f, 480.f });

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.25f);
        const F32 intensity = 0.5f;
        mShader.uniform1fv(LLShaderMgr::ENVIRONMENT_INTENSITY, 1, &intensity);
        ensureFloats("float B", "env_intensity", { 0.5f });
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.25f);
        ensureFloats("float A again", "env_intensity", { 0.25f });

        const GLint ivec_a[4] = { 1, 2, 3, 4 };
        const GLint ivec_b[4] = { 1, 2, 3, 5 };
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, ivec_a);
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, ivec_b);
        mShader.uniform4iv(LLShaderMgr::SUN_UP_FACTOR, 1, ivec_a);
        ensureInts("ivec4 A again", "sun_up_factor", { 1, 2, 3, 4 });

        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        const GLint count = 4;
        mShader.uniform1iv(LLShaderMgr::MULTI_LIGHT_COUNT, 1, &count);
        ensureInts("int B", "light_count", { 4 });
        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        ensureInts("int A again", "light_count", { 3 });

        mShader.uniform1ui(LLShaderMgr::FRAME_ID, 7u);
        mShader.uniform1ui(LLShaderMgr::FRAME_ID, 8u);
        mShader.uniform1ui(LLShaderMgr::FRAME_ID, 7u);
        ensureUints("uint A again", "uFrameId", { 7u });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A float set twice holds its value, and a new value replaces it. Raw uploads behind the cache's back show
    // which sets reached GL: just after a link the cache holds nothing, not a zero, so a set of 0 uploads; and a set
    // of the value last uploaded does not.
    template<> template<>
    void llglslshader_object_t::test<3>()
    {
        setUp();
        const GLint loc = location("env_intensity");
        ensure("env_intensity is active", loc >= 0);

        glUniform1f(loc, 5.f);
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.f);
        ensureFloats("the first set uploads, a zero too", "env_intensity", { 0.f });

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        ensureFloats("set twice", "env_intensity", { 0.5f });

        glUniform1f(loc, 2.f);
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        ensureFloats("the value last uploaded is not uploaded again", "env_intensity", { 2.f });

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.75f);
        ensureFloats("a new value replaces it", "env_intensity", { 0.75f });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // LLGLSLShader::link forgets the cache: the relink resets every uniform to 0, and the value set before it,
    // set again, arrives.
    template<> template<>
    void llglslshader_object_t::test<4>()
    {
        setUp();
        const GLint before = location("color");
        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        ensureFloats("set before the relink", "color", { 0.25f, 0.5f, 0.75f, 1.f });

        ensure("program relinks", mShader.link());
        if (location("color") != before)
        {
            skip("the relink moved the uniform, so a stale slot would not be consulted");
        }
        ensureFloats("the relink reset it", "color", { 0.f, 0.f, 0.f, 0.f });

        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        ensureFloats("set again after the relink", "color", { 0.25f, 0.5f, 0.75f, 1.f });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // mapUniforms forgets the cache, and setMinimumAlpha's own: a new program, linked by GL alone and mapped, holds
    // the values set again after it, though they are the ones set on the program it replaced.
    template<> template<>
    void llglslshader_object_t::test<5>()
    {
        setUp();
        const GLint before = location("color");
        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        mShader.setMinimumAlpha(0.5f);
        ensureFloats("colour on the old program", "color", { 0.25f, 0.5f, 0.75f, 1.f });
        ensureFloats("minimum alpha on the old program", "minimum_alpha", { 0.5f });

        const GLuint fresh = linkRaw();
        ensure("the new program links", fresh != 0);
        LLGLSLShader::unbind();
        const GLuint old = mShader.mProgramObject;
        mShader.mProgramObject = fresh;
        forget_program_samplers(old);
        glDeleteProgram(old);
        ensure("the new program maps", mShader.mapUniforms());
        mShader.bind();
        if (location("color") != before)
        {
            skip("the new program put the uniform elsewhere, so a stale slot would not be consulted");
        }
        ensureFloats("the new program starts at 0", "color", { 0.f, 0.f, 0.f, 0.f });

        mShader.uniform4f(LLShaderMgr::DIFFUSE_COLOR, 0.25f, 0.5f, 0.75f, 1.f);
        ensureFloats("colour set again", "color", { 0.25f, 0.5f, 0.75f, 1.f });
        mShader.setMinimumAlpha(0.5f);
        ensureFloats("minimum alpha set again", "minimum_alpha", { 0.5f });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A bool takes the int setters and the float ones, and holds whichever set it last, the int set after a float
    // one included. INT_MIN and -0.0 share their bits: true as an int, false as a float, so only the kind a slot
    // records tells the float set from the int one before it.
    template<> template<>
    void llglslshader_object_t::test<6>()
    {
        setUp();

        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 1);
        ensureInts("true by int", "uGrainAnimate", { 1 });
        mShader.uniform1f(LLShaderMgr::GRAIN_ANIMATE, 0.f);
        ensureInts("false by float", "uGrainAnimate", { 0 });
        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, 1);
        ensureInts("true by int again", "uGrainAnimate", { 1 });

        // Read at run time, so nothing folds the sign of the zero away.
        volatile U32 sign_bit = 0x80000000u;
        const F32 negative_zero = std::bit_cast<F32>(static_cast<U32>(sign_bit));
        mShader.uniform1i(LLShaderMgr::GRAIN_ANIMATE, INT_MIN);
        ensureInts("true by INT_MIN", "uGrainAnimate", { 1 });
        mShader.uniform1f(LLShaderMgr::GRAIN_ANIMATE, negative_zero);
        ensureInts("false by -0.0, the same bits", "uGrainAnimate", { 0 });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // The reserved index and the name of one uniform share its slot: A by index, B by name, then A by index holds A.
    template<> template<>
    void llglslshader_object_t::test<7>()
    {
        setUp();
        const LLStaticHashedString light_count("light_count");

        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        mShader.uniform1i(light_count, 4);
        ensureInts("by name", "light_count", { 4 });
        mShader.uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, 3);
        ensureInts("by index after the name", "light_count", { 3 });
        mShader.uniform1i(light_count, 4);
        ensureInts("by name after the index", "light_count", { 4 });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // The fast setters upload without comparing and leave the cache describing what they uploaded: a cached set of
    // the value before a fast one arrives.
    template<> template<>
    void llglslshader_object_t::test<8>()
    {
        setUp();

        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        mShader.fastUniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.25f);
        ensureFloats("fast float", "env_intensity", { 0.25f });
        mShader.uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.5f);
        ensureFloats("cached float after the fast one", "env_intensity", { 0.5f });

        const F32 a[4] = { 0.25f, 0.5f, 0.75f, 1.f };
        const F32 b[4] = { 1.f, 0.75f, 0.5f, 0.25f };
        mShader.uniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, a);
        mShader.fastUniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, b);
        ensureFloats("fast vec4", "specular_color", { 1.f, 0.75f, 0.5f, 0.25f });
        mShader.uniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, a);
        ensureFloats("cached vec4 after the fast one", "specular_color", { 0.25f, 0.5f, 0.75f, 1.f });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // An array set uploads every time and forgets what its location held: element 0 set alone, then the array over
    // it, then element 0 alone again holds element 0's value; and an array whose element 0 is unchanged still
    // reaches GL.
    template<> template<>
    void llglslshader_object_t::test<9>()
    {
        setUp();

        const F32 single[4]    = { 0.25f, 0.5f, 0.75f, 1.f };
        const F32 two[8]       = { 1.f, 1.f, 1.f, 1.f,   0.5f, 0.5f, 0.5f, 0.5f };
        const F32 two_again[8] = { 1.f, 1.f, 1.f, 1.f,   0.25f, 0.25f, 0.25f, 0.25f };

        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 1, single);
        ensureFloats("element 0 alone", "light_diffuse[0]", { 0.25f, 0.5f, 0.75f, 1.f });
        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 2, two);
        ensureFloats("element 0 by the array", "light_diffuse[0]", { 1.f, 1.f, 1.f, 1.f });
        ensureFloats("element 1 by the array", "light_diffuse[1]", { 0.5f, 0.5f, 0.5f, 0.5f });
        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 1, single);
        ensureFloats("element 0 alone again", "light_diffuse[0]", { 0.25f, 0.5f, 0.75f, 1.f });

        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 2, two);
        mShader.uniform4fv(LLShaderMgr::LIGHT_DIFFUSE, 2, two_again);
        ensureFloats("element 1 by an array whose element 0 did not change", "light_diffuse[1]",
                     { 0.25f, 0.25f, 0.25f, 0.25f });

        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }
}
