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
// source tree's fonts with no window behind it. Each benchmark is one
// operation, named for its language, or one pass named for what it covers;
// Catch2 reports the mean of its samples and the interval around it. Where
// an operation leaves the text changed, the calls take turns putting it
// back -- a character typed, then taken away -- and each is one edit.
//
// The output is a table, not a verdict; a number is read against the same
// row from another build, on the same quiet machine. Every change to the
// edit pipeline, the highlighter, the layout, the folds or find is measured
// here before and after.

#include "linden_common.h"

#include "alcodeeditor.h"
#include "alfindbar.h"
#include "alfoldmodel.h"
#include "aloutputview.h"
#include "alquickopen.h"
#include "../llspellcheckengine.h"
#include "altextfind.h"
#include "altextsearch.h"
#include "alvimkeymap.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"
#include "albigscript.h"
#include "altestbenchmark.h"

#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include <algorithm>
#include <cstdio>
#include <memory>
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

    // One language's editor, over its big script.
    struct Subject
    {
        const char*        name;
        const char*        syntax;
        // The column its numbers were read under, which ends each
        // benchmark's name.
        const char*        language;
        const std::string& text;
        // What opens a block comment at the top of the text, and what it
        // opens, closed again.
        const char*        opener;
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

    // The headless UI the binary made before its first case: a case skips
    // where it could not be.
    void requireUI()
    {
        if (!ll_test::HeadlessUI::get().ok())
        {
            SKIP("no UI, since LLUI_TEST_APP_DIR does not point at the source tree");
        }
    }

    // The views a case made, dying when it ends however it ends, and focus
    // -- which the find bar takes, and the text when it goes -- put back as
    // the case found it.
    struct Views
    {
        ll_test::FocusScope  focus;
        std::vector<LLView*> made;

        ~Views()
        {
            for (LLView* view : made)
            {
                view->die();
            }
        }

        template <class T>
        T* keep(T* view)
        {
            made.push_back(view);
            return view;
        }
    };

    // The find's worker, which a text this long is looked through on,
    // closed and waited for when a case ends: nothing of its own left
    // running.
    struct WorkerClosed
    {
        ~WorkerClosed() { ALTextFind::closeWorker(); }
    };

    // Both languages' editors, made for a case.
    struct Editors
    {
        WorkerClosed worker;
        Views        views;
        Subject      subjects[2] = {
            { "lsl", "lsl", "LSL", scripts().lsl, "/*" },
            { "slua", "slua", "SLua", scripts().slua, "--[[" },
        };

        Editors()
        {
            requireUI();
            for (Subject& s : subjects)
            {
                s.editor = views.keep(makeEditor(s));
            }
        }

        Subject* begin() { return subjects; }
        Subject* end() { return subjects + 2; }
    };

    S32 lastLine(ALCodeEditor& e) { return e.document().lineCount() - 1; }

    // A character typed at the top of the text, or taken away again: a
    // step of the two that leave the text as it was, each through the undo
    // journal as a person's edits go.
    void editAtTop(ALCodeEditor& e, std::string_view what, size_t step)
    {
        if (step == 0)
        {
            e.setCaret(ALTextPos(0, 0));
            e.insertText(what);
        }
        else
        {
            e.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(0, static_cast<S32>(what.size()))));
        }
    }

    // Both steps.
    void editAtTop(ALCodeEditor& e, std::string_view what)
    {
        editAtTop(e, what, 0);
        editAtTop(e, what, 1);
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

    // A table of counts, under its heading and the languages' columns.
    void countHeading(const char* heading)
    {
        std::printf("\n%s\n  %-52s %10s %10s\n", heading, "", "LSL", "SLua");
    }

    void countRow(const char* name, size_t lsl, size_t slua)
    {
        std::printf("  %-52s %10zu %10zu\n", name, lsl, slua);
    }

    // A benchmark for each language, named for the row and its column.
    template <class F>
    void both(const char* name, Editors& editors, F&& op)
    {
        for (Subject& s : editors)
        {
            ALCodeEditor& e = *s.editor;
            BENCHMARK(std::string(name) + ", " + s.language)
            {
                return op(s, e);
            };
        }
    }

    // As both, each call a step of a pass of `steps` that leaves the text
    // as it found it -- a character typed, then taken away -- taken in
    // turn; and the pass finished after, off the clock, so that the next
    // row finds the text as this one did.
    template <class F>
    void bothInSteps(const char* name, Editors& editors, size_t steps, F&& op)
    {
        for (Subject& s : editors)
        {
            ALCodeEditor& e     = *s.editor;
            size_t        calls = 0;
            BENCHMARK(std::string(name) + ", " + s.language)
            {
                return op(s, e, calls++ % steps);
            };
            while (calls % steps != 0)
            {
                op(s, e, calls++ % steps);
            }
        }
    }

    // As both, each call's own setup -- an editor emptied before a load --
    // left off the clock. An operation of milliseconds is one run of a
    // sample, so the setup is before every one.
    template <typename S, typename F>
    void bothAfter(const char* name, Editors& editors, S&& setup, F&& op)
    {
        for (Subject& s : editors)
        {
            ALCodeEditor& e = *s.editor;
            BENCHMARK_ADVANCED(std::string(name) + ", " + s.language)(Catch::Benchmark::Chronometer meter)
            {
                setup(s, e);
                meter.measure([&] { op(s, e); });
            };
        }
    }

    TEST_CASE("altextengine_bench: loading and highlighting", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        countHeading("The scripts");
        countRow("lines", editors.subjects[0].editor->document().lineCount(), editors.subjects[1].editor->document().lineCount());
        countRow("bytes", editors.subjects[0].text.size(), editors.subjects[1].text.size());
        std::fflush(stdout);

        // Into an editor holding nothing, as a script opened is; and over the
        // same text again, as one loaded again is, whose lines are made in
        // the strings of the lines they replace.
        bothAfter("load: setText into an empty text", editors, [](Subject&, ALCodeEditor& e) { e.setText(std::string_view()); },
                  [](Subject& s, ALCodeEditor& e) { e.setText(s.text); });
        both("load: setText over the same text", editors, [](Subject& s, ALCodeEditor& e) { e.setText(s.text); });
        both("full highlight: every line lexed", editors, [](Subject&, ALCodeEditor& e) {
            e.highlighter().wordsChanged();
            return e.highlighter().tokens(lastLine(e)).size();
        });
        // The whole script inside one comment -- LSL's /* */, SLua's long
        // bracket --[==[ ]==] -- lexed: every byte of it inside a span.
        const char* openers[2] = { "/*\n", "--[==[\n" };
        const char* closers[2] = { "\n*/", "\n]==]" };
        for (int i = 0; i < 2; ++i)
        {
            editors.subjects[i].editor->setText(openers[i] + editors.subjects[i].text + closers[i]);
        }
        both("full highlight: all of it in one comment", editors, [](Subject&, ALCodeEditor& e) {
            e.highlighter().wordsChanged();
            return e.highlighter().tokens(lastLine(e)).size();
        });
    }

    TEST_CASE("altextengine_bench: an edit at the top typed and taken away", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        for (Subject& s : editors)
        {
            s.editor->highlighter().tokens(lastLine(*s.editor));
        }
        bothInSteps("the edit alone, per edit", editors, 2, [](Subject&, ALCodeEditor& e, size_t step) { editAtTop(e, "x", step); });
        both("a line broken at the top and joined, the edit alone", editors, [](Subject&, ALCodeEditor& e) {
            e.setCaret(ALTextPos(0, 0));
            e.insertText("\n");
            e.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        });
        size_t relexed[2] = {};
        size_t comment[2] = {};
        bothInSteps("the edit, then the highlight to the end, per edit", editors, 2, [&](Subject& s, ALCodeEditor& e, size_t step) {
            editAtTop(e, "x", step);
            const size_t tokens = e.highlighter().tokens(lastLine(e)).size();
            if (step == 0)
            {
                relexed[&s - editors.subjects] = e.highlighter().lastLexed();
            }
            return tokens;
        });
        bothInSteps("a block comment opened at the top, then closed", editors, 2, [&](Subject& s, ALCodeEditor& e, size_t step) {
            editAtTop(e, s.opener, step);
            const size_t tokens = e.highlighter().tokens(lastLine(e)).size();
            if (step == 0)
            {
                comment[&s - editors.subjects] = e.highlighter().lastLexed();
            }
            return tokens;
        });

        // A hundred characters typed at the top as a run, then taken back in
        // one step and put back in another, a step a call. Typed again for
        // every undo, the run would cost far more than the undo it is for:
        // Catch2 warms an operation this quick up by running it tens of
        // thousands of times at once.
        for (Subject& s : editors)
        {
            s.editor->setCaret(ALTextPos(0, 0));
            for (int c = 0; c < 100; ++c)
            {
                s.editor->handleUnicodeCharHere(static_cast<llwchar>('a' + c % 26));
            }
        }
        bothInSteps("undo of a 100-character typing run", editors, 2, [](Subject&, ALCodeEditor& e, size_t step) {
            if (step == 0)
            {
                e.undo();
            }
            else
            {
                e.redo();
            }
        });

        countHeading("An edit at the top (typed and taken away, through the journal)");
        countRow("lines relexed after it (where relexing stops)", relexed[0], relexed[1]);
        countRow("lines relexed (the first comment's end stops it)", comment[0], comment[1]);
        std::fflush(stdout);
    }

    // Completion as a word is typed at the end of the script, the list
    // open: each key narrows it, over the grammar's words, the language's
    // and the script's own.
    TEST_CASE("altextengine_bench: completion as a word is typed", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        for (Subject& s : editors)
        {
            s.editor->setCaret(s.editor->document().end());
            s.editor->insertText("\n");
        }
        size_t offered[2] = {};
        bothInSteps("a word typed and taken back, the list open, per key", editors, 8, [&](Subject& s, ALCodeEditor& e, size_t step) {
            if (step < 4)
            {
                e.handleUnicodeCharHere(static_cast<llwchar>("tota"[step]));
                if (step == 3)
                {
                    offered[&s - editors.subjects] = e.completions().size();
                }
            }
            else
            {
                e.handleKeyHere(KEY_BACKSPACE, MASK_NONE);
            }
        });
        for (Subject& s : editors)
        {
            s.editor->closeCompletion();
        }

        countHeading("Completion");
        countRow("offered at four letters", offered[0], offered[1]);
        std::fflush(stdout);
    }

    TEST_CASE("altextengine_bench: finding and replacing", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        size_t  found[2][2] = {};
        both("regex find-all: every call, \\w+\\s*\\(", editors, [&](Subject& s, ALCodeEditor& e) {
            ALTextSearchOptions options;
            options.regex         = true;
            options.caseSensitive = true;
            return found[0][&s - editors.subjects] = ALTextSearch::matches(e.document(), "\\w+\\s*\\(", options).size();
        });
        both("plain find-all: total", editors, [&](Subject& s, ALCodeEditor& e) {
            ALTextSearchOptions options;
            return found[1][&s - editors.subjects] = ALTextSearch::matches(e.document(), "total", options).size();
        });

        for (Subject& s : editors)
        {
            s.editor->showFind(true);
            s.editor->findBar()->setQuery("total");
            s.editor->findBar()->setReplacement("count");
        }
        size_t replaced[2][2] = {};
        both("Replace All: total for count, then undone", editors, [&](Subject& s, ALCodeEditor& e) {
            replaced[0][&s - editors.subjects] = e.replaceAllMatches();
            e.undo();
        });
        both("Replace All: (\\w+)\\s*\\( for $1 (, then undone", editors, [&](Subject& s, ALCodeEditor& e) {
            e.findBar()->setQuery("(\\w+)\\s*\\(");
            e.findBar()->setReplacement("$1 (");
            e.findBar()->findChild<LLUICtrl>("regex")->handleMouseDown(0, 0, MASK_NONE);
            replaced[1][&s - editors.subjects] = e.replaceAllMatches();
            e.findBar()->findChild<LLUICtrl>("regex")->handleMouseDown(0, 0, MASK_NONE);
            e.undo();
        });
        for (Subject& s : editors)
        {
            s.editor->findBar()->setQuery("total");
        }
        // A word typed into the bar a key at a time, the bar open over the
        // text, a key a call.
        bothInSteps("a query typed into the find bar, per key", editors, 5, [](Subject&, ALCodeEditor& e, size_t step) {
            static const char* const typed[] = { "t", "to", "tot", "tota", "total" };
            e.findBar()->setQuery(typed[step]);
        });

        countHeading("Finding");
        countRow("regex find-all: every call, \\w+\\s*\\(: matches", found[0][0], found[0][1]);
        countRow("plain find-all: total: matches", found[1][0], found[1][1]);
        countRow("Replace All: total for count: replaced", replaced[0][0], replaced[0][1]);
        countRow("Replace All: (\\w+)\\s*\\( for $1 (: replaced", replaced[1][0], replaced[1][1]);
        std::fflush(stdout);
    }

    // What lies over the text slides with each edit: every match found,
    // a squiggle on every other line.
    TEST_CASE("altextengine_bench: layers over the text sliding with an edit", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        size_t  lit[2] = {};
        for (Subject& s : editors)
        {
            s.editor->showFind(true);
            s.editor->findBar()->setQuery("total");
            lit[&s - editors.subjects] = s.editor->findMatches().size();
        }
        bothInSteps("an edit at the top, every match of \"total\" lit", editors, 2,
                    [](Subject&, ALCodeEditor& e, size_t step) { editAtTop(e, "x", step); });
        for (Subject& s : editors)
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
        bothInSteps("an edit at the top, a squiggle on every other line", editors, 2,
                    [](Subject&, ALCodeEditor& e, size_t step) { editAtTop(e, "x", step); });

        countHeading("Layers over the text");
        countRow("matches lit", lit[0], lit[1]);
        std::fflush(stdout);
    }

    // The spell check over comments and strings, as the platform's own
    // checker answers it: every line checked afresh, and Next Misspelling
    // round a script with none, the most it ever looks at.
    TEST_CASE("altextengine_bench: spelling as the platform's checker answers", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;

        std::unique_ptr<LLSpellCheckEngine> engine = LLSpellCheckEngine::create();
        const bool                          real   = engine && engine->setLanguage("en_US");

        size_t asked = 0;
        auto   check = [&](const std::string& word) {
            ++asked;
            return !real || engine->checkWord(word);
        };
        // The checker taken back from the editors however the case ends: it
        // asks the engine, which goes before they do.
        struct Unchecked
        {
            Editors& editors;
            ~Unchecked()
            {
                for (Subject& s : editors)
                {
                    s.editor->setSpellCheck(false);
                    s.editor->setSpellChecker(nullptr);
                }
            }
        } unchecked{ editors };
        for (Subject& s : editors)
        {
            s.editor->setSpellChecker(check);
            s.editor->setSpellCheck(true);
        }
        size_t words[2] = {};
        both("every line checked afresh", editors, [&](Subject& s, ALCodeEditor& e) {
            e.recheckSpelling();
            asked        = 0;
            size_t found = 0;
            for (S32 line = 0; line < e.document().lineCount(); ++line)
            {
                found += e.misspellings(line).size();
            }
            words[&s - editors.subjects] = asked;
            return found;
        });
        both("Next Misspelling round a script with none, afresh", editors, [](Subject&, ALCodeEditor& e) {
            e.recheckSpelling();
            return e.misspellingFrom(ALTextPos(0, 0), true).has_value();
        });
        both("Next Misspelling round it again, checked already", editors,
             [](Subject&, ALCodeEditor& e) { return e.misspellingFrom(ALTextPos(0, 0), true).has_value(); });

        countHeading(real ? "Spelling (the platform's checker, en_US)" : "Spelling (no checker to be had: every word right)");
        countRow("words asked of the checker", words[0], words[1]);
        std::fflush(stdout);
    }

    TEST_CASE("altextengine_bench: every line at once", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        both("Select All and Tab, then undone", editors, [](Subject&, ALCodeEditor& e) {
            e.perform(ALEditorCommand::SelectAll);
            e.handleKeyHere(KEY_TAB, MASK_NONE);
            e.undo();
        });
    }

    // What the crash journal writes as typing goes on: a history of
    // 3,000 steps, a character typed at the top before each write. As LLSD
    // built step by step, as it was, and as notation from what each step
    // was written as the last time. The character is taken back after each
    // write, off the clock, so that every write is of the same history.
    TEST_CASE("altextengine_bench: the history written against a crash", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        for (Subject& s : editors)
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
        const auto typedThenWritten = [&editors](const char* name, auto&& write) {
            for (Subject& s : editors)
            {
                ALCodeEditor& e = *s.editor;
                BENCHMARK_ADVANCED(std::string(name) + ", " + s.language)(Catch::Benchmark::Chronometer meter)
                {
                    // Each y a step of its own, however many a sample
                    // types, so as many undos take them all away and the
                    // next sample writes the same history.
                    meter.measure([&] {
                        e.undoJournal().breakRun();
                        e.setCaret(ALTextPos(0, 0));
                        e.insertText("y");
                        return write(s, e);
                    });
                    for (int i = 0; i < meter.runs(); ++i)
                    {
                        e.undo();
                    }
                };
            }
        };
        size_t written[2] = {};
        typedThenWritten("as LLSD, every step built", [&](Subject& s, ALCodeEditor& e) {
            const LLSD history = e.undoJournal().asLLSD();
            return written[&s - editors.subjects] = static_cast<size_t>(history["undo"].size());
        });
        typedThenWritten("as notation, from each step's last", [](Subject&, ALCodeEditor& e) { return e.undoJournal().asNotation().size(); });

        countHeading("The history written against a crash (3,000 steps)");
        countRow("steps written", written[0], written[1]);
        std::fflush(stdout);
    }

    // Where a caret is as it is seen: every line's end counted in columns,
    // as the trailer and vim's vertical motions ask of one line at a time;
    // then the same of a line with a combining accent in it, which the
    // characters are walked for.
    TEST_CASE("altextengine_bench: display columns at every line's end", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        both("ASCII lines, all 50,000", editors, [](Subject&, ALCodeEditor& e) {
            const ALTextDocument& d       = e.document();
            size_t                columns = 0;
            for (S32 line = 0; line < d.lineCount(); ++line)
            {
                columns += static_cast<size_t>(d.displayColumn(ALTextPos(line, d.lineLength(line)), 4));
            }
            return columns;
        });
        const ALTextDocument accented("\tinteger cafe\xCC\x81 = llStringLength(\"cr\xC3\xA8" "me br\xC3\xBB" "l\xC3\xA9" "e\"); // \xE6\x97\xA5\xE6\x9C\xAC");
        BENCHMARK("a line with accents and CJK, per thousand")
        {
            size_t columns = 0;
            for (int i = 0; i < 1000; ++i)
            {
                columns += static_cast<size_t>(accented.displayColumn(ALTextPos(0, accented.lineLength(0)), 4));
            }
            return columns;
        };
    }

    // A line of 20,000 characters, as generated code or a list written out
    // at length has, put in near the top: a character typed in its middle
    // and taken away, the line laid out again after each; and the x of a
    // column along it with the column back from that x, as the caret and
    // the mouse ask.
    TEST_CASE("altextengine_bench: a line of twenty thousand characters", "[altextengine_bench][!benchmark]")
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
            s.editor->setCaret(ALTextPos(5, 0));
            s.editor->insertText(long_line + "\n");
        }
        // Each keystroke another letter somewhere else in the middle, so
        // that the line is one the shaper has not seen, as it is after a
        // keystroke.
        S32 pass = 0;
        S32 at   = 0;
        bothInSteps("a keystroke in its middle, laid out again", editors, 2, [&](Subject&, ALCodeEditor& e, size_t step) {
            if (step == 0)
            {
                ++pass;
                at                  = 5000 + (pass * 7) % 10000;
                const char typed[2] = { static_cast<char>('a' + pass % 26), 0 };
                e.setCaret(ALTextPos(5, at));
                e.insertText(typed);
            }
            else
            {
                e.deleteRange(ALTextRange(ALTextPos(5, at), ALTextPos(5, at + 1)));
            }
            return e.layout().line(5).glyphs.size();
        });
        both("a column's x and the column at an x, per thousand", editors, [](Subject&, ALCodeEditor& e) {
            ALTextLayout& laid   = e.layout();
            size_t        column = 0;
            for (S32 i = 0; i < 1000; ++i)
            {
                column += static_cast<size_t>(laid.columnAt(5, 0, laid.xOf(5, (i * 7919) % 20000) + 1.f, true));
            }
            return column;
        });
    }

    // The widest line, of 400 characters, near the top: a character typed
    // at its end and taken away; and the same with the widest asked after
    // each, as the horizontal scrollbar asks after every key.
    TEST_CASE("altextengine_bench: the widest line", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        for (Subject& s : editors)
        {
            s.editor->setCaret(ALTextPos(5, 0));
            s.editor->insertText(std::string(400, 'w') + "\n");
        }
        const auto typedAtWidest = [](ALCodeEditor& e, size_t step, bool ask) {
            if (step == 0)
            {
                const S32 end = e.document().lineLength(5);
                e.setCaret(ALTextPos(5, end));
                e.insertText("x");
            }
            else
            {
                const S32 end = e.document().lineLength(5) - 1;
                e.deleteRange(ALTextRange(ALTextPos(5, end), ALTextPos(5, end + 1)));
            }
            return ask ? static_cast<size_t>(e.layout().contentWidth()) : 0;
        };
        bothInSteps("a character typed at its end and taken back, per edit", editors, 2,
                    [&](Subject&, ALCodeEditor& e, size_t step) { return typedAtWidest(e, step, false); });
        bothInSteps("the same, the widest asked after each", editors, 2,
                    [&](Subject&, ALCodeEditor& e, size_t step) { return typedAtWidest(e, step, true); });
    }

    // Word wrap on: a character typed at the top and taken away, and the
    // last line's top found after each, which every line's height above it
    // goes into; the width moved by a pixel and back, and the lines in
    // view laid out again after each, as a window dragged wider does; and
    // a line's top asked at random, as drawing and the mouse ask.
    TEST_CASE("altextengine_bench: word wrap", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        for (Subject& s : editors)
        {
            s.editor->setWordWrap(true);
            for (S32 line = 0; line < 60; ++line)
            {
                s.editor->layout().line(line);
            }
        }
        bothInSteps("a keystroke at the top, the last line's top found", editors, 2, [](Subject&, ALCodeEditor& e, size_t step) {
            editAtTop(e, "x", step);
            return e.layout().lineTop(lastLine(e));
        });
        bothInSteps("the width moved by a pixel, 60 lines laid out again", editors, 2, [](Subject&, ALCodeEditor& e, size_t step) {
            ALTextLayout& laid = e.layout();
            laid.setWrapWidth(laid.wrapWidth() + (step == 0 ? 1 : -1));
            size_t rows = 0;
            for (S32 line = 0; line < 60; ++line)
            {
                rows += laid.line(line).rows.size();
            }
            return rows + static_cast<size_t>(laid.lineTop(60));
        });
        both("a line's top and the line at a y, per thousand", editors, [](Subject&, ALCodeEditor& e) {
            ALTextLayout& laid  = e.layout();
            size_t        lines = 0;
            for (S32 i = 0; i < 1000; ++i)
            {
                lines += static_cast<size_t>(laid.lineAtY(laid.lineTop((i * 7919) % LINES) + 1));
            }
            return lines;
        });
    }

    // A bracket left open at the top: its partner looked for as the caret
    // rests beside it, and the depth the rainbow and the indent read at the
    // end, each after a character typed and taken away below it.
    TEST_CASE("altextengine_bench: brackets with an opener at the top unmatched", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        for (Subject& s : editors)
        {
            s.editor->setCaret(ALTextPos(0, 0));
            s.editor->insertText("(");
        }
        both("its partner looked for, per look", editors, [](Subject&, ALCodeEditor& e) {
            ALTextPos out;
            return e.matchBracketAt(ALTextPos(0, 0), out);
        });
        both("an edit on the next line, then looked for again", editors, [](Subject&, ALCodeEditor& e) {
            e.setCaret(ALTextPos(1, 0));
            e.insertText("x");
            e.deleteRange(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 1)));
            ALTextPos out;
            return e.matchBracketAt(ALTextPos(0, 0), out);
        });
        both("an edit at the top, then the depth at the end", editors, [](Subject&, ALCodeEditor& e) {
            e.setCaret(ALTextPos(0, 1));
            e.insertText("x");
            e.deleteRange(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 2)));
            return e.bracketDepthAt(e.document().end());
        });
    }

    TEST_CASE("altextengine_bench: folding", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors editors;
        size_t  regions[2] = {};
        both("fold regions rebuilt", editors, [&](Subject& s, ALCodeEditor& e) {
            ALFoldModel model;
            return regions[&s - editors.subjects] = model.regions(e.document(), e.getTabWidth()).size();
        });
        both("an edit at the top, then the editor's fold regions", editors, [](Subject&, ALCodeEditor& e) {
            editAtTop(e, "x");
            return e.foldRegions().size();
        });
        const auto brokenAndJoined = [](Subject&, ALCodeEditor& e) {
            e.setCaret(ALTextPos(0, 0));
            e.insertText("\n");
            e.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
            return e.foldRegions().size();
        };
        both("a line broken at the top and joined, then the editor's fold regions", editors, brokenAndJoined);
        // A third of the blocks never closed, so that what is open grows down
        // the text, and every place the walk keeps is under all of it.
        for (Subject& s : editors)
        {
            s.editor->setText(withoutClosers(s.text, &s == editors.subjects ? "}" : "end"));
            s.editor->foldRegions();
        }
        both("a third never closed: the same", editors, brokenAndJoined);

        countHeading("Folding");
        countRow("regions", regions[0], regions[1]);
        std::fflush(stdout);
    }

    TEST_CASE("altextengine_bench: laying out every line", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors   editors;
        const S32 tab   = editors.subjects[0].editor->layout().tabWidth();
        S32       width = 600;
        both("unwrapped", editors, [&](Subject&, ALCodeEditor& e) {
            ALTextLayout& layout = e.layout();
            // A tab width either way, so that every call lays out afresh.
            layout.setTabWidth(layout.tabWidth() == tab ? tab + 1 : tab);
            size_t rows = 0;
            for (S32 line = 0; line < layout.lineCount(); ++line)
            {
                rows += static_cast<size_t>(layout.rowCount(line));
            }
            return rows + static_cast<size_t>(layout.totalHeight());
        });
        for (Subject& s : editors)
        {
            s.editor->layout().setTabWidth(tab);
        }
        both("wrapped at 600 px", editors, [&](Subject&, ALCodeEditor& e) {
            ALTextLayout& layout = e.layout();
            // One pixel either way, so that every call lays out afresh.
            width = width == 600 ? 601 : 600;
            layout.setWrapWidth(width);
            size_t rows = 0;
            for (S32 line = 0; line < layout.lineCount(); ++line)
            {
                rows += static_cast<size_t>(layout.rowCount(line));
            }
            return rows + static_cast<size_t>(layout.totalHeight());
        });
    }

    // Vim's commands over the lines a :g picks, each line's change one
    // edit or all of them one; and text typed again by `.`. Over every
    // line laid out, wrapped at 600 px: the layout as laying out every line
    // leaves it, which these rows were first measured over.
    TEST_CASE("altextengine_bench: vim over every line", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        Editors      editors;
        ALVimKeymap* vims[2] = {};
        for (Subject& s : editors)
        {
            ALTextLayout& layout = s.editor->layout();
            layout.setWrapWidth(600);
            for (S32 line = 0; line < layout.lineCount(); ++line)
            {
                layout.rowCount(line);
            }
            auto keymap                 = std::make_unique<ALVimKeymap>();
            vims[&s - editors.subjects] = keymap.get();
            s.editor->setModalKeymap(std::move(keymap));
        }
        both(":g/total/s//count/, then undone", editors, [&](Subject& s, ALCodeEditor& e) {
            vims[&s - editors.subjects]->takeLine(e, ':', "g/total/s//count/", true);
            e.undo();
        });
        both(":%s/\\(\\w\\+\\)\\s*(/\\1 (/g, then undone", editors, [&](Subject& s, ALCodeEditor& e) {
            vims[&s - editors.subjects]->takeLine(e, ':', "%s/\\(\\w\\+\\)\\s*(/\\1 (/g", true);
            e.undo();
        });
        both(":g/total/>, then undone", editors, [&](Subject& s, ALCodeEditor& e) {
            vims[&s - editors.subjects]->takeLine(e, ':', "g/total/>", true);
            e.undo();
        });
        both(":g/total/d, then undone", editors, [&](Subject& s, ALCodeEditor& e) {
            vims[&s - editors.subjects]->takeLine(e, ':', "g/total/d", true);
            e.undo();
        });
        for (Subject& s : editors)
        {
            s.editor->setCaret(ALTextPos(0, 0));
            typeKeys(*s.editor, "Athe quick brown fox jumps over the lazy dog, the quick brown fox jumps over the lazy dog, and again.\x1b");
        }
        both("`.`: a 100-character insert typed again, then undone", editors, [](Subject&, ALCodeEditor& e) {
            typeKeys(e, ".");
            e.undo();
        });
        // Stepping by character along that line, as l does: to its start,
        // then a step at a time along it.
        bothInSteps("l along a line of 100 characters, per step", editors, 100,
                    [](Subject&, ALCodeEditor& e, size_t step) { typeKeys(e, step == 0 ? "0" : "l"); });
        // A search, then n through its matches; and a search typed on the
        // search line, with the cursor moved along it, as incsearch sees it.
        for (Subject& s : editors)
        {
            vims[&s - editors.subjects]->takeLine(*s.editor, '/', "total", true);
        }
        both("n through the matches of \"total\", per n", editors, [](Subject&, ALCodeEditor& e) { typeKeys(e, "n"); });
        bothInSteps("/total typed, the cursor moved along it, per key", editors, 15, [](Subject&, ALCodeEditor& e, size_t step) {
            if (step < 6)
            {
                const char key[2] = { "/total"[step], 0 };
                typeKeys(e, key);
            }
            else if (step < 10)
            {
                e.handleKeyHere(KEY_LEFT, MASK_NONE);
            }
            else if (step < 14)
            {
                e.handleKeyHere(KEY_RIGHT, MASK_NONE);
            }
            else
            {
                e.handleKeyHere(KEY_ESCAPE, MASK_NONE);
            }
        });
        // A macro of 500 motions played back while a mapping is made: every
        // key it feeds is looked at as the start of one.
        for (Subject& s : editors)
        {
            vims[&s - editors.subjects]->takeLine(*s.editor, ':', "nmap <F5> x", true);
            s.editor->setCaret(ALTextPos(0, 0));
            typeKeys(*s.editor, "gg");
            std::string recorded = "qa";
            recorded += std::string(250, 'j') + std::string(250, 'k') + "q";
            typeKeys(*s.editor, recorded.c_str());
        }
        both("@a: a macro of 500 motions, a mapping made", editors, [](Subject&, ALCodeEditor& e) { typeKeys(e, "@a"); });
        // The whole text selected by lines, then moved within: every motion
        // makes a selection of all of it.
        for (Subject& s : editors)
        {
            typeKeys(*s.editor, "ggVG");
        }
        bothInSteps("a motion with the whole text selected (ggVG, then k and j)", editors, 2,
                    [](Subject&, ALCodeEditor& e, size_t step) { typeKeys(e, step == 0 ? "k" : "j"); });
        for (Subject& s : editors)
        {
            s.editor->handleKeyHere(KEY_ESCAPE, MASK_NONE);
        }
        both(":sort, then undone", editors, [&](Subject& s, ALCodeEditor& e) {
            vims[&s - editors.subjects]->takeLine(e, ':', "sort", true);
            e.undo();
        });
        both(":sort i, then undone", editors, [&](Subject& s, ALCodeEditor& e) {
            vims[&s - editors.subjects]->takeLine(e, ':', "sort i", true);
            e.undo();
        });
    }

    // The Output pane's log, full: four lanes of 500 entries, every other
    // one an error and every fourth with a URL in it.
    TEST_CASE("altextengine_bench: the Output log of two thousand entries", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        requireUI();
        Views views;

        ALOutputView::Params p(LLUICtrlFactory::getDefaultParams<ALOutputView>());
        p.name              = "output";
        p.rect              = LLRect(0, 800, 1000, 0);
        p.capacity          = 500;
        ALOutputView* log   = views.keep(LLUICtrlFactory::create<ALOutputView>(p));
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
        BENCHMARK("a filter typed: the errors alone, then all again")
        {
            log->setFilter([](const ALOutputView::Entry& entry) { return entry.kind == "error"; });
            log->setFilter(nullptr);
            return log->document().lineCount();
        };
        BENCHMARK("an entry past the fill, the oldest let go of")
        {
            log->append(next_one());
            return log->document().lineCount();
        };
    }

    // Quick Open over the names of a big project, a query typed a letter
    // at a time and taken back, a key a call.
    TEST_CASE("altextengine_bench: Quick Open over two thousand candidates", "[altextengine_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        requireUI();
        Views views;

        ALQuickOpen::Params p(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
        p.name              = "quick";
        p.rect              = LLRect(0, 400, 500, 0);
        ALQuickOpen* quick  = views.keep(LLUICtrlFactory::create<ALQuickOpen>(p));
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
        // Six letters typed, then six taken back: the query from "f" to
        // "flscst", and back through "f" to nothing.
        const std::string query = "flscst";
        size_t            keys  = 0;
        const auto        key   = [&]() {
            const size_t step = keys++ % 12;
            quick->setQuery(query.substr(0, step < 6 ? step + 1 : 11 - step));
            return quick->listed().size();
        };
        BENCHMARK("a query typed and taken back, per key")
        {
            return key();
        };
        while (keys % 12 != 0)
        {
            key();
        }
    }
}
