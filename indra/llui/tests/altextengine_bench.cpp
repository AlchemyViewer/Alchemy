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

#include "alcodeeditor.h"
#include "alfindbar.h"
#include "alfoldmodel.h"
#include "aloutputview.h"
#include "alquickopen.h"
#include "../llspellcheckengine.h"
#include "altextsearch.h"
#include "alvimkeymap.h"

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

// Only an optimised build measures, and only it has a use for these: an
// unoptimised one would say they are unused.
#if defined(LL_RELEASE)
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

    // As ms_per_item, a pass at a time, but each pass's own setup -- an
    // editor emptied before a load -- left out of what is timed.
    double ms_per_item_after(const std::function<void()>& setup, const std::function<void()>& pass)
    {
        setup();
        pass();
        double samples[5];
        for (double& sample : samples)
        {
            size_t          passes = 0;
            clock::duration spent{};
            const auto      start = clock::now();
            do
            {
                setup();
                const auto from = clock::now();
                pass();
                spent += clock::now() - from;
                ++passes;
            } while (clock::now() - start < std::chrono::milliseconds(50));
            sample = std::chrono::duration<double, std::milli>(spent).count() / double(passes);
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

    // A text with every third line that is a closer alone -- `}`, `end` --
    // left empty, as a paste cut short leaves its blocks: never closed.
    std::string withoutClosers(const std::string& text, std::string_view closer)
    {
        std::string out;
        out.reserve(text.size());
        size_t at   = 0;
        S32    seen = 0;
        while (at < text.size())
        {
            const size_t           end   = std::min(text.find('\n', at), text.size());
            const std::string_view line  = std::string_view(text).substr(at, end - at);
            const size_t           begin = line.find_first_not_of(" \t");
            const bool             alone = begin != std::string_view::npos && line.substr(begin) == closer;
            if (!alone || ++seen % 3 != 0)
            {
                out.append(line);
            }
            if (end < text.size())
            {
                out.push_back('\n');
            }
            at = end + 1;
        }
        return out;
    }

    // Keys typed as vim sees them: the key, then its character where the
    // key was not taken; an escape character is Escape.
    void typeKeys(ALCodeEditor& e, const char* keys)
    {
        for (const char* c = keys; *c; ++c)
        {
            if (*c == '\x1b')
            {
                e.handleKeyHere(KEY_ESCAPE, MASK_NONE);
            }
            else if (!e.handleKeyHere(static_cast<KEY>(toupper(static_cast<unsigned char>(*c))), MASK_NONE))
            {
                e.handleUnicodeCharHere(static_cast<llwchar>(static_cast<unsigned char>(*c)));
            }
        }
    }

    void row(const char* name, double lsl, double slua, const char* note = "")
    {
        std::printf("  %-52s %10.3f %10.3f  %s\n", name, lsl, slua, note);
    }

    // A row with one number, not one for each language.
    void rowOne(const char* name, double ms)
    {
        std::printf("  %-52s %10.3f\n", name, ms);
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

    template <typename S, typename F>
    void bothAfter(const char* name, Subject (&subjects)[2], S&& setup, F&& op)
    {
        double ms[2];
        for (int i = 0; i < 2; ++i)
        {
            ALCodeEditor& e = *subjects[i].editor;
            ms[i]           = ms_per_item_after([&] { setup(subjects[i], e); }, [&] { op(subjects[i], e); });
        }
        row(name, ms[0], ms[1]);
    }
}
#endif // LL_RELEASE

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
    // Into an editor holding nothing, as a script opened is; and over the
    // same text again, as one loaded again is, whose lines are made in
    // the strings of the lines they replace.
    bothAfter("load: setText into an empty text", subjects, [](Subject&, ALCodeEditor& e) { e.setText(std::string_view()); },
              [](Subject& s, ALCodeEditor& e) { e.setText(s.text); });
    both("load: setText over the same text", subjects, 1, [](Subject& s, ALCodeEditor& e) { e.setText(s.text); });
    both("full highlight: every line lexed", subjects, 1, [](Subject&, ALCodeEditor& e) {
        e.highlighter().wordsChanged();
        g_sink = g_sink + e.highlighter().tokens(lastLine(e)).size();
    });
    // The whole script inside one comment -- LSL's /* */, SLua's long
    // bracket --[==[ ]==] -- lexed: every byte of it inside a span.
    {
        const char* openers[2] = { "/*\n", "--[==[\n" };
        const char* closers[2] = { "\n*/", "\n]==]" };
        for (int i = 0; i < 2; ++i)
        {
            subjects[i].editor->setText(openers[i] + subjects[i].text + closers[i]);
        }
        both("full highlight: all of it in one comment", subjects, 1, [](Subject&, ALCodeEditor& e) {
            e.highlighter().wordsChanged();
            g_sink = g_sink + e.highlighter().tokens(lastLine(e)).size();
        });
        for (Subject& s : subjects)
        {
            s.editor->setText(s.text);
        }
    }

    std::printf("\nAn edit at the top (typed and taken away, through the journal)\n");
    for (Subject& s : subjects)
    {
        s.editor->highlighter().tokens(lastLine(*s.editor));
    }
    both("the edit alone, per edit", subjects, 2, [](Subject&, ALCodeEditor& e) { editAtTop(e, "x"); });
    both("a line broken at the top and joined, the edit alone", subjects, 1, [](Subject&, ALCodeEditor& e) {
        e.setCaret(ALTextPos(0, 0));
        e.insertText("\n");
        e.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
    });
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

    // Completion as a word is typed at the end of the script, the list
    // open: each key narrows it, over the grammar's words, the language's
    // and the script's own.
    std::printf("\nCompletion\n");
    for (Subject& s : subjects)
    {
        s.editor->setCaret(s.editor->document().end());
        s.editor->insertText("\n");
    }
    size_t offered[2] = {};
    both("a word typed and taken back, the list open, per key", subjects, 8, [&](Subject& s, ALCodeEditor& e) {
        for (const char c : std::string("tota"))
        {
            e.handleUnicodeCharHere(static_cast<llwchar>(c));
        }
        offered[&s - subjects] = e.completions().size();
        for (int i = 0; i < 4; ++i)
        {
            e.handleKeyHere(KEY_BACKSPACE, MASK_NONE);
        }
    });
    countRow("  offered at four letters", offered[0], offered[1]);
    for (Subject& s : subjects)
    {
        ALCodeEditor& e = *s.editor;
        e.closeCompletion();
        e.deleteRange(ALTextRange(e.document().lineEnd(lastLine(e) - 1), e.document().end()));
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
    both("Replace All: (\\w+)\\s*\\( for $1 (, then undone", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        e.findBar()->setQuery("(\\w+)\\s*\\(");
        e.findBar()->setReplacement("$1 (");
        e.findBar()->findChild<LLUICtrl>("regex")->handleMouseDown(0, 0, MASK_NONE);
        replaced[&s - subjects] = e.replaceAllMatches();
        e.findBar()->findChild<LLUICtrl>("regex")->handleMouseDown(0, 0, MASK_NONE);
        e.undo();
    });
    countRow("  replaced", replaced[0], replaced[1]);
    for (Subject& s : subjects)
    {
        s.editor->findBar()->setQuery("total");
    }
    // A word typed into the bar a key at a time, the bar open over the text.
    both("a query typed into the find bar, per key", subjects, 5, [&](Subject&, ALCodeEditor& e) {
        for (const char* typed : { "t", "to", "tot", "tota", "total" })
        {
            e.findBar()->setQuery(typed);
        }
    });
    for (Subject& s : subjects)
    {
        g_sink = g_sink + s.editor->findMatches().size();
    }

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

    // The spell check over comments and strings, as the platform's own
    // checker answers it: every line checked afresh, and Next Misspelling
    // round a script with none, the most it ever looks at.
    {
        std::unique_ptr<LLSpellCheckEngine> engine = LLSpellCheckEngine::create();
        const bool                          real   = engine && engine->setLanguage("en_US");
        std::printf("\nSpelling (%s)\n", real ? "the platform's checker, en_US" : "no checker to be had: every word right");
        size_t asked = 0;
        auto   check = [&](const std::string& word) {
            ++asked;
            return !real || engine->checkWord(word);
        };
        for (Subject& s : subjects)
        {
            s.editor->setSpellChecker(check);
            s.editor->setSpellCheck(true);
        }
        size_t words[2] = {};
        both("every line checked afresh", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
            e.recheckSpelling();
            asked = 0;
            for (S32 line = 0; line < e.document().lineCount(); ++line)
            {
                g_sink = g_sink + e.misspellings(line).size();
            }
            words[&s - subjects] = asked;
        });
        countRow("  words asked of the checker", words[0], words[1]);
        both("Next Misspelling round a script with none, afresh", subjects, 1, [](Subject&, ALCodeEditor& e) {
            e.recheckSpelling();
            g_sink = g_sink + e.misspellingFrom(ALTextPos(0, 0), true).has_value();
        });
        both("Next Misspelling round it again, checked already", subjects, 1, [](Subject&, ALCodeEditor& e) {
            g_sink = g_sink + e.misspellingFrom(ALTextPos(0, 0), true).has_value();
        });
        for (Subject& s : subjects)
        {
            s.editor->setSpellCheck(false);
            s.editor->setSpellChecker(nullptr);
        }
    }

    std::printf("\nEvery line at once\n");
    both("Select All and Tab, then undone", subjects, 1, [](Subject&, ALCodeEditor& e) {
        e.perform(ALEditorCommand::SelectAll);
        e.handleKeyHere(KEY_TAB, MASK_NONE);
        e.undo();
    });

    // What the crash journal writes as typing goes on: a history of
    // 3,000 steps, a character typed at the top before each write. As LLSD
    // built step by step, as it was, and as notation from what each step
    // was written as the last time.
    std::printf("\nThe history written against a crash (3,000 steps)\n");
    for (Subject& s : subjects)
    {
        ALCodeEditor& e = *s.editor;
        e.undoJournal().clear();
        for (S32 i = 0; i < 3000; ++i)
        {
            // Each at a line of its own: a step of its own.
            e.setCaret(ALTextPos(i * 7 % lastLine(e), 0));
            e.insertText("x");
        }
    }
    size_t written[2] = {};
    both("as LLSD, every step built", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        e.setCaret(ALTextPos(0, 0));
        e.insertText("y");
        const LLSD history      = e.undoJournal().asLLSD();
        written[&s - subjects] = history["undo"].size();
        g_sink                 = g_sink + written[&s - subjects];
    });
    both("as notation, from each step's last", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        e.setCaret(ALTextPos(0, 0));
        e.insertText("y");
        g_sink = g_sink + e.undoJournal().asNotation().size();
    });
    countRow("  steps written", written[0], written[1]);
    // The scripts as they were: an x at the start of every seventh line
    // makes SLua's ends names, and every block after the first so made
    // would be measured open to the end of the text.
    for (Subject& s : subjects)
    {
        s.editor->setText(s.text);
        s.editor->undoJournal().clear();
    }

    // A bracket left open at the top: its partner looked for as the caret
    // rests beside it, and the depth the rainbow and the indent read at the
    // end, each after a character typed and taken away below it.
    // Where a caret is as it is seen: every line's end counted in columns,
    // as the trailer and vim's vertical motions ask of one line at a time;
    // then the same of a line with a combining accent in it, which the
    // characters are walked for.
    std::printf("\nDisplay columns (every line's end)\n");
    both("ASCII lines, per thousand", subjects, static_cast<size_t>(LINES / 1000), [](Subject&, ALCodeEditor& e) {
        const ALTextDocument& d = e.document();
        for (S32 line = 0; line < d.lineCount(); ++line)
        {
            g_sink = g_sink + static_cast<size_t>(d.displayColumn(ALTextPos(line, d.lineLength(line)), 4));
        }
    });
    {
        const ALTextDocument accented("\tinteger cafe\xCC\x81 = llStringLength(\"cr\xC3\xA8" "me br\xC3\xBB" "l\xC3\xA9" "e\"); // \xE6\x97\xA5\xE6\x9C\xAC");
        rowOne("a line with accents and CJK, per thousand", ms_per_item(1, [&] {
            for (int i = 0; i < 1000; ++i)
            {
                g_sink = g_sink + static_cast<size_t>(accented.displayColumn(ALTextPos(0, accented.lineLength(0)), 4));
            }
        }));
    }

    // A line of 20,000 characters, as generated code or a list written out
    // at length has, put in near the top: a character typed in its middle
    // and taken away, the line laid out again after each; and the x of a
    // column along it with the column back from that x, as the caret and
    // the mouse ask.
    std::printf("\nA line of 20,000 characters\n");
    {
        std::string long_line;
        while (long_line.size() < 20000)
        {
            long_line += "value = f(x, [y, z]) + ";
        }
        for (Subject& s : subjects)
        {
            s.editor->setCaret(ALTextPos(5, 0));
            s.editor->insertText(long_line + "\n");
        }
        // Each pass another letter somewhere else in the middle, so that the
        // line is one the shaper has not seen, as it is after a keystroke.
        both("a keystroke in its middle, laid out again", subjects, 2, [](Subject&, ALCodeEditor& e) {
            static S32 pass = 0;
            ++pass;
            const S32  at     = 5000 + (pass * 7) % 10000;
            const char typed[2] = { static_cast<char>('a' + pass % 26), 0 };
            e.setCaret(ALTextPos(5, at));
            e.insertText(typed);
            g_sink = g_sink + e.layout().line(5).glyphs.size();
            e.deleteRange(ALTextRange(ALTextPos(5, at), ALTextPos(5, at + 1)));
            g_sink = g_sink + e.layout().line(5).glyphs.size();
        });
        both("a column's x and the column at an x, per thousand", subjects, 1, [](Subject&, ALCodeEditor& e) {
            ALTextLayout& laid = e.layout();
            for (S32 i = 0; i < 1000; ++i)
            {
                const S32 column = (i * 7919) % 20000;
                g_sink           = g_sink + static_cast<size_t>(laid.columnAt(5, 0, laid.xOf(5, column) + 1.f, true));
            }
        });
        for (Subject& s : subjects)
        {
            s.editor->deleteRange(ALTextRange(ALTextPos(5, 0), ALTextPos(6, 0)));
        }
    }

    // The widest line, of 400 characters, near the top: a character typed
    // at its end and taken away; and the same with the widest asked after
    // each, as the horizontal scrollbar asks after every key.
    std::printf("\nThe widest line\n");
    for (Subject& s : subjects)
    {
        s.editor->setCaret(ALTextPos(5, 0));
        s.editor->insertText(std::string(400, 'w') + "\n");
    }
    const auto typedAtWidest = [](ALCodeEditor& e, bool ask) {
        const S32 end = e.document().lineLength(5);
        e.setCaret(ALTextPos(5, end));
        e.insertText("x");
        g_sink = g_sink + (ask ? static_cast<size_t>(e.layout().contentWidth()) : 0);
        e.deleteRange(ALTextRange(ALTextPos(5, end), ALTextPos(5, end + 1)));
        g_sink = g_sink + (ask ? static_cast<size_t>(e.layout().contentWidth()) : 0);
    };
    both("a character typed at its end and taken back, per edit", subjects, 2, [&](Subject&, ALCodeEditor& e) { typedAtWidest(e, false); });
    both("the same, the widest asked after each", subjects, 2, [&](Subject&, ALCodeEditor& e) { typedAtWidest(e, true); });
    for (Subject& s : subjects)
    {
        s.editor->deleteRange(ALTextRange(ALTextPos(5, 0), ALTextPos(6, 0)));
    }

    // Word wrap on: a character typed at the top and taken away, and the
    // last line's top found after each, which every line's height above it
    // goes into; the width moved by a pixel and back, and the lines in
    // view laid out again after each, as a window dragged wider does; and
    // a line's top asked at random, as drawing and the mouse ask.
    std::printf("\nWord wrap\n");
    for (Subject& s : subjects)
    {
        s.editor->setWordWrap(true);
        for (S32 line = 0; line < 60; ++line)
        {
            g_sink = g_sink + s.editor->layout().line(line).rows.size();
        }
    }
    both("a keystroke at the top, the last line's top found", subjects, 2, [](Subject&, ALCodeEditor& e) {
        e.setCaret(ALTextPos(0, 0));
        e.insertText("x");
        g_sink = g_sink + static_cast<size_t>(e.layout().lineTop(lastLine(e)));
        e.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)));
        g_sink = g_sink + static_cast<size_t>(e.layout().lineTop(lastLine(e)));
    });
    both("the width moved by a pixel, 60 lines laid out again", subjects, 2, [](Subject&, ALCodeEditor& e) {
        ALTextLayout& laid  = e.layout();
        const S32     width = laid.wrapWidth();
        for (const S32 to : { width + 1, width })
        {
            laid.setWrapWidth(to);
            for (S32 line = 0; line < 60; ++line)
            {
                g_sink = g_sink + laid.line(line).rows.size();
            }
            g_sink = g_sink + static_cast<size_t>(laid.lineTop(60));
        }
    });
    both("a line's top and the line at a y, per thousand", subjects, 1, [](Subject&, ALCodeEditor& e) {
        ALTextLayout& laid = e.layout();
        for (S32 i = 0; i < 1000; ++i)
        {
            const S32 line = (i * 7919) % LINES;
            g_sink         = g_sink + static_cast<size_t>(laid.lineAtY(laid.lineTop(line) + 1));
        }
    });
    for (Subject& s : subjects)
    {
        s.editor->setWordWrap(false);
    }

    std::printf("\nBrackets (an opener at the top, unmatched)\n");
    for (Subject& s : subjects)
    {
        s.editor->setCaret(ALTextPos(0, 0));
        s.editor->insertText("(");
    }
    both("its partner looked for, per look", subjects, 1, [](Subject&, ALCodeEditor& e) {
        ALTextPos out;
        g_sink = g_sink + e.matchBracketAt(ALTextPos(0, 0), out);
    });
    both("an edit on the next line, then looked for again", subjects, 1, [](Subject&, ALCodeEditor& e) {
        e.setCaret(ALTextPos(1, 0));
        e.insertText("x");
        e.deleteRange(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 1)));
        ALTextPos out;
        g_sink = g_sink + e.matchBracketAt(ALTextPos(0, 0), out);
    });
    both("an edit at the top, then the depth at the end", subjects, 1, [](Subject&, ALCodeEditor& e) {
        e.setCaret(ALTextPos(0, 1));
        e.insertText("x");
        e.deleteRange(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 2)));
        g_sink = g_sink + e.bracketDepthAt(e.document().end());
    });
    for (Subject& s : subjects)
    {
        s.editor->deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)));
        s.editor->undoJournal().clear();
    }

    std::printf("\nFolding\n");
    size_t regions[2] = {};
    both("fold regions rebuilt", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        ALFoldModel model;
        regions[&s - subjects] = model.regions(e.document(), e.getTabWidth()).size();
        g_sink                 = g_sink + regions[&s - subjects];
    });
    countRow("  regions", regions[0], regions[1]);
    both("an edit at the top, then the editor's fold regions", subjects, 1, [&](Subject&, ALCodeEditor& e) {
        editAtTop(e, "x");
        g_sink = g_sink + e.foldRegions().size();
    });
    const auto brokenAndJoined = [](Subject&, ALCodeEditor& e) {
        e.setCaret(ALTextPos(0, 0));
        e.insertText("\n");
        e.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        g_sink = g_sink + e.foldRegions().size();
    };
    both("a line broken at the top and joined, then the editor's fold regions", subjects, 1, brokenAndJoined);
    // A third of the blocks never closed, so that what is open grows down
    // the text, and every place the walk keeps is under all of it.
    for (Subject& s : subjects)
    {
        s.editor->setText(withoutClosers(s.text, &s == subjects ? "}" : "end"));
        g_sink = g_sink + s.editor->foldRegions().size();
    }
    both("a third never closed: the same", subjects, 1, brokenAndJoined);
    for (Subject& s : subjects)
    {
        s.editor->setText(s.text);
        s.editor->undoJournal().clear();
    }

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

    // Vim's commands over the lines a :g picks, each line's change one
    // edit or all of them one; and text typed again by `.`.
    std::printf("\nVim over every line\n");
    ALVimKeymap* vims[2] = {};
    for (Subject& s : subjects)
    {
        auto keymap         = std::make_unique<ALVimKeymap>();
        vims[&s - subjects] = keymap.get();
        s.editor->setModalKeymap(std::move(keymap));
    }
    both(":g/total/s//count/, then undone", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        vims[&s - subjects]->takeLine(e, ':', "g/total/s//count/", true);
        e.undo();
    });
    both(":%s/\\(\\w\\+\\)\\s*(/\\1 (/g, then undone", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        vims[&s - subjects]->takeLine(e, ':', "%s/\\(\\w\\+\\)\\s*(/\\1 (/g", true);
        e.undo();
    });
    both(":g/total/>, then undone", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        vims[&s - subjects]->takeLine(e, ':', "g/total/>", true);
        e.undo();
    });
    both(":g/total/d, then undone", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        vims[&s - subjects]->takeLine(e, ':', "g/total/d", true);
        e.undo();
    });
    for (Subject& s : subjects)
    {
        s.editor->setCaret(ALTextPos(0, 0));
        typeKeys(*s.editor, "Athe quick brown fox jumps over the lazy dog, the quick brown fox jumps over the lazy dog, and again.\x1b");
    }
    both("`.`: a 100-character insert typed again, then undone", subjects, 1, [](Subject&, ALCodeEditor& e) {
        typeKeys(e, ".");
        e.undo();
    });
    // Stepping by character along that line, as l does.
    both("l along a line of 100 characters, per step", subjects, 100, [](Subject&, ALCodeEditor& e) {
        typeKeys(e, "0");
        for (int i = 0; i < 99; ++i)
        {
            typeKeys(e, "l");
        }
    });
    // A search, then n through its matches; and a search typed on the
    // search line, with the cursor moved along it, as incsearch sees it.
    for (Subject& s : subjects)
    {
        vims[&s - subjects]->takeLine(*s.editor, '/', "total", true);
    }
    both("n through the matches of \"total\", per n", subjects, 10, [](Subject&, ALCodeEditor& e) {
        typeKeys(e, "nnnnnnnnnn");
    });
    both("/total typed, the cursor moved along it, per key", subjects, 15, [](Subject&, ALCodeEditor& e) {
        typeKeys(e, "/total");
        for (int i = 0; i < 4; ++i)
        {
            e.handleKeyHere(KEY_LEFT, MASK_NONE);
        }
        for (int i = 0; i < 4; ++i)
        {
            e.handleKeyHere(KEY_RIGHT, MASK_NONE);
        }
        e.handleKeyHere(KEY_ESCAPE, MASK_NONE);
    });
    // A macro of 500 motions played back while a mapping is made: every
    // key it feeds is looked at as the start of one.
    for (Subject& s : subjects)
    {
        vims[&s - subjects]->takeLine(*s.editor, ':', "nmap <F5> x", true);
        s.editor->setCaret(ALTextPos(0, 0));
        typeKeys(*s.editor, "gg");
        std::string recorded = "qa";
        recorded += std::string(250, 'j') + std::string(250, 'k') + "q";
        typeKeys(*s.editor, recorded.c_str());
    }
    both("@a: a macro of 500 motions, a mapping made", subjects, 1, [](Subject&, ALCodeEditor& e) { typeKeys(e, "@a"); });
    // The whole text selected by lines, then moved within: every motion
    // makes a selection of all of it.
    for (Subject& s : subjects)
    {
        typeKeys(*s.editor, "ggVG");
    }
    both("a motion with the whole text selected (ggVG, then k and j)", subjects, 2, [](Subject&, ALCodeEditor& e) { typeKeys(e, "kj"); });
    for (Subject& s : subjects)
    {
        s.editor->handleKeyHere(KEY_ESCAPE, MASK_NONE);
    }
    both(":sort, then undone", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        vims[&s - subjects]->takeLine(e, ':', "sort", true);
        e.undo();
    });
    both(":sort i, then undone", subjects, 1, [&](Subject& s, ALCodeEditor& e) {
        vims[&s - subjects]->takeLine(e, ':', "sort i", true);
        e.undo();
    });

    for (Subject& s : subjects)
    {
        s.editor->die();
    }

    // The Output pane's log, full: four lanes of 500 entries, every other
    // one an error and every fourth with a URL in it.
    std::printf("\nThe Output log (2,000 entries)\n");
    {
        ALOutputView::Params p(LLUICtrlFactory::getDefaultParams<ALOutputView>());
        p.name              = "output";
        p.rect              = LLRect(0, 800, 1000, 0);
        p.capacity          = 500;
        ALOutputView* log   = LLUICtrlFactory::create<ALOutputView>(p);
        log->setFont(LLFontGL::getFontMonospace());
        S32        serial   = 0;
        const auto next_one = [&serial]() {
            ALOutputView::Entry entry;
            entry.time   = "12:00:00";
            entry.source = "Object";
            entry.kind   = serial % 2 ? "error" : "";
            entry.lane   = static_cast<U8>(serial % 4);
            entry.text   = serial % 4 == 0 ? llformat("message %d, see http://example.com/page/%d", serial, serial)
                                           : llformat("message %d: the quick brown fox jumps over the lazy dog", serial);
            ++serial;
            return entry;
        };
        for (S32 i = 0; i < 2000; ++i)
        {
            log->append(next_one());
        }
        rowOne("a filter typed: the errors alone, then all again", ms_per_item(1, [&] {
                   log->setFilter([](const ALOutputView::Entry& entry) { return entry.kind == "error"; });
                   log->setFilter(nullptr);
                   g_sink = g_sink + log->document().lineCount();
               }));
        rowOne("an entry past the fill, the oldest let go of", ms_per_item(100, [&] {
                   for (S32 i = 0; i < 100; ++i)
                   {
                       log->append(next_one());
                   }
                   g_sink = g_sink + log->document().lineCount();
               }));
        log->die();
    }
    // Quick Open over the names of a big project, a query typed a letter
    // at a time and taken back.
    std::printf("\nQuick Open (2,000 candidates)\n");
    {
        ALQuickOpen::Params p(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
        p.name              = "quick";
        p.rect              = LLRect(0, 400, 500, 0);
        ALQuickOpen* quick  = LLUICtrlFactory::create<ALQuickOpen>(p);
        std::vector<ALQuickOpen::Candidate> names;
        for (S32 i = 0; i < 2000; ++i)
        {
            ALQuickOpen::Candidate one;
            one.label  = llformat("%s_%s_%d.xml", i % 3 ? "floater" : "panel", i % 5 ? "script_studio" : "preferences", i);
            one.detail = "skins/default/xui/en";
            one.also   = llformat("Script Studio %d", i);
            one.value  = one.label;
            names.push_back(std::move(one));
        }
        quick->setCandidates(std::move(names));
        rowOne("a query typed and taken back, per key", ms_per_item(10, [&] {
                   const std::string query = "flscst";
                   for (size_t n = 1; n <= query.size(); ++n)
                   {
                       quick->setQuery(query.substr(0, n));
                   }
                   for (size_t n = query.size() - 1; n > 0; --n)
                   {
                       quick->setQuery(query.substr(0, n));
                   }
                   quick->setQuery(std::string());
                   g_sink = g_sink + quick->listed().size();
               }));
        quick->die();
    }
    std::printf("\n(checksum %zu)\n", static_cast<size_t>(g_sink));
    return 0;
#endif
}
