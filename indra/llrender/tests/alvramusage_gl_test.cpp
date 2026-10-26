/**
 * @file alvramusage_gl_test.cpp
 * @brief ALVRAMUsage on the hidden window: what each kind of allocation is counted as, and the total.
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

#include "../algpubuffer.h"
#include "../aluniformbuffer.h"
#include "../alvramusage.h"
#include "../llcubemaparray.h"
#include "../llgl.h"
#include "../llglheaders.h"
#include "../llimagegl.h"
#include "../llrender.h"
#include "../llrendertarget.h"
#include "../llvertexbuffer.h"

#include "llheadlessgl_fixture.h"

#include "llimage.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

namespace
{
    // Every figure less the one before.
    struct Delta
    {
        S64 mStreamed;
        S64 mOther;
        S64 mTargets;
        S64 mProbes;
        S64 mGeometry;
        S64 mBuffers;
        S64 mTotal;
    };

    Delta delta_since(const ALVRAMUsage& before)
    {
        const ALVRAMUsage now = ALVRAMUsage::current();
        return { (S64)(now.mStreamedTextures - before.mStreamedTextures), (S64)(now.mOtherTextures - before.mOtherTextures),
                 (S64)(now.mRenderTargets - before.mRenderTargets),       (S64)(now.mReflectionProbes - before.mReflectionProbes),
                 (S64)(now.mGeometry - before.mGeometry),                 (S64)(now.mGPUBuffers - before.mGPUBuffers),
                 (S64)(now.total() - before.total()) };
    }

    struct alvramusage_data
    {
        ~alvramusage_data()
        {
            mTarget.release();
            mProbes  = nullptr;
            mTexture = nullptr;
            mBuffer.release();
            mUniforms.release();
        }

        ll_test::SharedGLScope    mGL;
        LLRenderTarget            mTarget;
        LLPointer<LLCubeMapArray> mProbes;
        LLPointer<LLImageGL>      mTexture;
        ALGPUBuffer               mBuffer;
        ALUniformBuffer           mUniforms;
    };

    // A render target, a probe array, a texture of each kind and the buffers are each counted in their own figure, at
    // their storage's every level and the size their formats pad to, and the total is what all of them add; each leaves
    // its figure as it is released. The buffers are an ALGPUBuffer and a uniform buffer's own store. Each is measured
    // from just before it: the first texture upload of a run can make the staging ring's buffer.
    TEST_CASE_METHOD(alvramusage_data, "ALVRAMUsage: each allocation is counted in its own figure", "[ALVRAMUsage]")
    {
        {
            INFO("a 16x8 RGBA8 target with mips over D32F with stencil");
            const ALRenderTargetDesc desc{ .width = 16, .height = 8, .color = { GL_RGBA8 },
                                           .depth = ALRenderTargetDesc::Depth::D32F, .stencil = true,
                                           .mips = ALRenderTargetDesc::Mips::Manual };
            const ALVRAMUsage before = ALVRAMUsage::current();
            REQUIRE(mTarget.ensure(desc) == LLRenderTarget::EnsureResult::Built);
            // Colour: 16x8, 8x4, 4x2, 2x1, 1x1 = 171 texels at 4 bytes; depth: 128 texels at 8.
            constexpr S64 BYTES = 171 * 4 + 128 * 8;
            CHECK((S64)LLRenderTarget::bytesFor(desc) == BYTES);
            const Delta d = delta_since(before);
            CHECK(d.mTargets == BYTES);
            CHECK(d.mStreamed == 0);
            CHECK(d.mOther == 0);
            CHECK(d.mTotal == BYTES);

            mTarget.release();
            CHECK(delta_since(before).mTargets == 0);
        }

        {
            INFO("a probe array of 2 cubes at 16x16 RGB8 with mips");
            const ALVRAMUsage before = ALVRAMUsage::current();
            mProbes = new LLCubeMapArray();
            mProbes->allocate(16, 3, 2, LLImageGLMemory::REFLECTION_PROBE, /*use_mips=*/true, /*hdr=*/false);
            // 12 layers of 16x16, 8x8, 4x4, 2x2, 1x1 = 341 texels, padded to 4 bytes.
            CHECK(delta_since(before).mProbes == 12 * 341 * 4);
            CHECK(delta_since(before).mTargets == 0);
            CHECK(delta_since(before).mOther == 0);

            mProbes = nullptr;
            CHECK(delta_since(before).mProbes == 0);
        }

        {
            INFO("an 8x8 RGB image, padded to 4 bytes, then counted as streamed");
            LLPointer<LLImageRaw> raw = new LLImageRaw(8, 8, 3);
            std::memset(raw->getData(), 0x40, (size_t)8 * 8 * 3);
            mTexture = new LLImageGL(/*usemipmaps=*/false);
            // Uploaded once first, so that whatever the first upload of a run makes is made before the measure.
            REQUIRE(mTexture->createGLTexture(0, raw.get()));
            mTexture->destroyGLTexture();

            const ALVRAMUsage before = ALVRAMUsage::current();
            REQUIRE(mTexture->createGLTexture(0, raw.get()));
            CHECK(delta_since(before).mOther == 8 * 8 * 4);
            CHECK(delta_since(before).mTotal == 8 * 8 * 4);
            mTexture->setVRAMClass(LLImageGLMemory::STREAMED_TEXTURE);
            CHECK(delta_since(before).mOther == 0);
            CHECK(delta_since(before).mStreamed == 8 * 8 * 4);

            mTexture = nullptr;
            CHECK(delta_since(before).mStreamed == 0);
        }

        {
            INFO("a GPU buffer");
            const ALVRAMUsage before = ALVRAMUsage::current();
            REQUIRE(mBuffer.allocate(4096));
            CHECK(delta_since(before).mBuffers == 4096);
            CHECK(delta_since(before).mTotal == 4096);

            mBuffer.release();
            CHECK(delta_since(before).mBuffers == 0);
        }

        {
            INFO("a vertex buffer, in geometry alone, and back to the pool's reserve as it goes");
            const ALVRAMUsage before = ALVRAMUsage::current();
            LLPointer<LLVertexBuffer> vb = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(vb->allocateBuffer(64, 96));
            const ALVRAMUsage held = ALVRAMUsage::current();
            const S64 in_use = (S64)(held.mGeometry - held.mGeometryPooled) -
                               (S64)(before.mGeometry - before.mGeometryPooled);
            // 64 positions of 16 bytes and 96 indices of 2, at least, rounded as the pool rounds them.
            CHECK(in_use >= 64 * 16 + 96 * 2);
            const Delta d = delta_since(before);
            CHECK(d.mTotal == d.mGeometry);
            CHECK(d.mBuffers == 0);

            vb = nullptr;
            const ALVRAMUsage after = ALVRAMUsage::current();
            CHECK((S64)(after.mGeometry - after.mGeometryPooled) == (S64)(before.mGeometry - before.mGeometryPooled));
        }

        {
            INFO("a uniform buffer's own store");
            const ALVRAMUsage before = ALVRAMUsage::current();
            const U32 words[64] = {};
            mUniforms.update(words, sizeof(words), ALUniformBuffer::EUsage::DYNAMIC);
            CHECK(delta_since(before).mBuffers == (S64)sizeof(words));
            CHECK(delta_since(before).mTotal == (S64)sizeof(words));

            mUniforms.release();
            CHECK(delta_since(before).mBuffers == 0);
        }

        const ALVRAMUsage now = ALVRAMUsage::current();
        UNSCOPED_INFO("the total is every figure but the pool's share of geometry, which is inside geometry");
        CHECK(now.total() == now.mStreamedTextures + now.mOtherTextures + now.mRenderTargets + now.mReflectionProbes +
                                 now.mGeometry + now.mGPUBuffers);
        CHECK(now.mGeometryPooled <= now.mGeometry);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }
}
