/**
 * @file altextdraw_bench.cpp
 * @brief What a frame of the code editor costs to draw
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

// What the script studio's editor costs to draw, a frame at a time, over a
// script of 50,000 lines, LSL and SLua side by side: the code editor as the
// studio makes it, drawn into llrender's hidden window with the viewer's GL.
// A number is the CPU's milliseconds per frame: the median of five samples,
// each as many frames as fit in fifty milliseconds. What is counted of a
// frame is the editor's draw and the batch flushed, which is everything
// the viewer's thread does for it, the driver's work on each draw call
// included; the GPU is waited for between frames, off the clock, so that
// nothing queues up across them and what it takes to draw is not counted.
//
// Nothing here changes between frames, so every row is an idle frame: what
// the editor spends redrawing what it drew the frame before. The output is a
// table, read against the same row from another build on the same quiet
// machine. An unoptimised build, or one with no GL, exits 125, which CTest
// reads as skipped.

#include "linden_common.h"

#include "llglheaders.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include "alcodeeditor.h"

#include "alheadlessui_fixture.h"
#include "albigscript.h"

#include "../lluictrlfactory.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <list>
#include <memory>
#include <string>

#if defined(LL_RELEASE)
namespace
{
    using clock = std::chrono::steady_clock;

    constexpr int LINES = 50000;

    // Milliseconds per call of `frame`, which says how long its own part
    // took.
    double ms_per_frame(const std::function<double()>& frame)
    {
        frame();
        double samples[5];
        for (double& sample : samples)
        {
            size_t     frames  = 0;
            double     counted = 0.0;
            const auto start   = clock::now();
            do
            {
                counted += frame();
                ++frames;
            } while (clock::now() - start < std::chrono::milliseconds(50));
            sample = counted / double(frames);
        }
        std::sort(samples, samples + 5);
        return samples[2];
    }

    struct Subject
    {
        const char*   name;
        const char*   syntax;
        std::string   text;
        ALCodeEditor* editor = nullptr;
    };

    ALCodeEditor* makeEditor(const Subject& s)
    {
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name         = s.name;
        p.rect         = LLRect(0, 800, 1000, 0);
        p.default_text = "";
        p.syntax       = s.syntax;
        ALCodeEditor* editor = LLUICtrlFactory::create<ALCodeEditor>(p);
        editor->setFont(LLFontGL::getFontMonospace());
        editor->setText(s.text);
        return editor;
    }

    // The draw and the batch handed over, on the clock; then the GPU
    // waited for.
    double drawFrame(ALCodeEditor& e)
    {
        const auto start = clock::now();
        e.draw();
        gGL.flush();
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - start).count();
        glFinish();
        return ms;
    }

    void row(const char* name, double lsl, double slua)
    {
        std::printf("  %-52s %10.3f %10.3f\n", name, lsl, slua);
    }

    // How many draw calls a frame makes: each flush of the batch the
    // renderer is given, taken down rather than drawn from its cache.
    size_t drawCalls(ALCodeEditor& e)
    {
        std::list<LLVertexBufferData> capture;
        gGL.beginList(&capture);
        e.draw();
        gGL.flush();
        gGL.endList();
        glFinish();
        return capture.size();
    }

    template <class Setup>
    void countBoth(const char* name, Subject (&subjects)[2], Setup&& setup)
    {
        size_t counts[2];
        for (int i = 0; i < 2; ++i)
        {
            ALCodeEditor& e = *subjects[i].editor;
            setup(e);
            counts[i] = drawCalls(e);
        }
        std::printf("  %-52s %10zu %10zu\n", name, counts[0], counts[1]);
    }

    template <class Setup>
    void both(const char* name, Subject (&subjects)[2], Setup&& setup)
    {
        double ms[2];
        for (int i = 0; i < 2; ++i)
        {
            ALCodeEditor& e = *subjects[i].editor;
            setup(e);
            ms[i] = ms_per_frame([&] { return drawFrame(e); });
        }
        row(name, ms[0], ms[1]);
    }

    // Every place a word is written, as a whole word or not.
    std::vector<ALTextRange> placesOf(const ALCodeEditor& e, const std::string& word)
    {
        std::vector<ALTextRange> out;
        const ALTextDocument&    doc = e.document();
        for (S32 line = 0; line < doc.lineCount(); ++line)
        {
            const std::string& text = doc.line(line);
            for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + word.size()))
            {
                out.emplace_back(ALTextPos(line, static_cast<S32>(at)), ALTextPos(line, static_cast<S32>(at + word.size())));
            }
        }
        return out;
    }

    // A squiggle under each line's text, from its indentation to its end:
    // a problem on every line, as a script mid-rewrite has.
    std::vector<ALCodeEditor::Decoration> squiggleEveryLine(const ALCodeEditor& e)
    {
        std::vector<ALCodeEditor::Decoration> out;
        const ALTextDocument&                 doc = e.document();
        for (S32 line = 0; line < doc.lineCount(); ++line)
        {
            const std::string& text  = doc.line(line);
            const size_t      begin = text.find_first_not_of(" \t");
            if (begin == std::string::npos)
            {
                continue;
            }
            ALCodeEditor::Decoration d;
            d.range = ALTextRange(ALTextPos(line, static_cast<S32>(begin)), ALTextPos(line, static_cast<S32>(text.size())));
            d.color = LLColor4::red;
            out.push_back(d);
        }
        return out;
    }
}
#endif // LL_RELEASE

int main(int, char**)
{
#if !defined(LL_RELEASE)
    std::printf("Skipped: an unoptimised build has no numbers worth reading\n");
    return 125;
#else
    // The context first: the fonts put their glyphs in its textures.
    std::unique_ptr<ll_test::HeadlessGL> gl;
    try
    {
        gl = std::make_unique<ll_test::HeadlessGL>(true, true, true, /*needs_render=*/true);
        ll_test::installWhiteTexture();
    }
    catch (const std::exception& e)
    {
        std::printf("Skipped: no GL (%s)\n", e.what());
        return 125;
    }
    ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
    if (!ui.ok())
    {
        std::printf("Skipped: no UI, since LLUI_TEST_APP_DIR does not point at the source tree\n");
        return 125;
    }

    Subject subjects[2] = {
        { "lsl", "lsl", ll_test::bigLSL(LINES), nullptr },
        { "slua", "slua", ll_test::bigSLua(LINES), nullptr },
    };
    for (Subject& s : subjects)
    {
        s.editor = makeEditor(s);
    }

    std::printf("altextdraw_bench: the code editor drawn, 1000 x 800, over generated scripts (ms per frame)\n");
    std::printf("\n  %-52s %10s %10s\n", "", "LSL", "SLua");

    std::printf("\nAn idle frame at the top\n");
    both("plain", subjects, [](ALCodeEditor&) {});
    both("a squiggle under every line", subjects, [](ALCodeEditor& e) { e.setDecorations(squiggleEveryLine(e)); });
    both("  and every \"total\" lit", subjects, [](ALCodeEditor& e) { e.setHighlights(ALCodeEditor::Highlight::Search, placesOf(e, "total")); });
    for (Subject& s : subjects)
    {
        s.editor->setDecorations({});
        s.editor->clearHighlights();
    }
    both("with the map beside the text", subjects, [](ALCodeEditor& e) { e.setScrollMap(true); });
    for (Subject& s : subjects)
    {
        s.editor->setScrollMap(false);
    }

    // A line of 20,000 characters near the top, as generated code or a list
    // written out at length has: scrolled across to its middle, then
    // wrapped, which makes it every row in sight.
    std::printf("\nA line of 20,000 characters near the top\n");
    {
        std::string long_line;
        while (long_line.size() < 20000)
        {
            long_line += "value = f(x, [y, z]) + ";
        }
        for (Subject& s : subjects)
        {
            s.editor->setCaret(ALTextPos(2, 0));
            s.editor->insertText(long_line + "\n");
        }
        both("scrolled across to its middle", subjects, [](ALCodeEditor& e) { e.setScrollX(e.layout().line(2).width * 0.5f); });
        both("wrapped", subjects, [](ALCodeEditor& e) {
            e.setScrollX(0.f);
            e.setWordWrap(true);
        });
        countBoth("  draw calls", subjects, [](ALCodeEditor&) {});
        for (Subject& s : subjects)
        {
            s.editor->setWordWrap(false);
            s.editor->deleteRange(ALTextRange(ALTextPos(2, 0), ALTextPos(3, 0)));
        }
    }

    std::printf("\nDraw calls a frame\n");
    countBoth("plain", subjects, [](ALCodeEditor&) {});
    countBoth("a squiggle under every line", subjects, [](ALCodeEditor& e) { e.setDecorations(squiggleEveryLine(e)); });
    countBoth("the map beside the text", subjects, [](ALCodeEditor& e) {
        e.setDecorations({});
        e.setScrollMap(true);
    });
    for (Subject& s : subjects)
    {
        s.editor->setScrollMap(false);
    }

    for (Subject& s : subjects)
    {
        delete s.editor;
    }
    return 0;
#endif
}
