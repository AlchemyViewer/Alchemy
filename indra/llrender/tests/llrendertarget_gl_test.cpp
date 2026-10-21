/**
 * @file llrendertarget_gl_test.cpp
 * @brief LLRenderTarget on the hidden window: which fragment output reaches a target's colour attachment, and integer
 *        attachments allocated, cleared and drawn into, a lent one included.
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

#include "../llgl.h"
#include "../llglheaders.h"
#include "../llglstates.h"
#include "../llgltexture.h"
#include "../llimagegl.h"
#include "../llrendertarget.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
        constexpr U32 SIZE = 4;

        // A triangle over the viewport.
        const char* kVertex =
            "#version 400\n"
            "void main()\n"
            "{\n"
            "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;\n"
            "    gl_Position = vec4(p, 0.0, 1.0);\n"
            "}\n";

        // Four outputs, as a G-buffer pass writes, each its own colour.
        const char* kFragment =
            "#version 400\n"
            "out vec4 frag_data[4];\n"
            "void main()\n"
            "{\n"
            "    frag_data[0] = vec4(1.0, 0.0, 0.0, 1.0);\n"
            "    frag_data[1] = vec4(0.0, 1.0, 0.0, 1.0);\n"
            "    frag_data[2] = vec4(0.0, 0.0, 1.0, 1.0);\n"
            "    frag_data[3] = vec4(1.0, 1.0, 0.0, 1.0);\n"
            "}\n";

        const LLColor4U OUTPUT_COLOURS[] = { LLColor4U(255, 0, 0, 255), LLColor4U(0, 255, 0, 255),
                                             LLColor4U(0, 0, 255, 255), LLColor4U(255, 255, 0, 255) };

        // The integer targets' size, neither square nor a power of two.
        constexpr U32 INT_W = 6;
        constexpr U32 INT_H = 5;

        // One output of each integer kind, each a function of the pixel: unsigned past 2^24, signed either side of 0,
        // and four wide channels to the top of their range.
        const char* kIntegerFragment =
            "#version 400\n"
            "layout(location = 0) out uvec4 frag_uint;\n"
            "layout(location = 1) out ivec4 frag_int;\n"
            "layout(location = 2) out uvec4 frag_wide;\n"
            "void main()\n"
            "{\n"
            "    uvec2 p = uvec2(gl_FragCoord.xy);\n"
            "    frag_uint = uvec4(0x01000001u + p.y * 1000u + p.x, 0u, 0u, 0u);\n"
            "    frag_int = ivec4(-1 - int(p.x), int(p.y) * 300 - 1000, 0, 0);\n"
            "    frag_wide = uvec4(p.x, p.y, 0xFFFFFFFFu - p.x, 0x80000000u + p.y);\n"
            "}\n";

        // What the bound target's colour attachment holds, read as integers: four channels a texel, those its format
        // lacks read as GL fills them in, 0 and an alpha of 1.
        template <typename T>
        std::vector<T> readInteger(U32 attachment)
        {
            std::vector<T> texels((size_t)INT_W * INT_H * 4, T(0x55));
            glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment);
            glReadPixels(0, 0, INT_W, INT_H, GL_RGBA_INTEGER, std::is_signed_v<T> ? GL_INT : GL_UNSIGNED_INT,
                         texels.data());
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            return texels;
        }

        // Every texel of `texels` is `expected`.
        template <typename T>
        void require_every(const std::string& what, const std::vector<T>& texels, const T (&expected)[4])
        {
            INFO(what);
            for (size_t i = 0; i < texels.size(); ++i)
            {
                INFO("texel " << i / 4 << " channel " << i % 4);
                REQUIRE(texels[i] == expected[i % 4]);
            }
        }

        using Depth        = ALRenderTargetDesc::Depth;
        using Mips         = ALRenderTargetDesc::Mips;
        using EnsureResult = LLRenderTarget::EnsureResult;

        // Two colour attachments over a depth-stencil buffer, at a size neither square nor a power of two.
        constexpr ALRenderTargetDesc GBUFFER{ .width = 6, .height = 5, .color = { GL_RGBA8, GL_RGBA16F },
                                              .depth = Depth::D24, .stencil = true };

        // A description, and what a case calls it.
        struct NamedDesc
        {
            const char*        what;
            ALRenderTargetDesc desc;
        };

        // The GL objects a target is made of.
        struct Names
        {
            U32              framebuffer = 0;
            std::vector<U32> color;
            U32              depth = 0;

            bool operator==(const Names&) const = default;
        };

        Names names_of(LLRenderTarget& target)
        {
            Names names;
            target.bindTarget();
            GLint framebuffer = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
            target.flush();
            names.framebuffer = (U32)framebuffer;
            for (U32 attachment = 0; attachment < target.getNumTextures(); ++attachment)
            {
                names.color.push_back(target.getTexture(attachment));
            }
            names.depth = target.getDepth();
            return names;
        }

        // What LLRenderTarget counts for a target `desc` describes, from the bytes each format takes a pixel.
        U32 bytes_of(const ALRenderTargetDesc& desc)
        {
            U32 per_pixel = 0;
            for (U32 attachment = 0; attachment < desc.colorCount(); ++attachment)
            {
                switch (desc.color[attachment])
                {
                    case GL_RGBA16F: per_pixel += 8; break;
                    case GL_R8: per_pixel += 1; break;
                    default: per_pixel += 4; break;
                }
            }
            if (desc.depth != Depth::None)
            {
                per_pixel += desc.depth == Depth::D32F && desc.stencil ? 8 : 4;
            }
            return desc.width * desc.height * per_pixel;
        }

        // The storage GL holds for the texture `name`.
        struct Storage
        {
            S32 width  = 0;
            S32 height = 0;
            S32 format = 0;
            S32 levels = 0;
        };

        Storage storage_of(U32 name)
        {
            Storage storage;
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, name);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &storage.width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &storage.height);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &storage.format);
            // The levels with an image, counted by their widths: GL_TEXTURE_IMMUTABLE_LEVELS would say it in one query,
            // but it is GL 4.3's, past the floor's 4.1.
            for (S32 width = storage.width; width > 0; ++storage.levels)
            {
                width = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, storage.levels + 1, GL_TEXTURE_WIDTH, &width);
            }
            gGL.getTextureSlot(0)->unbind();
            return storage;
        }

        // The sized format GL allocates depth as, for what a description asks.
        S32 depth_format_of(const ALRenderTargetDesc& desc)
        {
            if (desc.depth == Depth::D32F)
            {
                return desc.stencil ? GL_DEPTH32F_STENCIL8 : GL_DEPTH_COMPONENT32F;
            }
            return desc.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
        }

        // Each of the target's textures holds the storage `desc` describes.
        void require_storage(LLRenderTarget& target, const ALRenderTargetDesc& desc)
        {
            const S32 levels = desc.mips == Mips::None ? 1 : (S32)std::bit_width(std::max(desc.width, desc.height));
            REQUIRE(target.getNumTextures() == desc.colorCount());
            for (U32 attachment = 0; attachment < desc.colorCount(); ++attachment)
            {
                CAPTURE(attachment);
                const Storage storage = storage_of(target.getTexture(attachment));
                CHECK(storage.width == (S32)desc.width);
                CHECK(storage.height == (S32)desc.height);
                CHECK(storage.format == (S32)desc.color[attachment]);
                CHECK(storage.levels == levels);
            }
            if (desc.depth == Depth::None)
            {
                CHECK(target.getDepth() == 0u);
                return;
            }
            REQUIRE(target.getDepth() != 0u);
            const Storage depth = storage_of(target.getDepth());
            CHECK(depth.width == (S32)desc.width);
            CHECK(depth.height == (S32)desc.height);
            CHECK(depth.format == depth_format_of(desc));
        }

        // The target holds nothing: no textures, no depth of its own or shared, and the description of no target.
        void require_released(const LLRenderTarget& target)
        {
            CHECK(target.getNumTextures() == 0u);
            CHECK(target.getDepth() == 0u);
            CHECK_FALSE(target.sharesDepth());
            CHECK_FALSE(target.isComplete());
            CHECK(target.desc() == ALRenderTargetDesc{});
        }

        // The colour of the target's first attachment at its middle.
        LLColor4U pixel_of(LLRenderTarget& target)
        {
            target.bindTarget();
            U8 pixel[4] = { 0 };
            glReadPixels(target.getWidth() / 2, target.getHeight() / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            target.flush();
            return LLColor4U(pixel[0], pixel[1], pixel[2], pixel[3]);
        }

        // What the target's framebuffer has attached at `point`, 0 for nothing.
        U32 attached_at(LLRenderTarget& target, GLenum point)
        {
            target.bindTarget();
            GLint name = -1;
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, point, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
            target.flush();
            return (U32)name;
        }

        // Clear the owner's depth to `value`, through the owner's framebuffer.
        void clear_depth(LLRenderTarget& owner, F64 value)
        {
            LLGLDepthTest write(GL_TRUE, GL_TRUE);
            glClearDepth(value);
            owner.bindTarget();
            owner.clear(GL_DEPTH_BUFFER_BIT);
            owner.flush();
        }

        // The depth at the middle of the target, read through its own framebuffer.
        F32 depth_through(LLRenderTarget& target)
        {
            target.bindTarget();
            F32 depth = -1.f;
            glReadPixels(target.getWidth() / 2, target.getHeight() / 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
            target.flush();
            return depth;
        }

        // The label the GL object `name` carries.
        std::string label_of(GLenum identifier, U32 name)
        {
            char    label[256] = {};
            GLsizei length     = 0;
            glGetObjectLabel(identifier, name, (GLsizei)sizeof(label), &length, label);
            return std::string(label, (size_t)length);
        }

        // The target's framebuffer is labelled `label`, each colour attachment `label.colorN`, and its own depth, where
        // `depth` says it has one, `label.depth`; or, for an empty `label`, none of them is labelled.
        void require_labels(LLRenderTarget& target, const std::string& label, bool depth)
        {
            CAPTURE(label);
            auto expected = [&label](const std::string& suffix) { return label.empty() ? label : label + suffix; };
            CHECK(label_of(GL_FRAMEBUFFER, names_of(target).framebuffer) == label);
            for (U32 attachment = 0; attachment < target.getNumTextures(); ++attachment)
            {
                CAPTURE(attachment);
                CHECK(label_of(GL_TEXTURE, target.getTexture(attachment)) == expected(".color" + std::to_string(attachment)));
            }
            if (depth)
            {
                CHECK(label_of(GL_TEXTURE, target.getDepth()) == expected(".depth"));
            }
        }

        // Debug GL, under which alone GL objects are labelled, for as long as it lives.
        struct DebugGLScope
        {
            bool mWas = gDebugGL;
            DebugGLScope() { gDebugGL = true; }
            ~DebugGLScope() { gDebugGL = mWas; }
        };

        // Makes `name`, a texture released already, the next name LLImageGL::generateTextures hands out, as a driver
        // that reuses names may make it of its own accord. The texture is deleted now rather than frames on; every name
        // GL would hand out before it is taken and held while this lives, so that it is the lowest free name; and the
        // thread's pool of names is emptied and refilled from GL, all of the refill taken but its first, which is it.
        // mHandedBack is false where GL never hands the name out again.
        struct NextTextureName
        {
            static constexpr int DELETE_FRAMES = 4;    // LLImageGL deletes a texture DELETE_DELAY (3) frames after it
            static constexpr S32 POOL_SIZE     = 1024; // the names LLImageGL::generateTextures takes from GL at a time

            explicit NextTextureName(U32 name)
            {
                for (int frame = 0; frame < DELETE_FRAMES; ++frame)
                {
                    LLImageGL::updateClass();
                }
                for (U32 tries = 0; tries < (1u << 16); ++tries)
                {
                    GLuint next = 0;
                    glGenTextures(1, &next);
                    if (next == name)
                    {
                        glDeleteTextures(1, &next);
                        mHandedBack = true;
                        break;
                    }
                    mHeld.push_back(next);
                }
                if (mHandedBack)
                {
                    // The pool's names are its context's, and a new class starts it afresh.
                    LLImageGL::cleanupClass();
                    LLImageGL::initClass(nullptr, LLGLTexture::MAX_GL_IMAGE_CATEGORY, false, false, false);
                    std::vector<U32> pooled(POOL_SIZE - 1);
                    LLImageGL::generateTextures((S32)pooled.size(), pooled.data());
                    mHeld.insert(mHeld.end(), pooled.begin(), pooled.end());
                }
            }
            ~NextTextureName() { glDeleteTextures((GLsizei)mHeld.size(), mHeld.data()); }

            NextTextureName(const NextTextureName&)            = delete;
            NextTextureName& operator=(const NextTextureName&) = delete;

            bool                mHandedBack = false;
            std::vector<GLuint> mHeld;
        };

        // Puts back the context's largest texture size, however the case ends.
        struct MaxTextureSizeRestore
        {
            S32 mSaved = gGLManager.mGLMaxTextureSize;
            ~MaxTextureSizeRestore() { gGLManager.mGLMaxTextureSize = mSaved; }
        };

    // The program, vertex array and clear colour the test sets go back with
    // mGL.
    struct llrendertarget_data
    {
        ~llrendertarget_data()
        {
            // A case that failed between a bindTarget and its flush left a target bound, which release refuses.
            while (LLRenderTarget::getCurrentBoundTarget())
            {
                LLRenderTarget::getCurrentBoundTarget()->flush();
            }
            mTarget.release();
            mInteger.release();
            mMixed.release();
            mLent.release();
            mCopy.release();
            mSharer.release();
            mDepthSharer.release();
            mOwner.release();
            mWide.release();
            mLentImage = nullptr;
            if (mVAO)
            {
                glDeleteVertexArrays(1, &mVAO);
            }
            glUseProgram(0);
            for (GLuint program : { mProgram, mIntegerProgram })
            {
                if (program)
                {
                    glDeleteProgram(program);
                }
            }
        }

        // The integer program, linked.
        GLuint require_integer_program()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kIntegerFragment);
            {
                INFO("integer shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            const GLuint program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            REQUIRE(linked == GL_TRUE);
            return program;
        }

        void setUp()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            {
                INFO("shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mProgram = glCreateProgram();
            glAttachShader(mProgram, vs);
            glAttachShader(mProgram, fs);
            glLinkProgram(mProgram);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint linked = GL_FALSE;
            glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
            REQUIRE(linked == GL_TRUE);
            glGenVertexArrays(1, &mVAO);

            REQUIRE(mTarget.allocate(SIZE, SIZE, GL_RGBA8));
        }

        // What the target holds after the triangle draws into it, its draw buffers set to output `output`, or left as
        // bindTarget sets them when it is negative.
        LLColor4U drawn(S32 output)
        {
            mTarget.bindTarget();
            gGL.setClearColor(LLColor4::transparent);
            mTarget.clear();
            if (output >= 0)
            {
                mTarget.setDrawOutput((U32)output);
            }
            glUseProgram(mProgram);
            glBindVertexArray(mVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glUseProgram(0);
            U8 pixel[4] = { 0 };
            glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            mTarget.flush();
            return LLColor4U(pixel[0], pixel[1], pixel[2], pixel[3]);
        }

        ll_test::SharedGLScope mGL;
        LLRenderTarget mTarget;
        LLRenderTarget mInteger;
        LLRenderTarget mMixed;
        LLRenderTarget mLent;
        LLRenderTarget mCopy;
        LLRenderTarget mOwner;
        LLRenderTarget mSharer;
        LLRenderTarget mDepthSharer;
        LLRenderTarget mWide;
        LLPointer<LLImageGL> mLentImage;
        GLuint mVAO = 0;
        GLuint mProgram = 0;
        GLuint mIntegerProgram = 0;
    };


    // setDrawOutput: a single-attachment target takes the output it names and none of the others, and the next
    // bindTarget gives it output 0 again.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: setDrawOutput sends the named output to a single-attachment target", "[llrendertarget_gl]")
    {
        setUp();
        UNSCOPED_INFO("bound: output 0");
        CHECK(drawn(-1) == OUTPUT_COLOURS[0]);
        for (S32 output = 0; output < 4; ++output)
        {
            CAPTURE(output);
            REQUIRE(drawn(output) == OUTPUT_COLOURS[output]);
        }
        UNSCOPED_INFO("rebound: output 0 again");
        CHECK(drawn(-1) == OUTPUT_COLOURS[0]);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // Integer attachments: R32UI, RG16I and RGBA32UI allocate as themselves beside a depth buffer. clear takes each to
    // the clear colour rounded to integers -- an unsigned one held at 0 below and at its top above -- and leaves them
    // alone when only the depth is asked for; a program's integer outputs reach them exactly.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: integer attachments keep their formats through clear and draw", "[llrendertarget_gl]")
    {
        mIntegerProgram = require_integer_program();
        glGenVertexArrays(1, &mVAO);
        REQUIRE(mInteger.allocate(INT_W, INT_H, GL_R32UI, /*depth=*/true));
        CHECK(mInteger.addColorAttachment(GL_RG16I));
        CHECK(mInteger.addColorAttachment(GL_RGBA32UI));

        const GLenum formats[] = { GL_R32UI, GL_RG16I, GL_RGBA32UI };
        for (U32 attachment = 0; attachment < 3; ++attachment)
        {
            CAPTURE(attachment);
            UNSCOPED_INFO("the target's record");
            REQUIRE(mInteger.getInternalFormat(attachment) == (U32)formats[attachment]);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mInteger.getTexture(attachment));
            GLint format = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
            UNSCOPED_INFO("the texture's storage");
            REQUIRE((U32)format == (U32)formats[attachment]);
        }
        gGL.getTextureSlot(0)->unbind();

        mInteger.bindTarget();
        CHECK((U32)glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == (U32)GL_FRAMEBUFFER_COMPLETE);

        gGL.setClearColor(LLColor4(7.f, 9.f, 11.f, 13.f));
        mInteger.clear();
        require_every<U32>("R32UI cleared", readInteger<U32>(0), { 7u, 0u, 0u, 1u });
        require_every<S32>("RG16I cleared", readInteger<S32>(1), { 7, 9, 0, 1 });
        require_every<U32>("RGBA32UI cleared", readInteger<U32>(2), { 7u, 9u, 11u, 13u });

        gGL.setClearColor(LLColor4(-5.4f, -6.6f, 1.e10f, 0.4f));
        mInteger.clear(GL_DEPTH_BUFFER_BIT);
        require_every<U32>("R32UI left alone by a depth clear", readInteger<U32>(0), { 7u, 0u, 0u, 1u });
        mInteger.clear();
        require_every<U32>("R32UI held at 0", readInteger<U32>(0), { 0u, 0u, 0u, 1u });
        require_every<S32>("RG16I below 0", readInteger<S32>(1), { -5, -7, 0, 1 });
        require_every<U32>("RGBA32UI held at its top", readInteger<U32>(2), { 0u, 0u, 0xFFFFFFFFu, 0u });

        LLGLDisable depth_test(GL_DEPTH_TEST);
        glUseProgram(mIntegerProgram);
        glBindVertexArray(mVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);
        const std::vector<U32> uints = readInteger<U32>(0);
        const std::vector<S32> ints = readInteger<S32>(1);
        const std::vector<U32> wide = readInteger<U32>(2);
        mInteger.flush();

        for (U32 y = 0; y < INT_H; ++y)
        {
            for (U32 x = 0; x < INT_W; ++x)
            {
                CAPTURE(x, y);
                const size_t i = ((size_t)y * INT_W + x) * 4;
                UNSCOPED_INFO("R32UI drawn");
                REQUIRE(uints[i] == 0x01000001u + y * 1000u + x);
                UNSCOPED_INFO("RG16I r drawn");
                REQUIRE(ints[i] == -1 - (S32)x);
                UNSCOPED_INFO("RG16I g drawn");
                REQUIRE(ints[i + 1] == (S32)y * 300 - 1000);
                UNSCOPED_INFO("RGBA32UI r drawn");
                REQUIRE(wide[i] == x);
                UNSCOPED_INFO("RGBA32UI g drawn");
                REQUIRE(wide[i + 1] == y);
                UNSCOPED_INFO("RGBA32UI b drawn");
                REQUIRE(wide[i + 2] == 0xFFFFFFFFu - x);
                UNSCOPED_INFO("RGBA32UI a drawn");
                REQUIRE(wide[i + 3] == 0x80000000u + y);
            }
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A target of both kinds: clear takes the float attachment to the clear colour and the integer one to its integers,
    // and with the draw buffers narrowed to the first it clears that one alone, as it would a target of one kind.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: a mixed float and integer target clears each attachment as its kind", "[llrendertarget_gl]")
    {
        REQUIRE(mMixed.allocate(INT_W, INT_H, GL_RGBA8));
        CHECK(mMixed.addColorAttachment(GL_R32UI));

        mMixed.bindTarget();
        gGL.setClearColor(LLColor4(1.f, 0.f, 1.f, 1.f));
        mMixed.clear();
        std::vector<U8> rgba((size_t)INT_W * INT_H * 4, 0x55);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, INT_W, INT_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        require_every<U8>("RGBA8 cleared", rgba, { 255, 0, 255, 255 });
        require_every<U32>("R32UI cleared", readInteger<U32>(1), { 1u, 0u, 0u, 1u });

        mMixed.setDrawBuffers(1);
        gGL.setClearColor(LLColor4::transparent);
        mMixed.clear();
        glReadPixels(0, 0, INT_W, INT_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        require_every<U8>("RGBA8 cleared again", rgba, { 0, 0, 0, 0 });
        require_every<U32>("R32UI not drawn to, so left", readInteger<U32>(1), { 1u, 0u, 0u, 1u });
        mMixed.flush();

        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A texture lent by setColorAttachment answers for its format as one the target allocated does, so an integer one
    // clears to its integers; and returning it forgets the format.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: a texture lent by setColorAttachment has its format until returned", "[llrendertarget_gl]")
    {
        const std::vector<U32> zeros((size_t)INT_W * INT_H, 0u);
        mLentImage = new LLImageGL(INT_W, INT_H, 1, /*usemipmaps=*/false);
        mLentImage->setExplicitFormat(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT);
        CHECK(mLentImage->createGLTexture(0, reinterpret_cast<const U8*>(zeros.data())));

        mLent.setColorAttachment(mLentImage.get());
        CHECK(mLent.getInternalFormat(0) == (U32)GL_R32UI);

        mLent.bindTarget();
        gGL.setClearColor(LLColor4(5.f, 6.f, 7.f, 8.f));
        mLent.clear();
        require_every<U32>("R32UI cleared to its integers", readInteger<U32>(0), { 5u, 0u, 0u, 1u });
        mLent.flush();

        mLent.releaseColorAttachment();
        CHECK(mLent.getInternalFormat(0) == 0u);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // copyFromBound copies the colour of the framebuffer bound now into a target, scaled with the filter asked for, as
    // the selection outline copies the window behind its lines at half size: a linear halving averages each square of
    // four. The scissor in force does not clip it, and what was bound is bound after.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: copyFromBound copies the bound framebuffer past the scissor", "[llrendertarget_gl]")
    {
        setUp();
        constexpr U32 HALF = SIZE / 2;
        REQUIRE(mCopy.allocate(HALF, HALF, GL_RGBA8));
        mCopy.bindTarget();
        gGL.setClearColor(LLColor4(0.f, 0.f, 1.f, 1.f));
        mCopy.clear();
        mCopy.flush();

        // Black, but for a white first column.
        mTarget.bindTarget();
        gGL.setClearColor(LLColor4::black);
        mTarget.clear();
        {
            LLGLSScissor column(0, 0, 1, SIZE);
            gGL.setClearColor(LLColor4::white);
            mTarget.clear();
        }
        {
            LLGLSScissor corner(0, 0, 1, 1);
            mCopy.copyFromBound(0, 0, SIZE, SIZE, 0, 0, HALF, HALF, GL_LINEAR);
        }
        UNSCOPED_INFO("what was bound is bound after");
        CHECK(LLRenderTarget::getCurrentBoundTarget() == &mTarget);
        mTarget.flush();

        mCopy.bindTarget();
        std::vector<U8> px((size_t)HALF * HALF * 4);
        glReadPixels(0, 0, HALF, HALF, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        mCopy.flush();
        auto red_at = [&px](U32 x, U32 y) { return (U32)px[((size_t)y * HALF + x) * 4]; };
        auto blue_at = [&px](U32 x, U32 y) { return (U32)px[((size_t)y * HALF + x) * 4 + 2]; };
        UNSCOPED_INFO("the white column and the black beside it averaged");
        CHECK(red_at(0, 0) >= 127u);
        CHECK(red_at(0, 0) <= 128u);
        UNSCOPED_INFO("black squares black");
        CHECK(red_at(1, 0) == 0u);
        UNSCOPED_INFO("copied outside the scissor too");
        CHECK(blue_at(HALF - 1, HALF - 1) == 0u);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // ensure builds a target once and keeps it after: the same description again keeps every GL name and what the
    // target holds, and counts no more bytes. A description wider than the context's largest texture is kept as the
    // size allocate holds it to, and a target allocate built is kept by the description it was built as.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: ensure keeps a target that already matches its description", "[llrendertarget_gl]")
    {
        const U32 bytes = LLRenderTarget::sBytesAllocated;
        REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);
        CHECK(mOwner.desc() == GBUFFER);
        require_storage(mOwner, GBUFFER);
        CHECK(LLRenderTarget::sBytesAllocated - bytes == bytes_of(GBUFFER));

        const Names built = names_of(mOwner);
        mOwner.bindTarget();
        gGL.setClearColor(LLColor4(1.f, 0.f, 1.f, 1.f));
        mOwner.clear(GL_COLOR_BUFFER_BIT);
        mOwner.flush();

        CHECK(mOwner.ensure(GBUFFER) == EnsureResult::Kept);
        UNSCOPED_INFO("the same names");
        CHECK(names_of(mOwner) == built);
        UNSCOPED_INFO("what the target held");
        CHECK(pixel_of(mOwner) == LLColor4U(255, 0, 255, 255));
        CHECK(LLRenderTarget::sBytesAllocated - bytes == bytes_of(GBUFFER));

        const U32 max_size = (U32)gGLManager.mGLMaxTextureSize;
        const ALRenderTargetDesc too_wide{ .width = max_size + 3, .height = 1, .color = { GL_R8 } };
        REQUIRE(mWide.ensure(too_wide) == EnsureResult::Built);
        CHECK(mWide.getWidth() == max_size);
        UNSCOPED_INFO("a width past the largest texture, held to it");
        CHECK(mWide.ensure(too_wide) == EnsureResult::Kept);

        REQUIRE(mTarget.allocate(SIZE, SIZE, GL_RGBA8, /*depth=*/true, /*stencil=*/true, ALTextureSlot::TT_TEXTURE,
                                 LLRenderTarget::MIPS_AUTO, LLRenderTarget::DEPTH_FMT_32F));
        REQUIRE(mTarget.addColorAttachment(GL_RGBA16F));
        const ALRenderTargetDesc allocated{ .width = SIZE, .height = SIZE, .color = { GL_RGBA8, GL_RGBA16F },
                                            .depth = Depth::D32F, .stencil = true, .mips = Mips::Auto };
        CHECK(mTarget.desc() == allocated);
        CHECK(mTarget.ensure(allocated) == EnsureResult::Kept);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A description that differs in any field builds the target again, to that description: its storage is what the
    // description says and the byte count follows it. Asked again, it is kept.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: ensure builds a target again when its size or a format changes", "[llrendertarget_gl]")
    {
        const U32 bytes = LLRenderTarget::sBytesAllocated;
        REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);

        const NamedDesc changes[] = {
            { "width", { .width = 9, .height = 5, .color = { GL_RGBA8, GL_RGBA16F }, .depth = Depth::D24, .stencil = true } },
            { "height", { .width = 9, .height = 7, .color = { GL_RGBA8, GL_RGBA16F }, .depth = Depth::D24, .stencil = true } },
            { "a colour format", { .width = 9, .height = 7, .color = { GL_RGBA8, GL_RGB10_A2 }, .depth = Depth::D24, .stencil = true } },
            { "a colour attachment more", { .width = 9, .height = 7, .color = { GL_RGBA8, GL_RGB10_A2, GL_R32F }, .depth = Depth::D24, .stencil = true } },
            { "a colour attachment fewer", { .width = 9, .height = 7, .color = { GL_RGBA8 }, .depth = Depth::D24, .stencil = true } },
            { "the depth format", { .width = 9, .height = 7, .color = { GL_RGBA8 }, .depth = Depth::D32F, .stencil = true } },
            { "stencil", { .width = 9, .height = 7, .color = { GL_RGBA8 }, .depth = Depth::D32F } },
            { "mips", { .width = 9, .height = 7, .color = { GL_RGBA8 }, .depth = Depth::D32F, .mips = Mips::Manual } },
            { "no depth", { .width = 9, .height = 7, .color = { GL_RGBA8 }, .mips = Mips::Manual } },
            { "no colour", { .width = 9, .height = 7, .depth = Depth::D24 } },
        };
        for (const auto& [what, desc] : changes)
        {
            CAPTURE(what);
            REQUIRE(mOwner.ensure(desc) == EnsureResult::Built);
            CHECK(mOwner.desc() == desc);
            require_storage(mOwner, desc);
            CHECK(LLRenderTarget::sBytesAllocated - bytes == bytes_of(desc));
            CHECK(mOwner.ensure(desc) == EnsureResult::Kept);
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A description GL cannot build -- wider than the context's largest texture, which allocate is let past here --
    // and one no target can be built from, both leave the target released and the byte count where it was before the
    // target was built. After either, the target builds as before.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: a failed ensure leaves the target released and the byte count right", "[llrendertarget_gl]")
    {
        const U32 bytes = LLRenderTarget::sBytesAllocated;
        {
            MaxTextureSizeRestore restore;
            const U32 max_size = (U32)restore.mSaved;
            gGLManager.mGLMaxTextureSize = restore.mSaved * 2;
            const NamedDesc refused[] = {
                { "depth too wide", { .width = max_size + 1, .height = 1, .color = { GL_RGBA8, GL_RGBA8 }, .depth = Depth::D24, .stencil = true } },
                { "colour too wide", { .width = max_size + 1, .height = 1, .color = { GL_RGBA8, GL_RGBA8 } } },
            };
            for (const auto& [what, desc] : refused)
            {
                CAPTURE(what);
                REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);
                CHECK(mOwner.ensure(desc) == EnsureResult::Failed);
                require_released(mOwner);
                CHECK(LLRenderTarget::sBytesAllocated == bytes);
            }
        }

        const NamedDesc unbuildable[] = {
            { "no size", { .color = { GL_RGBA8 } } },
            { "no attachment", { .width = 4, .height = 4 } },
            { "a format after a 0", { .width = 4, .height = 4, .color = { GL_RGBA8, 0, GL_RGBA8 } } },
            { "stencil without depth", { .width = 4, .height = 4, .color = { GL_RGBA8 }, .stencil = true } },
            { "generated mips of an integer format", { .width = 4, .height = 4, .color = { GL_R32UI }, .mips = Mips::Auto } },
            { "generated mips with no colour", { .width = 4, .height = 4, .depth = Depth::D24, .mips = Mips::Auto } },
        };
        for (const auto& [what, desc] : unbuildable)
        {
            CAPTURE(what);
            REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);
            CHECK(mOwner.ensure(desc) == EnsureResult::Failed);
            require_released(mOwner);
            CHECK(LLRenderTarget::sBytesAllocated == bytes);
        }

        REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);
        require_storage(mOwner, GBUFFER);
        CHECK(LLRenderTarget::sBytesAllocated - bytes == bytes_of(GBUFFER));
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A target sharing its depth: built, it has the owner's depth at the owner's attachment point and reads what the
    // owner wrote there. Each time the owner is built again -- another depth format, no stencil, another colour
    // format, released and built as it was -- the sharer's next ensure attaches the new depth and keeps its colour,
    // names and contents both, and the one after that is kept. Built at another size with its owner, it follows too.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: ensure re-points a depth sharer when its owner is built again", "[llrendertarget_gl]")
    {
        const ALRenderTargetDesc sharer{ .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA16F } };
        const U32 bytes = LLRenderTarget::sBytesAllocated;
        REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);
        REQUIRE(mSharer.ensure(sharer, mOwner) == EnsureResult::Built);
        CHECK(mSharer.sharesDepth());
        CHECK(mSharer.desc() == sharer);
        UNSCOPED_INFO("the sharer counts its colour alone");
        CHECK(LLRenderTarget::sBytesAllocated - bytes == bytes_of(GBUFFER) + bytes_of(sharer));
        CHECK(attached_at(mSharer, GL_DEPTH_ATTACHMENT) == mOwner.getDepth());
        CHECK(attached_at(mSharer, GL_STENCIL_ATTACHMENT) == mOwner.getDepth());
        clear_depth(mOwner, 0.25);
        CHECK(std::abs(depth_through(mSharer) - 0.25f) < 1.e-4f);
        CHECK(mSharer.ensure(sharer, mOwner) == EnsureResult::Kept);

        mSharer.bindTarget();
        gGL.setClearColor(LLColor4(1.f, 0.f, 1.f, 1.f));
        mSharer.clear(GL_COLOR_BUFFER_BIT);
        mSharer.flush();
        const U32 colour = mSharer.getTexture(0);

        const NamedDesc owners[] = {
            { "the depth format", { .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA8, GL_RGBA16F }, .depth = Depth::D32F, .stencil = true } },
            { "no stencil", { .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA8, GL_RGBA16F }, .depth = Depth::D32F } },
            { "stencil again", { .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA8, GL_RGBA16F }, .depth = Depth::D24, .stencil = true } },
            { "a colour format", { .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA8, GL_RGB10_A2 }, .depth = Depth::D24, .stencil = true } },
            { "released and built as it was", { .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA8, GL_RGB10_A2 }, .depth = Depth::D24, .stencil = true } },
        };
        F64 depth = 0.25;
        for (const auto& [what, owner] : owners)
        {
            CAPTURE(what);
            if (owner == mOwner.desc())
            {
                mOwner.release();
            }
            REQUIRE(mOwner.ensure(owner) == EnsureResult::Built);
            CHECK(mSharer.ensure(sharer, mOwner) == EnsureResult::Built);
            UNSCOPED_INFO("the sharer's colour kept");
            CHECK(mSharer.getTexture(0) == colour);
            CHECK(pixel_of(mSharer) == LLColor4U(255, 0, 255, 255));
            CHECK(mSharer.desc() == sharer);
            CHECK(attached_at(mSharer, GL_DEPTH_ATTACHMENT) == mOwner.getDepth());
            CHECK(attached_at(mSharer, GL_STENCIL_ATTACHMENT) == (owner.stencil ? mOwner.getDepth() : 0u));
            depth += 0.125;
            clear_depth(mOwner, depth);
            UNSCOPED_INFO("what the owner wrote, read through the sharer");
            CHECK(std::abs(depth_through(mSharer) - (F32)depth) < 1.e-4f);
            CHECK(mSharer.ensure(sharer, mOwner) == EnsureResult::Kept);
        }

        const ALRenderTargetDesc owner_resized{ .width = 9, .height = 7, .color = { GL_RGBA8 }, .depth = Depth::D24, .stencil = true };
        const ALRenderTargetDesc sharer_resized{ .width = 9, .height = 7, .color = { GL_RGBA16F } };
        REQUIRE(mOwner.ensure(owner_resized) == EnsureResult::Built);
        REQUIRE(mSharer.ensure(sharer_resized, mOwner) == EnsureResult::Built);
        require_storage(mSharer, sharer_resized);
        CHECK(attached_at(mSharer, GL_DEPTH_ATTACHMENT) == mOwner.getDepth());
        clear_depth(mOwner, 0.875);
        CHECK(std::abs(depth_through(mSharer) - 0.875f) < 1.e-4f);
        CHECK(LLRenderTarget::sBytesAllocated - bytes == bytes_of(owner_resized) + bytes_of(sharer_resized));
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // GL may hand a deleted texture's name to the next texture made, so an owner built again can hold its new depth
    // under its last depth's name. A sharer attached to the last depth still holds that texture, which a framebuffer
    // keeps alive past its name, and is attached again all the same: which build it holds is told by the owner's
    // serial, never by the name. Where GL does not hand the name back there is nothing to show.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: a sharer is attached again to an owner's depth built under the old one's name", "[llrendertarget_gl]")
    {
        const ALRenderTargetDesc owner{ .width = 8, .height = 8, .depth = Depth::D32F };
        const ALRenderTargetDesc sharer{ .width = 8, .height = 8, .color = { GL_RGBA8 } };
        REQUIRE(mOwner.ensure(owner) == EnsureResult::Built);
        REQUIRE(mSharer.ensure(sharer, mOwner) == EnsureResult::Built);
        clear_depth(mOwner, 0.25);
        REQUIRE(std::abs(depth_through(mSharer) - 0.25f) < 1.e-4f);
        const U32 name = mOwner.getDepth();

        mOwner.release();
        const NextTextureName next(name);
        if (!next.mHandedBack)
        {
            SKIP("GL does not hand a deleted texture's name out again");
        }
        REQUIRE(mOwner.ensure(owner) == EnsureResult::Built);
        if (mOwner.getDepth() != name)
        {
            SKIP("GL handed the deleted depth's name to another texture first");
        }

        CHECK(mSharer.ensure(sharer, mOwner) == EnsureResult::Built);
        CHECK(attached_at(mSharer, GL_DEPTH_ATTACHMENT) == name);
        clear_depth(mOwner, 0.75);
        UNSCOPED_INFO("what the owner's new depth holds, read through the sharer");
        CHECK(std::abs(depth_through(mSharer) - 0.75f) < 1.e-4f);
        CHECK(mSharer.ensure(sharer, mOwner) == EnsureResult::Kept);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A target that shareDepthBuffer gave its owner's depth is kept by ensure as a sharer of that owner; asked to be
    // a target with no shared depth, it is built again without it.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: ensure takes a depth shared by shareDepthBuffer as its own sharing", "[llrendertarget_gl]")
    {
        const ALRenderTargetDesc sharer{ .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA16F } };
        REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);
        REQUIRE(mSharer.allocate(GBUFFER.width, GBUFFER.height, GL_RGBA16F));
        mOwner.shareDepthBuffer(mSharer);
        CHECK(mSharer.sharesDepth());
        CHECK(mSharer.ensure(sharer, mOwner) == EnsureResult::Kept);

        CHECK(mSharer.ensure(sharer) == EnsureResult::Built);
        CHECK_FALSE(mSharer.sharesDepth());
        CHECK(attached_at(mSharer, GL_DEPTH_ATTACHMENT) == 0u);
        CHECK(mSharer.ensure(sharer) == EnsureResult::Kept);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A sharer that cannot share as described -- a size other than the owner's, depth of its own, an owner with no
    // depth, itself as owner -- is released, and the byte count goes back to the owner's alone.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: a sharer that cannot share fails released", "[llrendertarget_gl]")
    {
        const ALRenderTargetDesc sharer{ .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA16F } };
        const U32 bytes = LLRenderTarget::sBytesAllocated;
        REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);

        const NamedDesc refused[] = {
            { "another size", { .width = GBUFFER.width + 1, .height = GBUFFER.height, .color = { GL_RGBA16F } } },
            { "depth of its own", { .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA16F }, .depth = Depth::D24 } },
            { "stencil of its own", { .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA16F }, .stencil = true } },
            { "no size", { .color = { GL_RGBA16F } } },
            { "generated mips with no colour", { .width = GBUFFER.width, .height = GBUFFER.height, .mips = Mips::Auto } },
        };
        for (const auto& [what, desc] : refused)
        {
            CAPTURE(what);
            REQUIRE(mSharer.ensure(sharer, mOwner) == EnsureResult::Built);
            CHECK(mSharer.ensure(desc, mOwner) == EnsureResult::Failed);
            require_released(mSharer);
            CHECK(LLRenderTarget::sBytesAllocated - bytes == bytes_of(GBUFFER));
        }

        UNSCOPED_INFO("its own owner");
        REQUIRE(mSharer.ensure(sharer, mOwner) == EnsureResult::Built);
        CHECK(mSharer.ensure(sharer, mSharer) == EnsureResult::Failed);
        require_released(mSharer);

        UNSCOPED_INFO("an owner with no depth");
        REQUIRE(mSharer.ensure(sharer, mOwner) == EnsureResult::Built);
        REQUIRE(mOwner.ensure({ .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA8 } }) == EnsureResult::Built);
        CHECK(mSharer.ensure(sharer, mOwner) == EnsureResult::Failed);
        require_released(mSharer);

        UNSCOPED_INFO("a sharer with no colour of its own");
        REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);
        const ALRenderTargetDesc depth_only{ .width = GBUFFER.width, .height = GBUFFER.height };
        CHECK(mSharer.ensure(depth_only, mOwner) == EnsureResult::Built);
        CHECK(attached_at(mSharer, GL_DEPTH_ATTACHMENT) == mOwner.getDepth());
        UNSCOPED_INFO("its owner's depth is what it renders into");
        CHECK(mSharer.isComplete());
        CHECK(mSharer.ensure(depth_only, mOwner) == EnsureResult::Kept);
        CHECK(LLRenderTarget::sBytesAllocated - bytes == bytes_of(GBUFFER));
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A label names the framebuffer and each attachment a target allocated, on every build after it is set: built
    // again at another size with an attachment more, all of them are named again. Set on a built target, it renames
    // what is there. A sharer names its framebuffer and colour, a sharer with no colour its framebuffer, and neither
    // renames the owner's depth; a target setColorAttachment lends a texture to names its framebuffer and leaves the
    // texture as its owner named it. allocate and addColorAttachment name what they build as ensure does, and an
    // empty label takes the names away.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: a label names what each build of a target makes", "[llrendertarget_gl]")
    {
        if (!gGLManager.mHasDebugOutput || !glGetObjectLabel)
        {
            SKIP("this context keeps no object labels");
        }
        const DebugGLScope debug_gl;

        mOwner.setLabel("test.gbuffer");
        REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);
        require_labels(mOwner, "test.gbuffer", true);

        const ALRenderTargetDesc wider{ .width = 9, .height = 7, .color = { GL_RGBA8, GL_RGBA16F, GL_RGB10_A2 },
                                        .depth = Depth::D32F, .stencil = true };
        REQUIRE(mOwner.ensure(wider) == EnsureResult::Built);
        require_labels(mOwner, "test.gbuffer", true);

        mOwner.setLabel("test.renamed");
        require_labels(mOwner, "test.renamed", true);

        mSharer.setLabel("test.sharer");
        REQUIRE(mSharer.ensure({ .width = 9, .height = 7, .color = { GL_RGBA16F } }, mOwner) == EnsureResult::Built);
        require_labels(mSharer, "test.sharer", false);
        CHECK(label_of(GL_TEXTURE, mOwner.getDepth()) == "test.renamed.depth");

        mDepthSharer.setLabel("test.depth_only");
        REQUIRE(mDepthSharer.ensure({ .width = 9, .height = 7 }, mOwner) == EnsureResult::Built);
        require_labels(mDepthSharer, "test.depth_only", false);
        CHECK(label_of(GL_TEXTURE, mOwner.getDepth()) == "test.renamed.depth");

        const std::vector<U32> zeros((size_t)INT_W * INT_H, 0u);
        mLentImage = new LLImageGL(INT_W, INT_H, 1, /*usemipmaps=*/false);
        mLentImage->setExplicitFormat(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT);
        REQUIRE(mLentImage->createGLTexture(0, reinterpret_cast<const U8*>(zeros.data())));
        mLent.setLabel("test.lent");
        mLent.setColorAttachment(mLentImage.get());
        CHECK(label_of(GL_FRAMEBUFFER, names_of(mLent).framebuffer) == "test.lent");
        CHECK(label_of(GL_TEXTURE, mLentImage->getTexName()) == "");
        mLent.releaseColorAttachment();

        mTarget.setLabel("test.allocated");
        REQUIRE(mTarget.allocate(SIZE, SIZE, GL_RGBA8, /*depth=*/true));
        REQUIRE(mTarget.addColorAttachment(GL_RGBA16F));
        require_labels(mTarget, "test.allocated", true);

        mOwner.setLabel("");
        require_labels(mOwner, "", true);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // Two sharers of two owners' depths that swap their framebuffers swap the depths they hold: each, ensured again
    // as its own owner's sharer, attaches that owner's depth again rather than keeping the other's.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: swapFBORefs takes the shared depth with the framebuffer", "[llrendertarget_gl]")
    {
        const ALRenderTargetDesc sharer{ .width = GBUFFER.width, .height = GBUFFER.height, .color = { GL_RGBA16F } };
        REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);
        REQUIRE(mWide.ensure(GBUFFER) == EnsureResult::Built);
        REQUIRE(mSharer.ensure(sharer, mOwner) == EnsureResult::Built);
        REQUIRE(mDepthSharer.ensure(sharer, mWide) == EnsureResult::Built);

        mSharer.swapFBORefs(mDepthSharer);
        CHECK(attached_at(mSharer, GL_DEPTH_ATTACHMENT) == mWide.getDepth());
        CHECK(attached_at(mDepthSharer, GL_DEPTH_ATTACHMENT) == mOwner.getDepth());

        // Each holds the other owner's depth now, which is not the one it is asked to share.
        CHECK(mSharer.ensure(sharer, mOwner) == EnsureResult::Built);
        CHECK(mDepthSharer.ensure(sharer, mWide) == EnsureResult::Built);
        CHECK(attached_at(mSharer, GL_DEPTH_ATTACHMENT) == mOwner.getDepth());
        CHECK(attached_at(mDepthSharer, GL_DEPTH_ATTACHMENT) == mWide.getDepth());
        CHECK(mSharer.ensure(sharer, mOwner) == EnsureResult::Kept);
        CHECK(mDepthSharer.ensure(sharer, mWide) == EnsureResult::Kept);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A label names the target, so after a swap each target's label is on the objects it holds now, not on those it
    // gave up: a swapped pair, the exposure's or the lens flare's, shows each target under its own name.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: swapFBORefs leaves each target's label on what it holds", "[llrendertarget_gl]")
    {
        if (!gGLManager.mHasDebugOutput || !glGetObjectLabel)
        {
            SKIP("this context keeps no object labels");
        }
        const DebugGLScope debug_gl;

        const ALRenderTargetDesc state{ .width = 2, .height = 1, .color = { GL_RGBA16F } };
        mTarget.setLabel("test.current");
        mCopy.setLabel("test.last");
        REQUIRE(mTarget.ensure(state) == EnsureResult::Built);
        REQUIRE(mCopy.ensure(state) == EnsureResult::Built);
        const Names target_before = names_of(mTarget);

        mTarget.swapFBORefs(mCopy);
        REQUIRE(names_of(mCopy) == target_before);
        require_labels(mTarget, "test.current", false);
        require_labels(mCopy, "test.last", false);

        mTarget.swapFBORefs(mCopy);
        REQUIRE(names_of(mTarget) == target_before);
        require_labels(mTarget, "test.current", false);
        require_labels(mCopy, "test.last", false);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // Outside debug GL nothing is labelled: a label set before a build, or on a built target, names none of what the
    // target makes, and a swapped pair whose labels differ is not labelled again each swap, as the exposure's is every
    // frame. The target keeps its label for when it is.
    TEST_CASE_METHOD(llrendertarget_data, "llrendertarget_gl: outside debug GL a label names nothing", "[llrendertarget_gl]")
    {
        if (!gGLManager.mHasDebugOutput || !glGetObjectLabel)
        {
            SKIP("this context keeps no object labels");
        }
        REQUIRE_FALSE(gDebugGL);

        mOwner.setLabel("test.gbuffer");
        REQUIRE(mOwner.ensure(GBUFFER) == EnsureResult::Built);
        require_labels(mOwner, "", true);
        mOwner.setLabel("test.renamed");
        require_labels(mOwner, "", true);
        CHECK(mOwner.getLabel() == "test.renamed");

        const ALRenderTargetDesc state{ .width = 2, .height = 1, .color = { GL_RGBA16F } };
        mTarget.setLabel("test.current");
        mCopy.setLabel("test.last");
        REQUIRE(mTarget.ensure(state) == EnsureResult::Built);
        REQUIRE(mCopy.ensure(state) == EnsureResult::Built);
        mTarget.swapFBORefs(mCopy);
        require_labels(mTarget, "", false);
        require_labels(mCopy, "", false);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }
}
