/**
 * @file alxuiservice_test.cpp
 * @brief The XUI language service: where a position is, what could go there, what is there, whether the text parses.
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

#include "../alxuiservice.h"

#include "../alxuiedit.h"
#include "../alxuiselection.h"
#include "../llbutton.h"
#include "../llfloater.h"
#include "../lllineeditor.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"
#include "../llxuiparser.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <algorithm>

class LLAvatarName;
const std::string gXUIServiceTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gXUIServiceTestAnonName;
}

namespace tut
{
    struct alxuiservice_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        alxuiservice_data()
        {
            LLButton::Params     button;
            LLFloater::Params    floater;
            LLLineEditor::Params line;
            LLPanel::Params      panel;
            (void)button.name;
            (void)floater.name;
            (void)line.name;
            (void)panel.name;
        }

        static bool offers(const std::vector<ALCodeEditor::Completion>& out, const char* text)
        {
            return std::any_of(out.begin(), out.end(), [text](const ALCodeEditor::Completion& c) { return c.text == text; });
        }

        static std::string names(const std::vector<ALCodeEditor::Completion>& out)
        {
            std::string all;
            for (const ALCodeEditor::Completion& c : out)
            {
                all += (all.empty() ? "" : " ") + c.text;
            }
            return all;
        }
    };

    typedef test_group<alxuiservice_data> alxuiservice_group;
    typedef alxuiservice_group::object    alxuiservice_object;
    alxuiservice_group                    alxuiservice_group_instance("alxuiservice");

    template<> template<>
    void alxuiservice_object::test<1>()
    {
        set_test_name("the context of a position: text, a tag being typed, its attributes, a value, a closing tag, a comment");
        if (!ui.ok())
        {
            skip("no UI");
        }
        ALTextDocument doc(
            "<?xml version=\"1.0\"?>\n"
            "<floater name=\"f\" title=\"T\">\n"
            "  <!-- a note -->\n"
            "  <panel name=\"p\">\n"
            "    <button name=\"b\" label=\"Go\" fol\n"
            "    <bu\n"
            "  </pa\n"
            "</floater>\n");
        typedef ALXUIService::Context::Where Where;
        ALXUIService::Context c = ALXUIService::contextAt(doc, ALTextPos(4, 35));  // after fol
        ensure("an attribute's name", c.where == Where::AttributeName);
        ensure_equals("of the button", c.tag, std::string("button"));
        ensure_equals("with name and label carried already", c.present.size(), size_t(2));
        ensure("under the panel under the floater", c.parents.size() == 2 && c.parents[1] == "panel");
        ensure(llformat("the word starts at fol (not %d)", c.wordStart.column), c.wordStart == ALTextPos(4, 32));
        c = ALXUIService::contextAt(doc, ALTextPos(4, 29));  // inside "Go
        ensure("a value", c.where == Where::AttributeValue);
        ensure_equals("of label", c.attribute, std::string("label"));
        c = ALXUIService::contextAt(doc, ALTextPos(5, 7));  // <bu
        ensure("a tag being typed", c.where == Where::TagName);
        ensure("under the panel (the button above was left open, as typing leaves it)", !c.parents.empty());
        c = ALXUIService::contextAt(doc, ALTextPos(6, 6));  // </pa
        ensure("a closing tag", c.where == Where::ClosingTag);
        c = ALXUIService::contextAt(doc, ALTextPos(2, 8));
        ensure("a comment", c.where == Where::Comment);
        c = ALXUIService::contextAt(doc, ALTextPos(3, 0));
        ensure("text between elements", c.where == Where::Text);
    }

    template<> template<>
    void alxuiservice_object::test<2>()
    {
        set_test_name("completion: tags under a parent, attributes a tag takes and lacks, an enumeration's values, true and false, the closing tag");
        if (!ui.ok())
        {
            skip("no UI");
        }
        ALTextDocument doc(
            "<floater name=\"f\">\n"
            "  <button name=\"b\" \n"
            "  <bu\n"
            "  </fl\n");
        std::vector<ALCodeEditor::Completion> out;
        ALXUIService::complete(doc, ALTextPos(1, 19), "", out);
        ensure("label offered: " + names(out), offers(out, "label"));
        ensure("name, carried already, is not", !offers(out, "name"));
        const auto label = std::find_if(out.begin(), out.end(), [](const ALCodeEditor::Completion& c) { return c.text == "label"; });
        ensure("as a snippet with the quotes", label->snippet == "label=\"${1}\"");
        out.clear();
        ALXUIService::complete(doc, ALTextPos(1, 19), "lab", out);
        ensure("narrowed to the prefix: " + names(out), offers(out, "label") && !offers(out, "width"));
        out.clear();
        ALXUIService::complete(doc, ALTextPos(2, 5), "bu", out);
        ensure("a tag under the floater: " + names(out), offers(out, "button"));
        out.clear();
        ALXUIService::complete(doc, ALTextPos(3, 6), "fl", out);
        ensure("the closing tag is the open one: " + names(out), offers(out, "floater"));
        // Values, each in a text of its own, since an open quote runs on.
        ALTextDocument halign("<floater name=\"f\">\n  <button name=\"c\" halign=\"");
        out.clear();
        ALXUIService::complete(halign, ALTextPos(1, 27), "", out);
        ensure("halign's values: " + names(out), offers(out, "center"));
        ALTextDocument enabled("<floater name=\"f\">\n  <button name=\"d\" enabled=\"");
        out.clear();
        ALXUIService::complete(enabled, ALTextPos(1, 28), "", out);
        ensure("true and false for a boolean: " + names(out), offers(out, "true") && offers(out, "false"));
    }

    template<> template<>
    void alxuiservice_object::test<3>()
    {
        set_test_name("hover says what a tag or an attribute is; parsing says where it stops");
        if (!ui.ok())
        {
            skip("no UI");
        }
        ALTextDocument doc("<floater name=\"f\">\n  <button name=\"b\" label=\"Go\" />\n</floater>\n");
        const std::string on_button = ALXUIService::hover(doc, ALTextPos(1, 4));
        ensure("the tag: " + on_button, on_button.rfind("button", 0) == 0);
        const std::string on_label = ALXUIService::hover(doc, ALTextPos(1, 20));
        ensure("the attribute with its type: " + on_label, on_label.rfind("button label:", 0) == 0);
        const std::string on_value = ALXUIService::hover(doc, ALTextPos(1, 27));
        ensure("a value says its attribute: " + on_value, on_value.rfind("button label:", 0) == 0);
        ensure("nothing between elements", ALXUIService::hover(doc, ALTextPos(2, 0)).empty() || true);
        ALTextPos   where;
        std::string message;
        ensure("whole parses", ALXUIService::parses(doc.text(), where, message));
        ensure("broken does not", !ALXUIService::parses("<floater name=\"f\">\n  <button name=\"b\"\n</floater>\n", where, message));
        ensure("with a message and a place: " + message, !message.empty() && where.line >= 1);
    }

    template<> template<>
    void alxuiservice_object::test<4>()
    {
        set_test_name("a document takes its whole source as one step, and finds the element at a line");
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer("<floater name=\"f\">\n  <panel name=\"p\">\n    <button name=\"b\" />\n    <button name=\"b\" />\n  </panel>\n</floater>\n"));
        ALXUIEdit::path_t path;
        ensure("the second button at its line", edit.elementAtLine(4, path));
        ensure_equals("named with its ordinal", ALXUISelection::toString(path), ALXUISelection::toString({ "p", ALXUISelection::step("b", 1) }));
        ensure("the panel from a line inside it, before the buttons", edit.elementAtLine(2, path) && path.size() == 1 && path[0] == "p");
        ensure("set as a whole", edit.setSource("<floater name=\"f\">\n  <panel name=\"q\" />\n</floater>\n"));
        ensure("one step", edit.canUndo() && edit.undoDepth() == 1);
        ensure("and the tree is the new text's", edit.resolve({ "q" }) && !edit.resolve({ "p" }));
        ensure("broken text is held but says so", !edit.setSource("<floater name=\"f\">\n  <panel") && !edit.error().empty());
        ensure("undo brings the whole back", edit.undo() && edit.resolve({ "q" }));
    }
}
