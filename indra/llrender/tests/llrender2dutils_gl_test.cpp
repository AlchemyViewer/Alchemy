/**
 * @file llrender2dutils_gl_test.cpp
 * @brief The quads the 2D helpers emit, read back from the buffers they land in.
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

#include "../llglheaders.h"

#include "llheadlessgl_fixture.h"

#include "../llgltexture.h"
#include "../llrender2dutils.h"
#include "../llvertexbuffer.h"
#include "llimage.h"

#include "../test/lltut.h"

#include <cstring>
#include <list>
#include <vector>

namespace tut
{
    struct llrender2dutils_data
    {
        // One context for the binary: standing it up is the expensive part,
        // and LLImageGL's class state does not survive a re-init.
        static ll_test::HeadlessGL& gl()
        {
            static ll_test::HeadlessGL instance(/*needs_vbos=*/true,
                                                /*needs_imagegl=*/true,
                                                /*needs_llrender=*/true,
                                                /*needs_render=*/true);
            return instance;
        }

        llrender2dutils_data()
        {
            gl();
        }

        static LLPointer<LLGLTexture> makeTexture(U16 width, U16 height)
        {
            LLPointer<LLImageRaw> raw = new LLImageRaw(width, height, 4);
            std::memset(raw->getData(), 0xFF, static_cast<size_t>(width) * height * 4);
            LLPointer<LLGLTexture> texture = new LLGLTexture(raw, /*usemipmaps=*/false);
            texture->createGLTexture(0, raw);
            return texture;
        }

        struct Bounds
        {
            F32 min_x = 0.f;
            F32 max_x = 0.f;
            F32 min_y = 0.f;
            F32 max_y = 0.f;
            size_t count = 0;
        };

        // The extent of everything `draw` emits, read back from GL: the
        // upload streams straight to the buffer and keeps no copy on this
        // side, so the buffer itself is the only place the vertices are.
        template <typename Draw>
        static Bounds boundsOf(Draw draw)
        {
            std::list<LLVertexBufferData> capture;
            gGL.beginList(&capture);
            draw();
            gGL.flush();
            gGL.endList();

            Bounds b;
            const U32 stride = LLVertexBuffer::sTypeSize[LLVertexBuffer::TYPE_VERTEX];
            for (LLVertexBufferData& data : capture)
            {
                if (data.mVB.isNull() || data.mCount == 0)
                {
                    continue;
                }
                data.mVB->setBuffer();
                std::vector<F32> floats(static_cast<size_t>(data.mCount) * stride / sizeof(F32));
                glGetBufferSubData(GL_ARRAY_BUFFER,
                                   data.mVB->getOffset(LLVertexBuffer::TYPE_VERTEX),
                                   data.mCount * stride,
                                   floats.data());
                for (U32 i = 0; i < data.mCount; ++i)
                {
                    const F32* v = floats.data() + static_cast<size_t>(i) * stride / sizeof(F32);
                    if (b.count == 0)
                    {
                        b.min_x = b.max_x = v[0];
                        b.min_y = b.max_y = v[1];
                    }
                    b.min_x = llmin(b.min_x, v[0]);
                    b.max_x = llmax(b.max_x, v[0]);
                    b.min_y = llmin(b.min_y, v[1]);
                    b.max_y = llmax(b.max_y, v[1]);
                    ++b.count;
                }
            }
            LLVertexBuffer::unbind();
            return b;
        }

        static void ensureNear(const std::string& what, F32 actual, F32 expected)
        {
            ensure(what + ": " + std::to_string(actual) + " vs " + std::to_string(expected),
                   fabsf(actual - expected) < 1e-3f);
        }
    };

    typedef test_group<llrender2dutils_data> llrender2dutils_test;
    typedef llrender2dutils_test::object     llrender2dutils_object;
    tut::llrender2dutils_test llrender2dutils_testcase("llrender2dutils_gl");

    // A rotated image turns about its own centre and keeps its size. An
    // odd-sized image at a quarter turn covers the same pixels it does
    // unrotated -- an open folder arrow sits where the closed one did.
    template<> template<>
    void llrender2dutils_object::test<1>()
    {
        LLPointer<LLGLTexture> texture = makeTexture(4, 4);

        const Bounds flat = boundsOf([&]() {
            gl_draw_scaled_rotated_image(10, 20, 9, 9, 0.f, texture.get());
        });
        ensure_equals("unrotated, one quad", flat.count, (size_t)6);
        ensureNear("unrotated left", flat.min_x, 10.f);
        ensureNear("unrotated right", flat.max_x, 19.f);
        ensureNear("unrotated bottom", flat.min_y, 20.f);
        ensureNear("unrotated top", flat.max_y, 29.f);

        const Bounds turned = boundsOf([&]() {
            gl_draw_scaled_rotated_image(10, 20, 9, 9, 90.f, texture.get());
        });
        ensure_equals("turned, one quad", turned.count, (size_t)6);
        ensureNear("turned left", turned.min_x, flat.min_x);
        ensureNear("turned right", turned.max_x, flat.max_x);
        ensureNear("turned bottom", turned.min_y, flat.min_y);
        ensureNear("turned top", turned.max_y, flat.max_y);
    }

    // The turn is a turn: at a quarter turn a wide image stands tall, and
    // it does so about the centre of the rect it was asked to fill.
    template<> template<>
    void llrender2dutils_object::test<2>()
    {
        LLPointer<LLGLTexture> texture = makeTexture(4, 4);

        const Bounds turned = boundsOf([&]() {
            gl_draw_scaled_rotated_image(10, 20, 8, 2, 90.f, texture.get());
        });
        ensure_equals("one quad", turned.count, (size_t)6);
        // Centre (14, 21); half extents swap to (1, 4).
        ensureNear("left", turned.min_x, 13.f);
        ensureNear("right", turned.max_x, 15.f);
        ensureNear("bottom", turned.min_y, 17.f);
        ensureNear("top", turned.max_y, 25.f);
    }
}
