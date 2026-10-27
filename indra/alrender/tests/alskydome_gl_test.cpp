/**
 * @file alskydome_gl_test.cpp
 * @brief WindLight's dome two ways on the hidden window: the sky program over the dome's mesh, as it was drawn before
 *        the sky's composite (tests/skyraster/deferred/skyF.glsl on ALSkyDome's strips, placed as
 *        LLDrawPoolWLSky::renderDome placed them), and windlightDome along each pixel's
 *        view ray turned into the dome's frame by ALFarPlane::eyeToSkyFrame, as the haze calls it for far water.
 *        Above the camera's level the two differ by the facets; below it the mesh's skirt leaves the sphere the function
 *        continues.
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

#include "alskyraster_gl.h"

#include <limits>

namespace
{
    using namespace al_sky_test;

    constexpr S32 W = 192;
    constexpr S32 H = 128;

    // The default sky at midday, the same with a rainbow's moisture and a halo's ice, a sunset under thicker haze, and
    // a night lit by the moon, whose glow factor below 1 takes the glow away.
    const Sky* const DOME_SKIES[] = { &SKIES[0], &RAINBOW_SKY, &SKIES[1], &SKIES[2] };

    // The views' vertical field of view, in degrees: a tangent of 0.6 at its half.
    const F32 FOV = 2.f * atanf(0.6f) * RAD_TO_DEG;

    // Toward the light and away from it at the horizon, up into the cap, and down onto water.
    std::array<View, 4> views(const Sky& sky)
    {
        return { View{ sky.mAzimuth, 0.f, FOV }, View{ sky.mAzimuth + 180.f, -4.f, FOV },
                 View{ sky.mAzimuth + 60.f, 35.f, FOV }, View{ sky.mAzimuth - 30.f, -22.f, FOV } };
    }

    // The analytic pass: a screen triangle, each pixel's view ray in eye space turned into the dome's frame.
    const char* kRayVertex =
        "#version 400\n"
        "in vec3 position;\n"
        "out vec2 vary_ndc;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = vec4(position.xy, 0.0, 1.0);\n"
        "    vary_ndc = position.xy;\n"
        "}\n";

    const char* kRayFragment =
        "#version 400\n"
        "in vec2 vary_ndc;\n"
        "out vec4 frag_color;\n"
        "uniform vec2 test_tan_half;\n"
        "uniform mat3 test_eye_to_sky;\n"
        "vec3 windlightDome(vec3 dir);\n"
        "void main()\n"
        "{\n"
        "    vec3 eye = vec3(vary_ndc * test_tan_half, -1.0);\n"
        "    frag_color = vec4(windlightDome(test_eye_to_sky * normalize(eye)), 1.0);\n"
        "}\n";

    // The haze pass's screen triangle (deferred/hazeV.glsl) without the atmospherics' varyings.
    const char* kHazeVertex = R"(/*[EXTRA_CODE_HERE]*/

in vec3 position;

out vec2 vary_fragcoord;

void main()
{
    gl_Position = vec4(position.xy, 0.0, 1.0);
    vary_fragcoord = position.xy * 0.5 + 0.5;
}
)";

    // What the haze pass reads of the scene and of WindLight's in-scatter, made up so that the dome's weight runs
    // across the screen: none over the left quarter, then the square of the distance from it, a little less in green and
    // blue. The water lies 1 km out along each pixel's ray, and WindLight's in-scatter grows with the weight.
    const char* kHazeScene = R"(/*[EXTRA_CODE_HERE]*/

in vec2 vary_fragcoord;

uniform vec2 test_tan_half;

float getDepth(vec2 tc)
{
    return 0.5;
}

bool isFarDepth(float d)
{
    return false;
}

vec4 getNorm(vec2 tc)
{
    return vec4(0.0, 0.0, 1.0, 0.0);
}

vec4 getPositionWithDepth(vec2 tc, float depth)
{
    return vec4(normalize(vec3((tc * 2.0 - 1.0) * test_tan_half, -1.0)) * 1000.0, 1.0);
}

void calcAtmosphericVarsLinear(vec3 inPositionEye, vec3 norm, vec3 light_dir, out vec3 sunlit, out vec3 amblit,
                               out vec3 additive, out vec3 atten)
{
    float x = max(vary_fragcoord.x - 0.25, 0.0);
    vec3 weight = vec3(1.0, 0.9, 0.8) * (x * x);
    atten = vec3(1.0) - weight;
    additive = vec3(0.5, 0.6, 0.7) * weight;
    sunlit = vec3(0.0);
    amblit = vec3(0.0);
}

vec3 getAdditiveColor()
{
    return vec3(0.0);
}

vec3 getAtmosAttenuation()
{
    return vec3(1.0);
}
)";

    // The weight the haze scene gives the dome at column @a x, in its strongest channel.
    F32 hazeWeight(S32 x)
    {
        const F32 from = llmax(((F32)x + 0.5f) / (F32)W - 0.25f, 0.f);
        return from * from;
    }

    struct alskydome_data
    {
        alskydome_data()
        : mShaders(mTree.prefix())
        {
        }

        ~alskydome_data()
        {
            LLGLSLShader::unbind();
            mSky.unload();
            mRay.unload();
            mShaders.clearShaderObjects();
            mFull.release();
            mDome = nullptr;
            mTriangle = nullptr;
            mRainbow = nullptr;
            mHalo = nullptr;
            if (mEnvironment)
            {
                glDeleteBuffers(1, &mEnvironment);
            }
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
        }

        // The sky program as LLViewerShaderMgr built it, without the emissive buffer, so it writes the dome's colour
        // to the albedo attachment as it is; the analytic pass; the dome's mesh; and a float target the size of the
        // view.
        void setUp()
        {
            mTree.populate();
            loadSharedObjects(mShaders);
            buildProgram(mSky, "Deferred Windlight Sky Shader",
                         { { "skyraster/deferred/skyV.glsl", GL_VERTEX_SHADER },
                           { "skyraster/deferred/skyF.glsl", GL_FRAGMENT_SHADER },
                           { "windlight/windlightDomeF.glsl", GL_FRAGMENT_SHADER } },
                         false);

            {
                std::map<std::string, std::string> defines;
                S32 level = 1;
                const GLuint dome = mShaders.loadShaderFile("windlight/windlightDomeF.glsl", level, GL_FRAGMENT_SHADER, &defines);
                UNSCOPED_INFO("windlightDomeF compiles");
                REQUIRE(dome != 0);
                const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kRayVertex);
                const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kRayFragment);
                UNSCOPED_INFO("the ray pass compiles");
                REQUIRE(vs != 0);
                REQUIRE(fs != 0);
                mRay.mProgramObject = glCreateProgram();
                mRay.mName = "windlightDome along the ray (test)";
                mRay.attachObject(vs);
                mRay.attachObject(fs);
                mRay.attachObject(dome);
                UNSCOPED_INFO("the ray pass links");
                REQUIRE(mRay.mapAttributes());
                glDeleteShader(vs);
                glDeleteShader(fs);
                UNSCOPED_INFO("the ray pass maps");
                REQUIRE(mRay.mapUniforms());
            }

            mDome     = domeMesh();
            mTriangle = screenTriangle();
            mRainbow  = gradient(1);
            mHalo     = gradient(4);

            REQUIRE(mFull.allocate(W, H, GL_RGBA32F));

            glGenBuffers(1, &mEnvironment);
        }

        // The sky program over the mesh into the float target, every band drawn on the far plane in order, so the
        // later covers the earlier where they overlap. Read back.
        std::vector<F32> raster(const Sky& sky, const View& view)
        {
            mFull.bindTarget();
            gGL.setClearColor(LLColor4(-1.f, -1.f, -1.f, 0.f));
            mFull.clear(GL_COLOR_BUFFER_BIT);
            gGL.setClearColor(LLColor4::transparent);

            loadView(view, W, H);
            bindEnvironment(mSky, mEnvironment, sky, ALFarPlane::eyeToSkyFrame(gGL.getModelviewMatrix()));
            mSky.bind();
            bindDome(mSky, sky, mRainbow, mHalo);
            {
                LLGLDisable   blend(GL_BLEND);
                LLGLDisable   cull(GL_CULL_FACE);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                drawDome(mDome);
            }
            mSky.unbind();

            std::vector<F32> out(size_t(W) * H * 4);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out.data());
            mFull.flush();
            return out;
        }

        // windlightDome along each pixel's view ray, through the rotation from the eye to the dome's frame the
        // Environment block's eyeToSky holds. Read back.
        std::vector<F32> analytic(const Sky& sky, const View& view)
        {
            mFull.bindTarget();
            gGL.setClearColor(LLColor4(-1.f, -1.f, -1.f, 0.f));
            mFull.clear(GL_COLOR_BUFFER_BIT);
            gGL.setClearColor(LLColor4::transparent);

            loadView(view, W, H);
            mEyeToSky = ALFarPlane::eyeToSkyFrame(gGL.getModelviewMatrix());
            bindEnvironment(mRay, mEnvironment, sky, mEyeToSky);
            mRay.bind();
            bindDome(mRay, sky, mRainbow, mHalo);
            const F32 tan_half = tanf(view.mFov * 0.5f * DEG_TO_RAD);
            glUniform2f(glGetUniformLocation(mRay.mProgramObject, "test_tan_half"), tan_half * (F32)W / (F32)H, tan_half);
            glUniformMatrix3fv(glGetUniformLocation(mRay.mProgramObject, "test_eye_to_sky"), 1, GL_FALSE, mEyeToSky.data());
            {
                LLGLDisable   blend(GL_BLEND);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            mRay.unbind();

            std::vector<F32> out(size_t(W) * H * 4);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out.data());
            mFull.flush();
            return out;
        }

        static LLVector3 colour(const std::vector<F32>& image, S32 x, S32 y)
        {
            const F32* p = &image[(size_t(y) * W + x) * 4];
            return LLVector3(p[0], p[1], p[2]);
        }

        SkyShaderTree             mTree;
        ll_test::SharedGLScope    mGL;
        ll_test::TestShaderMgr    mShaders;
        LLGLSLShader              mSky;
        LLGLSLShader              mRay;
        LLRenderTarget            mFull;
        LLPointer<LLVertexBuffer> mDome;
        LLPointer<LLVertexBuffer> mTriangle;
        LLPointer<LLImageGL>      mRainbow;
        LLPointer<LLImageGL>      mHalo;
        GLuint                    mEnvironment = 0;
        std::array<F32, 9>        mEyeToSky{};
    };

    // How far windlightDome may lie from the raster dome, relative to the brighter of the two (or to RELATIVE_FLOOR,
    // where both are darker): above the camera's level, where the mesh's facets are all that separates them, most near
    // the horizon, where the colour turns fastest with the 50 m the dome raises its point by; in the sliver below the
    // camera's level, where the skirt's bands and the sphere disagree on which side of the camera's level that raise
    // puts the point; and below it. There the mesh's skirt lies some 3 km from the camera where the sphere lies up to
    // 17 km away, so the raise tilts the mesh's rays up to 0.8 degrees and the sphere's 0.13: the glow barely notices,
    // a rainbow's or halo's band shifts by the difference.
    constexpr F32 RELATIVE_FLOOR = 0.02f;
    constexpr F32 CAP_BOUND      = 0.01f;
    constexpr F32 SLIVER_BOUND   = 0.04f;
    constexpr F32 SKIRT_BOUND    = 0.04f;
    constexpr F32 SKIRT_BANDS    = 0.25f;
    // How far below the camera's level the sliver reaches: the 50 m raise, over the 4.2 km to the sphere at the
    // horizon, is 0.68 degrees.
    constexpr F32 SLIVER = 0.0175f;

    F32 relativeDifference(const LLVector3& a, const LLVector3& b)
    {
        F32 worst = 0.f;
        for (S32 c = 0; c < 3; ++c)
        {
            const F32 scale = llmax(RELATIVE_FLOOR, llmax(fabsf(a.mV[c]), fabsf(b.mV[c])));
            worst = llmax(worst, fabsf(a.mV[c] - b.mV[c]) / scale);
        }
        return worst;
    }

    // The haze's weight floor, below which it leaves the dome out (class3/deferred/hazeF.glsl's HAZE_DOME_MIN_WEIGHT),
    // and what it allows there: 0.94% of the in-scatter, or 2e-5 times WindLight's HDR scale.
    constexpr F32 HAZE_FLOOR    = 1.f / 256.f;
    constexpr F32 HAZE_RELATIVE = 0.0094f;
    constexpr F32 HAZE_ABSOLUTE = 2e-5f;
    constexpr F32 SKY_HDR_SCALE = 1.25f;

    // A map whose every texel is infinite, so that any read of it shows, even one weighed by zero.
    LLPointer<LLImageGL> infiniteMap()
    {
        constexpr S32 SIZE = 4;
        const std::vector<F32> texels(size_t(SIZE) * SIZE * 4, std::numeric_limits<F32>::infinity());
        LLPointer<LLImageGL> map = new LLImageGL(SIZE, SIZE, 4, false);
        map->setNeedsAlphaAndPickMask(false);
        map->setExplicitFormat(GL_RGBA32F, GL_RGBA, GL_FLOAT);
        UNSCOPED_INFO("the infinite map");
        REQUIRE(map->createGLTexture(0, reinterpret_cast<const U8*>(texels.data())));
        return map;
    }
}

TEST_CASE_METHOD(alskydome_data, "alskydome_gl: windlightDome is the raster dome above the camera's level and continues it below",
                 "[alskydome_gl]")
{
    setUp();
    S32 cap_pixels = 0;
    S32 skirt_pixels = 0;
    for (const Sky* entry : DOME_SKIES)
    {
        const Sky& sky = *entry;
        const bool bands = sky.mMoisture > 0.f || sky.mIce > 0.f;
        for (const View& view : views(sky))
        {
            CAPTURE(sky.mName, view.mBearing, view.mPitch);
            const std::vector<F32> mesh = raster(sky, view);
            const std::vector<F32> ray  = analytic(sky, view);

            F32 cap = 0.f;
            F32 sliver = 0.f;
            F32 skirt = 0.f;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    const LLVector3 m = colour(mesh, x, y);
                    if (m.mV[VRED] < 0.f)
                    {
                        CAPTURE(x, y);
                        FAIL("the mesh leaves a pixel uncovered");
                    }
                    const F32 diff = relativeDifference(colour(ray, x, y), m);
                    const F32 rise = domeRay(view, mEyeToSky, W, H, x, y).mV[VY];
                    if (rise >= 0.f)
                    {
                        cap = llmax(cap, diff);
                        ++cap_pixels;
                    }
                    else
                    {
                        F32& below = rise > -SLIVER ? sliver : skirt;
                        below = llmax(below, diff);
                        ++skirt_pixels;
                    }
                }
            }
            CAPTURE(cap, sliver, skirt);
            CHECK(cap < CAP_BOUND);
            CHECK(sliver < SLIVER_BOUND);
            CHECK(skirt < (bands ? SKIRT_BANDS : SKIRT_BOUND));
        }
    }
    UNSCOPED_INFO("the views see the cap and the skirt");
    CHECK(cap_pixels > 0);
    CHECK(skirt_pixels > 0);
}

TEST_CASE_METHOD(alskydome_data, "alskydome_gl: windlightDome reads the rainbow's and halo's maps only where the sky sets them",
                 "[alskydome_gl]")
{
    setUp();
    const LLPointer<LLImageGL> rainbow = mRainbow;
    const LLPointer<LLImageGL> halo    = mHalo;
    const LLPointer<LLImageGL> poison  = infiniteMap();
    // The rainbow and halo sky, with each of its two strengths and with neither.
    const Sky& both = RAINBOW_SKY;
    for (const auto& [moisture, ice] : { std::make_pair(0.f, 0.f), std::make_pair(both.mMoisture, 0.f),
                                         std::make_pair(0.f, both.mIce), std::make_pair(both.mMoisture, both.mIce) })
    {
        Sky sky = both;
        sky.mMoisture = moisture;
        sky.mIce = ice;
        const View view = views(sky)[1];
        mRainbow = rainbow;
        mHalo = halo;
        const std::vector<F32> plain = analytic(sky, view);
        for (S32 poisoned_map = 0; poisoned_map < 2; ++poisoned_map)
        {
            const bool rainbow_poisoned = poisoned_map == 0;
            mRainbow = rainbow_poisoned ? poison : rainbow;
            mHalo = rainbow_poisoned ? halo : poison;
            const std::vector<F32> poisoned = analytic(sky, view);
            S32 differ = 0;
            for (size_t px = 0; px < size_t(W) * H; ++px)
            {
                for (S32 c = 0; c < 3; ++c)
                {
                    differ += !(poisoned[px * 4 + c] == plain[px * 4 + c]);
                }
            }
            const bool weighed = rainbow_poisoned ? moisture != 0.f : ice != 0.f;
            CAPTURE(moisture, ice, rainbow_poisoned, differ);
            if (weighed)
            {
                UNSCOPED_INFO("a map the sky weighs in is read");
                CHECK(differ > 0);
            }
            else
            {
                UNSCOPED_INFO("a map the sky weighs at zero is not read");
                CHECK(differ == 0);
            }
        }
    }
    mRainbow = rainbow;
    mHalo = halo;
}

TEST_CASE_METHOD(alskydome_data, "alskydome_gl: the haze on water leaves the dome out only where its weight cannot show",
                 "[alskydome_gl]")
{
    setUp();
    // The haze on water as the viewer builds it, and the same with its weight floor at zero, so that it converges on
    // the dome at every pixel, over the made-up scene.
    std::error_code ec;
    const std::string haze = LLFile::getContents(std::string(AL_TEST_SHADER_DIR) + "/class3/deferred/hazeF.glsl", ec);
    REQUIRE(!ec);
    const std::string floor_test = ">= HAZE_DOME_MIN_WEIGHT";
    const size_t at = haze.find(floor_test);
    UNSCOPED_INFO("the haze tests the dome's weight against its floor");
    REQUIRE(at != std::string::npos);
    std::string everywhere = haze;
    everywhere.replace(at, floor_test.size(), ">= 0.0");
    REQUIRE(mTree.write("skytest/hazeF.glsl", haze));
    REQUIRE(mTree.write("skytest/hazeEverywhereF.glsl", everywhere));
    REQUIRE(mTree.write("skytest/hazeV.glsl", kHazeVertex));
    REQUIRE(mTree.write("skytest/hazeSceneF.glsl", kHazeScene));
    LLGLSLShader programs[2];
    for (S32 floored = 0; floored < 2; ++floored)
    {
        buildProgram(programs[floored], floored ? "Haze On Water Shader (test)" : "Haze On Water Shader, no floor (test)",
                     { { "skytest/hazeV.glsl", GL_VERTEX_SHADER },
                       { floored ? "skytest/hazeF.glsl" : "skytest/hazeEverywhereF.glsl", GL_FRAGMENT_SHADER },
                       { "skytest/hazeSceneF.glsl", GL_FRAGMENT_SHADER },
                       { "windlight/atmosphericsF.glsl", GL_FRAGMENT_SHADER },
                       { "windlight/windlightDomeF.glsl", GL_FRAGMENT_SHADER } },
                     false, { { "HAZE_ON_WATER", "1" } });
    }

    // Down onto the water toward a low sun, where the dome behind it glows.
    const Sky& sky = SKIES[1];
    const View view{ sky.mAzimuth, -10.f, FOV };
    std::vector<F32> pictures[2];
    for (S32 floored = 0; floored < 2; ++floored)
    {
        LLGLSLShader& program = programs[floored];
        mFull.bindTarget();
        loadView(view, W, H);
        bindEnvironment(program, mEnvironment, sky, ALFarPlane::eyeToSkyFrame(gGL.getModelviewMatrix()));
        program.bind();
        bindDome(program, sky, mRainbow, mHalo);
        program.uniform1f(LLShaderMgr::SKY_HDR_SCALE, SKY_HDR_SCALE);
        const F32 tan_half = tanf(view.mFov * 0.5f * DEG_TO_RAD);
        glUniform2f(glGetUniformLocation(program.mProgramObject, "test_tan_half"), tan_half * (F32)W / (F32)H, tan_half);
        glUniform4f(glGetUniformLocation(program.mProgramObject, "waterPlane"), 0.f, 0.f, 0.f, 1.f);
        glUniform1f(glGetUniformLocation(program.mProgramObject, "waterGroundFade"), 0.f);
        {
            LLGLDisable   blend(GL_BLEND);
            LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
            mTriangle->setBuffer();
            mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
        }
        program.unbind();
        pictures[floored].resize(size_t(W) * H * 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, pictures[floored].data());
        mFull.flush();
    }

    // Where the weight is zero, or at the floor and past it, the two are one picture; under the floor they differ by
    // what the floor allows.
    S32 none = 0;
    S32 under = 0;
    S32 over = 0;
    S32 apart = 0;
    F32 most = 0.f;
    for (S32 y = 0; y < H; ++y)
    {
        for (S32 x = 0; x < W; ++x)
        {
            const F32 weight = hazeWeight(x);
            const bool skipped = weight > 0.f && weight < HAZE_FLOOR;
            none += weight == 0.f;
            under += skipped;
            over += weight >= HAZE_FLOOR;
            for (S32 c = 0; c < 3; ++c)
            {
                const size_t i = (size_t(y) * W + x) * 4 + c;
                const F32 floored = pictures[1][i];
                const F32 converged = pictures[0][i];
                const F32 diff = fabsf(floored - converged);
                if (skipped)
                {
                    most = llmax(most, diff / llmax(converged, 1e-6f));
                    apart += diff > llmax(HAZE_RELATIVE * converged, HAZE_ABSOLUTE * SKY_HDR_SCALE);
                }
                else
                {
                    apart += floored != converged;
                }
            }
        }
    }
    CAPTURE(none, under, over, most);
    UNSCOPED_INFO("the scene runs the weight from zero through the floor");
    CHECK(none > 0);
    CHECK(under > 0);
    CHECK(over > 0);
    CHECK(apart == 0);
    for (LLGLSLShader& program : programs)
    {
        program.unload();
    }
}
