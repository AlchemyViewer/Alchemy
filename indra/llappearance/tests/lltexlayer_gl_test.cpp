/**
 * @file lltexlayer_gl_test.cpp
 * @brief LLTexLayer's morph mask readback on the hidden window: the alpha of a layer smaller than the bake target,
 *        read through the target's texture and through its framebuffer, into buffers that end at a page no access is
 *        allowed to.
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

#include "../llavatarappearancedefines.h"
#include "../lltexlayer.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llrender.h"
#include "llrendertarget.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#if LL_WINDOWS
#include "llwin32headers.h"
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace
{
    // The bake target's size: every layer the viewer bakes is drawn into its lower left corner.
    const S32 TARGET_W = LLAvatarAppearanceDefines::SCRATCH_TEX_WIDTH;
    const S32 TARGET_H = LLAvatarAppearanceDefines::SCRATCH_TEX_HEIGHT;

    // A layer well inside the target, neither its size nor a multiple of four wide, off its origin.
    constexpr S32 LAYER_X = 24;
    constexpr S32 LAYER_Y = 40;
    constexpr S32 LAYER_W = 1001;
    constexpr S32 LAYER_H = 513;

    // The alpha the target holds at (x, y): a row or a column out of place reads another value.
    U8 alphaAt(S32 x, S32 y)
    {
        return (U8)(x * 7 + y * 13 + (x >> 8) * 59 + (y >> 8) * 101);
    }

    // `size` bytes that end where a page no access is allowed to begins, so the first byte written past them faults.
    class GuardedBuffer
    {
    public:
        explicit GuardedBuffer(size_t size)
        : mSize(size)
        {
#if LL_WINDOWS
            SYSTEM_INFO info;
            GetSystemInfo(&info);
            const size_t page = info.dwPageSize;
#else
            const size_t page = (size_t)sysconf(_SC_PAGESIZE);
#endif
            const size_t usable = (size + page - 1) / page * page;
            mMapped = usable + page;
#if LL_WINDOWS
            mBase = static_cast<U8*>(VirtualAlloc(nullptr, mMapped, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            DWORD old_protection = 0;
            mGuarded = mBase && VirtualProtect(mBase + usable, page, PAGE_NOACCESS, &old_protection);
#else
            void* mapped = mmap(nullptr, mMapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            mBase = mapped == MAP_FAILED ? nullptr : static_cast<U8*>(mapped);
            mGuarded = mBase && mprotect(mBase + usable, page, PROT_NONE) == 0;
#endif
            if (mBase)
            {
                mData = mBase + usable - size;
            }
        }

        ~GuardedBuffer()
        {
            if (mBase)
            {
#if LL_WINDOWS
                VirtualFree(mBase, 0, MEM_RELEASE);
#else
                munmap(mBase, mMapped);
#endif
            }
        }

        GuardedBuffer(const GuardedBuffer&)            = delete;
        GuardedBuffer& operator=(const GuardedBuffer&) = delete;

        bool   guarded() const { return mGuarded; }
        U8*    data() const { return mData; }
        size_t size() const { return mSize; }

    private:
        U8*    mBase    = nullptr;
        U8*    mData    = nullptr;
        size_t mSize    = 0;
        size_t mMapped  = 0;
        bool   mGuarded = false;
    };

    // A bake-sized RGBA8 target holding alphaAt in every texel, bound as the layers draw into it.
    struct lltexlayer_readback_data
    {
        ~lltexlayer_readback_data()
        {
            // A case that failed between the bindTarget and its flush left the target bound, which release refuses.
            while (LLRenderTarget::getCurrentBoundTarget())
            {
                LLRenderTarget::getCurrentBoundTarget()->flush();
            }
            mTarget.release();
        }

        void bindFilledTarget()
        {
            REQUIRE(mTarget.allocate(TARGET_W, TARGET_H, GL_RGBA8));
            std::vector<U8> texels((size_t)TARGET_W * TARGET_H * 4);
            for (S32 y = 0; y < TARGET_H; ++y)
            {
                for (S32 x = 0; x < TARGET_W; ++x)
                {
                    U8* texel = &texels[((size_t)y * TARGET_W + x) * 4];
                    texel[0] = (U8)x;
                    texel[1] = (U8)y;
                    texel[2] = 0x5A;
                    texel[3] = alphaAt(x, y);
                }
            }
            gGL.getTextureSlot(0)->bind(&mTarget);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TARGET_W, TARGET_H, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
            gGL.getTextureSlot(0)->unbind();
            mTarget.bindTarget();
        }

        // The alpha read back for the rectangle (x, y, width, height), into guarded scratch of `scratch_bytes` and
        // a guarded alpha buffer filled with 0xCD beforehand. What readBackAlpha answered goes in `error`.
        std::vector<U8> readBack(S32 x, S32 y, S32 width, S32 height, bool from_texture, size_t scratch_bytes,
                                 GLenum& error)
        {
            GuardedBuffer scratch(scratch_bytes);
            GuardedBuffer alpha((size_t)width * height);
            REQUIRE(scratch.guarded());
            REQUIRE(alpha.guarded());
            std::fill_n(alpha.data(), alpha.size(), U8(0xCD));
            error = LLTexLayer::readBackAlpha(&mTarget, x, y, width, height, from_texture, scratch.data(), scratch.size(),
                                              alpha.data());
            return std::vector<U8>(alpha.data(), alpha.data() + alpha.size());
        }

        ll_test::SharedGLScope mGL;
        LLRenderTarget mTarget;
    };

    // Every byte of `alpha` is alphaAt of its pixel of the rectangle (x, y, width, height), or 0 where that pixel lies
    // outside the target.
    void require_alpha_of(const std::vector<U8>& alpha, S32 x, S32 y, S32 width, S32 height)
    {
        REQUIRE(alpha.size() == (size_t)width * height);
        size_t wrong = 0;
        for (S32 row = 0; row < height; ++row)
        {
            for (S32 col = 0; col < width; ++col)
            {
                const S32 tx = x + col;
                const S32 ty = y + row;
                const bool inside = tx >= 0 && tx < TARGET_W && ty >= 0 && ty < TARGET_H;
                const U8 expected = inside ? alphaAt(tx, ty) : 0;
                const U8 actual = alpha[(size_t)row * width + col];
                if (actual != expected)
                {
                    if (!wrong)
                    {
                        UNSCOPED_INFO("first wrong pixel (" << col << ", " << row << "): " << (S32)actual << ", expected "
                                                           << (S32)expected);
                    }
                    ++wrong;
                }
            }
        }
        CHECK(wrong == 0);
    }

    // The read Intel's drivers take: glGetTexImage writes the whole of the bake-sized level, though the layer is a
    // fraction of it, so the scratch is the level's and the layer's rows are taken out of it at its offset.
    TEST_CASE_METHOD(lltexlayer_readback_data, "lltexlayer_gl: a readback through the texture takes a layer smaller than the target out of the whole level", "[lltexlayer_gl]")
    {
        bindFilledTarget();
        const size_t scratch_bytes = LLTexLayer::alphaReadbackScratchBytes(&mTarget, LAYER_W, LAYER_H, true);
        CHECK(scratch_bytes == (size_t)TARGET_W * TARGET_H * 4);

        GLenum error = GL_NO_ERROR;
        const std::vector<U8> alpha = readBack(LAYER_X, LAYER_Y, LAYER_W, LAYER_H, true, scratch_bytes, error);
        mTarget.flush();
        REQUIRE(error == GL_NO_ERROR);
        require_alpha_of(alpha, LAYER_X, LAYER_Y, LAYER_W, LAYER_H);
    }

    // The read every other driver takes, glReadPixels of the rectangle alone: the same alpha, through scratch no
    // larger than the rectangle's RGBA.
    TEST_CASE_METHOD(lltexlayer_readback_data, "lltexlayer_gl: a readback through the framebuffer reads the same alpha through the rectangle's scratch", "[lltexlayer_gl]")
    {
        bindFilledTarget();
        const size_t scratch_bytes = LLTexLayer::alphaReadbackScratchBytes(&mTarget, LAYER_W, LAYER_H, false);
        CHECK(scratch_bytes == (size_t)LAYER_W * LAYER_H * 4);

        GLenum error = GL_NO_ERROR;
        const std::vector<U8> alpha = readBack(LAYER_X, LAYER_Y, LAYER_W, LAYER_H, false, scratch_bytes, error);
        mTarget.flush();
        REQUIRE(error == GL_NO_ERROR);
        require_alpha_of(alpha, LAYER_X, LAYER_Y, LAYER_W, LAYER_H);
    }

    // Scratch the size of the layer's RGBA, short of the whole level glGetTexImage writes: the read is refused and
    // nothing is written.
    TEST_CASE_METHOD(lltexlayer_readback_data, "lltexlayer_gl: a texture readback refuses scratch short of the level and writes nothing", "[lltexlayer_gl]")
    {
        bindFilledTarget();
        GLenum error = GL_NO_ERROR;
        const std::vector<U8> alpha = readBack(LAYER_X, LAYER_Y, LAYER_W, LAYER_H, true, (size_t)LAYER_W * LAYER_H * 4, error);
        mTarget.flush();
        CHECK(error == GL_INVALID_VALUE);
        CHECK(std::all_of(alpha.begin(), alpha.end(), [](U8 value) { return value == 0xCD; }));
    }

    // A rectangle running off the target's top right corner: the part inside reads as the target holds it and the rest
    // as 0, with no read past the end of the level's scratch.
    TEST_CASE_METHOD(lltexlayer_readback_data, "lltexlayer_gl: a texture readback of a rectangle past the level's edge reads 0 outside it", "[lltexlayer_gl]")
    {
        bindFilledTarget();
        const S32 x = TARGET_W - 10;
        const S32 y = TARGET_H - 7;
        const S32 width = 30;
        const S32 height = 20;
        GLenum error = GL_NO_ERROR;
        const std::vector<U8> alpha =
            readBack(x, y, width, height, true, LLTexLayer::alphaReadbackScratchBytes(&mTarget, width, height, true), error);
        mTarget.flush();
        REQUIRE(error == GL_NO_ERROR);
        require_alpha_of(alpha, x, y, width, height);
    }
}
