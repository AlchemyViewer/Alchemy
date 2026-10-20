/**
 * @file alskydome_gl_test.cpp
 * @brief WindLight's dome two ways on the hidden window: the sky program over the dome's mesh (deferred/skyF.glsl on
 *        ALSkyDome's strips, placed as LLDrawPoolWLSky::renderDome places them), and windlightDome along each pixel's
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

#include "alfarplane.h"
#include "alskydome.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llimage.h"
#include "llimagegl.h"
#include "llmatrix4a.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llstrider.h"
#include "llvertexbuffer.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    constexpr S32 W = 192;
    constexpr S32 H = 128;
    constexpr F32 TAN_HALF_FOV = 0.6f;
    constexpr F32 NEAR_CLIP    = 0.5f;
    constexpr F32 FAR_CLIP     = 1024.f;

    // LLSettingsSky's DOME_RADIUS, and its DOME_OFFSET of it: where LLEnvironment::getCamHeight puts the camera.
    constexpr F32 DOME_RADIUS   = 15000.f;
    constexpr F32 CAMERA_HEIGHT = 0.96f * 15000.f;

    // Where the camera is in the agent's frame. renderDome takes the dome to it, so nothing should depend on it.
    const LLVector3 CAMERA(118.f, -43.f, 27.f);

    // A sky's settings, as the Environment block and the sky's loose uniforms take them.
    struct Sky
    {
        const char* mName;
        LLVector3   mBlueDensity;
        LLVector3   mBlueHorizon;
        F32         mHazeDensity;
        F32         mHazeHorizon;
        F32         mDensityMultiplier;
        F32         mMaxY;
        LLVector3   mGlow;
        F32         mCloudShadow;
        LLVector3   mLight;      ///< sky_light: the sun's colour, or the moon's at 0.7
        LLVector3   mAmbient;    ///< sky_ambient
        F32         mElevation;  ///< the light's, in degrees
        F32         mAzimuth;    ///< the light's, in degrees clockwise from north
        F32         mGlowFactor; ///< sun_moon_glow_factor: 1 with the sun up, the moon's brightness / 4 with it down
        F32         mMoisture;
        F32         mDroplet;
        F32         mIce;
    };

    // The default sky at midday, the same with a rainbow's moisture and a halo's ice, a sunset under thicker haze, and
    // a night lit by the moon, whose glow factor below 1 takes the glow away.
    const Sky SKIES[] = {
        { "midday", { 0.2447f, 0.4487f, 0.7599f }, { 0.4954f, 0.4954f, 0.6399f }, 0.7f, 0.19f, 0.0001f, 1605.f,
          { 5.f, 0.001f, -0.4799f }, 0.27f, { 0.7342f, 0.7815f, 0.8999f }, { 0.35f, 0.35f, 0.45f }, 60.f, 80.f, 1.f,
          0.f, 800.f, 0.f },
        { "midday, rainbow and halo", { 0.2447f, 0.4487f, 0.7599f }, { 0.4954f, 0.4954f, 0.6399f }, 0.7f, 0.19f, 0.0001f,
          1605.f, { 5.f, 0.001f, -0.4799f }, 0.27f, { 0.7342f, 0.7815f, 0.8999f }, { 0.35f, 0.35f, 0.45f }, 35.f, 200.f,
          1.f, 0.6f, 800.f, 0.5f },
        { "sunset", { 0.18f, 0.3f, 0.52f }, { 0.62f, 0.42f, 0.38f }, 1.4f, 0.32f, 0.00025f, 906.f,
          { 2.2f, 0.001f, -0.62f }, 0.32f, { 1.25f, 0.62f, 0.31f }, { 0.16f, 0.12f, 0.14f }, 3.f, 260.f, 1.f, 0.f, 800.f,
          0.f },
        { "night, moonlit", { 0.12f, 0.18f, 0.38f }, { 0.18f, 0.22f, 0.36f }, 0.5f, 0.12f, 0.00018f, 1200.f,
          { 5.f, 0.001f, -0.4799f }, 0.2f, { 0.21f, 0.245f, 0.35f }, { 0.04f, 0.045f, 0.08f }, 35.f, 140.f, 0.125f, 0.f,
          800.f, 0.f },
    };

    // A view: the camera's bearing clockwise from north, and its pitch above the horizon, in degrees.
    struct View
    {
        F32 mBearing;
        F32 mPitch;
    };

    // Toward the light and away from it at the horizon, up into the cap, and down onto water.
    std::array<View, 4> views(const Sky& sky)
    {
        return { View{ sky.mAzimuth, 0.f }, View{ sky.mAzimuth + 180.f, -4.f }, View{ sky.mAzimuth + 60.f, 35.f },
                 View{ sky.mAzimuth - 30.f, -22.f } };
    }

    // The light in the dome's frame, (north, up, east).
    LLVector3 lightNorm(const Sky& sky)
    {
        const F32 el = sky.mElevation * DEG_TO_RAD;
        const F32 az = sky.mAzimuth * DEG_TO_RAD;
        return LLVector3(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el));
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

    struct alskydome_data
    {
        alskydome_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
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

        // A smooth texture, so a lookup's place shows in what it reads.
        static LLPointer<LLImageGL> gradient(U32 seed)
        {
            constexpr S32 SIZE = 64;
            LLPointer<LLImageRaw> raw = new LLImageRaw(SIZE, SIZE, 4);
            U8* data = raw->getData();
            for (S32 y = 0; y < SIZE; ++y)
            {
                for (S32 x = 0; x < SIZE; ++x)
                {
                    const F32 u = (F32)x / (SIZE - 1);
                    const F32 v = (F32)y / (SIZE - 1);
                    U8* px = data + (y * SIZE + x) * 4;
                    px[0] = (U8)std::lround(255.f * (0.5f + 0.5f * sinf(6.f * v + (F32)seed)));
                    px[1] = (U8)std::lround(255.f * (0.5f + 0.5f * cosf(5.f * v + 2.f * u)));
                    px[2] = (U8)std::lround(255.f * v);
                    px[3] = 255;
                }
            }
            return new LLImageGL(raw, false);
        }

        // The sky program as LLViewerShaderMgr builds it, without the emissive buffer, so it writes the dome's colour
        // to the albedo attachment as it is; the analytic pass; the dome's mesh as LLVOWLSky fills it; and a float
        // target the size of the view.
        void setUp()
        {
            std::map<std::string, std::string> defines;
            for (const char* object : { "deferred/textureUtilV.glsl", "objects/nonindexedTextureV.glsl" })
            {
                CAPTURE(object);
                S32 level = 1;
                REQUIRE(mShaders.loadShaderFile(object, level, GL_VERTEX_SHADER, &defines) != 0);
            }
            for (const char* object : { "deferred/globalF.glsl", "environment/srgbF.glsl" })
            {
                CAPTURE(object);
                S32 level = 1;
                REQUIRE(mShaders.loadShaderFile(object, level, GL_FRAGMENT_SHADER, &defines) != 0);
            }

            mSky.mName = "Deferred Windlight Sky Shader";
            mSky.mFeatures.hasSrgb = true;
            mSky.mShaderFiles.clear();
            mSky.mShaderFiles.push_back(std::make_pair("deferred/skyV.glsl", GL_VERTEX_SHADER));
            mSky.mShaderFiles.push_back(std::make_pair("deferred/skyF.glsl", GL_FRAGMENT_SHADER));
            mSky.mShaderFiles.push_back(std::make_pair("windlight/windlightDomeF.glsl", GL_FRAGMENT_SHADER));
            mSky.mShaderLevel = 1;
            UNSCOPED_INFO("the sky program builds");
            REQUIRE(mSky.createShader());

            {
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

            const U32 verts   = ALSkyDome::vertexCount(ALSkyDome::STACKS);
            const U32 indices = ALSkyDome::indexCount(ALSkyDome::STACKS);
            mDome = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0);
            REQUIRE(mDome->allocateBuffer(verts, indices));
            LLStrider<LLVector3> vertices;
            LLStrider<LLVector2> tex_coords;
            LLStrider<U16>       index;
            REQUIRE(mDome->getVertexStrider(vertices));
            REQUIRE(mDome->getTexCoord0Strider(tex_coords));
            REQUIRE(mDome->getIndexStrider(index));
            ALSkyDome::buildStrips(0, ALSkyDome::STACKS, DOME_RADIUS, vertices, tex_coords, index);
            mDome->unmapBuffer();

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();

            mRainbow = gradient(1);
            mHalo    = gradient(4);

            REQUIRE(mFull.allocate(W, H, GL_RGBA32F));

            glGenBuffers(1, &mEnvironment);
        }

        // The Environment block as LLEnvironment packs it from @a sky (LLSettingsVOSky::applySpecial's terms), at the
        // offsets the linked program gives each member, and bound where every program reads it.
        void bindEnvironment(const Sky& sky)
        {
            const GLuint program = mSky.mProgramObject;
            const GLuint block = glGetUniformBlockIndex(program, "Environment");
            REQUIRE(block != GL_INVALID_INDEX);
            GLint size = 0;
            glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
            REQUIRE(size > 0);
            std::vector<U8> data((size_t)size, 0);
            const auto put = [&](const char* name, const F32* value, S32 count)
            {
                GLuint index = GL_INVALID_INDEX;
                glGetUniformIndices(program, 1, &name, &index);
                if (index == GL_INVALID_INDEX)
                {
                    return;
                }
                GLint offset = -1;
                glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_OFFSET, &offset);
                REQUIRE(offset >= 0);
                REQUIRE((size_t)offset + count * sizeof(F32) <= data.size());
                std::memcpy(data.data() + offset, value, count * sizeof(F32));
            };
            const auto put3 = [&](const char* name, const LLVector3& v) { put(name, v.mV, 3); };
            const auto put1 = [&](const char* name, F32 v) { put(name, &v, 1); };

            LLVector3 extinction;
            LLVector3 blue_weight;
            LLVector3 haze_weight;
            LLVector3 light_atten;
            LLVector3 ambient_below;
            for (S32 c = 0; c < 3; ++c)
            {
                extinction.mV[c]    = llmax(fabsf(sky.mBlueDensity.mV[c]) + fabsf(sky.mHazeDensity), 1e-6f);
                blue_weight.mV[c]   = sky.mBlueDensity.mV[c] / extinction.mV[c];
                haze_weight.mV[c]   = sky.mHazeDensity / extinction.mV[c];
                light_atten.mV[c]   = (sky.mBlueDensity.mV[c] + sky.mHazeDensity * 0.25f) * (sky.mDensityMultiplier * sky.mMaxY);
                ambient_below.mV[c] = sky.mAmbient.mV[c] + llmax(0.f, 1.f - sky.mAmbient.mV[c]) * sky.mCloudShadow * 0.5f;
            }
            put3("blue_density", sky.mBlueDensity);
            put1("haze_density", sky.mHazeDensity);
            put3("blue_horizon", sky.mBlueHorizon);
            put1("haze_horizon", sky.mHazeHorizon);
            put3("glow", sky.mGlow);
            put1("max_y", sky.mMaxY);
            put1("cloud_shadow", sky.mCloudShadow);
            put3("haze_extinction", extinction);
            put3("blue_weight", blue_weight);
            put3("haze_weight", haze_weight);
            put3("sky_light", sky.mLight);
            put3("sky_ambient", sky.mAmbient);
            put3("sky_light_atten", light_atten);
            put3("sky_ambient_below", ambient_below);

            glBindBuffer(GL_UNIFORM_BUFFER, mEnvironment);
            glBufferData(GL_UNIFORM_BUFFER, size, data.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
            glBindBufferBase(GL_UNIFORM_BUFFER, LLGLSLShader::UB_ENVIRONMENT, mEnvironment);
        }

        // The dome's loose uniforms and maps, as LLDrawPoolWLSky::bindDome binds them, on the bound @a program.
        void bindDome(LLGLSLShader& program, const Sky& sky)
        {
            const LLVector3 light = lightNorm(sky);
            program.uniform3fv(LLShaderMgr::LIGHTNORM, 1, light.mV);
            program.uniform1f(LLShaderMgr::DENSITY_MULTIPLIER, sky.mDensityMultiplier);
            program.uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, sky.mGlowFactor);
            program.uniform1f(LLShaderMgr::MOISTURE_LEVEL, sky.mMoisture);
            program.uniform1f(LLShaderMgr::DROPLET_RADIUS, sky.mDroplet);
            program.uniform1f(LLShaderMgr::ICE_LEVEL, sky.mIce);
            program.uniform3f(LLShaderMgr::WL_CAMPOSLOCAL, 0.f, CAMERA_HEIGHT, 0.f);
            program.uniform1f(LLShaderMgr::SKY_DOME_RADIUS, DOME_RADIUS);
            for (const auto& [uniform, map] : { std::make_pair(LLShaderMgr::RAINBOW_MAP, mRainbow.get()),
                                                std::make_pair(LLShaderMgr::HALO_MAP, mHalo.get()) })
            {
                const S32 channel = program.enableTexture(uniform);
                if (channel > -1)
                {
                    gGL.getTextureSlot(channel)->bindSampled(map, ALSamplers::AnisoWrap);
                }
            }
        }

        // The camera looking along @a view from CAMERA: the agent's frame (east, north, up) turned to the eye's.
        static void loadView(const View& view)
        {
            gGL.matrixMode(LLRender::MM_PROJECTION);
            LLMatrix4a proj;
            proj.clear();
            F32* p = proj.getF32ptr();
            p[0]  = 1.f / (TAN_HALF_FOV * (F32)W / (F32)H);
            p[5]  = 1.f / TAN_HALF_FOV;
            p[10] = (FAR_CLIP + NEAR_CLIP) / (NEAR_CLIP - FAR_CLIP);
            p[11] = -1.f;
            p[14] = 2.f * FAR_CLIP * NEAR_CLIP / (NEAR_CLIP - FAR_CLIP);
            gGL.loadMatrix(proj);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
            gGL.rotatef(-view.mPitch, 1.f, 0.f, 0.f);
            gGL.rotatef(-90.f, 1.f, 0.f, 0.f);
            gGL.rotatef(view.mBearing, 0.f, 0.f, 1.f);
            gGL.translatef(-CAMERA.mV[VX], -CAMERA.mV[VY], -CAMERA.mV[VZ]);
        }

        // The sky program over the mesh into @a target, as LLDrawPoolWLSky::renderDome draws it: the dome taken to the
        // camera, turned from its frame to the agent's and lowered by the camera's height, every band drawn on the far
        // plane in order, so the later covers the earlier where they overlap. Read back.
        std::vector<F32> raster(LLRenderTarget& target, const Sky& sky, const View& view)
        {
            target.bindTarget();
            gGL.setClearColor(LLColor4(-1.f, -1.f, -1.f, 0.f));
            target.clear(GL_COLOR_BUFFER_BIT);
            gGL.setClearColor(LLColor4::transparent);

            loadView(view);
            mSky.bind();
            bindDome(mSky, sky);
            gGL.pushMatrix();
            gGL.translatef(CAMERA.mV[VX], CAMERA.mV[VY], CAMERA.mV[VZ]);
            gGL.rotatef(120.f, 1.f / F_SQRT3, 1.f / F_SQRT3, 1.f / F_SQRT3);
            gGL.scalef(0.333f, 0.333f, 0.333f);
            gGL.translatef(0.f, -CAMERA_HEIGHT, 0.f);
            {
                LLGLDisable          blend(GL_BLEND);
                LLGLDisable          cull(GL_CULL_FACE);
                LLGLDepthTest        no_depth(GL_FALSE, GL_FALSE);
                mDome->setBuffer();
                mDome->drawRange(LLRender::TRIANGLE_STRIP, 0, mDome->getNumVerts() - 1, mDome->getNumIndices(), 0);
            }
            gGL.popMatrix();
            mSky.unbind();

            std::vector<F32> out(size_t(target.getWidth()) * target.getHeight() * 4);
            glReadPixels(0, 0, target.getWidth(), target.getHeight(), GL_RGBA, GL_FLOAT, out.data());
            target.flush();
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

            loadView(view);
            mEyeToSky = ALFarPlane::eyeToSkyFrame(gGL.getModelviewMatrix());
            mRay.bind();
            bindDome(mRay, sky);
            glUniform2f(glGetUniformLocation(mRay.mProgramObject, "test_tan_half"), TAN_HALF_FOV * (F32)W / (F32)H,
                        TAN_HALF_FOV);
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

        // Pixel (x, y)'s view ray in the dome's frame, as the ray pass works it.
        LLVector3 domeRay(S32 x, S32 y) const
        {
            const F32 nx = ((F32)x + 0.5f) / (F32)W * 2.f - 1.f;
            const F32 ny = ((F32)y + 0.5f) / (F32)H * 2.f - 1.f;
            LLVector3 eye(nx * TAN_HALF_FOV * (F32)W / (F32)H, ny * TAN_HALF_FOV, -1.f);
            eye.normVec();
            LLVector3 dir;
            for (S32 r = 0; r < 3; ++r)
            {
                dir.mV[r] = mEyeToSky[0 * 3 + r] * eye.mV[0] + mEyeToSky[1 * 3 + r] * eye.mV[1] + mEyeToSky[2 * 3 + r] * eye.mV[2];
            }
            return dir;
        }

        static LLVector3 colour(const std::vector<F32>& image, S32 x, S32 y)
        {
            const F32* p = &image[(size_t(y) * W + x) * 4];
            return LLVector3(p[0], p[1], p[2]);
        }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader           mSky;
        LLGLSLShader           mRay;
        LLRenderTarget         mFull;
        LLPointer<LLVertexBuffer> mDome;
        LLPointer<LLVertexBuffer> mTriangle;
        LLPointer<LLImageGL>   mRainbow;
        LLPointer<LLImageGL>   mHalo;
        GLuint                 mEnvironment = 0;
        std::array<F32, 9>     mEyeToSky{};
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
    for (const Sky& sky : SKIES)
    {
        bindEnvironment(sky);
        const bool bands = sky.mMoisture > 0.f || sky.mIce > 0.f;
        for (const View& view : views(sky))
        {
            CAPTURE(sky.mName, view.mBearing, view.mPitch);
            const std::vector<F32> mesh = raster(mFull, sky, view);
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
                    const F32 rise = domeRay(x, y).mV[VY];
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
    const Sky& both = SKIES[1];
    for (const auto& [moisture, ice] : { std::make_pair(0.f, 0.f), std::make_pair(both.mMoisture, 0.f),
                                         std::make_pair(0.f, both.mIce), std::make_pair(both.mMoisture, both.mIce) })
    {
        Sky sky = both;
        sky.mMoisture = moisture;
        sky.mIce = ice;
        bindEnvironment(sky);
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
