/**
 * @file alscriptinspectorpane_test.cpp
 * @brief Script Studio's inspector over the studio's own window, the window's side faked.
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

#include "../alscriptinspectorpane.h"

#include "../alscriptstudiocaret.h"
#include "../alscriptstudiowords.h"
#include "alcodeeditor.h"
#include "alscriptstudio_fixture.h"
#include "altextview.h"
#include "llfocusmgr.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;

    // The window's side of the inspector, faked.
    struct FakeInspectorWindow : public ALScriptInspectorPane::Window
    {
        void goToDeclared(const LLSD& value) override { went.push_back(value); }
        bool preprocessed(const Doc&) const override { return expanded; }

        std::vector<LLSD> went;
        bool              expanded = false;
    };

    ALScriptAnalysis::Result hover(const std::string& label)
    {
        ALScriptAnalysis::Result result;
        result.hover.found = true;
        result.hover.label = label;
        return result;
    }
    ALCodeEditor::Decoration squiggle(S32 line, S32 from, S32 to, const std::string& message,
                                      ALCodeEditor::Decoration::Style style = ALCodeEditor::Decoration::Style::Squiggle)
    {
        ALCodeEditor::Decoration out;
        out.range   = ALTextRange(ALTextPos(line, from), ALTextPos(line, to));
        out.style   = style;
        out.message = message;
        return out;
    }
}

namespace
{
    struct alscriptinspectorpane_data
    {
        // The viewer the units ask, attached first and let go of last.
        al_studio_test::StudioViewer viewer;
        al_studio_test::StudioWindowOf<FakeInspectorWindow> window;

        alscriptinspectorpane_data()
        {
            auto& sources = viewer.slots;
            sources.keywords                      = [](bool) {
                LLSD keywords;
                keywords["functions"]["llSay"] = LLSD().with("return", "").with("tooltip", "Says it.");
                return keywords;
            };
            sources.lslHelpUrl = [] { return std::string("https://wiki.example/[LSL_STRING]"); };
            ALScriptStudioWords::forget();
        }
        ~alscriptinspectorpane_data()
        {
            ALScriptStudioWords::forget();
            gFocusMgr.setKeyboardFocus(nullptr);
        }
        ALScriptInspectorPane* pane()
        {
            ALScriptInspectorPane* found = window.find<ALScriptInspectorPane>("inspector_pane");
            if (!found)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            return found;
        }
        FakeInspectorWindow& told() { return window.pane(); }
        // A tab, its caret on `count`, asked about there.
        Doc& tab(const std::string& id, const std::string& text = "integer count;\ndefault { state_entry() { count = 1; } }\n")
        {
            pane();
            Doc& doc       = window.services().addDoc(id);
            doc.loaded     = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name     = "editor_" + id;
            p.rect     = LLRect(0, 200, 400, 0);
            doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(doc.editor);
            doc.editor->setText(text);
            doc.editor->setCaret(ALTextPos(0, 10));
            doc.caret->inspectAt = ALTextPos(0, 8);
            return doc;
        }
        std::string shown() { return pane()->view()->document().text(); }
    };

    TEST_CASE_METHOD(alscriptinspectorpane_data, "alscriptinspectorpane: a hover shows its declaration as a link and its type as code and page", "[alscriptinspectorpane]")
    {
        ALScriptInspectorPane*   inspector = pane();
        Doc&                     doc       = tab("a");
        ALScriptAnalysis::Result result    = hover("integer count");
        result.hover.hasDefinition         = true;
        result.hover.definitionLine        = 0;
        result.hover.definitionColumn      = 8;
        result.hover.expected              = "float";
        result.hover.typeDetail            = "integer\n  count";
        result.hover.documentation         = "How many.";
        result.hover.link                  = "https://example.com/count";
        inspector->inspected(doc, result, ALTextPos(0, 8));
        CHECK(shown() ==
                      std::string("integer count\nDeclared on line 1.\nExpected here: float\n\ninteger\n  count\n\nHow many.\n"
                                  "https://example.com/count"));
        const std::vector<ALTextView::Substitution>& links = inspector->view()->substitutions();
        const ALTextView::Substitution*              to    = nullptr;
        const ALTextView::Substitution*              url   = nullptr;
        for (const ALTextView::Substitution& link : links)
        {
            to  = link.range.begin == ALTextPos(1, 0) ? &link : to;
            url = link.range.begin == ALTextPos(8, 0) ? &link : url;
        }
        {
            INFO("the declaration's line a link there");
            REQUIRE(to);
            REQUIRE(to->link);
            CHECK(to->tooltip == "Go to the declaration");
        }
        {
            INFO("to the line");
            CHECK(to->value["line"].asInteger() == 0);
            CHECK(to->value["column"].asInteger() == 8);
        }
        UNSCOPED_INFO("in the script");
        REQUIRE(to->value["path"].asString().empty());
        {
            INFO("the page a link");
            REQUIRE(url);
            CHECK(url->link);
        }
        std::set<S32> styled;
        for (const ALTextView::Style& style : inspector->view()->styles())
        {
            styled.insert(style.range.begin.line);
        }
        UNSCOPED_INFO("the declaration as code");
        REQUIRE(styled.count(0) == 1);
        {
            INFO("the type as code");
            REQUIRE(styled.count(4) == 1);
            REQUIRE(styled.count(5) == 1);
        }
        {
            INFO("the words not");
            REQUIRE(styled.count(7) == 0);
            REQUIRE(styled.count(2) == 0);
        }

        // Followed: the window's.
        window.floater->setVisible(true);
        ALTextView* view = inspector->view();
        for (S32 y = view->getRect().getHeight() - 1; y > 0 && told().went.empty(); --y)
        {
            view->handleMouseDown(4, y, MASK_NONE);
            view->handleMouseUp(4, y, MASK_NONE);
        }
        {
            INFO("followed");
            REQUIRE(told().went.size() == 1);
            CHECK(told().went[0]["line"].asInteger() == 0);
        }
    }

    TEST_CASE_METHOD(alscriptinspectorpane_data, "alscriptinspectorpane: an answer for another tab or a place the caret has left is not shown", "[alscriptinspectorpane]")
    {
        ALScriptInspectorPane* inspector = pane();
        Doc&                   doc       = tab("a");
        Doc&                   other     = tab("b");
        inspector->show("before");
        inspector->inspected(other, hover("x"), ALTextPos(0, 8));
        UNSCOPED_INFO("another tab's");
        CHECK(shown() == std::string("before"));
        inspector->inspected(doc, hover("x"), ALTextPos(1, 0));
        UNSCOPED_INFO("the caret gone");
        CHECK(shown() == std::string("before"));
        inspector->inspected(doc, hover("x"), ALTextPos(0, 8));
        UNSCOPED_INFO("this one");
        CHECK(shown() == std::string("x"));
        inspector->forget();
        UNSCOPED_INFO("forgotten");
        CHECK(shown() == std::string());
    }

    TEST_CASE_METHOD(alscriptinspectorpane_data, "alscriptinspectorpane: a built-in the analyzer has no words for shows the definitions' words", "[alscriptinspectorpane]")
    {
        ALScriptInspectorPane* inspector = pane();
        Doc&                   doc       = tab("a", "default { state_entry() { llSay(0, \"hi\"); } }\n");
        doc.editor->setCaret(ALTextPos(0, 28));
        inspector->inspected(doc, hover("llSay(integer channel, string msg)"), ALTextPos(0, 8));
        UNSCOPED_INFO("the definitions' words and page");
        CHECK(shown() ==
                      std::string("llSay(integer channel, string msg)\n\nSays it.\nhttps://wiki.example/llSay"));
        ALScriptAnalysis::Result own = hover("llSay(integer channel, string msg)");
        own.hover.documentation      = "The analyzer's.";
        inspector->inspected(doc, own, ALTextPos(0, 8));
        UNSCOPED_INFO("the analyzer's first");
        CHECK(shown() == std::string("llSay(integer channel, string msg)\n\nThe analyzer's."));
        own.hover.link = "https://mine.example";
        inspector->inspected(doc, own, ALTextPos(0, 8));
        UNSCOPED_INFO("its own page");
        REQUIRE(shown().find("\nhttps://mine.example") != std::string::npos);
        own.hover.documentation.clear();
        inspector->inspected(doc, own, ALTextPos(0, 8));
        UNSCOPED_INFO("the definitions' words, its own page");
        CHECK(shown() ==
                      std::string("llSay(integer channel, string msg)\n\nSays it.\nhttps://mine.example"));
        doc.editor->setCaret(ALTextPos(0, 0));
        inspector->inspected(doc, hover("default"), ALTextPos(0, 8));
        UNSCOPED_INFO("a word the definitions have not");
        CHECK(shown() == std::string("default"));
    }

    TEST_CASE_METHOD(alscriptinspectorpane_data, "alscriptinspectorpane: a name declared in an include is said to be there and linked there", "[alscriptinspectorpane]")
    {
        ALScriptInspectorPane* inspector = pane();
        Doc&                   doc       = tab("a");
        told().expanded                  = true;
        doc.expanded.map.addFile("a", "object:a");
        doc.expanded.map.addFile("inc.lsl", "disk:/inc.lsl");
        ALSourceMap::Segment segment;
        segment.outLine = 0;
        segment.length  = 20;
        segment.file    = 1;
        segment.line    = 4;
        doc.expanded.map.add(segment);
        doc.expanded.map.finish();
        ALScriptAnalysis::Result result = hover("integer count");
        result.hover.hasDefinition      = true;
        result.hover.definitionLine     = 0;
        result.hover.definitionColumn   = 8;
        inspector->inspected(doc, result, ALTextPos(0, 8));
        UNSCOPED_INFO("in the include");
        CHECK(shown() == std::string("integer count\nDeclared in inc.lsl on line 5."));
        const std::vector<ALTextView::Substitution>& to_include = inspector->view()->substitutions();
        REQUIRE_FALSE(to_include.empty());
        const ALTextView::Substitution& to = to_include.back();
        {
            INFO("linked there");
            CHECK(to.value["path"].asString() == "disk:/inc.lsl");
            CHECK(to.value["line"].asInteger() == 4);
        }
        result.hover.definitionLine = 3;
        inspector->inspected(doc, result, ALTextPos(0, 8));
        UNSCOPED_INFO("from nothing of the source: not said");
        CHECK(shown() == std::string("integer count"));
    }

    TEST_CASE_METHOD(alscriptinspectorpane_data, "alscriptinspectorpane: the squiggles under the caret are listed under the hover or alone", "[alscriptinspectorpane]")
    {
        ALScriptInspectorPane* inspector = pane();
        Doc&                   doc       = tab("a");
        doc.editor->setDecorations({ squiggle(0, 8, 13, "Unused."), squiggle(0, 0, 13, "Twice."), squiggle(0, 8, 13, ""),
                                     squiggle(0, 8, 13, "Lit.", ALCodeEditor::Decoration::Style::Background),
                                     squiggle(1, 0, 5, "Elsewhere.") });
        UNSCOPED_INFO("each squiggle under the caret that says something");
        CHECK(inspector->problemsAt(doc, ALTextPos(0, 9)) == std::string("Problem: Unused.\nProblem: Twice."));
        CHECK(inspector->problemsAt(doc, ALTextPos(2, 0)) == std::string());
        inspector->inspected(doc, hover("integer count"), ALTextPos(0, 8));
        UNSCOPED_INFO("under the name");
        CHECK(shown() == std::string("integer count\n\nProblem: Unused.\nProblem: Twice."));
        inspector->inspected(doc, ALScriptAnalysis::Result(), ALTextPos(0, 8));
        UNSCOPED_INFO("alone");
        CHECK(shown() == std::string("Problem: Unused.\nProblem: Twice."));
    }

    TEST_CASE_METHOD(alscriptinspectorpane_data, "alscriptinspectorpane: a name from a required module is said to be in its file and linked there", "[alscriptinspectorpane]")
    {
        ALScriptInspectorPane* inspector = pane();
        Doc&                   doc       = tab("a", "local util = require(\"util\")\nprint(util.twice(2))\n");
        told().expanded                  = true;
        // The module's own map, over the same files as the script's.
        ALSourceMap module;
        module.addFile("a", "object:a");
        module.addFile("util.luau", "disk:/lib/util.luau");
        ALSourceMap::Segment segment;
        segment.outLine = 0;
        segment.length  = 40;
        segment.file    = 1;
        segment.line    = 0;
        for (S32 line = 0; line < 6; ++line)
        {
            segment.outLine = segment.line = line;
            module.add(segment);
        }
        module.finish();
        doc.expanded.moduleMaps.emplace_back("disk:/lib/util.luau", module);
        ALScriptAnalysis::Result result = hover("function M.twice(n: number): number");
        result.hover.hasDefinition      = true;
        result.hover.definitionLine     = 3;
        result.hover.definitionColumn   = 11;
        result.hover.definitionFile     = "disk:/lib/util.luau";
        inspector->inspected(doc, result, ALTextPos(0, 8));
        UNSCOPED_INFO("in the module");
        CHECK(shown() == std::string("function M.twice(n: number): number\nDeclared in util.luau on line 4."));
        const std::vector<ALTextView::Substitution>& to_module = inspector->view()->substitutions();
        REQUIRE_FALSE(to_module.empty());
        const ALTextView::Substitution& to = to_module.back();
        {
            INFO("linked there");
            CHECK(to.value["path"].asString() == "disk:/lib/util.luau");
            CHECK(to.value["line"].asInteger() == 3);
            CHECK(to.value["column"].asInteger() == 11);
        }
        result.hover.definitionFile = "disk:/lib/gone.luau";
        inspector->inspected(doc, result, ALTextPos(0, 8));
        UNSCOPED_INFO("a module the expansion has not: not said");
        CHECK(shown() == std::string("function M.twice(n: number): number"));
        result.hover.definitionFile = "disk:/lib/util.luau";
        told().expanded             = false;
        inspector->inspected(doc, result, ALTextPos(0, 8));
        UNSCOPED_INFO("nor where the script is not expanded");
        CHECK(shown() == std::string("function M.twice(n: number): number"));
    }
}
