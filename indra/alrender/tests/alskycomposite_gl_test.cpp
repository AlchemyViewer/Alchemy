/**
 * @file alskycomposite_gl_test.cpp
 * @brief The whole sky two ways on the hidden window: as it was drawn before its composite, every layer rasterized into
 *        the G-buffer in turn by its own program (tests/skyraster) and resolved by the soften pass's sky branch, and as
 *        the composite draws it, every layer worked out along the pixel's view ray and composited in one pass over the
 *        stars' attachment. The two must leave the same lit picture but where the composite deliberately departs: the
 *        8-bit albedo no longer clamps a layer at 1 (D6), and below the horizon the dome is the sphere rather than the
 *        old mesh's skirt (D7).
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
#include "alstarfield.h"

#include <fmt/format.h>

#include <bit>

namespace
{
    using namespace al_sky_test;

    constexpr S32 W = 256;
    constexpr S32 H = 160;

    // WindLight's fake HDR scale, the same both ways.
    constexpr F32 SKY_HDR_SCALE = 1.25f;

    // The sky's resolve as the soften pass's SOFTEN_SKY branch ran it before the composite: skyColor() of what the
    // layers left in the G-buffer, the G-buffer read as gbufferUtil.glsl read it. With TEST_ENCODED_ALBEDO the albedo
    // is a float attachment the layers wrote display-encoded values into, decoded here as the sRGB attachment's read
    // decoded them.
    const char* kResolveVertex = R"(in vec3 position;

out vec2 vary_fragcoord;

void main()
{
    gl_Position = vec4(position.xy, 0.0, 1.0);
    vary_fragcoord = position.xy * 0.5 + 0.5;
}
)";

    const char* kResolveFragment = R"(/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

in vec2 vary_fragcoord;

uniform sampler2D diffuseRect;
uniform sampler2D normalMap;
#if defined(HAS_EMISSIVE)
uniform sampler2D emissiveRect;
#endif

uniform float sky_hdr_scale;

vec3 srgb_to_linear(vec3 c);

void main()
{
    vec2 tc = vary_fragcoord.xy;
    vec4 albedo = texture(diffuseRect, tc);
#ifdef TEST_ENCODED_ALBEDO
    albedo.rgb = srgb_to_linear(albedo.rgb);
#endif
    float flag = texture(normalMap, tc).w;

    vec3 color;
    if (GET_GBUFFER_FLAG(flag, GBUFFER_FLAG_HAS_HDRI))
    {
#if defined(HAS_EMISSIVE)
        color = texture(emissiveRect, tc).rgb;
#else
        color = albedo.rgb;
#endif
    }
    else
    {
#if defined(HAS_EMISSIVE)
        color = srgb_to_linear(texture(emissiveRect, tc).rgb);
#else
        color = albedo.rgb;
#endif
        color *= sky_hdr_scale;
    }
    frag_color = vec4(color, 1.0);
}
)";

    // The composite's layers through the composite's own functions, every one worked out at every pixel and laid in
    // the composite's order, clouds last: what the composite must leave wherever the clouds let anything through.
    const char* kInOrderFragment = R"(/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

in vec2 vary_fragcoord;

uniform int   skyLayers;
uniform float sky_hdr_scale;

vec3 srgb_to_linear(vec3 c);
vec3 skyRay(vec2 ndc);
vec3 skyStars(vec2 tc);
vec3 windlightDome(vec3 dir);
vec4 skySun(vec3 dir);
vec4 skyMoon(vec3 dir);
vec4 skyBloom(vec3 dir);
vec3 skyMeteors(vec2 ndc);
vec3 skyAurora(vec3 dir);
vec4 skyClouds(vec3 dir);

void main()
{
    vec2 ndc = vary_fragcoord * 2.0 - 1.0;
    vec3 dir = skyRay(ndc);
    vec3 color = windlightDome(dir);
    if ((skyLayers & TEST_LAYER_STARS) != 0)
    {
        color += skyStars(vary_fragcoord);
    }
    if ((skyLayers & TEST_LAYER_BLOOM) != 0)
    {
        vec4 bloom = skyBloom(dir);
        color = mix(color, bloom.rgb, bloom.a);
    }
    if ((skyLayers & TEST_LAYER_SUN) != 0)
    {
        vec4 sun = skySun(dir);
        color = mix(color, sun.rgb, sun.a);
    }
    if ((skyLayers & TEST_LAYER_MOON) != 0)
    {
        vec4 moon = skyMoon(dir);
        color = mix(color, moon.rgb, moon.a);
    }
    if ((skyLayers & TEST_LAYER_METEORS) != 0)
    {
        color += skyMeteors(ndc);
    }
    if ((skyLayers & TEST_LAYER_AURORA) != 0)
    {
        color += skyAurora(dir);
    }
    if ((skyLayers & TEST_LAYER_CLOUDS) != 0)
    {
        vec4 clouds = skyClouds(dir);
        color = mix(color, clouds.rgb, clouds.a);
    }
    frag_color = vec4(srgb_to_linear(color) * sky_hdr_scale, 1.0);
}
)";

    // The clouds alone along each pixel's ray: their colour decoded and scaled as the composite leaves it, and their
    // opacity.
    const char* kCloudsAloneFragment = R"(/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

in vec2 vary_fragcoord;

uniform float sky_hdr_scale;

vec3 srgb_to_linear(vec3 c);
vec3 skyRay(vec2 ndc);
vec4 skyClouds(vec3 dir);

void main()
{
    vec4 clouds = skyClouds(skyRay(vary_fragcoord * 2.0 - 1.0));
    frag_color = vec4(srgb_to_linear(clouds.rgb) * sky_hdr_scale, clouds.a);
}
)";

    // The sun's halo as the composite lays its bloom, drawn as a quad (alskylayers_gl_test's reference).
    const char* kHaloVertex = R"(//[ENGINE_BLOCK Matrices]

in vec3 position;
in vec2 texcoord0;

out vec2 vary_texcoord0;

void main()
{
    vec4 pos = modelview_projection_matrix * vec4(position - vec3(0.0, 0.0, 50.0), 1.0);
    pos.z = pos.w;
    gl_Position = pos;
    vary_texcoord0 = texcoord0;
}
)";

    const char* kHaloFragment = R"(/*[EXTRA_CODE_HERE]*/

out vec4 frag_data[4];

uniform sampler2D diffuseMap;
uniform float blend_factor;
uniform vec4 color;

in vec2 vary_texcoord0;

void main()
{
    vec4 c = texture(diffuseMap, vary_texcoord0) * color;
    frag_data[0] = vec4(0.0);
    frag_data[1] = vec4(0.0);
    frag_data[2] = vec4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);
#if defined(HAS_EMISSIVE)
    frag_data[3] = c;
#else
    frag_data[0] = c;
#endif
}
)";

    // The moonlit night under a cloud deck dense enough to be opaque over most of the sky, with gaps.
    const Sky CLOUD_DECK_SKY = { "night, moonlit, a cloud deck", { 0.12f, 0.18f, 0.38f }, { 0.18f, 0.22f, 0.36f }, 0.5f,
                                 0.12f, 0.00018f, 1200.f, { 5.f, 0.001f, -0.4799f }, 0.45f, { 0.21f, 0.245f, 0.35f },
                                 { 0.04f, 0.045f, 0.08f }, 35.f, 140.f, 0.125f, 0.f, 800.f, 0.f,
                                 { 0.22f, 0.23f, 0.3f }, { 0.81f, 0.13f, 1.f }, { 0.09f, 0.55f, 0.11f }, 0.35f,
                                 0.2f };

    // A frame of the sky: its settings, a view, and its layers (ALSkyLayers::Layer), with what each takes.
    struct Scene
    {
        const char* mName;
        const Sky*  mSky;
        View        mView;
        S32         mLayers;
        F32         mSunElevation;   ///< the sun's, where it is drawn
        F32         mSunBearing;
        F32         mMoonElevation;  ///< the moon's, where it is drawn
        F32         mMoonBearing;
        F32         mStarAlpha;      ///< the star brightness over 500
        F32         mAurora;         ///< the aurora's intensity
        F32         mCloudBlend;     ///< how far the clouds' noise has blended toward the next
        F32         mHdriSplit;      ///< the HDRI's share of the screen, for an HDRI scene
    };

    using L = ALSkyLayers::Layer;

    const Scene SCENES[] = {
        { "clouded midday, the sun in view", &SKIES[0], { 80.f, 40.f, 70.f }, L::SUN | L::CLOUDS, 60.f, 80.f, 0.f, 0.f,
          0.f, 0.f, 0.f, 0.f },
        { "clouded midday, across the horizon", &SKIES[0], { 120.f, 8.f, 70.f }, L::SUN | L::CLOUDS, 60.f, 80.f, 0.f,
          0.f, 0.f, 0.f, 0.3f, 0.f },
        { "sunset, the sun and its bloom on the horizon", &SKIES[1], { 255.f, 4.f, 50.f },
          L::STARS | L::BLOOM | L::SUN | L::CLOUDS, 3.f, 260.f, 0.f, 0.f, 0.03f, 0.f, 0.35f, 0.f },
        { "moonlit night: stars, moon, meteors, aurora and clouds", &SKIES[2], { 140.f, 25.f, 80.f },
          L::STARS | L::MOON | L::METEORS | L::AURORA | L::CLOUDS, 0.f, 0.f, 35.f, 140.f, 0.5f, 0.6f, 0.f, 0.f },
        { "moonlit night as a probe's face draws it", &SKIES[2], { 140.f, 25.f, 80.f }, L::MOON | L::CLOUDS, 0.f, 0.f,
          35.f, 140.f, 0.5f, 0.6f, 0.f, 0.f },
        { "rainbow and halo below the horizon", &RAINBOW_SKY, { 20.f, -12.f, 60.f }, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
          0.f, 0.f },
        { "HDRI beside WindLight's dome", &SKIES[0], { 30.f, 15.f, 70.f }, L::HDRI, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
          0.5f },
    };

    // The stars' brightness cull is the stars' own (alstarfield_gl_test); here every star is drawn.
    constexpr U32 STAR_COUNT = ALStarField::DEFAULT_COUNT;

    // A meteor shower over the moonlit night.
    std::vector<ALSkyLayers::Meteor> meteors()
    {
        struct M { F32 el, br, half, heading; LLColor4U colour; F32 width; };
        const M showers[] = {
            { 35.f, 130.f, 0.06f, 30.f, LLColor4U(160, 90, 40, 0), 0.6f },
            { 28.f, 150.f, 0.09f, 160.f, LLColor4U(220, 210, 180, 0), 1.0f },
            { 40.f, 120.f, 0.12f, 250.f, LLColor4U(90, 255, 115, 0), 2.7f },
        };
        std::vector<ALSkyLayers::Meteor> out;
        for (const M& m : showers)
        {
            const LLVector3 dir = agentDirection(m.el, m.br);
            LLVector3 east = LLVector3::z_axis % dir;
            east.normVec();
            const LLVector3 north = dir % east;
            const F32 heading = m.heading * DEG_TO_RAD;
            ALSkyLayers::Meteor meteor;
            meteor.mMidpoint = dir * DOME_RADIUS;
            meteor.mHalf = (east * sinf(heading) + north * cosf(heading)) * (DOME_RADIUS * m.half);
            meteor.mColour = m.colour;
            meteor.mColour.mV[VALPHA] = (U8)llclamp(std::lround(m.width * 0.25f * 255.f), 0L, 255L);
            meteor.mWidth = m.width;
            out.push_back(meteor);
        }
        return out;
    }

    // The sky's pictures of a scene, linear, as the lit screen takes them.
    using Picture = std::vector<F32>;

    // How two pictures differ over a region, display encoded (an HDRI's colour linear): the most, and the pixels past
    // a bound.
    struct Difference
    {
        F32 mMost   = 0.f;
        S32 mBeyond = 0;
        S32 mPixels = 0;
        S32 mWorstX = -1;
        S32 mWorstY = -1;

        std::string summary() const
        {
            return fmt::format("most {:.6f} ({:.2f}/255) at ({}, {}), {} of {} pixels past the bound", mMost,
                               mMost * 255.f, mWorstX, mWorstY, mBeyond, mPixels);
        }
    };

    F32 encode(F32 linear)
    {
        linear = llmax(linear, 0.f);
        return linear < 0.0031308f ? linear * 12.92f : 1.055f * powf(linear, 1.f / 2.4f) - 0.055f;
    }

    struct alskycomposite_gl_data
    {
        alskycomposite_gl_data()
        : mShaders(mTree.prefix())
        {
        }

        ~alskycomposite_gl_data()
        {
            LLGLSLShader::unbind();
            for (S32 path = 0; path < 2; ++path)
            {
                for (LLGLSLShader* program : { &mOldSky[path], &mOldHdri[path], &mOldSun[path], &mOldMoon[path],
                                               &mOldMeteors[path], &mOldAurora[path], &mOldClouds[path], &mSunHalo[path],
                                               &mStarPoints[path], &mStarQuads[path], &mResolve[path][0],
                                               &mResolve[path][1], &mComposite[path], &mInOrder[path],
                                               &mCloudsAlone[path] })
                {
                    program->unload();
                }
                for (LLRenderTarget& target : mGBuffer[path])
                {
                    target.release();
                }
            }
            mShaders.clearShaderObjects();
            mScreen.release();
            for (LLPointer<LLVertexBuffer>* buffer : { &mTriangle, &mDome, &mSphere, &mStarBuffer })
            {
                *buffer = nullptr;
            }
            for (LLPointer<LLImageGL>* map : { &mRainbow, &mHaloMap, &mSunTexture, &mMoonTexture, &mBloomTexture,
                                               &mCloudNoise, &mCloudNoiseNext, &mHdri })
            {
                *map = nullptr;
            }
            if (mEnvironment)
            {
                glDeleteBuffers(1, &mEnvironment);
            }
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
        }

        void setUp()
        {
            mTree.populate();
            REQUIRE(mTree.write("skytest/skyResolveV.glsl", kResolveVertex));
            REQUIRE(mTree.write("skytest/skyResolveF.glsl", kResolveFragment));
            REQUIRE(mTree.write("skytest/sunHaloV.glsl", kHaloVertex));
            REQUIRE(mTree.write("skytest/sunHaloF.glsl", kHaloFragment));
            loadSharedObjects(mShaders);
            loadDeferredObjects(mShaders);

            for (S32 path = 0; path < 2; ++path)
            {
                const bool emissive = path == 1;
                const auto old_program = [&](LLGLSLShader& program, const char* name, const char* stem,
                                             const std::vector<std::pair<std::string, std::string>>& permutations = {})
                {
                    std::vector<std::pair<std::string, GLenum>> files = {
                        { fmt::format("skyraster/deferred/{}V.glsl", stem), GL_VERTEX_SHADER },
                        { fmt::format("skyraster/deferred/{}F.glsl", stem), GL_FRAGMENT_SHADER },
                        { "skyraster/deferred/ditherEmissiveF.glsl", GL_FRAGMENT_SHADER } };
                    if (std::string(stem) == "sky")
                    {
                        files.emplace_back("windlight/windlightDomeF.glsl", GL_FRAGMENT_SHADER);
                    }
                    buildProgram(program, name, files, emissive, permutations);
                };
                old_program(mOldSky[path], "Deferred Windlight Sky Shader (before the composite)", "sky");
                old_program(mOldHdri[path], "Environment Map Program (before the composite)", "sky",
                            { { "HAS_HDRI", "1" } });
                old_program(mOldSun[path], "Deferred Windlight Sun Program (before the composite)", "sunDisc");
                old_program(mOldMoon[path], "Deferred Windlight Moon Program (before the composite)", "moon");
                old_program(mOldMeteors[path], "Deferred Meteor Program (before the composite)", "meteors");
                old_program(mOldAurora[path], "Deferred Aurora Program (before the composite)", "aurora");
                old_program(mOldClouds[path], "Deferred Windlight Cloud Program (before the composite)", "clouds");
                buildProgram(mSunHalo[path], "The sun's halo before WindLight (test)",
                             { { "skytest/sunHaloV.glsl", GL_VERTEX_SHADER },
                               { "skytest/sunHaloF.glsl", GL_FRAGMENT_SHADER } },
                             emissive);
                for (S32 quads = 0; quads < 2; ++quads)
                {
                    LLGLSLShader& program = quads ? mStarQuads[path] : mStarPoints[path];
                    program.mShaderGroup = LLGLSLShader::SG_SKY;
                    buildProgram(program, quads ? "Deferred Star Quad Program" : "Deferred Star Program",
                                 { { "deferred/starsV.glsl", GL_VERTEX_SHADER },
                                   { "deferred/valueNoiseUtil.glsl", GL_VERTEX_SHADER },
                                   { "deferred/starsF.glsl", GL_FRAGMENT_SHADER } },
                                 emissive, quads ? std::vector<std::pair<std::string, std::string>>{ { "STAR_QUADS", "1" } }
                                                 : std::vector<std::pair<std::string, std::string>>{});
                }
                for (S32 encoded = 0; encoded < 2; ++encoded)
                {
                    buildProgram(mResolve[path][encoded], "The sky's resolve before the composite (test)",
                                 { { "skytest/skyResolveV.glsl", GL_VERTEX_SHADER },
                                   { "skytest/skyResolveF.glsl", GL_FRAGMENT_SHADER } },
                                 emissive,
                                 encoded ? std::vector<std::pair<std::string, std::string>>{ { "TEST_ENCODED_ALBEDO", "1" } }
                                         : std::vector<std::pair<std::string, std::string>>{});
                }
                // The sky's pass as LLViewerShaderMgr builds it, the sun bloom on.
                std::vector<std::pair<std::string, GLenum>> pass = {
                    { "deferred/skyCompositeV.glsl", GL_VERTEX_SHADER },
                    { "deferred/skyCompositeMainF.glsl", GL_FRAGMENT_SHADER } };
                for (const char* file : ALSkyLayers::COMPOSITE_FILES)
                {
                    pass.emplace_back(file, GL_FRAGMENT_SHADER);
                }
                mComposite[path].mFeatures.isDeferred = true;
                buildProgram(mComposite[path], "Deferred Sky Composite Shader", pass, emissive, { { "SUN_BLOOM", "1" } });

                // The G-buffer as the viewer allocates it, and with float colour attachments, where the layers'
                // values are neither clamped nor rounded.
                for (S32 storage = 0; storage < 2; ++storage)
                {
                    const bool stored = storage == 0;
                    LLRenderTarget& target = mGBuffer[path][storage];
                    REQUIRE(target.allocate(W, H, stored ? GL_SRGB8_ALPHA8 : GL_RGBA32F));
                    REQUIRE(target.addColorAttachment(GL_RGBA8));
                    REQUIRE(target.addColorAttachment(GL_RGB10_A2));
                    REQUIRE(target.addColorAttachment(stored ? GL_R11F_G11F_B10F : GL_RGBA32F));
                }
            }

            REQUIRE(mScreen.allocate(W, H, GL_RGBA32F));
            mTriangle    = screenTriangle();
            mDome        = domeMesh();
            mSphere      = fineSphere();
            mRainbow     = gradient(1);
            mHaloMap     = gradient(4);
            mSunTexture  = disc(1, 0.8f);
            mMoonTexture = disc(3, 0.9f);
            mBloomTexture = disc(7, 1.f);
            mCloudNoise  = cloudNoise(11);
            mCloudNoiseNext = cloudNoise(13);
            mHdri        = gradient(9, false);
            glGenBuffers(1, &mEnvironment);

            mField = ALStarField::generate(STAR_COUNT, DOME_RADIUS);
            mLayout = ALStarField::layOut(mField);
            mStarBuffer = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXTURE_INDEX);
            REQUIRE(mStarBuffer->allocateBuffer(mLayout.vertices(), 0));
            LLStrider<LLVector4a> star_vertices;
            REQUIRE(mStarBuffer->getVertexStrider(star_vertices));
            ALStarField::fillVertices(mField, mLayout, star_vertices);
            mStarBuffer->unmapBuffer();
        }

        // The composite's layers in order at every pixel, and the clouds alone, built from the composite's files.
        void buildReferences()
        {
            REQUIRE(mTree.write("skytest/skyInOrderF.glsl", kInOrderFragment));
            REQUIRE(mTree.write("skytest/skyCloudsAloneF.glsl", kCloudsAloneFragment));
            const std::vector<std::pair<std::string, std::string>> layers = {
                { "SUN_BLOOM", "1" },
                { "TEST_LAYER_STARS", std::to_string(L::STARS) },
                { "TEST_LAYER_BLOOM", std::to_string(L::BLOOM) },
                { "TEST_LAYER_SUN", std::to_string(L::SUN) },
                { "TEST_LAYER_MOON", std::to_string(L::MOON) },
                { "TEST_LAYER_METEORS", std::to_string(L::METEORS) },
                { "TEST_LAYER_AURORA", std::to_string(L::AURORA) },
                { "TEST_LAYER_CLOUDS", std::to_string(L::CLOUDS) } };
            for (S32 path = 0; path < 2; ++path)
            {
                for (S32 alone = 0; alone < 2; ++alone)
                {
                    std::vector<std::pair<std::string, GLenum>> files = {
                        { "skytest/skyResolveV.glsl", GL_VERTEX_SHADER },
                        { alone ? "skytest/skyCloudsAloneF.glsl" : "skytest/skyInOrderF.glsl", GL_FRAGMENT_SHADER } };
                    for (const char* file : ALSkyLayers::COMPOSITE_FILES)
                    {
                        files.emplace_back(file, GL_FRAGMENT_SHADER);
                    }
                    buildProgram(alone ? mCloudsAlone[path] : mInOrder[path],
                                 alone ? "The clouds alone (test)" : "The composite's layers in order (test)", files,
                                 path == 1, layers);
                }
            }
        }

        static ALSkyLayers::Corners sunCorners(const Scene& scene)
        {
            return ALSkyLayers::bodyCorners(bodyRotation(scene.mSunElevation, scene.mSunBearing), BODY_DISTANCE, 1.f,
                                            SUN_DISK_RADIUS);
        }

        static ALSkyLayers::Corners moonCorners(const Scene& scene)
        {
            return ALSkyLayers::bodyCorners(bodyRotation(scene.mMoonElevation, scene.mMoonBearing), BODY_DISTANCE, 1.f,
                                            MOON_DISK_RADIUS);
        }

        // The stars into the bound G-buffer, as ALSky::renderStars draws them: every star, into
        // the one attachment, the moon's quad masking those behind it.
        void drawStars(const Scene& scene, bool emissive, LLRenderTarget& target)
        {
            LLGLEnable  blend(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ADD);
            target.setDrawAttachment(ALSkyLayers::starAttachment(emissive));
            gGL.pushMatrix();
            gGL.translatef(CAMERA.mV[VX], CAMERA.mV[VY], CAMERA.mV[VZ]);

            const bool moon = (scene.mLayers & L::MOON) != 0;
            const LLVector3 moon_dir = agentDirection(scene.mMoonElevation, scene.mMoonBearing);
            const auto bind = [&](LLGLSLShader& program)
            {
                program.bind();
                program.uniform1f(LLShaderMgr::CUSTOM_ALPHA, scene.mStarAlpha);
                program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)W, (F32)H);
                program.uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, moon_dir.mV);
                program.uniform1f(LLShaderMgr::MOON_BRIGHTNESS, moon ? 0.8f : 0.f);
                program.uniform1f(LLShaderMgr::WATER_TIME, 321.f);
                if (moon)
                {
                    const ALSkyLayers::Corners c = moonCorners(scene);
                    const LLVector3 centre = (c[0] + c[3]) * 0.5f;
                    const F32 dist = centre.length();
                    LLVector3 half_u = (c[2] - c[0]) * (0.5f / dist);
                    LLVector3 half_v = (c[0] - c[1]) * (0.5f / dist);
                    half_u /= half_u.lengthSquared();
                    half_v /= half_v.lengthSquared();
                    const LLVector3 dir = centre / dist;
                    program.uniform3fv(LLShaderMgr::MOON_QUAD_CENTER, 1, dir.mV);
                    program.uniform3fv(LLShaderMgr::MOON_QUAD_U, 1, half_u.mV);
                    program.uniform3fv(LLShaderMgr::MOON_QUAD_V, 1, half_v.mV);
                    bindMap(program, LLShaderMgr::DIFFUSE_MAP, mMoonTexture, ALSamplers::AnisoClamp);
                }
                else
                {
                    program.uniform3f(LLShaderMgr::MOON_QUAD_CENTER, 0.f, 0.f, 0.f);
                }
            };

            bind(mStarQuads[emissive]);
            mStarBuffer->setBuffer();
            mStarBuffer->drawArrays(LLRender::TRIANGLES, 0, mLayout.mQuads * ALStarField::QUAD_VERTICES);
            mStarQuads[emissive].unbind();

            ALStarField::Frustum whole;
            for (LLVector3& normal : whole.mNormals)
            {
                normal.set(0.f, 0.f, 0.f);
            }
            ALStarField::Draws draws;
            ALStarField::pointDraws(mLayout, whole, 0, draws);
            LLGLEnable point_size(GL_PROGRAM_POINT_SIZE);
            bind(mStarPoints[emissive]);
            mStarBuffer->setBuffer();
            mStarBuffer->drawMultiArrays(LLRender::POINTS, draws.mFirst.data(), draws.mCount.data(),
                                         (U32)draws.mFirst.size());
            mStarPoints[emissive].unbind();

            gGL.popMatrix();
            target.setDrawBuffers();
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
        }

        // The old sky into @a emissive's G-buffer, every layer of @a scene by its own program over @a mesh in the
        // pool's order, then resolved. The G-buffer as the viewer allocates it where @a stored, float otherwise.
        Picture drawOld(const Scene& scene, bool emissive, bool stored, LLVertexBuffer* mesh)
        {
            const Sky& sky = *scene.mSky;
            const LLRender::eGeomModes mode = mesh == mSphere ? LLRender::TRIANGLES : LLRender::TRIANGLE_STRIP;
            LLRenderTarget& target = mGBuffer[emissive][stored ? 0 : 1];
            target.bindTarget();
            const F32 magenta[4] = { 1.f, 0.f, 1.f, 1.f };
            for (S32 attachment = 0; attachment < 4; ++attachment)
            {
                glClearBufferfv(GL_COLOR, attachment, magenta);
            }
            loadView(scene.mView, W, H);
            const std::array<F32, 9> eye_to_sky = ALFarPlane::eyeToSkyFrame(gGL.getModelviewMatrix());
            bindEnvironment(mComposite[emissive], mEnvironment, sky, eye_to_sky);
            {
                LLGLDisable   cull(GL_CULL_FACE);
                LLGLDisable   srgb(GL_FRAMEBUFFER_SRGB);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);

                // The dome, or the HDRI beside it.
                {
                    LLGLDisable blend(GL_BLEND);
                    const bool hdri = (scene.mLayers & L::HDRI) != 0;
                    LLGLSLShader& program = hdri ? mOldHdri[emissive] : mOldSky[emissive];
                    program.bind();
                    bindDome(program, sky, mRainbow, mHaloMap);
                    if (hdri)
                    {
                        bindMap(program, LLShaderMgr::ENVIRONMENT_MAP, mHdri, ALSamplers::TrilinearClamp);
                        program.uniform1f(LLShaderMgr::SKY_HDR_SCALE, HDRI_SCALE);
                        LLMatrix3 rot;
                        rot.setRot(0.f, HDRI_ROTATION * DEG_TO_RAD, 0.f);
                        program.uniformMatrix3fv(LLShaderMgr::DEFERRED_ENV_MAT, 1, GL_FALSE, (F32*)rot.mMatrix);
                        program.uniform1f(LLShaderMgr::HDRI_SPLIT_SCREEN, scene.mHdriSplit);
                    }
                    drawDome(mesh, mode);
                    program.unbind();
                }

                if (scene.mLayers & L::STARS)
                {
                    drawStars(scene, emissive, target);
                }

                LLGLEnable blend(GL_BLEND);
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
                if (scene.mLayers & L::BLOOM)
                {
                    LLPointer<LLVertexBuffer> quad = bodyQuad(ALSkyLayers::bloomCorners(sunCorners(scene)));
                    LLGLSLShader& program = mSunHalo[emissive];
                    program.bind();
                    bindMap(program, LLShaderMgr::DIFFUSE_MAP, mBloomTexture, ALSamplers::AnisoClamp);
                    program.uniform4f(LLShaderMgr::DIFFUSE_COLOR, BLOOM_COLOR.mV[VRED], BLOOM_COLOR.mV[VGREEN],
                                      BLOOM_COLOR.mV[VBLUE], BLOOM_OPACITY);
                    drawQuad(quad);
                    program.unbind();
                }
                if (scene.mLayers & L::SUN)
                {
                    LLPointer<LLVertexBuffer> quad = bodyQuad(sunCorners(scene));
                    LLGLSLShader& program = mOldSun[emissive];
                    program.bind();
                    bindMap(program, LLShaderMgr::DIFFUSE_MAP, mSunTexture, ALSamplers::AnisoClamp);
                    program.uniform1f(LLShaderMgr::BLEND_FACTOR, 0.f);
                    drawQuad(quad);
                    program.unbind();
                }
                if (scene.mLayers & L::MOON)
                {
                    LLPointer<LLVertexBuffer> quad = bodyQuad(moonCorners(scene));
                    LLGLSLShader& program = mOldMoon[emissive];
                    program.bind();
                    bindMap(program, LLShaderMgr::DIFFUSE_MAP, mMoonTexture, ALSamplers::AnisoClamp);
                    program.uniform1f(LLShaderMgr::MOON_BRIGHTNESS, MOON_BRIGHTNESS);
                    const LLVector3 moon_dir = agentDirection(scene.mMoonElevation, scene.mMoonBearing);
                    program.uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, moon_dir.mV);
                    drawQuad(quad);
                    program.unbind();
                }

                gGL.setSceneBlendType(LLRender::BT_ADD);
                if (scene.mLayers & L::METEORS)
                {
                    LLPointer<LLVertexBuffer> quads = meteorQuads(meteors());
                    LLGLSLShader& program = mOldMeteors[emissive];
                    program.bind();
                    program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)W, (F32)H);
                    glUniform1f(glGetUniformLocation(program.mProgramObject, "meteor_width_pixels"),
                                ALSkyLayers::METEOR_WIDTH_PIXELS);
                    gGL.pushMatrix();
                    gGL.translatef(CAMERA.mV[VX], CAMERA.mV[VY], CAMERA.mV[VZ]);
                    quads->setBuffer();
                    quads->drawArrays(LLRender::TRIANGLES, 0, quads->getNumVerts());
                    gGL.popMatrix();
                    program.unbind();
                }
                if (scene.mLayers & L::AURORA)
                {
                    LLGLSLShader& program = mOldAurora[emissive];
                    program.bind();
                    bindDome(program, sky, mRainbow, mHaloMap);
                    program.uniform1f(LLShaderMgr::AURORA_INTENSITY, scene.mAurora);
                    program.uniform1f(LLShaderMgr::AURORA_TIME, AURORA_TIME);
                    drawDome(mesh, mode);
                    program.unbind();
                }

                gGL.setSceneBlendType(LLRender::BT_ALPHA);
                if (scene.mLayers & L::CLOUDS)
                {
                    LLGLSLShader& program = mOldClouds[emissive];
                    program.bind();
                    bindDome(program, sky, mRainbow, mHaloMap);
                    bindMap(program, LLShaderMgr::CLOUD_NOISE_MAP, mCloudNoise, ALSamplers::AnisoWrap);
                    if (scene.mCloudBlend > 0.f)
                    {
                        bindMap(program, LLShaderMgr::CLOUD_NOISE_MAP_NEXT, mCloudNoiseNext, ALSamplers::AnisoWrap);
                    }
                    program.uniform1f(LLShaderMgr::BLEND_FACTOR, scene.mCloudBlend);
                    drawDome(mesh, mode);
                    program.unbind();
                }
            }
            target.flush();

            // The resolve, reading the G-buffer as bindDeferredShader binds it: the albedo decoded on the sampler.
            mScreen.bindTarget();
            LLGLSLShader& resolve = mResolve[emissive][stored ? 0 : 1];
            resolve.bind();
            resolve.uniform1f(LLShaderMgr::SKY_HDR_SCALE, SKY_HDR_SCALE);
            const S32 albedo = resolve.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE);
            if (albedo > -1)
            {
                target.bindTexture(0, albedo, ALSamplers::PointClamp | ALSampler::SRGBDecode);
            }
            const S32 normal = resolve.enableTexture(LLShaderMgr::NORMAL_MAP);
            if (normal > -1)
            {
                target.bindTexture(2, normal, ALSamplers::PointClamp);
            }
            const S32 emissive_channel = resolve.enableTexture(LLShaderMgr::DEFERRED_EMISSIVE);
            if (emissive_channel > -1)
            {
                target.bindTexture(3, emissive_channel, ALSamplers::PointClamp);
            }
            Picture picture = drawScreen();
            resolve.unbind();
            mScreen.flush();
            return picture;
        }

        // The sky as the composite draws it: the stars into their attachment of @a emissive's G-buffer, cleared to
        // black (unless mClearStars says otherwise), then every layer along each pixel's view ray in one pass over it,
        // into @a screen (the float screen where it is null), by @a composite (the sky's pass where it is null).
        Picture drawComposite(const Scene& scene, bool emissive, LLRenderTarget* screen = nullptr,
                              LLGLSLShader* composite = nullptr)
        {
            LLRenderTarget& out = screen ? *screen : mScreen;
            const Sky& sky = *scene.mSky;
            LLRenderTarget& target = mGBuffer[emissive][0];
            target.bindTarget();
            const F32 magenta[4] = { 1.f, 0.f, 1.f, 1.f };
            const F32 black[4]   = { 0.f, 0.f, 0.f, 0.f };
            for (S32 attachment = 0; attachment < 4; ++attachment)
            {
                const bool stars = mClearStars && attachment == (S32)ALSkyLayers::starAttachment(emissive);
                glClearBufferfv(GL_COLOR, attachment, stars ? black : magenta);
            }
            loadView(scene.mView, W, H);
            const std::array<F32, 9> eye_to_sky = ALFarPlane::eyeToSkyFrame(gGL.getModelviewMatrix());
            {
                LLGLDisable   cull(GL_CULL_FACE);
                LLGLDisable   srgb(GL_FRAMEBUFFER_SRGB);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                if (scene.mLayers & L::STARS)
                {
                    drawStars(scene, emissive, target);
                }
            }
            target.flush();

            // The meteors' streaks through the matrices that drew the old ones.
            gGL.pushMatrix();
            gGL.translatef(CAMERA.mV[VX], CAMERA.mV[VY], CAMERA.mV[VZ]);
            LLMatrix4a mvp;
            mvp.setMul(gGL.getModelviewMatrix(), gGL.getProjectionMatrix());
            gGL.popMatrix();
            std::vector<LLVector4> meteor_data;
            const U32 meteor_count = (scene.mLayers & L::METEORS)
                ? ALSkyLayers::packMeteors(meteors(), mvp, (F32)W, (F32)H, ALSkyLayers::METEOR_WIDTH_PIXELS, meteor_data)
                : 0;

            out.bindTarget();
            LLGLSLShader& program = composite ? *composite : mComposite[emissive];
            bindEnvironment(program, mEnvironment, sky, eye_to_sky);
            program.bind();
            bindDome(program, sky, mRainbow, mHaloMap);
            program.uniform1i(LLShaderMgr::SKY_LAYERS, scene.mLayers);
            program.uniform1f(LLShaderMgr::SKY_HDR_SCALE, SKY_HDR_SCALE);
            program.uniform1f(LLShaderMgr::SKY_CLASSIC_SCALE, mClassicScale);

            const S32 stars = program.enableTexture(emissive ? LLShaderMgr::DEFERRED_EMISSIVE : LLShaderMgr::DEFERRED_DIFFUSE);
            if (stars > -1)
            {
                target.bindTexture(ALSkyLayers::starAttachment(emissive), stars,
                                   emissive ? ALSamplers::PointClamp : ALSamplers::PointClamp | ALSampler::SRGBDecode);
            }

            if (scene.mLayers & L::HDRI)
            {
                bindMap(program, LLShaderMgr::SKY_HDRI_MAP, mHdri, ALSamplers::TrilinearClamp);
                LLMatrix3 rot;
                rot.setRot(0.f, HDRI_ROTATION * DEG_TO_RAD, 0.f);
                program.uniformMatrix3fv(LLShaderMgr::SKY_HDRI_ROTATION, 1, GL_FALSE, (F32*)rot.mMatrix);
                program.uniform1f(LLShaderMgr::SKY_HDRI_SCALE, HDRI_SCALE);
                program.uniform1f(LLShaderMgr::HDRI_SPLIT_SCREEN, scene.mHdriSplit);
            }

            const std::array<LLVector3, 3> sun =
                ALSkyLayers::rayQuad(sunCorners(scene), LLVector3(0.f, 0.f, -ALSkyLayers::SUN_DROP));
            bindMap(program, LLShaderMgr::SKY_SUN_MAP, mSunTexture, ALSamplers::AnisoClamp);
            program.uniform1f(LLShaderMgr::SKY_SUN_BLEND, 0.f);
            program.uniform3fv(LLShaderMgr::SKY_SUN_QUAD, 3, sun[0].mV);

            const std::array<LLVector3, 3> bloom = ALSkyLayers::rayQuad(ALSkyLayers::bloomCorners(sunCorners(scene)),
                                                                        LLVector3(0.f, 0.f, -ALSkyLayers::SUN_DROP));
            bindMap(program, LLShaderMgr::SKY_BLOOM_MAP, mBloomTexture, ALSamplers::AnisoClamp);
            program.uniform1f(LLShaderMgr::SKY_BLOOM_BLEND, 0.f);
            program.uniform3fv(LLShaderMgr::SKY_BLOOM_QUAD, 3, bloom[0].mV);
            program.uniform3fv(LLShaderMgr::SKY_BLOOM_COLOR, 1, BLOOM_COLOR.mV);

            const std::array<LLVector3, 3> moon = ALSkyLayers::rayQuad(moonCorners(scene));
            bindMap(program, LLShaderMgr::SKY_MOON_MAP, mMoonTexture, ALSamplers::AnisoClamp);
            program.uniform3fv(LLShaderMgr::SKY_MOON_QUAD, 3, moon[0].mV);
            program.uniform1f(LLShaderMgr::SKY_MOON_FADE,
                              ALSkyLayers::moonFade(agentDirection(scene.mMoonElevation, scene.mMoonBearing).mV[VZ]));
            program.uniform1f(LLShaderMgr::MOON_BRIGHTNESS, MOON_BRIGHTNESS);

            program.uniform4fv(LLShaderMgr::SKY_METEOR_DATA, (U32)meteor_data.size(),
                               meteor_data.empty() ? nullptr : meteor_data[0].mV);
            program.uniform1i(LLShaderMgr::SKY_METEOR_COUNT, (S32)meteor_count);

            program.uniform1f(LLShaderMgr::AURORA_INTENSITY, scene.mAurora);
            program.uniform1f(LLShaderMgr::AURORA_TIME, AURORA_TIME);

            bindMap(program, LLShaderMgr::CLOUD_NOISE_MAP, mCloudNoise, ALSamplers::AnisoWrap);
            bindMap(program, LLShaderMgr::CLOUD_NOISE_MAP_NEXT, mCloudNoiseNext, ALSamplers::AnisoWrap);
            program.uniform1f(LLShaderMgr::SKY_CLOUD_BLEND, scene.mCloudBlend);

            Picture picture = drawScreen();
            program.unbind();
            out.flush();
            return picture;
        }

        // The stars alone over black into @a emissive's G-buffer, float where @a stored is false: their attachment,
        // read back.
        Picture drawStarsAlone(const Scene& scene, bool emissive, bool stored)
        {
            LLRenderTarget& target = mGBuffer[emissive][stored ? 0 : 1];
            target.bindTarget();
            const F32 black[4] = { 0.f, 0.f, 0.f, 0.f };
            for (S32 attachment = 0; attachment < 4; ++attachment)
            {
                glClearBufferfv(GL_COLOR, attachment, black);
            }
            loadView(scene.mView, W, H);
            if (scene.mLayers & L::STARS)
            {
                LLGLDisable   cull(GL_CULL_FACE);
                LLGLDisable   srgb(GL_FRAMEBUFFER_SRGB);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                drawStars(scene, emissive, target);
            }
            Picture out = readAttachment(ALSkyLayers::starAttachment(emissive), W, H);
            target.flush();
            return out;
        }

        // The screen triangle over the bound screen target, read back.
        Picture drawScreen()
        {
            {
                LLGLDisable   blend(GL_BLEND);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            Picture out(size_t(W) * H * 4);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out.data());
            return out;
        }

        // How @a a and @a b differ, over the pixels @a in marks, display encoded but where the HDRI shows (its colour
        // linear): each channel allowed @a allowance, and @a steps of the path's storage at the larger value, and on
        // the emissive path what ditherEmissive moved the old value by.
        static Difference compare(const Picture& a, const Picture& b, const std::vector<bool>& in, bool emissive,
                                  F32 allowance, F32 steps, const std::vector<bool>& linear = {})
        {
            Difference d;
            for (size_t px = 0; px < size_t(W) * H; ++px)
            {
                if (!in[px])
                {
                    continue;
                }
                ++d.mPixels;
                const bool hdri = !linear.empty() && linear[px];
                bool beyond = false;
                for (S32 c = 0; c < 3; ++c)
                {
                    const F32 va = hdri ? a[px * 4 + c] : encode(a[px * 4 + c] / SKY_HDR_SCALE);
                    const F32 vb = hdri ? b[px * 4 + c] : encode(b[px * 4 + c] / SKY_HDR_SCALE);
                    const F32 diff = fabsf(va - vb);
                    const F32 larger = llmax(va, vb);
                    if (diff > d.mMost)
                    {
                        d.mMost = diff;
                        d.mWorstX = (S32)(px % W);
                        d.mWorstY = (S32)(px / W);
                    }
                    F32 bound = allowance + steps * storageStep(larger, emissive, c);
                    if (emissive)
                    {
                        bound += 2.f * ditherReach(c) * larger;
                    }
                    beyond = beyond || diff > bound;
                }
                d.mBeyond += beyond;
            }
            return d;
        }

        // Where the 8-bit albedo's picture lies past @a steps of 8 bits from the float one's, display encoded: where it
        // clamped a layer at 1, which the composite no longer does (D6).
        static std::vector<bool> clamped(const Picture& stored, const Picture& unstored, F32 steps)
        {
            std::vector<bool> out(size_t(W) * H, false);
            for (size_t px = 0; px < out.size(); ++px)
            {
                for (S32 c = 0; c < 3; ++c)
                {
                    const F32 a = encode(stored[px * 4 + c] / SKY_HDR_SCALE);
                    const F32 b = encode(unstored[px * 4 + c] / SKY_HDR_SCALE);
                    out[px] = out[px] || fabsf(a - b) > steps / 255.f;
                }
            }
            return out;
        }

        // The pixels whose view ray rises above the camera's level, those below it, and those the HDRI shows.
        static void regions(const Scene& scene, std::vector<bool>& above, std::vector<bool>& below,
                            std::vector<bool>& hdri)
        {
            loadView(scene.mView, W, H);
            const std::array<F32, 9> eye_to_sky = ALFarPlane::eyeToSkyFrame(gGL.getModelviewMatrix());
            above.assign(size_t(W) * H, false);
            below.assign(size_t(W) * H, false);
            hdri.assign(size_t(W) * H, false);
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    const size_t px = size_t(y) * W + x;
                    const bool up = domeRay(scene.mView, eye_to_sky, W, H, x, y).mV[VY] >= 0.f;
                    above[px] = up;
                    below[px] = !up;
                    const F32 ndc_x = ((F32)x + 0.5f) / W * 2.f - 1.f;
                    hdri[px] = (scene.mLayers & L::HDRI) && -ndc_x > (1.f - scene.mHdriSplit) * 2.f - 1.f;
                }
            }
        }


        static constexpr F32 HDRI_SCALE      = 4.f;
        static constexpr F32 HDRI_ROTATION   = 30.f;
        static constexpr F32 MOON_BRIGHTNESS = 1.2f;
        static constexpr F32 AURORA_TIME     = 1234.5f;
        inline static const LLVector3 BLOOM_COLOR = LLVector3(1.f, 0.82f, 0.6f);

        SkyShaderTree          mTree;
        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mOldSky[2];
        LLGLSLShader mOldHdri[2];
        LLGLSLShader mOldSun[2];
        LLGLSLShader mOldMoon[2];
        LLGLSLShader mOldMeteors[2];
        LLGLSLShader mOldAurora[2];
        LLGLSLShader mOldClouds[2];
        LLGLSLShader mSunHalo[2];
        LLGLSLShader mStarPoints[2];
        LLGLSLShader mStarQuads[2];
        LLGLSLShader mResolve[2][2];
        LLGLSLShader mComposite[2];
        LLGLSLShader mInOrder[2];
        LLGLSLShader mCloudsAlone[2];
        LLRenderTarget mGBuffer[2][2];
        LLRenderTarget mScreen;
        LLPointer<LLVertexBuffer> mTriangle;
        LLPointer<LLVertexBuffer> mDome;
        LLPointer<LLVertexBuffer> mSphere;
        LLPointer<LLVertexBuffer> mStarBuffer;
        LLPointer<LLImageGL> mRainbow;
        LLPointer<LLImageGL> mHaloMap;
        LLPointer<LLImageGL> mSunTexture;
        LLPointer<LLImageGL> mMoonTexture;
        LLPointer<LLImageGL> mBloomTexture;
        LLPointer<LLImageGL> mCloudNoise;
        LLPointer<LLImageGL> mCloudNoiseNext;
        LLPointer<LLImageGL> mHdri;
        std::vector<ALStarField::Star> mField;
        ALStarField::Layout mLayout;

        GLuint mEnvironment = 0;

        // Whether drawComposite clears the stars' attachment to black, as a view that draws the stars does, or leaves
        // it the G-buffer's clear, as one that does not; and the classic sky's gain the sky's pass is given.
        bool mClearStars   = true;
        F32  mClassicScale = 1.f;
    };

    // How far the composite may lie from the old sky drawn over the sphere, display encoded, with nothing stored: the
    // filter's and the layers' arithmetic, and a star whose blends now round in a new order.
    constexpr F32 MATHS = 2.f / 255.f;

    // How far the old sky over the dome's own mesh lies from it over the sphere, display encoded: above the camera's
    // level its facets, which move the clouds' detail noise by texels near the horizon (28/255 at most); below it its
    // skirt (D7), next to nothing for the haze (4/255 at most) but a rainbow's or halo's band shifted bodily (96/255
    // with the test's bands, which are bright over the whole map).
    constexpr F32 FACETS      = 40.f / 255.f;
    constexpr F32 SKIRT       = 6.f / 255.f;
    constexpr F32 SKIRT_BANDS = 0.45f;

    // The largest finite half float, and the most the sky's pass scales the sky by (sky_classic_scale under a classic
    // sky).
    constexpr F32 HALF_MAX      = 65504.f;
    constexpr F32 CLASSIC_SCALE = 1.1f;

    // An HDRI holding @a value in every colour channel of every texel, as a float map.
    LLPointer<LLImageGL> uniformHdri(F32 value)
    {
        constexpr S32 SIZE = 4;
        std::vector<F32> texels(size_t(SIZE) * SIZE * 4, value);
        for (size_t alpha = 3; alpha < texels.size(); alpha += 4)
        {
            texels[alpha] = 1.f;
        }
        LLPointer<LLImageGL> map = new LLImageGL(SIZE, SIZE, 4, false);
        map->setNeedsAlphaAndPickMask(false);
        map->setExplicitFormat(GL_RGBA32F, GL_RGBA, GL_FLOAT);
        UNSCOPED_INFO("the HDRI map");
        REQUIRE(map->createGLTexture(0, reinterpret_cast<const U8*>(texels.data())));
        return map;
    }
}

TEST_CASE_METHOD(alskycomposite_gl_data, "alskycomposite_gl: the composite is the old sky but where it departs on purpose",
                 "[alskycomposite_gl]")
{
    setUp();
    S32 clamped_somewhere = 0;
    for (const Scene& scene : SCENES)
    {
        std::vector<bool> above;
        std::vector<bool> below;
        std::vector<bool> hdri;
        regions(scene, above, below, hdri);

        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            CAPTURE(scene.mName, emissive);

            const Picture composite   = drawComposite(scene, emissive);
            const Picture sphere      = drawOld(scene, emissive, false, mSphere);
            const Picture sphere_8bit = drawOld(scene, emissive, true, mSphere);
            const Picture dome        = drawOld(scene, emissive, true, mDome);

            // The maths: the composite against the old programs over the sphere, nothing stored. The stars are in
            // their attachment both ways, so their storage counts here.
            std::vector<bool> unclamped_hdri(size_t(W) * H, true);

            // The storage: against the same in the path's own attachments, where the 8-bit albedo clamped a layer or
            // an HDRI at 1 (D6) and rounded every layer; R11G11B10F only rounds.
            // On the albedo path the old program clamped an HDRI to 1 itself, to fit the attachment.
            std::vector<bool> clamps = emissive ? std::vector<bool>(size_t(W) * H, false)
                                                : clamped(sphere_8bit, sphere, 2.f);
            for (size_t px = 0; !emissive && px < clamps.size(); ++px)
            {
                for (S32 c = 0; c < 3; ++c)
                {
                    clamps[px] = clamps[px] || (hdri[px] && composite[px * 4 + c] > 1.f);
                }
            }
            // The composite reads the stars from their attachment in the path's own storage, so the maths leaves out
            // the pixels a star lit; the storage takes them in. A float attachment keeps the faint stars that each
            // add under half a step and round away, one by one, in 8 bits: the old albedo path lost them too.
            const Picture stars = drawStarsAlone(scene, emissive, false);
            std::vector<bool> unclamped(size_t(W) * H);
            S32 starlit = 0;
            for (size_t px = 0; px < unclamped.size(); ++px)
            {
                const bool star = stars[px * 4] > 0.f || stars[px * 4 + 1] > 0.f || stars[px * 4 + 2] > 0.f;
                starlit += star;
                unclamped[px] = !clamps[px];
                unclamped_hdri[px] = !(clamps[px] && hdri[px]) && !star;
            }
            const Difference maths = compare(composite, sphere, unclamped_hdri, emissive, MATHS, 1.f, hdri);
            const S32 clamp_count = (S32)std::count(clamps.begin(), clamps.end(), true);
            // The old sky stored the dome and then each layer over what was stored. 8 bits round each store to the
            // nearest step; R11G11B10F may round it toward zero, as GL allows and llvmpipe does, so there the old sky
            // may lie up to a step under the composite for every store.
            const S32 stores = 1 + std::popcount((U32)(scene.mLayers & ~L::HDRI));
            const Difference stored = compare(composite, sphere_8bit, unclamped, emissive, MATHS,
                                              emissive ? (F32)stores : 2.f, hdri);
            const Difference clamp_diff = compare(composite, sphere_8bit, clamps, emissive, 0.f, 0.f, hdri);

            // The dome's mesh: above the camera's level its facets, below it its skirt (D7).
            std::vector<bool> above_wl(above.size());
            std::vector<bool> below_wl(below.size());
            for (size_t px = 0; px < above.size(); ++px)
            {
                above_wl[px] = above[px] && !hdri[px] && !clamps[px];
                below_wl[px] = below[px] && !hdri[px] && !clamps[px];
            }
            const Difference facets = compare(dome, sphere_8bit, above_wl, emissive, 0.f, 0.f, hdri);
            const Difference skirt  = compare(dome, sphere_8bit, below_wl, emissive, 0.f, 0.f, hdri);

            const std::string maths_summary  = maths.summary();
            const std::string stored_summary = stored.summary();
            const std::string clamp_summary  = clamp_diff.summary();
            const std::string facets_summary = facets.summary();
            const std::string skirt_summary  = skirt.summary();
            CAPTURE(maths_summary, stored_summary, clamp_count, clamp_summary, facets_summary, skirt_summary, starlit);
            CHECK(maths.mBeyond == 0);
            CHECK(stored.mBeyond == 0);
            if (emissive)
            {
                CHECK(clamp_count == 0);
            }
            CHECK(facets.mMost < FACETS);
            const bool bands = scene.mSky->mMoisture > 0.f || scene.mSky->mIce > 0.f;
            CHECK(skirt.mMost < (bands ? SKIRT_BANDS : SKIRT));
            clamped_somewhere += clamp_count;
        }
    }
    UNSCOPED_INFO("the 8-bit albedo clamped some layer past 1");
    CHECK(clamped_somewhere > 0);
}

TEST_CASE_METHOD(alskycomposite_gl_data,
                 "alskycomposite_gl: under an opaque cloud deck the composite is the cloud and elsewhere every layer in order",
                 "[alskycomposite_gl]")
{
    setUp();
    buildReferences();
    const S32 every_layer = L::STARS | L::BLOOM | L::SUN | L::MOON | L::METEORS | L::AURORA | L::CLOUDS;
    const Scene deck = { "an opaque cloud deck over the bodies and the night's layers", &CLOUD_DECK_SKY,
                         { 140.f, 25.f, 80.f }, every_layer, 15.f, 115.f, 35.f, 160.f, 0.5f, 0.6f, 0.2f, 0.f };
    Scene under = deck;
    under.mLayers = every_layer & ~L::CLOUDS;
    Scene dome = deck;
    dome.mLayers = 0;

    for (S32 path = 0; path < 2; ++path)
    {
        const bool emissive = path == 1;
        CAPTURE(emissive);
        const Picture composite = drawComposite(deck, emissive);
        const Picture in_order  = drawComposite(deck, emissive, nullptr, &mInOrder[emissive]);
        const Picture clouds    = drawComposite(deck, emissive, nullptr, &mCloudsAlone[emissive]);
        const Picture layers    = drawComposite(under, emissive);
        const Picture dome_only = drawComposite(dome, emissive);

        // Where the deck is opaque the composite is the cloud, and elsewhere the layers in order, both but for rounding:
        // GLSL holds no two programs to round the same expressions alike, and llvmpipe's compiler leaves the
        // composite's clouds up to a millionth of their value from the reference's (NVIDIA's, none). A layer the
        // composite dropped or laid out of order would move a pixel by far more.
        constexpr F32 ROUNDING = 1e-5f;
        S32 opaque = 0;
        S32 through = 0;
        S32 hidden = 0;
        S32 opaque_differ = 0;
        S32 through_differ = 0;
        F32 most = 0.f;
        for (size_t px = 0; px < size_t(W) * H; ++px)
        {
            const bool solid = clouds[px * 4 + 3] >= 1.f;
            opaque += solid;
            through += !solid;
            bool shows = false;
            for (S32 c = 0; c < 3; ++c)
            {
                const F32 v = composite[px * 4 + c];
                const F32 expected = solid ? clouds[px * 4 + c] : in_order[px * 4 + c];
                const F32 off = fabsf(v - expected) / llmax(fabsf(expected), 1e-3f);
                (solid ? opaque_differ : through_differ) += off > ROUNDING;
                most = llmax(most, off);
                shows = shows || layers[px * 4 + c] != dome_only[px * 4 + c];
            }
            hidden += solid && shows;
        }
        CAPTURE(opaque, through, hidden, most);
        UNSCOPED_INFO("the deck is opaque over some of the sky and lets the rest through");
        CHECK(opaque > 0);
        CHECK(through > 0);
        UNSCOPED_INFO("the deck hides layers that show without it");
        CHECK(hidden > 0);
        CHECK(opaque_differ == 0);
        CHECK(through_differ == 0);
    }
}

TEST_CASE_METHOD(alskycomposite_gl_data,
                 "alskycomposite_gl: an HDRI past the half float range or below zero composites finite and not negative",
                 "[alskycomposite_gl]")
{
    setUp();
    // The screen as the viewer allocates it under HDR, as the probes' faces are: half floats.
    LLRenderTarget half_float;
    REQUIRE(half_float.allocate(W, H, GL_RGBA16F));
    const Scene scene = { "HDRI over the whole screen", &SKIES[0], { 30.f, 15.f, 70.f }, L::HDRI, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 1.f };
    mClassicScale = CLASSIC_SCALE;
    for (F32 texel : { 1.0e6f, -1.f })
    {
        mHdri = uniformHdri(texel);
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            CAPTURE(texel, emissive);
            const Picture picture = drawComposite(scene, emissive, &half_float);
            S32 unbounded = 0;
            F32 least = 0.f;
            F32 most = 0.f;
            for (size_t px = 0; px < size_t(W) * H; ++px)
            {
                for (S32 c = 0; c < 3; ++c)
                {
                    const F32 v = picture[px * 4 + c];
                    least = llmin(least, v);
                    most = llmax(most, v);
                    unbounded += !std::isfinite(v) || v < 0.f || v > HALF_MAX;
                }
            }
            CAPTURE(least, most);
            CHECK(unbounded == 0);
        }
    }
    half_float.release();
}

TEST_CASE_METHOD(alskycomposite_gl_data,
                 "alskycomposite_gl: an HDRI composites as bright as R11G11B10F holds and the half float range keeps",
                 "[alskycomposite_gl]")
{
    // An HDRI's texel, after its exposure (HDRI_SCALE), is drawn as it is up to SKY_HDRI_MAX, 65024, and the pass's
    // output, the classic gain on it, is held to the half float's 65504. Half floats step by 32 there.
    setUp();
    LLRenderTarget half_float;
    REQUIRE(half_float.allocate(W, H, GL_RGBA16F));
    const Scene scene = { "HDRI over the whole screen", &SKIES[0], { 30.f, 15.f, 70.f }, L::HDRI, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 1.f };
    struct Case
    {
        F32 mTexel;
        F32 mClassic;
        F32 mExpected;
    };
    const Case cases[] = { { 15000.f, 1.f, 15000.f * HDRI_SCALE }, { 1.0e6f, 1.f, 65024.f },
                           { 1.0e6f, CLASSIC_SCALE, HALF_MAX } };
    for (const Case& c : cases)
    {
        mHdri = uniformHdri(c.mTexel);
        mClassicScale = c.mClassic;
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            CAPTURE(c.mTexel, c.mClassic, emissive);
            const Picture picture = drawComposite(scene, emissive, &half_float);
            F32 least = HALF_MAX;
            F32 most  = 0.f;
            for (size_t px = 0; px < size_t(W) * H; ++px)
            {
                for (S32 ch = 0; ch < 3; ++ch)
                {
                    least = llmin(least, picture[px * 4 + ch]);
                    most  = llmax(most, picture[px * 4 + ch]);
                }
            }
            CAPTURE(least, most);
            CHECK(most <= c.mExpected + 32.f);
            CHECK(least >= c.mExpected - 32.f);
        }
    }
    mClassicScale = 1.f;
    half_float.release();
}

TEST_CASE_METHOD(alskycomposite_gl_data,
                 "alskycomposite_gl: under a classic sky the sky's pass gains as the soften pass does",
                 "[alskycomposite_gl]")
{
    // The soften pass's classic variant scales what it lights by 1.1 (softenLightF.glsl's writeColor), and the sky's
    // pass takes the same gain as a uniform; both leave the glow in the alpha at nothing.
    setUp();
    for (const Scene& scene : SCENES)
    {
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            CAPTURE(scene.mName, emissive);
            const Picture modern = drawComposite(scene, emissive);
            mClassicScale = CLASSIC_SCALE;
            const Picture classic = drawComposite(scene, emissive);
            mClassicScale = 1.f;
            F32 most = 0.f;
            S32 alpha = 0;
            for (size_t px = 0; px < size_t(W) * H; ++px)
            {
                for (S32 c = 0; c < 3; ++c)
                {
                    const F32 expected = modern[px * 4 + c] * CLASSIC_SCALE;
                    most = llmax(most, fabsf(classic[px * 4 + c] - expected) / llmax(expected, 1e-3f));
                }
                alpha += modern[px * 4 + 3] != 0.f || classic[px * 4 + 3] != 0.f;
            }
            CAPTURE(most);
            CHECK(most < 1e-5f);
            CHECK(alpha == 0);
        }
    }
}

TEST_CASE_METHOD(alskycomposite_gl_data, "alskycomposite_gl: a view without the stars reads nothing of their attachment",
                 "[alskycomposite_gl]")
{
    // A view whose layers leave the stars out has no need to clear their attachment, which then keeps the G-buffer's
    // magenta clear: the composite must draw the same over it as over black.
    setUp();
    S32 scenes = 0;
    for (const Scene& scene : SCENES)
    {
        if (scene.mLayers & L::STARS)
        {
            continue;
        }
        ++scenes;
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            CAPTURE(scene.mName, emissive);
            const Picture cleared = drawComposite(scene, emissive);
            mClearStars = false;
            const Picture uncleared = drawComposite(scene, emissive);
            mClearStars = true;
            S32 differ = 0;
            for (size_t px = 0; px < size_t(W) * H; ++px)
            {
                for (S32 c = 0; c < 3; ++c)
                {
                    differ += cleared[px * 4 + c] != uncleared[px * 4 + c];
                }
            }
            CHECK(differ == 0);
        }
    }
    CHECK(scenes > 0);
}
