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
// Each benchmark is one frame, named for its language, and its time is the
// CPU's; Catch2 reports the mean of its samples and the interval around it.
// What is counted of a frame is the editor's draw and the batch flushed,
// which is everything the viewer's thread does for it, the driver's work on
// each draw call included; the GPU is waited for between frames, off the
// clock, so that nothing queues up across them and what it takes to draw is
// not counted.
//
// Nothing here changes between frames, so every row is an idle frame: what
// the editor spends redrawing what it drew the frame before. The output is a
// table, read against the same row from another build on the same quiet
// machine.

#include "linden_common.h"

#include "llglheaders.h"

#include "aluigl_fixture.h"

#include "alcodeeditor.h"

#include "alheadlessui_fixture.h"
#include "albigscript.h"
#include "altestbenchmark.h"

#include "../lluictrlfactory.h"

#include <cstdio>
#include <list>
#include <string>
#include <vector>

namespace
{
    constexpr int LINES = 50000;

    // The scripts, made once for every case of a run.
    struct Scripts
    {
        std::string lsl  = ll_test::bigLSL(LINES);
        std::string slua = ll_test::bigSLua(LINES);
    };

    const Scripts& scripts()
    {
        static const Scripts made;
        return made;
    }

    struct Subject
    {
        const char*        name;
        const char*        syntax;
        // The column its numbers were read under, which ends each
        // benchmark's name.
        const char*        language;
        const std::string& text;
        ALCodeEditor*      editor = nullptr;
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

    // Both languages' editors, made for a case in the binary's shared
    // context, whose UI puts the fonts' glyphs in its textures, and dying
    // when the case ends however it ends.
    struct Editors
    {
        Subject subjects[2] = {
            { "lsl", "lsl", "LSL", scripts().lsl },
            { "slua", "slua", "SLua", scripts().slua },
        };

        Editors()
        {
            ll_test::uiGL();
            ll_test::installWhiteTexture();
            if (!ll_test::HeadlessUI::get(/*gl_textures=*/true).ok())
            {
                SKIP("no UI, since LLUI_TEST_APP_DIR does not point at the source tree");
            }
            for (Subject& s : subjects)
            {
                s.editor = makeEditor(s);
            }
        }

        ~Editors()
        {
            for (Subject& s : subjects)
            {
                s.editor->die();
            }
        }

        Subject* begin() { return subjects; }
        Subject* end() { return subjects + 2; }
    };

    // A frame of each language's editor, once `setup` has made it what the
    // row draws: the draw and the batch handed over, on the clock; then the
    // GPU waited for. A frame is one run of a sample, so the wait is between
    // every two.
    template <class Setup>
    void both(const char* name, Editors& editors, Setup&& setup)
    {
        for (Subject& s : editors)
        {
            ALCodeEditor& e = *s.editor;
            setup(e);
            BENCHMARK_ADVANCED(std::string(name) + ", " + s.language)(Catch::Benchmark::Chronometer meter)
            {
                meter.measure([&] {
                    e.draw();
                    gGL.flush();
                });
                glFinish();
            };
        }
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

    // A table of counts, under its heading and the languages' columns.
    void countHeading(const char* heading)
    {
        std::printf("\n%s\n  %-52s %10s %10s\n", heading, "", "LSL", "SLua");
    }

    template <class Setup>
    void countBoth(const char* name, Editors& editors, Setup&& setup)
    {
        size_t counts[2];
        for (int i = 0; i < 2; ++i)
        {
            ALCodeEditor& e = *editors.subjects[i].editor;
            setup(e);
            counts[i] = drawCalls(e);
        }
        std::printf("  %-52s %10zu %10zu\n", name, counts[0], counts[1]);
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

    TEST_CASE("altextdraw_bench: an idle frame at the top", "[altextdraw_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        both("plain", editors, [](ALCodeEditor&) {});
        both("a squiggle under every line", editors, [](ALCodeEditor& e) { e.setDecorations(squiggleEveryLine(e)); });
        both("a squiggle under every line and every \"total\" lit", editors,
             [](ALCodeEditor& e) { e.setHighlights(ALCodeEditor::Highlight::Search, placesOf(e, "total")); });
        for (Subject& s : editors)
        {
            s.editor->setDecorations({});
            s.editor->clearHighlights();
        }
        both("with the map beside the text", editors, [](ALCodeEditor& e) { e.setScrollMap(true); });
    }

    // A line of 20,000 characters near the top, as generated code or a list
    // written out at length has: scrolled across to its middle, then
    // wrapped, which makes it every row in sight.
    TEST_CASE("altextdraw_bench: a line of twenty thousand characters near the top", "[altextdraw_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors     editors;
        std::string long_line;
        while (long_line.size() < 20000)
        {
            long_line += "value = f(x, [y, z]) + ";
        }
        for (Subject& s : editors)
        {
            s.editor->setCaret(ALTextPos(2, 0));
            s.editor->insertText(long_line + "\n");
        }
        both("scrolled across to its middle", editors, [](ALCodeEditor& e) { e.setScrollX(e.layout().line(2).width * 0.5f); });
        both("wrapped", editors, [](ALCodeEditor& e) {
            e.setScrollX(0.f);
            e.setWordWrap(true);
        });

        countHeading("A line of 20,000 characters near the top");
        countBoth("draw calls, wrapped", editors, [](ALCodeEditor&) {});
        std::fflush(stdout);
    }

    // Each after a frame drawn plain, as an editor in sight has been.
    TEST_CASE("altextdraw_bench: draw calls a frame", "[altextdraw_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        for (Subject& s : editors)
        {
            drawCalls(*s.editor);
        }
        countHeading("Draw calls a frame");
        countBoth("plain", editors, [](ALCodeEditor&) {});
        countBoth("a squiggle under every line", editors, [](ALCodeEditor& e) { e.setDecorations(squiggleEveryLine(e)); });
        countBoth("the map beside the text", editors, [](ALCodeEditor& e) {
            e.setDecorations({});
            e.setScrollMap(true);
        });
        std::fflush(stdout);
    }
}
