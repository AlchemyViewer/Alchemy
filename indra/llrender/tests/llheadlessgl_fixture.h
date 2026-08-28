/**
 * @file llheadlessgl_fixture.h
 * @brief One-shot GL bring-up shared across the GL-backed llrender
 *        integration tests.
 *
 * The context comes from SDL, on a window that is never shown. That
 * is deliberately the same GL the viewer itself runs on -- WGL on
 * Windows, EGL or GLX on Linux -- where OSMesa, which this used to
 * use, is a software path we do not ship and so did not test.
 * It also drops a dependency that distributions have begun removing.
 *
 * Standing the context up is the expensive part — TUT spins a fresh
 * fixture per test method, so we hide a static-local instance
 * behind getHeadlessGl() and let every fixture struct embed a
 * reference. That keeps each test isolated against its own state
 * (font registry, atlas pages) while reusing the GL context.
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

#ifndef LL_LLHEADLESSGL_FIXTURE_H
#define LL_LLHEADLESSGL_FIXTURE_H

#include "linden_common.h"

#include "../llgltexture.h"
#include "../llglslshader.h"
#include "../llimagegl.h"
#include "../llfontfreetype.h"
#include "../alfontshaping.h"
#include "../aluniformbuffer.h"
#include "../llfontvertexbuffer.h"
#include "../llrender.h"
#include "../llshadermgr.h"
#include "../llvertexbuffer.h"

#include <SDL3/SDL.h>

namespace ll_test
{
    // Test-only accessor for LLFontVertexBuffer's private capture lists.
    // Friended in llfontvertexbuffer.h so we can count emitted quads per
    // pass without poisoning the production API with public accessors.
    struct VertexBufferProbe
    {
        struct CaptureCounts
        {
            size_t shadow_quads     = 0;
            size_t foreground_quads = 0;
        };

        static CaptureCounts count(const LLFontVertexBuffer& buf)
        {
            CaptureCounts c;
            for (const LLVertexBufferData& e : buf.mShadowBufferList)
                c.shadow_quads += e.mCount / 6;
            for (const LLVertexBufferData& e : buf.mForegroundBufferList)
                c.foreground_quads += e.mCount / 6;
            return c;
        }
    };

    // Drop transient font caches that would otherwise leak between tests
    // sharing this binary's HeadlessGl singleton. Safe to call repeatedly;
    // does NOT touch LLFontManager / LLImageGL / GL state — those stay
    // alive for the whole binary because LLFontGL's static fontp caches
    // (getFontSansSerif et al.) dangle across a re-init cycle (see
    // llfontgl_test.cpp:64-74). The shape LRU is the only piece we can
    // safely flush mid-binary, since its entries hold raw face* and would
    // re-populate on the next shape call regardless.
    inline void resetFontState()
    {
        ALFontShaping::clearCache();
    }

    // RAII scope — calls resetFontState() in dtor. Embed in a TUT fixture
    // struct to guarantee the shape cache is empty after every test,
    // regardless of how the test exited.
    struct FontStateScope
    {
        FontStateScope() = default;
        ~FontStateScope() { resetFontState(); }

        FontStateScope(const FontStateScope&) = delete;
        FontStateScope& operator=(const FontStateScope&) = delete;
    };

    // Concrete LLShaderMgr for render-output tests. The tests never load real
    // shaders from disk and don't pump the per-frame uniform updates the viewer
    // does, so getShaderDirPrefix returns empty and updateShaderUniforms is a
    // no-op. LLRender::syncMatrices still pushes matrix uniforms directly each
    // draw — that path lives in llrender.cpp and doesn't go through the mgr.
    class TestShaderMgr : public LLShaderMgr
    {
    public:
        TestShaderMgr() { sInstance = this; initAttribsAndUniforms(); }
        ~TestShaderMgr() override { if (sInstance == this) sInstance = nullptr; }

        std::string getShaderDirPrefix() override { return std::string(); }
        void updateShaderUniforms(LLGLSLShader* /*shader*/) override {}
    };

    // GL 3.3 core pass-through UI vertex shader. Same uniform / attribute
    // shape as indra/newview/app_settings/shaders/class1/interface/uiV.glsl;
    // LLRender::syncMatrices recognises modelview_projection_matrix and
    // texture_matrix0 by reserved-name lookup.
    inline const char* kTestUIVertexShader()
    {
        return
            "#version 330\n"
            "uniform mat4 texture_matrix0;\n"
            "uniform mat4 modelview_projection_matrix;\n"
            "in vec3 position;\n"
            "in vec4 diffuse_color;\n"
            "in vec2 texcoord0;\n"
            "out vec4 vertex_color;\n"
            "out vec2 vary_texcoord0;\n"
            "void main()\n"
            "{\n"
            "    gl_Position = modelview_projection_matrix * vec4(position, 1.0);\n"
            "    vary_texcoord0 = (texture_matrix0 * vec4(texcoord0, 0.0, 1.0)).xy;\n"
            "    vertex_color = diffuse_color;\n"
            "}\n";
    }

    // GL 3.3 core pass-through UI fragment shader. Mirrors uiF.glsl's
    // shadowMode==0 fast path. Tests run with LLFontGL::sEnableShaderShadow
    // off (default), so the multi-tap shadow branches are not exercised
    // and aren't compiled in here.
    inline const char* kTestUIFragmentShader()
    {
        return
            "#version 330\n"
            "out vec4 frag_color;\n"
            "uniform sampler2D diffuseMap;\n"
            "in vec2 vary_texcoord0;\n"
            "in vec4 vertex_color;\n"
            "void main()\n"
            "{\n"
            "    frag_color = vertex_color * texture(diffuseMap, vary_texcoord0);\n"
            "}\n";
    }

    // Compile a shader stage from inline source. Returns 0 on failure;
    // logs the compiler diagnostic so test setup failures show up in CI.
    inline GLuint compileTestShader(GLenum stage, const char* src)
    {
        GLuint sh = glCreateShader(stage);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok = GL_FALSE;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLchar log[2048] = {0};
            glGetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
            LL_WARNS("HeadlessGL") << "Shader compile failed: " << log << LL_ENDL;
            glDeleteShader(sh);
            return 0;
        }
        return sh;
    }

    // Compile, link, and map gUIProgram via the standard LLGLSLShader API.
    // attachObject + mapAttributes binds reserved attrib slots and links;
    // mapUniforms reads back uniform locations against the LLShaderMgr's
    // reserved-name table. The TestShaderMgr ctor already populated that
    // table via initAttribsAndUniforms.
    inline bool setupTestUIProgram(LLShaderMgr* /*mgr*/)
    {
        GLuint vs = compileTestShader(GL_VERTEX_SHADER, kTestUIVertexShader());
        GLuint fs = compileTestShader(GL_FRAGMENT_SHADER, kTestUIFragmentShader());
        if (!vs || !fs)
        {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            return false;
        }

        gUIProgram.mProgramObject = glCreateProgram();
        gUIProgram.mName = "ui (test stub)";
        gUIProgram.attachObject(vs);
        gUIProgram.attachObject(fs);
        // mapAttributes binds reserved attrib slots, links the program,
        // then reads back mAttribute + mAttributeMask. Returns false on
        // link failure.
        if (!gUIProgram.mapAttributes())
        {
            glDeleteShader(vs);
            glDeleteShader(fs);
            gUIProgram.unload();
            gUIProgram.mProgramObject = 0;
            return false;
        }
        // Stage shaders are referenced by the linked program now; safe
        // to delete the standalone handles.
        glDeleteShader(vs);
        glDeleteShader(fs);

        if (!gUIProgram.mapUniforms())
        {
            gUIProgram.unload();
            gUIProgram.mProgramObject = 0;
            return false;
        }
        return true;
    }

    inline void teardownTestUIProgram()
    {
        if (gUIProgram.mProgramObject)
        {
            LLGLSLShader::unbind();
            gUIProgram.unload();
            gUIProgram.mProgramObject = 0;
        }
    }

    // Read back the OSMesa framebuffer's color buffer as RGBA8 (tightly
    // packed, row 0 = bottom of GL viewport). Tests use this to assert
    // pixel-level invariants — alignment column position, drop-shadow
    // offset, underline row band — against an actually-rendered frame.
    inline std::vector<U8> readFramebufferRGBA(S32 w, S32 h)
    {
        std::vector<U8> px(static_cast<std::size_t>(w) * h * 4, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        return px;
    }

    // Route the debug context's messages into the test log. Anything the
    // driver considers an error or a portability problem shows up named,
    // rather than as a wrong pixel three tests later. Notifications are
    // dropped -- drivers use them for buffer-allocation chatter.
#ifndef APIENTRY
#  define APIENTRY
#endif
    inline void APIENTRY onGLDebugMessage(GLenum /*source*/, GLenum type, GLuint id,
                                          GLenum severity, GLsizei /*length*/,
                                          const GLchar* message, const void* /*user*/)
    {
        if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
            return;

        const char* kind = (type == GL_DEBUG_TYPE_ERROR)               ? "error"
                         : (type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR)  ? "undefined behaviour"
                         : (type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR) ? "deprecated"
                         : (type == GL_DEBUG_TYPE_PORTABILITY)         ? "portability"
                         : (type == GL_DEBUG_TYPE_PERFORMANCE)         ? "performance"
                         : "other";

        LL_WARNS("HeadlessGL") << "GL " << kind << " [" << id << "]: "
                               << (message ? message : "(no message)") << LL_ENDL;
    }

    inline void installDebugMessageCallback()
    {
        if (!glDebugMessageCallback)
        {
            LL_WARNS("HeadlessGL") << "No glDebugMessageCallback; GL debug output is off" << LL_ENDL;
            return;
        }
        glEnable(GL_DEBUG_OUTPUT);
        // Synchronous, so a message arrives while the call that caused it is
        // still on the stack and a breakpoint here is worth setting.
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(&onGLDebugMessage, nullptr);
    }

    // Owns the GL context plus the GL-side singletons that the llrender
    // code paths assume are alive. Construction order is context →
    // LLImageGL → LLFontManager (→ TestShaderMgr + gUIProgram when
    // needs_render); teardown is the reverse.
    struct HeadlessGL
    {
        static constexpr S32 WIDTH  = 256;
        static constexpr S32 HEIGHT = 256;

        // needs_render = true compiles gUIProgram and binds it so
        // LLFontVertexBuffer::render() / LLFontGL::render() complete
        // end-to-end against OSMesa. Implies the other three flags
        // (rendering needs vbos + imagegl + llrender to be live).
        HeadlessGL(bool needs_vbos = true, bool needs_imagegl = true,
                   bool needs_llrender = true, bool needs_render = false)
        {
            if (needs_render)
            {
                needs_vbos = needs_imagegl = needs_llrender = true;
            }

#if !LL_WINDOWS && !LL_DARWIN
            // Where there is no display -- CI, a container, WSL without an X
            // or Wayland socket -- SDL's offscreen driver gives a context over
            // EGL with no surface at all. With a display, the default driver's
            // hidden window is closer to what ships.
            if (!SDL_getenv("SDL_VIDEO_DRIVER")
                && !SDL_getenv("DISPLAY")
                && !SDL_getenv("WAYLAND_DISPLAY"))
            {
                SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
            }
#endif

            if (!SDL_Init(SDL_INIT_VIDEO))
            {
                throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
            }
            mOwnsSDL = true;

            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            // A debug context costs nothing here and turns "the draw came out
            // wrong" into a message naming the call that did it. Tests are
            // exactly where that is worth paying for.
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

            mSDLWindow = SDL_CreateWindow("llrender-gl-test", WIDTH, HEIGHT,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            if (!mSDLWindow)
            {
                throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
            }

            mSDLContext = SDL_GL_CreateContext(mSDLWindow);
            if (!mSDLContext)
            {
                throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
            }
            SDL_GL_MakeCurrent(mSDLWindow, mSDLContext);

            // The window used to do this on the way up. Nothing else will.
            llverify(gGLManager.initGL());

            installDebugMessageCallback();

            // Both of these take an LLWindow only to hand to worker threads
            // they are not being asked to start: ENABLE_GL_WORK_QUEUE is 0,
            // and the texture and media threads are off below.
            if (needs_vbos)
            {
                LLVertexBuffer::initClass(nullptr);
            }

            if (needs_llrender)
            {
                gGL.init(needs_vbos);
            }

            if (needs_imagegl)
            {
                // num_catagories=1 mirrors the smallest sane init the viewer uses;
                // the test never tags textures so the count is moot beyond ≥1.
                // thread_texture_loads/thread_media_updates default to false so no
                // worker threads are spun up — keeps teardown simple.
                LLImageGL::initClass(nullptr, LLGLTexture::MAX_GL_IMAGE_CATEGORY, false, false, false);
            }

            LLFontManager::initClass();

            if (needs_render)
            {
                mShaderMgr = std::make_unique<TestShaderMgr>();
                if (!setupTestUIProgram(mShaderMgr.get()))
                {
                    throw std::runtime_error("Failed to compile stub gUIProgram");
                }
                gUIProgram.bind();

                // Pixel-coord ortho for the 256x256 framebuffer; tests render
                // at known pixel positions and verify via glReadPixels.
                glViewport(0, 0, WIDTH, HEIGHT);
                gGL.matrixMode(LLRender::MM_PROJECTION);
                gGL.pushMatrix();
                gGL.loadIdentity();
                gGL.ortho(0.f, (F32)WIDTH, 0.f, (F32)HEIGHT, -1.f, 1.f);
                gGL.matrixMode(LLRender::MM_MODELVIEW);
                gGL.pushMatrix();
                gGL.loadIdentity();
            }
        }

        ~HeadlessGL()
        {
            if (mShaderMgr)
            {
                gGL.matrixMode(LLRender::MM_PROJECTION);
                gGL.popMatrix();
                gGL.matrixMode(LLRender::MM_MODELVIEW);
                gGL.popMatrix();

                teardownTestUIProgram();
                mShaderMgr.reset();
            }

            LLFontManager::cleanupClass();
            LLImageGL::cleanupClass();
            gGL.shutdown();
            LLVertexBuffer::cleanupClass();
            // Drop ALUniformBuffer's process-lifetime statics (streaming rings, the
            // rigged-palette scratch ring) with this context: a stale buffer name
            // surviving into the next fixture's fresh context would alias or fail.
            ALUniformBuffer::cleanupClass();

            gGLManager.shutdownGL();
            if (mSDLContext)
            {
                SDL_GL_DestroyContext(mSDLContext);
            }
            if (mSDLWindow)
            {
                SDL_DestroyWindow(mSDLWindow);
            }
            if (mOwnsSDL)
            {
                SDL_Quit();
            }
        }

        void swapBuffer()
        {
            SDL_GL_SwapWindow(mSDLWindow);
        }

        // Clear the framebuffer to opaque black so a fresh readFramebufferRGBA
        // reflects only what the test rendered this turn.
        void clearFramebuffer()
        {
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        HeadlessGL(const HeadlessGL&)            = delete;
        HeadlessGL& operator=(const HeadlessGL&) = delete;

    private:
        SDL_Window*   mSDLWindow  = nullptr;
        SDL_GLContext mSDLContext = nullptr;
        bool          mOwnsSDL    = false;
        std::unique_ptr<TestShaderMgr> mShaderMgr;
    };
}

#endif // LL_LLHEADLESSGL_FIXTURE_H
