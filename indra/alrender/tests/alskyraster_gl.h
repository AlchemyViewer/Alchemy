/**
 * @file alskyraster_gl.h
 * @brief What the sky's GL tests share: a shader tree holding the viewer's shaders beside the sky as it was drawn before
 *        its composite (tests/skyraster, the files as they stood at the stars' rework), and the skies, views, maps and
 *        draws that set either picture up on the hidden window.
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

#pragma once

#include "alfarplane.h"
#include "alskylayers.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llimage.h"
#include "llimagegl.h"
#include "llmath.h"
#include "llmatrix4a.h"
#include "llquaternion.h"
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
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace al_sky_test
{
    // LLSettingsSky's DOME_RADIUS, and its DOME_OFFSET of it: where LLEnvironment::getCamHeight puts the camera.
    constexpr F32 DOME_RADIUS   = 15000.f;
    constexpr F32 CAMERA_HEIGHT = 0.96f * 15000.f;

    // ALSky's HEAVENLY_BODY_DIST (HORIZON_DIST less 20 m), and the sun's and moon's disk radii.
    constexpr F32 BODY_DISTANCE    = 1024.f - 20.f;
    constexpr F32 SUN_DISK_RADIUS  = 0.5f;
    constexpr F32 MOON_DISK_RADIUS = SUN_DISK_RADIUS * 0.9f;

    // The opacity the composite lays the sun bloom at, at most (deferred/skyBodiesF.glsl's skyBloom): the halo's,
    // which the old picture draws it at.
    constexpr F32 BLOOM_OPACITY = 0.9f;

    constexpr F32 NEAR_CLIP = 0.5f;
    constexpr F32 FAR_CLIP  = 1024.f;

    // Where the camera is in the agent's frame. The sky is taken to it, so nothing should depend on it.
    const LLVector3 CAMERA(118.f, -43.f, 27.f);

    // A sky's settings, as the Environment block and the sky's loose uniforms take them, and the clouds'.
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
        LLVector3   mCloudColor;
        LLVector3   mCloudPosDensity1;
        LLVector3   mCloudPosDensity2;
        F32         mCloudScale;
        F32         mCloudVariance;
    };

    // A clouded midday, a sunset under thicker haze, and a night lit by the moon, whose glow factor below 1 takes the
    // glow away.
    inline const Sky SKIES[] = {
        { "midday", { 0.2447f, 0.4487f, 0.7599f }, { 0.4954f, 0.4954f, 0.6399f }, 0.7f, 0.19f, 0.0001f, 1605.f,
          { 5.f, 0.001f, -0.4799f }, 0.27f, { 0.7342f, 0.7815f, 0.8999f }, { 0.35f, 0.35f, 0.45f }, 60.f, 80.f, 1.f,
          0.f, 800.f, 0.f, { 0.41f, 0.41f, 0.41f }, { 1.6869f, 0.5269f, 0.88f }, { 0.07f, 0.4f, 0.125f }, 0.4199f,
          0.27f },
        { "sunset", { 0.18f, 0.3f, 0.52f }, { 0.62f, 0.42f, 0.38f }, 1.4f, 0.32f, 0.00025f, 906.f,
          { 2.2f, 0.001f, -0.62f }, 0.32f, { 1.25f, 0.62f, 0.31f }, { 0.16f, 0.12f, 0.14f }, 3.f, 260.f, 1.f, 0.f, 800.f,
          0.f, { 0.62f, 0.48f, 0.44f }, { 0.33f, 0.71f, 0.62f }, { 0.13f, 0.27f, 0.2f }, 0.55f, 0.4f },
        { "night, moonlit", { 0.12f, 0.18f, 0.38f }, { 0.18f, 0.22f, 0.36f }, 0.5f, 0.12f, 0.00018f, 1200.f,
          { 5.f, 0.001f, -0.4799f }, 0.2f, { 0.21f, 0.245f, 0.35f }, { 0.04f, 0.045f, 0.08f }, 35.f, 140.f, 0.125f, 0.f,
          800.f, 0.f, { 0.22f, 0.23f, 0.3f }, { 0.81f, 0.13f, 0.76f }, { 0.09f, 0.55f, 0.11f }, 0.35f, 0.2f },
    };

    // The clouded midday with a rainbow's moisture and a halo's ice, which the dome draws in bands below the horizon
    // too.
    inline const Sky RAINBOW_SKY = { "midday, rainbow and halo", { 0.2447f, 0.4487f, 0.7599f },
                                     { 0.4954f, 0.4954f, 0.6399f }, 0.7f, 0.19f, 0.0001f, 1605.f,
                                     { 5.f, 0.001f, -0.4799f }, 0.27f, { 0.7342f, 0.7815f, 0.8999f },
                                     { 0.35f, 0.35f, 0.45f }, 35.f, 200.f, 1.f, 0.6f, 800.f, 0.5f,
                                     { 0.41f, 0.41f, 0.41f }, { 1.6869f, 0.5269f, 0.88f }, { 0.07f, 0.4f, 0.125f },
                                     0.4199f, 0.27f };

    // A view: the camera's bearing clockwise from north, its pitch above the horizon and its vertical field of view, in
    // degrees.
    struct View
    {
        F32 mBearing;
        F32 mPitch;
        F32 mFov;
    };

    // The light in the dome's frame, (north, up, east).
    inline LLVector3 lightNorm(const Sky& sky)
    {
        const F32 el = sky.mElevation * DEG_TO_RAD;
        const F32 az = sky.mAzimuth * DEG_TO_RAD;
        return LLVector3(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el));
    }

    // A direction in the agent's frame (east, north, up) from an elevation and a bearing clockwise from north.
    inline LLVector3 agentDirection(F32 elevation, F32 bearing)
    {
        const F32 el = elevation * DEG_TO_RAD;
        const F32 az = bearing * DEG_TO_RAD;
        return LLVector3(sinf(az) * cosf(el), cosf(az) * cosf(el), sinf(el));
    }

    // A rotation that turns +x to the direction at @a elevation and @a bearing, as a sky's body rotation does.
    inline LLQuaternion bodyRotation(F32 elevation, F32 bearing)
    {
        LLQuaternion rotation;
        rotation.shortestArc(LLVector3::x_axis, agentDirection(elevation, bearing));
        return rotation;
    }

    // The camera looking along @a view from CAMERA onto a @a width by @a height viewport: the agent's frame turned to
    // the eye's.
    inline void loadView(const View& view, S32 width, S32 height)
    {
        gGL.matrixMode(LLRender::MM_PROJECTION);
        const F32 tan_half = tanf(view.mFov * 0.5f * DEG_TO_RAD);
        LLMatrix4a proj;
        proj.clear();
        F32* p = proj.getF32ptr();
        p[0]  = 1.f / (tan_half * (F32)width / (F32)height);
        p[5]  = 1.f / tan_half;
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

    // Pixel (x, y)'s view ray in the dome's frame, through @a eye_to_sky, for a @a view on a @a width by @a height
    // viewport.
    inline LLVector3 domeRay(const View& view, const std::array<F32, 9>& eye_to_sky, S32 width, S32 height, S32 x,
                             S32 y)
    {
        const F32 tan_half = tanf(view.mFov * 0.5f * DEG_TO_RAD);
        const F32 nx = ((F32)x + 0.5f) / (F32)width * 2.f - 1.f;
        const F32 ny = ((F32)y + 0.5f) / (F32)height * 2.f - 1.f;
        LLVector3 eye(nx * tan_half * (F32)width / (F32)height, ny * tan_half, -1.f);
        eye.normVec();
        LLVector3 dir;
        for (S32 r = 0; r < 3; ++r)
        {
            dir.mV[r] = eye_to_sky[0 * 3 + r] * eye.mV[0] + eye_to_sky[1 * 3 + r] * eye.mV[1] +
                        eye_to_sky[2 * 3 + r] * eye.mV[2];
        }
        return dir;
    }

    // A step of an attachment's storage at @a value: a step of 8 bits, or of R11G11B10F's mantissa, six bits for red
    // and green and five for blue, at value's exponent (its least, 2^-14, below it).
    inline F32 storageStep(F32 value, bool emissive, S32 channel)
    {
        if (!emissive)
        {
            return 1.f / 255.f;
        }
        const S32 mantissa = channel == 2 ? 5 : 6;
        S32 exponent = 0;
        std::frexp(llmax(fabsf(value), 0x1p-14f), &exponent);
        return std::ldexp(1.f, exponent - 1 - mantissa);
    }

    // The raster sky's ditherEmissive (skyraster/deferred/ditherEmissiveF.glsl): how far it moves a value written to
    // the emissive buffer, relative to the value.
    inline F32 ditherReach(S32 channel)
    {
        return channel == 2 ? 0.5f / 32.f : 0.5f / 64.f;
    }

    // The shader tree the tests build from: every file of the viewer's class1 tree, and the sky as it was drawn before
    // its composite under skyraster/, in one place a TestShaderMgr reads once populate() has filled it.
    class SkyShaderTree
    {
    public:
        void populate()
        {
            copyTree(std::string(AL_TEST_SHADER_DIR) + "/class1", "");
            copyTree(AL_TEST_SKYRASTER_DIR, "skyraster/");
        }

        std::string prefix() const { return mDir.prefix(); }

        // Write a test's own file into the tree.
        bool write(const std::string& path, const std::string& source) const { return mDir.write(path, source); }

    private:
        void copyTree(const std::string& from, const std::string& into)
        {
            const std::filesystem::path root = fsyspath(from);
            std::error_code ec;
            for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
            {
                if (!it->is_regular_file())
                {
                    continue;
                }
                std::string relative = fsyspath(std::filesystem::relative(it->path(), root)).string();
                std::replace(relative.begin(), relative.end(), '\\', '/');
                std::error_code read_ec;
                const std::string source = LLFile::getContents(fsyspath(it->path()).string(), read_ec);
                REQUIRE(!read_ec);
                REQUIRE(mDir.write(into + relative, source));
            }
            REQUIRE(!ec);
        }

        ll_test::TestShaderDir mDir;
    };

    // The shared objects every program of the sky attaches, loaded into @a shaders ahead of the programs.
    inline void loadSharedObjects(ll_test::TestShaderMgr& shaders)
    {
        std::map<std::string, std::string> defines;
        for (const auto& [object, stage] : { std::make_pair("deferred/textureUtilV.glsl", GL_VERTEX_SHADER),
                                             std::make_pair("objects/nonindexedTextureV.glsl", GL_VERTEX_SHADER),
                                             std::make_pair("deferred/globalF.glsl", GL_FRAGMENT_SHADER),
                                             std::make_pair("environment/srgbF.glsl", GL_FRAGMENT_SHADER) })
        {
            CAPTURE(object);
            S32 level = 1;
            REQUIRE(shaders.loadShaderFile(object, level, stage, &defines) != 0);
        }
    }

    // The shared objects a deferred program attaches besides (LLShaderMgr::attachShaderFeatures' isDeferred), as the
    // sky's pass is built, loaded into @a shaders after loadSharedObjects.
    inline void loadDeferredObjects(ll_test::TestShaderMgr& shaders)
    {
        std::map<std::string, std::string> defines;
        for (const auto& [object, stage] : { std::make_pair("windlight/atmosphericsVarsV.glsl", GL_VERTEX_SHADER),
                                             std::make_pair("windlight/atmosphericsVarsF.glsl", GL_FRAGMENT_SHADER),
                                             std::make_pair("deferred/deferredUtil.glsl", GL_FRAGMENT_SHADER),
                                             std::make_pair("windlight/atmosphericsFuncs.glsl", GL_FRAGMENT_SHADER),
                                             std::make_pair("windlight/atmosphericsF.glsl", GL_FRAGMENT_SHADER) })
        {
            CAPTURE(object);
            S32 level = 1;
            REQUIRE(shaders.loadShaderFile(object, level, stage, &defines) != 0);
        }
    }

    // Build @a program from @a files, with @a permutations, the emissive buffer's HAS_EMISSIVE where @a emissive.
    inline void buildProgram(LLGLSLShader& program, const char* name,
                             const std::vector<std::pair<std::string, GLenum>>& files, bool emissive,
                             const std::vector<std::pair<std::string, std::string>>& permutations = {})
    {
        program.mName = name;
        program.mFeatures.hasSrgb = true;
        program.mShaderFiles.clear();
        for (const auto& file : files)
        {
            program.mShaderFiles.push_back(file);
        }
        program.mShaderLevel = 1;
        program.clearPermutations();
        if (emissive)
        {
            program.addPermutation("HAS_EMISSIVE", "1");
        }
        for (const auto& [define, value] : permutations)
        {
            program.addPermutation(define, value);
        }
        CAPTURE(name, emissive);
        UNSCOPED_INFO("the program builds");
        REQUIRE(program.createShader());
    }

    // The Environment block as LLEnvironment packs it from @a sky (LLSettingsVOSky::applySpecial's terms) and the
    // view's @a eye_to_sky, at the offsets @a program gives each member, into @a buffer, bound where every program reads
    // it.
    inline void bindEnvironment(const LLGLSLShader& program, GLuint buffer, const Sky& sky,
                                const std::array<F32, 9>& eye_to_sky)
    {
        const GLuint object = program.mProgramObject;
        const GLuint block = glGetUniformBlockIndex(object, "Environment");
        REQUIRE(block != GL_INVALID_INDEX);
        GLint size = 0;
        glGetActiveUniformBlockiv(object, block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
        REQUIRE(size > 0);
        std::vector<U8> data((size_t)size, 0);
        const auto offset_of = [&](const char* name) -> GLint
        {
            GLuint index = GL_INVALID_INDEX;
            glGetUniformIndices(object, 1, &name, &index);
            if (index == GL_INVALID_INDEX)
            {
                return -1;
            }
            GLint offset = -1;
            glGetActiveUniformsiv(object, 1, &index, GL_UNIFORM_OFFSET, &offset);
            return offset;
        };
        const auto put = [&](const char* name, const F32* value, S32 count)
        {
            const GLint offset = offset_of(name);
            if (offset < 0)
            {
                return;
            }
            REQUIRE((size_t)offset + count * sizeof(F32) <= data.size());
            std::memcpy(data.data() + offset, value, count * sizeof(F32));
        };
        const auto put3 = [&](const char* name, const LLVector3& v) { put(name, v.mV, 3); };
        const auto put1 = [&](const char* name, F32 v) { put(name, &v, 1); };

        const F32 cloud_shadow = sky.mCloudShadow;
        LLVector3 extinction;
        LLVector3 blue_weight;
        LLVector3 haze_weight;
        LLVector3 light_atten;
        LLVector3 ambient_below;
        LLVector3 atmos_ambient;
        LLVector3 cloud_sunlight;
        const LLVector3 light = lightNorm(sky);
        for (S32 c = 0; c < 3; ++c)
        {
            extinction.mV[c]    = llmax(fabsf(sky.mBlueDensity.mV[c]) + fabsf(sky.mHazeDensity), 1e-6f);
            blue_weight.mV[c]   = sky.mBlueDensity.mV[c] / extinction.mV[c];
            haze_weight.mV[c]   = sky.mHazeDensity / extinction.mV[c];
            light_atten.mV[c]   = (sky.mBlueDensity.mV[c] + sky.mHazeDensity * 0.25f) * (sky.mDensityMultiplier * sky.mMaxY);
            ambient_below.mV[c] = sky.mAmbient.mV[c] + llmax(0.f, 1.f - sky.mAmbient.mV[c]) * cloud_shadow * 0.5f;
            atmos_ambient.mV[c] = sky.mAmbient.mV[c] + (1.f - sky.mAmbient.mV[c]) * cloud_shadow * 0.5f;
            cloud_sunlight.mV[c] = sky.mLight.mV[c] * expf(-light_atten.mV[c] / llmax(1e-6f, 2.f * light.mV[VY]));
        }
        put3("blue_density", sky.mBlueDensity);
        put1("haze_density", sky.mHazeDensity);
        put3("blue_horizon", sky.mBlueHorizon);
        put1("haze_horizon", sky.mHazeHorizon);
        put3("cloud_pos_density1", sky.mCloudPosDensity1);
        put3("cloud_pos_density2", sky.mCloudPosDensity2);
        put1("cloud_scale", sky.mCloudScale);
        put3("cloud_color", sky.mCloudColor);
        put1("cloud_shadow", cloud_shadow);
        put3("glow", sky.mGlow);
        put1("max_y", sky.mMaxY);
        put3("haze_extinction", extinction);
        put3("blue_weight", blue_weight);
        put3("haze_weight", haze_weight);
        put3("atmos_ambient", atmos_ambient);
        put3("sky_sunlight", sky.mLight);
        put3("sky_light", sky.mLight);
        put3("sky_ambient", sky.mAmbient);
        put3("sky_light_atten", light_atten);
        put3("cloud_sunlight", cloud_sunlight);
        put3("sky_ambient_below", ambient_below);

        // A std140 mat3 is three columns, each a vec4's stride apart.
        const GLint eye = offset_of("eyeToSky");
        if (eye >= 0)
        {
            for (S32 column = 0; column < 3; ++column)
            {
                REQUIRE((size_t)eye + column * 16 + 3 * sizeof(F32) <= data.size());
                std::memcpy(data.data() + eye + column * 16, &eye_to_sky[column * 3], 3 * sizeof(F32));
            }
        }

        glBindBuffer(GL_UNIFORM_BUFFER, buffer);
        glBufferData(GL_UNIFORM_BUFFER, size, data.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, LLGLSLShader::UB_ENVIRONMENT, buffer);
    }

    // Bind @a texture to @a program's sampler @a uniform through @a sampler, where the program reads it and there is
    // one.
    inline void bindMap(LLGLSLShader& program, U32 uniform, LLImageGL* texture, ALSampler sampler)
    {
        if (!texture)
        {
            return;
        }
        const S32 channel = program.enableTexture(uniform);
        if (channel > -1)
        {
            gGL.getTextureSlot(channel)->bindSampled(texture, sampler);
        }
    }

    // The dome's loose uniforms and maps, as ALSky::bindDome binds them, on the bound @a program.
    inline void bindDome(LLGLSLShader& program, const Sky& sky, LLImageGL* rainbow, LLImageGL* halo)
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
        program.uniform1f(LLShaderMgr::CLOUD_VARIANCE, sky.mCloudVariance);
        bindMap(program, LLShaderMgr::RAINBOW_MAP, rainbow, ALSamplers::AnisoWrap);
        bindMap(program, LLShaderMgr::HALO_MAP, halo, ALSamplers::AnisoWrap);
    }

    // A smooth texture, so a lookup's place shows in what it reads: opaque, @a mips or not.
    inline LLPointer<LLImageGL> gradient(U32 seed, bool mips = false)
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
        return new LLImageGL(raw, mips);
    }

    // A disc that fades out at its rim through every alpha, its colour turning across it so a lookup's place shows:
    // a sun's, moon's or bloom's texture, mipmapped as theirs are. @a rim is the disc's radius, a fraction of the half
    // width.
    inline LLPointer<LLImageGL> disc(U32 seed, F32 rim)
    {
        constexpr S32 SIZE = 128;
        LLPointer<LLImageRaw> raw = new LLImageRaw(SIZE, SIZE, 4);
        U8* data = raw->getData();
        for (S32 y = 0; y < SIZE; ++y)
        {
            for (S32 x = 0; x < SIZE; ++x)
            {
                const F32 u = ((F32)x + 0.5f) / SIZE * 2.f - 1.f;
                const F32 v = ((F32)y + 0.5f) / SIZE * 2.f - 1.f;
                const F32 r = sqrtf(u * u + v * v);
                U8* px = data + (y * SIZE + x) * 4;
                px[0] = (U8)std::lround(255.f * (0.75f + 0.25f * sinf(4.f * u + (F32)seed)));
                px[1] = (U8)std::lround(255.f * (0.6f + 0.3f * cosf(3.f * v + (F32)seed)));
                px[2] = (U8)std::lround(255.f * (0.5f + 0.4f * u * v));
                px[3] = (U8)llclamp(std::lround(255.f * (rim - r) / (0.25f * rim)), 0L, 255L);
            }
        }
        return new LLImageGL(raw, true);
    }

    // Tiling value noise in red, as the clouds read their noise: mipmapped and wrapped.
    inline LLPointer<LLImageGL> cloudNoise(U32 seed)
    {
        constexpr S32 SIZE = 128;
        constexpr S32 CELLS = 8;
        std::vector<F32> lattice(CELLS * CELLS);
        U32 state = seed * 2654435761u + 1u;
        for (F32& value : lattice)
        {
            state = state * 1664525u + 1013904223u;
            value = (F32)(state >> 8) / (F32)(1u << 24);
        }
        LLPointer<LLImageRaw> raw = new LLImageRaw(SIZE, SIZE, 4);
        U8* data = raw->getData();
        for (S32 y = 0; y < SIZE; ++y)
        {
            for (S32 x = 0; x < SIZE; ++x)
            {
                const F32 fx = (F32)x / SIZE * CELLS;
                const F32 fy = (F32)y / SIZE * CELLS;
                const S32 x0 = (S32)fx;
                const S32 y0 = (S32)fy;
                F32 tx = fx - x0;
                F32 ty = fy - y0;
                tx = tx * tx * (3.f - 2.f * tx);
                ty = ty * ty * (3.f - 2.f * ty);
                const auto at = [&](S32 i, S32 j) { return lattice[(j % CELLS) * CELLS + (i % CELLS)]; };
                const auto mix = [](F32 a, F32 b, F32 t) { return a + (b - a) * t; };
                const F32 n = mix(mix(at(x0, y0), at(x0 + 1, y0), tx), mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), tx), ty);
                U8* px = data + (y * SIZE + x) * 4;
                px[0] = px[1] = px[2] = (U8)std::lround(255.f * n);
                px[3] = 255;
            }
        }
        return new LLImageGL(raw, true);
    }

    // WindLight's sky dome as a mesh, in the dome's frame (north, up, east), which the old sky program shaded from the
    // camera's point inside the sphere (windlight/windlightDomeF.glsl's windlightDomeAt): rings of a sphere about the
    // origin, from the zenith down past the camera's level. The last rings are a skirt: the third from last lowered
    // 2048 m, the next collapsed onto the axis below it, and the last two back out on the sphere, so the strips overlap
    // below the camera's level and the later band drawn covers the earlier. Triangle strips of SLICES vertices a ring.
    namespace ALSkyDome
    {
        // The stacks the dome has, and the vertices a ring has. Any fewer and the facets show.
        constexpr U32 STACKS = 18;
        constexpr U32 SLICES = 2 * STACKS;

        // The vertices and indices buildStrips writes for @a stacks stacks.
        constexpr U32 vertexCount(U32 stacks) { return SLICES * (stacks + 1); }
        constexpr U32 indexCount(U32 stacks) { return 1 + stacks * (2 + 2 * SLICES); }

        // The polar angle of ring @a ring, from the zenith: PI/8 (1 - (1 - t^4)^2) at t = ring / STACKS, which gathers
        // the rings toward the horizon.
        inline F32 ringAngle(U32 ring)
        {
            F32 t = float(ring) * (1.f / STACKS);

            // ^4 the parameter of the tesselation to bias things toward 0 (the dome's apex)
            t *= t;
            t *= t;

            // invert and square the parameter of the tesselation to bias things toward 1 (the horizon)
            t = 1.f - t;
            t = t * t;
            t = 1.f - t;

            return (F_PI / 8.f) * t;
        }

        // Stacks [@a begin_stack, @a end_stack) of a dome of @a radius: rings begin_stack + 1 to end_stack + 1, their
        // positions, their planar texture coordinates ((1 - east) / 2, (1 - north) / 2) and one triangle strip over
        // them.
        inline void buildStrips(U32 begin_stack, U32 end_stack, F32 radius, LLStrider<LLVector3>& vertices,
                                LLStrider<LLVector2>& tex_coords, LLStrider<U16>& indices)
        {
            llassert(end_stack <= STACKS);

            for (U32 i = begin_stack + 1; i <= end_stack + 1; ++i)
            {
                const F32 phi0 = ringAngle(i);

                for (U32 j = 0; j < SLICES; ++j)
                {
                    const F32 theta = F_TWO_PI * (float(j) / float(SLICES));

                    // standard transformation from spherical to rectangular coordinates
                    const F32 x0 = sin(phi0) * cos(theta);
                    const F32 y0 = cos(phi0);
                    const F32 z0 = sin(phi0) * sin(theta);

                    if (i == STACKS - 2)
                    {
                        *vertices++ = LLVector3(x0 * radius, y0 * radius - 1024.f * 2.f, z0 * radius);
                    }
                    else if (i == STACKS - 1)
                    {
                        *vertices++ = LLVector3(0, y0 * radius - 1024.f * 2.f, 0);
                    }
                    else
                    {
                        *vertices++ = LLVector3(x0 * radius, y0 * radius, z0 * radius);
                    }

                    // planar uv coordinates, x and z transposed so that they animate in the global frame, where +x is
                    // east and +y north
                    *tex_coords++ = LLVector2((-z0 + 1.f) / 2.f, (-x0 + 1.f) / 2.f);
                }
            }

            // the triangle strip
            *indices++ = 0;

            S32 k = 0;
            for (U32 i = 1; i <= end_stack - begin_stack; ++i)
            {
                *indices++ = (U16)(i * SLICES + (U32)k);

                k = (k + 1) % (S32)SLICES;
                for (U32 j = 0; j < SLICES; ++j)
                {
                    *indices++ = (U16)((i - 1) * SLICES + (U32)k);
                    *indices++ = (U16)(i * SLICES + (U32)k);

                    k = (k + 1) % (S32)SLICES;
                }

                if ((--k) < 0)
                {
                    k = (S32)SLICES - 1;
                }

                *indices++ = (U16)(i * SLICES + (U32)k);
            }
        }
    } // namespace ALSkyDome

    // The dome's mesh, as LLVOWLSky filled it.
    inline LLPointer<LLVertexBuffer> domeMesh()
    {
        const U32 verts   = ALSkyDome::vertexCount(ALSkyDome::STACKS);
        const U32 indices = ALSkyDome::indexCount(ALSkyDome::STACKS);
        LLPointer<LLVertexBuffer> dome = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0);
        REQUIRE(dome->allocateBuffer(verts, indices));
        LLStrider<LLVector3> vertices;
        LLStrider<LLVector2> tex_coords;
        LLStrider<U16>       index;
        REQUIRE(dome->getVertexStrider(vertices));
        REQUIRE(dome->getTexCoord0Strider(tex_coords));
        REQUIRE(dome->getIndexStrider(index));
        ALSkyDome::buildStrips(0, ALSkyDome::STACKS, DOME_RADIUS, vertices, tex_coords, index);
        dome->unmapBuffer();
        return dome;
    }

    // Draw @a dome, a mesh in the dome's frame of @a mode's primitives, with the bound program as
    // LLDrawPoolWLSky::renderDome drew it: taken to the camera, turned from its frame to the agent's and lowered by the
    // camera's height.
    inline void drawDome(LLVertexBuffer* dome, LLRender::eGeomModes mode = LLRender::TRIANGLE_STRIP)
    {
        gGL.pushMatrix();
        gGL.translatef(CAMERA.mV[VX], CAMERA.mV[VY], CAMERA.mV[VZ]);
        gGL.rotatef(120.f, 1.f / F_SQRT3, 1.f / F_SQRT3, 1.f / F_SQRT3);
        gGL.scalef(0.333f, 0.333f, 0.333f);
        gGL.translatef(0.f, -CAMERA_HEIGHT, 0.f);
        dome->setBuffer();
        dome->drawRange(mode, 0, dome->getNumVerts() - 1, dome->getNumIndices(), 0);
        gGL.popMatrix();
    }

    // The dome's whole sphere as a far finer mesh than the dome's, with no skirt, and the dome's planar texture
    // coordinates: rings a ninth of a degree apart from the zenith to 25 degrees, past where the camera's level meets
    // the sphere (16.3 degrees), whose facets lie a hundredth as deep as the dome's, and on to the nadir three degrees
    // apart. A ray meets it where it meets the sphere, so what the old programs draw over it is the sphere's.
    inline LLPointer<LLVertexBuffer> fineSphere()
    {
        constexpr U32 CAP_RINGS  = 225;
        constexpr U32 REST_RINGS = 52;
        constexpr U32 RINGS      = CAP_RINGS + REST_RINGS;
        constexpr U32 SLICES     = 230;
        constexpr F32 CAP        = 25.f * DEG_TO_RAD;
        LLPointer<LLVertexBuffer> mesh = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0);
        REQUIRE(mesh->allocateBuffer((RINGS + 1) * SLICES, RINGS * SLICES * 6));
        LLStrider<LLVector3> vertices;
        LLStrider<LLVector2> tex_coords;
        LLStrider<U16>       index;
        REQUIRE(mesh->getVertexStrider(vertices));
        REQUIRE(mesh->getTexCoord0Strider(tex_coords));
        REQUIRE(mesh->getIndexStrider(index));
        for (U32 ring = 0; ring <= RINGS; ++ring)
        {
            const F32 phi = ring <= CAP_RINGS ? (F32)ring / CAP_RINGS * CAP
                                              : CAP + (F32)(ring - CAP_RINGS) / REST_RINGS * (F_PI - CAP);
            for (U32 slice = 0; slice < SLICES; ++slice)
            {
                const F32 theta = F_TWO_PI * (F32)slice / SLICES;
                const F32 x = sinf(phi) * cosf(theta);
                const F32 y = cosf(phi);
                const F32 z = sinf(phi) * sinf(theta);
                *vertices++   = LLVector3(x, y, z) * DOME_RADIUS;
                *tex_coords++ = LLVector2((1.f - z) / 2.f, (1.f - x) / 2.f);
            }
        }
        for (U32 ring = 0; ring < RINGS; ++ring)
        {
            for (U32 slice = 0; slice < SLICES; ++slice)
            {
                const U16 a = (U16)(ring * SLICES + slice);
                const U16 b = (U16)(ring * SLICES + (slice + 1) % SLICES);
                const U16 c = (U16)(a + SLICES);
                const U16 d = (U16)(b + SLICES);
                for (U16 i : { a, c, b, b, c, d })
                {
                    *index++ = i;
                }
            }
        }
        mesh->unmapBuffer();
        return mesh;
    }

    // A sun's or moon's quad as LLVOSky filled its face: the corners about the camera, their texture coordinates, and
    // two triangles.
    inline LLPointer<LLVertexBuffer> bodyQuad(const ALSkyLayers::Corners& corners)
    {
        LLPointer<LLVertexBuffer> quad = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0);
        REQUIRE(quad->allocateBuffer(4, 6));
        LLStrider<LLVector3> vertices;
        LLStrider<LLVector2> tex_coords;
        LLStrider<U16>       index;
        REQUIRE(quad->getVertexStrider(vertices));
        REQUIRE(quad->getTexCoord0Strider(tex_coords));
        REQUIRE(quad->getIndexStrider(index));
        for (const LLVector3& corner : corners)
        {
            *vertices++ = corner;
        }
        *tex_coords++ = LLVector2(0.f, 1.f);
        *tex_coords++ = LLVector2(0.f, 0.f);
        *tex_coords++ = LLVector2(1.f, 1.f);
        *tex_coords++ = LLVector2(1.f, 0.f);
        for (U16 i : { 0, 2, 1, 1, 2, 3 })
        {
            *index++ = i;
        }
        quad->unmapBuffer();
        return quad;
    }

    // Draw @a quad with the bound program, taken to the camera.
    inline void drawQuad(LLVertexBuffer* quad)
    {
        gGL.pushMatrix();
        gGL.translatef(CAMERA.mV[VX], CAMERA.mV[VY], CAMERA.mV[VZ]);
        quad->setBuffer();
        quad->drawRange(LLRender::TRIANGLES, 0, 3, 6, 0);
        gGL.popMatrix();
    }

    // The meteors' quads as LLVOWLSky::updateMeteorGeometry filled them: six vertices a meteor, each with the streak's
    // midpoint, its half toward the head, its colour and its corner.
    inline LLPointer<LLVertexBuffer> meteorQuads(const std::vector<ALSkyLayers::Meteor>& meteors)
    {
        static const LLVector2 CORNERS[6] = { LLVector2(-1.f, -1.f), LLVector2(1.f, -1.f), LLVector2(1.f, 1.f),
                                              LLVector2(-1.f, -1.f), LLVector2(1.f, 1.f), LLVector2(-1.f, 1.f) };
        LLPointer<LLVertexBuffer> quads = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_NORMAL |
                                                             LLVertexBuffer::MAP_COLOR | LLVertexBuffer::MAP_TEXCOORD0);
        REQUIRE(quads->allocateBuffer((U32)meteors.size() * 6, 0));
        LLStrider<LLVector3> positions;
        LLStrider<LLVector3> normals;
        LLStrider<LLColor4U> colours;
        LLStrider<LLVector2> tex_coords;
        REQUIRE(quads->getVertexStrider(positions));
        REQUIRE(quads->getNormalStrider(normals));
        REQUIRE(quads->getColorStrider(colours));
        REQUIRE(quads->getTexCoord0Strider(tex_coords));
        for (const ALSkyLayers::Meteor& meteor : meteors)
        {
            for (const LLVector2& corner : CORNERS)
            {
                *positions++  = meteor.mMidpoint;
                *normals++    = meteor.mHalf;
                *colours++    = meteor.mColour;
                *tex_coords++ = LLVector2(corner.mV[VX], corner.mV[VY] * meteor.mWidth);
            }
        }
        quads->unmapBuffer();
        return quads;
    }

    // The screen triangle the layers' passes draw.
    inline LLPointer<LLVertexBuffer> screenTriangle()
    {
        LLPointer<LLVertexBuffer> triangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
        REQUIRE(triangle->allocateBuffer(3, 0));
        LLStrider<LLVector3> vert;
        REQUIRE(triangle->getVertexStrider(vert));
        vert[0].set(-1.f, 1.f, 0.f);
        vert[1].set(-1.f, -3.f, 0.f);
        vert[2].set(3.f, 1.f, 0.f);
        triangle->unmapBuffer();
        return triangle;
    }

    // Attachment @a attachment of the bound target, @a width by @a height, read back as floats.
    inline std::vector<F32> readAttachment(S32 attachment, S32 width, S32 height)
    {
        std::vector<F32> out(size_t(width) * height * 4);
        glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, out.data());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        return out;
    }
} // namespace al_sky_test
