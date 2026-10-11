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

#include "../test/lltut.h"

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

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
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
        const Part* part(const std::string& begins)
        {
            static std::vector<Part> held;
            held = parts();
            for (const Part& one : held)
            {
                if (one.text.rfind(begins, 0) == 0)
                {
                    return &one;
                }
            }
            return nullptr;
        }
    };

    typedef test_group<alscriptcrumbsbar_data> alscriptcrumbsbar_group;
    typedef alscriptcrumbsbar_group::object    alscriptcrumbsbar_object;
    alscriptcrumbsbar_group                    alscriptcrumbsbar_instance("alscriptcrumbsbar");

    template<> template<>
    void alscriptcrumbsbar_object::test<1>()
    {
        set_test_name("the path the caret is in, each step offering those beside it; made again where the path, the tab or its name changed");
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        doc.editor->setCaret(ALTextPos(3, 5));
        place(crumbs, doc);
        const std::string top = "Script a [default, f, g, h, other]";
        ensure_equals("the path", path(), top + ", default [default, f, g, h, other], state_entry [state_entry, touch_start]");
        ensure_equals("kept", doc.caret->crumbPath.size(), size_t(2));
        ensure_equals("the script's step", jump()->path()[0].value, std::string("top"));
        ensure_equals("said", jump()->path()[0].toolTip,
                      std::string("Script a: click to go to the top; the arrow lists the script's declarations"));
        ensure_equals("a symbol's", jump()->path()[2].toolTip, std::string("Go to state_entry; the arrow lists other items at this level"));

        doc.editor->setCaret(ALTextPos(4, 0));
        place(crumbs, doc);
        ensure("the trailer is", trailer().find("Ln 5, Col 1") != std::string::npos);
        doc.editor->setCaret(ALTextPos(11, 0));
        place(crumbs, doc);
        ensure_equals("in f", path(), top + ", f [default, f, g, h, other]");
        doc.editor->setCaret(ALTextPos(9, 0));
        place(crumbs, doc);
        ensure_equals("between: the script", path(), top);
        doc.editor->setCaret(ALTextPos(4, 5));
        place(crumbs, doc);
        ensure("at a symbol's very end: still in it", path().find("state_entry [") != std::string::npos);
        doc.editor->setCaret(ALTextPos(13, 5));
        place(crumbs, doc);
        ensure_equals("where two meet: the later", jump()->path().back().label, std::string("h"));
        doc.editor->setCaret(ALTextPos(17, 0));
        place(crumbs, doc);
        ensure_equals("alone in its state: nothing beside it", path(), top + ", other [default, f, g, h, other], timer");

        doc.name = "Renamed";
        place(crumbs, doc);
        ensure_equals("a new name: again", jump()->path()[0].label, std::string("Renamed"));
        Doc& other = tab("b");
        other.name = "Other";
        place(crumbs, other);
        ensure_equals("not in front: nothing", jump()->path()[0].label, std::string("Renamed"));
        crumbs->forget();
        ensure("forgotten", jump()->path().empty() && jump()->trailer().empty());
        place(crumbs, doc);
        ensure_equals("and shown again whatever", jump()->path()[0].label, std::string("Renamed"));
    }

    template<> template<>
    void alscriptcrumbsbar_object::test<2>()
    {
        set_test_name("the trailer: read only, vim's word, the caret, what is selected and the problems, each pressable one with its tip; what goes first where narrow");
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        crumbs->setTips({ "line tip", "problems tip", "source tip", "expanded tip" });
        doc.editor->setCaret(ALTextPos(2, 6));
        crumbs->showTrailer(doc);
        ensure_equals("the caret and the indentation", trailer(), std::string("Ln 3, Col 7, Tab Size: 4"));
        ensure("pressed for a line", part("Ln")->value == "line" && part("Ln")->toolTip == "line tip");

        doc.modifiable  = false;
        told().banner   = "-- INSERT --";
        told().errors   = 2;
        told().warnings = 1;
        doc.editor->setSelection(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 0)));
        crumbs->showTrailer(doc);
        ensure_equals("in order", trailer(), std::string("Read-only, -- INSERT --, Ln 5, Col 1, 2 lines selected, Tab Size: 4, 2 errors, 1 warning"));
        const std::vector<Part>& raw = jump()->trailer();
        ensure("a dot between each", raw.size() == 13 && raw[1].text == "   \xC2\xB7   " && raw[11].text == raw[1].text);
        ensure_equals("the last a part", raw[12].text, std::string("1 warning"));
        ensure("the dots only between", raw[1].between && raw[11].between && !raw[12].between);
        ensure("where narrow: the indentation first, then the selection, the counts and read only last, the place never",
               part("Tab")->drop > part("2 lines")->drop && part("2 lines")->drop > part("2 errors")->drop && part("2 errors")->drop > 0 &&
                   part("Read")->drop > 0 && part("Ln")->drop == 0);
        ensure_equals("read only, said", part("Read")->toolTip, std::string("You can view this script but not change it"));
        ensure("the problems pressable", part("2 errors")->value == "problems" && part("1 warning")->toolTip == "problems tip");
        doc.editor->setSelection(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 2)));
        crumbs->showTrailer(doc);
        ensure("a line begun counts", part("3 lines") != nullptr);
        doc.editor->setText("a\tb\xC3\xA9\n");
        doc.editor->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)));
        crumbs->showTrailer(doc);
        ensure("characters, a tab one and an accented one one", part("4 characters selected") != nullptr);
        doc.editor->setCaret(ALTextPos(0, 2));
        crumbs->showTrailer(doc);
        ensure_equals("the column as seen, past the tab", part("Ln")->text,
                      "Ln 1, Col " + std::to_string(doc.editor->getTabWidth() + 1));
        doc.loaded = false;
        crumbs->showTrailer(doc);
        ensure("not loaded: not said read only, nor how it is indented", part("Read") == nullptr && part("Tab") == nullptr);
    }

    template<> template<>
    void alscriptcrumbsbar_object::test<3>()
    {
        set_test_name("its code size, coloured near and past its limit; what a save would send, past half; the view, pressable");
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
        ensure("weighed", weighed && weighed->text == "LSL (Mono) 40.0 of 64 KB used" && !weighed->color);
        ensure("before the optimizer", weighed->toolTip.find("Measured before the preprocessor") != std::string::npos);
        doc.weighing->exact = true;
        doc.weighing->sent  = true;
        doc.weighing->weight->total = 60000;
        crumbs->showTrailer(doc);
        ensure("as sent", part("LSL (Mono)")->toolTip.find("as it is saved") != std::string::npos);
        ensure("near: the warning's", part("LSL (Mono)")->color == warn);
        doc.weighing->weight->total = 70000;
        crumbs->showTrailer(doc);
        ensure("past: the error's", part("LSL (Mono)")->color == error);
        doc.weighing->weight->estimate = true;
        crumbs->showTrailer(doc);
        ensure("an estimate", part("LSL (Mono) ~") != nullptr);
        doc.weighing->weight.reset();

        const size_t LIMIT       = ALScriptEnvelope::MAX_ASSET_BYTES;
        doc.weighing->assetBytes = LIMIT / 2;
        crumbs->showTrailer(doc);
        ensure("half: nothing", parts().size() == 2);
        doc.weighing->assetBytes = LIMIT / 2 + 1;
        crumbs->showTrailer(doc);
        ensure("past half", part(std::to_string((LIMIT / 2 + 1 + 1023) / 1024) + " KB") != nullptr);
        ensure("said", parts().back().toolTip.find("Save size") != std::string::npos && !parts().back().color);
        doc.weighing->assetBytes = LIMIT * 9 / 10 + 1;
        crumbs->showTrailer(doc);
        ensure("near: the warning's", parts().back().color == warn);
        doc.weighing->assetBytes = LIMIT + 1;
        crumbs->showTrailer(doc);
        ensure("over: the error's", parts().back().color == error);
        ensure("and by how much", parts().back().toolTip.find("1 over the") != std::string::npos);
        doc.notecard             = true;
        doc.weighing->assetBytes = static_cast<size_t>(LLNotecard::MAX_SIZE) / 2 + 1;
        crumbs->showTrailer(doc);
        ensure("a notecard's by its own limit", parts().back().toolTip.find("Notecard text") != std::string::npos);
        doc.notecard             = false;
        doc.weighing->assetBytes = 0;

        crumbs->setTips({ "line tip", "problems tip", "source tip", "expanded tip" });
        doc.expandedEditor = editor("expanded_a", "integer x;\n");
        crumbs->showTrailer(doc);
        const Part  shown = parts().back();
        ensure("the source in front", shown.text == "Source" && shown.value == "expanded" && shown.toolTip == "source tip");
        doc.view = Doc::View::Expanded;
        doc.expandedEditor->setCaret(ALTextPos(0, 3));
        crumbs->showTrailer(doc);
        ensure("the expansion", parts().back().text == "Preprocessed" && parts().back().toolTip == "expanded tip");
        ensure("its own caret", part("Ln 1, Col 4") != nullptr);
    }

    template<> template<>
    void alscriptcrumbsbar_object::test<4>()
    {
        set_test_name("what the optimizer made of it, in place of its weight, while the preprocessed view is in front");
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
        ensure_equals("before and after", part("LSL (Mono)")->text, std::string("LSL (Mono) ~20.0 \xE2\x86\x92 ~10.0 of 64 KB used"));
        ensure_equals("once", parts().size(), size_t(4));
        doc.uploaded.codeAfter = 60000;
        crumbs->showTrailer(doc);
        ensure("near: the warning's", part("LSL (Mono)")->color == doc.editor->markColor(ALCodeEditor::Mark::Warning));
        doc.uploaded.codeAfter = 70000;
        crumbs->showTrailer(doc);
        ensure("past: the error's", part("LSL (Mono)")->color == doc.editor->markColor(ALCodeEditor::Mark::Error));
        told().target = ALScriptWeight::Target::LSO;
        crumbs->showTrailer(doc);
        ensure("measured, not estimated", part("LSL (LSO)") && part("LSL (LSO)")->text.find('~') == std::string::npos);
        doc.editor->insertText("x");
        crumbs->showTrailer(doc);
        ensure("typed since: its weight", part("LSL (Mono) 40.0") != nullptr);
        doc.view             = Doc::View::Source;
        doc.uploaded.version = doc.editor->document().version();
        told().target        = ALScriptWeight::Target::Mono;
        crumbs->showTrailer(doc);
        ensure("the source in front: its weight", part("LSL (Mono) 40.0") != nullptr);
    }

    template<> template<>
    void alscriptcrumbsbar_object::test<5>()
    {
        set_test_name("a step chosen: the script's top as a place, a symbol's name as a stretch, one gone as nowhere");
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        crumbs->choose("top");
        crumbs->choose(ALScriptPlaces::outlineValue(doc, 1));
        crumbs->choose("9\ngone");
        doc.outline.erase(doc.outline.begin());
        crumbs->choose("1\ntouch_start");
        ensure_equals("each", joined(told().chosen), std::string("a 0:0-0:0, a 2:4-2:15, a nowhere, a 5:4-5:15"));
    }

    template<> template<>
    void alscriptcrumbsbar_object::test<6>()
    {
        set_test_name("how the script is indented: a tab's width or a level's spaces, pressable, the tip saying where it was said; the menu's choices for that script alone");
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        crumbs->showTrailer(doc);
        ensure("tabs, by default", part("Tab Size: 4") && part("Tab Size")->value == "indent");
        ensure("the default's tip", part("Tab Size")->toolTip.find("Default indentation from Preferences") != std::string::npos);
        ensure("checked as it is", crumbs->indentChecked("tabs") && crumbs->indentChecked("width_4") && !crumbs->indentChecked("spaces"));
        ensure("nothing chosen to forget", !crumbs->indentEnabled("read"));
        doc.editor->setReadsIndentation(true);
        doc.editor->setText("a\n  b\n");
        crumbs->showTrailer(doc);
        ensure("read from the script", part("Spaces: 2") && part("Spaces")->toolTip.find("detected from this script's lines") != std::string::npos);

        crumbs->indentAct("width_4");
        ensure("a width chosen", part("Spaces: 4") && part("Spaces")->toolTip.find("set for this script") != std::string::npos);
        ensure("and checked", crumbs->indentChecked("width_4") && !crumbs->indentChecked("width_2"));
        ensure("the text untouched", doc.editor->text() == "a\n  b\n");
        crumbs->indentAct("tabs");
        ensure("tabs chosen", part("Tab Size: 4") != nullptr && crumbs->indentChecked("tabs"));
        ensure("to be forgotten", crumbs->indentEnabled("read"));
        crumbs->indentAct("read");
        ensure("the script's own again", part("Spaces: 2") != nullptr);

        crumbs->indentAct("convert_tabs");
        ensure_equals("converted, a tab of the width", doc.editor->text(), std::string("a\n\tb\n"));
        ensure("and tabs from here on", part("Tab Size: 2") != nullptr);
        doc.modifiable = false;
        ensure("not a script that may only be read", !crumbs->indentEnabled("convert_spaces"));
        crumbs->indentAct("convert_spaces");
        ensure_equals("left as it was", doc.editor->text(), std::string("a\n\tb\n"));
    }

    template<> template<>
    void alscriptcrumbsbar_object::test<7>()
    {
        set_test_name("a check out a while is said, and pressed shows the problems; one just asked, or answered, is not");
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        doc.loaded                 = true;
        const F64 now              = LLTimer::getTotalSeconds();
        doc.check->requestedVersion = doc.editor->document().version();
        doc.check->analysisVersion  = doc.check->requestedVersion + 1;
        doc.check->askedAt          = now;
        crumbs->showTrailer(doc);
        ensure("just asked: nothing", part("Checking") == nullptr);
        doc.check->askedAt = now - 1.0;
        crumbs->showTrailer(doc);
        const Part* checking = part("Checking");
        ensure("out a while: said", checking != nullptr && checking->value == "problems");
        doc.check->analysisVersion = doc.check->requestedVersion;
        crumbs->showTrailer(doc);
        ensure("answered: gone", part("Checking") == nullptr);
    }

    template<> template<>
    void alscriptcrumbsbar_object::test<8>()
    {
        set_test_name("a caret moving says its place and the selection again, and nothing else; the rest said again where what it says changed");
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        told().errors             = 2;
        crumbs->showTrailer(doc);
        S32& said = window.services().wordsSaid;
        said      = 0;
        doc.editor->setCaret(ALTextPos(4, 0));
        crumbs->showTrailer(doc);
        ensure_equals("the place alone said", said, 1);
        ensure_equals("and the rest kept", trailer(), std::string("Ln 5, Col 1, Tab Size: 4, 2 errors"));
        doc.editor->setSelection(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 0)));
        said = 0;
        crumbs->showTrailer(doc);
        ensure_equals("and the selection", said, 2);

        said          = 0;
        told().errors = 3;
        crumbs->showTrailer(doc);
        ensure("counted again: said again", said > 2 && part("3 errors") != nullptr);
        said = 0;
        doc.editor->setTabWidth(2);
        crumbs->showTrailer(doc);
        ensure("indented otherwise", said > 2 && part("Tab Size: 2") != nullptr);
        said = 0;
        crumbs->setTips({ "line", "problems", "source", "expanded" });
        crumbs->showTrailer(doc);
        ensure("other tips: said again", said > 2 && part("3 errors")->toolTip == "problems");
        said = 0;
        Doc& other = tab("b");
        crumbs->showTrailer(other);
        ensure("another tab: its own", said > 2 && part("3 errors") != nullptr && part("Tab Size: 4") != nullptr);
    }

    template<> template<>
    void alscriptcrumbsbar_object::test<9>()
    {
        set_test_name("what is left from LSL counted, pressed to show it; none, not said");
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        crumbs->showTrailer(doc);
        ensure("none: not said", trailer().find("left from LSL") == std::string::npos);
        doc.shownMigration = 3;
        crumbs->showTrailer(doc);
        const Part* left = part("3 left from LSL");
        ensure("counted", left != nullptr && left->value == "migration" && !left->toolTip.empty());
        doc.shownMigration = 1;
        crumbs->showTrailer(doc);
        ensure("counted again", part("1 left from LSL") != nullptr);
    }

    template<> template<>
    void alscriptcrumbsbar_object::test<10>()
    {
        set_test_name("the view's tip says why a save preprocesses the script; a preprocessed view of one a save sends as written says so, in the warning's colour");
        ALScriptCrumbsBar* crumbs = bar();
        Doc&               doc    = tab("a");
        doc.expandedEditor        = editor("expanded_a", "integer x;\n");
        doc.view                  = Doc::View::Expanded;
        told().wanted             = ALPreprocessor::Wanted::Directives;
        crumbs->showTrailer(doc);
        ensure("not warned where a save preprocesses it", part("Not preprocessed on save") == nullptr);
        const Part* view = part("Preprocessed");
        ensure("why, on the view's tip: " + (view ? view->toolTip : std::string()), view && view->toolTip.find("it has directives") != std::string::npos);
        told().wanted = ALPreprocessor::Wanted::Enveloped;
        crumbs->showTrailer(doc);
        ensure("saved with it before", part("Preprocessed") && part("Preprocessed")->toolTip.find("saved with the preprocessor before") != std::string::npos);
        told().wanted = ALPreprocessor::Wanted::No;
        crumbs->showTrailer(doc);
        const Part* warned = part("Not preprocessed on save");
        ensure("said where a save sends it as written", warned && warned->color == doc.editor->markColor(ALCodeEditor::Mark::Warning) &&
                                                             warned->toolTip.find("as written") != std::string::npos);
        doc.expandedEditor = nullptr;
        doc.view = Doc::View::Source;
        crumbs->showTrailer(doc);
        ensure("nothing said with no preprocessed view", part("Not preprocessed on save") == nullptr);
    }
}
