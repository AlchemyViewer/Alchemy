/**
 * @file alstarfield_gl_test.cpp
 * @brief The stars two ways on the hidden window, from the same fixed-seed field: as they were drawn before their
 *        rework (a quad of six vertices a star, every term worked per fragment, into all four attachments), and as
 *        ALSky draws them now (a point a star, a quad for each spiked one, the cells in view, the stars bright
 *        enough to show, into the one attachment the sky's colour is in). The two must leave the same picture.
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

#include "alstarfield.h"
#include "alskyraster_gl.h"

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
#include "alstarfield_draws.h"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr S32 W = 480;
    constexpr S32 H = 320;
    constexpr F32 NEAR_CLIP = 0.5f;
    constexpr F32 FAR_CLIP  = 1024.f;

    // LLSettingsSky's DOME_RADIUS.
    constexpr F32 DOME_RADIUS = 15000.f;

    // Where the camera is in the agent's frame. The stars are taken to it, so nothing should depend on it.
    const LLVector3 CAMERA(118.f, -43.f, 27.f);

    // What the dome left in each attachment where the stars drew over it before the sky's composite
    // (tests/skyraster/deferred/skyF.glsl): its colour in the albedo, or in the emissive with the emissive buffer, and
    // the sky's flags in the normal's alpha.
    const F32 DOME[4]     = { 0.021f, 0.034f, 0.071f, 1.f };
    const F32 NO_COLOUR[4] = { 0.f, 0.f, 0.f, 1.f };
    const F32 SKY_FLAGS[4] = { 0.f, 0.f, 0.f, 0.f };

    // Half a step of 8 bits: what ALSky's brightness cull lets a left-out star add, at most.
    constexpr F32 HALF_QUANTUM = 0.5f / 255.f;

    // A frame of the night sky: the camera's bearing clockwise from north, its pitch above the horizon and its
    // vertical field of view, in degrees; how far the stars have turned about z, in degrees; the scintillation's
    // time; the star brightness (custom_alpha); and the moon, its brightness 0 for none, its elevation and bearing in
    // the stars' frame, and the half-size of its quad.
    struct Scene
    {
        const char* mName;
        F32 mBearing;
        F32 mPitch;
        F32 mFov;
        F32 mRotation;
        F32 mTime;
        F32 mStarAlpha;
        F32 mMoonBrightness;
        F32 mMoonElevation;
        F32 mMoonBearing;
        F32 mMoonSize;
    };

    const Scene SCENES[] = {
        { "night, looking up", 30.f, 35.f, 60.f, 17.f, 1234.5f, 0.5f, 0.f, 0.f, 0.f, 0.f },
        { "night, at the horizon", 200.f, -3.f, 60.f, 245.f, 40000.25f, 0.5f, 0.f, 0.f, 0.f, 0.f },
        { "night, zenith, wide", 75.f, 80.f, 100.f, 3.f, 77.f, 0.8f, 0.f, 0.f, 0.f, 0.f },
        { "moonlit, the moon in view", 120.f, 25.f, 50.f, 90.f, 5000.f, 0.5f, 0.9f, 30.f, 214.f, 0.08f },
        { "dusk", 300.f, 20.f, 60.f, 140.f, 321.f, 0.03f, 0.f, 0.f, 0.f, 0.f },
        { "dusk, moon up", 10.f, 45.f, 70.f, 300.f, 9.75f, 0.06f, 0.6f, 55.f, 305.f, 0.05f },
    };

    // A direction in the stars' frame from an elevation and a bearing clockwise from north (+y), x east.
    LLVector3 direction(F32 elevation, F32 bearing)
    {
        const F32 el = elevation * DEG_TO_RAD;
        const F32 az = bearing * DEG_TO_RAD;
        return LLVector3(sinf(az) * cosf(el), cosf(az) * cosf(el), sinf(el));
    }

    // The stars' shaders as they were before their rework (deferred/starsV.glsl and starsF.glsl at the fixed seed's
    // commit), the engine's matrix blocks spliced in where they say so, as the viewer's own shaders take them: the
    // picture the rework has to keep.
    const char* kOldVertex = R"(//[ENGINE_BLOCK Matrices]
uniform vec2 screen_res;

in vec3 position;
in vec4 diffuse_color;
in vec2 texcoord0;

out vec3 vary_world_dir;
out vec2 vary_corner;
out vec4 vary_color;
out float vary_intensity;
out float vary_subpixel_fade;

vec3 srgb_to_linear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

void main()
{
    vec4 clip_center = modelview_projection_matrix * vec4(position, 1.0);

    float intensity = diffuse_color.a;

    float base_px   = mix(1.8, 7.0, intensity);
    float spike_px  = smoothstep(0.90, 1.0, intensity) * 12.0;
    float ideal_px  = base_px + spike_px;

    const float min_px    = 2.6;
    float quad_pixels     = max(ideal_px, min_px);
    vary_subpixel_fade    = (ideal_px * ideal_px) / (quad_pixels * quad_pixels);

    vec2 pixel_offset = texcoord0 * (quad_pixels * 0.5);

    vec2 ndc_offset = pixel_offset * (2.0 / max(screen_res, vec2(1.0)));
    clip_center.xy += ndc_offset * clip_center.w;

#ifdef REVERSE_Z
    clip_center.z = 0.0;
#else
    clip_center.z = clip_center.w;
#endif

    gl_Position = clip_center;

    vary_corner    = texcoord0;
    vary_world_dir = normalize(position);
    vary_color     = vec4(srgb_to_linear(diffuse_color.rgb), intensity);
    vary_intensity = intensity;
}
)";

    const char* kOldFragment = R"(/*[EXTRA_CODE_HERE]*/
out vec4 frag_data[4];

in vec3 vary_world_dir;
in vec2 vary_corner;
in vec4 vary_color;
in float vary_intensity;
in float vary_subpixel_fade;

uniform float custom_alpha;
uniform float time;
uniform vec3  moon_dir;
uniform float moon_brightness;

uniform vec3 moonQuadCenter;
uniform vec3 moonQuadU;
uniform vec3 moonQuadV;
uniform sampler2D diffuseMap;

float hash31(vec3 p)
{
    p  = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise3D(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    float n000 = hash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash31(i + vec3(1.0, 1.0, 1.0));

    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y),
               u.z);
}

float airmassKastenYoung(float cos_zenith)
{
    float cz = clamp(cos_zenith, 0.02, 1.0);
    float elev_deg = degrees(asin(cz));
    return 1.0 / (cz + 0.50572 * pow(max(6.07995 + elev_deg, 1.0), -1.6364));
}

void main()
{
    float r2   = dot(vary_corner, vary_corner);
    float core = exp(-r2 * 9.0);
    float halo = exp(-r2 * 1.8) * 0.06;

    float ax = 1.0 - abs(vary_corner.x);
    float ay = 1.0 - abs(vary_corner.y);
    float spike_h = exp(-abs(vary_corner.y) * 32.0) * ax;
    float spike_v = exp(-abs(vary_corner.x) * 32.0) * ay;
    float spike_strength = smoothstep(0.90, 1.0, vary_intensity);
    float spike = (spike_h + spike_v) * spike_strength * 0.30;

    float shape = core + halo + spike;

    if (shape < 0.002) discard;

    float moon_facing = dot(vary_world_dir, moonQuadCenter);
    if (moon_facing > 0.0)
    {
        vec3 on_quad = vary_world_dir / moon_facing - moonQuadCenter;
        vec2 quad = vec2(dot(on_quad, moonQuadU), dot(on_quad, moonQuadV));
        if (all(lessThan(abs(quad), vec2(1.0))) && textureLod(diffuseMap, quad * 0.5 + 0.5, 0.0).a > 2./255.)
        {
            discard;
        }
    }

    float cos_zenith = vary_world_dir.z;
    float airmass    = airmassKastenYoung(cos_zenith);

    const vec3 k_ext = vec3(0.05, 0.13, 0.30);
    vec3 extinction  = exp(-airmass * k_ext);

    vec3 seed = vary_world_dir * 120.0;

    float n_fast = valueNoise3D(seed + vec3(0.0, 0.0, time * 2.3));
    float n_slow = valueNoise3D(seed * 0.37 + vec3(0.0, 0.0, time * 0.7));
    float twinkle_base = mix(n_fast, n_slow, 0.35);

    float twinkle_amount = clamp(0.18 + 0.14 * (airmass - 1.0), 0.0, 0.65) * vary_intensity;
    float twinkle = 1.0 + (twinkle_base - 0.5) * 2.0 * twinkle_amount;

    vec3 chroma_noise = vec3(
        valueNoise3D(seed * 0.55 + vec3(0.0, 0.0, time * 1.7 +  0.0)),
        valueNoise3D(seed * 0.55 + vec3(0.0, 0.0, time * 1.3 +  5.7)),
        valueNoise3D(seed * 0.55 + vec3(0.0, 0.0, time * 2.1 + 13.1)));
    float chroma_amt = 0.18 * twinkle_amount;
    vec3  color_jitter = 1.0 + (chroma_noise - 0.5) * 2.0 * chroma_amt;

    float moon_up    = max(0.0, moon_dir.z);
    float moon_power = moon_brightness * moon_up;
    float sky_wash   = moon_power * (1.0 - vary_intensity) * 0.55;
    float moon_cos   = max(0.0, dot(vary_world_dir, moon_dir));
    float direct_glare = pow(moon_cos, 10.0) * moon_power * 0.85;
    float moon_fade  = clamp(sky_wash + direct_glare, 0.0, 0.95);

    vec3 star_color = vary_color.rgb * color_jitter * extinction * twinkle;
    star_color *= vary_intensity;
    star_color *= custom_alpha;
    star_color *= (1.0 - moon_fade);
    star_color *= vary_subpixel_fade;

    vec3 premult = star_color * shape;

    float bloom_boost = 1.0 + vary_intensity * vary_intensity * 1.2;

    frag_data[1] = vec4(0.0);
    frag_data[2] = vec4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);

#if defined(HAS_EMISSIVE)
    frag_data[0] = vec4(0.0);
    frag_data[3] = vec4(premult * bloom_boost, shape);
#else
    frag_data[0] = vec4(premult * (1.0 + vary_intensity), shape);
#endif
}
)";

    // The attachments of one draw, read back: every channel of every pixel, as floats.
    struct Picture
    {
        std::vector<std::vector<F32>> mAttachments;

        const F32* pixel(S32 attachment, S32 x, S32 y) const
        {
            return &mAttachments[attachment][(size_t(y) * W + x) * 4];
        }
    };

    // A step of the attachment's storage at @a value: a step of 8 bits, or of R11G11B10F's mantissa, six bits for red
    // and green and five for blue, at value's exponent (its least, 2^-14, below it).
    F32 storageStep(F32 value, bool emissive, S32 channel)
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

    // How two pictures' star attachment differ, over the colour channels: the most, the pixels where it differs at
    // all, by half a step of 8 bits or more, and past the bound compare() is given, and the pixels a star lit in
    // either.
    struct Difference
    {
        F32 mMost = 0.f;
        S32 mDiffer = 0;
        S32 mPastHalfQuantum = 0;
        S32 mBeyond = 0;
        S32 mLit = 0;

        std::string summary() const
        {
            return fmt::format("most {:.6f} ({:.2f}/255), {} of {} lit pixels differ, {} by half a step of 8 bits or "
                               "more, {} past the bound",
                               mMost, mMost * 255.f, mDiffer, mLit, mPastHalfQuantum, mBeyond);
        }
    };

    struct alstarfield_gl_data
    {
        // The programs build from the sky tests' shader tree, the old stars' beside its files (setUp).
        alstarfield_gl_data()
        : mShaders(mTree.prefix())
        {
        }

        // The tree the programs build from, and the old stars' files in it.
        void populateTree()
        {
            mTree.populate();
            REQUIRE(mTree.write("startest/starsOldV.glsl", kOldVertex));
            REQUIRE(mTree.write("startest/starsOldF.glsl", kOldFragment));
        }

        ~alstarfield_gl_data()
        {
            LLGLSLShader::unbind();
            for (S32 path = 0; path < 2; ++path)
            {
                mOld[path].unload();
                mStarPoints[path].unload();
                mStarQuads[path].unload();
                mTarget[path].release();
            }
            mShaders.clearShaderObjects();
            mOldQuads = nullptr;
            mStarBuffer = nullptr;
            mMoonTexture = nullptr;
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
        }

        // The old program, from the text above, for the emissive path (1) or the albedo's (0): built as the viewer
        // builds its own, its matrix blocks spliced from the engine's where it says so.
        void buildOld(LLGLSLShader& program, bool emissive)
        {
            program.mName = "stars before their rework (test)";
            program.mShaderFiles.clear();
            program.mShaderFiles.push_back(std::make_pair("startest/starsOldV.glsl", GL_VERTEX_SHADER));
            program.mShaderFiles.push_back(std::make_pair("startest/starsOldF.glsl", GL_FRAGMENT_SHADER));
            program.mShaderLevel = 1;
            if (emissive)
            {
                program.addPermutation("HAS_EMISSIVE", "1");
            }
            UNSCOPED_INFO("the old star program builds");
            REQUIRE(program.createShader());
        }

        // A star program as LLViewerShaderMgr builds it: the points', or the spiked stars' quads'.
        void buildStars(LLGLSLShader& program, bool emissive, bool quads)
        {
            program.mName = quads ? "Deferred Star Quad Program" : "Deferred Star Program";
            program.mShaderFiles.clear();
            program.mShaderFiles.push_back(std::make_pair("deferred/starsV.glsl", GL_VERTEX_SHADER));
            program.mShaderFiles.push_back(std::make_pair("deferred/valueNoiseUtil.glsl", GL_VERTEX_SHADER));
            program.mShaderFiles.push_back(std::make_pair("deferred/starsF.glsl", GL_FRAGMENT_SHADER));
            program.mShaderLevel = 1;
            program.mShaderGroup = LLGLSLShader::SG_SKY;
            if (quads)
            {
                program.addPermutation("STAR_QUADS", "1");
            }
            if (emissive)
            {
                program.addPermutation("HAS_EMISSIVE", "1");
            }
            UNSCOPED_INFO("the star program builds");
            REQUIRE(program.createShader());
        }

        // A moon's texture: an opaque disc that fades out at its rim through every alpha the mask's 2 / 255 cut
        // could fall between.
        static LLPointer<LLImageGL> moonDisc()
        {
            constexpr S32 SIZE = 64;
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
                    px[0] = px[1] = px[2] = 200;
                    px[3] = (U8)llclamp(std::lround(255.f * (0.9f - r) / 0.15f), 0L, 255L);
                }
            }
            return new LLImageGL(raw, false);
        }

        // The field at the default count; the old buffer, six vertices a star with their corner offsets, as
        // LLVOWLSky::updateStarGeometry filled it, and the buffer ALSky fills now; both paths' programs; and a G-buffer
        // for each, the emissive one with four attachments and the albedo's with three.
        void setUp()
        {
            populateTree();

            // The objects every program gets.
            std::map<std::string, std::string> defines;
            for (const auto& [object, stage] : { std::make_pair("deferred/textureUtilV.glsl", GL_VERTEX_SHADER),
                                                 std::make_pair("objects/nonindexedTextureV.glsl", GL_VERTEX_SHADER),
                                                 std::make_pair("deferred/globalF.glsl", GL_FRAGMENT_SHADER) })
            {
                CAPTURE(object);
                S32 level = 1;
                REQUIRE(mShaders.loadShaderFile(object, level, stage, &defines) != 0);
            }

            mField = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);
            REQUIRE(mField.size() == ALStarField::DEFAULT_COUNT);

            static const LLVector2 CORNERS[6] = { LLVector2(-1.f, -1.f), LLVector2(1.f, -1.f), LLVector2(1.f, 1.f),
                                                  LLVector2(-1.f, -1.f), LLVector2(1.f, 1.f), LLVector2(-1.f, 1.f) };
            mOldQuads = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_COLOR
                                           | LLVertexBuffer::MAP_TEXCOORD0);
            REQUIRE(mOldQuads->allocateBuffer((U32)mField.size() * 6, 0));
            LLStrider<LLVector3> vertices;
            LLStrider<LLColor4U> colours;
            LLStrider<LLVector2> corners;
            REQUIRE(mOldQuads->getVertexStrider(vertices));
            REQUIRE(mOldQuads->getColorStrider(colours));
            REQUIRE(mOldQuads->getTexCoord0Strider(corners));
            for (const ALStarField::Star& star : mField)
            {
                for (const LLVector2& corner : CORNERS)
                {
                    *(vertices++) = star.mPosition;
                    *(colours++)  = star.mColour;
                    *(corners++)  = corner;
                }
            }
            mOldQuads->unmapBuffer();

            mLaidOut = mField;
            mLayout = ALStarField::layOut(mLaidOut);
            mStarBuffer = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXTURE_INDEX);
            REQUIRE(mStarBuffer->allocateBuffer(mLayout.vertices(), 0));
            LLStrider<LLVector4a> star_vertices;
            REQUIRE(mStarBuffer->getVertexStrider(star_vertices));
            ALStarField::fillVertices(mLaidOut, mLayout, star_vertices);
            mStarBuffer->unmapBuffer();

            mMoonTexture = moonDisc();

            for (S32 path = 0; path < 2; ++path)
            {
                buildOld(mOld[path], path == 1);
                buildStars(mStarPoints[path], path == 1, false);
                buildStars(mStarQuads[path], path == 1, true);
                REQUIRE(mTarget[path].allocate(W, H, GL_RGBA8));
                REQUIRE(mTarget[path].addColorAttachment(GL_RGBA8));
                REQUIRE(mTarget[path].addColorAttachment(GL_RGB10_A2));
                if (path == 1)
                {
                    REQUIRE(mTarget[path].addColorAttachment(GL_R11F_G11F_B10F));
                }
            }
        }

        // The camera looking along @a scene from CAMERA: the agent's frame (east, north, up) turned to the eye's, the
        // picture moved @a shift pixels right and half that up.
        static void loadView(const Scene& scene, F32 shift = 0.f)
        {
            gGL.matrixMode(LLRender::MM_PROJECTION);
            const F32 tan_half = tanf(scene.mFov * 0.5f * DEG_TO_RAD);
            LLMatrix4a proj;
            proj.clear();
            F32* p = proj.getF32ptr();
            p[0]  = 1.f / (tan_half * (F32)W / (F32)H);
            p[5]  = 1.f / tan_half;
            p[10] = (FAR_CLIP + NEAR_CLIP) / (NEAR_CLIP - FAR_CLIP);
            p[11] = -1.f;
            p[14] = 2.f * FAR_CLIP * NEAR_CLIP / (NEAR_CLIP - FAR_CLIP);
            p[8]  = -shift * 2.f / (F32)W;
            p[9]  = -shift / (F32)H;
            gGL.loadMatrix(proj);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
            gGL.rotatef(-scene.mPitch, 1.f, 0.f, 0.f);
            gGL.rotatef(-90.f, 1.f, 0.f, 0.f);
            gGL.rotatef(scene.mBearing, 0.f, 0.f, 1.f);
            gGL.translatef(-CAMERA.mV[VX], -CAMERA.mV[VY], -CAMERA.mV[VZ]);
        }

        // The star uniforms on the bound @a program, as ALSky::renderStars sets them, in the stars'
        // frame; the moon's mask only where @a mask_moon.
        void bindUniforms(LLGLSLShader& program, const Scene& scene, bool mask_moon = true)
        {
            program.uniform1f(LLShaderMgr::CUSTOM_ALPHA, scene.mStarAlpha);
            program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)W, (F32)H);
            program.uniform1f(LLShaderMgr::WATER_TIME, scene.mTime);

            const LLVector3 moon = direction(scene.mMoonElevation, scene.mMoonBearing);
            program.uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, moon.mV);
            program.uniform1f(LLShaderMgr::MOON_BRIGHTNESS, scene.mMoonBrightness);
            if (mask_moon && scene.mMoonBrightness > 0.f)
            {
                // The quad on the plane a unit away along the moon, its axes scaled so the quad spans [-1, 1].
                LLVector3 u = moon % LLVector3(0.f, 0.f, 1.f);
                u.normVec();
                LLVector3 v = u % moon;
                v.normVec();
                u /= scene.mMoonSize;
                v /= scene.mMoonSize;
                program.uniform3fv(LLShaderMgr::MOON_QUAD_CENTER, 1, moon.mV);
                program.uniform3fv(LLShaderMgr::MOON_QUAD_U, 1, u.mV);
                program.uniform3fv(LLShaderMgr::MOON_QUAD_V, 1, v.mV);
                const S32 channel = program.enableTexture(LLShaderMgr::DIFFUSE_MAP);
                REQUIRE(channel > -1);
                gGL.getTextureSlot(channel)->bindSampled(mMoonTexture, ALSamplers::AnisoClamp);
            }
            else
            {
                program.uniform3f(LLShaderMgr::MOON_QUAD_CENTER, 0.f, 0.f, 0.f);
            }
        }

        // The dome's attachments as the stars find them: the colour where the path keeps the sky's, and the flags.
        void clearDome(bool emissive)
        {
            glClearBufferfv(GL_COLOR, 0, emissive ? NO_COLOUR : DOME);
            glClearBufferfv(GL_COLOR, 1, NO_COLOUR);
            glClearBufferfv(GL_COLOR, 2, SKY_FLAGS);
            if (emissive)
            {
                glClearBufferfv(GL_COLOR, 3, DOME);
            }
        }

        // Read every attachment of the bound @a target back.
        static Picture readBack(LLRenderTarget& target, S32 attachments)
        {
            Picture picture;
            picture.mAttachments.resize(attachments);
            for (S32 a = 0; a < attachments; ++a)
            {
                picture.mAttachments[a].resize(size_t(W) * H * 4);
                glReadBuffer(GL_COLOR_ATTACHMENT0 + a);
                glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, picture.mAttachments[a].data());
            }
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            return picture;
        }

        // The stars drawn the old way, every star a quad of six vertices, into the G-buffer of @a emissive's path; the
        // moon's mask only where @a mask_moon, the picture moved @a shift pixels.
        Picture drawOld(const Scene& scene, bool emissive, bool mask_moon = true, F32 shift = 0.f)
        {
            LLRenderTarget& target = mTarget[emissive];
            target.bindTarget();
            clearDome(emissive);
            loadView(scene, shift);
            {
                LLGLEnable    blend(GL_BLEND);
                LLGLDisable   cull(GL_CULL_FACE);
                LLGLDisable   srgb(GL_FRAMEBUFFER_SRGB);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                gGL.setSceneBlendType(LLRender::BT_ADD);
                gGL.pushMatrix();
                gGL.translatef(CAMERA.mV[VX], CAMERA.mV[VY], CAMERA.mV[VZ]);
                gGL.rotatef(scene.mRotation, 0.f, 0.f, 1.f);
                LLGLSLShader& program = mOld[emissive];
                program.bind();
                bindUniforms(program, scene, mask_moon);
                mOldQuads->setBuffer();
                mOldQuads->drawArrays(LLRender::TRIANGLES, 0, mOldQuads->getNumVerts());
                program.unbind();
                gGL.popMatrix();
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
            }
            Picture picture = readBack(target, emissive ? 4 : 3);
            target.flush();
            return picture;
        }

        // The stars drawn as ALSky::renderStars draws them, into the G-buffer of @a emissive's path;
        // every point, the frustum's cull aside, where @a everything, and every star however faint where
        // @a every_star.
        Picture drawStars(const Scene& scene, bool emissive, bool everything = false, bool every_star = false)
        {
            {
                const F32 moon_power = scene.mMoonBrightness * llmax(0.f, sinf(scene.mMoonElevation * DEG_TO_RAD));
                mQuadsDrawn = ALStarField::quadsDrawn(
                    mLayout, every_star ? 0 : ALStarField::faintestVisible(scene.mStarAlpha, moon_power, emissive));
            }
            LLRenderTarget& target = mTarget[emissive];
            target.bindTarget();
            clearDome(emissive);
            loadView(scene);
            {
                LLGLEnable    blend(GL_BLEND);
                LLGLDisable   cull(GL_CULL_FACE);
                LLGLDisable   srgb(GL_FRAMEBUFFER_SRGB);
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                gGL.setSceneBlendType(LLRender::BT_ADD);
                target.setDrawAttachment(ALSkyLayers::starAttachment(emissive));
                gGL.pushMatrix();
                gGL.translatef(CAMERA.mV[VX], CAMERA.mV[VY], CAMERA.mV[VZ]);
                gGL.rotatef(scene.mRotation, 0.f, 0.f, 1.f);
                // The spiked stars as quads, then the rest as points.
                LLGLSLShader& quads = mStarQuads[emissive];
                quads.bind();
                bindUniforms(quads, scene);
                mStarBuffer->setBuffer();
                mStarBuffer->drawArrays(LLRender::TRIANGLES, 0, mQuadsDrawn * ALStarField::QUAD_VERTICES);
                quads.unbind();

                LLMatrix4a mvp;
                mvp.setMul(gGL.getModelviewMatrix(), gGL.getProjectionMatrix());
                const F32 moon_power = scene.mMoonBrightness * llmax(0.f, sinf(scene.mMoonElevation * DEG_TO_RAD));
                const U32 faintest =
                    every_star ? 0 : ALStarField::faintestVisible(scene.mStarAlpha, moon_power, emissive);
                ALStarField::pointDraws(mLayout, ALStarField::pointFrustum(mvp, W, H), faintest, mDraws);
                if (everything)
                {
                    // Every cell, each down to the faintest star the frame keeps.
                    ALStarField::Frustum whole;
                    for (LLVector3& normal : whole.mNormals)
                    {
                        normal.set(0.f, 0.f, 0.f);
                    }
                    ALStarField::pointDraws(mLayout, whole, faintest, mDraws);
                }

                LLGLEnable    point_size(GL_PROGRAM_POINT_SIZE);
                LLGLSLShader& points = mStarPoints[emissive];
                points.bind();
                bindUniforms(points, scene);
                mStarBuffer->setBuffer();
                mStarBuffer->drawMultiArrays(LLRender::POINTS, mDraws.mFirst.data(), mDraws.mCount.data(),
                                             (U32)mDraws.mFirst.size());
                points.unbind();
                gGL.popMatrix();
                target.setDrawBuffers();
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
            }
            Picture picture = readBack(target, emissive ? 4 : 3);
            target.flush();
            return picture;
        }

        // The G-buffer of @a emissive's path as the dome leaves it, before any star.
        Picture drawNothing(bool emissive)
        {
            LLRenderTarget& target = mTarget[emissive];
            target.bindTarget();
            clearDome(emissive);
            Picture picture = readBack(target, emissive ? 4 : 3);
            target.flush();
            return picture;
        }

        // How the stars' attachment of two pictures differ, a star lighting a pixel where either differs from
        // @a blank. Each channel of a pixel may differ by a step of its storage (where an addition rounds the other
        // way: the order of the maths, or of the stars' blends), half a step of 8 bits (what the brightness cull may
        // leave out of it) and @a allowance.
        static Difference compare(const Picture& a, const Picture& b, const Picture& blank, S32 attachment,
                                  F32 allowance = 0.f)
        {
            const bool emissive = attachment == 3;
            Difference d;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    const F32* pa = a.pixel(attachment, x, y);
                    const F32* pb = b.pixel(attachment, x, y);
                    const F32* pn = blank.pixel(attachment, x, y);
                    F32 most = 0.f;
                    bool lit = false;
                    bool beyond = false;
                    for (S32 c = 0; c < 3; ++c)
                    {
                        const F32 diff = fabsf(pa[c] - pb[c]);
                        most = llmax(most, diff);
                        lit = lit || pa[c] != pn[c] || pb[c] != pn[c];
                        const F32 bound = storageStep(llmax(pa[c], pb[c]), emissive, c) + HALF_QUANTUM + allowance;
                        beyond = beyond || diff > bound;
                    }
                    d.mMost = llmax(d.mMost, most);
                    d.mDiffer += most > 0.f;
                    d.mPastHalfQuantum += most >= HALF_QUANTUM;
                    d.mBeyond += beyond;
                    d.mLit += lit;
                }
            }
            return d;
        }

        ll_test::SharedGLScope     mGL;
        al_sky_test::SkyShaderTree mTree;
        ll_test::TestShaderMgr     mShaders;
        std::vector<ALStarField::Star> mField;
        std::vector<ALStarField::Star> mLaidOut;
        ALStarField::Layout mLayout;
        ALStarField::Draws mDraws;
        U32 mQuadsDrawn = 0;
        LLPointer<LLVertexBuffer> mOldQuads;
        LLPointer<LLVertexBuffer> mStarBuffer;
        LLPointer<LLImageGL> mMoonTexture;
        LLGLSLShader   mOld[2];
        LLGLSLShader   mStarPoints[2];
        LLGLSLShader   mStarQuads[2];
        LLRenderTarget mTarget[2];
    };
}

TEST_CASE_METHOD(alstarfield_gl_data, "alstarfield_gl: the stars leave the picture they left before their rework",
                 "[alstarfield_gl]")
{
    // The old quads' corners are snapped to the rasterizer's sub-pixel grid, 1/256 of a pixel, where a point's centre
    // is worked exactly; so the old picture moved by one step of that grid is the measure of what may differ. With
    // every star drawn, the new picture may differ from the old at no more pixels than that, and at no pixel by more
    // than the most it moves any pixel, besides the roundings compare() allows. The brightness cull then leaves out
    // stars that add under half a step each (its own test): many pixels at dusk, each by no more than compare() allows.
    constexpr F32 SUBPIXEL = 1.f / 256.f;

    setUp();
    for (const Scene& scene : SCENES)
    {
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            const S32 attachment = ALSkyLayers::starAttachment(emissive);
            CAPTURE(scene.mName, emissive);
            const Picture blank   = drawNothing(emissive);
            const Picture before  = drawOld(scene, emissive);
            const Picture shifted = drawOld(scene, emissive, true, SUBPIXEL);
            const Picture every   = drawStars(scene, emissive, false, true);
            const Picture after   = drawStars(scene, emissive);
            const Difference jitter = compare(before, shifted, blank, attachment);
            const Difference raster = compare(before, every, blank, attachment, jitter.mMost);
            const Difference d      = compare(before, after, blank, attachment, jitter.mMost);
            const std::string summary = d.summary();
            const std::string every_star = raster.summary();
            const std::string old_moved = jitter.summary();
            CAPTURE(summary, every_star, old_moved);
            UNSCOPED_INFO("the scene shows stars");
            CHECK(d.mLit > 100);
            CHECK(raster.mBeyond == 0);
            CHECK(raster.mDiffer <= jitter.mDiffer);
            CHECK(d.mBeyond == 0);
            // Every other attachment is as the sky left it, where the old stars added into all of them.
            for (S32 other = 0; other < (emissive ? 4 : 3); ++other)
            {
                if (other != attachment)
                {
                    CAPTURE(other);
                    CHECK(after.mAttachments[other] == blank.mAttachments[other]);
                }
            }
            if (scene.mMoonBrightness > 0.f)
            {
                const Difference moon = compare(before, drawOld(scene, emissive, false), blank, attachment);
                CAPTURE(moon.mDiffer);
                UNSCOPED_INFO("the moon hides stars");
                CHECK(moon.mDiffer > 10);
            }
        }
    }
}

TEST_CASE_METHOD(alstarfield_gl_data, "alstarfield_gl: the cull leaves out only cells that draw nothing", "[alstarfield_gl]")
{
    setUp();
    for (const Scene& scene : SCENES)
    {
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            const S32 attachment = ALSkyLayers::starAttachment(emissive);
            CAPTURE(scene.mName, emissive);
            const Picture blank = drawNothing(emissive);
            const Picture all   = drawStars(scene, emissive, true);
            const Picture culled = drawStars(scene, emissive);
            const U32 drawn = al_star_test::drawnPoints(mDraws);
            const Difference d = compare(all, culled, blank, attachment);
            const std::string summary = d.summary();
            CAPTURE(summary, drawn, mLayout.mPoints);
            CHECK(d.mDiffer == 0);
            UNSCOPED_INFO("the cull leaves cells out");
            CHECK(drawn < mLayout.mPoints * 3 / 4);
        }
    }
}

TEST_CASE_METHOD(alstarfield_gl_data, "alstarfield_gl: the brightness cull leaves out only stars that add under half a step",
                 "[alstarfield_gl]")
{
    setUp();
    U32 left_out_somewhere = 0;
    for (const Scene& scene : SCENES)
    {
        for (S32 path = 0; path < 2; ++path)
        {
            const bool emissive = path == 1;
            const S32 attachment = ALSkyLayers::starAttachment(emissive);
            CAPTURE(scene.mName, emissive);
            const Picture blank   = drawNothing(emissive);
            const Picture every   = drawStars(scene, emissive, false, true);
            const U32 all_points  = al_star_test::drawnPoints(mDraws);
            const U32 all_quads   = mQuadsDrawn;
            const Picture visible = drawStars(scene, emissive);
            const U32 points      = al_star_test::drawnPoints(mDraws);
            const U32 quads       = mQuadsDrawn;
            // Each star left out adds under half a step to any pixel. On the 8-bit path every blend rounds, so what
            // each adds rounds away and the picture is the same; on the emissive path two left out where they overlap
            // add together, so a pixel may differ by twice that, and few do by more than once.
            const Difference d = compare(every, visible, blank, attachment, emissive ? ALStarField::LEFT_OUT : 0.f);
            const Difference once = compare(every, visible, blank, attachment);
            const std::string summary = d.summary();
            CAPTURE(summary, once.mBeyond, all_points, points, all_quads, quads);
            CHECK(d.mBeyond == 0);
            CHECK(once.mBeyond * 1000 <= d.mLit);
            if (!emissive)
            {
                CHECK(d.mDiffer == 0);
            }
            left_out_somewhere += all_points - points;
        }
    }
    UNSCOPED_INFO("the dusk scenes leave stars out");
    CHECK(left_out_somewhere > 0);
}
