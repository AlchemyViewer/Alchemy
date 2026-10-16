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

#include "../alscriptstudiochecking.h"
#include "../alscriptstudioweighing.h"

#include "alpanefolds.h"
#include "alscriptfixes.h"
#include "alpanelist.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfontgl.h"
#include "llscrolllistitem.h"
#include "lltabcontainer.h"

#include "alscriptstudio_fixture.h"

#include <catch2/catch_test_macros.hpp>

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
    class FakeWindow : public ALScriptProblemsPane::Window
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
        std::string scriptIcon(bool lua, const std::string& include) const override { return include.empty() ? (lua ? "Inv_Script_Luau" : "Inv_Script") : "Studio_File"; }
        bool        applyFix(Doc& doc, const ALScriptFix& fix, U32) override
        {
            fixed.push_back(doc.id + ":" + fix.title);
            return true;
        }
        void fixAll(Doc& doc, const Doc::FixPick& pick) override { kinds.push_back(doc.id + ":" + (pick.migration ? "from LSL" : pick.key)); }
        void refreshProblems(Doc& doc) override { refreshed.push_back(doc.id); }
        void runtimeCleared(Doc& doc) override { cleared.push_back(doc.id); }
        bool isLint(bool, const std::string& id) const override { return lints.count(id) > 0; }
        ALScriptLints::Level lintLevel(bool, const std::string& id) const override
        {
            const auto it = levels.find(id);
            return it != levels.end() ? it->second : ALScriptLints::Level::Warning;
        }
        void setLintLevel(bool, const std::string& id, ALScriptLints::Level level) override { levels[id] = level; }
        void showLintSettings() override { ++settings; }
        LLUUID rootOf(const ALScriptRef& ref) const override
        {
            const auto found = roots.find(ref.object);
            return found != roots.end() ? found->second : LLUUID::null;
        }

        S32                                         counts      = 0;
        S32                                         filtersKept = 0;
        S32                                         settings    = 0;
        std::vector<ALScriptProblemsPane::Place>    chosen;
        std::vector<bool>                           toEditor;
        std::vector<std::string>                    fixed;
        std::vector<std::string>                    kinds;
        std::vector<std::string>                    refreshed;
        std::vector<std::string>                    cleared;
        std::set<std::string>                       lints;
        std::map<std::string, ALScriptLints::Level> levels;
        // Each prim's object, by its root.
        std::map<LLUUID, LLUUID>                    roots;
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

namespace
{
    struct alscriptproblemspane_data
    {
        // The window the pane finds through the view tree: the fakes.
        al_studio_test::StudioWindowOf<FakeWindow> window;
        al_studio_test::FakeServices&             services = window.services();
        FakeWindow&                               studio   = window.pane();
        ALScriptProblemsPane*                pane = nullptr;

        ALScriptProblemsPane& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            // The tab as the skin built it.
            pane = window.find<ALScriptProblemsPane>("problems_tab");
            UNSCOPED_INFO("the skin builds the tab as the pane");
            REQUIRE(pane != nullptr);
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
            d.check->analysisVersion = d.editor->document().version();
            return d;
        }

        // Its problems gathered, as the window gathers them.
        ALScriptProblemsPane::Made gather(Doc& d)
        {
            ALScriptProblemsPane::Making making;
            making.target      = ALScriptWeight::Target::Mono;
            making.includeName = [](const std::string& path) { return path.substr(path.rfind('/') + 1); };
            ALScriptProblemsPane::Made made = ALScriptProblemsPane::make(d, services, making);
            d.setShown(made.rows);
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
                out += std::string(value.has("heading") ? "|#" : "|") + item->getColumn(1)->getValue().asString();
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
                if (!rows[i]->getValue().has("heading") && rows[i]->getColumn(1)->getValue().asString() == message)
                {
                    list()->selectNthItem(static_cast<S32>(i));
                    return;
                }
            }
            FAIL("no row says " + message);
        }
        void tick(const char* box, bool on)
        {
            LLCheckBoxCtrl* check = window.find<LLCheckBoxCtrl>(box);
            CAPTURE(box);
            REQUIRE(check);
            check->set(on);
            check->onCommit();
        }
        void pick(const char* combo, const std::string& value)
        {
            LLComboBox* box = window.find<LLComboBox>(combo);
            CAPTURE(combo);
            REQUIRE(box);
            INFO("the combo offers " << value);
            REQUIRE(box->selectByValue(LLSD(value)));
            box->onCommit();
        }
    };

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: gathered problems come in order and fixes lapse once the text changes", "[ALScriptProblemsPane]")
    {
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
        d.check->analysis.push_back(problem(S::Parser, V::Error, 6, "syntax error, unexpected"));
        ALScriptProblem lint = problem(S::Lint, V::Warning, 4, "a local shadows another");
        lint.code            = "LocalShadow";
        lint.key             = "shadow";
        lint.fixes.push_back(fix("Rename it", true, true, 4));
        d.check->analysis.push_back(lint);
        d.check->analysis.push_back(problem(S::Lint, V::Note, 1, "in the include", "disk:/scripts/lib.lsl"));
        // What it said as it ran, three times; and the weight, over.
        d.runtime.push_back({ 8, -1, std::string(), "Math Error", 3 });
        ALScriptWeight weight;
        weight.target   = ALScriptWeight::Target::Mono;
        weight.total    = 70000;
        weight.limit    = 65536;
        weight.estimate = true;
        d.weighing->weight        = weight;
        d.weighing->version = d.editor->document().version();

        ALScriptProblemsPane::Made made = gather(d);
        std::vector<std::string> messages;
        for (const Doc::Shown& row : made.rows)
        {
            messages.push_back(row.message);
        }
        {
            CAPTURE(messages);
            REQUIRE(made.rows.size() == size_t(6));
        }
        UNSCOPED_INFO("the weight first, at no line");
        CHECK(made.rows[0].origin == services.words("OriginWeight"));
        UNSCOPED_INFO("the weight a warning, the studio's reckoning");
        CHECK(made.rows[0].level == Doc::Level::Warning);
        UNSCOPED_INFO("then the compiler's own");
        CHECK(made.rows[1].message == std::string("the compiler's own"));
        UNSCOPED_INFO("the lint with its name");
        CHECK(made.rows[2].message == std::string("a local shadows another [LocalShadow]"));
        UNSCOPED_INFO("the analyzer's error, the compiler's on its line said once");
        CHECK(made.rows[3].message == std::string("syntax error, unexpected"));
        UNSCOPED_INFO("the run-time error, how many times");
        CHECK(made.rows[4].message == "Math Error " + services.words("RepeatedTimes", { { "[COUNT]", "3" } }));
        UNSCOPED_INFO("a run-time error is an error");
        CHECK(made.rows[4].level == Doc::Level::Error);
        UNSCOPED_INFO("the include's last");
        CHECK(made.rows[5].fileName == std::string("lib.lsl"));
        UNSCOPED_INFO("the squiggles: the script's own");
        REQUIRE(made.decorations.size() == size_t(4));
        const auto underline = [&](S32 line) {
            for (const ALCodeEditor::Decoration& each : made.decorations)
            {
                if (each.range.begin.line == line)
                {
                    return each.style;
                }
            }
            return ALCodeEditor::Decoration::Style::Background;
        };
        using Style = ALCodeEditor::Decoration::Style;
        {
            INFO("an error waved, a warning dashed, a run-time error waved");
            CHECK(underline(2) == Style::Squiggle);
            CHECK(underline(4) == Style::Dashed);
            CHECK(underline(8) == Style::Squiggle);
        }
        UNSCOPED_INFO("the run-time mark on its line");
        REQUIRE(std::find(made.marks.begin(), made.marks.end(), std::make_pair(S32(8), ALCodeEditor::Mark::Runtime)) != made.marks.end());
        CHECK(ALScriptProblemsPane::markOf(Doc::Level::Error) == ALCodeEditor::Mark::Error);
        CHECK(ALScriptProblemsPane::markOf(Doc::Level::Warning) == ALCodeEditor::Mark::Warning);
        CHECK(ALScriptProblemsPane::markOf(Doc::Level::Note) == ALCodeEditor::Mark::Note);
        UNSCOPED_INFO("the lint's line offers its fix");
        REQUIRE(made.fixable.size() == size_t(1));
        UNSCOPED_INFO("which changes the script");
        CHECK(made.fixable[0] == std::make_pair(S32(4), true));

        // Typed in since the check: the fixes are of a text there is no
        // longer, and the compiler's error on the analyzer's line is its own
        // again.
        d.editor->setText("line 0\nline 1\n");
        made = gather(d);
        REQUIRE(made.fixable.empty());
        UNSCOPED_INFO("the compiler's error said");
        CHECK(std::any_of(made.rows.begin(), made.rows.end(), [](const Doc::Shown& row) { return row.message == "syntax error"; }));
        UNSCOPED_INFO("nor the weight, weighed of the old text");
        CHECK(std::none_of(made.rows.begin(), made.rows.end(), [&](const Doc::Shown& row) { return row.origin == services.words("OriginWeight"); }));
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: the list follows its filters and scope and the boxes say the counts", "[ALScriptProblemsPane]")
    {
        ALScriptProblemsPane& out = make();
        using S                   = ALScriptProblem::Source;
        using V                   = ALScriptProblem::Severity;
        Doc& door                 = doc("door");
        Doc& lamp                 = doc("lamp");
        door.check->analysis      = { problem(S::Parser, V::Error, 1, "door error"), problem(S::Lint, V::Warning, 3, "door warning"),
                                      problem(S::Lint, V::Note, 5, "door note") };
        lamp.check->analysis       = { problem(S::Parser, V::Error, 2, "lamp error") };
        gather(lamp);
        gather(door);
        out.fill(&door);
        UNSCOPED_INFO("the one in front's");
        CHECK(shown() == std::string("|door error|door warning|door note"));
        CHECK(out.held() == 3);
        UNSCOPED_INFO("said on the tab");
        CHECK(studio.counts > 0);
        LLCheckBoxCtrl* errors = window.find<LLCheckBoxCtrl>("problems_errors");
        REQUIRE(errors);
        UNSCOPED_INFO("counted on the box");
        CHECK(errors->getLabel() == services.words("FilterErrorsCount", { { "[COUNT]", "1" } }));

        tick("problems_errors", false);
        UNSCOPED_INFO("the filter kept");
        CHECK(studio.filtersKept == 1);
        UNSCOPED_INFO("no errors, and what is hidden said");
        CHECK(shown() ==
                      "|door warning|door note|#" + services.counted("ProblemsHidden", 1));
        tick("problems_errors", true);
        pick("problems_origin", services.words("OriginLint"));
        UNSCOPED_INFO("the lint's alone");
        CHECK(shown() == "|door warning|door note|#" + services.counted("ProblemsHidden", 1));
        pick("problems_origin", "");
        LLFilterEditor* filter = window.find<LLFilterEditor>("problems_filter");
        REQUIRE(filter);
        filter->setText(std::string("NOTE"));
        filter->onCommit();
        UNSCOPED_INFO("the words, in either case");
        CHECK(shown() == "|door note|#" + services.counted("ProblemsHidden", 2));
        UNSCOPED_INFO("rows to show: nothing said over them");
        CHECK_FALSE(out.list()->saysEmpty());
        filter->setText(std::string());
        filter->onCommit();

        pick("problems_scope", "all");
        UNSCOPED_INFO("every script's, each under its name");
        CHECK(shown() ==
                      "|#door.lsl   \xC2\xB7   " + services.counted("ProblemErrors", 1) + ", " + services.counted("ProblemWarnings", 1) + ", " +
                          services.counted("ProblemNotes", 1) + "|door error|door warning|door note|#lamp.lsl   \xC2\xB7   " +
                          services.counted("ProblemErrors", 1) + "|lamp error");
        CHECK(out.held() == 4);
        // Another script's problems gathered again: listed, since every
        // one's are.
        lamp.check->analysis.push_back(problem(S::Lint, V::Warning, 7, "lamp warning"));
        gather(lamp);
        UNSCOPED_INFO("the new one listed");
        REQUIRE(shown().find("lamp warning") != std::string::npos);
        pick("problems_scope", "this");
        UNSCOPED_INFO("back to the one");
        CHECK(shown() == std::string("|door error|door warning|door note"));
        gather(lamp);
        UNSCOPED_INFO("another's gathered leaves the list alone");
        REQUIRE(shown().find("lamp") == std::string::npos);

        // Closed, it is let go of.
        out.closed("door");
        REQUIRE(out.listedId().empty());
        LLSD state;
        out.saveState(state);
        UNSCOPED_INFO("the scope kept");
        CHECK(state["problem_scope"].asString() == std::string("this"));

        // A script with nothing wrong with it says so where the rows would be.
        Doc& clean = doc("clean");
        gather(clean);
        out.fill(&clean);
        {
            INFO("nothing listed, said");
            REQUIRE(out.list()->saysEmpty());
            CHECK(out.list()->emptyWords() == services.words("NoProblemsIn", { { "[NAME]", "clean.lsl" } }));
        }
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: sorted by source, the most pressing first, as the filter lists them", "[ALScriptProblemsPane]")
    {
        ALScriptProblemsPane& out = make();
        using S                   = ALScriptProblem::Source;
        using V                   = ALScriptProblem::Severity;
        Doc& door                 = doc("door");
        door.check->analysis      = { problem(S::Lint, V::Warning, 1, "a lint"), problem(S::Types, V::Error, 2, "a type"),
                                      problem(S::Parser, V::Error, 3, "a parse") };
        gather(door);
        out.fill(&door);
        out.list()->sortByColumnIndex(2, true);
        UNSCOPED_INFO("Tapple's: the parser's, then the types', then the lint's");
        CHECK(shown() == std::string("|a parse|a type|a lint"));
        out.list()->sortByColumnIndex(2, false);
        UNSCOPED_INFO("the other way, the other way round");
        CHECK(shown() == std::string("|a lint|a type|a parse"));
        LLComboBox* origins = window.find<LLComboBox>("problems_origin");
        REQUIRE(origins);
        std::vector<std::string> listed;
        for (const LLScrollListItem* item : origins->getAllData())
        {
            listed.push_back(item->getValue().asString());
        }
        UNSCOPED_INFO("the filter in the same order, after any");
        CHECK(listed == std::vector<std::string>{ "", services.words("OriginCompiler"), services.words("OriginParser"),
                                                  services.words("OriginPreprocessor"), services.words("OriginRuntime"),
                                                  services.words("OriginMigration"), services.words("OriginTypes"), services.words("OriginLint"),
                                                  services.words("OriginOptimizer"), services.words("OriginWeight"),
                                                  services.words("OriginDefinitions") });
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: selectFirstError and a chosen row go to the problem's place", "[ALScriptProblemsPane]")
    {
        ALScriptProblemsPane& out = make();
        Doc&                  d   = doc("door");
        d.problems.push_back({ 1, 0, true, std::string(), "ERROR", "compiler first" });
        d.check->analysis = { problem(ALScriptProblem::Source::Lint, ALScriptProblem::Severity::Warning, 0, "a warning before"),
                       problem(ALScriptProblem::Source::Parser, ALScriptProblem::Severity::Error, 5, "parser later") };
        gather(d);
        out.fill(&d);
        out.selectFirstError(false);
        REQUIRE_FALSE(studio.chosen.empty());
        UNSCOPED_INFO("the first error, the compiler's");
        CHECK(studio.chosen.back().line == 1);
        UNSCOPED_INFO("taken to the script");
        REQUIRE(studio.toEditor.back());
        out.selectFirstError(true);
        UNSCOPED_INFO("the checkers' own");
        CHECK(studio.chosen.back().line == 5);
        CHECK(studio.chosen.back().endColumn == 6);

        choose("a warning before");
        out.choose(false);
        UNSCOPED_INFO("a row walked to");
        CHECK(studio.chosen.back().line == 0);
        UNSCOPED_INFO("the keyboard left in the list");
        REQUIRE_FALSE(studio.toEditor.back());
        CHECK(studio.chosen.back().doc == std::string("door"));

        // The list's keys: return goes there to type, escape back to the
        // script without going.
        {
            INFO("return");
            CHECK(out.list()->handleKeyHere(KEY_RETURN, MASK_NONE));
            CHECK(studio.chosen.back().line == 0);
            REQUIRE(studio.toEditor.back());
        }
        const size_t gone     = studio.chosen.size();
        const size_t revealed = services.reveals.size();
        {
            INFO("escape");
            CHECK(out.list()->handleKeyHere(KEY_ESCAPE, MASK_NONE));
            REQUIRE(services.reveals.size() == revealed + 1);
            REQUIRE(services.reveals.back());
            REQUIRE(studio.chosen.size() == gone);
        }
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: the menu offers only what the chosen problem allows and acts on it", "[ALScriptProblemsPane]")
    {
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
        d.check->analysis      = { one, two, plain };
        gather(d);
        out.fill(&d);

        choose("no lint");
        {
            INFO("no lint to turn off");
            CHECK_FALSE(out.enabled("off"));
            CHECK_FALSE(out.enabled("error"));
        }
        {
            INFO("copying always");
            CHECK(out.enabled("copy"));
            CHECK(out.enabled("copy_all"));
        }
        UNSCOPED_INFO("no run-time errors to let go of");
        CHECK_FALSE(out.enabled("clear_runtime"));
        UNSCOPED_INFO("of no kind, none to fix along with it");
        CHECK_FALSE(out.fixShown("kind"));
        out.act("fix_kind");
        UNSCOPED_INFO("nor made");
        REQUIRE(studio.kinds.empty());
        UNSCOPED_INFO("Fix All, over two");
        CHECK(out.fixShown("all"));

        choose("shadowed once [LocalShadow]");
        {
            INFO("its lint");
            CHECK(out.enabled("off"));
            CHECK(out.enabled("error"));
        }
        UNSCOPED_INFO("a warning");
        CHECK_FALSE(out.lintIsError());
        out.act("error");
        UNSCOPED_INFO("made an error");
        CHECK(out.lintIsError());
        out.act("error");
        UNSCOPED_INFO("and back");
        CHECK_FALSE(out.lintIsError());
        out.act("off");
        CHECK(studio.levels["LocalShadow"] == ALScriptLints::Level::Off);
        out.act("settings");
        CHECK(studio.settings == 1);
        UNSCOPED_INFO("every one of its kind");
        CHECK(out.fixShown("kind"));
        out.act("fix:0");
        UNSCOPED_INFO("its fix made");
        REQUIRE(studio.fixed.size() == size_t(1));
        CHECK(studio.fixed[0] == std::string("door:Rename"));
        out.act("fix_kind");
        out.act("fix_all");
        UNSCOPED_INFO("of its kind, then of all");
        REQUIRE(studio.kinds.size() == size_t(2));
        CHECK(studio.kinds[0] == std::string("door:shadow"));
        UNSCOPED_INFO("then every kind");
        CHECK(studio.kinds[1] == std::string("door:"));

        out.act("copy_all");
        REQUIRE_FALSE(services.statuses.empty());
        UNSCOPED_INFO("copied, and said");
        CHECK(services.statuses.back() == services.counted("ProblemsCopied", 3));

        d.runtime.push_back({ 1, -1, std::string(), "Math Error", 1 });
        UNSCOPED_INFO("run-time errors to let go of");
        CHECK(out.enabled("clear_runtime"));
        out.act("clear_runtime");
        REQUIRE(d.runtime.empty());
        UNSCOPED_INFO("and not recalled when it is opened again");
        CHECK(studio.cleared == std::vector<std::string>{ d.id });
        UNSCOPED_INFO("and the tab's problems gathered again");
        REQUIRE(studio.refreshed.size() == size_t(1));
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: a compiler error a line after the analyzer's is merged and dated after edits", "[ALScriptProblemsPane]")
    {
        make();
        Doc& d = doc("door");
        using S = ALScriptProblem::Source;
        using V = ALScriptProblem::Severity;
        // The analyzer at the missing `;`, the compiler at what came after.
        d.problems.push_back({ 7, 0, true, std::string(), "ERROR", "syntax error" });
        d.problems.push_back({ 2, 0, true, std::string(), "ERROR", "far from any" });
        d.check->analysis.push_back(problem(S::Parser, V::Error, 6, "Missing ';'"));
        d.check->analysisVersion = d.editor->document().version();
        ALScriptProblemsPane::Made made = gather(d);
        std::vector<std::string> messages;
        for (const Doc::Shown& row : made.rows)
        {
            messages.push_back(row.message);
        }
        UNSCOPED_INFO("the one a line after said once");
        REQUIRE(std::find(messages.begin(), messages.end(), "syntax error") == messages.end());
        UNSCOPED_INFO("one further off kept");
        REQUIRE(std::find(messages.begin(), messages.end(), "far from any") != messages.end());
        // Typed in since the save: what the compiler said is of the text
        // as it was.
        d.editor->insertText("x");
        made = gather(d);
        bool marked = false;
        for (const Doc::Shown& row : made.rows)
        {
            marked |= row.message == "far from any " + services.words("CompilerAsSaved");
        }
        UNSCOPED_INFO("as of the last save");
        CHECK(marked);
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: an LSL lint shows its NOLINT name and an error no code number", "[ALScriptProblemsPane]")
    {
        make();
        Doc& d = doc("door");
        using S = ALScriptProblem::Source;
        using V = ALScriptProblem::Severity;
        ALScriptProblem unused = problem(S::Lint, V::Warning, 2, "declared but never used");
        unused.key             = "LSLDeclaredButNotUsed";
        unused.code            = "20009";
        ALScriptProblem wrong  = problem(S::Types, V::Error, 3, "type mismatch");
        wrong.code             = "10002";
        d.check->analysis       = { unused, wrong };
        d.check->analysisVersion = d.editor->document().version();
        ALScriptProblemsPane::Made made = gather(d);
        bool named = false, numbered = false;
        for (const Doc::Shown& row : made.rows)
        {
            named |= row.message == "declared but never used [DeclaredButNotUsed]";
            numbered |= row.message.find("10002") != std::string::npos;
        }
        UNSCOPED_INFO("the lint named");
        CHECK(named);
        UNSCOPED_INFO("by the name a NOLINT comment turns it off by");
        CHECK(ALScriptFixes::suppressed(unused, "integer x; // NOLINT(DeclaredButNotUsed)", "", false));
        UNSCOPED_INFO("the error no number");
        CHECK_FALSE(numbered);
        LLComboBox* origins = window.find<LLComboBox>("problems_origin");
        {
            INFO("the origins offer the weight");
            REQUIRE(origins);
            CHECK(origins->selectByValue(LLSD(services.words("OriginWeight"))));
        }
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: code past four fifths of its limit warns what is left once optimized", "[ALScriptProblemsPane]")
    {
        make();
        Doc& d = doc("door");
        ALScriptWeight weight;
        weight.target       = ALScriptWeight::Target::LSO;
        weight.total        = 14 * 1024;
        weight.limit        = 16 * 1024;
        d.weighing->weight  = weight;
        d.weighing->version = d.editor->document().version();
        d.weighing->exact   = true;
        const auto weights  = [&](const ALScriptProblemsPane::Made& made) {
            std::vector<Doc::Shown> rows;
            std::copy_if(made.rows.begin(), made.rows.end(), std::back_inserter(rows), [&](const Doc::Shown& row) { return row.origin == services.words("OriginWeight"); });
            return rows;
        };
        std::vector<Doc::Shown> rows = weights(gather(d));
        UNSCOPED_INFO("near: one row");
        REQUIRE(rows.size() == size_t(1));
        CHECK(rows[0].level == Doc::Level::Warning);
        UNSCOPED_INFO("what is left");
        CHECK(rows[0].message == services.words("WeightNear", { { "[SIZE]", "14.0" }, { "[LIMIT]", "16" }, { "[LEFT]", "2.0" }, { "[TARGET]", "LSL (LSO)" } }));
        d.weighing->exact = false;
        UNSCOPED_INFO("before the optimizer, nothing");
        REQUIRE(weights(gather(d)).empty());
        d.weighing->exact            = true;
        d.weighing->weight->total    = 12 * 1024;
        UNSCOPED_INFO("at three quarters, nothing");
        REQUIRE(weights(gather(d)).empty());
        d.weighing->weight->total    = 17 * 1024;
        rows = weights(gather(d));
        {
            INFO("over: the over row alone");
            REQUIRE(rows.size() == 1);
            REQUIRE(rows[0].message.find("more than") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: a problem in generated code is listed as such and not marked in the source", "[ALScriptProblemsPane]")
    {
        make();
        Doc& d = doc("door");
        ALScriptProblem made_here = problem(ALScriptProblem::Source::Parser, ALScriptProblem::Severity::Error, 1, "in a switch's jump table", Doc::GENERATED);
        d.check->analysis          = { made_here };
        d.check->analysisVersion   = d.editor->document().version();
        const ALScriptProblemsPane::Made made = gather(d);
        REQUIRE(made.rows.size() == size_t(1));
        CHECK(made.rows[0].fileName == services.words("InGeneratedCode"));
        REQUIRE(made.marks.empty());
        REQUIRE(made.decorations.empty());
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: a checked script with no tab is listed after open ones and chosen by its item", "[ALScriptProblemsPane]")
    {
        make();
        Doc& d            = doc("door");
        d.check->analysis = { problem(ALScriptProblem::Source::Parser, ALScriptProblem::Severity::Error, 1, "in the open one") };
        gather(d);
        LLUUID object, item;
        object.generate();
        item.generate();
        const ALScriptRef closed(object, item);
        Doc::Shown        row;
        row.line      = 3;
        row.column    = 1;
        row.hasColumn = true;
        row.level     = Doc::Level::Warning;
        row.origin    = "Lint";
        row.message   = "in the closed one";
        pane->checkedScript(closed, "hinge.lsl", false, { row }, "House");
        pane->showEveryScript();
        const std::string listed  = shown();
        const std::string heading = "#" + services.words("ProblemsChecked", { { "[NAME]", "hinge.lsl" }, { "[OBJECT]", "House" } });
        {
            INFO("the open one's, then the closed one's under its name and object: " + listed);
            CHECK(listed.find("in the open one") < listed.find(heading));
            CHECK(listed.find(heading) < listed.find("in the closed one"));
            REQUIRE(listed.find("in the closed one") != std::string::npos);
        }
        UNSCOPED_INFO("counted with the rest");
        CHECK(pane->held() == 2);

        // Chosen by its item and name, with no tab.
        const std::vector<LLScrollListItem*> rows = list()->getAllData();
        S32 at = -1;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            at = rows[i]->getValue()["owner"].asString().rfind("checked:", 0) == 0 ? static_cast<S32>(i) : at;
        }
        UNSCOPED_INFO("a row of it");
        REQUIRE(at >= 0);
        list()->selectNthItem(at);
        pane->choose(false);
        REQUIRE_FALSE(studio.chosen.empty());
        const ALScriptProblemsPane::Place& place = studio.chosen.back();
        {
            INFO("by its item, with no tab");
            CHECK(place.ref == closed);
            CHECK(place.name == "hinge.lsl");
            REQUIRE(place.doc.empty());
            CHECK(place.line == 3);
        }

        // Opened, its tab says; a new check lets the last one's go.
        pane->forgetChecked(closed);
        {
            INFO("given way");
            REQUIRE(shown().find("in the closed one") == std::string::npos);
            CHECK(pane->held() == 1);
        }
        pane->checkedScript(closed, "hinge.lsl", false, { row }, "House");
        UNSCOPED_INFO("back");
        CHECK(pane->checkedCount() == size_t(1));
        pane->clearChecked();
        {
            INFO("let go of");
            CHECK(pane->checkedCount() == 0);
            REQUIRE(shown().find("in the closed one") == std::string::npos);
        }
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: a closed script's last recompile errors list beside its check until clean", "[ALScriptProblemsPane]")
    {
        make();
        LLUUID object, item;
        object.generate();
        item.generate();
        const ALScriptRef closed(object, item);
        const auto        row = [](const std::string& message, const std::string& origin) {
            Doc::Shown made;
            made.line    = 2;
            made.level   = Doc::Level::Error;
            made.origin  = origin;
            made.message = message;
            return made;
        };
        pane->showEveryScript();
        pane->compiledScript(closed, "hinge.lsl", false, {}, "House");
        UNSCOPED_INFO("clean, and nothing listed of it: nothing to list");
        CHECK(pane->checkedCount() == 0);

        pane->checkedScript(closed, "hinge.lsl", false, { row("the check's", "Lint") }, "House");
        pane->compiledScript(closed, "hinge.lsl", false, { row("the compiler's", "Compiler") }, "House");
        REQUIRE(shown().find("the check's") != std::string::npos);
        REQUIRE(shown().find("the compiler's") != std::string::npos);
        CHECK(pane->held() == 2);

        pane->compiledScript(closed, "hinge.lsl", false, { row("again", "Compiler") }, "House");
        {
            INFO("the last recompile's alone");
            REQUIRE(shown().find("the compiler's") == std::string::npos);
            REQUIRE(shown().find("again") != std::string::npos);
        }
        pane->compiledScript(closed, "hinge.lsl", false, {}, "House");
        {
            INFO("clean: the compiler's gone, the check's stay");
            REQUIRE(shown().find("again") == std::string::npos);
            REQUIRE(shown().find("the check's") != std::string::npos);
        }

        // Recompiled alone, with no check: gone with its rows.
        LLUUID other;
        other.generate();
        const ALScriptRef lone(object, other);
        pane->compiledScript(lone, "latch.lsl", false, { row("latch", "Compiler") }, "House");
        CHECK(pane->checkedCount() == size_t(2));
        pane->compiledScript(lone, "latch.lsl", false, {}, "House");
        CHECK(pane->checkedCount() == size_t(1));
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: out of sight the pane counts at once but lists rows only when seen or chosen", "[ALScriptProblemsPane]")
    {
        ALScriptProblemsPane& out  = make();
        using S                    = ALScriptProblem::Source;
        using V                    = ALScriptProblem::Severity;
        LLTabContainer* tabs       = window.find<LLTabContainer>("bottom_tabs");
        Doc&            door       = doc("door");
        door.check->analysis       = { problem(S::Parser, V::Error, 1, "door error"), problem(S::Lint, V::Warning, 3, "door warning") };
        REQUIRE(tabs);
        tabs->selectTabByName("references_tab");
        CHECK_FALSE(ALPaneFolds::inSight(&out));
        const S32 said = studio.counts;
        gather(door);
        {
            INFO("counted, and said on the tab");
            CHECK(out.held() == 2);
            CHECK(studio.counts > said);
        }
        UNSCOPED_INFO("not listed");
        CHECK(shown() == std::string());
        out.pump();
        UNSCOPED_INFO("nor while unseen");
        CHECK(shown() == std::string());
        tabs->selectTabByName("problems_tab");
        out.pump();
        UNSCOPED_INFO("seen: listed");
        CHECK(shown() == std::string("|door error|door warning"));

        tabs->selectTabByName("references_tab");
        door.check->analysis.push_back(problem(S::Parser, V::Error, 5, "a second error"));
        gather(door);
        {
            INFO("counted again, the rows as they were");
            CHECK(out.held() == 3);
            CHECK(shown() == "|door error|door warning");
        }
        const size_t chosen = studio.chosen.size();
        out.selectFirstError(false);
        UNSCOPED_INFO("a row to choose: listed first");
        REQUIRE(shown().find("a second error") != std::string::npos);
        {
            INFO("and chosen");
            REQUIRE(studio.chosen.size() == chosen + 1);
            CHECK(studio.chosen.back().line == 1);
        }
        tabs->selectTabByName("problems_tab");
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: a refill keeps matching rows and the selection and lists at most a thousand", "[ALScriptProblemsPane]")
    {
        ALScriptProblemsPane& out = make();
        using S                   = ALScriptProblem::Source;
        using V                   = ALScriptProblem::Severity;
        Doc& door                 = doc("door");
        const auto row_saying     = [this](const std::string& message) -> LLScrollListItem* {
            for (LLScrollListItem* item : list()->getAllData())
            {
                if (!item->getValue().has("heading") && item->getColumn(1)->getValue().asString() == message)
                {
                    return item;
                }
            }
            return nullptr;
        };
        door.check->analysis = { problem(S::Parser, V::Error, 1, "first"), problem(S::Lint, V::Warning, 3, "second") };
        gather(door);
        LLScrollListItem* second = row_saying("second");
        REQUIRE(second != nullptr);
        CHECK(second->getColumn(3)->getValue().asString().rfind("4:", 0) == 0);
        choose("second");
        door.check->analysis = { problem(S::Parser, V::Error, 2, "first"), problem(S::Lint, V::Warning, 4, "second") };
        gather(door);
        UNSCOPED_INFO("moved a line: the same row");
        REQUIRE(row_saying("second") == second);
        UNSCOPED_INFO("where it is now");
        CHECK(second->getColumn(3)->getValue().asString().rfind("5:", 0) == 0);
        UNSCOPED_INFO("still chosen");
        CHECK(list()->getFirstSelected() == second);

        std::vector<ALScriptProblem> many;
        for (S32 i = 0; i < 1005; ++i)
        {
            many.push_back(problem(S::Lint, V::Warning, i % 10, "many " + std::to_string(i)));
        }
        door.check->analysis = many;
        gather(door);
        UNSCOPED_INFO("all counted");
        CHECK(out.held() == 1005);
        S32 rows = 0;
        for (LLScrollListItem* item : list()->getAllData())
        {
            rows += item->getValue().has("heading") ? 0 : 1;
        }
        CHECK(rows == 1000);
        UNSCOPED_INFO("the rest said");
        REQUIRE(shown().find(services.counted("ProblemsUnlisted", 5)) != std::string::npos);
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: showObject lists that object's open and checked scripts and no other's", "[ALScriptProblemsPane]")
    {
        make();
        const LLUUID house = LLUUID::generateNewID();
        const LLUUID shed  = LLUUID::generateNewID();
        Doc&         door  = doc("door");
        Doc&         lamp  = doc("lamp");
        Doc&         bench = doc("bench");
        studio.roots[door.ref.object]  = house;
        studio.roots[lamp.ref.object]  = house;
        studio.roots[bench.ref.object] = shed;
        door.check->analysis  = { problem(ALScriptProblem::Source::Parser, ALScriptProblem::Severity::Error, 1, "the door's") };
        lamp.check->analysis  = { problem(ALScriptProblem::Source::Parser, ALScriptProblem::Severity::Error, 1, "the lamp's") };
        bench.check->analysis = { problem(ALScriptProblem::Source::Parser, ALScriptProblem::Severity::Error, 1, "the bench's") };
        gather(door);
        gather(lamp);
        gather(bench);
        // A script the check read, in the house's root prim, whose object
        // has gone out of sight since.
        const ALScriptRef closed(house, LLUUID::generateNewID());
        Doc::Shown        row;
        row.line    = 3;
        row.level   = Doc::Level::Warning;
        row.origin  = "Lint";
        row.message = "the hinge's";
        pane->clearChecked();
        pane->checkedScript(closed, "hinge.lsl", false, { row }, "House");
        pane->showObject(house);
        std::string listed = shown();
        {
            INFO("the house's open scripts: " + listed);
            REQUIRE(listed.find("the door's") != std::string::npos);
            REQUIRE(listed.find("the lamp's") != std::string::npos);
        }
        UNSCOPED_INFO("and the one it read: " + listed);
        REQUIRE(listed.find("the hinge's") != std::string::npos);
        UNSCOPED_INFO("not the shed's: " + listed);
        REQUIRE(listed.find("the bench's") == std::string::npos);
        UNSCOPED_INFO("counted as the house's");
        CHECK(pane->held() == 3);

        // In front or not, the shed's script is not the house's.
        services.front = 2;
        pane->fill(&bench);
        UNSCOPED_INFO("the shed's in front, still not listed");
        REQUIRE(shown().find("the bench's") == std::string::npos);

        // Every open script's lists them all; and it is what is kept.
        pane->showEveryScript();
        listed = shown();
        {
            INFO("every open one's: " + listed);
            REQUIRE(listed.find("the bench's") != std::string::npos);
            REQUIRE(listed.find("the door's") != std::string::npos);
        }
        pane->showObject(house);
        LLSD state;
        pane->saveState(state);
        UNSCOPED_INFO("one object's is not kept");
        CHECK(state["problem_scope"].asString() == std::string("all"));
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: only LSL leftovers in a ported script are listed and counted as from LSL", "[ALScriptProblemsPane]")
    {
        make();
        Doc& d          = doc("door");
        d.language.lua  = true;
        using S         = ALScriptProblem::Source;
        using V         = ALScriptProblem::Severity;
        ALScriptProblem truth = problem(S::Lint, V::Warning, 1, "a number read as a truth");
        truth.code            = "SlNumberTruth";
        truth.key             = "LuauLintSlNumberTruth";
        ALScriptProblem sleep = problem(S::Lint, V::Note, 2, "sleeps");
        sleep.code            = "SlSleepingCall";
        sleep.key             = "LuauLintSlSleepingCall";
        ALScriptProblem compat = problem(S::Lint, V::Warning, 3, "llcompat.Say is deprecated");
        compat.key             = "LuauLintDeprecatedMemberUse";
        compat.args            = { "llcompat.Say" };
        ALScriptProblem abs = problem(S::Lint, V::Warning, 4, "ll.Abs is deprecated");
        abs.key             = "LuauLintDeprecatedMemberUseReason";
        abs.args            = { "ll.Abs", "math.abs" };
        ALScriptProblem note = problem(S::Assistant, V::Note, 0, "a note");
        note.key             = "SluaNote";
        d.check->analysis        = { note, truth, sleep, compat, abs };
        d.check->analysisVersion = d.editor->document().version();
        ALScriptProblemsPane::Made made = gather(d);
        std::string from_lsl;
        for (const Doc::Shown& row : made.rows)
        {
            if (row.migration)
            {
                UNSCOPED_INFO("said to be from LSL");
                REQUIRE(row.origin == services.words("OriginMigration"));
                from_lsl += row.message.substr(0, 4) + ";";
            }
        }
        UNSCOPED_INFO("the three");
        CHECK(from_lsl == std::string("a no;a nu;llco;"));
        d.setShown(made.rows);
        CHECK(d.shownMigration == 3);
        pane->showOrigin("OriginMigration");
        LLComboBox* origins = window.find<LLComboBox>("problems_origin");
        {
            INFO("listed alone");
            REQUIRE(origins);
            CHECK(origins->getValue().asString() == services.words("OriginMigration"));
        }
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: only an LSL leftover's menu offers and makes the from-LSL fix", "[ALScriptProblemsPane]")
    {
        ALScriptProblemsPane& out = make();
        Doc&                  d   = doc("door");
        d.language.lua            = true;
        ALScriptProblem zero      = problem(ALScriptProblem::Source::Lint, ALScriptProblem::Severity::Warning, 2, "reads nothing at 0");
        zero.code                 = "SlZeroIndex";
        zero.fixes.push_back(fix("Write it t[1]", true, false, 2));
        ALScriptProblem shadow = problem(ALScriptProblem::Source::Lint, ALScriptProblem::Severity::Warning, 4, "shadowed");
        shadow.code            = "LocalShadow";
        shadow.fixes.push_back(fix("Rename", true, true, 4));
        d.check->analysis = { zero, shadow };
        gather(d);
        out.fill(&d);
        choose("shadowed [LocalShadow]");
        UNSCOPED_INFO("not of another");
        CHECK_FALSE(out.fixShown("migration"));
        choose("reads nothing at 0 [SlZeroIndex]");
        CHECK(out.fixShown("migration"));
        out.act("fix_migration");
        REQUIRE_FALSE(studio.kinds.empty());
        CHECK(studio.kinds.back() == "door:from LSL");
    }

    TEST_CASE_METHOD(alscriptproblemspane_data, "ALScriptProblemsPane: a problem naming no line is listed and chosen at none and marked nowhere", "[ALScriptProblemsPane]")
    {
        ALScriptProblemsPane& out = make();
        Doc&                  d   = doc("door");
        d.problems.push_back({ -1, 0, false, std::string(), "ERROR", "the compiler's, nowhere" });
        d.problems.push_back({ 3, 0, false, std::string(), "WARN", "the compiler's, at 3" });
        d.runtime.push_back({ -1, -1, std::string(), "Stack-Heap Collision", 1 });
        const ALScriptProblemsPane::Made made = gather(d);
        REQUIRE(made.rows.size() == size_t(3));
        {
            INFO("at none, first");
            CHECK(made.rows[0].line == -1);
            CHECK(made.rows[1].line == -1);
            CHECK(made.rows[2].line == 3);
        }
        UNSCOPED_INFO("a warning said WARN");
        CHECK(made.rows[2].level == Doc::Level::Warning);
        {
            INFO("marked at its line alone");
            REQUIRE(made.marks.size() == 1);
            CHECK(made.marks[0].first == 3);
        }
        {
            INFO("waved at its line alone");
            REQUIRE(made.decorations.size() == 1);
            CHECK(made.decorations[0].range.begin.line == 3);
        }
        out.fill(&d);
        list()->updateSort();
        for (const LLScrollListItem* item : list()->getAllData())
        {
            const std::string message = item->getColumn(1)->getValue().asString();
            const std::string where   = item->getColumn(3)->getValue().asString();
            CAPTURE(message, where);
            REQUIRE((message == "the compiler's, at 3" ? where == "4" : where.empty()));
        }
        choose("the compiler's, nowhere");
        out.choose(false);
        REQUIRE_FALSE(studio.chosen.empty());
        UNSCOPED_INFO("chosen at none");
        CHECK(studio.chosen.back().line == -1);
        choose("the compiler's, at 3");
        out.choose(false);
        UNSCOPED_INFO("chosen at its line");
        CHECK(studio.chosen.back().line == 3);

        // The first error gone to: one at none, listed first, only where no
        // error names a line.
        out.selectFirstError(false);
        UNSCOPED_INFO("one at none, where no error is at a line");
        CHECK(studio.chosen.back().line == -1);
        d.problems.push_back({ 5, 0, false, std::string(), "ERROR", "the compiler's, at 5" });
        gather(d);
        out.fill(&d);
        out.selectFirstError(false);
        UNSCOPED_INFO("one at a line, past those at none");
        CHECK(studio.chosen.back().line == 5);
    }
}
