/**
 * @file alskylayers_gl_test.cpp
 * @brief The sky's layers two ways on the hidden window: as their programs drew them into the G-buffer before the
 *        composite (tests/skyraster), and as the composite works each out along the pixel's view ray. Each layer is
 *        laid over the same background both ways and the pictures compared: against the old programs drawing into a
 *        float target, where only the maths can differ, and into the attachment the path stored the sky in, where
 *        that attachment's storage differs too.
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

#include <fmt/format.h>

namespace
{
    using namespace al_sky_test;

    constexpr S32 W = 256;
    constexpr S32 H = 192;

    // The layer the test program lays over its background (kLayerTest's test_layer).
    enum TestLayer : S32
    {
        TEST_SUN   = 1,
        TEST_MOON  = 2,
        TEST_BLOOM = 3,
        TEST_CLOUDS = 4,
    };

    // The screen triangle the layers are drawn over.
    const char* kLayerTestVertex = R"(in vec3 position;

out vec2 vary_fragcoord;

void main()
{
    gl_Position = vec4(position.xy, 0.0, 1.0);
    vary_fragcoord = position.xy * 0.5 + 0.5;
}
)";

    // One layer along each pixel's view ray over a background, through the per-ray functions the composite calls.
    const char* kLayerTest = R"(/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

in vec2 vary_fragcoord;

//[ENGINE_BLOCK Matrices]
//[ENGINE_BLOCK Environment]

uniform int  test_layer;
uniform vec3 test_background;

vec4 skySun(vec3 dir);
vec4 skyMoon(vec3 dir);
vec4 skyBloom(vec3 dir);
vec4 skyClouds(vec3 dir);

void main()
{
    vec4 eye = inv_proj * vec4(vary_fragcoord * 2.0 - 1.0, 0.5, 1.0);
    vec3 dir = normalize(eyeToSky * (eye.xyz / eye.w));
    vec3 color = test_background;
    if (test_layer == 1)
    {
        vec4 layer = skySun(dir);
        color = mix(color, layer.rgb, layer.a);
    }
    else if (test_layer == 2)
    {
        vec4 layer = skyMoon(dir);
        color = mix(color, layer.rgb, layer.a);
    }
    else if (test_layer == 3)
    {
        vec4 layer = skyBloom(dir);
        color = mix(color, layer.rgb, layer.a);
    }
    else if (test_layer == 4)
    {
        vec4 layer = skyClouds(dir);
        color = mix(color, layer.rgb, layer.a);
    }
    frag_color = vec4(color, 1.0);
}
)";

    // The sun's halo as it was drawn before WindLight (LLDrawPoolSky::renderSunHalo at the open-source import), which
    // no shader ever drew: its square about the sun, textured and tinted by the sun's colour at the halo's opacity,
    // alpha blended. Its texture pair is blended as the sun's is, and the square lowered with the sun disc, as the
    // composite lays the bloom.
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
uniform sampler2D altDiffuseMap;
uniform float blend_factor;
uniform vec4 color;

in vec2 vary_texcoord0;

void main()
{
    vec4 c = mix(texture(diffuseMap, vary_texcoord0), texture(altDiffuseMap, vary_texcoord0), blend_factor) * color;
    frag_data[0] = vec4(0.0);
    frag_data[1] = vec4(0.0);
    frag_data[2] = vec4(0.0);
#if defined(HAS_EMISSIVE)
    frag_data[3] = c;
#else
    frag_data[0] = c;
#endif
}
)";

    // The background a layer is laid over, display encoded, which each path's attachment holds exactly: 8 bits'
    // steps for the albedo, six- and five-bit mantissas for the emissive buffer.
    const F32 BACKGROUND[2][4] = { { 64.f / 255.f, 96.f / 255.f, 128.f / 255.f, 1.f }, { 0.25f, 0.375f, 0.5f, 1.f } };

    // A sun or moon: its elevation and bearing, its scale, the view onto it, how far its texture pair has blended (the
    // sun's), and its brightness (the moon's).
    struct BodyCase
    {
        const char* mName;
        F32  mElevation;
        F32  mBearing;
        F32  mScale;
        View mView;
        F32  mBlend;
        F32  mBrightness;
    };

    // At midday, on the horizon where the quad is widest, at the zenith where it takes its own axes, and large in a wide
    // view, blending toward the next texture in two of them.
    const BodyCase SUNS[] = {
        { "sun at midday", 60.f, 80.f, 1.f, { 80.f, 57.f, 30.f }, 0.f, 1.f },
        { "sun on the horizon", 3.f, 260.f, 1.f, { 255.f, 2.f, 30.f }, 0.f, 1.f },
        { "sun overhead, blending", 89.6f, 30.f, 1.5f, { 30.f, 80.f, 40.f }, 0.4f, 1.f },
        { "large sun, wide view, blending", 35.f, 120.f, 3.f, { 140.f, 25.f, 80.f }, 0.7f, 1.f },
    };

    // Low, where it fades, high, and brighter than its texture, which an 8-bit attachment clamps.
    const BodyCase MOONS[] = {
        { "moon low", 12.f, 140.f, 1.f, { 145.f, 10.f, 30.f }, 0.f, 1.f },
        { "moon high, large", 50.f, 300.f, 2.f, { 300.f, 48.f, 40.f }, 0.f, 0.8f },
        { "moon bright", 30.f, 200.f, 1.5f, { 205.f, 28.f, 35.f }, 0.f, 1.6f },
    };

    // Over the sun at midday and on the horizon, blending toward the next pair, and about a large sun.
    const BodyCase BLOOMS[] = {
        { "bloom at midday", 60.f, 80.f, 1.f, { 80.f, 57.f, 50.f }, 0.f, 1.f },
        { "bloom on the horizon, blending", 3.f, 260.f, 1.f, { 255.f, 2.f, 50.f }, 0.5f, 1.f },
        { "bloom about a large sun", 35.f, 120.f, 2.5f, { 140.f, 25.f, 90.f }, 0.f, 1.f },
    };

    // The sun's colour the bloom is tinted by, held to 1 as the fixed-function halo's vertex colour was.
    const LLVector3 BLOOM_COLOR(1.f, 0.82f, 0.6f);

    // Clouds: the sky they are lit by, a view, and how far their noise has blended toward the next.
    struct CloudCase
    {
        const char* mName;
        S32  mSky;
        View mView;
        F32  mBlend;
    };

    // Toward the sun and overhead at midday, the horizon at sunset, where the cap meets the sky below the camera's
    // level, and up at night; the noise blending in two.
    const CloudCase CLOUDS[] = {
        { "midday, toward the sun", 0, { 80.f, 30.f, 70.f }, 0.f },
        { "midday, overhead", 0, { 200.f, 75.f, 60.f }, 0.f },
        { "sunset, at the horizon, blending", 1, { 250.f, 3.f, 60.f }, 0.35f },
        { "night, looking up, blending", 2, { 100.f, 40.f, 80.f }, 0.6f },
    };

    // How a layer drawn two ways differs, over the colour channels: the most, the pixels past the bound compare() is
    // given, and the pixels the layer reached in either.
    struct Difference
    {
        F32 mMost   = 0.f;
        S32 mBeyond = 0;
        S32 mLit    = 0;

        std::string summary() const
        {
            return fmt::format("most {:.6f} ({:.2f}/255), {} of {} lit pixels past the bound", mMost, mMost * 255.f,
                               mBeyond, mLit);
        }
    };

    struct alskylayers_gl_data
    {
        alskylayers_gl_data()
        : mShaders(mTree.prefix())
        {
        }

        ~alskylayers_gl_data()
        {
            LLGLSLShader::unbind();
            for (S32 path = 0; path < 2; ++path)
            {
                mOldSun[path].unload();
                mOldMoon[path].unload();
                mHalo[path].unload();
                mOldClouds[path].unload();
                mLayers[path].unload();
                for (LLRenderTarget& target : mGBuffer[path])
                {
                    target.release();
                }
            }
            mShaders.clearShaderObjects();
            mFloat.release();
            mTriangle = nullptr;
            mSunTexture = nullptr;
            mSunTextureNext = nullptr;
            mMoonTexture = nullptr;
            mBloomTexture = nullptr;
            mBloomTextureNext = nullptr;
            mCloudNoise = nullptr;
            mCloudNoiseNext = nullptr;
            mDome = nullptr;
            mFineDome = nullptr;
            if (mEnvironment)
            {
                glDeleteBuffers(1, &mEnvironment);
            }
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
        }

        // Every program both ways for both paths, the maps, and the targets: a G-buffer for each path whose colour
        // attachments are as the path stored the sky (the albedo 8 bits, the emissive buffer R11G11B10F) or float,
        // and a float target for the per-ray pictures.
        void setUp()
        {
            mTree.populate();
            REQUIRE(mTree.write("skytest/skyLayerTestV.glsl", kLayerTestVertex));
            REQUIRE(mTree.write("skytest/skyLayerTestF.glsl", kLayerTest));
            REQUIRE(mTree.write("skytest/sunHaloV.glsl", kHaloVertex));
            REQUIRE(mTree.write("skytest/sunHaloF.glsl", kHaloFragment));
            loadSharedObjects(mShaders);

            for (S32 path = 0; path < 2; ++path)
            {
                const bool emissive = path == 1;
                buildProgram(mOldSun[path], "Deferred Windlight Sun Program (before the composite)",
                             { { "skyraster/deferred/sunDiscV.glsl", GL_VERTEX_SHADER },
                               { "skyraster/deferred/sunDiscF.glsl", GL_FRAGMENT_SHADER } },
                             emissive);
                buildProgram(mOldMoon[path], "Deferred Windlight Moon Program (before the composite)",
                             { { "skyraster/deferred/moonV.glsl", GL_VERTEX_SHADER },
                               { "skyraster/deferred/moonF.glsl", GL_FRAGMENT_SHADER } },
                             emissive);
                buildProgram(mHalo[path], "The sun's halo before WindLight (test)",
                             { { "skytest/sunHaloV.glsl", GL_VERTEX_SHADER },
                               { "skytest/sunHaloF.glsl", GL_FRAGMENT_SHADER } },
                             emissive);
                buildProgram(mOldClouds[path], "Deferred Windlight Cloud Program (before the composite)",
                             { { "skyraster/deferred/cloudsV.glsl", GL_VERTEX_SHADER },
                               { "skyraster/deferred/cloudsF.glsl", GL_FRAGMENT_SHADER } },
                             emissive);
                buildProgram(mLayers[path], "The sky's layers along the view ray (test)",
                             { { "skytest/skyLayerTestV.glsl", GL_VERTEX_SHADER },
                               { "skytest/skyLayerTestF.glsl", GL_FRAGMENT_SHADER },
                               { "deferred/skyBodiesF.glsl", GL_FRAGMENT_SHADER },
                               { "deferred/skyCloudsF.glsl", GL_FRAGMENT_SHADER },
                               { "windlight/windlightDomeF.glsl", GL_FRAGMENT_SHADER } },
                             emissive, { { "SUN_BLOOM", "1" } });

                for (S32 storage = 0; storage < 2; ++storage)
                {
                    const bool stored = storage == 0;
                    LLRenderTarget& target = mGBuffer[path][storage];
                    REQUIRE(target.allocate(W, H, !emissive && !stored ? GL_RGBA32F : GL_RGBA8));
                    REQUIRE(target.addColorAttachment(GL_RGBA8));
                    REQUIRE(target.addColorAttachment(GL_RGB10_A2));
                    REQUIRE(target.addColorAttachment(stored ? GL_R11F_G11F_B10F : GL_RGBA32F));
                }
            }

            REQUIRE(mFloat.allocate(W, H, GL_RGBA32F));
            mTriangle = screenTriangle();
            mSunTexture     = disc(1, 0.8f);
            mSunTextureNext = disc(5, 0.6f);
            mMoonTexture    = disc(3, 0.9f);
            mBloomTexture     = disc(7, 1.f);
            mBloomTextureNext = disc(9, 0.7f);
            mCloudNoise       = cloudNoise(11);
            mCloudNoiseNext   = cloudNoise(13);
            mDome             = domeMesh();
            mFineDome         = fineSphere();
            glGenBuffers(1, &mEnvironment);
        }

        // The attachment the path keeps the sky's colour in: the emissive buffer's, or the albedo's.
        static S32 skyAttachment(bool emissive) { return emissive ? 3 : 0; }

        // A layer drawn by its old program, @a draw, over the background into @a emissive's G-buffer, the attachments
        // as the path stored them where @a stored and float otherwise. Its colour attachment, read back.
        template <typename Draw>
        std::vector<F32> drawOld(const View& view, bool emissive, bool stored, Draw&& draw)
        {
            LLRenderTarget& target = mGBuffer[emissive][stored ? 0 : 1];
            target.bindTarget();
            const F32 none[4] = { 0.f, 0.f, 0.f, 1.f };
            for (S32 attachment = 0; attachment < 4; ++attachment)
            {
                glClearBufferfv(GL_COLOR, attachment, attachment == skyAttachment(emissive) ? BACKGROUND[emissive] : none);
            }
            loadView(view, W, H);
            {
                LLGLEnable    blend(GL_BLEND);
                LLGLDisable   cull(GL_CULL_FACE);
                LLGLDisable   srgb(GL_FRAMEBUFFER_SRGB);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                draw();
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
            }
            std::vector<F32> out = readAttachment(skyAttachment(emissive), W, H);
            target.flush();
            return out;
        }

        // A layer along each pixel's view ray over the background, for @a emissive's path, @a bind setting its
        // uniforms. Read back.
        template <typename Bind>
        std::vector<F32> drawLayer(const View& view, bool emissive, TestLayer layer, Bind&& bind, const Sky& sky = SKIES[0])
        {
            mFloat.bindTarget();
            loadView(view, W, H);
            LLGLSLShader& program = mLayers[emissive];
            bindEnvironment(program, mEnvironment, sky, ALFarPlane::eyeToSkyFrame(gGL.getModelviewMatrix()));
            program.bind();
            glUniform1i(glGetUniformLocation(program.mProgramObject, "test_layer"), layer);
            glUniform3fv(glGetUniformLocation(program.mProgramObject, "test_background"), 1, BACKGROUND[emissive]);
            bind(program);
            {
                LLGLDisable   blend(GL_BLEND);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            program.unbind();
            std::vector<F32> out(size_t(W) * H * 4);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out.data());
            mFloat.flush();
            return out;
        }

        // The sun's quad, as LLVOSky makes it.
        static ALSkyLayers::Corners sunCorners(const BodyCase& sun)
        {
            return ALSkyLayers::bodyCorners(bodyRotation(sun.mElevation, sun.mBearing), BODY_DISTANCE, sun.mScale,
                                            SUN_DISK_RADIUS);
        }

        static ALSkyLayers::Corners moonCorners(const BodyCase& moon)
        {
            return ALSkyLayers::bodyCorners(bodyRotation(moon.mElevation, moon.mBearing), BODY_DISTANCE, moon.mScale,
                                            MOON_DISK_RADIUS);
        }

        // The sun by its old program, as LLDrawPoolWLSky::renderHeavenlyBodies drew it: its quad about the camera,
        // alpha blended, the texture pair bound only where the sky blends between them.
        std::vector<F32> oldSun(const BodyCase& sun, bool emissive, bool stored)
        {
            LLPointer<LLVertexBuffer> quad = bodyQuad(sunCorners(sun));
            return drawOld(sun.mView, emissive, stored, [&]
            {
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
                LLGLSLShader& program = mOldSun[emissive];
                program.bind();
                bindMap(program, LLShaderMgr::DIFFUSE_MAP, mSunTexture, ALSamplers::AnisoClamp);
                if (sun.mBlend > 0.f)
                {
                    bindMap(program, LLShaderMgr::ALTERNATE_DIFFUSE_MAP, mSunTextureNext, ALSamplers::AnisoClamp);
                }
                program.uniform1f(LLShaderMgr::BLEND_FACTOR, sun.mBlend);
                drawQuad(quad);
                program.unbind();
            });
        }

        // The sun along each pixel's ray, as the composite binds it: its quad lowered as the old vertex stage lowered it.
        std::vector<F32> raySun(const BodyCase& sun, bool emissive)
        {
            const std::array<LLVector3, 3> quad =
                ALSkyLayers::rayQuad(sunCorners(sun), LLVector3(0.f, 0.f, -ALSkyLayers::SUN_DROP));
            return drawLayer(sun.mView, emissive, TEST_SUN, [&](LLGLSLShader& program)
            {
                bindMap(program, LLShaderMgr::SKY_SUN_MAP, mSunTexture, ALSamplers::AnisoClamp);
                bindMap(program, LLShaderMgr::SKY_SUN_MAP_NEXT, mSunTextureNext, ALSamplers::AnisoClamp);
                program.uniform1f(LLShaderMgr::SKY_SUN_BLEND, sun.mBlend);
                program.uniform3fv(LLShaderMgr::SKY_SUN_QUAD, 3, quad[0].mV);
            });
        }

        // The moon by its old program: its quad about the camera, alpha blended, faded by its direction's height.
        std::vector<F32> oldMoon(const BodyCase& moon, bool emissive, bool stored)
        {
            LLPointer<LLVertexBuffer> quad = bodyQuad(moonCorners(moon));
            const LLVector3 dir = agentDirection(moon.mElevation, moon.mBearing);
            return drawOld(moon.mView, emissive, stored, [&]
            {
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
                LLGLSLShader& program = mOldMoon[emissive];
                program.bind();
                bindMap(program, LLShaderMgr::DIFFUSE_MAP, mMoonTexture, ALSamplers::AnisoClamp);
                program.uniform1f(LLShaderMgr::MOON_BRIGHTNESS, moon.mBrightness);
                program.uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, dir.mV);
                drawQuad(quad);
                program.unbind();
            });
        }

        std::vector<F32> rayMoon(const BodyCase& moon, bool emissive)
        {
            const std::array<LLVector3, 3> quad = ALSkyLayers::rayQuad(moonCorners(moon));
            const F32 fade = ALSkyLayers::moonFade(agentDirection(moon.mElevation, moon.mBearing).mV[VZ]);
            return drawLayer(moon.mView, emissive, TEST_MOON, [&](LLGLSLShader& program)
            {
                bindMap(program, LLShaderMgr::SKY_MOON_MAP, mMoonTexture, ALSamplers::AnisoClamp);
                program.uniform3fv(LLShaderMgr::SKY_MOON_QUAD, 3, quad[0].mV);
                program.uniform1f(LLShaderMgr::SKY_MOON_FADE, fade);
                program.uniform1f(LLShaderMgr::MOON_BRIGHTNESS, moon.mBrightness);
            });
        }

        // The sun's halo as it was drawn before WindLight: its square about the sun's quad, alpha blended.
        std::vector<F32> oldBloom(const BodyCase& sun, bool emissive, bool stored)
        {
            LLPointer<LLVertexBuffer> quad = bodyQuad(ALSkyLayers::bloomCorners(sunCorners(sun)));
            return drawOld(sun.mView, emissive, stored, [&]
            {
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
                LLGLSLShader& program = mHalo[emissive];
                program.bind();
                bindMap(program, LLShaderMgr::DIFFUSE_MAP, mBloomTexture, ALSamplers::AnisoClamp);
                bindMap(program, LLShaderMgr::ALTERNATE_DIFFUSE_MAP, mBloomTextureNext, ALSamplers::AnisoClamp);
                program.uniform1f(LLShaderMgr::BLEND_FACTOR, sun.mBlend);
                program.uniform4f(LLShaderMgr::DIFFUSE_COLOR, BLOOM_COLOR.mV[VRED], BLOOM_COLOR.mV[VGREEN],
                                  BLOOM_COLOR.mV[VBLUE], BLOOM_OPACITY);
                drawQuad(quad);
                program.unbind();
            });
        }

        // The bloom along each pixel's ray, its square lowered with the sun disc.
        std::vector<F32> rayBloom(const BodyCase& sun, bool emissive)
        {
            const std::array<LLVector3, 3> quad = ALSkyLayers::rayQuad(ALSkyLayers::bloomCorners(sunCorners(sun)),
                                                                       LLVector3(0.f, 0.f, -ALSkyLayers::SUN_DROP));
            return drawLayer(sun.mView, emissive, TEST_BLOOM, [&](LLGLSLShader& program)
            {
                bindMap(program, LLShaderMgr::SKY_BLOOM_MAP, mBloomTexture, ALSamplers::AnisoClamp);
                bindMap(program, LLShaderMgr::SKY_BLOOM_MAP_NEXT, mBloomTextureNext, ALSamplers::AnisoClamp);
                program.uniform1f(LLShaderMgr::SKY_BLOOM_BLEND, sun.mBlend);
                program.uniform3fv(LLShaderMgr::SKY_BLOOM_QUAD, 3, quad[0].mV);
                program.uniform3fv(LLShaderMgr::SKY_BLOOM_COLOR, 1, BLOOM_COLOR.mV);
            });
        }

        // The dome's sphere as a far finer mesh than the dome's, with no skirt: rings every tenth of a degree from the
        // zenith to 22.5 degrees, past where the camera's level meets it, with the dome's planar texture coordinates.
        // Its facets are a hundredth of the dome's in depth, so the clouds drawn over it are the sphere's.
        static LLPointer<LLVertexBuffer> fineSphere()
        {
            constexpr U32 RINGS  = 225;
            constexpr U32 SLICES = 256;
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
                const F32 phi = (F32)ring / RINGS * (F_PI / 8.f);
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

        // The clouds by their old program, as LLDrawPoolWLSky::renderSkyCloudsDeferred drew them: the dome's mesh,
        // alpha blended, the next noise bound only while the sky blends toward it.
        std::vector<F32> oldClouds(const CloudCase& clouds, bool emissive, bool stored, LLVertexBuffer* mesh)
        {
            const Sky& sky = SKIES[clouds.mSky];
            return drawOld(clouds.mView, emissive, stored, [&]
            {
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
                LLGLSLShader& program = mOldClouds[emissive];
                bindEnvironment(program, mEnvironment, sky, ALFarPlane::eyeToSkyFrame(gGL.getModelviewMatrix()));
                program.bind();
                bindDome(program, sky, nullptr, nullptr);
                bindMap(program, LLShaderMgr::CLOUD_NOISE_MAP, mCloudNoise, ALSamplers::AnisoWrap);
                if (clouds.mBlend > 0.f)
                {
                    bindMap(program, LLShaderMgr::CLOUD_NOISE_MAP_NEXT, mCloudNoiseNext, ALSamplers::AnisoWrap);
                }
                program.uniform1f(LLShaderMgr::BLEND_FACTOR, clouds.mBlend);
                drawDome(mesh, mesh == mFineDome ? LLRender::TRIANGLES : LLRender::TRIANGLE_STRIP);
                program.unbind();
            });
        }

        // The clouds along each pixel's ray.
        std::vector<F32> rayClouds(const CloudCase& clouds, bool emissive)
        {
            const Sky& sky = SKIES[clouds.mSky];
            return drawLayer(clouds.mView, emissive, TEST_CLOUDS, [&](LLGLSLShader& program)
            {
                bindDome(program, sky, nullptr, nullptr);
                bindMap(program, LLShaderMgr::CLOUD_NOISE_MAP, mCloudNoise, ALSamplers::AnisoWrap);
                bindMap(program, LLShaderMgr::CLOUD_NOISE_MAP_NEXT, mCloudNoiseNext, ALSamplers::AnisoWrap);
                program.uniform1f(LLShaderMgr::SKY_CLOUD_BLEND, clouds.mBlend);
            }, sky);
        }

        // How pictures @a a and @a b differ where either differs from the background, each channel allowed
        // @a allowance plus @a steps of the path's storage at the larger value; pixels @a skip marks are left out.
        static Difference compare(const std::vector<F32>& a, const std::vector<F32>& b, bool emissive, F32 allowance,
                                  F32 steps, const std::vector<bool>& skip = {}, F32 dithered = 0.f)
        {
            Difference d;
            for (size_t px = 0; px < size_t(W) * H; ++px)
            {
                if (!skip.empty() && skip[px])
                {
                    continue;
                }
                const F32* pa = &a[px * 4];
                const F32* pb = &b[px * 4];
                bool lit = false;
                bool beyond = false;
                for (S32 c = 0; c < 3; ++c)
                {
                    const F32 diff = fabsf(pa[c] - pb[c]);
                    d.mMost = llmax(d.mMost, diff);
                    lit = lit || fabsf(pa[c] - BACKGROUND[emissive][c]) > 1e-6f || fabsf(pb[c] - BACKGROUND[emissive][c]) > 1e-6f;
                    const F32 larger = llmax(pa[c], pb[c]);
                    beyond = beyond ||
                             diff > allowance + steps * storageStep(larger, emissive, c) + dithered * ditherReach(c) * larger;
                }
                d.mLit += lit;
                d.mBeyond += beyond;
            }
            return d;
        }

        // The pixels where the old program's 8-bit picture lies past a step from its float one: where the albedo
        // clamped what the layer wrote or blended past 1, which the composite no longer does (D6).
        static std::vector<bool> clamped(const std::vector<F32>& stored, const std::vector<F32>& unstored)
        {
            std::vector<bool> past(size_t(W) * H, false);
            for (size_t px = 0; px < past.size(); ++px)
            {
                for (S32 c = 0; c < 3; ++c)
                {
                    past[px] = past[px] || fabsf(stored[px * 4 + c] - unstored[px * 4 + c]) > 1.f / 255.f + 1e-5f;
                }
            }
            return past;
        }

        // Compare a layer's three pictures: the per-ray one against the old program's float one within @a maths, and
        // against its stored one within a step of the storage besides, where the blend rounded: GL leaves the
        // precision of a blend into an 8-bit attachment, and the rounding of a conversion to R11G11B10F, to the
        // implementation. Where the layer @a can_clamp, writing past 1, the 8-bit albedo also clamped it (D6), and
        // those pixels are counted into @a clamped_pixels and left out; R11G11B10F holds the layer's whole range.
        static void check(const std::vector<F32>& ray, const std::vector<F32>& old_float,
                          const std::vector<F32>& old_stored, bool emissive, F32 maths, S32& clamped_pixels,
                          bool can_clamp = false)
        {
            const std::vector<bool> clamps =
                emissive || !can_clamp ? std::vector<bool>() : clamped(old_stored, old_float);
            const S32 clamp_count = (S32)std::count(clamps.begin(), clamps.end(), true);
            const F32 rounding = 1.f;
            const Difference d_maths = compare(ray, old_float, emissive, maths, 0.f);
            const Difference d_storage = compare(ray, old_stored, emissive, maths, rounding, clamps);
            const Difference d_stored = compare(old_stored, old_float, emissive, maths, rounding, clamps);
            const std::string maths_summary = d_maths.summary();
            const std::string storage_summary = d_storage.summary();
            CAPTURE(maths_summary, storage_summary, clamp_count);
            UNSCOPED_INFO("the layer is in view");
            CHECK(d_maths.mLit > 50);
            CHECK(d_maths.mBeyond == 0);
            CHECK(d_storage.mBeyond == 0);
            CHECK(d_stored.mBeyond == 0);
            clamped_pixels += clamp_count;
        }

        SkyShaderTree          mTree;
        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader           mOldSun[2];
        LLGLSLShader           mOldMoon[2];
        LLGLSLShader           mHalo[2];
        LLGLSLShader           mOldClouds[2];
        LLGLSLShader           mLayers[2];
        LLRenderTarget         mGBuffer[2][2];
        LLRenderTarget         mFloat;
        LLPointer<LLVertexBuffer> mTriangle;
        LLPointer<LLImageGL>   mSunTexture;
        LLPointer<LLImageGL>   mSunTextureNext;
        LLPointer<LLImageGL>   mMoonTexture;
        LLPointer<LLImageGL>   mBloomTexture;
        LLPointer<LLImageGL>   mBloomTextureNext;
        LLPointer<LLImageGL>   mCloudNoise;
        LLPointer<LLImageGL>   mCloudNoiseNext;
        LLPointer<LLVertexBuffer> mDome;
        LLPointer<LLVertexBuffer> mFineDome;
        GLuint                 mEnvironment = 0;
    };

    // How far the per-ray clouds may lie from the old program's over the sphere, display encoded: the filter's
    // rounding (1.7/255 at most). How far that lies from the old program's over the dome's mesh above the camera's
    // level, its facets (30/255 at most, at cloud edges near the horizon); and below it, its skirt (3.2/255).
    constexpr F32 CLOUD_MATHS  = 2.f / 255.f;
    constexpr F32 CLOUD_FACETS = 40.f / 255.f;
    constexpr F32 CLOUD_SKIRT  = 4.f / 255.f;

    // How far the per-ray sun and moon may lie from their raster quads with nothing stored between: what the filter
    // makes of derivatives taken from the ray's coordinates rather than interpolated across the quad's two triangles.
    constexpr F32 BODY_MATHS = 2.f / 255.f;
}

TEST_CASE_METHOD(alskylayers_gl_data, "alskylayers_gl: the sun along the view ray is the sun's quad", "[alskylayers_gl]")
{
    setUp();
    S32 clamped_pixels = 0;
    for (const BodyCase& sun : SUNS)
    {
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            CAPTURE(sun.mName, emissive);
            check(raySun(sun, emissive), oldSun(sun, emissive, false), oldSun(sun, emissive, true), emissive,
                  BODY_MATHS, clamped_pixels);
        }
    }
}

TEST_CASE_METHOD(alskylayers_gl_data, "alskylayers_gl: the sun behind the camera draws nothing", "[alskylayers_gl]")
{
    // Looking straight away from the sun, every ray meets the plane of its quad behind the camera, and the quad's
    // mirror image lies where the view looks.
    setUp();
    for (const BodyCase& sun : SUNS)
    {
        BodyCase away = sun;
        away.mView = { sun.mBearing + 180.f, -sun.mElevation, sun.mView.mFov };
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            CAPTURE(sun.mName, emissive);
            const Difference d = compare(raySun(away, emissive), oldSun(away, emissive, false), emissive, 0.f, 0.f);
            CHECK(d.mLit == 0);
        }
    }
}

TEST_CASE_METHOD(alskylayers_gl_data, "alskylayers_gl: the moon along the view ray is the moon's quad", "[alskylayers_gl]")
{
    setUp();
    S32 clamped_pixels = 0;
    for (const BodyCase& moon : MOONS)
    {
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            CAPTURE(moon.mName, emissive);
            check(rayMoon(moon, emissive), oldMoon(moon, emissive, false), oldMoon(moon, emissive, true), emissive,
                  BODY_MATHS, clamped_pixels, true);
        }
    }
    // The bright moon passes 1, which the albedo clamped.
    UNSCOPED_INFO("the 8-bit albedo clamped the bright moon");
    CHECK(clamped_pixels > 0);
}

TEST_CASE_METHOD(alskylayers_gl_data, "alskylayers_gl: the bloom along the view ray is the sun's halo", "[alskylayers_gl]")
{
    setUp();
    S32 clamped_pixels = 0;
    for (const BodyCase& sun : BLOOMS)
    {
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            CAPTURE(sun.mName, emissive);
            check(rayBloom(sun, emissive), oldBloom(sun, emissive, false), oldBloom(sun, emissive, true), emissive,
                  BODY_MATHS, clamped_pixels);
        }
    }
}

TEST_CASE_METHOD(alskylayers_gl_data, "alskylayers_gl: the clouds along the view ray are the clouds on the dome",
                 "[alskylayers_gl]")
{
    // The clouds' texture coordinates are linear in the point on the dome, and so are the haze terms of the point, so
    // the clouds along the ray are the clouds the old program drew over the sphere itself: over a mesh fine enough,
    // the same up to the filter. Over the dome's own mesh they differ by its facets, which near the horizon move the
    // point a ray meets by tens of metres and the detail noise, sixteen times as fine, by texels (accepted, plan §4),
    // and below the camera's level, where the mesh's skirt bends toward the axis and the ray continues the sphere
    // (D7), over the sliver the 50 m raise lifts above the horizon. The 8-bit albedo then clamps the clouds' colour,
    // up to 2 (D6). With the emissive buffer the old program dithered what it wrote (ditherEmissive).
    setUp();
    S32 clamped_pixels = 0;
    for (const CloudCase& clouds : CLOUDS)
    {
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            CAPTURE(clouds.mName, emissive);
            const std::vector<F32> ray          = rayClouds(clouds, emissive);
            const std::vector<F32> sphere       = oldClouds(clouds, emissive, false, mFineDome);
            const std::vector<F32> dome_float   = oldClouds(clouds, emissive, false, mDome);
            const std::vector<F32> dome_stored  = oldClouds(clouds, emissive, true, mDome);

            // The pixels above and below the camera's level, by their ray in the dome's frame.
            loadView(clouds.mView, W, H);
            const std::array<F32, 9> eye_to_sky = ALFarPlane::eyeToSkyFrame(gGL.getModelviewMatrix());
            std::vector<bool> below(size_t(W) * H);
            std::vector<bool> cap(size_t(W) * H);
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    const bool under = domeRay(clouds.mView, eye_to_sky, W, H, x, y).mV[VY] < 0.f;
                    below[size_t(y) * W + x] = under;
                    cap[size_t(y) * W + x] = !under;
                }
            }

            const F32 dithered = emissive ? 1.f : 0.f;
            const std::vector<bool> clamps = emissive ? std::vector<bool>() : clamped(dome_stored, dome_float);
            const S32 clamp_count = (S32)std::count(clamps.begin(), clamps.end(), true);

            const Difference maths  = compare(ray, sphere, emissive, CLOUD_MATHS, 0.f, {}, dithered);
            const Difference facets = compare(dome_float, sphere, emissive, CLOUD_FACETS, 0.f, below, 2.f * dithered);
            const Difference skirt  = compare(dome_float, sphere, emissive, 0.f, 0.f, cap, 2.f * dithered);
            const Difference stored = compare(dome_stored, dome_float, emissive, 1e-5f, 1.f, clamps, 2.f * dithered);
            const std::string maths_summary  = maths.summary();
            const std::string facets_summary = facets.summary();
            const std::string skirt_summary  = skirt.summary();
            const std::string stored_summary = stored.summary();
            CAPTURE(maths_summary, facets_summary, skirt_summary, stored_summary, clamp_count);
            UNSCOPED_INFO("the clouds are in view");
            CHECK(maths.mLit > 1000);
            CHECK(maths.mBeyond == 0);
            CHECK(facets.mBeyond == 0);
            CHECK(skirt.mMost < CLOUD_SKIRT);
            CHECK(stored.mBeyond == 0);
            clamped_pixels += clamp_count;
        }
    }
    UNSCOPED_INFO("the 8-bit albedo clamped bright clouds");
    CHECK(clamped_pixels > 0);
}
