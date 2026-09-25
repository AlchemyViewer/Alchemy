/**
 * @file alscriptproblemspane_test.cpp
 * @brief Script Studio's Problems tab, over the studio's own window: a tab's problems gathered, listed through the filters, and the menu's offers.
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

#include "../alscriptproblemspane.h"

#include "alpanelist.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfontgl.h"
#include "llscrolllistitem.h"

#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    typedef ALScriptProblemsPane::Doc Doc;

    // The window's side, answered as a test says, and what was asked of it
    // kept.
    class FakeWindow final : public ALScriptProblemsPane::Window
    {
    public:
        void problemCountsChanged() override { ++counts; }
        void problemFiltersChanged() override { ++filtersKept; }
        void problemChosen(const ALScriptProblemsPane::Place& place, bool to_editor) override
        {
            chosen.push_back(place);
            toEditor.push_back(to_editor);
        }
        std::string problemIcon(const Doc&, const std::string& include) const override { return include.empty() ? "Inv_Script" : "Studio_File"; }
        bool        applyFix(Doc& doc, const ALScriptFix& fix, U32) override
        {
            fixed.push_back(doc.id + ":" + fix.title);
            return true;
        }
        void fixAllOfKind(Doc& doc, const std::string& key) override { kinds.push_back(doc.id + ":" + key); }
        void refreshProblems(Doc& doc) override { refreshed.push_back(doc.id); }
        bool isLint(bool, const std::string& id) const override { return lints.count(id) > 0; }
        ALScriptLints::Level lintLevel(bool, const std::string& id) const override
        {
            const auto it = levels.find(id);
            return it != levels.end() ? it->second : ALScriptLints::Level::Warning;
        }
        void setLintLevel(bool, const std::string& id, ALScriptLints::Level level) override { levels[id] = level; }
        void showLintSettings() override { ++settings; }

        S32                                         counts      = 0;
        S32                                         filtersKept = 0;
        S32                                         settings    = 0;
        std::vector<ALScriptProblemsPane::Place>    chosen;
        std::vector<bool>                           toEditor;
        std::vector<std::string>                    fixed;
        std::vector<std::string>                    kinds;
        std::vector<std::string>                    refreshed;
        std::set<std::string>                       lints;
        std::map<std::string, ALScriptLints::Level> levels;
    };

    ALScriptProblem problem(ALScriptProblem::Source source, ALScriptProblem::Severity severity, S32 line, const std::string& message,
                            const std::string& file = std::string())
    {
        ALScriptProblem p;
        p.source    = source;
        p.severity  = severity;
        p.line      = line;
        p.column    = 2;
        p.endLine   = line;
        p.endColumn = 6;
        p.message   = message;
        p.file      = file;
        return p;
    }

    ALScriptFix fix(const std::string& title, bool preferred = true, bool safe = true, S32 line = 0)
    {
        ALScriptFix f;
        f.kind      = ALScriptFix::Kind::Fix;
        f.title     = title;
        f.preferred = preferred;
        f.safe      = safe;
        ALScriptEdit edit;
        edit.line      = line;
        edit.column    = 0;
        edit.endLine   = line;
        edit.endColumn = 1;
        f.edits.push_back(edit);
        return f;
    }
}

namespace tut
{
    struct alscriptproblemspane_data
    {
        al_studio_test::StudioWindow         window;
        al_studio_test::FakeServices         services{ window.floater };
        FakeWindow                           studio;
        std::unique_ptr<ALScriptProblemsPane> pane;

        ALScriptProblemsPane& make()
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            pane = std::make_unique<ALScriptProblemsPane>(*window.tab("problems_tab"), services, studio);
            return *pane;
        }

        // A script's tab over an editor of its own, holding ten lines.
        Doc& doc(const std::string& id)
        {
            LLUUID object, item;
            object.generate();
            item.generate();
            Doc& d       = services.addDoc(id, ALScriptRef(object, item), id + ".lsl");
            d.loaded     = true;
            d.modifiable = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name   = "editor_" + id;
            p.rect   = LLRect(0, 200, 400, 0);
            p.syntax = "lsl";
            d.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            d.editor->setFont(LLFontGL::getFontMonospace());
            window.floater->addChild(d.editor);
            std::string text;
            for (int i = 0; i < 10; ++i)
            {
                text += "line " + std::to_string(i) + "\n";
            }
            d.editor->setText(text);
            d.analysisVersion = d.editor->document().version();
            return d;
        }

        // Its problems gathered, as the window gathers them.
        ALScriptProblemsPane::Made gather(Doc& d)
        {
            ALScriptProblemsPane::Making making;
            making.target      = ALScriptWeight::Target::Mono;
            making.includeName = [](const std::string& path) { return path.substr(path.rfind('/') + 1); };
            ALScriptProblemsPane::Made made = ALScriptProblemsPane::make(d, services, making);
            d.shown                         = made.rows;
            if (pane)
            {
                pane->changed(d);
            }
            return made;
        }

        ALPaneList* list() const { return pane->list(); }
        // The problems the list shows, by what they say, in order; the
        // headings as `#name`.
        std::string shown() const
        {
            list()->updateSort();
            std::string out;
            for (const LLScrollListItem* item : list()->getAllData())
            {
                const LLSD& value = item->getValue();
                out += "|" + (value.has("heading") ? "#" + item->getColumn(1)->getValue().asString() : value["message"].asString());
            }
            return out;
        }
        // The row saying `message` chosen, as a click would.
        void choose(const std::string& message)
        {
            list()->updateSort();
            const std::vector<LLScrollListItem*> rows = list()->getAllData();
            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (rows[i]->getValue()["message"].asString() == message)
                {
                    list()->selectNthItem(static_cast<S32>(i));
                    return;
                }
            }
            fail("no row says " + message);
        }
        void tick(const char* box, bool on)
        {
            LLCheckBoxCtrl* check = window.find<LLCheckBoxCtrl>(box);
            check->set(on);
            check->onCommit();
        }
        void pick(const char* combo, const std::string& value)
        {
            LLComboBox* box = window.find<LLComboBox>(combo);
            ensure("offered: " + value, box->selectByValue(LLSD(value)));
            box->onCommit();
        }
    };
    typedef test_group<alscriptproblemspane_data> alscriptproblemspane_group;
    typedef alscriptproblemspane_group::object    alscriptproblemspane_object;
    alscriptproblemspane_group                    alscriptproblemspane_instance("ALScriptProblemsPane");

    template <>
    template <>
    void alscriptproblemspane_object::test<1>()
    {
        set_test_name("a tab's problems gathered: in order, each at its level, the compiler's under the analyzer's, and fixes only for the text they were made on");
        make();
        Doc& d = doc("door");
        using S = ALScriptProblem::Source;
        using V = ALScriptProblem::Severity;
        // The compiler, of the text last saved: an error the analyzer found
        // too, and one it did not.
        d.problems.push_back({ 6, 0, true, std::string(), "ERROR", "syntax error" });
        d.problems.push_back({ 2, 0, true, std::string(), "ERROR", "the compiler's own" });
        // The analyzers, of the text as it stands: that error, a lint with
        // a fix, and a note in an include.
        d.analysis.push_back(problem(S::Parser, V::Error, 6, "syntax error, unexpected"));
        ALScriptProblem lint = problem(S::Lint, V::Warning, 4, "a local shadows another");
        lint.code            = "LocalShadow";
        lint.key             = "shadow";
        lint.fixes.push_back(fix("Rename it", true, true, 4));
        d.analysis.push_back(lint);
        d.analysis.push_back(problem(S::Lint, V::Note, 1, "in the include", "disk:/scripts/lib.lsl"));
        // What it said as it ran, three times; and the weight, over.
        d.runtime.push_back({ 8, -1, std::string(), "Math Error", 3 });
        ALScriptWeight weight;
        weight.target   = ALScriptWeight::Target::Mono;
        weight.total    = 70000;
        weight.limit    = 65536;
        weight.estimate = true;
        d.weight        = weight;
        d.weightVersion = d.editor->document().version();

        ALScriptProblemsPane::Made made = gather(d);
        std::vector<std::string> messages;
        for (const Doc::Shown& row : made.rows)
        {
            messages.push_back(row.message);
        }
        ensure_equals("six rows", made.rows.size(), size_t(6));
        ensure_equals("the weight first, at no line", made.rows[0].origin, services.words("OriginWeight"));
        ensure("the weight a warning, the studio's reckoning", made.rows[0].level == Doc::Level::Warning);
        ensure_equals("then the compiler's own", made.rows[1].message, std::string("the compiler's own"));
        ensure_equals("the lint with its name", made.rows[2].message, std::string("a local shadows another [LocalShadow]"));
        ensure_equals("the analyzer's error, the compiler's on its line said once", made.rows[3].message, std::string("syntax error, unexpected"));
        ensure_equals("the run-time error, how many times", made.rows[4].message, "Math Error " + services.words("RepeatedTimes", { { "[COUNT]", "3" } }));
        ensure("a run-time error is an error", made.rows[4].level == Doc::Level::Error);
        ensure_equals("the include's last", made.rows[5].fileName, std::string("lib.lsl"));
        ensure_equals("the squiggles: the script's own", made.decorations.size(), size_t(4));
        ensure("the run-time mark on its line",
               std::find(made.marks.begin(), made.marks.end(), std::make_pair(S32(8), ALCodeEditor::Mark::Runtime)) != made.marks.end());
        ensure_equals("the lint's line offers its fix", made.fixable.size(), size_t(1));
        ensure("which changes the script", made.fixable[0] == std::make_pair(S32(4), true));

        // Typed in since the check: the fixes are of a text there is no
        // longer, and the compiler's error on the analyzer's line is its own
        // again.
        d.editor->setText("line 0\nline 1\n");
        made = gather(d);
        ensure("no fix offered", made.fixable.empty());
        ensure("the compiler's error said", std::any_of(made.rows.begin(), made.rows.end(), [](const Doc::Shown& row) { return row.message == "syntax error"; }));
        ensure("nor the weight, weighed of the old text",
               std::none_of(made.rows.begin(), made.rows.end(), [&](const Doc::Shown& row) { return row.origin == services.words("OriginWeight"); }));
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<2>()
    {
        set_test_name("the list through its filters -- levels, origin, words -- and its scope, with the counts the boxes and the tab say");
        ALScriptProblemsPane& out = make();
        using S                   = ALScriptProblem::Source;
        using V                   = ALScriptProblem::Severity;
        Doc& door                 = doc("door");
        Doc& lamp                 = doc("lamp");
        door.analysis             = { problem(S::Parser, V::Error, 1, "door error"), problem(S::Lint, V::Warning, 3, "door warning"),
                                      problem(S::Lint, V::Note, 5, "door note") };
        lamp.analysis             = { problem(S::Parser, V::Error, 2, "lamp error") };
        gather(lamp);
        gather(door);
        out.fill(&door);
        ensure_equals("the one in front's", shown(), std::string("|door error|door warning|door note"));
        ensure_equals("three to list", out.held(), 3);
        ensure("said on the tab", studio.counts > 0);
        ensure_equals("counted on the box", window.find<LLCheckBoxCtrl>("problems_errors")->getLabel(),
                      services.words("FilterErrorsCount", { { "[COUNT]", "1" } }));

        tick("problems_errors", false);
        ensure("the filter kept", studio.filtersKept == 1);
        ensure_equals("no errors, and what is hidden said", shown(),
                      "|door warning|door note|#" + services.counted("ProblemsHidden", 1));
        tick("problems_errors", true);
        pick("problems_origin", services.words("OriginLint"));
        ensure_equals("the lint's alone", shown(), "|door warning|door note|#" + services.counted("ProblemsHidden", 1));
        pick("problems_origin", "");
        window.find<LLFilterEditor>("problems_filter")->setText(std::string("NOTE"));
        window.find<LLFilterEditor>("problems_filter")->onCommit();
        ensure_equals("the words, in either case", shown(), "|door note|#" + services.counted("ProblemsHidden", 2));
        window.find<LLFilterEditor>("problems_filter")->setText(std::string());
        window.find<LLFilterEditor>("problems_filter")->onCommit();

        pick("problems_scope", "all");
        ensure_equals("every script's, each under its name", shown(),
                      "|#door.lsl   \xC2\xB7   " + services.counted("ProblemErrors", 1) + ", " + services.counted("ProblemWarnings", 1) + ", " +
                          services.counted("ProblemNotes", 1) + "|door error|door warning|door note|#lamp.lsl   \xC2\xB7   " +
                          services.counted("ProblemErrors", 1) + "|lamp error");
        ensure_equals("four to list", out.held(), 4);
        // Another script's problems gathered again: listed, since every
        // one's are.
        lamp.analysis.push_back(problem(S::Lint, V::Warning, 7, "lamp warning"));
        gather(lamp);
        ensure("the new one listed", shown().find("lamp warning") != std::string::npos);
        pick("problems_scope", "this");
        ensure_equals("back to the one", shown(), std::string("|door error|door warning|door note"));
        gather(lamp);
        ensure("another's gathered leaves the list alone", shown().find("lamp") == std::string::npos);

        // Closed, it is let go of.
        out.closed("door");
        ensure("forgotten", out.listedId().empty());
        LLSD state;
        out.saveState(state);
        ensure_equals("the scope kept", state["problem_scope"].asString(), std::string("this"));
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<3>()
    {
        set_test_name("the first error chosen and gone to, passing over the compiler's where asked; a row chosen goes to its place");
        ALScriptProblemsPane& out = make();
        Doc&                  d   = doc("door");
        d.problems.push_back({ 1, 0, true, std::string(), "ERROR", "compiler first" });
        d.analysis = { problem(ALScriptProblem::Source::Lint, ALScriptProblem::Severity::Warning, 0, "a warning before"),
                       problem(ALScriptProblem::Source::Parser, ALScriptProblem::Severity::Error, 5, "parser later") };
        gather(d);
        out.fill(&d);
        out.selectFirstError(false);
        ensure_equals("the first error, the compiler's", studio.chosen.back().line, 1);
        ensure("taken to the script", studio.toEditor.back());
        out.selectFirstError(true);
        ensure_equals("the checkers' own", studio.chosen.back().line, 5);
        ensure_equals("its stretch", studio.chosen.back().endColumn, 6);

        choose("a warning before");
        out.choose(false);
        ensure_equals("a row walked to", studio.chosen.back().line, 0);
        ensure("the keyboard left in the list", !studio.toEditor.back());
        ensure_equals("whose it is", studio.chosen.back().doc, std::string("door"));
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<4>()
    {
        set_test_name("the menu offers what the chosen problem allows: its lint turned off or made an error, its fixes, every one of its kind");
        ALScriptProblemsPane& out = make();
        Doc&                  d   = doc("door");
        studio.lints.insert("LocalShadow");
        ALScriptProblem one = problem(ALScriptProblem::Source::Lint, ALScriptProblem::Severity::Warning, 2, "shadowed once");
        one.code            = "LocalShadow";
        one.key             = "shadow";
        one.fixes.push_back(fix("Rename", true, true, 2));
        ALScriptProblem two = one;
        two.line            = 6;
        two.endLine         = 6;
        two.message         = "shadowed twice";
        two.fixes           = { fix("Rename", true, true, 6) };
        ALScriptProblem plain = problem(ALScriptProblem::Source::Parser, ALScriptProblem::Severity::Error, 8, "no lint");
        d.analysis            = { one, two, plain };
        gather(d);
        out.fill(&d);

        choose("no lint");
        ensure("no lint to turn off", !out.enabled("off") && !out.enabled("error"));
        ensure("copying always", out.enabled("copy") && out.enabled("copy_all"));
        ensure("no run-time errors to let go of", !out.enabled("clear_runtime"));
        ensure("of no kind, none to fix along with it", !out.fixShown("kind"));
        out.act("fix_kind");
        ensure("nor made", studio.kinds.empty());
        ensure("Fix All, over two", out.fixShown("all"));

        choose("shadowed once [LocalShadow]");
        ensure("its lint", out.enabled("off") && out.enabled("error"));
        ensure("a warning", !out.lintIsError());
        out.act("error");
        ensure("made an error", out.lintIsError());
        out.act("error");
        ensure("and back", !out.lintIsError());
        out.act("off");
        ensure("turned off", studio.levels["LocalShadow"] == ALScriptLints::Level::Off);
        out.act("settings");
        ensure_equals("the settings", studio.settings, 1);
        ensure("every one of its kind", out.fixShown("kind"));
        out.act("fix:0");
        ensure_equals("its fix made", studio.fixed.size(), size_t(1));
        ensure_equals("which", studio.fixed[0], std::string("door:Rename"));
        out.act("fix_kind");
        out.act("fix_all");
        ensure_equals("of its kind, then of all", studio.kinds.size(), size_t(2));
        ensure_equals("by its kind", studio.kinds[0], std::string("door:shadow"));
        ensure_equals("then every kind", studio.kinds[1], std::string("door:"));

        out.act("copy_all");
        ensure_equals("copied, and said", services.statuses.back(), services.counted("ProblemsCopied", 3));

        d.runtime.push_back({ 1, -1, std::string(), "Math Error", 1 });
        ensure("run-time errors to let go of", out.enabled("clear_runtime"));
        out.act("clear_runtime");
        ensure("let go of", d.runtime.empty());
        ensure_equals("and the tab's problems gathered again", studio.refreshed.size(), size_t(1));
    }
}
