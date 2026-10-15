/**
 * @file alscriptcrumbsbar_test.cpp
 * @brief Script Studio's bar under the editor over the studio's own window, the window's side faked.
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

#include "../alscriptcrumbsbar.h"

#include "../alscriptstudiocaret.h"
#include "../alscriptstudiochecking.h"
#include "../alscriptstudioplaces.h"
#include "../alscriptstudioweighing.h"

#include "alcodeeditor.h"
#include "alscriptenvelope.h"
#include "alscriptstudio_fixture.h"
#include "llfocusmgr.h"
#include "llnotecard.h"
#include "lltimer.h"
#include "lluicolortable.h"

#include <catch2/catch_test_macros.hpp>

#include <list>
#include <optional>

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef ALJumpBar::TrailerPart   Part;
    typedef std::vector<std::string> Names;

    // The window's side of the bar, faked: what it says, and a record of
    // what it was told.
    struct FakeCrumbsWindow : public ALScriptCrumbsBar::Window
    {
        void crumbChosen(Doc& doc, std::optional<ALTextRange> at) override
        {
            chosen.push_back(doc.id + (at ? " " + std::to_string(at->begin.line) + ":" + std::to_string(at->begin.column) + "-" +
                                                std::to_string(at->end.line) + ":" + std::to_string(at->end.column)
                                          : std::string(" nowhere")));
        }
        void        trailerChosen(const std::string& value) override { pressed.push_back(value); }
        std::string vimBanner() const override { return banner; }
        void        problemCounts(const Doc&, S32& e, S32& w) const override
        {
            e = errors;
            w = warnings;
        }
        std::optional<ALScriptWeight::Target> weightTarget(const Doc&) const override { return target; }
        ALPreprocessor::Wanted                preprocessedWhy(const Doc&) const override { return wanted; }

        Names                                 chosen, pressed;
        std::string                           banner;
        S32                                   errors = 0, warnings = 0;
        std::optional<ALScriptWeight::Target> target;
        ALPreprocessor::Wanted                wanted = ALPreprocessor::Wanted::No;
    };

    ALScriptSpan span(S32 line, S32 column, S32 end_line, S32 end_column)
    {
        ALScriptSpan out;
        out.line      = line;
        out.column    = column;
        out.endLine   = end_line;
        out.endColumn = end_column;
        return out;
    }
    ALScriptOutlineEntry entry(const std::string& name, S32 depth, const ALScriptSpan& whole, S32 name_column)
    {
        ALScriptOutlineEntry out;
        out.name     = name;
        out.depth    = depth;
        out.span     = whole;
        out.nameSpan = span(whole.line, name_column, whole.line, name_column + static_cast<S32>(name.size()));
        return out;
    }
    std::string joined(const Names& names)
    {
        std::string out;
        for (const std::string& name : names)
        {
            out += (out.empty() ? "" : ", ") + name;
        }
        return out;
    }
    const std::string SCRIPT = "default\n{\n    state_entry()\n    {\n    }\n    touch_start(integer n)\n    {\n    }\n}\n\nf()\n{\n}\n"
                               "g(){}h(){}\nstate other\n{\n    timer()\n    {\n    }\n}\n";

    // A colour of the table set while this lives, and put back as it was
    // after -- a person's own again, or none -- for the UI tests after it.
    class HeldColor
    {
    public:
        HeldColor(const std::string& name, const LLColor4& color) : mName(name)
        {
            const auto& own = LLUIColorTable::instance().getUserColors();
            if (const auto found = own.find(name); found != own.end())
            {
                mWas = found->second.get();
            }
            LLUIColorTable::instance().setColor(name, color);
        }
        ~HeldColor()
        {
            if (mWas)
            {
                LLUIColorTable::instance().setColor(mName, *mWas);
            }
            else
            {
                LLUIColorTable::instance().resetToDefault(mName);
            }
        }
        HeldColor(const HeldColor&)            = delete;
        HeldColor& operator=(const HeldColor&) = delete;

    private:
        std::string             mName;
        std::optional<LLColor4> mWas;
    };
}

namespace
{
    struct alscriptcrumbsbar_data
    {
        al_studio_test::StudioWindowOf<FakeCrumbsWindow> window;
        // The marks' colours told apart, as the skin's are: with none, every
        // mark is one red.
        HeldColor warningMark{ "CodeMarkWarning", LLColor4::yellow };
        HeldColor errorMark{ "CodeMarkError", LLColor4::red };

        ~alscriptcrumbsbar_data() { gFocusMgr.setKeyboardFocus(nullptr); }
        ALScriptCrumbsBar* bar()
        {
            ALScriptCrumbsBar* found = window.find<ALScriptCrumbsBar>("crumbs");
            if (!found)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            // The jump bar the skin gives the bar, which every check after
            // reads what the bar shows from.
            INFO("the skin gives the bar its jump bar");
            REQUIRE(jump() != nullptr);
            return found;
        }
        ALJumpBar*        jump() { return window.find<ALJumpBar>("breadcrumb"); }
        // The caret's path found, as the caret's unit finds it, and shown.
        void place(ALScriptCrumbsBar* crumbs, Doc& doc)
        {
            doc.caret->crumbPath = ALScriptPlaces::pathAt(doc.outline, doc.editor->caret());
            doc.caret->crumbsOf  = doc.check->analysisVersion;
            crumbs->showPath(doc);
        }
        FakeCrumbsWindow& told() { return window.pane(); }
        ALCodeEditor*     editor(const std::string& name, const std::string& text)
        {
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name               = name;
            p.rect               = LLRect(0, 200, 400, 0);
            ALCodeEditor* made   = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(made);
            made->setText(text);
            return made;
        }
        // A script: a state with two events, then a function.
        Doc& tab(const std::string& id)
        {
            bar();
            Doc& doc       = window.services().addDoc(id, ALScriptRef(), "Script " + id);
            doc.loaded     = true;
            doc.modifiable = true;
            doc.editor     = editor("editor_" + id, SCRIPT);
            doc.outline    = { entry("default", 0, span(0, 0, 8, 1), 0),     entry("state_entry", 1, span(2, 4, 4, 5), 4),
                               entry("touch_start", 1, span(5, 4, 7, 5), 4), entry("f", 0, span(10, 0, 12, 1), 0),
                               entry("g", 0, span(13, 0, 13, 5), 0),         entry("h", 0, span(13, 5, 13, 10), 5),
                               entry("other", 0, span(14, 0, 19, 1), 6),     entry("timer", 1, span(16, 4, 18, 5), 4) };
            return doc;
        }
        std::string path()
        {
            Names out;
            for (const ALJumpBar::Crumb& crumb : jump()->path())
            {
                Names others;
                for (const auto& [label, value] : crumb.alternatives)
                {
                    others.push_back(label);
                }
                out.push_back(crumb.label + (others.empty() ? "" : " [" + joined(others) + "]"));
            }
            return joined(out);
        }
        // The trailer's parts, without the dots between them.
        std::vector<Part> parts()
        {
            std::vector<Part> out;
            for (const Part& part : jump()->trailer())
            {
                if (part.text != "   \xC2\xB7   ")
                {
                    out.push_back(part);
                }
            }
            return out;
        }
        std::string trailer()
        {
            Names out;
            for (const Part& part : parts())
            {
                out.push_back(part.text);
            }
            return joined(out);
        }
        // The part that begins so, or none.
        const Part* part(const std::string& begins)
        {
            const std::vector<Part>& now = held.emplace_back(parts());
            for (const Part& one : now)
            {
                if (one.text.rfind(begins, 0) == 0)
                {
                    return &one;
                }
            }
            return nullptr;
        }
        // The parts each part() looked through, kept for the case: a part one
        // call found is still there after the next, so a check that compares
        // two reads both, whichever is found first.
        std::list<std::vector<Part>> held;
    };

    TEST_CASE_METHOD(alscriptcrumbsbar_data, "alscriptcrumbsbar: the caret's path offers each step's siblings and follows changes", "[alscriptcrumbsbar]")
    {
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        doc.editor->setCaret(ALTextPos(3, 5));
        place(crumbs, doc);
        const std::string top = "Script a [default, f, g, h, other]";
        CHECK(path() == top + ", default [default, f, g, h, other], state_entry [state_entry, touch_start]");
        UNSCOPED_INFO("kept");
        CHECK(doc.caret->crumbPath.size() == size_t(2));
        REQUIRE(jump()->path().size() == size_t(3));
        UNSCOPED_INFO("the script's step");
        CHECK(jump()->path()[0].value == std::string("top"));
        UNSCOPED_INFO("said");
        CHECK(jump()->path()[0].toolTip ==
                      std::string("Script a: click to go to the top; the arrow lists the script's declarations"));
        UNSCOPED_INFO("a symbol's");
        CHECK(jump()->path()[2].toolTip == std::string("Go to state_entry; the arrow lists other items at this level"));

        doc.editor->setCaret(ALTextPos(4, 0));
        place(crumbs, doc);
        CHECK(trailer().find("Ln 5, Col 1") != std::string::npos);
        doc.editor->setCaret(ALTextPos(11, 0));
        place(crumbs, doc);
        UNSCOPED_INFO("in f");
        CHECK(path() == top + ", f [default, f, g, h, other]");
        doc.editor->setCaret(ALTextPos(9, 0));
        place(crumbs, doc);
        UNSCOPED_INFO("between: the script");
        CHECK(path() == top);
        doc.editor->setCaret(ALTextPos(4, 5));
        place(crumbs, doc);
        UNSCOPED_INFO("at a symbol's very end: still in it");
        CHECK(path().find("state_entry [") != std::string::npos);
        doc.editor->setCaret(ALTextPos(13, 5));
        place(crumbs, doc);
        REQUIRE_FALSE(jump()->path().empty());
        UNSCOPED_INFO("where two meet: the later");
        CHECK(jump()->path().back().label == std::string("h"));
        doc.editor->setCaret(ALTextPos(17, 0));
        place(crumbs, doc);
        UNSCOPED_INFO("alone in its state: nothing beside it");
        CHECK(path() == top + ", other [default, f, g, h, other], timer");

        doc.name = "Renamed";
        place(crumbs, doc);
        REQUIRE_FALSE(jump()->path().empty());
        UNSCOPED_INFO("a new name: again");
        CHECK(jump()->path()[0].label == std::string("Renamed"));
        Doc& other = tab("b");
        other.name = "Other";
        place(crumbs, other);
        REQUIRE_FALSE(jump()->path().empty());
        UNSCOPED_INFO("not in front: nothing");
        CHECK(jump()->path()[0].label == std::string("Renamed"));
        crumbs->forget();
        {
            INFO("forgotten");
            CHECK(jump()->path().empty());
            CHECK(jump()->trailer().empty());
        }
        place(crumbs, doc);
        REQUIRE_FALSE(jump()->path().empty());
        UNSCOPED_INFO("and shown again whatever");
        CHECK(jump()->path()[0].label == std::string("Renamed"));
    }

    TEST_CASE_METHOD(alscriptcrumbsbar_data, "alscriptcrumbsbar: the trailer's parts and tips and which go first where it is narrow", "[alscriptcrumbsbar]")
    {
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        crumbs->setTips({ "line tip", "problems tip", "source tip", "expanded tip" });
        doc.editor->setCaret(ALTextPos(2, 6));
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("the caret and the indentation");
        CHECK(trailer() == std::string("Ln 3, Col 7, Tab Size: 4"));
        {
            INFO("pressed for a line");
            const Part* line = part("Ln");
            REQUIRE(line != nullptr);
            CHECK(line->value == "line");
            CHECK(line->toolTip == "line tip");
        }

        doc.modifiable  = false;
        told().banner   = "-- INSERT --";
        told().errors   = 2;
        told().warnings = 1;
        doc.editor->setSelection(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 0)));
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("in order");
        CHECK(trailer() == std::string("Read-only, -- INSERT --, Ln 5, Col 1, 2 lines selected, Tab Size: 4, 2 errors, 1 warning"));
        const std::vector<Part>& raw = jump()->trailer();
        {
            INFO("a dot between each");
            REQUIRE(raw.size() == 13);
            CHECK(raw[1].text == "   \xC2\xB7   ");
            CHECK(raw[11].text == raw[1].text);
        }
        UNSCOPED_INFO("the last a part");
        CHECK(raw[12].text == std::string("1 warning"));
        {
            INFO("the dots only between");
            CHECK(raw[1].between);
            CHECK(raw[11].between);
            CHECK_FALSE(raw[12].between);
        }
        const Part* indentation = part("Tab");
        const Part* selection   = part("2 lines");
        const Part* errors      = part("2 errors");
        const Part* warnings    = part("1 warning");
        const Part* readOnly    = part("Read");
        const Part* position    = part("Ln");
        REQUIRE(indentation != nullptr);
        REQUIRE(selection != nullptr);
        REQUIRE(errors != nullptr);
        REQUIRE(readOnly != nullptr);
        REQUIRE(position != nullptr);
        {
            INFO("where narrow: the indentation first, then the selection, the counts and read only last, the place never");
            CHECK(indentation->drop > selection->drop);
            CHECK(selection->drop > errors->drop);
            CHECK(errors->drop > 0);
            CHECK(readOnly->drop > 0);
            CHECK(position->drop == 0);
        }
        UNSCOPED_INFO("read only, said");
        CHECK(readOnly->toolTip == std::string("You can view this script but not change it"));
        {
            INFO("the problems pressable");
            CHECK(errors->value == "problems");
            REQUIRE(warnings != nullptr);
            CHECK(warnings->toolTip == "problems tip");
        }
        doc.editor->setSelection(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 2)));
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("a line begun counts");
        CHECK(part("3 lines") != nullptr);
        doc.editor->setText("a\tb\xC3\xA9\n");
        doc.editor->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)));
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("characters, a tab one and an accented one one");
        CHECK(part("4 characters selected") != nullptr);
        doc.editor->setCaret(ALTextPos(0, 2));
        crumbs->showTrailer(doc);
        const Part* column = part("Ln");
        REQUIRE(column != nullptr);
        UNSCOPED_INFO("the column as seen, past the tab");
        CHECK(column->text == "Ln 1, Col " + std::to_string(doc.editor->getTabWidth() + 1));
        doc.loaded = false;
        crumbs->showTrailer(doc);
        {
            INFO("not loaded: not said read only, nor how it is indented");
            CHECK(part("Read") == nullptr);
            CHECK(part("Tab") == nullptr);
        }
    }

    TEST_CASE_METHOD(alscriptcrumbsbar_data, "alscriptcrumbsbar: code and save sizes coloured near their limits and the view pressable", "[alscriptcrumbsbar]")
    {
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        const LLColor4     error  = doc.editor->markColor(ALCodeEditor::Mark::Error);
        const LLColor4     warn   = doc.editor->markColor(ALCodeEditor::Mark::Warning);
        ALScriptWeight     weight;
        weight.target = ALScriptWeight::Target::Mono;
        weight.limit  = 65536;
        weight.total  = 40960;
        doc.weighing->weight = weight;
        crumbs->showTrailer(doc);
        const Part* weighed = part("LSL (Mono)");
        {
            INFO("weighed");
            REQUIRE(weighed);
            CHECK(weighed->text == "LSL (Mono) 40.0 of 64 KB used");
            CHECK_FALSE(weighed->color);
        }
        UNSCOPED_INFO("before the optimizer");
        CHECK(weighed->toolTip.find("Measured before the preprocessor") != std::string::npos);
        doc.weighing->exact = true;
        doc.weighing->sent  = true;
        doc.weighing->weight->total = 60000;
        crumbs->showTrailer(doc);
        weighed = part("LSL (Mono)");
        REQUIRE(weighed != nullptr);
        UNSCOPED_INFO("as sent");
        CHECK(weighed->toolTip.find("as it is saved") != std::string::npos);
        UNSCOPED_INFO("near: the warning's");
        CHECK(weighed->color == warn);
        doc.weighing->weight->total = 70000;
        crumbs->showTrailer(doc);
        weighed = part("LSL (Mono)");
        REQUIRE(weighed != nullptr);
        UNSCOPED_INFO("past: the error's");
        CHECK(weighed->color == error);
        doc.weighing->weight->estimate = true;
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("an estimate");
        CHECK(part("LSL (Mono) ~") != nullptr);
        doc.weighing->weight.reset();

        const size_t LIMIT       = ALScriptEnvelope::MAX_ASSET_BYTES;
        doc.weighing->assetBytes = LIMIT / 2;
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("half: nothing");
        CHECK(parts().size() == 2);
        doc.weighing->assetBytes = LIMIT / 2 + 1;
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("past half");
        REQUIRE(part(std::to_string((LIMIT / 2 + 1 + 1023) / 1024) + " KB") != nullptr);
        {
            INFO("said");
            CHECK(parts().back().toolTip.find("Save size") != std::string::npos);
            CHECK_FALSE(parts().back().color);
        }
        doc.weighing->assetBytes = LIMIT * 9 / 10 + 1;
        crumbs->showTrailer(doc);
        REQUIRE_FALSE(parts().empty());
        UNSCOPED_INFO("near: the warning's");
        CHECK(parts().back().color == warn);
        doc.weighing->assetBytes = LIMIT + 1;
        crumbs->showTrailer(doc);
        REQUIRE_FALSE(parts().empty());
        UNSCOPED_INFO("over: the error's");
        CHECK(parts().back().color == error);
        UNSCOPED_INFO("and by how much");
        CHECK(parts().back().toolTip.find("1 over the") != std::string::npos);
        doc.notecard             = true;
        doc.weighing->assetBytes = static_cast<size_t>(LLNotecard::MAX_SIZE) / 2 + 1;
        crumbs->showTrailer(doc);
        REQUIRE_FALSE(parts().empty());
        UNSCOPED_INFO("a notecard's by its own limit");
        CHECK(parts().back().toolTip.find("Notecard text") != std::string::npos);
        doc.notecard             = false;
        doc.weighing->assetBytes = 0;

        crumbs->setTips({ "line tip", "problems tip", "source tip", "expanded tip" });
        doc.expandedEditor = editor("expanded_a", "integer x;\n");
        crumbs->showTrailer(doc);
        REQUIRE_FALSE(parts().empty());
        const Part shown = parts().back();
        {
            INFO("the source in front");
            CHECK(shown.text == "Source");
            CHECK(shown.value == "expanded");
            CHECK(shown.toolTip == "source tip");
        }
        doc.view = Doc::View::Expanded;
        doc.expandedEditor->setCaret(ALTextPos(0, 3));
        crumbs->showTrailer(doc);
        REQUIRE_FALSE(parts().empty());
        {
            INFO("the expansion");
            CHECK(parts().back().text == "Preprocessed");
            CHECK(parts().back().toolTip == "expanded tip");
        }
        UNSCOPED_INFO("its own caret");
        CHECK(part("Ln 1, Col 4") != nullptr);
    }

    TEST_CASE_METHOD(alscriptcrumbsbar_data, "alscriptcrumbsbar: the optimizer's sizes replace the weight while the expansion is in front", "[alscriptcrumbsbar]")
    {
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        ALScriptWeight     weight;
        weight.target       = ALScriptWeight::Target::Mono;
        weight.limit        = 65536;
        weight.total        = 40960;
        doc.weighing->weight = weight;
        doc.expandedEditor  = editor("expanded_a", "integer x;\n");
        doc.view            = Doc::View::Expanded;
        told().wanted       = ALPreprocessor::Wanted::Setting;
        doc.uploaded.valid      = true;
        doc.uploaded.version    = doc.editor->document().version();
        doc.uploaded.codeBefore = 20480;
        doc.uploaded.codeAfter  = 10240;
        told().target           = ALScriptWeight::Target::Mono;
        crumbs->showTrailer(doc);
        const Part* weighed = part("LSL (Mono)");
        REQUIRE(weighed != nullptr);
        UNSCOPED_INFO("before and after");
        CHECK(weighed->text == std::string("LSL (Mono) ~20.0 \xE2\x86\x92 ~10.0 of 64 KB used"));
        UNSCOPED_INFO("once");
        CHECK(parts().size() == size_t(4));
        doc.uploaded.codeAfter = 60000;
        crumbs->showTrailer(doc);
        weighed = part("LSL (Mono)");
        REQUIRE(weighed != nullptr);
        UNSCOPED_INFO("near: the warning's");
        CHECK(weighed->color == doc.editor->markColor(ALCodeEditor::Mark::Warning));
        doc.uploaded.codeAfter = 70000;
        crumbs->showTrailer(doc);
        weighed = part("LSL (Mono)");
        REQUIRE(weighed != nullptr);
        UNSCOPED_INFO("past: the error's");
        CHECK(weighed->color == doc.editor->markColor(ALCodeEditor::Mark::Error));
        told().target = ALScriptWeight::Target::LSO;
        crumbs->showTrailer(doc);
        {
            INFO("measured, not estimated");
            REQUIRE(part("LSL (LSO)"));
            CHECK(part("LSL (LSO)")->text.find('~') == std::string::npos);
        }
        doc.editor->insertText("x");
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("typed since: its weight");
        CHECK(part("LSL (Mono) 40.0") != nullptr);
        doc.view             = Doc::View::Source;
        doc.uploaded.version = doc.editor->document().version();
        told().target        = ALScriptWeight::Target::Mono;
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("the source in front: its weight");
        CHECK(part("LSL (Mono) 40.0") != nullptr);
    }

    TEST_CASE_METHOD(alscriptcrumbsbar_data, "alscriptcrumbsbar: a chosen step goes to the top or a symbol's name or nowhere if gone", "[alscriptcrumbsbar]")
    {
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        crumbs->choose("top");
        crumbs->choose(ALScriptPlaces::outlineValue(doc, 1));
        crumbs->choose("9\ngone");
        doc.outline.erase(doc.outline.begin());
        crumbs->choose("1\ntouch_start");
        CHECK(joined(told().chosen) == std::string("a 0:0-0:0, a 2:4-2:15, a nowhere, a 5:4-5:15"));
    }

    TEST_CASE_METHOD(alscriptcrumbsbar_data, "alscriptcrumbsbar: the indentation part names its source and its menu sets this script only", "[alscriptcrumbsbar]")
    {
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        crumbs->showTrailer(doc);
        {
            INFO("tabs, by default");
            REQUIRE(part("Tab Size: 4"));
            CHECK(part("Tab Size")->value == "indent");
        }
        UNSCOPED_INFO("the default's tip");
        CHECK(part("Tab Size")->toolTip.find("Default indentation from Preferences") != std::string::npos);
        {
            INFO("checked as it is");
            CHECK(crumbs->indentChecked("tabs"));
            CHECK(crumbs->indentChecked("width_4"));
            CHECK_FALSE(crumbs->indentChecked("spaces"));
        }
        UNSCOPED_INFO("nothing chosen to forget");
        CHECK_FALSE(crumbs->indentEnabled("read"));
        doc.editor->setReadsIndentation(true);
        doc.editor->setText("a\n  b\n");
        crumbs->showTrailer(doc);
        {
            INFO("read from the script");
            REQUIRE(part("Spaces: 2"));
            CHECK(part("Spaces")->toolTip.find("detected from this script's lines") != std::string::npos);
        }

        crumbs->indentAct("width_4");
        {
            INFO("a width chosen");
            REQUIRE(part("Spaces: 4"));
            CHECK(part("Spaces")->toolTip.find("set for this script") != std::string::npos);
        }
        {
            INFO("and checked");
            CHECK(crumbs->indentChecked("width_4"));
            CHECK_FALSE(crumbs->indentChecked("width_2"));
        }
        UNSCOPED_INFO("the text untouched");
        CHECK(doc.editor->text() == "a\n  b\n");
        crumbs->indentAct("tabs");
        {
            INFO("tabs chosen");
            CHECK(part("Tab Size: 4") != nullptr);
            CHECK(crumbs->indentChecked("tabs"));
        }
        UNSCOPED_INFO("to be forgotten");
        CHECK(crumbs->indentEnabled("read"));
        crumbs->indentAct("read");
        UNSCOPED_INFO("the script's own again");
        CHECK(part("Spaces: 2") != nullptr);

        crumbs->indentAct("convert_tabs");
        UNSCOPED_INFO("converted, a tab of the width");
        CHECK(doc.editor->text() == std::string("a\n\tb\n"));
        UNSCOPED_INFO("and tabs from here on");
        CHECK(part("Tab Size: 2") != nullptr);
        doc.modifiable = false;
        UNSCOPED_INFO("not a script that may only be read");
        CHECK_FALSE(crumbs->indentEnabled("convert_spaces"));
        crumbs->indentAct("convert_spaces");
        UNSCOPED_INFO("left as it was");
        CHECK(doc.editor->text() == std::string("a\n\tb\n"));
    }

    TEST_CASE_METHOD(alscriptcrumbsbar_data, "alscriptcrumbsbar: a check out a while is said but one just asked or answered is not", "[alscriptcrumbsbar]")
    {
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        doc.loaded                 = true;
        const F64 now              = LLTimer::getTotalSeconds();
        doc.check->requestedVersion = doc.editor->document().version();
        doc.check->analysisVersion  = doc.check->requestedVersion + 1;
        doc.check->askedAt          = now;
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("just asked: nothing");
        CHECK(part("Checking") == nullptr);
        doc.check->askedAt = now - 1.0;
        crumbs->showTrailer(doc);
        const Part* checking = part("Checking");
        {
            INFO("out a while: said");
            REQUIRE(checking != nullptr);
            CHECK(checking->value == "problems");
        }
        doc.check->analysisVersion = doc.check->requestedVersion;
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("answered: gone");
        CHECK(part("Checking") == nullptr);
    }

    TEST_CASE_METHOD(alscriptcrumbsbar_data, "alscriptcrumbsbar: only what changed is said again so a caret move says just its place", "[alscriptcrumbsbar]")
    {
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        told().errors             = 2;
        crumbs->showTrailer(doc);
        S32& said = window.services().wordsSaid;
        said      = 0;
        doc.editor->setCaret(ALTextPos(4, 0));
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("the place alone said");
        CHECK(said == 1);
        UNSCOPED_INFO("and the rest kept");
        CHECK(trailer() == std::string("Ln 5, Col 1, Tab Size: 4, 2 errors"));
        doc.editor->setSelection(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 0)));
        said = 0;
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("and the selection");
        CHECK(said == 2);

        said          = 0;
        told().errors = 3;
        crumbs->showTrailer(doc);
        {
            INFO("counted again: said again");
            CHECK(said > 2);
            REQUIRE(part("3 errors") != nullptr);
        }
        said = 0;
        doc.editor->setTabWidth(2);
        crumbs->showTrailer(doc);
        {
            INFO("indented otherwise");
            CHECK(said > 2);
            CHECK(part("Tab Size: 2") != nullptr);
        }
        said = 0;
        crumbs->setTips({ "line", "problems", "source", "expanded" });
        crumbs->showTrailer(doc);
        {
            INFO("other tips: said again");
            CHECK(said > 2);
            const Part* problems = part("3 errors");
            REQUIRE(problems != nullptr);
            CHECK(problems->toolTip == "problems");
        }
        said = 0;
        Doc& other = tab("b");
        crumbs->showTrailer(other);
        {
            INFO("another tab: its own");
            CHECK(said > 2);
            CHECK(part("3 errors") != nullptr);
            CHECK(part("Tab Size: 4") != nullptr);
        }
    }

    TEST_CASE_METHOD(alscriptcrumbsbar_data, "alscriptcrumbsbar: what is left from LSL is counted and pressable and not said when none", "[alscriptcrumbsbar]")
    {
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("none: not said");
        CHECK(trailer().find("left from LSL") == std::string::npos);
        doc.shownMigration = 3;
        crumbs->showTrailer(doc);
        const Part* left = part("3 left from LSL");
        {
            INFO("counted");
            REQUIRE(left != nullptr);
            CHECK(left->value == "migration");
            CHECK_FALSE(left->toolTip.empty());
        }
        doc.shownMigration = 1;
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("counted again");
        CHECK(part("1 left from LSL") != nullptr);
    }

    TEST_CASE_METHOD(alscriptcrumbsbar_data, "alscriptcrumbsbar: the view's tip says why a save preprocesses and warns where it will not", "[alscriptcrumbsbar]")
    {
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        doc.expandedEditor        = editor("expanded_a", "integer x;\n");
        doc.view                  = Doc::View::Expanded;
        told().wanted             = ALPreprocessor::Wanted::Directives;
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("not warned where a save preprocesses it");
        CHECK(part("Not preprocessed on save") == nullptr);
        const Part* view = part("Preprocessed");
        {
            INFO("why, on the view's tip: " + (view ? view->toolTip : std::string()));
            REQUIRE(view);
            CHECK(view->toolTip.find("it has directives") != std::string::npos);
        }
        told().wanted = ALPreprocessor::Wanted::Setting;
        crumbs->showTrailer(doc);
        {
            INFO("the setting's");
            REQUIRE(part("Preprocessed"));
            CHECK(part("Preprocessed")->toolTip.find("it has directives") == std::string::npos);
        }
        told().wanted = ALPreprocessor::Wanted::No;
        crumbs->showTrailer(doc);
        const Part* warned = part("Not preprocessed on save");
        {
            INFO("said where a save sends it as written");
            REQUIRE(warned);
            CHECK(warned->color == doc.editor->markColor(ALCodeEditor::Mark::Warning));
            CHECK(warned->toolTip.find("as written") != std::string::npos);
        }
        doc.expandedEditor = nullptr;
        doc.view = Doc::View::Source;
        crumbs->showTrailer(doc);
        UNSCOPED_INFO("nothing said with no preprocessed view");
        CHECK(part("Not preprocessed on save") == nullptr);
    }
}
