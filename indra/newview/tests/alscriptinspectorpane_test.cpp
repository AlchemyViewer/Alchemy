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

#include "../test/lltut.h"

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

namespace tut
{
    struct alscriptinspectorpane_data
    {
        al_studio_test::StudioWindowOf<FakeInspectorWindow> window;

        alscriptinspectorpane_data()
        {
            ALScriptStudioWords::Sources& sources = ALScriptStudioWords::sources();
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
            ALScriptStudioWords::sources() = ALScriptStudioWords::Sources();
            ALScriptStudioWords::forget();
            gFocusMgr.setKeyboardFocus(nullptr);
        }
        ALScriptInspectorPane* pane()
        {
            ALScriptInspectorPane* found = window.find<ALScriptInspectorPane>("inspector_pane");
            if (!found)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

    typedef test_group<alscriptinspectorpane_data> alscriptinspectorpane_group;
    typedef alscriptinspectorpane_group::object    alscriptinspectorpane_object;
    alscriptinspectorpane_group                    alscriptinspectorpane_instance("alscriptinspectorpane");

    template<> template<>
    void alscriptinspectorpane_object::test<1>()
    {
        set_test_name("a name's declaration, where it is declared as a link there, what is expected, its type as code, its words and page");
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
        ensure_equals("said", shown(),
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
        ensure("the declaration's line a link there", to && to->link && to->tooltip == "Go to the declaration");
        ensure("to the line", to->value["line"].asInteger() == 0 && to->value["column"].asInteger() == 8);
        ensure("in the script", to->value["path"].asString().empty());
        ensure("the page a link", url && url->link);
        std::set<S32> styled;
        for (const ALTextView::Style& style : inspector->view()->styles())
        {
            styled.insert(style.range.begin.line);
        }
        ensure("the declaration as code", styled.count(0) == 1);
        ensure("the type as code", styled.count(4) == 1 && styled.count(5) == 1);
        ensure("the words not", styled.count(7) == 0 && styled.count(2) == 0);

        // Followed: the window's.
        window.floater->setVisible(true);
        ALTextView* view = inspector->view();
        for (S32 y = view->getRect().getHeight() - 1; y > 0 && told().went.empty(); --y)
        {
            view->handleMouseDown(4, y, MASK_NONE);
            view->handleMouseUp(4, y, MASK_NONE);
        }
        ensure("followed", told().went.size() == 1 && told().went[0]["line"].asInteger() == 0);
    }

    template<> template<>
    void alscriptinspectorpane_object::test<2>()
    {
        set_test_name("an answer for another tab, or a place the caret has left, not shown");
        ALScriptInspectorPane* inspector = pane();
        Doc&                   doc       = tab("a");
        Doc&                   other     = tab("b");
        inspector->show("before");
        inspector->inspected(other, hover("x"), ALTextPos(0, 8));
        ensure_equals("another tab's", shown(), std::string("before"));
        inspector->inspected(doc, hover("x"), ALTextPos(1, 0));
        ensure_equals("the caret gone", shown(), std::string("before"));
        inspector->inspected(doc, hover("x"), ALTextPos(0, 8));
        ensure_equals("this one", shown(), std::string("x"));
        inspector->forget();
        ensure_equals("forgotten", shown(), std::string());
    }

    template<> template<>
    void alscriptinspectorpane_object::test<3>()
    {
        set_test_name("a word of the language's the analyzer has no words for: the definitions', and its page");
        ALScriptInspectorPane* inspector = pane();
        Doc&                   doc       = tab("a", "default { state_entry() { llSay(0, \"hi\"); } }\n");
        doc.editor->setCaret(ALTextPos(0, 28));
        inspector->inspected(doc, hover("llSay(integer channel, string msg)"), ALTextPos(0, 8));
        ensure_equals("the definitions' words and page", shown(),
                      std::string("llSay(integer channel, string msg)\n\nSays it.\nhttps://wiki.example/llSay"));
        ALScriptAnalysis::Result own = hover("llSay(integer channel, string msg)");
        own.hover.documentation      = "The analyzer's.";
        inspector->inspected(doc, own, ALTextPos(0, 8));
        ensure_equals("the analyzer's first", shown(), std::string("llSay(integer channel, string msg)\n\nThe analyzer's."));
        own.hover.link = "https://mine.example";
        inspector->inspected(doc, own, ALTextPos(0, 8));
        ensure("its own page", shown().find("\nhttps://mine.example") != std::string::npos);
        own.hover.documentation.clear();
        inspector->inspected(doc, own, ALTextPos(0, 8));
        ensure_equals("the definitions' words, its own page", shown(),
                      std::string("llSay(integer channel, string msg)\n\nSays it.\nhttps://mine.example"));
        doc.editor->setCaret(ALTextPos(0, 0));
        inspector->inspected(doc, hover("default"), ALTextPos(0, 8));
        ensure_equals("a word the definitions have not", shown(), std::string("default"));
    }

    template<> template<>
    void alscriptinspectorpane_object::test<4>()
    {
        set_test_name("declared in an include, through the expansion: said in it, and linked there");
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
        ensure_equals("in the include", shown(), std::string("integer count\nDeclared in inc.lsl on line 5."));
        const ALTextView::Substitution& to = inspector->view()->substitutions().back();
        ensure("linked there", to.value["path"].asString() == "disk:/inc.lsl" && to.value["line"].asInteger() == 4);
        result.hover.definitionLine = 3;
        inspector->inspected(doc, result, ALTextPos(0, 8));
        ensure_equals("from nothing of the source: not said", shown(), std::string("integer count"));
    }

    template<> template<>
    void alscriptinspectorpane_object::test<5>()
    {
        set_test_name("what is squiggled where the caret is, each with what it says, under the name or alone");
        ALScriptInspectorPane* inspector = pane();
        Doc&                   doc       = tab("a");
        doc.editor->setDecorations({ squiggle(0, 8, 13, "Unused."), squiggle(0, 0, 13, "Twice."), squiggle(0, 8, 13, ""),
                                     squiggle(0, 8, 13, "Lit.", ALCodeEditor::Decoration::Style::Background),
                                     squiggle(1, 0, 5, "Elsewhere.") });
        ensure_equals("each", inspector->problemsAt(doc, ALTextPos(0, 9)), std::string("Problem: Unused.\nProblem: Twice."));
        ensure_equals("none", inspector->problemsAt(doc, ALTextPos(2, 0)), std::string());
        inspector->inspected(doc, hover("integer count"), ALTextPos(0, 8));
        ensure_equals("under the name", shown(), std::string("integer count\n\nProblem: Unused.\nProblem: Twice."));
        inspector->inspected(doc, ALScriptAnalysis::Result(), ALTextPos(0, 8));
        ensure_equals("alone", shown(), std::string("Problem: Unused.\nProblem: Twice."));
    }
}
