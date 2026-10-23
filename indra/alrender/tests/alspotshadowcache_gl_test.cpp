/**
 * @file alspotshadowcache_gl_test.cpp
 * @brief A spot shadow draw's geometry, folded from a real vertex buffer on the hidden window: what the buffer holds
 *        now is part of it, so a caster written again draws its map again.
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

#include "../alspotshadowcache.h"

#include "llstrider.h"
#include "llvertexbuffer.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    using Cache = ALSpotShadowCache;
    using Hash  = Cache::Hash;

    struct spot_shadow_gl_data
    {
        static LLPointer<LLVertexBuffer> makeBuffer()
        {
            LLPointer<LLVertexBuffer> vb = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(vb->allocateBuffer(4, 6));
            return vb;
        }

        // A quad's draw from @a vb, as the viewer folds one from its draw info.
        static U64 fold(const LLVertexBuffer* vb)
        {
            Hash draw;
            Cache::foldGeometry(draw, vb, 0, 3, 6, 0, 0);
            return draw.value();
        }

        ll_test::SharedGLScope mGL;
    };

    TEST_CASE_METHOD(spot_shadow_gl_data, "alspotshadowcache_gl: a draw's geometry folds what its buffer holds now", "[alspotshadowcache_gl]")
    {
        LLPointer<LLVertexBuffer> vb = makeBuffer();
        const U64                 held = fold(vb.get());
        CHECK(fold(vb.get()) == held);

        // A rebuild writes the buffer in place, keeping the buffer and the draw's range.
        {
            LLStrider<LLVector3> pos;
            REQUIRE(vb->getVertexStrider(pos, 0, 4));
            for (S32 i = 0; i < 4; ++i)
            {
                pos[i].set((F32)i, 0.f, 0.f);
            }
            vb->unmapBuffer();
        }
        CHECK(fold(vb.get()) != held);

        // Another buffer drawn over the same range is another caster.
        LLPointer<LLVertexBuffer> other = makeBuffer();
        CHECK(fold(other.get()) != fold(vb.get()));
    }
}
