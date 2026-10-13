/**
 * @file llrender_test_checks.cpp
 * @brief The state guard's checks on what llrender keeps for the process: the
 *        GL context, gGL's matrix stacks and switches, the bound shader, the
 *        live GL images, the font manager, and the DPI, scale and UI
 *        transform text is laid out at.
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

#include "../llfontfreetype.h"
#include "../llfontgl.h"
#include "../llglslshader.h"
#include "../llimagegl.h"
#include "../llrender.h"

#include "alteststateguard.h"

#include "SDL3/SDL_video.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <string>

namespace ll_test
{
    // Friended in llrender.h, which keeps its stacks to itself.
    struct RenderProbe
    {
        // How many matrices each of gGL's stacks has pushed, and which stack
        // its matrix calls go to.
        static std::string matrixStacks()
        {
            static const char* const names[LLRender::NUM_MATRIX_MODES] = { "modelview", "projection", "texture" };
            std::string text;
            for (U32 mode = 0; mode < LLRender::NUM_MATRIX_MODES; ++mode)
            {
                text += std::string(names[mode]) + " " + std::to_string(gGL.mMatIdx[mode]) + ", ";
            }
            return text + "calls to " + names[gGL.mMatrixMode];
        }
    };
}

namespace
{
    // The context current on this thread: its handle, or none. The hidden
    // window's context is SDL's, on every platform.
    std::string currentContext()
    {
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        return context ? llformat("%p", static_cast<void*>(context)) : std::string("none");
    }

    // The errors GL holds for the next glGetError, which reading clears.
    std::string unreadErrors()
    {
        if (!SDL_GL_GetCurrentContext())
        {
            return "no context";
        }
        std::string text;
        // A context holds one flag per kind of error, so few can be pending.
        for (S32 i = 0; i < 8; ++i)
        {
            const GLenum error = glGetError();
            if (error == GL_NO_ERROR)
            {
                break;
            }
            text += llformat("%s0x%04x", text.empty() ? "" : ", ", error);
        }
        return text.empty() ? std::string("none") : text;
    }

    class LLRenderChecks : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(Catch::TestRunInfo const&) override
        {
            // A case that makes a context of its own ends it, and one on the
            // shared context leaves that one current.
            ll_test::StateGuard::add("the GL context", currentContext);
            // A case pops what it pushed: the stacks are LL_MATRIX_STACK_DEPTH
            // deep, and a case after one that left a push draws through it.
            ll_test::StateGuard::add("gGL's matrix stacks", ll_test::RenderProbe::matrixStacks);
            ll_test::StateGuard::add("the bound shader", []
            {
                const LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
                return shader ? shader->mName : std::string("none");
            });
            ll_test::StateGuard::add("LLRender's switches", []
            {
                return std::string("reverse-Z ") + (LLRender::sReverseZ ? "on" : "off")
                    + ", compute " + (LLRender::sCompute ? "on" : "off");
            });
            ll_test::StateGuard::add("the GL viewport", []
            {
                return llformat("%d %d %d %d", gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3]);
            });
            // A case reads the errors it made: left, the next case's own
            // check for none would find them. Reading them clears them, so
            // the guard reads what is left once it has reported, and the
            // next case starts with none rather than being blamed for that.
            ll_test::StateGuard::add("unread GL errors", unreadErrors, [] { unreadErrors(); });
            // A case that ends its context releases the images it made in it:
            // one left over holds a name in a context that is gone. While a
            // shared context lives, the images in it may outlive a case, as
            // the font atlas pages it rasterizes into do.
            ll_test::StateGuard::add("GL images outliving their context", []
            {
                return SDL_GL_GetCurrentContext() ? std::string("a context is current")
                                                  : std::to_string(LLImageGL::sImageList.size());
            });
            ll_test::StateGuard::add("the font manager",
                                     [] { return std::string(gFontManagerp ? "live" : "none"); });
            // The fonts are made for the process at one DPI and scale, which
            // a later case's text is laid out and measured at.
            ll_test::StateGuard::add("LLFontGL's DPI and scale", []
            {
                return llformat("DPI %g x %g, scale %g x %g", LLFontGL::sHorizDPI, LLFontGL::sVertDPI,
                                LLFontGL::sScaleX, LLFontGL::sScaleY);
            });
            // A case that moves the UI origin, or pushes a transform, puts it
            // back: the next case's text and images land where it says.
            ll_test::StateGuard::add("LLFontGL's UI transform", []
            {
                return llformat("origin %g %g, depth %g, scale %g x %g, %zu pushed", LLFontGL::sCurOrigin.mX,
                                LLFontGL::sCurOrigin.mY, LLFontGL::sCurDepth, LLFontGL::sCurScaleX,
                                LLFontGL::sCurScaleY, LLFontGL::sOriginStack.size());
            });
        }
    };
}

CATCH_REGISTER_LISTENER(LLRenderChecks)
