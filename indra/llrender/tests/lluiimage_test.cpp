/**
 * @file lluiimage_test.cpp
 * @brief LLUIImage's display-list cache holds an image's shape, not its place.
 *
 * An image is drawn at many positions -- every icon in a list, every
 * background of a floater being dragged -- and the recording made of it
 * has to serve all of them. These tests pin that a recording carries no
 * position of its own, and that moving the image replays it rather
 * than making another.
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
#include "../lluiimage.h"
#include "../llvertexbuffer.h"
#include "llimage.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

namespace ll_test
{
    // Test-only view of LLUIImage's cache, friended in lluiimage.h.
    struct UIImageProbe
    {
        static size_t recordings(const LLUIImage& image)
        {
            return image.mDisplayLists.size();
        }

        // Every vertex position the image's recordings hold, read back from GL:
        // the upload streams straight to the buffer and keeps no copy on this
        // side, so the buffer itself is the only place the vertices are.
        static std::vector<LLVector3> positions(const LLUIImage& image)
        {
            std::vector<LLVector3> out;
            const U32 stride = LLVertexBuffer::sTypeSize[LLVertexBuffer::TYPE_VERTEX];
            for (auto& entry : image.mDisplayLists)
            {
                for (LLVertexBufferData& data : entry.second.list)
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
                        out.emplace_back(v[0], v[1], v[2]);
                    }
                }
            }
            LLVertexBuffer::unbind();
            return out;
        }
    };
}

namespace
{
    struct lluiimage_data
    {
        // One context for the binary: standing it up is the expensive part,
        // and LLImageGL's class state does not survive a re-init.
        static ll_test::HeadlessGL& gl()
        {
            return ll_test::sharedHeadlessGL();
        }

        lluiimage_data()
        {
            gl();
            LLUIImage::enableDisplayListsCollection(true);
        }

        ~lluiimage_data()
        {
            // Drops every recording, and the references the cleanup list holds.
            LLUIImage::cleanupClass();
        }

        static LLPointer<LLGLTexture> makeTexture(U16 width, U16 height)
        {
            LLPointer<LLImageRaw> raw = new LLImageRaw(width, height, 4);
            std::memset(raw->getData(), 0xFF, static_cast<size_t>(width) * height * 4);
            LLPointer<LLGLTexture> texture = new LLGLTexture(raw, /*usemipmaps=*/false);
            texture->createGLTexture(0, raw);
            return texture;
        }

        // A recording's bounds are the image's size, from the origin.
        static void require_extent(const std::vector<LLVector3>& verts, F32 width, F32 height)
        {
            REQUIRE_FALSE(verts.empty());
            F32 min_x = verts[0].mV[VX];
            F32 max_x = min_x;
            F32 min_y = verts[0].mV[VY];
            F32 max_y = min_y;
            for (const LLVector3& v : verts)
            {
                min_x = llmin(min_x, v.mV[VX]);
                max_x = llmax(max_x, v.mV[VX]);
                min_y = llmin(min_y, v.mV[VY]);
                max_y = llmax(max_y, v.mV[VY]);
            }
            REQUIRE(min_x == 0.f);
            REQUIRE(min_y == 0.f);
            REQUIRE(max_x == width);
            REQUIRE(max_y == height);
        }
    };


    // One recording serves every position the image is drawn at. A floater
    // being dragged is this test run at frame rate.
    TEST_CASE_METHOD(lluiimage_data, "LLUIImage: one recording serves every position but not every size or colour", "[LLUIImage]")
    {
        LLPointer<LLGLTexture> texture = makeTexture(4, 4);
        LLPointer<LLUIImage> image = new LLUIImage("probe", texture.get());

        image->draw(10, 10, 8, 8);
        UNSCOPED_INFO("the first draw records");
        CHECK(ll_test::UIImageProbe::recordings(*image) == (size_t)1);

        image->draw(100, 50, 8, 8);
        UNSCOPED_INFO("another position replays");
        CHECK(ll_test::UIImageProbe::recordings(*image) == (size_t)1);

        gGL.pushUIMatrix();
        gGL.translateUI(30.f, 40.f, 0.f);
        image->draw(10, 10, 8, 8);
        gGL.popUIMatrix();
        UNSCOPED_INFO("another UI offset replays");
        CHECK(ll_test::UIImageProbe::recordings(*image) == (size_t)1);

        image->draw(10, 10, 16, 8);
        UNSCOPED_INFO("another size is another recording");
        CHECK(ll_test::UIImageProbe::recordings(*image) == (size_t)2);

        image->draw(10, 10, 8, 8, LLColor4::red);
        UNSCOPED_INFO("another colour is another recording");
        CHECK(ll_test::UIImageProbe::recordings(*image) == (size_t)3);
    }

    // The recording holds the image's shape and nothing of where it was made:
    // drawn under a UI offset and away from the origin, its vertices still
    // span exactly [0, width] x [0, height].
    TEST_CASE_METHOD(lluiimage_data, "LLUIImage: a plain image's recording spans its size from the origin", "[LLUIImage]")
    {
        LLPointer<LLGLTexture> texture = makeTexture(4, 4);
        LLPointer<LLUIImage> image = new LLUIImage("probe", texture.get());

        gGL.pushUIMatrix();
        gGL.translateUI(30.f, 40.f, 0.f);
        image->draw(10, 10, 8, 6);
        gGL.popUIMatrix();

        const std::vector<LLVector3> verts = ll_test::UIImageProbe::positions(*image);
        UNSCOPED_INFO("a plain image is one quad");
        REQUIRE(verts.size() == (size_t)6);
        require_extent(verts, 8.f, 6.f);
    }

    // The nine-slice path works out its own placement from the UI offset, so
    // it is checked apart from the plain quad.
    TEST_CASE_METHOD(lluiimage_data, "LLUIImage: a nine-slice image's recording spans its size from the origin", "[LLUIImage]")
    {
        LLPointer<LLGLTexture> texture = makeTexture(16, 16);
        LLPointer<LLUIImage> image = new LLUIImage("probe", texture.get());
        image->setScaleRegion(LLRectf(0.25f, 0.75f, 0.75f, 0.25f));

        gGL.pushUIMatrix();
        gGL.translateUI(30.f, 40.f, 0.f);
        image->draw(10, 10, 32, 24);
        gGL.popUIMatrix();

        const std::vector<LLVector3> verts = ll_test::UIImageProbe::positions(*image);
        UNSCOPED_INFO("nine slices are eighteen triangles");
        REQUIRE(verts.size() == (size_t)54);
        require_extent(verts, 32.f, 24.f);
    }
}
