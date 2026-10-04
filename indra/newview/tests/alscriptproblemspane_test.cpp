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
#include "alpanelist.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfontgl.h"
#include "llscrolllistitem.h"
#include "lltabcontainer.h"

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

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            // The tab as the skin built it.
            pane = window.find<ALScriptProblemsPane>("problems_tab");
            ensure("the skin builds the tab as the pane", pane != nullptr);
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
        ensure("an error waved, a warning dashed, a run-time error waved",
               underline(2) == Style::Squiggle && underline(4) == Style::Dashed && underline(8) == Style::Squiggle);
        ensure("the run-time mark on its line",
               std::find(made.marks.begin(), made.marks.end(), std::make_pair(S32(8), ALCodeEditor::Mark::Runtime)) != made.marks.end());
        ensure("each level's mark", ALScriptProblemsPane::markOf(Doc::Level::Error) == ALCodeEditor::Mark::Error &&
                                        ALScriptProblemsPane::markOf(Doc::Level::Warning) == ALCodeEditor::Mark::Warning &&
                                        ALScriptProblemsPane::markOf(Doc::Level::Note) == ALCodeEditor::Mark::Note);
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
        door.check->analysis      = { problem(S::Parser, V::Error, 1, "door error"), problem(S::Lint, V::Warning, 3, "door warning"),
                                      problem(S::Lint, V::Note, 5, "door note") };
        lamp.check->analysis       = { problem(S::Parser, V::Error, 2, "lamp error") };
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
        ensure("rows to show: nothing said over them", !out.list()->saysEmpty());
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
        lamp.check->analysis.push_back(problem(S::Lint, V::Warning, 7, "lamp warning"));
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

        // A script with nothing wrong with it says so where the rows would be.
        Doc& clean = doc("clean");
        gather(clean);
        out.fill(&clean);
        ensure("nothing listed, said", out.list()->saysEmpty()
                                           && out.list()->emptyWords() == services.words("NoProblemsIn", { { "[NAME]", "clean.lsl" } }));
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<3>()
    {
        set_test_name("the first error chosen and gone to, passing over the compiler's where asked; a row chosen goes to its place");
        ALScriptProblemsPane& out = make();
        Doc&                  d   = doc("door");
        d.problems.push_back({ 1, 0, true, std::string(), "ERROR", "compiler first" });
        d.check->analysis = { problem(ALScriptProblem::Source::Lint, ALScriptProblem::Severity::Warning, 0, "a warning before"),
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

        // The list's keys: return goes there to type, escape back to the
        // script without going.
        ensure("return", out.list()->handleKeyHere(KEY_RETURN, MASK_NONE) && studio.chosen.back().line == 0 && studio.toEditor.back());
        const size_t gone = studio.chosen.size();
        ensure("escape", out.list()->handleKeyHere(KEY_ESCAPE, MASK_NONE) && services.reveals.back() && studio.chosen.size() == gone);
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
        d.check->analysis      = { one, two, plain };
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
        ensure("and not recalled when it is opened again", studio.cleared == std::vector<std::string>{ d.id });
        ensure_equals("and the tab's problems gathered again", studio.refreshed.size(), size_t(1));
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<5>()
    {
        set_test_name("the compiler's error a line from the analyzer's is the same error said once; the compiler's said to be as of the last save once the text has changed");
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
        ensure("the one a line after said once", std::find(messages.begin(), messages.end(), "syntax error") == messages.end());
        ensure("one further off kept", std::find(messages.begin(), messages.end(), "far from any") != messages.end());
        // Typed in since the save: what the compiler said is of the text
        // as it was.
        d.editor->insertText("x");
        made = gather(d);
        bool marked = false;
        for (const Doc::Shown& row : made.rows)
        {
            marked |= row.message == "far from any " + services.words("CompilerAsSaved");
        }
        ensure("as of the last save", marked);
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<6>()
    {
        set_test_name("an LSL lint says its name, which a NOLINT comment turns it off by; an error says no number; the origins offer the weight");
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
        ensure("the lint named", named);
        ensure("the error no number", !numbered);
        LLComboBox* origins = window.find<LLComboBox>("problems_origin");
        ensure("the origins offer the weight", origins && origins->selectByValue(LLSD(services.words("OriginWeight"))));
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<7>()
    {
        set_test_name("code past four fifths of its target's limit is a warning that says what is left to run in; not before the optimizer, nor under");
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
        ensure_equals("near: one row", rows.size(), size_t(1));
        ensure("a warning", rows[0].level == Doc::Level::Warning);
        ensure("what is left", rows[0].message == services.words("WeightNear", { { "[SIZE]", "14.0" }, { "[LIMIT]", "16" }, { "[LEFT]", "2.0" }, { "[TARGET]", "LSL (LSO)" } }));
        d.weighing->exact = false;
        ensure("before the optimizer, nothing", weights(gather(d)).empty());
        d.weighing->exact            = true;
        d.weighing->weight->total    = 12 * 1024;
        ensure("at three quarters, nothing", weights(gather(d)).empty());
        d.weighing->weight->total    = 17 * 1024;
        rows = weights(gather(d));
        ensure("over: the over row alone", rows.size() == 1 && rows[0].message.find("more than") != std::string::npos);
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<8>()
    {
        set_test_name("a problem in code the preprocessor made is listed under that name, not marked on the source's line of its number");
        make();
        Doc& d = doc("door");
        ALScriptProblem made_here = problem(ALScriptProblem::Source::Parser, ALScriptProblem::Severity::Error, 1, "in a switch's jump table", Doc::GENERATED);
        d.check->analysis          = { made_here };
        d.check->analysisVersion   = d.editor->document().version();
        const ALScriptProblemsPane::Made made = gather(d);
        ensure_equals("one row", made.rows.size(), size_t(1));
        ensure_equals("named so", made.rows[0].fileName, services.words("InGeneratedCode"));
        ensure("no mark, no squiggle", made.marks.empty() && made.decorations.empty());
    }

    template<> template<>
    void alscriptproblemspane_object::test<9>()
    {
        set_test_name("a script no tab holds, which an object's check reached, is listed after the open ones under its name, chosen by its item, and gives way to its tab");
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
        ensure("the open one's, then the closed one's under its name and object: " + listed,
               listed.find("in the open one") < listed.find(heading) && listed.find(heading) < listed.find("in the closed one") &&
                   listed.find("in the closed one") != std::string::npos);
        ensure_equals("counted with the rest", pane->held(), 2);

        // Chosen by its item and name, with no tab.
        const std::vector<LLScrollListItem*> rows = list()->getAllData();
        S32 at = -1;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            at = rows[i]->getValue()["owner"].asString().rfind("checked:", 0) == 0 ? static_cast<S32>(i) : at;
        }
        ensure("a row of it", at >= 0);
        list()->selectNthItem(at);
        pane->choose(false);
        ensure("chosen", !studio.chosen.empty());
        const ALScriptProblemsPane::Place& place = studio.chosen.back();
        ensure("by its item, with no tab", place.ref == closed && place.name == "hinge.lsl" && place.doc.empty() && place.line == 3);

        // Opened, its tab says; a new check lets the last one's go.
        pane->forgetChecked(closed);
        ensure("given way", shown().find("in the closed one") == std::string::npos && pane->held() == 1);
        pane->checkedScript(closed, "hinge.lsl", false, { row }, "House");
        ensure_equals("back", pane->checkedCount(), size_t(1));
        pane->clearChecked();
        ensure("let go of", pane->checkedCount() == 0 && shown().find("in the closed one") == std::string::npos);
    }

    template<> template<>
    void alscriptproblemspane_object::test<10>()
    {
        set_test_name("what a recompile's compiler said of a closed script is listed beside its check's rows, the last recompile's alone, and goes when it compiles clean");
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
        ensure("clean, and nothing listed of it: nothing to list", pane->checkedCount() == 0);

        pane->checkedScript(closed, "hinge.lsl", false, { row("the check's", "Lint") }, "House");
        pane->compiledScript(closed, "hinge.lsl", false, { row("the compiler's", "Compiler") }, "House");
        ensure("both listed", shown().find("the check's") != std::string::npos && shown().find("the compiler's") != std::string::npos);
        ensure_equals("counted", pane->held(), 2);

        pane->compiledScript(closed, "hinge.lsl", false, { row("again", "Compiler") }, "House");
        ensure("the last recompile's alone", shown().find("the compiler's") == std::string::npos && shown().find("again") != std::string::npos);
        pane->compiledScript(closed, "hinge.lsl", false, {}, "House");
        ensure("clean: the compiler's gone, the check's stay", shown().find("again") == std::string::npos && shown().find("the check's") != std::string::npos);

        // Recompiled alone, with no check: gone with its rows.
        LLUUID other;
        other.generate();
        const ALScriptRef lone(object, other);
        pane->compiledScript(lone, "latch.lsl", false, { row("latch", "Compiler") }, "House");
        ensure_equals("listed", pane->checkedCount(), size_t(2));
        pane->compiledScript(lone, "latch.lsl", false, {}, "House");
        ensure_equals("gone", pane->checkedCount(), size_t(1));
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<11>()
    {
        set_test_name("out of sight, the counts said at once and the rows listed once the list is seen, or once a row is to be chosen");
        ALScriptProblemsPane& out  = make();
        using S                    = ALScriptProblem::Source;
        using V                    = ALScriptProblem::Severity;
        LLTabContainer* tabs       = window.find<LLTabContainer>("bottom_tabs");
        Doc&            door       = doc("door");
        door.check->analysis       = { problem(S::Parser, V::Error, 1, "door error"), problem(S::Lint, V::Warning, 3, "door warning") };
        tabs->selectTabByName("references_tab");
        ensure("out of sight", !ALPaneFolds::inSight(&out));
        const S32 said = studio.counts;
        gather(door);
        ensure("counted, and said on the tab", out.held() == 2 && studio.counts > said);
        ensure_equals("not listed", shown(), std::string());
        out.pump();
        ensure_equals("nor while unseen", shown(), std::string());
        tabs->selectTabByName("problems_tab");
        out.pump();
        ensure_equals("seen: listed", shown(), std::string("|door error|door warning"));

        tabs->selectTabByName("references_tab");
        door.check->analysis.push_back(problem(S::Parser, V::Error, 5, "a second error"));
        gather(door);
        ensure("counted again, the rows as they were", out.held() == 3 && shown() == "|door error|door warning");
        const size_t chosen = studio.chosen.size();
        out.selectFirstError(false);
        ensure("a row to choose: listed first", shown().find("a second error") != std::string::npos);
        ensure("and chosen", studio.chosen.size() == chosen + 1 && studio.chosen.back().line == 1);
        tabs->selectTabByName("problems_tab");
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<12>()
    {
        set_test_name("a refill keeps the rows that say the same, moved or not, and the row chosen; no more than a thousand listed, the rest counted");
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
        ensure("listed", second != nullptr && second->getColumn(3)->getValue().asString().rfind("4:", 0) == 0);
        choose("second");
        door.check->analysis = { problem(S::Parser, V::Error, 2, "first"), problem(S::Lint, V::Warning, 4, "second") };
        gather(door);
        ensure("moved a line: the same row", row_saying("second") == second);
        ensure("where it is now", second->getColumn(3)->getValue().asString().rfind("5:", 0) == 0);
        ensure("still chosen", list()->getFirstSelected() == second);

        std::vector<ALScriptProblem> many;
        for (S32 i = 0; i < 1005; ++i)
        {
            many.push_back(problem(S::Lint, V::Warning, i % 10, "many " + std::to_string(i)));
        }
        door.check->analysis = many;
        gather(door);
        ensure_equals("all counted", out.held(), 1005);
        S32 rows = 0;
        for (LLScrollListItem* item : list()->getAllData())
        {
            rows += item->getValue().has("heading") ? 0 : 1;
        }
        ensure_equals("a thousand listed", rows, 1000);
        ensure("the rest said", shown().find(services.counted("ProblemsUnlisted", 5)) != std::string::npos);
    }

    template<> template<>
    void alscriptproblemspane_object::test<13>()
    {
        set_test_name("an object's check lists that object's scripts: its open ones and those it read, none of another object's open beside them");
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
        ensure("the house's open scripts: " + listed, listed.find("the door's") != std::string::npos && listed.find("the lamp's") != std::string::npos);
        ensure("and the one it read: " + listed, listed.find("the hinge's") != std::string::npos);
        ensure("not the shed's: " + listed, listed.find("the bench's") == std::string::npos);
        ensure_equals("counted as the house's", pane->held(), 3);

        // In front or not, the shed's script is not the house's.
        services.front = 2;
        pane->fill(&bench);
        ensure("the shed's in front, still not listed", shown().find("the bench's") == std::string::npos);

        // Every open script's lists them all; and it is what is kept.
        pane->showEveryScript();
        listed = shown();
        ensure("every open one's: " + listed, listed.find("the bench's") != std::string::npos && listed.find("the door's") != std::string::npos);
        pane->showObject(house);
        LLSD state;
        pane->saveState(state);
        ensure_equals("one object's is not kept", state["problem_scope"].asString(), std::string("all"));
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<14>()
    {
        set_test_name("what a script moved from LSL is left with -- a converter's note, an LSL habit's lint, a call to llcompat -- listed as "
                      "from LSL, counted, and the origin that lists them alone; not a performance lint, nor ll's own deprecation");
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
                ensure_equals("said to be from LSL", row.origin, services.words("OriginMigration"));
                from_lsl += row.message.substr(0, 4) + ";";
            }
        }
        ensure_equals("the three", from_lsl, std::string("a no;a nu;llco;"));
        d.setShown(made.rows);
        ensure_equals("counted", d.shownMigration, 3);
        pane->showOrigin("OriginMigration");
        LLComboBox* origins = window.find<LLComboBox>("problems_origin");
        ensure("listed alone", origins && origins->getValue().asString() == services.words("OriginMigration"));
    }

    template <>
    template <>
    void alscriptproblemspane_object::test<15>()
    {
        set_test_name("the menu of a problem left from LSL offers what is left from LSL fixed, and makes it; another problem's does not");
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
        ensure("not of another", !out.fixShown("migration"));
        choose("reads nothing at 0 [SlZeroIndex]");
        ensure("offered", out.fixShown("migration"));
        out.act("fix_migration");
        ensure("asked", !studio.kinds.empty() && studio.kinds.back() == "door:from LSL");
    }
}
