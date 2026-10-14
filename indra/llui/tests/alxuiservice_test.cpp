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

#include "alxuiservice.h"

#include "alxuiedit.h"
#include "alxuiselection.h"
#include "../llbutton.h"
#include "../llfloater.h"
#include "../lllineeditor.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"
#include "../llxuiparser.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
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

    TEST_CASE_METHOD(alxuiservice_data, "alxuiservice: contextAt tells which part of the markup a position is in", "[alxuiservice]")
    {
        if (!ui.ok())
        {
            SKIP("no UI");
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
        CHECK(c.where == Where::AttributeName);
        CHECK(c.tag == std::string("button"));
        UNSCOPED_INFO("with name and label carried already");
        CHECK(c.present.size() == size_t(2));
        {
            INFO("under the panel under the floater");
            REQUIRE(c.parents.size() == 2);
            CHECK(c.parents[1] == "panel");
        }
        UNSCOPED_INFO("the word starts at fol, not column " << c.wordStart.column);
        CHECK(c.wordStart == ALTextPos(4, 32));
        c = ALXUIService::contextAt(doc, ALTextPos(4, 29));  // inside "Go
        CHECK(c.where == Where::AttributeValue);
        CHECK(c.attribute == std::string("label"));
        c = ALXUIService::contextAt(doc, ALTextPos(5, 7));  // <bu
        CHECK(c.where == Where::TagName);
        {
            INFO("under the panel (the button above was left open, as typing leaves it)");
            REQUIRE(c.parents.size() == 2);
            CHECK(c.parents[1] == "panel");
        }
        c = ALXUIService::contextAt(doc, ALTextPos(6, 6));  // </pa
        CHECK(c.where == Where::ClosingTag);
        c = ALXUIService::contextAt(doc, ALTextPos(2, 8));
        CHECK(c.where == Where::Comment);
        c = ALXUIService::contextAt(doc, ALTextPos(3, 0));
        CHECK(c.where == Where::Text);
    }

    TEST_CASE_METHOD(alxuiservice_data, "alxuiservice: completion offers the tags and attributes and values that fit the place", "[alxuiservice]")
    {
        if (!ui.ok())
        {
            SKIP("no UI");
        }
        ALTextDocument doc(
            "<floater name=\"f\">\n"
            "  <button name=\"b\" \n"
            "  <bu\n"
            "  </fl\n");
        std::vector<ALCodeEditor::Completion> out;
        ALXUIService::complete(doc, ALTextPos(1, 19), "", out);
        UNSCOPED_INFO("label offered: " + names(out));
        REQUIRE(offers(out, "label"));
        UNSCOPED_INFO("name, carried already, is not");
        CHECK_FALSE(offers(out, "name"));
        const auto label = std::find_if(out.begin(), out.end(), [](const ALCodeEditor::Completion& c) { return c.text == "label"; });
        UNSCOPED_INFO("as a snippet with the quotes");
        CHECK(label->snippet == "label=\"${1}\"");
        out.clear();
        ALXUIService::complete(doc, ALTextPos(1, 19), "lab", out);
        {
            INFO("narrowed to the prefix: " + names(out));
            CHECK(offers(out, "label"));
            CHECK_FALSE(offers(out, "width"));
        }
        out.clear();
        ALXUIService::complete(doc, ALTextPos(2, 5), "bu", out);
        UNSCOPED_INFO("a tag under the floater: " + names(out));
        CHECK(offers(out, "button"));
        out.clear();
        ALXUIService::complete(doc, ALTextPos(3, 6), "fl", out);
        UNSCOPED_INFO("the closing tag is the open one: " + names(out));
        CHECK(offers(out, "floater"));
        // Values, each in a text of its own, since an open quote runs on.
        ALTextDocument halign("<floater name=\"f\">\n  <button name=\"c\" halign=\"");
        out.clear();
        ALXUIService::complete(halign, ALTextPos(1, 27), "", out);
        UNSCOPED_INFO("halign's values: " + names(out));
        CHECK(offers(out, "center"));
        ALTextDocument enabled("<floater name=\"f\">\n  <button name=\"d\" enabled=\"");
        out.clear();
        ALXUIService::complete(enabled, ALTextPos(1, 28), "", out);
        {
            INFO("true and false for a boolean: " + names(out));
            CHECK(offers(out, "true"));
            CHECK(offers(out, "false"));
        }
    }

    TEST_CASE_METHOD(alxuiservice_data, "alxuiservice: hover describes a tag or attribute and parses says where it fails", "[alxuiservice]")
    {
        if (!ui.ok())
        {
            SKIP("no UI");
        }
        ALTextDocument doc("<floater name=\"f\">\n  <button name=\"b\" label=\"Go\" />\n</floater>\n");
        const std::string on_button = ALXUIService::hover(doc, ALTextPos(1, 4));
        UNSCOPED_INFO("the tag: " + on_button);
        CHECK(on_button.rfind("button", 0) == 0);
        const std::string on_label = ALXUIService::hover(doc, ALTextPos(1, 20));
        UNSCOPED_INFO("the attribute with its type: " + on_label);
        CHECK(on_label.rfind("button label:", 0) == 0);
        const std::string on_value = ALXUIService::hover(doc, ALTextPos(1, 27));
        UNSCOPED_INFO("a value says its attribute: " + on_value);
        CHECK(on_value.rfind("button label:", 0) == 0);
        UNSCOPED_INFO("nothing between elements");
        CHECK(ALXUIService::hover(doc, ALTextPos(2, 0)).empty());
        ALTextPos   where;
        std::string message;
        CHECK(ALXUIService::parses(doc.text(), where, message));
        CHECK_FALSE(ALXUIService::parses("<floater name=\"f\">\n  <button name=\"b\"\n</floater>\n", where, message));
        {
            INFO("with a message and a place: " + message);
            CHECK_FALSE(message.empty());
            CHECK(where.line >= 1);
        }
    }

    TEST_CASE_METHOD(alxuiservice_data, "alxuiservice: a whole source is set as one undo step and a line finds its element", "[alxuiservice]")
    {
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer("<floater name=\"f\">\n  <panel name=\"p\">\n    <button name=\"b\" />\n    <button name=\"b\" />\n  </panel>\n</floater>\n"));
        ALXUIEdit::path_t path;
        UNSCOPED_INFO("the second button at its line");
        REQUIRE(edit.elementAtLine(4, path));
        UNSCOPED_INFO("named with its ordinal");
        CHECK(ALXUISelection::toString(path) == ALXUISelection::toString({ "p", ALXUISelection::step("b", 1) }));
        {
            INFO("the panel from a line inside it, before the buttons");
            CHECK(edit.elementAtLine(2, path));
            REQUIRE(path.size() == 1);
            CHECK(path[0] == "p");
        }
        UNSCOPED_INFO("set as a whole");
        CHECK(edit.setSource("<floater name=\"f\">\n  <panel name=\"q\" />\n</floater>\n"));
        {
            INFO("one step");
            CHECK(edit.canUndo());
            CHECK(edit.undoDepth() == 1);
        }
        {
            INFO("and the tree is the new text's");
            CHECK(edit.resolve({ "q" }));
            CHECK_FALSE(edit.resolve({ "p" }));
        }
        {
            INFO("broken text is held but says so");
            CHECK_FALSE(edit.setSource("<floater name=\"f\">\n  <panel"));
            CHECK_FALSE(edit.error().empty());
        }
        {
            INFO("undo brings the whole back");
            CHECK(edit.undo());
            CHECK(edit.resolve({ "q" }));
        }
    }
}
