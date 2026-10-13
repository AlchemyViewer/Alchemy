/**
 * @file alimageslot_gl_test.cpp
 * @brief ALImageSlot on the hidden window: what a unit issues to GL and what GL then holds, a redundant bind issuing
 *        nothing, refreshState putting back what was changed behind LLRender's back, forgetTextures dropping a
 *        deleted name, the formats an image cannot take, and integer textures loaded and stored as images.
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

#include "../alimageslot.h"
#include "../altexture3d.h"
#include "../llgl.h"
#include "../llglheaders.h"
#include "../llimagegl.h"
#include "../llrender.h"
#include "../llrendertarget.h"

#include "llheadlessgl_fixture.h"

#include "llimage.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

namespace
{
        constexpr U32 SIZE = 8;

        // What GL holds at image unit `unit`.
        struct UnitState
        {
            GLint mTexture = -1;
            GLint mLevel   = -1;
            GLint mLayered = -1;
            GLint mLayer   = -1;
            GLint mAccess  = -1;
            GLint mFormat  = -1;
        };

        UnitState unitState(U32 unit)
        {
            UnitState state;
            glGetIntegeri_v(GL_IMAGE_BINDING_NAME, unit, &state.mTexture);
            glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL, unit, &state.mLevel);
            glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED, unit, &state.mLayered);
            glGetIntegeri_v(GL_IMAGE_BINDING_LAYER, unit, &state.mLayer);
            glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, unit, &state.mAccess);
            glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, unit, &state.mFormat);
            return state;
        }

        // Each texel of the r32ui image at unit 0 loaded, tripled, offset by its place and stored back.
        const char* kUintKernel =
            "#version 430\n"
            "layout(local_size_x = 8, local_size_y = 8) in;\n"
            "layout(binding = 0, r32ui) uniform uimage2D target;\n"
            "void main()\n"
            "{\n"
            "    ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
            "    if (any(greaterThanEqual(p, imageSize(target))))\n"
            "    {\n"
            "        return;\n"
            "    }\n"
            "    uint v = imageLoad(target, p).r;\n"
            "    imageStore(target, p, uvec4(v * 3u + uint(p.x + p.y * 100), 0u, 0u, 0u));\n"
            "}\n";

        // An R32UI texture's level 0, as GL holds it.
        std::vector<U32> readUints(U32 texture, U32 width, U32 height)
        {
            std::vector<U32> texels((size_t)width * height, 0x55555555u);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, texture);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, texels.data());
            gGL.getTextureSlot(0)->unbind();
            return texels;
        }

    // The program and texture slots the tests change, and compute's switch, go back with mGL; the image units are
    // unbound here.
    struct alimageslot_data
    {
        ~alimageslot_data()
        {
            for (U32 unit = 0; unit < AL_NUM_IMAGE_SLOTS; ++unit)
            {
                gGL.getImageSlot(unit)->unbind();
            }
            mTarget.release();
            mOther.release();
            mUint.release();
            if (mKernel)
            {
                glUseProgram(0);
                glDeleteProgram(mKernel);
            }
        }

        // Compute in use, an R32F target with a mip level beyond the first and an RGBA16F one without, or the case
        // skipped where the context has no compute.
        void require_targets()
        {
            if (!ll_test::contextHasCompute())
            {
                SKIP("the context has no compute");
            }
            LLRender::sCompute = true;
            REQUIRE(mTarget.allocate(SIZE, SIZE, GL_R32F, false, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_MANUAL));
            REQUIRE(mOther.allocate(SIZE, SIZE, GL_RGBA16F));
        }

        ll_test::SharedGLScope mGL;
        LLRenderTarget mTarget;
        LLRenderTarget mOther;
        LLRenderTarget mUint;
        GLuint mKernel = 0;
    };


    // A bind reaches GL as stated, a bind of what the unit already holds issues nothing, a change of any part of it
    // issues one call, and unbind returns the unit to texture 0 once.
    TEST_CASE_METHOD(alimageslot_data, "alimageslot_gl: a bind reaches GL as stated and only a change issues a call", "[alimageslot_gl]")
    {
        require_targets();
        ALImageSlot* slot = gGL.getImageSlot(2);
        CHECK(slot->getIndex() == 2);

        U32 binds = ALImageSlot::sImageBinds;
        CHECK(slot->bind(&mTarget, 0, ALImageSlot::READ_WRITE, 1));
        UNSCOPED_INFO("the bind issues one call");
        CHECK(ALImageSlot::sImageBinds == binds + 1);
        UnitState state = unitState(2);
        CHECK(state.mTexture == (GLint)mTarget.getTexture());
        CHECK(state.mLevel == 1);
        CHECK(state.mLayered == GL_FALSE);
        CHECK(state.mAccess == GL_READ_WRITE);
        UNSCOPED_INFO("at the target's own format");
        CHECK(state.mFormat == GL_R32F);
        CHECK(slot->getCurrTexture() == mTarget.getTexture());

        binds = ALImageSlot::sImageBinds;
        CHECK(slot->bind(&mTarget, 0, ALImageSlot::READ_WRITE, 1));
        UNSCOPED_INFO("the same bind issues nothing");
        CHECK(ALImageSlot::sImageBinds == binds);

        CHECK(slot->bind(&mTarget, 0, ALImageSlot::READ_ONLY, 1));
        UNSCOPED_INFO("a new access issues one call");
        CHECK(ALImageSlot::sImageBinds == binds + 1);
        CHECK(unitState(2).mAccess == GL_READ_ONLY);

        CHECK(slot->bind(&mTarget, 0, ALImageSlot::READ_ONLY, 0));
        UNSCOPED_INFO("a new level issues one call");
        CHECK(ALImageSlot::sImageBinds == binds + 2);
        CHECK(unitState(2).mLevel == 0);

        CHECK(slot->bind(&mOther, 0, ALImageSlot::READ_ONLY, 0));
        UNSCOPED_INFO("a new texture issues one call");
        CHECK(ALImageSlot::sImageBinds == binds + 3);
        state = unitState(2);
        CHECK(state.mTexture == (GLint)mOther.getTexture());
        UNSCOPED_INFO("at its own format");
        CHECK(state.mFormat == GL_RGBA16F);

        slot->unbind();
        UNSCOPED_INFO("unbind issues one call");
        CHECK(ALImageSlot::sImageBinds == binds + 4);
        CHECK(unitState(2).mTexture == 0);
        CHECK(slot->getCurrTexture() == 0u);
        slot->unbind();
        UNSCOPED_INFO("unbinding nothing issues nothing");
        CHECK(ALImageSlot::sImageBinds == binds + 4);

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A unit changed behind LLRender's back is put back by refreshState, every part of it.
    TEST_CASE_METHOD(alimageslot_data, "alimageslot_gl: refreshState puts back every part of a unit changed behind its back", "[alimageslot_gl]")
    {
        require_targets();
        CHECK(gGL.getImageSlot(3)->bind(&mTarget, 0, ALImageSlot::WRITE_ONLY, 1));
        CHECK(gGL.getImageSlot(4)->bind(&mOther, 0, ALImageSlot::READ_ONLY));

        glBindImageTexture(3, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R8);
        glBindImageTexture(4, mTarget.getTexture(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);
        UNSCOPED_INFO("clobbered");
        CHECK(unitState(3).mTexture == 0);

        gGL.refreshState();

        UnitState state = unitState(3);
        {
            INFO("unit 3 put back");
            CHECK(state.mTexture == (GLint)mTarget.getTexture());
            CHECK(state.mLevel == 1);
            CHECK(state.mAccess == GL_WRITE_ONLY);
            CHECK(state.mFormat == GL_R32F);
        }
        state = unitState(4);
        {
            INFO("unit 4 put back");
            CHECK(state.mTexture == (GLint)mOther.getTexture());
            CHECK(state.mAccess == GL_READ_ONLY);
            CHECK(state.mFormat == GL_RGBA16F);
        }

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // forgetTextures drops a unit holding a name about to be deleted, and only that one, so the name bound again
    // afterwards -- which GL is free to hand to a new texture -- is bound rather than skipped. And deleting a target
    // the viewer's way, through LLImageGL's deferred delete, reaches it.
    TEST_CASE_METHOD(alimageslot_data, "alimageslot_gl: forgetTextures drops only the units that held a deleted name", "[alimageslot_gl]")
    {
        require_targets();
        ALImageSlot* slot = gGL.getImageSlot(1);
        ALImageSlot* kept = gGL.getImageSlot(5);
        CHECK(slot->bind(&mTarget, 0, ALImageSlot::READ_ONLY));
        CHECK(kept->bind(&mOther, 0, ALImageSlot::READ_ONLY));

        const U32 name = mTarget.getTexture();
        gGL.forgetTextures(1, &name);
        UNSCOPED_INFO("the slot forgot the name");
        CHECK(slot->getCurrTexture() == 0u);
        UNSCOPED_INFO("the other slot did not");
        CHECK(kept->getCurrTexture() == mOther.getTexture());

        const U32 binds = ALImageSlot::sImageBinds;
        CHECK(slot->bind(&mTarget, 0, ALImageSlot::READ_ONLY));
        UNSCOPED_INFO("the forgotten name is bound, not skipped");
        CHECK(ALImageSlot::sImageBinds == binds + 1);

        // The viewer's way: the target's textures go on LLImageGL's free list and are deleted some frames on.
        const U32 other = mOther.getTexture();
        mOther.release();
        for (U32 frame = 0; frame < 8; ++frame)
        {
            LLImageGL::updateClass();
        }
        UNSCOPED_INFO("the deleted texture's slot forgot it");
        CHECK(kept->getCurrTexture() == 0u);
        UNSCOPED_INFO("GL dropped it too");
        CHECK(unitState(5).mTexture == 0);
        UNSCOPED_INFO("the live one stays");
        CHECK(slot->getCurrTexture() == name);
        CHECK(glIsTexture(other) == GL_FALSE);

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // What an image cannot be bound to: an sRGB texture -- which an LLImageGL made from an image is -- a depth
    // texture, and a unit past the slots. Each is refused, with nothing issued and the unit unchanged; the same image
    // with a linear format, and a volume, bind.
    TEST_CASE_METHOD(alimageslot_data, "alimageslot_gl: sRGB and depth textures and units past the slots are refused", "[alimageslot_gl]")
    {
        require_targets();
        ALImageSlot* slot = gGL.getImageSlot(6);
        CHECK(slot->bind(&mTarget, 0, ALImageSlot::READ_ONLY));
        const U32 binds = ALImageSlot::sImageBinds;

        LLPointer<LLImageRaw> raw = new LLImageRaw(4, 4, 4);
        std::memset(raw->getData(), 0x80, 4 * 4 * 4);
        LLPointer<LLImageGL> srgb = new LLImageGL(raw, false);
        CHECK(srgb->getStorageInternalFormat() == (S32)GL_SRGB8_ALPHA8);
        CHECK_FALSE(slot->bind(srgb.get(), ALImageSlot::READ_ONLY));

        LLRenderTarget depth;
        REQUIRE(depth.allocate(SIZE, SIZE, 0, true));
        CHECK_FALSE(slot->bindManual(depth.getDepth(), 0, false, 0, ALImageSlot::READ_ONLY,
                                                  GL_DEPTH_COMPONENT24));
        depth.release();

        CHECK_FALSE(gGL.getImageSlot(AL_NUM_IMAGE_SLOTS)->bind(&mTarget, 0, ALImageSlot::READ_ONLY));

        UNSCOPED_INFO("nothing issued");
        CHECK(ALImageSlot::sImageBinds == binds);
        UNSCOPED_INFO("the unit unchanged");
        CHECK(unitState(6).mTexture == (GLint)mTarget.getTexture());
        UNSCOPED_INFO("nor the slot");
        CHECK(slot->getCurrTexture() == mTarget.getTexture());

        LLPointer<LLImageGL> linear = new LLImageGL(4, 4, 4, false);
        linear->setExplicitFormat(GL_RGBA8, GL_RGBA);
        REQUIRE(linear->createGLTexture(0, raw.get()));
        CHECK(slot->bind(linear.get(), ALImageSlot::READ_ONLY));
        UnitState state = unitState(6);
        CHECK(state.mTexture == (GLint)linear->getTexName());
        CHECK(state.mFormat == GL_RGBA8);
        CHECK(state.mLayered == GL_FALSE);

        LLPointer<ALTexture3D> volume = new ALTexture3D();
        REQUIRE(volume->allocate(4, 4, 4, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr));
        CHECK(slot->bind(volume.get(), ALImageSlot::WRITE_ONLY));
        state = unitState(6);
        CHECK(state.mTexture == (GLint)volume->getGLName());
        UNSCOPED_INFO("every slice");
        CHECK(state.mLayered == GL_TRUE);
        UNSCOPED_INFO("at its own format");
        CHECK(state.mFormat == GL_RGBA16F);
        slot->unbind();

        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // An R32UI target and an R32UI image, each the r32ui image of a kernel that loads every texel, changes it and
    // stores it back. The target's texels start where LLRenderTarget::clear put them and the image's where its upload
    // did, and both come back changed exactly, past what a float could carry.
    TEST_CASE_METHOD(alimageslot_data, "alimageslot_gl: a kernel changes R32UI target and image texels exactly", "[alimageslot_gl]")
    {
        require_targets();
        constexpr U32 W = 13;
        constexpr U32 H = 7;

        const GLuint cs = ll_test::compileTestShader(GL_COMPUTE_SHADER, kUintKernel);
        UNSCOPED_INFO("kernel compiles");
        REQUIRE(cs != 0);
        mKernel = glCreateProgram();
        glAttachShader(mKernel, cs);
        glLinkProgram(mKernel);
        glDeleteShader(cs);
        GLint linked = GL_FALSE;
        glGetProgramiv(mKernel, GL_LINK_STATUS, &linked);
        UNSCOPED_INFO("kernel links");
        REQUIRE(linked == GL_TRUE);

        REQUIRE(mUint.allocate(W, H, GL_R32UI));
        mUint.bindTarget();
        gGL.setClearColor(LLColor4(5.f, 0.f, 0.f, 0.f));
        mUint.clear();
        mUint.flush();

        ALImageSlot* slot = gGL.getImageSlot(0);
        glUseProgram(mKernel);
        CHECK(slot->bind(&mUint, 0, ALImageSlot::READ_WRITE));
        CHECK(unitState(0).mFormat == GL_R32UI);
        glDispatchCompute((W + 7) / 8, (H + 7) / 8, 1);
        gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_UPDATE);
        const std::vector<U32> target = readUints(mUint.getTexture(), W, H);

        std::vector<U32> values((size_t)W * H);
        for (size_t i = 0; i < values.size(); ++i)
        {
            values[i] = 0x01000001u + (U32)i * 0x101u;
        }
        LLPointer<LLImageGL> image = new LLImageGL(W, H, 1, /*usemipmaps=*/false);
        image->setExplicitFormat(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT);
        REQUIRE(image->createGLTexture(0, reinterpret_cast<const U8*>(values.data())));
        CHECK(slot->bind(image.get(), ALImageSlot::READ_WRITE));
        CHECK(unitState(0).mFormat == GL_R32UI);
        glDispatchCompute((W + 7) / 8, (H + 7) / 8, 1);
        gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_UPDATE);
        const std::vector<U32> held = readUints(image->getTexName(), W, H);
        glUseProgram(0);
        slot->unbind();

        for (U32 y = 0; y < H; ++y)
        {
            for (U32 x = 0; x < W; ++x)
            {
                CAPTURE(x, y);
                const size_t i = (size_t)y * W + x;
                REQUIRE(target[i] == 5u * 3u + x + y * 100u);
                UNSCOPED_INFO("the image");
                REQUIRE(held[i] == values[i] * 3u + x + y * 100u);
            }
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }
}
