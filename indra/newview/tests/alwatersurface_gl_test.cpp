/**
 * @file alwatersurface_gl_test.cpp
 * @brief The water surface's draws on the hidden window, and the water's stencil bits: where the exclusion pass
 *        removes the water, and which pixels the water shades, which the haze pass reads.
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

#include "../alwaterfield.h"
#include "../alwaterstencil.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llvertexbuffer.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include "../test/lltut.h"

#include <map>
#include <string>
#include <vector>

namespace tut
{
    namespace
    {
        constexpr S32 W = 16;
        constexpr S32 H = 16;

        // A surface of one colour, placed by the matrices.
        const char* kColourVertex =
            "#version 330\n"
            "layout (std140) uniform Matrices\n"
            "{\n"
            "    mat4 modelview_matrix;\n"
            "    mat4 projection_matrix;\n"
            "    mat4 modelview_projection_matrix;\n"
            "    mat4 inv_proj;\n"
            "    mat4 texture_matrix0;\n"
            "    mat3 normal_matrix;\n"
            "};\n"
            "in vec3 position;\n"
            "void main() { gl_Position = modelview_projection_matrix * vec4(position, 1.0); }\n";

        const char* kColourFragment =
            "#version 330\n"
            "uniform vec4 color;\n"
            "out vec4 frag_color;\n"
            "void main() { frag_color = color; }\n";

        // The water scene: 512 m of the plane square across the target, 32 m a pixel, agent space at the global
        // origin. Four regions, one a cell: the south-east one's water at 30 m, the others' at 20 m.
        constexpr F32 SPAN = 512.f;
        constexpr F64 CELL = 256.0;
        constexpr F32 LOW = 20.f;
        constexpr F32 HIGH = 30.f;
        constexpr F32 NEAR_Z = 1.f;
        constexpr F32 FAR_Z = 200.f;

        const LLColor4 GROUND(0.5f, 0.5f, 0.5f, 1.f);
        const LLColor4 WATER(0.f, 0.f, 1.f, 1.f);

        // A camera at height `z` looking straight down, or up: eye space is agent space moved down to the camera
        // and, looking up, turned half over about x, so y and z run the other way.
        LLMatrix4a view(F32 z, bool up)
        {
            const F32 s = up ? -1.f : 1.f;
            LLMatrix4a m;
            m.setRow<0>(LLVector4a(1.f, 0.f, 0.f, 0.f));
            m.setRow<1>(LLVector4a(0.f, s, 0.f, 0.f));
            m.setRow<2>(LLVector4a(0.f, 0.f, s, 0.f));
            m.setRow<3>(LLVector4a(0.f, 0.f, -s * z, 1.f));
            return m;
        }

        // The orthographic projection of [0, SPAN] square onto the target, from that camera.
        LLMatrix4a ortho(bool up)
        {
            const F32 y0 = up ? -SPAN : 0.f;
            LLMatrix4a m;
            m.setRow<0>(LLVector4a(2.f / SPAN, 0.f, 0.f, 0.f));
            m.setRow<1>(LLVector4a(0.f, 2.f / SPAN, 0.f, 0.f));
            m.setRow<2>(LLVector4a(0.f, 0.f, -2.f / (FAR_Z - NEAR_Z), 0.f));
            m.setRow<3>(LLVector4a(-1.f, -1.f - 2.f * y0 / SPAN, -(FAR_Z + NEAR_Z) / (FAR_Z - NEAR_Z), 1.f));
            return m;
        }

        std::vector<ALWaterField::Region> regions()
        {
            return { { 0, 0, 1, LOW }, { 1, 0, 1, HIGH }, { 0, 1, 1, LOW }, { 1, 1, 1, LOW } };
        }

    struct alwatersurface_data
    {
        // Each test starts with identity matrices, whatever ran before.
        alwatersurface_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
        }

        // The viewport, the matrices, the shader and the clear colour the tests set go back with mGL.
        ~alwatersurface_data()
        {
            if (mScene.isBoundInStack())
            {
                mScene.flush();
            }
            LLGLSLShader::unbind();
            mColour.unload();
            mExclusion.unload();
            mShaders.clearShaderObjects();
            mCellMap = nullptr;
            mScene.release();
        }

        // The water scene in a target whose depth carries stencil, cleared: the colour program, the viewer's own
        // exclusion program, and the cell map ALWaterSurface would make for these regions.
        void setUpWater(LLRenderTarget::eDepthFormat format)
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kColourVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kColourFragment);
            ensure("colour shaders compile", vs && fs);
            mColour.mProgramObject = glCreateProgram();
            mColour.mName = "colour (test)";
            mColour.attachObject(vs);
            mColour.attachObject(fs);
            ensure("colour program links", mColour.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            ensure("colour program maps", mColour.mapUniforms());

            std::map<std::string, std::string> defines;
            for (const char* object : { "deferred/textureUtilV.glsl", "objects/nonindexedTextureV.glsl" })
            {
                S32 level = 1;
                ensure(std::string("compiled ") + object, mShaders.loadShaderFile(object, level, GL_VERTEX_SHADER, &defines) != 0);
            }
            for (const char* object : { "deferred/globalF.glsl", "environment/waterSurface.glsl" })
            {
                S32 level = 1;
                ensure(std::string("compiled ") + object, mShaders.loadShaderFile(object, level, GL_FRAGMENT_SHADER, &defines) != 0);
            }
            mExclusion.mName = "Water Exclusion Shader";
            mExclusion.mFeatures.hasWaterSurface = true;
            mExclusion.mShaderFiles.clear();
            mExclusion.mShaderFiles.push_back(std::make_pair("environment/waterExclusionV.glsl", GL_VERTEX_SHADER));
            mExclusion.mShaderFiles.push_back(std::make_pair("environment/waterExclusionF.glsl", GL_FRAGMENT_SHADER));
            mExclusion.mShaderLevel = 1;
            ensure("exclusion program builds", mExclusion.createShader());

            // The camera over cell (1, 1), the agent region's origin at the global one.
            const std::vector<ALWaterField::Region> cells = regions();
            mLayout = ALWaterField::layout(300.0, 300.0, CELL, 256.f, 256000.0, cells, 0.0);
            mSquare = ALWaterField::cellSquare(mLayout, 0.0, 0.0);
            std::vector<F32> texels;
            ALWaterField::fillCells(mLayout, cells, LOW, texels);
            mCellMap = new LLImageGL(ALWaterField::MAP_SIZE, ALWaterField::MAP_SIZE, 2, false);
            mCellMap->setExplicitFormat(GL_RG32F, GL_RG, GL_FLOAT);
            ensure("cell map", mCellMap->createGLTexture(0, reinterpret_cast<const U8*>(texels.data())));

            ensure("scene allocates", mScene.allocate(W, H, GL_RGBA8, true, true, ALTextureSlot::TT_TEXTURE,
                                                      LLRenderTarget::MIPS_NONE, format));
            ensure("the scene has stencil", mScene.hasStencil());
            mScene.bindTarget();
            gGL.setClearColor(LLColor4::black);
            mScene.clear();
        }

        // The camera both programs draw from.
        void look(F32 z, bool up)
        {
            mCameraZ = z;
            mView = view(z, up);
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(ortho(up));
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadMatrix(mView);
        }

        void quad(F32 x0, F32 x1, F32 y0, F32 y1, F32 z)
        {
            LLGLDisable cull(GL_CULL_FACE);
            gGL.begin(LLRender::TRIANGLES);
            gGL.vertex3f(x0, y0, z);
            gGL.vertex3f(x1, y0, z);
            gGL.vertex3f(x1, y1, z);
            gGL.vertex3f(x0, y0, z);
            gGL.vertex3f(x1, y1, z);
            gGL.vertex3f(x0, y1, z);
            gGL.end();
            gGL.flush();
        }

        // Opaque scene geometry over the whole span: depth written.
        void ground(F32 z)
        {
            opaque(0.f, SPAN, 0.f, SPAN, z);
        }

        void opaque(F32 x0, F32 x1, F32 y0, F32 y1, F32 z)
        {
            LLGLDepthTest depth(GL_TRUE, GL_TRUE);
            mColour.bind();
            mColour.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, GROUND.mV);
            quad(x0, x1, y0, y1, z);
        }

        // A PASS_INVISIBLE prim's face, in the state LLDrawPoolWaterExclusion::render draws them in.
        void exclusion(F32 x0, F32 x1, F32 y0, F32 y1, F32 z)
        {
            mExclusion.bind();
            const S32 channel = mExclusion.enableTexture(LLShaderMgr::WATER_CELL_MAP);
            if (channel > -1)
            {
                gGL.getTextureSlot(channel)->bindSampled(mCellMap.get(), ALSamplers::PointClamp);
            }
            mExclusion.uniform4fv(LLShaderMgr::WATER_CELL_SQUARE, 1, mSquare.mV);
            mExclusion.uniform1f(LLShaderMgr::WATER_DEFAULT_HEIGHT, LOW);
            LLMatrix4a eye_to_agent;
            eye_to_agent.setAffineInverse(mView);
            mExclusion.uniformMatrix4fv(LLShaderMgr::INVERSE_MODELVIEW_MATRIX, eye_to_agent);
            mExclusion.uniform3f(LLShaderMgr::WL_CAMPOSLOCAL, 300.f, 300.f, mCameraZ);

            LLGLDepthTest depth(GL_TRUE, GL_FALSE);
            LLGLSColorMask no_color(false, false);
            ALWaterStencil::Exclude stencil;
            quad(x0, x1, y0, y1, z);
        }

        // The water over the four regions at their heights, in `colour`.
        void water(const LLColor4& colour)
        {
            LLGLDepthTest depth(GL_TRUE, GL_FALSE);
            mColour.bind();
            mColour.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, colour.mV);
            for (const ALWaterField::Region& cell : regions())
            {
                const F32 x0 = (F32)(cell.mCellX * CELL);
                const F32 y0 = (F32)(cell.mCellY * CELL);
                quad(x0, x0 + (F32)CELL, y0, y0 + (F32)CELL, cell.mWaterHeight);
            }
        }

        // The water as LLDrawPoolWater draws it: where no exclusion prim removed it, marking where it shades.
        void waterUnderStencil()
        {
            ALWaterStencil::Surface stencil;
            water(WATER);
        }

        // A quad of water in the pool's state, at any height: a crest where it is above the plane.
        void waterQuad(F32 x0, F32 x1, F32 y0, F32 y1, F32 z)
        {
            LLGLDepthTest depth(GL_TRUE, GL_FALSE);
            ALWaterStencil::Surface stencil;
            mColour.bind();
            mColour.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, WATER.mV);
            quad(x0, x1, y0, y1, z);
        }

        // The haze pass's draw over the whole target, depth untested, adding `colour`.
        void hazeDraw(const LLColor4& colour)
        {
            LLGLDepthTest depth(GL_FALSE);
            mColour.bind();
            mColour.uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, colour.mV);
            quad(0.f, SPAN, 0.f, SPAN, 50.f);
        }

        LLColor4U pixel(S32 x, S32 y)
        {
            U8 rgba[4] = {};
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            return LLColor4U(rgba[0], rgba[1], rgba[2], rgba[3]);
        }

        bool wet(S32 x, S32 y)
        {
            const LLColor4U c = pixel(x, y);
            return c.mV[VBLUE] == 255 && c.mV[VRED] == 0;
        }

        U8 stencil(S32 x, S32 y)
        {
            U8 value = 0;
            glReadPixels(x, y, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &value);
            return value;
        }

        static std::vector<LLRenderTarget::eDepthFormat> formats()
        {
            return { LLRenderTarget::DEPTH_FMT_24, LLRenderTarget::DEPTH_FMT_32F };
        }

        static std::string named(LLRenderTarget::eDepthFormat format, const std::string& what)
        {
            return what + (format == LLRenderTarget::DEPTH_FMT_32F ? " (32F)" : " (24)");
        }

        void tearDownWater()
        {
            mScene.flush();
            LLGLSLShader::unbind();
            mColour.unload();
            mExclusion.unload();
            mShaders.clearShaderObjects();
            mCellMap = nullptr;
            mScene.release();
        }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;

        LLGLSLShader           mColour;
        LLGLSLShader           mExclusion;
        LLRenderTarget         mScene;
        LLPointer<LLImageGL>   mCellMap;
        ALWaterField::Layout   mLayout;
        LLVector4              mSquare;
        LLMatrix4a             mView;
        F32                    mCameraZ = 0.f;
    };

    typedef test_group<alwatersurface_data> alwatersurface_t;
    typedef alwatersurface_t::object alwatersurface_object_t;
    tut::alwatersurface_t tut_alwatersurface("alwatersurface_gl");
    }

    // From above, an exclusion prim over the water removes the water behind it; one under the water removes nothing,
    // since the water hides it.
    template<> template<>
    void alwatersurface_object_t::test<1>()
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            exclusion(0.f, 256.f, 0.f, SPAN, LOW + 5.f);
            exclusion(256.f, SPAN, 256.f, SPAN, LOW - 5.f);
            waterUnderStencil();
            for (S32 y : { 2, 12 })
            {
                ensure(named(format, "a prim above the water removes it, row " + std::to_string(y)), !wet(4, y));
            }
            ensure(named(format, "a prim under the water removes nothing"), wet(12, 12));
            ensure(named(format, "nor does nothing"), wet(12, 4));
            tearDownWater();
        }
    }

    // From below, the reverse: the prim under the water, on the camera's side, removes it; the one above it does not.
    template<> template<>
    void alwatersurface_object_t::test<2>()
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(5.f, true);
            ground(60.f);
            exclusion(0.f, 256.f, 0.f, SPAN, LOW + 5.f);
            exclusion(256.f, SPAN, 0.f, SPAN, LOW - 5.f);
            waterUnderStencil();
            for (S32 y : { 2, 12 })
            {
                ensure(named(format, "a prim above the water from below removes nothing, row " + std::to_string(y)), wet(4, y));
                ensure(named(format, "a prim under it does, row " + std::to_string(y)), !wet(12, y));
            }
            tearDownWater();
        }
    }

    // A prim behind opaque geometry marks nothing: the pass tests the scene's depth.
    template<> template<>
    void alwatersurface_object_t::test<3>()
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            opaque(0.f, 256.f, 0.f, SPAN, 50.f);
            exclusion(0.f, SPAN, 0.f, SPAN, HIGH + 5.f);
            for (S32 y : { 2, 12 })
            {
                ensure_equals(named(format, "behind the wall, row " + std::to_string(y)), (U32)stencil(4, y), 0U);
                ensure_equals(named(format, "in the open, row " + std::to_string(y)), (U32)stencil(12, y),
                              ALWaterStencil::EXCLUDE_BIT);
            }
            tearDownWater();
        }
    }

    // A prim over two regions whose water lies at different heights tests each part against its own: at 25 m it is
    // above the west region's 20 m water and under the east one's 30 m.
    template<> template<>
    void alwatersurface_object_t::test<4>()
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            exclusion(0.f, SPAN, 0.f, 256.f, 25.f);
            waterUnderStencil();
            ensure(named(format, "over the 20 m water it removes the water"), !wet(4, 4));
            ensure(named(format, "under the 30 m water it removes nothing"), wet(12, 4));
            ensure(named(format, "where it is not, the water is"), wet(4, 12) && wet(12, 12));
            tearDownWater();
        }
    }

    // From below, the surface is drawn twice, once where the exclude bit is clear and once where it is set, and each
    // pixel takes exactly one of them.
    template<> template<>
    void alwatersurface_object_t::test<5>()
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(5.f, true);
            ground(60.f);
            exclusion(256.f, SPAN, 0.f, SPAN, LOW - 5.f);
            {
                LLGLEnable blend(GL_BLEND);
                gGL.blendFunc(LLRender::BF_ONE, LLRender::BF_ONE);
                {
                    ALWaterStencil::Surface state;
                    water(LLColor4(1.f, 0.f, 0.f, 0.f));
                }
                {
                    ALWaterStencil::Excluded state;
                    water(LLColor4(0.f, 1.f, 0.f, 0.f));
                }
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
            }
            for (S32 x = 0; x < W; ++x)
            {
                const LLColor4U c = pixel(x, 6);
                const bool distorted = c.mV[VRED] == 255;
                const bool undistorted = c.mV[VGREEN] == 255;
                ensure(named(format, "column " + std::to_string(x) + " drawn once"), distorted != undistorted);
                ensure(named(format, "column " + std::to_string(x) + " undistorted where excluded"), undistorted == (x >= 8));
            }
            tearDownWater();
        }
    }

    // The next frame's clear empties what the pass marked: the stencil states hand the write mask back whole, and a
    // clear writes stencil only through it.
    template<> template<>
    void alwatersurface_object_t::test<6>()
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            exclusion(0.f, SPAN, 0.f, 256.f, HIGH + 5.f);
            waterUnderStencil();
            ensure_equals(named(format, "excluded"), (U32)stencil(8, 4), ALWaterStencil::EXCLUDE_BIT);
            ensure_equals(named(format, "water"), (U32)stencil(8, 12), ALWaterStencil::SURFACE_BIT);
            mScene.clear();
            ensure_equals(named(format, "excluded, cleared"), (U32)stencil(8, 4), 0U);
            ensure_equals(named(format, "water, cleared"), (U32)stencil(8, 12), 0U);
            tearDownWater();
        }
    }

    // The water marks the pixels it shades and no others: a crest a metre and a half above the plane is marked, a box
    // whose top lies two centimetres above it is not, and nor is a pixel an exclusion prim removed the water from. A
    // test of the depth against the plane would take the box for water and the crest for not.
    template<> template<>
    void alwatersurface_object_t::test<7>()
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            opaque(0.f, 128.f, 256.f, SPAN, LOW + 0.02f);
            exclusion(128.f, 256.f, 256.f, SPAN, LOW + 5.f);
            waterQuad(0.f, 256.f, 256.f, SPAN, LOW);
            waterQuad(256.f, SPAN, 256.f, SPAN, LOW + 1.5f);
            for (S32 x = 0; x < W; ++x)
            {
                const U32 bits = stencil(x, 12);
                const std::string column = named(format, "column " + std::to_string(x));
                ensure_equals(column + " marked as water", (bits & ALWaterStencil::SURFACE_BIT) != 0, x >= 8);
                ensure_equals(column + " excluded", (bits & ALWaterStencil::EXCLUDE_BIT) != 0, x >= 4 && x < 8);
            }
            tearDownWater();
        }
    }

    // The haze pass's two draws, one where the surface bit is clear and one where it is set, shade every pixel once,
    // the second exactly where the water is.
    template<> template<>
    void alwatersurface_object_t::test<8>()
    {
        for (LLRenderTarget::eDepthFormat format : formats())
        {
            setUpWater(format);
            look(100.f, false);
            ground(0.f);
            exclusion(0.f, 256.f, 0.f, SPAN, LOW + 5.f);
            waterUnderStencil();
            mScene.clear(GL_COLOR_BUFFER_BIT);
            {
                LLGLEnable blend(GL_BLEND);
                gGL.blendFunc(LLRender::BF_ONE, LLRender::BF_ONE);
                {
                    ALWaterStencil::OffWater state;
                    hazeDraw(LLColor4(1.f, 0.f, 0.f, 0.f));
                }
                {
                    ALWaterStencil::OnWater state;
                    hazeDraw(LLColor4(0.f, 1.f, 0.f, 0.f));
                }
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
            }
            for (S32 x = 0; x < W; ++x)
            {
                const LLColor4U c = pixel(x, 10);
                const bool plain = c.mV[VRED] == 255;
                const bool on_water = c.mV[VGREEN] == 255;
                const std::string column = named(format, "column " + std::to_string(x));
                ensure(column + " hazed once", plain != on_water);
                ensure_equals(column + " converged where the water is", on_water, x >= 8);
            }
            tearDownWater();
        }
    }
}
