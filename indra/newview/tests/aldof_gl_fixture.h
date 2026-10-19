/**
 * @file aldof_gl_fixture.h
 * @brief Depth of field's passes as LLPipeline::renderDoF draws them, the circle of confusion worked from the scene's
 *        depth in the gather and the combine, beside passes that carry it in a sharp copy's alpha: for the test that
 *        holds the two together and the benchmark that times them.
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

#ifndef AL_DOF_GL_FIXTURE_H
#define AL_DOF_GL_FIXTURE_H

#include "alpostchain_gl_fixture.h"

#include "llfile.h"
#include "llmath.h"

#include <cmath>
#include <filesystem>
#include <limits>

namespace ll_test
{
    // The viewer's shader tree, copied, with passes carrying the circle of confusion in a sharp copy beside its own:
    // a pass writing the scene and its circle into a sharp copy; the gather reading the circle back from that copy's
    // alpha at each tap; and the combine reading it at its pixel, written over the scene under a mask that keeps the
    // scene's alpha. The circle is worked out by the same function in both, dofCoFUtilF.glsl's, and the copy carries
    // it as it is, in a float target, so the two differ in where the circle is read from alone.
    class DoFReferenceTree : public TestShaderDir
    {
    public:
        DoFReferenceTree()
        {
            const std::filesystem::path source = fsyspath(std::string(AL_TEST_SHADER_DIR) + "/class1");
            const std::filesystem::path copy   = fsyspath(prefix() + "1");
            std::error_code ec;
            std::filesystem::copy(source, copy,
                                  std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                                  ec);
            REQUIRE_FALSE(ec);

            REQUIRE(write("deferred/refCoFF.glsl", R"(
/*[EXTRA_CODE_HERE]*/
out vec4 frag_color;
uniform sampler2D diffuseRect;
uniform float max_cof;
in vec2 vary_fragcoord;
float dofCoF(vec2 tc);
void main()
{
    vec2 tc = vary_fragcoord.xy;
    frag_color.rgb = texture(diffuseRect, tc).rgb;
    frag_color.a = dofCoF(tc);
}
)"));

            REQUIRE(write("deferred/refDofCombineF.glsl", R"(
/*[EXTRA_CODE_HERE]*/
out vec4 frag_color;
uniform sampler2D diffuseRect;
uniform sampler2D lightMap;
uniform vec2 screen_res;
uniform float max_cof;
uniform float res_scale;
in vec2 vary_fragcoord;
// dofCombineF's dofBlur, the near field told from the sharp copy's circle.
vec3 refBlur(vec2 tc)
{
#if FRONT_BLUR
    return textureLod(diffuseRect, tc, 0.0).rgb;
#else
    vec2  size = vec2(textureSize(diffuseRect, 0));
    vec2  p    = tc * size - 0.5;
    vec2  f    = fract(p);
    ivec2 i0   = ivec2(floor(p));
    ivec2 hi   = ivec2(size) - 1;
    ivec2 t[4] = ivec2[4](i0, i0 + ivec2(1, 0), i0 + ivec2(0, 1), i0 + ivec2(1, 1));
    float w[4] = float[4]((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    vec3  sum      = vec3(0.0);
    float sum_w    = 0.0;
    vec3  filtered = vec3(0.0);
    for (int k = 0; k < 4; ++k)
    {
        ivec2 c = clamp(t[k], ivec2(0), hi);
        vec3  v = texelFetch(diffuseRect, c, 0).rgb;
        filtered += v * w[k];
        float wk = (texture(lightMap, (vec2(c) + 0.5) / size).a > 0.0) ? 0.0 : w[k];
        sum   += v * wk;
        sum_w += wk;
    }
    return (sum_w > 0.0) ? sum / sum_w : filtered;
#endif
}
void main()
{
    vec4 dof = vec4(refBlur(vary_fragcoord.xy), 0.0);
    vec4 diff = texture(lightMap, vary_fragcoord.xy);
#if FRONT_BLUR
    float a = min(abs(diff.a) * res_scale*res_scale, 1.0);
#else
    float a = (diff.a > 0.0) ? 0.0 : min(abs(diff.a) * res_scale*res_scale, 1.0);
#endif
    if (a > 0.25 && a < 0.75)
    {
        float sc = a/res_scale;
        vec4 col;
        col = texture(lightMap, vary_fragcoord.xy+vec2(sc,sc)/screen_res);
        col += texture(lightMap, vary_fragcoord.xy+vec2(-sc,sc)/screen_res);
        col += texture(lightMap, vary_fragcoord.xy+vec2(sc,-sc)/screen_res);
        col += texture(lightMap, vary_fragcoord.xy+vec2(-sc,-sc)/screen_res);
        diff = mix(diff, col*0.25, a);
    }
    frag_color = mix(diff, dof, a);
}
)"));

            // The gather as it read the circle from its source's alpha, one value per texel, under the same rule for
            // which taps a ring takes.
            std::error_code read_ec;
            std::string gather = LLFile::getContents(std::string(AL_TEST_SHADER_DIR) + "/class1/deferred/postDeferredF.glsl", read_ec);
            REQUIRE_FALSE(read_ec);
            replaceOnce(gather, "    float sc = abs(dofCoF(tc));\n", "    float sc = abs(s.a);\n");
            replaceOnce(gather, "    float sc = -dofCoF(tc);\n", "    float sc = -s.a;\n");
            replaceOnce(gather, "        float sc = dofCoF(tc);\n", "        float sc = diff.a;\n");
            REQUIRE(write("deferred/refPostDeferredF.glsl", gather));
        }

    private:
        static void replaceOnce(std::string& text, const std::string& from, const std::string& to)
        {
            const size_t at = text.find(from);
            CAPTURE(from);
            REQUIRE(at != std::string::npos);
            REQUIRE(text.find(from, at + 1) == std::string::npos);
            text.replace(at, from.size(), to);
        }
    };

    // The scene under reverse-Z, as the viewer renders it: the depth convention the programs compile against is set
    // before any of them is built, and the one before it put back after, for the tests the binary runs next on the
    // context it shares.
    struct ReverseZScope
    {
        ReverseZScope()
        {
            REQUIRE(gGLManager.mHasClipControl);
            set(true);
        }
        ~ReverseZScope() { set(mWas); }

        ReverseZScope(const ReverseZScope&)            = delete;
        ReverseZScope& operator=(const ReverseZScope&) = delete;

        static void set(bool reverse)
        {
            LLRender::sReverseZ = reverse;
            glClipControl(GL_LOWER_LEFT, reverse ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
            glClearDepth(reverse ? 0.0 : 1.0);
            LLGLDepthTest::rebase();
        }

        const bool mWas = LLRender::sReverseZ;
    };

    class DoFPasses
    {
    public:
        // The lens, as renderDoF reads it from its settings and the camera.
        struct Lens
        {
            F32 focus      = 16.f;  // metres
            F32 fnumber    = 9.f;   // CameraFNumber
            F32 focal      = 50.f;  // CameraFocalLength, mm
            F32 max_cof    = 10.f;  // CameraMaxCoF
            F32 res_scale  = 0.7f;  // CameraDoFResScale
            bool near_blur = false; // RenderDepthOfFieldNearBlur
            S32 blades     = 0;     // RenderBokehApertureBlades: 3 and up is the shaped gather
        };

        static constexpr F32 FOV        = 1.0471976f; // the camera's and CameraFieldOfView's, 60 degrees
        static constexpr F32 NEAR_PLANE = 0.1f;

        DoFPasses()
        {
            const auto deferred = [](LLShaderFeatures& features) { features.isDeferred = true; };
            const ShaderFiles vertex = { { "deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER } };
            const auto with = [&](std::initializer_list<const char*> fragments)
            {
                ShaderFiles files = vertex;
                for (const char* fragment : fragments)
                {
                    files.push_back({ fragment, GL_FRAGMENT_SHADER });
                }
                return files;
            };

            for (U32 near_blur = 0; near_blur < 2; ++near_blur)
            {
                for (U32 shaped = 0; shaped < 2; ++shaped)
                {
                    const Permutations variant = { { "FRONT_BLUR", near_blur ? "1" : "0" }, { "DOF_SHAPED", shaped ? "1" : "0" } };
                    mPost.build(mGather[near_blur][shaped], "Deferred Post Shader",
                                with({ "deferred/postDeferredF.glsl", "deferred/dofCoFUtilF.glsl" }), variant, deferred);
                    mPost.build(mRefGather[near_blur][shaped], "Deferred Post Shader (sharp copy)",
                                with({ "deferred/refPostDeferredF.glsl" }), variant, deferred);
                }
                const Permutations front = { { "FRONT_BLUR", near_blur ? "1" : "0" } };
                mPost.build(mCombine[near_blur], "Deferred DoFCombine Shader",
                            with({ "deferred/dofCombineF.glsl", "deferred/dofCoFUtilF.glsl" }), front, deferred);
                mPost.build(mRefCombine[near_blur], "Deferred DoFCombine Shader (sharp copy)",
                            with({ "deferred/refDofCombineF.glsl" }), front, deferred);
            }
            mPost.build(mRefCoF, "Deferred CoF Shader", with({ "deferred/refCoFF.glsl", "deferred/dofCoFUtilF.glsl" }), {},
                        deferred);
        }

        ~DoFPasses()
        {
            mPost.unloadPrograms();
            for (LLRenderTarget* target : { &mScene, &mDepth, &mSharp, &mBlur, &mOut, &mRefScene })
            {
                target->release();
            }
        }

        // A scene of width x height: the screen in RGBA16F, its colour up to a few times white with highlights well
        // past it and its alpha a glow tag; its depth in cells `cell` pixels across, each at its own distance between
        // half a metre and two hundred, so the circle of confusion steps at every cell's edge. The sharp copy in
        // `sharp_format` and the blur in `blur_format`, as wide as the lens's resolution scale makes it.
        void setUp(U32 width, U32 height, const Lens& lens, U32 cell = 8, U32 sharp_format = GL_RGBA32F,
                   U32 blur_format = GL_RGBA32F)
        {
            mLens = lens;
            REQUIRE(mScene.allocate(width, height, GL_RGBA16F));
            REQUIRE(mRefScene.allocate(width, height, GL_RGBA16F));
            REQUIRE(mOut.allocate(width, height, GL_RGBA16F));
            REQUIRE(mDepth.allocate(width, height, 0, true, true, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                                    LLRenderTarget::DEPTH_FMT_32F));
            REQUIRE(mSharp.allocate(width, height, sharp_format));
            mBlurWidth  = std::max(1u, (U32)(width * llmin(lens.res_scale, 1.f)));
            mBlurHeight = std::max(1u, (U32)(height * llmin(lens.res_scale, 1.f)));
            REQUIRE(mBlur.allocate(mBlurWidth, mBlurHeight, blur_format));

            std::vector<F32> colour = PostChainGL::pattern(width, height, 7);
            for (size_t i = 0; i < colour.size(); i += 4)
            {
                for (size_t c = 0; c < 3; ++c)
                {
                    colour[i + c] *= 3.f;
                }
                if (colour[i + 3] > 0.97f)
                {
                    colour[i] = colour[i + 1] = colour[i + 2] = 40.f;
                }
            }
            PostChainGL::write(mScene, colour);

            mProjection = al_perspective(FOV, (F32)width / (F32)height, NEAR_PLANE, std::numeric_limits<F32>::infinity());
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(mProjection);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();

            mDepth.bindTarget();
            {
                LLGLDepthTest depth(GL_TRUE, GL_TRUE);
                for (U32 y = 0; y < height; y += cell)
                {
                    for (U32 x = 0; x < width; x += cell)
                    {
                        uint32_t h = (x / cell) * 2654435761u ^ (y / cell) * 40503u;
                        h ^= h >> 15;
                        h *= 0x2c1b3c6du;
                        h ^= h >> 12;
                        const F32 t = (F32)(h & 0xffff) / 65535.f;
                        LLGLSScissor scissor(x, y, cell, cell);
                        glClearDepth(storedDepth(0.5f * std::pow(400.f, t)));
                        mDepth.clear(GL_DEPTH_BUFFER_BIT);
                    }
                }
                glClearDepth(0.0);
            }
            mDepth.flush();
        }

        // The circle of confusion's uniforms as renderDoF works them out, and the gather's own.
        void setCoFUniforms(LLGLSLShader& program) const
        {
            const F32 subject = mLens.focus * 1000.f;
            const F32 dv      = 2.f * mLens.focal * tanf(FOV / 2.f);
            const F32 focal   = dv / (2.f * tanf(FOV / 2.f));
            const F32 blur_constant = focal * focal / (mLens.fnumber * (subject - focal)) / 1000.f;
            const F32 magnification = focal / (subject - focal);
            const F32 pixel_angle   = (F32)mScene.getHeight() / FOV;
            program.uniform1f(LLShaderMgr::DOF_FOCAL_DISTANCE, -subject / 1000.f);
            program.uniform1f(LLShaderMgr::DOF_BLUR_CONSTANT, blur_constant);
            program.uniform1f(LLShaderMgr::DOF_TAN_PIXEL_ANGLE, tanf(1.f / pixel_angle));
            program.uniform1f(LLShaderMgr::DOF_MAGNIFICATION, magnification);
            program.uniform1f(LLShaderMgr::DOF_MAX_COF, mLens.max_cof);
            program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)mScene.getWidth(), (F32)mScene.getHeight());
        }

        void setGatherUniforms(LLGLSLShader& program) const
        {
            program.uniform1f(LLShaderMgr::BOKEH_HIGHLIGHT_THRESHOLD, 0.f);
            program.uniform1f(LLShaderMgr::BOKEH_HIGHLIGHT_GAIN, 0.f);
            program.uniform1f(LLShaderMgr::BOKEH_HIGHLIGHT_CLAMP, 64.f);
            if (shaped())
            {
                const F32 half_sector = F_PI / (F32)llmax(mLens.blades, 3);
                program.uniform1i(LLShaderMgr::BOKEH_BLADES, mLens.blades);
                program.uniform1f(LLShaderMgr::BOKEH_APERTURE_ROTATION, 0.3f);
                program.uniform1f(LLShaderMgr::BOKEH_APERTURE_CURVATURE, 0.25f);
                program.uniform3f(LLShaderMgr::BOKEH_APERTURE_CONST, half_sector, 2.f * half_sector, cosf(half_sector));
                program.uniform2f(LLShaderMgr::BOKEH_ANAMORPHIC, 1.f, 1.f);
                program.uniform1f(LLShaderMgr::BOKEH_CAT_EYE, 0.3f);
                program.uniform1f(LLShaderMgr::BOKEH_FRINGE_AMOUNT, 0.2f);
                program.uniform3f(LLShaderMgr::BOKEH_FRINGE_NEAR_TINT, 1.f, 0.85f, 1.f);
                program.uniform3f(LLShaderMgr::BOKEH_FRINGE_FAR_TINT, 0.85f, 1.f, 0.9f);
                program.uniform1f(LLShaderMgr::BOKEH_SPHERICAL, 0.4f);
                program.uniform1f(LLShaderMgr::BOKEH_FIELD_STRETCH, 0.2f);
                program.uniform1f(LLShaderMgr::BOKEH_FIELD_FALLOFF, 2.f);
                program.uniform1f(LLShaderMgr::BOKEH_COMA_ASYMMETRY, 0.3f);
            }
        }

        F32 resScale() const
        {
            return llmin((F32)mBlurWidth / (F32)mScene.getWidth(), (F32)mBlurHeight / (F32)mScene.getHeight());
        }

        // renderDoF's gather and combine: from the scene and its depth into the blur, then the defocused scene into
        // mOut, the scene's alpha carried through.
        void drawFromDepth()
        {
            LLGLDisable blend(GL_BLEND);
            LLGLDepthTest depth(GL_FALSE);
            LLGLSLShader& gather = mGather[mLens.near_blur][shaped()];
            mBlur.bindTarget();
            gather.bind();
            gather.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &mScene, ALSamplers::PointMirror);
            gather.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &mDepth, ALSamplers::PointMirror);
            setCoFUniforms(gather);
            setGatherUniforms(gather);
            mPost.drawTriangle();
            gather.unbind();
            mBlur.flush();

            LLGLSLShader& combine = mCombine[mLens.near_blur];
            mOut.bindTarget();
            combine.bind();
            combine.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &mBlur, ALSamplers::PointMirror);
            combine.bindTexture(LLShaderMgr::DEFERRED_LIGHT, &mScene, ALSamplers::PointMirror);
            combine.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &mDepth, ALSamplers::PointMirror);
            setCoFUniforms(combine);
            combine.uniform1f(LLShaderMgr::DOF_RES_SCALE, resScale());
            mPost.drawTriangle();
            combine.unbind();
            mOut.flush();
        }

        // The circle carried in a sharp copy: the scene and its circle into the copy, the gather from that, and the
        // combine back over a copy of the scene, colour only.
        void drawFromSharpCopy()
        {
            LLGLDisable blend(GL_BLEND);
            LLGLDepthTest depth(GL_FALSE);
            mRefScene.copyContents(mScene, 0, 0, mScene.getWidth(), mScene.getHeight(), 0, 0, mScene.getWidth(),
                                   mScene.getHeight(), GL_COLOR_BUFFER_BIT, GL_NEAREST);

            mSharp.bindTarget();
            mRefCoF.bind();
            mRefCoF.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &mScene, ALSamplers::PointMirror);
            mRefCoF.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &mDepth);
            setCoFUniforms(mRefCoF);
            mPost.drawTriangle();
            mRefCoF.unbind();
            mSharp.flush();

            LLGLSLShader& gather = mRefGather[mLens.near_blur][shaped()];
            mBlur.bindTarget();
            gather.bind();
            gather.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &mSharp, ALSamplers::PointMirror);
            setCoFUniforms(gather);
            setGatherUniforms(gather);
            mPost.drawTriangle();
            gather.unbind();
            mBlur.flush();

            LLGLSLShader& combine = mRefCombine[mLens.near_blur];
            mRefScene.bindTarget();
            gGL.setColorMask(true, false);
            combine.bind();
            combine.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &mBlur, ALSamplers::PointMirror);
            combine.bindTexture(LLShaderMgr::DEFERRED_LIGHT, &mSharp, ALSamplers::PointMirror);
            setCoFUniforms(combine);
            combine.uniform1f(LLShaderMgr::DOF_RES_SCALE, resScale());
            mPost.drawTriangle();
            combine.unbind();
            mRefScene.flush();
            gGL.setColorMask(true, true);
        }

        bool shaped() const { return mLens.blades >= 3; }

        // The depth the projection stores for a surface `dist` metres in front of the eye.
        F32 storedDepth(F32 dist) const
        {
            LLVector4a clip;
            mProjection.transform4(LLVector4a(0.f, 0.f, -dist, 1.f), clip);
            return clip[2] / clip[3];
        }

        SharedGLScope mGL;
        ReverseZScope mReverseZ;
        DoFReferenceTree mTree;
        PostChainGL mPost{ mTree.prefix() };
        LLGLSLShader mGather[2][2];
        LLGLSLShader mRefGather[2][2];
        LLGLSLShader mCombine[2];
        LLGLSLShader mRefCombine[2];
        LLGLSLShader mRefCoF;
        Lens mLens;
        LLMatrix4a mProjection;
        U32 mBlurWidth = 1;
        U32 mBlurHeight = 1;
        LLRenderTarget mScene;
        LLRenderTarget mDepth;
        LLRenderTarget mSharp;
        LLRenderTarget mBlur;
        LLRenderTarget mOut;
        LLRenderTarget mRefScene;
    };
}

#endif // AL_DOF_GL_FIXTURE_H
