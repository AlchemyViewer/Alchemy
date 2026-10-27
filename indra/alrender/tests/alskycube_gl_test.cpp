/**
 * @file alskycube_gl_test.cpp
 * @brief The environment cube probe-less reflections read, on the hidden window: baked by ALSkyCube from the viewer's
 *        own program (deferred/skyCubeF.glsl), against windlightDome along each texel's direction, with every level
 *        made; neutral at every level before its first bake; its storage counted whole while it lives; and against the
 *        cube the CPU built before it (LLVOSky's createSkyTexture through LLAtmospherics, kept here), whose difference
 *        it reports.
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

#include "alskycube.h"

#include "alskyraster_gl.h"

#include "llcubemap.h"
#include "v3color.h"
#include "v3colorutil.h"
#include "v4color.h"
#include "v4coloru.h"

#include <catch2/catch_message.hpp>

#include <sstream>

using namespace al_sky_test;

namespace
{
    // The equirectangular view of every direction the old and new cubes are compared over: bearing across, clockwise
    // from north, and elevation up, from the nadir to the zenith.
    constexpr S32 PANORAMA_W = 256;
    constexpr S32 PANORAMA_H = 128;

    // What eight bits keep of a value: half a step either way.
    constexpr F32 HALF_STEP = 0.5f / 255.f;

    // The cube's first level against what windlightDome gives: the texels are rounded to eight bits, and the bake and
    // the lookup work a texel's direction out each their own way, which near the sun's glow, where the dome is steep,
    // tips a few texels across a rounding edge (measured: 0.56/255 at most, 0.25/255 on average, every sky).
    constexpr F32 BAKED = 1.5f * HALF_STEP;

    // A face's coarsest level against the mean of its first: each level rounded to eight bits as it is made.
    constexpr F32 MIPPED = 4.f / 255.f;

    // The pass that reads a cube along each pixel's direction (test_dirs, in the agent's frame) at test_lod
    // (test_mode 0), or works out what it should hold there (test_mode 1): windlightDome halved, in [0, 1], taking the
    // direction into the dome's frame (north, up, east) through test_to_dome, ALFarPlane::skyFrame as a matrix.
    const char* kLookupVertex = R"(in vec3 position;

void main()
{
    gl_Position = vec4(position.xy, 0.0, 1.0);
}
)";

    const char* kLookupFragment = R"(/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

uniform int         test_mode;
uniform float       test_lod;
uniform samplerCube test_cube;
uniform sampler2D   test_dirs;
uniform mat3        test_to_dome;

vec3 windlightDome(vec3 dir);

void main()
{
    vec3 dir = texelFetch(test_dirs, ivec2(gl_FragCoord.xy), 0).xyz;
    if (test_mode == 0)
    {
        frag_color = vec4(textureLod(test_cube, dir, test_lod).rgb, 1.0);
    }
    else
    {
        frag_color = vec4(clamp(windlightDome(test_to_dome * dir) * 0.5, vec3(0.0), vec3(1.0)), 1.0);
    }
}
)";

    // The cube the CPU built before the bake, a face a frame, as LLVOSky::createSkyTexture made it through
    // LLAtmospherics::calcSkyColorInDir's shiny path, and LLAtmospherics::updateFog's colour, which that path took
    // below the horizon. Kept here, in its arithmetic, so the bake's difference from it can be measured.
    namespace legacy
    {
        // LLFastLn: the table the fog's gamma took its powers from.
        class FastLn
        {
        public:
            FastLn()
            {
                mTable[0] = 0;
                for (S32 i = 1; i < 257; i++)
                {
                    mTable[i] = log((F32)i);
                }
            }

            F32 ln(F32 x) const
            {
                const F32 OO_255 = 0.003921568627450980392156862745098f;
                const F32 LN_255 = 5.5412635451584261462455391880218f;

                if (x < OO_255)
                {
                    return log(x);
                }
                else if (x < 1)
                {
                    x *= 255.f;
                    S32 index = llfloor(x);
                    F32 t = x - index;
                    F32 low = mTable[index];
                    F32 high = mTable[index + 1];
                    return low + t * (high - low) - LN_255;
                }
                else if (x <= 255)
                {
                    S32 index = llfloor(x);
                    F32 t = x - index;
                    F32 low = mTable[index];
                    F32 high = mTable[index + 1];
                    return low + t * (high - low);
                }
                return log(x);
            }

            F32 pow(F32 x, F32 y) const { return (F32)ll_fast_exp(y * ln(x)); }

        private:
            F32 mTable[257];
        };

        // AtmosphericsVars, as LLVOSky::cacheEnvironment took them from the sky.
        struct Vars
        {
            LLColor3  blue_density;
            LLColor3  blue_horizon;
            F32       haze_density = 0.f;
            F32       haze_horizon = 0.f;
            F32       density_multiplier = 0.f;
            F32       max_y = 0.f;
            LLVector4 sun_norm;
            LLColor3  sunlight;
            LLColor3  ambient;
            LLColor3  glow;
            F32       cloud_shadow = 0.f;
            F32       dome_radius = 0.f;
            F32       dome_offset = 0.f;
            LLColor3  light_atten;
            LLColor3  total_density;
        };

        Vars vars(const Sky& sky)
        {
            Vars v;
            v.blue_density       = LLColor3(sky.mBlueDensity.mV);
            v.blue_horizon       = LLColor3(sky.mBlueHorizon.mV);
            v.haze_density       = sky.mHazeDensity;
            v.haze_horizon       = sky.mHazeHorizon;
            v.density_multiplier = sky.mDensityMultiplier;
            v.max_y              = sky.mMaxY;
            const LLVector3 light = lightNorm(sky);
            v.sun_norm           = LLVector4(light.mV[0], light.mV[1], light.mV[2], 0.f);
            v.sunlight           = LLColor3(sky.mLight.mV);
            v.ambient            = LLColor3(sky.mAmbient.mV);
            v.glow               = LLColor3(sky.mGlow.mV);
            v.cloud_shadow       = sky.mCloudShadow;
            v.dome_radius        = DOME_RADIUS;
            v.dome_offset        = CAMERA_HEIGHT / DOME_RADIUS;
            // LLSettingsSky::getLightAttenuation(max_y) and getTotalDensity.
            v.light_atten        = (v.blue_density + smear(v.haze_density * 0.25f)) * v.density_multiplier * v.max_y;
            v.total_density      = v.blue_density + smear(v.haze_density);
            return v;
        }

        // LLAtmospherics::calcSkyColorWLVert's haze colour above the cloud, for Pn in WindLight's frame.
        LLColor3 hazeColor(const Vars& v, LLVector3 Pn)
        {
            // project the direction ray onto the sky dome.
            F32 phi = acos(Pn[1]);
            F32 sinA = sin(F_PI - phi);
            if (fabsf(sinA) < 0.01f)
            { //avoid division by zero
                sinA = 0.01f;
            }

            F32 Plen = v.dome_radius * sin(F_PI + phi + asin(v.dome_offset * sinA)) / sinA;

            Pn *= Plen;

            // Set altitude
            if (Pn[1] > 0.f)
            {
                Pn *= (v.max_y / Pn[1]);
            }
            else
            {
                Pn *= (-32000.f / Pn[1]);
            }

            Plen = Pn.length();
            Pn /= Plen;

            LLColor3 sunlight = v.sunlight;
            LLColor3 ambient = v.ambient;
            LLColor3 glow = v.glow;

            LLColor3 light_atten = v.light_atten;
            // LLSettingsSky::getLightTransmittanceFast.
            LLColor3 light_transmittance = componentExp(v.total_density * -(v.density_multiplier * Plen));

            LLColor3 temp2(0.f, 0.f, 0.f);
            LLColor3 temp1 = v.total_density;

            LLColor3 blue_weight = componentDiv(v.blue_density, temp1);
            LLColor3 blue_factor = v.blue_horizon * blue_weight;
            LLColor3 haze_weight = componentDiv(smear(v.haze_density), temp1);
            LLColor3 haze_factor = v.haze_horizon * haze_weight;

            temp2.mV[1] = llmax(F_APPROXIMATELY_ZERO, llmax(0.f, Pn[1]) * 1.0f + v.sun_norm.mV[1]);
            temp2.mV[1] = 1.f / temp2.mV[1];
            componentMultBy(sunlight, componentExp((light_atten * -1.f) * temp2.mV[1]));
            componentMultBy(sunlight, light_transmittance);

            temp2.mV[2] = Plen * v.density_multiplier;
            temp1 = componentExp((temp1 * -1.f) * temp2.mV[2]);

            temp2.mV[0] = Pn * LLVector3(v.sun_norm);
            temp2.mV[0] = 1.f - temp2.mV[0];
            temp2.mV[0] = llmax(temp2.mV[0], .001f);
            temp2.mV[0] *= glow.mV[0];
            temp2.mV[0] = pow(temp2.mV[0], glow.mV[2]);
            temp2.mV[0] += .25f;

            LLColor3 haze = (blue_factor * (sunlight + ambient) + componentMult(haze_factor, sunlight * temp2.mV[0] + ambient));
            componentMultBy(haze, LLColor3::white - temp1);
            return haze;
        }

        // WindLight's frame from a direction in the agent's, as calcSkyColorInDir took it: every axis negated.
        LLVector3 windlightFrame(const LLVector3& dir, bool mirrored)
        {
            const LLVector3 dome(dir[1], dir[2], dir[0]);
            return mirrored ? -dome : dome;
        }

        // LLAtmospherics::updateFog's colour, toward the light @a tosun_in (the agent's frame) a little above the
        // horizon, taking calcSkyColorInDir's plain path, whose LLSettingsSky::gammaCorrect left the input doubled and
        // clamped.
        LLColor3 fogColor(const Vars& v, const LLVector3& tosun_in)
        {
            static const FastLn fast_ln;
            LLVector3 tosun = tosun_in;
            const F32 tosun_z = tosun.mV[VZ];
            tosun.mV[VZ] = 0.f;
            tosun.normalize();
            LLVector3 perp_tosun(-tosun.mV[VY], tosun.mV[VX], 0.f);
            LLVector3 tosun_45 = tosun + perp_tosun;
            tosun_45.normalize();

            const F32 delta = 0.06f;
            tosun.mV[VZ] = delta;
            perp_tosun.mV[VZ] = delta;
            tosun_45.mV[VZ] = delta;
            tosun.normalize();
            perp_tosun.normalize();
            tosun_45.normalize();

            LLColor3 sum;
            for (const LLVector3& dir : { tosun, perp_tosun, tosun_45 })
            {
                sum += componentSaturate(hazeColor(v, windlightFrame(dir, true)) * 4.f);
            }
            const F32 m = color_max(sum);
            LLColor3 sky_fog_color = m > 1.f ? sum * (1.f / m) : sum;

            const F32 full_off = -0.25f;
            const F32 full_on = 0.00f;
            F32 on = (tosun_z - full_off) / (full_on - full_off);
            on = llclamp(on, 0.01f, 1.f);
            sky_fog_color *= 0.5f * on;

            for (S32 i = 0; i < 3; i++)
            {
                sky_fog_color.mV[i] = llmax(0.0001f, sky_fog_color.mV[i]);
            }
            const F32 gamma_inv = 1.f / 1.2f;
            for (S32 i = 0; i < 3; ++i)
            {
                sky_fog_color.mV[i] = fast_ln.pow(sky_fog_color.mV[i], gamma_inv);
            }
            return sky_fog_color;
        }

        // calcSkyColorInDir's shiny path for @a dir in the agent's frame, the fog colour below the horizon; or with
        // @a mirrored false, the sky along dir itself, and with @a desaturated false, its haze colour as it is.
        LLColor4 shiny(const Vars& v, const LLColor3& fog, const LLVector3& dir, bool mirrored, bool desaturated)
        {
            const F32 sky_saturation = 0.25f;
            const F32 land_saturation = 0.1f;

            if (desaturated && dir.mV[VZ] < -0.02f)
            {
                LLColor4 col;
                LLColor3 desat_fog = fog;
                F32 brightness = desat_fog.brightness();
                if (brightness < 0.15f)
                {
                    brightness = 0.15f;
                    desat_fog = smear(0.15f);
                }
                F32 greyscale_sat = brightness * (1.0f - land_saturation);
                desat_fog = desat_fog * land_saturation + smear(greyscale_sat);
                col = LLColor4(desat_fog * 0.5f, 0.f);
                float x = 1.0f - fabsf(-0.1f - dir.mV[VZ]);
                x *= x;
                col.mV[0] *= x * x;
                col.mV[1] *= powf(x, 2.5f);
                col.mV[2] *= x * x * x;
                return col;
            }

            const LLColor3 haze = hazeColor(v, windlightFrame(dir, mirrored));
            if (!desaturated)
            {
                return LLColor4(haze, 0.f);
            }
            const F32 brightness = haze.brightness();
            const F32 greyscale_sat = brightness * (1.0f - sky_saturation);
            return LLColor4(haze * sky_saturation + smear(greyscale_sat), 0.0f);
        }

        // The cube as LLVOSky::initCubeMap made it: each side's texels from LLVOSky::initSkyTextureDirs' directions,
        // row x and column y, eight bits, through LLCubeMap's flips onto GL's faces.
        LLPointer<LLCubeMap> cube(const Sky& sky, bool mirrored, bool desaturated)
        {
            constexpr S32 RES = 64;
            const Vars v = vars(sky);
            // The light in the agent's frame, as getClampedLightDirection gives it to the fog.
            LLVector3 tosun(v.sun_norm.mV[2], v.sun_norm.mV[0], v.sun_norm.mV[1]);
            if (tosun.mV[VZ] < -0.1f)
            {
                tosun.mV[VZ] = -0.1f;
            }
            const LLColor3 fog = fogColor(v, tosun);

            std::vector<LLPointer<LLImageRaw>> images;
            for (S32 side = 0; side < 6; ++side)
            {
                LLPointer<LLImageRaw> raw = new LLImageRaw(RES, RES, 4);
                U8* data = raw->getData();
                F32 coeff[3] = { 0, 0, 0 };
                const S32 curr_coef = side >> 1;
                const S32 side_dir = (((side & 1) << 1) - 1);
                const S32 x_coef = (curr_coef + 1) % 3;
                const S32 y_coef = (x_coef + 1) % 3;
                coeff[curr_coef] = (F32)side_dir;
                for (S32 x = 0; x < RES; ++x)
                {
                    for (S32 y = 0; y < RES; ++y)
                    {
                        coeff[x_coef] = F32((x << 1) + 1) / RES - 1.f;
                        coeff[y_coef] = F32((y << 1) + 1) / RES - 1.f;
                        LLVector3 dir(coeff[0], coeff[1], coeff[2]);
                        dir.normalize();
                        const LLColor4U texel = shiny(v, fog, dir, mirrored, desaturated);
                        std::memcpy(data + (x * RES + y) * 4, texel.mV, 4);
                    }
                }
                images.push_back(raw);
            }
            LLPointer<LLCubeMap> cube = new LLCubeMap();
            cube->init(images);
            gGL.getTextureSlot(0)->unbind();
            return cube;
        }
    } // namespace legacy

    // Absolute differences between two pictures over the pixels @a where keeps: the largest and the mean channel's.
    struct Difference
    {
        F32 mMost = 0.f;
        F64 mSum = 0.0;
        S32 mCount = 0;

        F32 mean() const { return mCount ? (F32)(mSum / mCount) : 0.f; }
        std::string summary() const
        {
            std::ostringstream out;
            out.precision(3);
            out << std::fixed << mMost * 255.f << "/255 at most, " << mean() * 255.f << "/255 on average";
            return out.str();
        }
    };

    template <typename Keep>
    Difference compare(const std::vector<F32>& a, const std::vector<F32>& b, Keep&& keep)
    {
        Difference d;
        for (size_t px = 0; px * 4 < a.size(); ++px)
        {
            if (!keep(px))
            {
                continue;
            }
            for (S32 c = 0; c < 3; ++c)
            {
                const F32 delta = fabsf(a[px * 4 + c] - b[px * 4 + c]);
                d.mMost = llmax(d.mMost, delta);
                d.mSum += delta;
                ++d.mCount;
            }
        }
        return d;
    }

    struct alskycube_gl_data
    {
        alskycube_gl_data()
        : mShaders(mTree.prefix())
        {
        }

        ~alskycube_gl_data()
        {
            LLGLSLShader::unbind();
            mBake.unload();
            mLookup.unload();
            mShaders.clearShaderObjects();
            mStrip.release();
            mPicture.release();
            mTriangle = nullptr;
            mRainbow = nullptr;
            mHalo = nullptr;
            for (U32 texture : { mCube, mStripDirs, mPanoramaDirs })
            {
                if (texture)
                {
                    LLImageGL::deleteTextures(1, &texture);
                }
            }
            if (mEnvironment)
            {
                glDeleteBuffers(1, &mEnvironment);
            }
        }

        // The viewer's bake program and the test's lookup, the strip the bake draws into, the pictures, and each
        // pixel's direction for the strip (every texel's centre, face by face) and for the panorama.
        void setUp()
        {
            mTree.populate();
            REQUIRE(mTree.write("skytest/skyCubeLookupV.glsl", kLookupVertex));
            REQUIRE(mTree.write("skytest/skyCubeLookupF.glsl", kLookupFragment));
            loadSharedObjects(mShaders);

            buildProgram(mBake, "Sky Environment Cube Program",
                         { { "deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER },
                           { "deferred/skyCubeF.glsl", GL_FRAGMENT_SHADER },
                           { "windlight/windlightDomeF.glsl", GL_FRAGMENT_SHADER } },
                         false);
            buildProgram(mLookup, "The environment cube along each pixel's direction (test)",
                         { { "skytest/skyCubeLookupV.glsl", GL_VERTEX_SHADER },
                           { "skytest/skyCubeLookupF.glsl", GL_FRAGMENT_SHADER },
                           { "windlight/windlightDomeF.glsl", GL_FRAGMENT_SHADER } },
                         false);

            REQUIRE(mStrip.allocate(ALSkyCube::STRIP_WIDTH, ALSkyCube::SIZE, GL_RGBA8));
            REQUIRE(mPicture.allocate(llmax((S32)ALSkyCube::STRIP_WIDTH, PANORAMA_W), llmax((S32)ALSkyCube::SIZE, PANORAMA_H),
                                      GL_RGBA32F));
            mTriangle = screenTriangle();
            mRainbow  = gradient(1);
            mHalo     = gradient(4);
            glGenBuffers(1, &mEnvironment);

            std::vector<LLVector3> strip(size_t(ALSkyCube::STRIP_WIDTH) * ALSkyCube::SIZE);
            for (U32 face = 0; face < 6; ++face)
            {
                for (U32 y = 0; y < ALSkyCube::SIZE; ++y)
                {
                    for (U32 x = 0; x < ALSkyCube::SIZE; ++x)
                    {
                        strip[y * ALSkyCube::STRIP_WIDTH + face * ALSkyCube::SIZE + x] =
                            ALSkyCube::texelDirection(face, x, y);
                    }
                }
            }
            mStripDirs = directions(strip, ALSkyCube::STRIP_WIDTH, ALSkyCube::SIZE);

            std::vector<LLVector3> panorama(size_t(PANORAMA_W) * PANORAMA_H);
            for (S32 y = 0; y < PANORAMA_H; ++y)
            {
                for (S32 x = 0; x < PANORAMA_W; ++x)
                {
                    const F32 bearing   = ((F32)x + 0.5f) / PANORAMA_W * 360.f;
                    const F32 elevation = ((F32)y + 0.5f) / PANORAMA_H * 180.f - 90.f;
                    panorama[y * PANORAMA_W + x] = agentDirection(elevation, bearing);
                }
            }
            mPanoramaDirs = directions(panorama, PANORAMA_W, PANORAMA_H);
        }

        // @a dirs as an RGB32F texture of @a width by @a height, read texel by texel.
        static U32 directions(const std::vector<LLVector3>& dirs, S32 width, S32 height)
        {
            U32 name = 0;
            LLImageGL::generateTextures(1, &name);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, name);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, dirs.data());
            gGL.getTextureSlot(0)->unbind();
            return name;
        }

        // The cube baked from @a sky, as ALSky bakes it: the Environment block and the dome's uniforms on the bound
        // program, ALSkyCube's bake over the strip.
        void bake(const Sky& sky)
        {
            if (!mCube)
            {
                mCube = ALSkyCube::allocate();
                REQUIRE(mCube != 0);
            }
            bindEnvironment(mBake, mEnvironment, sky, IDENTITY);
            mBake.bind();
            bindDome(mBake, sky, mRainbow, mHalo);
            ALSkyCube::bake(mBake, mCube, mStrip, *mTriangle);
            mBake.disableTexture(LLShaderMgr::RAINBOW_MAP);
            mBake.disableTexture(LLShaderMgr::HALO_MAP);
            mBake.unbind();
        }

        // The lookup over @a dirs (@a width by @a height): @a cube at @a lod, or with @a cube 0, what it should hold
        // for @a sky. Read back.
        std::vector<F32> lookup(const Sky& sky, U32 cube, F32 lod, U32 dirs, S32 width, S32 height)
        {
            mPicture.bindTarget();
            glViewport(0, 0, width, height);
            bindEnvironment(mLookup, mEnvironment, sky, IDENTITY);
            mLookup.bind();
            bindDome(mLookup, sky, mRainbow, mHalo);
            constexpr S32 DIRS_UNIT = 10;
            constexpr S32 CUBE_UNIT = 11;
            gGL.getTextureSlot(DIRS_UNIT)->bindManual(ALTextureSlot::TT_TEXTURE, dirs);
            if (cube)
            {
                gGL.getTextureSlot(CUBE_UNIT)->bindManual(ALTextureSlot::TT_CUBE_MAP, cube,
                                                          gGL.getSampler(ALSamplers::TrilinearClamp));
            }
            const GLuint object = mLookup.mProgramObject;
            glUniform1i(glGetUniformLocation(object, "test_dirs"), DIRS_UNIT);
            glUniform1i(glGetUniformLocation(object, "test_cube"), CUBE_UNIT);
            glUniform1i(glGetUniformLocation(object, "test_mode"), cube ? 0 : 1);
            glUniform1f(glGetUniformLocation(object, "test_lod"), lod);
            // ALFarPlane::skyFrame as a column-major matrix: its columns are the agent's axes in the dome's frame.
            F32 to_dome[9] = {};
            for (S32 axis = 0; axis < 3; ++axis)
            {
                LLVector3 agent;
                agent.mV[axis] = 1.f;
                const LLVector3 dome = ALFarPlane::skyFrame(agent);
                for (S32 row = 0; row < 3; ++row)
                {
                    to_dome[axis * 3 + row] = dome.mV[row];
                }
            }
            glUniformMatrix3fv(glGetUniformLocation(object, "test_to_dome"), 1, GL_FALSE, to_dome);
            {
                LLGLDisable   blend(GL_BLEND);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            gGL.getTextureSlot(DIRS_UNIT)->unbind();
            gGL.getTextureSlot(CUBE_UNIT)->unbind();
            mLookup.disableTexture(LLShaderMgr::RAINBOW_MAP);
            mLookup.disableTexture(LLShaderMgr::HALO_MAP);
            mLookup.unbind();

            std::vector<F32> out(size_t(width) * height * 4);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, out.data());
            mPicture.flush();
            return out;
        }

        // Face @a face's level @a level of @a cube, read back.
        static std::vector<F32> level(U32 cube, U32 face, U32 level)
        {
            const U32 size = ALSkyCube::SIZE >> level;
            std::vector<F32> out(size_t(size) * size * 4);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_CUBE_MAP, cube);
            glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA, GL_FLOAT, out.data());
            gGL.getTextureSlot(0)->unbind();
            return out;
        }

        static constexpr std::array<F32, 9> IDENTITY = { 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f };

        SkyShaderTree          mTree;
        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader           mBake;
        LLGLSLShader           mLookup;
        LLRenderTarget         mStrip;
        LLRenderTarget         mPicture;
        LLPointer<LLVertexBuffer> mTriangle;
        LLPointer<LLImageGL>   mRainbow;
        LLPointer<LLImageGL>   mHalo;
        GLuint                 mEnvironment = 0;
        U32                    mCube = 0;
        U32                    mStripDirs = 0;
        U32                    mPanoramaDirs = 0;
    };
}

TEST_CASE_METHOD(alskycube_gl_data, "alskycube_gl: each texel holds windlightDome along its direction, halved",
                 "[alskycube_gl]")
{
    setUp();
    for (const Sky& sky : SKIES)
    {
        CAPTURE(sky.mName);
        bake(sky);
        const std::vector<F32> baked    = lookup(sky, mCube, 0.f, mStripDirs, ALSkyCube::STRIP_WIDTH, ALSkyCube::SIZE);
        const std::vector<F32> expected = lookup(sky, 0, 0.f, mStripDirs, ALSkyCube::STRIP_WIDTH, ALSkyCube::SIZE);
        const Difference difference = compare(baked, expected, [](size_t) { return true; });
        const std::string summary = difference.summary();
        CAPTURE(summary);
        CHECK(difference.mCount == (S32)(ALSkyCube::STRIP_WIDTH * ALSkyCube::SIZE * 3));
        CHECK(difference.mMost <= BAKED);

        // The faces differ: the sky is not one colour, so a face read in the wrong place would show.
        F32 lowest = 1.f;
        F32 highest = 0.f;
        for (size_t px = 0; px * 4 < expected.size(); ++px)
        {
            lowest  = llmin(lowest, expected[px * 4 + 2]);
            highest = llmax(highest, expected[px * 4 + 2]);
        }
        CHECK(highest - lowest > 0.05f);
    }
}

TEST_CASE_METHOD(alskycube_gl_data, "alskycube_gl: every level is made from the first", "[alskycube_gl]")
{
    setUp();
    F32 brightest = 0.f;
    for (const Sky& sky : SKIES)
    {
        CAPTURE(sky.mName);
        bake(sky);
        for (U32 face = 0; face < 6; ++face)
        {
            CAPTURE(face);
            const std::vector<F32> first = level(mCube, face, 0);
            const std::vector<F32> last  = level(mCube, face, ALSkyCube::LEVELS - 1);
            REQUIRE(last.size() == 4);
            for (S32 c = 0; c < 3; ++c)
            {
                F64 sum = 0.0;
                for (size_t px = 0; px * 4 < first.size(); ++px)
                {
                    sum += first[px * 4 + c];
                }
                const F32 mean = (F32)(sum / (first.size() / 4));
                CAPTURE(c, mean, last[c]);
                CHECK(fabsf(last[c] - mean) <= MIPPED);
                brightest = llmax(brightest, mean);
            }
        }
    }
    // Some face bright enough that a level left unmade would show.
    CHECK(brightest > 0.1f);
}

// A cube the readers bind before its first bake, or where the bake cannot run, reads as something: complete, with
// every face and level NEUTRAL, read back level by level and through the trilinear sampler the readers' lookup takes,
// at each level. The storage GL makes holds undefined contents, black on most drivers.
TEST_CASE_METHOD(alskycube_gl_data, "alskycube_gl: a cube never baked is complete and neutral at every level",
                 "[alskycube_gl]")
{
    setUp();
    mCube = ALSkyCube::allocate();
    REQUIRE(mCube != 0);

    gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_CUBE_MAP, mCube);
    GLint immutable = GL_FALSE;
    GLint levels    = 0;
    glGetTexParameteriv(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_IMMUTABLE_FORMAT, &immutable);
    glGetTexParameteriv(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_IMMUTABLE_LEVELS, &levels);
    gGL.getTextureSlot(0)->unbind();
    CHECK(immutable == GL_TRUE);
    CHECK(levels == (GLint)ALSkyCube::LEVELS);

    const F32 neutral = ALSkyCube::NEUTRAL / 255.f;
    const auto neutral_everywhere = [neutral](const std::vector<F32>& texels, bool with_alpha)
    {
        F32 most = 0.f;
        for (size_t px = 0; px * 4 < texels.size(); ++px)
        {
            for (S32 c = 0; c < 3; ++c)
            {
                most = llmax(most, fabsf(texels[px * 4 + c] - neutral));
            }
            if (with_alpha)
            {
                most = llmax(most, fabsf(texels[px * 4 + 3] - 1.f));
            }
        }
        return most;
    };

    for (U32 face = 0; face < 6; ++face)
    {
        for (U32 l = 0; l < ALSkyCube::LEVELS; ++l)
        {
            CAPTURE(face, l);
            const std::vector<F32> texels = level(mCube, face, l);
            CHECK(texels.size() == size_t(ALSkyCube::SIZE >> l) * (ALSkyCube::SIZE >> l) * 4);
            CHECK(neutral_everywhere(texels, true) <= HALF_STEP);
        }
    }

    for (U32 l = 0; l < ALSkyCube::LEVELS; ++l)
    {
        CAPTURE(l);
        const std::vector<F32> read = lookup(SKIES[0], mCube, (F32)l, mStripDirs, ALSkyCube::STRIP_WIDTH, ALSkyCube::SIZE);
        CHECK(neutral_everywhere(read, false) <= HALF_STEP);
    }
}

// The cube's storage is counted as an image's from its making to its deletion: six faces, each with every level.
TEST_CASE_METHOD(alskycube_gl_data, "alskycube_gl: the cube's storage is counted whole while it lives",
                 "[alskycube_gl]")
{
    const U64 before = LLImageGL::getTextureBytesAllocated(LLImageGLMemory::OTHER_TEXTURE);
    mCube = ALSkyCube::allocate();
    REQUIRE(mCube != 0);
    const U64 whole =
        (U64)LLImageGL::dataFormatVRAMBytes(GL_RGBA8, ALSkyCube::SIZE, ALSkyCube::SIZE, ALSkyCube::LEVELS) * 6;
    CHECK(LLImageGL::getTextureBytesAllocated(LLImageGLMemory::OTHER_TEXTURE) - before == whole);

    LLImageGL::deleteTextures(1, &mCube);
    mCube = 0;
    CHECK(LLImageGL::getTextureBytesAllocated(LLImageGLMemory::OTHER_TEXTURE) == before);
}

TEST_CASE_METHOD(alskycube_gl_data, "alskycube_gl: the baked cube against the one the CPU built", "[alskycube_gl]")
{
    setUp();
    for (const Sky& sky : SKIES)
    {
        CAPTURE(sky.mName);
        bake(sky);
        const std::vector<F32> baked = lookup(sky, mCube, 0.f, mPanoramaDirs, PANORAMA_W, PANORAMA_H);

        // Above the old cube's horizon, below it, and the sliver between, by the panorama's rows.
        const auto elevation = [](size_t px) { return ((F32)(px / PANORAMA_W) + 0.5f) / PANORAMA_H * 180.f - 90.f; };
        const auto above = [&](size_t px) { return sinf(elevation(px) * DEG_TO_RAD) >= -0.02f; };
        const auto below = [&](size_t px) { return sinf(elevation(px) * DEG_TO_RAD) < -0.02f; };

        // The old cube as it was; its sky along each direction itself rather than the opposite one; and that sky's
        // haze colour as it is rather than at a quarter of its saturation, and in place of the fog below the horizon.
        const struct
        {
            const char* mName;
            bool        mMirrored;
            bool        mDesaturated;
        } OLD[] = { { "the old cube", true, true },
                    { "the old cube's sky along each direction itself", false, true },
                    { "that sky's haze colour as it is, below the horizon too", false, false } };
        for (const auto& old : OLD)
        {
            LLPointer<LLCubeMap> cube = legacy::cube(sky, old.mMirrored, old.mDesaturated);
            REQUIRE(cube->getGLName() != 0);
            const std::vector<F32> picture = lookup(sky, cube->getGLName(), 0.f, mPanoramaDirs, PANORAMA_W, PANORAMA_H);
            const Difference up   = compare(baked, picture, above);
            const Difference down = compare(baked, picture, below);
            WARN(sky.mName << ", the bake against " << old.mName << ": above its horizon " << up.summary()
                           << "; below it " << down.summary());
            CHECK(up.mCount > 0);
            CHECK(down.mCount > 0);
            cube = nullptr;
        }
    }
}
