/**
 * @file altextengine_bench.cpp
 * @brief The text engine over big scripts: loading, highlighting, an edit at
 *        the top, finding, folding and wrapping.
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

// What the script studio's editor costs over a script of 50,000 lines, LSL
// and SLua side by side: the code editor as the studio makes it, over the
// source tree's fonts with no window behind it. A number is milliseconds
// per operation: the median of five samples, each as many passes as fit in
// fifty milliseconds (and at least one). Where an operation leaves the text
// changed, the pass puts it back, and the number is per edit.
//
// The output is a table, not a verdict; a number is read against the same
// row from another build, on the same quiet machine. Every change to the
// edit pipeline, the highlighter, the layout, the folds or find is measured
// here before and after. An unoptimised build exits 125, which CTest reads
// as skipped, since its numbers would say nothing.

#include "linden_common.h"

#include "../alcodeeditor.h"
#include "../alfindbar.h"
#include "../alfoldmodel.h"
#include "../altextsearch.h"

#include "alheadlessui_fixture.h"
#include "albigscript.h"

#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

class LLAvatarName;
const std::string gBenchAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gBenchAnonName;
}

namespace
{
    using clock = std::chrono::steady_clock;

    constexpr int LINES = 50000;

    // Everything a row finds feeds this, so nothing is found for nothing.
    volatile size_t g_sink = 0;

    // Milliseconds per item: `pass` does `count` of them.
    double ms_per_item(size_t count, const std::function<void()>& pass)
    {
        pass();
        double samples[5];
        for (double& sample : samples)
        {
            size_t          passes = 0;
            const auto      start  = clock::now();
            clock::duration elapsed{};
            do
            {
                pass();
                ++passes;
                elapsed = clock::now() - start;
            } while (elapsed < std::chrono::milliseconds(50));
            sample = std::chrono::duration<double, std::milli>(elapsed).count() / (double(passes) * double(count));
        }
        std::sort(samples, samples + 5);
        return samples[2];
    }

    // One language's editor, over its big script.
    struct Subject
    {
        const char*   name;
        const char*   syntax;
        std::string   text;
        ALCodeEditor* editor = nullptr;
        // What opens a block comment at the top of the text, and what it
        // opens, closed again.
        const char*   opener;
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

    S32 lastLine(ALCodeEditor& e) { return e.document().lineCount() - 1; }

    // A character typed at the top of the text and taken away again, both
    // through the undo journal as a person's edits go.
    void editAtTop(ALCodeEditor& e, std::string_view what)
    {
        e.setCaret(ALTextPos(0, 0));
        e.insertText(what);
        e.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(0, static_cast<S32>(what.size()))));
    }

    void row(const char* name, double lsl, double slua, const char* note = "")
    {
        std::printf("  %-52s %10.3f %10.3f  %s\n", name, lsl, slua, note);
    }

    void countRow(const char* name, size_t lsl, size_t slua)
    {
        std::printf("  %-52s %10zu %10zu\n", name, lsl, slua);
    }

    template <class F>
    void both(const char* name, Subject (&subjects)[2], size_t count, F&& op)
    {
        double ms[2];
        for (int i = 0; i < 2; ++i)
        {
            ALCodeEditor& e = *subjects[i].editor;
            ms[i]           = ms_per_item(count, [&] { op(subjects[i], e); });
        }
        row(name, ms[0], ms[1]);
    }
}

int main(int, char**)
{
#if !defined(LL_RELEASE)
    std::printf("Skipped: an unoptimised build has no numbers worth reading\n");
    return 125;
#else
    ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
    if (!ui.ok())
    {
        std::printf("Skipped: no UI, since LLUI_TEST_APP_DIR does not point at the source tree\n");
        return 125;
    }

    Subject subjects[2] = {
        { "lsl", "lsl", ll_test::bigLSL(LINES), nullptr, "/*" },
        { "slua", "slua", ll_test::bigSLua(LINES), nullptr, "--[[" },
    };
    for (Subject& s : subjects)
    {
        s.editor = makeEditor(s);
    }

    std::printf("altextengine_bench: the code editor over generated scripts (ms per operation)\n");
    std::printf("\n  %-52s %10s %10s\n", "", "LSL", "SLua");
    countRow("lines", subjects[0].editor->document().lineCount(), subjects[1].editor->document().lineCount());
    countRow("bytes", subjects[0].text.size(), subjects[1].text.size());

    std::printf("\nLoading and highlighting\n");
    both("load: setText", subjects, 1, [](Subject& s, ALCodeEditor& e) { e.setText(s.text); });
    both("full highlight: every line lexed", subjects, 1, [](Subject&, ALCodeEditor& e) {
        e.highlighter().wordsChanged();
        g_sink = g_sink + e.highlighter().tokens(lastLine(e)).size();
    });

    std::printf("\nAn edit at the top (typed and taken away, through the journal)\n");
    for (Subject& s : subjects)
    {
        s.editor->highlighter().tokens(lastLine(*s.editor));
    }
    both("the edit alone, per edit", subjects, 2, [](Subject&, ALCodeEditor& e) { editAtTop(e, "x"); });
    size_t relexed[2] = {};
    both("the edit, then the highlight to the end, per edit", subjects, 2, [&](Subject& s, ALCodeEditor& e) {
        e.setCaret(ALTextPos(0, 0));
        e.insertText("x");
        g_sink = g_sink + e.highlighter().tokens(lastLine(e)).size();
        relexed[&s - subjects] = e.highlighter().lastLexed();
        e.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)));
        g_sink = g_sink + e.highlighter().tokens(lastLine(e)).size();
    });
    countRow("  lines relexed after it (where relexing stops)", relexed[0], relexed[1]);
    both("a block comment opened at the top, then closed", subjects, 2, [&](Subject& s, ALCodeEditor& e) {
        e.setCaret(ALTextPos(0, 0));
        e.insertText(s.opener);
        g_sink = g_sink + e.highlighter().tokens(lastLine(e)).size();
        relexed[&s - subjects] = e.highlighter().lastLexed();
        e.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(0, static_cast<S32>(strlen(s.opener)))));
        g_sink = g_sink + e.highlighter().tokens(lastLine(e)).size();
    });
    countRow("  lines relexed (the first comment's end stops it)", relexed[0], relexed[1]);

    // Only the undo timed: a hundred characters typed at the top as a run,
    // then taken back in one step.
    {
        double ms[2];
        for (int i = 0; i < 2; ++i)
        {
            ALCodeEditor& e = *subjects[i].editor;
            double        samples[5];
            for (double& sample : samples)
            {
                e.setCaret(ALTextPos(0, 0));
                for (int c = 0; c < 100; ++c)
                {
                    e.handleUnicodeCharHere(static_cast<llwchar>('a' + c % 26));
                }
                const auto start = clock::now();
                e.undo();
                sample = std::chrono::duration<double, std::milli>(clock::now() - start).count();
            }
            std::sort(samples, samples + 5);
            ms[i] = samples[2];
        }
        row("undo of a 100-character typing run", ms[0], ms[1]);
    }

    std::printf("\nFinding\n");
    size_t found[2] = {};
    both("regex find-all: every call, \\w+\\s*\\(", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        ALTextSearchOptions options;
        options.regex         = true;
        options.caseSensitive = true;
        found[&s - subjects]  = ALTextSearch::matches(e.document(), "\\w+\\s*\\(", options).size();
        g_sink                = g_sink + found[&s - subjects];
    });
    countRow("  matches", found[0], found[1]);
    both("plain find-all: total", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        ALTextSearchOptions options;
        found[&s - subjects] = ALTextSearch::matches(e.document(), "total", options).size();
        g_sink               = g_sink + found[&s - subjects];
    });
    countRow("  matches", found[0], found[1]);

    size_t replaced[2] = {};
    both("Replace All: total for count, then undone", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        if (!e.findShown())
        {
            e.showFind(true);
            e.findBar()->setQuery("total");
            e.findBar()->setReplacement("count");
        }
        replaced[&s - subjects] = e.replaceAllMatches();
        e.undo();
    });
    countRow("  replaced", replaced[0], replaced[1]);

    // What lies over the text slides with each edit: every match found,
    // a squiggle on every other line.
    std::printf("\nLayers over the text\n");
    size_t lit[2] = {};
    for (Subject& s : subjects)
    {
        lit[&s - subjects] = s.editor->findMatches().size();
    }
    both("an edit at the top, every match of \"total\" lit", subjects, 2, [](Subject&, ALCodeEditor& e) { editAtTop(e, "x"); });
    countRow("  matches lit", lit[0], lit[1]);
    for (Subject& s : subjects)
    {
        s.editor->hideFind();
        std::vector<ALCodeEditor::Decoration> squiggles;
        for (S32 line = 0; line < s.editor->document().lineCount(); line += 2)
        {
            ALCodeEditor::Decoration d;
            d.range = ALTextRange(ALTextPos(line, 0), ALTextPos(line, llmin(4, s.editor->document().lineLength(line))));
            squiggles.push_back(d);
        }
        s.editor->setDecorations(std::move(squiggles));
    }
    both("an edit at the top, a squiggle on every other line", subjects, 2, [](Subject&, ALCodeEditor& e) { editAtTop(e, "x"); });
    for (Subject& s : subjects)
    {
        s.editor->setDecorations({});
    }

    std::printf("\nFolding\n");
    size_t regions[2] = {};
    both("fold regions rebuilt", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        ALFoldModel model;
        regions[&s - subjects] = model.regions(e.document(), e.getTabWidth()).size();
        g_sink                 = g_sink + regions[&s - subjects];
    });
    countRow("  regions", regions[0], regions[1]);

    std::printf("\nLaying out every line\n");
    const S32 tab   = subjects[0].editor->layout().tabWidth();
    S32       width = 600;
    both("unwrapped", subjects, 1, [&](Subject&, ALCodeEditor& e) {
        ALTextLayout& layout = e.layout();
        // A tab width either way, so that every pass lays out afresh.
        layout.setTabWidth(layout.tabWidth() == tab ? tab + 1 : tab);
        for (S32 line = 0; line < layout.lineCount(); ++line)
        {
            g_sink = g_sink + layout.rowCount(line);
        }
        g_sink = g_sink + layout.totalHeight();
    });
    for (Subject& s : subjects)
    {
        s.editor->layout().setTabWidth(tab);
    }
    both("wrapped at 600 px", subjects, 1, [&](Subject&, ALCodeEditor& e) {
        ALTextLayout& layout = e.layout();
        // One pixel either way, so that every pass lays out afresh.
        width = width == 600 ? 601 : 600;
        layout.setWrapWidth(width);
        for (S32 line = 0; line < layout.lineCount(); ++line)
        {
            g_sink = g_sink + layout.rowCount(line);
        }
        g_sink = g_sink + layout.totalHeight();
    });

    for (Subject& s : subjects)
    {
        s.editor->die();
    }
    std::printf("\n(checksum %zu)\n", static_cast<size_t>(g_sink));
    return 0;
#endif
}
