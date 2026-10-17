/**
 * @file alxuiedit_test.cpp
 * @brief Every authored form moves by the delta it was given, and every byte the edit did not name is the byte that was there.
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

#include "alxuiedit.h"

#include "alxuiselection.h"

#include "alxuicatalog.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "lldir.h"
#include "llxmlnode.h"

#include <iostream>

#include <catch2/catch_test_macros.hpp>

namespace
{
    // Every element here is a panel: a widget that draws text cannot be
    // built in this fixture, and the layout rule this tests is LLView's
    // and the same for every tag.
    struct alxuiedit_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static constexpr S32 PARENT_WIDTH = 200;
        static constexpr S32 PARENT_HEIGHT = 100;

        struct TestPanel : public LLPanel
        {
            TestPanel(const LLPanel::Params& p) : LLPanel(p) {}
        };

        // One build of a file's text, and the rects it produced.
        struct Build
        {
            LLPanel*        panel = nullptr;
            LLXMLNodePtr    node;

            ~Build() { delete panel; }

            bool run(const std::string& xml)
            {
                if (!LLXMLNode::parseBuffer(xml.data(), xml.size(), node))
                {
                    return false;
                }
                LLPanel::Params params(LLUICtrlFactory::getDefaultParams<LLPanel>());
                params.rect = LLRect(0, PARENT_HEIGHT, PARENT_WIDTH, 0);
                panel = LLUICtrlFactory::create<TestPanel>(params);
                LLUICtrlFactory::instance().pushFileName("edit_test.xml");
                const bool ok = panel->initPanelXML(node, nullptr, LLUICtrlFactory::getDefaultParams<LLPanel>());
                LLUICtrlFactory::instance().popFileName();
                return ok;
            }

            LLRect rectOf(const std::string& name) const
            {
                LLView* view = panel ? panel->findChild<LLView>(name, true) : nullptr;
                return view ? view->getRect() : LLRect();
            }
        };

        // A root of a known size with a sibling before the element under
        // test, so that the forms measured from the last child built have
        // something to be measured from.
        static std::string file(const std::string& layout, const std::string& target)
        {
            return "<panel name=\"root\" layout=\"" + layout + "\""
                   " width=\"" + std::to_string(PARENT_WIDTH) + "\""
                   " height=\"" + std::to_string(PARENT_HEIGHT) + "\">\n"
                   "    <panel name=\"first\" left=\"10\" top=\"10\" width=\"30\" height=\"20\"/>\n"
                   "    <panel name=\"target\" " + target + "/>\n"
                   "</panel>\n";
        }

        static ALXUIEdit::Anchor anchorOf(const LLRect& rect, bool top_left)
        {
            ALXUIEdit::Anchor now;
            now.left = rect.mLeft;
            now.top = PARENT_HEIGHT - rect.mTop;
            now.bottom = rect.mBottom;
            now.width = rect.getWidth();
            now.height = rect.getHeight();
            now.topLeft = top_left;
            return now;
        }

        static std::string describe(const LLRect& rect)
        {
            return "(" + std::to_string(rect.mLeft) + "," + std::to_string(rect.mBottom)
                 + " " + std::to_string(rect.getWidth()) + "x" + std::to_string(rect.getHeight()) + ")";
        }

        // The file as someone wrote it: a declaration, a comment, an
        // entity, one attribute per line and CRLF throughout.
        static std::string raw()
        {
            return "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\" ?>\r\n"
                   "<!-- what the round trip has to keep -->\r\n"
                   "<panel\r\n"
                   " name=\"root\"\r\n"
                   " layout=\"topleft\"\r\n"
                   " label=\"Tom &amp; Jerry\"\r\n"
                   " width=\"200\"\r\n"
                   " height=\"100\">\r\n"
                   "    <panel\r\n"
                   "     name=\"target\"\r\n"
                   "     left=\"20\"\r\n"
                   "     top=\"10\"\r\n"
                   "     width=\"40\"\r\n"
                   "     height=\"20\" />\r\n"
                   "</panel>\r\n";
        }

        static std::string replaced(const std::string& text, const std::string& from, const std::string& to)
        {
            const size_t at = text.find(from);
            return at == std::string::npos ? text : text.substr(0, at) + to + text.substr(at + from.size());
        }
    };

    // The property: whichever form the file used, the element ends up
    // exactly the delta away from where it was.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: translate moves an element by the delta whatever form positions it", "[alxuiedit]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        struct Form
        {
            const char* layout;
            const char* attributes;
        };
        static const Form forms[] = {
            { "topleft",    "left=\"20\" top=\"30\" width=\"40\" height=\"20\"" },
            { "topleft",    "left_pad=\"6\" top_pad=\"8\" width=\"40\" height=\"20\"" },
            { "topleft",    "left_delta=\"6\" top_delta=\"8\" width=\"40\" height=\"20\"" },
            { "topleft",    "left=\"20\" top=\"30\" right=\"70\" bottom=\"50\"" },
            { "topleft",    "left=\"-60\" top=\"30\" width=\"40\" height=\"20\"" },
            { "topleft",    "left=\"20\" bottom_delta=\"-24\" width=\"40\" height=\"20\"" },
            { "bottomleft", "left=\"20\" bottom=\"30\" width=\"40\" height=\"20\"" },
            { "bottomleft", "left=\"20\" bottom_delta=\"-24\" width=\"40\" height=\"20\"" },
        };

        const S32 dx = 7;
        const S32 dy = 3;
        for (const Form& form : forms)
        {
            CAPTURE(form.layout, form.attributes);
            const std::string xml = file(form.layout, form.attributes);

            Build before;
            REQUIRE(before.run(xml));
            const LLRect was = before.rectOf("target");
            UNSCOPED_INFO("the element is there");
            REQUIRE(was.getWidth() > 0);

            ALXUIEdit edit;
            REQUIRE(edit.loadBuffer(xml));
            const bool translated = edit.translate({ "target" }, dx, dy, anchorOf(was, std::string(form.layout) == "topleft"));
            UNSCOPED_INFO("translates: " + edit.error());
            REQUIRE(translated);

            Build after;
            REQUIRE(after.run(edit.text()));
            LLRect expected(was);
            expected.translate(dx, dy);
            UNSCOPED_INFO("moved by the delta and nothing else: " + describe(after.rectOf("target"))
                          + " wanted " + describe(expected) + "\n" + edit.text());
            REQUIRE(after.rectOf("target") == expected);
        }
    }

    // An element the file does not position at all is given the one
    // attribute its layout counts from.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: an unpositioned element is given the attributes its layout counts from", "[alxuiedit]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const std::string xml = file("topleft", "width=\"40\" height=\"20\"");

        Build before;
        REQUIRE(before.run(xml));
        const LLRect was = before.rectOf("target");

        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(xml));
        const bool translated = edit.translate({ "target" }, 7, 3, anchorOf(was, true));
        UNSCOPED_INFO("translates: " + edit.error());
        CHECK(translated);
        {
            INFO("wrote left and top: " + edit.text());
            CHECK(edit.text().find("left=") != std::string::npos);
            CHECK(edit.text().find("top=") != std::string::npos);
        }

        Build after;
        REQUIRE(after.run(edit.text()));
        LLRect expected(was);
        expected.translate(7, 3);
        UNSCOPED_INFO("moved by the delta: " + describe(after.rectOf("target")) + "\n" + edit.text());
        CHECK(after.rectOf("target") == expected);
    }

    // A resize moves the far edge and leaves the edge the element is
    // positioned from where it was.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: a resize moves the far edges and keeps the ones it is positioned from", "[alxuiedit]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const std::string xml = file("topleft", "left=\"20\" top=\"30\" width=\"40\" height=\"20\"");

        Build before;
        REQUIRE(before.run(xml));
        const LLRect was = before.rectOf("target");

        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(xml));
        const bool resized = edit.resize({ "target" }, 10, 5, anchorOf(was, true));
        UNSCOPED_INFO("resizes: " + edit.error());
        CHECK(resized);

        Build after;
        REQUIRE(after.run(edit.text()));
        const LLRect now = after.rectOf("target");
        UNSCOPED_INFO("wider by the delta: " + describe(now));
        CHECK(now.getWidth() == was.getWidth() + 10);
        UNSCOPED_INFO("taller by the delta: " + describe(now));
        CHECK(now.getHeight() == was.getHeight() + 5);
        UNSCOPED_INFO("the left edge stayed");
        CHECK(now.mLeft == was.mLeft);
        UNSCOPED_INFO("the top edge stayed");
        CHECK(now.mTop == was.mTop);
    }

    // The round trip: a file read and written with no edit is the file.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: a file read and written with no edit is the same file", "[alxuiedit]")
    {
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(raw()));
        UNSCOPED_INFO("byte for byte");
        CHECK(edit.text() == raw());
        UNSCOPED_INFO("nothing to save");
        CHECK_FALSE(edit.dirty());
    }

    // One attribute changed changes its own bytes and no others.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: one attribute changed changes its own bytes and no others", "[alxuiedit]")
    {
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(raw()));
        CHECK(edit.setAttribute({ "target" }, "left", "25"));
        UNSCOPED_INFO("only the value moved");
        CHECK(edit.text() == replaced(raw(), "left=\"20\"", "left=\"25\""));
        CHECK(edit.dirty());

        // A value with a character XML spells differently is written
        // spelled that way.
        ALXUIEdit second;
        REQUIRE(second.loadBuffer(raw()));
        CHECK(second.setAttribute({}, "label", "Tom & Jerry & Co"));
        UNSCOPED_INFO("escaped as it is read");
        CHECK(second.text() ==
                      replaced(raw(), "label=\"Tom &amp; Jerry\"", "label=\"Tom &amp; Jerry &amp; Co\""));
    }

    // A removed attribute takes the whitespace before it, which is the
    // line it was written on.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: a removed attribute takes the line it was written on", "[alxuiedit]")
    {
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(raw()));
        CHECK(edit.removeAttribute({ "target" }, "top"));
        UNSCOPED_INFO("its line and nothing else");
        CHECK(edit.text() == replaced(raw(), "\r\n     top=\"10\"", ""));
        UNSCOPED_INFO("an attribute that is not there is not an edit");
        CHECK_FALSE(edit.removeAttribute({ "target" }, "top"));
    }

    // A move that would take a positive edge past zero would re-anchor
    // the element to the other side of its parent, which is a jump and
    // not a move.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: a move taking a positive edge past zero is refused", "[alxuiedit]")
    {
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(raw()));
        ALXUIEdit::Anchor now;
        now.left = 20;
        now.top = 10;
        now.width = 40;
        now.height = 20;
        UNSCOPED_INFO("refused");
        CHECK_FALSE(edit.translate({ "target" }, -25, 0, now));
        UNSCOPED_INFO("said why");
        CHECK_FALSE(edit.error().empty());
        UNSCOPED_INFO("and wrote nothing");
        CHECK(edit.text() == raw());
    }

    // The scan that finds an attribute's bytes, against every attribute of
    // every file the viewer ships: an offset off by one anywhere would
    // write over the wrong text.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: valueText finds every attribute of every shipped file where it is written", "[alxuiedit]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALXUICatalog catalog;
        catalog.scan(gDirUtilp->getSkinBaseDir());
        UNSCOPED_INFO("the skins are there");
        REQUIRE_FALSE(catalog.entries().empty());

        S32 files = 0;
        S32 attributes = 0;
        S32 spelled = 0;        // values written with an entity, which are not compared
        std::string wrong;
        for (const ALXUICatalog::Entry& entry : catalog.entries())
        {
            for (const ALXUICatalog::Layer& layer : entry.layers)
            {
                ALXUIEdit edit;
                if (!edit.loadFile(layer.path) || !edit.root())
                {
                    continue;   // the files that do not parse are the lint's business
                }
                ++files;

                std::vector<pugi::xml_node> stack{ edit.root() };
                while (!stack.empty())
                {
                    const pugi::xml_node node = stack.back();
                    stack.pop_back();
                    for (pugi::xml_node child : node.children())
                    {
                        if (child.type() == pugi::node_element)
                        {
                            stack.push_back(child);
                        }
                    }
                    for (pugi::xml_attribute attribute : node.attributes())
                    {
                        ++attributes;
                        std::string found;
                        if (!edit.valueText(node, attribute.name(), found))
                        {
                            if (wrong.size() < 400)
                            {
                                wrong += layer.path + ": " + node.name() + "/" + attribute.name() + " not found; ";
                            }
                            continue;
                        }
                        // A tab or a newline inside a value is a space by
                        // the time the parser hands it over, which the
                        // file's own bytes are not.
                        for (char& c : found)
                        {
                            c = (c == '\t' || c == '\r' || c == '\n') ? ' ' : c;
                        }
                        if (found.find('&') != std::string::npos)
                        {
                            ++spelled;      // the file spells a character; the bytes are its own
                        }
                        else if (found != attribute.value())
                        {
                            if (wrong.size() < 400)
                            {
                                wrong += layer.path + ": " + node.name() + "/" + attribute.name()
                                       + " read as \"" + found + "\"; ";
                            }
                        }
                    }
                }
            }
        }

        std::cout << "  [alxuiedit] " << attributes << " attributes over " << files
                  << " files, " << spelled << " of them spelled with an entity" << std::endl;
        UNSCOPED_INFO("every file was read");
        CHECK(files > 100);
        UNSCOPED_INFO("every attribute found where it is written: " + wrong);
        CHECK(wrong == std::string());
    }

    // Text is written into the element that has it, and into one that has
    // none by opening the tag that was closing itself.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: setText writes into an element's text or opens a self-closing tag", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <text name=\"a\">Hello</text>\n"
            "    <text name=\"b\" />\n"
            "</panel>\n";
        {
            ALXUIEdit edit;
            REQUIRE(edit.loadBuffer(source));
            CHECK(edit.setText({ "a" }, "Bonjour"));
            UNSCOPED_INFO("the text and nothing else");
            CHECK(edit.text() == replaced(source, ">Hello<", ">Bonjour<"));
        }
        {
            ALXUIEdit edit;
            REQUIRE(edit.loadBuffer(source));
            CHECK(edit.setText({ "b" }, "Bonjour"));
            UNSCOPED_INFO("the tag opens for it");
            CHECK(edit.text() ==
                          replaced(source, "<text name=\"b\" />", "<text name=\"b\">Bonjour</text>"));
        }
        {
            // What XML spells differently is spelled that way.
            ALXUIEdit edit;
            REQUIRE(edit.loadBuffer(source));
            CHECK(edit.setText({ "a" }, "Salt & Pepper"));
            UNSCOPED_INFO("escaped");
            CHECK(edit.text() == replaced(source, ">Hello<", ">Salt &amp; Pepper<"));
        }
    }

    // An element is written as the last child of its parent, on its own
    // line, indented the way the children already there are.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: insertElement adds a last child on its own line indented like its siblings", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "\t<panel name=\"inner\">\n"
            "\t\t<text name=\"a\">Hello</text>\n"
            "\t</panel>\n"
            "</panel>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));
        CHECK(edit.insertElement({ "inner" }, "<text name=\"b\">Bonjour</text>"));
        UNSCOPED_INFO("beside its sibling, indented like it");
        CHECK(edit.text() ==
                      replaced(source, "</text>\n\t</panel>",
                                       "</text>\n\t\t<text name=\"b\">Bonjour</text>\n\t</panel>"));

        // Into a parent with no children at all, which is where an
        // ancestor chain ends.
        const std::string bare =
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\"/>\n"
            "</panel>\n";
        ALXUIEdit second;
        REQUIRE(second.loadBuffer(bare));
        CHECK(second.insertElement({ "inner" }, "<text name=\"b\"/>"));
        UNSCOPED_INFO("the tag opens for it");
        CHECK(second.text() ==
                      replaced(bare, "<panel name=\"inner\"/>",
                                     "<panel name=\"inner\">\n        <text name=\"b\"/>\n    </panel>"));
    }

    // An element moved under another parent leaves the rest of the file
    // as it was, and arrives indented for where it lands.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: an element moved under another parent leaves the rest of the file as it was", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <text name=\"a\">Hello</text>\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"c\">Here</text>\n"
            "    </panel>\n"
            "</panel>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));
        CHECK(edit.moveElement({ "a" }, { "inner" }));
        UNSCOPED_INFO("under its new parent, and gone from where it was");
        CHECK(edit.text() ==
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"c\">Here</text>\n"
            "        <text name=\"a\">Hello</text>\n"
            "    </panel>\n"
            "</panel>\n");
    }

    // An element removed takes its own line with it, and its children.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: a removed element takes its own line and its children with it", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"c\">Here</text>\n"
            "    </panel>\n"
            "    <text name=\"a\">Hello</text>\n"
            "</panel>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));
        CHECK(edit.removeElement({ "inner" }));
        UNSCOPED_INFO("with everything under it");
        CHECK(edit.text() ==
            "<panel name=\"root\">\n"
            "    <text name=\"a\">Hello</text>\n"
            "</panel>\n");
        UNSCOPED_INFO("an element that is not there is not an edit");
        CHECK_FALSE(edit.removeElement({ "inner" }));
    }

    // The extent of an element is the whole of it: a comment, a quoted
    // angle bracket and a child of the same tag are all inside it.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: an element's extent is not cut short by comments or quoted brackets", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <panel name=\"a\" tool_tip=\"1 &gt; 0\">\n"
            "        <!-- </panel> is not the end of anything -->\n"
            "        <panel name=\"b\"/>\n"
            "    </panel>\n"
            "    <text name=\"z\">End</text>\n"
            "</panel>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));
        CHECK(edit.removeElement({ "a" }));
        UNSCOPED_INFO("all of it and no more");
        CHECK(edit.text() ==
            "<panel name=\"root\">\n"
            "    <text name=\"z\">End</text>\n"
            "</panel>\n");
    }

    // Order is what a menu is, so an element arrives beside a sibling and
    // takes that sibling's own indentation.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: insertBefore and insertAfter put an element beside a sibling at its indent", "[alxuiedit]")
    {
        const std::string source =
            "<menu name=\"root\">\n"
            "    <item name=\"a\"/>\n"
            "    <item name=\"b\"/>\n"
            "</menu>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));
        CHECK(edit.insertBefore({ "b" }, "<item name=\"x\"/>"));
        CHECK(edit.insertAfter({ "a" }, "<item name=\"y\"/>"));
        UNSCOPED_INFO("each where it was asked for");
        CHECK(edit.text() ==
            "<menu name=\"root\">\n"
            "    <item name=\"a\"/>\n"
            "    <item name=\"y\"/>\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"b\"/>\n"
            "</menu>\n");
    }

    // A reorder among siblings, which is a removal and an insertion, and
    // the sibling it lands beside is named as the tree stood before it.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: moveBefore reorders siblings and an element cannot move beside itself", "[alxuiedit]")
    {
        const std::string source =
            "<menu name=\"root\">\n"
            "    <item name=\"a\"/>\n"
            "    <item name=\"b\"/>\n"
            "    <item name=\"c\"/>\n"
            "</menu>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));
        UNSCOPED_INFO("last goes first");
        CHECK(edit.moveBefore({ "c" }, { "a" }));
        UNSCOPED_INFO("in that order");
        CHECK(edit.text() ==
            "<menu name=\"root\">\n"
            "    <item name=\"c\"/>\n"
            "    <item name=\"a\"/>\n"
            "    <item name=\"b\"/>\n"
            "</menu>\n");
        UNSCOPED_INFO("and not beside itself");
        CHECK_FALSE(edit.moveAfter({ "a" }, { "a" }));
    }

    // Siblings of one name are told apart by which of them they are, so
    // taking one away renumbers the ones after it -- including the one the
    // move is going to land beside.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: removing a same-named sibling renumbers the paths after it", "[alxuiedit]")
    {
        const std::string source =
            "<menu name=\"root\">\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"last\"/>\n"
            "</menu>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));

        // The ordinal is the index counted from zero, so x#2 is the third
        // of them. Without the renumbering it would land beside itself.
        UNSCOPED_INFO("the first goes after the third");
        CHECK(edit.moveAfter({ "x" }, { "x#2" }));
        UNSCOPED_INFO("three of them still, in the order asked for");
        CHECK(edit.text() ==
            "<menu name=\"root\">\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"last\"/>\n"
            "</menu>\n");

        ALXUIEdit::path_t after{ "x#2" };
        ALXUIEdit::path_t deeper{ "x#2", "child" };
        ALXUIEdit::path_t second{ "x#1" };
        ALXUIEdit::path_t elsewhere{ "other#2" };
        ALXUIEdit::path_t earlier{ "x" };
        const ALXUIEdit::path_t removed{ "x#1" };
        ALXUIEdit::afterRemoving(removed, after);
        ALXUIEdit::afterRemoving(removed, deeper);
        ALXUIEdit::afterRemoving(removed, second);
        ALXUIEdit::afterRemoving(removed, elsewhere);
        ALXUIEdit::afterRemoving(removed, earlier);
        UNSCOPED_INFO("the one after it counts one fewer");
        CHECK(after[0] == std::string("x#1"));
        UNSCOPED_INFO("and so does a path through it");
        CHECK(deeper[0] == std::string("x#1"));
        UNSCOPED_INFO("the one that was removed keeps its own step");
        CHECK(second[0] == std::string("x#1"));
        UNSCOPED_INFO("another name is untouched");
        CHECK(elsewhere[0] == std::string("other#2"));
        UNSCOPED_INFO("and so is the one before it");
        CHECK(earlier[0] == std::string("x"));
    }

    // A step is one operation as a caller asked for it, whatever it is
    // made of, and undoing it puts the file back byte for byte.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: each operation is one undo step and undoing restores the file exactly", "[alxuiedit]")
    {
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(raw()));
        UNSCOPED_INFO("nothing to undo yet");
        CHECK_FALSE(edit.canUndo());
        UNSCOPED_INFO("and it is not dirty");
        CHECK_FALSE(edit.dirty());

        CHECK(edit.setAttribute({ "target" }, "left", "30"));
        CHECK(edit.setAttribute({ "target" }, "top", "40"));
        CHECK(edit.undoDepth() == 2u);
        CHECK(edit.dirty());

        CHECK(edit.undo());
        CHECK(edit.undo());
        UNSCOPED_INFO("back to the file it was");
        CHECK(edit.text() == raw());
        UNSCOPED_INFO("and clean again");
        CHECK_FALSE(edit.dirty());
        UNSCOPED_INFO("with nothing left to undo");
        CHECK_FALSE(edit.canUndo());

        CHECK(edit.redo());
        CHECK(edit.redo());
        {
            INFO("both back");
            CHECK(edit.text().find("left=\"30\"") != std::string::npos);
            CHECK(edit.text().find("top=\"40\"") != std::string::npos);
        }

        {
            INFO("undo the redos");
            CHECK(edit.undo());
            CHECK(edit.undo());
        }
        UNSCOPED_INFO("a fresh edit clears what was undone");
        CHECK(edit.setAttribute({ "target" }, "left", "5"));
        UNSCOPED_INFO("nothing to redo");
        CHECK_FALSE(edit.canRedo());
    }

    // One undo for one move, however many splices it took.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: one undo takes back a whole move", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <text name=\"a\">Hello</text>\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"c\">Here</text>\n"
            "    </panel>\n"
            "</panel>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));
        CHECK(edit.moveElement({ "a" }, { "inner" }));
        UNSCOPED_INFO("one step for the move");
        CHECK(edit.undoDepth() == 1u);
        CHECK(edit.undo());
        UNSCOPED_INFO("the whole move came back");
        CHECK(edit.text() == source);

        // An operation that fails before it writes leaves no step.
        UNSCOPED_INFO("no such element");
        CHECK_FALSE(edit.removeElement({ "nowhere" }));
        UNSCOPED_INFO("and nothing to undo");
        CHECK_FALSE(edit.canUndo());
    }

    // Every other form of position comes off. A delta is read before the
    // edge it overrides and a padding is measured from a sibling the
    // element has left, so leaving one behind puts the element somewhere
    // nobody asked for.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: reauthor drops every other form of position", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <panel name=\"a\" left_delta=\"5\" top_pad=\"3\" right=\"90\" bottom=\"10\""
            " left=\"1\" top=\"2\" width=\"9\" height=\"8\" follows=\"left|top\" />\n"
            "</panel>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));

        ALXUIEdit::Anchor want;
        want.left = 30;
        want.top = 40;
        want.width = 50;
        want.height = 60;
        want.topLeft = true;
        CHECK(edit.reauthor({ "a" }, want));

        const pugi::xml_node node = edit.resolve({ "a" });
        CHECK(node.attribute("left").as_int() == 30);
        CHECK(node.attribute("top").as_int() == 40);
        CHECK(node.attribute("width").as_int() == 50);
        CHECK(node.attribute("height").as_int() == 60);
        CHECK_FALSE(node.attribute("left_delta"));
        CHECK_FALSE(node.attribute("top_pad"));
        CHECK_FALSE(node.attribute("right"));
        CHECK_FALSE(node.attribute("bottom"));
        UNSCOPED_INFO("what is not a position is untouched");
        CHECK(std::string_view(node.attribute("follows").value()) == "left|top");

        // And it is one step, however many splices it took.
        CHECK(edit.undoDepth() == 1u);
        CHECK(edit.undo());
        UNSCOPED_INFO("the file it was");
        CHECK(edit.text() == source);
    }

    // A parent that lays its children out itself is given a size and
    // nothing else, and a parent that counts up from its bottom is given
    // the edge it counts from.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: reauthor gives a size alone or a bottom edge as the parent needs", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <panel name=\"a\" left=\"1\" top=\"2\" width=\"9\" height=\"8\" />\n"
            "</panel>\n";

        ALXUIEdit::Anchor want;
        want.left = 30;
        want.top = 40;
        want.bottom = 70;
        want.width = 50;
        want.height = 60;

        {
            ALXUIEdit edit;
            REQUIRE(edit.loadBuffer(source));
            want.topLeft = true;
            UNSCOPED_INFO("re-authors as a size");
            CHECK(edit.reauthor({ "a" }, want, ALXUIEdit::AUTHOR_SIZE));
            const pugi::xml_node node = edit.resolve({ "a" });
            CHECK(node.attribute("width").as_int() == 50);
            CHECK(node.attribute("height").as_int() == 60);
            CHECK_FALSE(node.attribute("left"));
            CHECK_FALSE(node.attribute("top"));
        }
        {
            ALXUIEdit edit;
            REQUIRE(edit.loadBuffer(source));
            want.topLeft = false;
            UNSCOPED_INFO("re-authors from the bottom");
            CHECK(edit.reauthor({ "a" }, want));
            const pugi::xml_node node = edit.resolve({ "a" });
            CHECK(node.attribute("bottom").as_int() == 70);
            CHECK_FALSE(node.attribute("top"));
        }
    }

    // A step says what it did, so a caller holding something built from this
    // document can put one field of one element onto what it has rather than
    // building it all again. A step that did more than that says so by
    // saying nothing, since there is no one field for the caller to write.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: a step says what it did and where it leaves a renamed element", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <panel name=\"a\" left=\"1\" top=\"2\" width=\"9\" height=\"8\" />\n"
            "</panel>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));

        CHECK(edit.setAttribute({ "a" }, "width", "40"));
        CHECK(edit.undo());
        UNSCOPED_INFO("the step wrote one field");
        CHECK(edit.lastChange().oneField);
        UNSCOPED_INFO("and says which");
        CHECK(edit.lastChange().field == std::string("width"));
        std::string value;
        UNSCOPED_INFO("the element writes it still");
        CHECK(edit.fieldText({ "a" }, "width", value));
        UNSCOPED_INFO("with what it read before the step");
        CHECK(value == std::string("9"));

        CHECK(edit.redo());
        UNSCOPED_INFO("the same step, the other way");
        CHECK(edit.lastChange().oneField);
        UNSCOPED_INFO("and the field reads what the step wrote");
        CHECK(edit.fieldText({ "a" }, "width", value));
        UNSCOPED_INFO("which is what was written");
        CHECK(value == std::string("40"));

        // An attribute the file did not carry: undoing its arrival leaves
        // the element writing nothing, and nothing is not a value to put
        // onto anything.
        UNSCOPED_INFO("adds one");
        CHECK(edit.setAttribute({ "a" }, "tool_tip", "hello"));
        CHECK(edit.undo());
        UNSCOPED_INFO("still one field");
        CHECK(edit.lastChange().oneField);
        UNSCOPED_INFO("and the element no longer writes it");
        CHECK_FALSE(edit.fieldText({ "a" }, "tool_tip", value));

        // A name is what a path is made of, so writing one moves the element
        // as far as anything holding a path to it is concerned. A step says
        // where it was and where it now is, and an undo and a redo each say
        // which of the two the element is at.
        {
            ALXUIEdit named;
            REQUIRE(named.loadBuffer(
                "<panel name=\"root\">\n"
                "    <panel name=\"a\" width=\"9\" height=\"8\" />\n"
                "    <panel name=\"b\" width=\"9\" height=\"8\" />\n"
                "</panel>\n"));
            CHECK(named.setAttribute({ "b" }, "name", "a"));
            CHECK(named.undo());
            UNSCOPED_INFO("undone, it is where it was");
            CHECK(ALXUISelection::toString(named.lastPath()) == std::string("b"));
            CHECK(named.redo());
            // Its new name is one a sibling before it already carries, so it
            // is the second of that name and the step says so.
            UNSCOPED_INFO("done again, it is where the step put it");
            CHECK(ALXUISelection::toString(named.lastPath()) ==
                          ALXUISelection::step("a", 1));
        }

        // Renaming as an operation: it says where the element went, and the
        // paths a caller was holding are brought along -- the element's own,
        // one under it, a later sibling of the name it left, and a later
        // sibling of the name it took.
        {
            ALXUIEdit doc;
            REQUIRE(doc.loadBuffer(
                "<panel name=\"root\">\n"
                "    <panel name=\"a\" width=\"9\" height=\"8\" />\n"
                "    <panel name=\"b\" width=\"9\" height=\"8\">\n"
                "        <panel name=\"inner\" width=\"4\" height=\"4\" />\n"
                "    </panel>\n"
                "    <panel name=\"b\" width=\"9\" height=\"8\" />\n"
                "    <panel name=\"a\" width=\"9\" height=\"8\" />\n"
                "</panel>\n"));

            ALXUIEdit::path_t moved;
            CHECK(doc.rename({ "b" }, "a", moved));
            UNSCOPED_INFO("and says where it went");
            CHECK(ALXUISelection::toString(moved) ==
                          ALXUISelection::step("a", 1));

            const auto brought = [&moved](ALXUIEdit::path_t held)
            {
                ALXUIEdit::afterRenaming({ "b" }, moved, held);
                return ALXUISelection::toString(held);
            };
            UNSCOPED_INFO("a path under it comes along");
            CHECK(brought({ "b", "inner" }) ==
                          ALXUISelection::toString({ ALXUISelection::step("a", 1), "inner" }));
            UNSCOPED_INFO("the other of the name it left is now the first of them");
            CHECK(brought({ ALXUISelection::step("b", 1) }) == std::string("b"));
            UNSCOPED_INFO("and the later of the name it took counts one more");
            CHECK(brought({ ALXUISelection::step("a", 1) }) ==
                          ALXUISelection::step("a", 2));
            UNSCOPED_INFO("the one before it does not");
            CHECK(brought({ "a" }) == std::string("a"));
            UNSCOPED_INFO("nor does another branch entirely");
            CHECK(brought({ "elsewhere", "b" }) == std::string("elsewhere/b"));
        }

        // A step says what kind of thing it was, so that a caller can say so
        // in its own words -- and says it before the step is taken, since
        // afterwards it has moved to the other stack.
        {
            ALXUIEdit doc;
            REQUIRE(doc.loadBuffer(
                "<panel name=\"root\">\n"
                "    <panel name=\"a\" width=\"9\" height=\"8\">Hello</panel>\n"
                "</panel>\n"));

            CHECK(doc.setAttribute({ "a" }, "width", "40"));
            CHECK(doc.removeAttribute({ "a" }, "height"));
            CHECK(doc.setText({ "a" }, "Goodbye"));
            CHECK(doc.insertElement({ "a" }, "<panel name=\"b\"/>"));
            CHECK(doc.removeElement({ "a", "b" }));

            // The stack, read from the top down as undo would take it.
            static const ALXUIEdit::Did in_order[] = {
                ALXUIEdit::Did::RemovedElement,
                ALXUIEdit::Did::AddedElement,
                ALXUIEdit::Did::WroteText,
                ALXUIEdit::Did::TookFieldOut,
                ALXUIEdit::Did::WroteField,
            };
            for (const ALXUIEdit::Did did : in_order)
            {
                const ALXUIEdit::Change* next = doc.nextUndo();
                UNSCOPED_INFO("there is a step to take back");
                REQUIRE(next != nullptr);
                UNSCOPED_INFO("and it says what it was");
                REQUIRE(next->did == did);
                UNSCOPED_INFO("taken back");
                REQUIRE(doc.undo());
                UNSCOPED_INFO("which is what was taken back");
                REQUIRE(doc.lastChange().did == did);
            }
            UNSCOPED_INFO("and then there is not");
            CHECK(doc.nextUndo() == nullptr);
            UNSCOPED_INFO("but there is one to do again");
            CHECK(doc.nextRedo() != nullptr);
        }

        // A whole element arriving is not one field of one element.
        const std::string before = edit.text();
        CHECK(edit.insertElement({}, "<panel name=\"b\" width=\"4\" height=\"4\"/>"));
        CHECK(edit.undo());
        UNSCOPED_INFO("which the step does not claim to be");
        CHECK_FALSE(edit.lastChange().oneField);
        UNSCOPED_INFO("and the file is what it was");
        CHECK(edit.text() == before);
    }

    // An attribute spelt wrongly holds the author's value under a name
    // nothing reads. Taking it out and putting it back would move it to the
    // end of the element and rewrite whatever the file had between the
    // quotes, so only the name is spliced.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: renameAttribute respells only an attribute's name in one step", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <!-- a comment nobody may touch -->\n"
            "    <panel name=\"a\"  tool_tp='say &amp; do'  width=\"9\" />\n"
            "</panel>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));

        UNSCOPED_INFO("spelt again");
        CHECK(edit.renameAttribute({ "a" }, "tool_tp", "tool_tip"));
        UNSCOPED_INFO("only the name moved");
        CHECK(edit.text() ==
            "<panel name=\"root\">\n"
            "    <!-- a comment nobody may touch -->\n"
            "    <panel name=\"a\"  tool_tip='say &amp; do'  width=\"9\" />\n"
            "</panel>\n");

        // One step, because it is one thing done.
        UNSCOPED_INFO("says what it did");
        REQUIRE(edit.nextUndo() != nullptr);
        UNSCOPED_INFO("which is a name written again");
        CHECK(edit.nextUndo()->did == ALXUIEdit::Did::SpeltFieldAgain);
        UNSCOPED_INFO("under the name it now has");
        CHECK(edit.nextUndo()->field == std::string("tool_tip"));
        CHECK(edit.undo());
        UNSCOPED_INFO("the whole of it");
        CHECK(edit.text() == source);
        UNSCOPED_INFO("and there is nothing else to put back");
        CHECK_FALSE(edit.canUndo());

        // Two of a name on one element is one value chosen by which the
        // parser reads last, which is not a state to leave a file in.
        UNSCOPED_INFO("refuses to make a second width");
        CHECK_FALSE(edit.renameAttribute({ "a" }, "tool_tp", "width"));
        UNSCOPED_INFO("and says why");
        CHECK_FALSE(edit.error().empty());
        UNSCOPED_INFO("having changed nothing");
        CHECK(edit.text() == source);
        UNSCOPED_INFO("a name the element does not carry");
        CHECK_FALSE(edit.renameAttribute({ "a" }, "height", "width"));
        UNSCOPED_INFO("the name it has");
        CHECK_FALSE(edit.renameAttribute({ "a" }, "width", "width"));
        UNSCOPED_INFO("no element");
        CHECK_FALSE(edit.renameAttribute({ "nowhere" }, "width", "height"));
    }

    // An element cannot be moved into itself or beside anything under it.
    // The removal that starts a move used to take the destination with it,
    // and the element, lifted and with nowhere to land, was gone from the
    // file with the step half done; the answer is no, and the file as it was.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: an element cannot be moved into itself or beside anything under it", "[alxuiedit]")
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <panel name=\"outer\">\n"
            "        <text name=\"c\">Here</text>\n"
            "    </panel>\n"
            "</panel>\n";
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(source));
        UNSCOPED_INFO("not into itself");
        CHECK_FALSE(edit.moveElement({ "outer" }, { "outer" }));
        UNSCOPED_INFO("not into its own child");
        CHECK_FALSE(edit.moveElement({ "outer" }, { "outer", "c" }));
        UNSCOPED_INFO("not beside its own child");
        CHECK_FALSE(edit.moveBefore({ "outer" }, { "outer", "c" }));
        UNSCOPED_INFO("nor after it");
        CHECK_FALSE(edit.moveAfter({ "outer" }, { "outer", "c" }));
        UNSCOPED_INFO("and the file is as it was");
        CHECK(edit.text() == source);
        UNSCOPED_INFO("with nothing to undo");
        CHECK_FALSE(edit.canUndo());
        UNSCOPED_INFO("and a reason given");
        CHECK_FALSE(edit.error().empty());
    }

    // A step says where it leaves the element it moved or made, read off
    // the file once the step is taken. Among siblings of one name that is
    // the only place the answer is right: the element before the sibling a
    // move went before is the moved element itself when the two share a
    // name, and the sibling has become the second of them.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: a step records where the element it moved or made ends up", "[alxuiedit]")
    {
        ALXUIEdit doc;
        REQUIRE(doc.loadBuffer(
            "<panel name=\"root\">\n"
            "    <panel name=\"a\" width=\"1\"/>\n"
            "    <panel name=\"a\" width=\"2\"/>\n"
            "    <panel name=\"b\"/>\n"
            "</panel>\n"));

        const auto after = [&doc]()
        {
            const ALXUIEdit::Change* step = doc.nextUndo();
            return step ? ALXUISelection::toString(step->after) : std::string("(no step)");
        };
        const auto widthAt = [&doc](const std::string& path)
        {
            return std::string(doc.resolve(ALXUISelection::fromString(path)).attribute("width").as_string("?"));
        };

        // Up, before a sibling of its own name: it takes that sibling's
        // place among them.
        CHECK(doc.moveBefore({ ALXUISelection::step("a", 1) }, { "a" }));
        UNSCOPED_INFO("and is the first of the name now");
        CHECK(after() == std::string("a"));
        UNSCOPED_INFO("which is the one that moved");
        CHECK(widthAt("a") == std::string("2"));
        UNSCOPED_INFO("and the other is the second");
        CHECK(widthAt(ALXUISelection::step("a", 1)) == std::string("1"));

        // After another sibling, past the other of its name.
        CHECK(doc.moveAfter({ "a" }, { "b" }));
        UNSCOPED_INFO("and is the second of the name again");
        CHECK(after() == ALXUISelection::step("a", 1));
        UNSCOPED_INFO("the same element");
        CHECK(widthAt(ALXUISelection::step("a", 1)) == std::string("2"));

        // Into another element.
        CHECK(doc.moveElement({ ALXUISelection::step("a", 1) }, { "b" }));
        UNSCOPED_INFO("and is under it");
        CHECK(after() == std::string("b/a"));
        UNSCOPED_INFO("still itself");
        CHECK(widthAt("b/a") == std::string("2"));

        // Made, last among a parent's children; before a sibling; after one.
        CHECK(doc.insertElement({}, "<panel name=\"a\" width=\"3\"/>"));
        UNSCOPED_INFO("which is the second of that name among the root's children");
        CHECK(after() == ALXUISelection::step("a", 1));
        UNSCOPED_INFO("and is the new one");
        CHECK(widthAt(ALXUISelection::step("a", 1)) == std::string("3"));
        CHECK(doc.insertBefore({ "a" }, "<panel name=\"a\" width=\"4\"/>"));
        UNSCOPED_INFO("which is now the first of them");
        CHECK(after() == std::string("a"));
        UNSCOPED_INFO("and is the new one");
        CHECK(widthAt("a") == std::string("4"));
        CHECK(doc.insertAfter({ "b" }, "<panel name=\"c\"/>"));
        UNSCOPED_INFO("where it was put");
        CHECK(after() == std::string("c"));

        // Taken out: what is left where it was is what held it.
        CHECK(doc.removeElement({ "b", "a" }));
        UNSCOPED_INFO("leaving its parent");
        CHECK(after() == std::string("b"));

        // The record travels with the step: put back, the element reads as
        // it did before; done again, it is where the step put it.
        CHECK(doc.undo());
        UNSCOPED_INFO("undone, the element is where it was");
        CHECK(ALXUISelection::toString(doc.lastPath()) == std::string("b/a"));
        CHECK(doc.redo());
        UNSCOPED_INFO("done again, its parent is what is left");
        CHECK(ALXUISelection::toString(doc.lastPath()) == std::string("b"));
    }

    // The undo stack is capped by what it weighs, so past the cap a step
    // taken is a step the stack does not deepen for. The count of steps
    // taken goes on counting, which is what a caller keeping a history of
    // its own across several documents has to count.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: the undo stack is capped by weight but stepsTaken keeps counting", "[alxuiedit]")
    {
        const std::string heavy =
            "<panel name=\"root\">\n"
            "    <!-- " + std::string(2500000, 'x') + " -->\n"
            "    <panel name=\"a\" width=\"1\"/>\n"
            "</panel>\n";
        ALXUIEdit doc;
        REQUIRE(doc.loadBuffer(heavy));
        UNSCOPED_INFO("nothing taken yet");
        CHECK(doc.stepsTaken() == 0u);
        CHECK(doc.setAttribute({ "a" }, "width", "2"));
        CHECK(doc.stepsTaken() == 1u);
        CHECK(doc.undoDepth() == 1u);
        CHECK(doc.setAttribute({ "a" }, "width", "3"));
        CHECK(doc.stepsTaken() == 2u);
        UNSCOPED_INFO("but the stack is full, so still one held");
        CHECK(doc.undoDepth() == 1u);
        CHECK(doc.undo());
        UNSCOPED_INFO("which puts back the last of them");
        CHECK(doc.resolve({ "a" }).attribute("width").as_int() == 2);
        UNSCOPED_INFO("and takes back none of the count");
        CHECK(doc.stepsTaken() == 2u);
        CHECK(doc.loadBuffer(heavy));
        UNSCOPED_INFO("which starts the count over");
        CHECK(doc.stepsTaken() == 0u);
    }

    // A copy of an element lands after it, children and all, at the depth
    // it was at, keeping every byte of it; the step says where the copy is.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: duplicateElement copies an element and its children right after it", "[alxuiedit]")
    {
        ALXUIEdit doc;
        REQUIRE(doc.loadBuffer(
            "<panel name=\"root\">\n"
            "    <panel name=\"row\" width=\"9\">\n"
            "        <text name=\"t\">Hi &amp; bye</text>\n"
            "    </panel>\n"
            "    <panel name=\"after\"/>\n"
            "</panel>\n"));
        CHECK(doc.duplicateElement({ "row" }));
        UNSCOPED_INFO("after the original, as it was written");
        CHECK(doc.text() ==
            "<panel name=\"root\">\n"
            "    <panel name=\"row\" width=\"9\">\n"
            "        <text name=\"t\">Hi &amp; bye</text>\n"
            "    </panel>\n"
            "    <panel name=\"row\" width=\"9\">\n"
            "        <text name=\"t\">Hi &amp; bye</text>\n"
            "    </panel>\n"
            "    <panel name=\"after\"/>\n"
            "</panel>\n");
        const ALXUIEdit::Change* step = doc.nextUndo();
        REQUIRE(step != nullptr);
        UNSCOPED_INFO("which added an element");
        CHECK(step->did == ALXUIEdit::Did::AddedElement);
        UNSCOPED_INFO("and says where the copy is");
        CHECK(ALXUISelection::toString(step->after) ==
                      ALXUISelection::step("row", 1));
        UNSCOPED_INFO("the root is not copied");
        CHECK_FALSE(doc.duplicateElement({}));
        UNSCOPED_INFO("nor nothing");
        CHECK_FALSE(doc.duplicateElement({ "nowhere" }));
        CHECK(doc.undo());
        UNSCOPED_INFO("puts it back");
        CHECK(doc.text() ==
            "<panel name=\"root\">\n"
            "    <panel name=\"row\" width=\"9\">\n"
            "        <text name=\"t\">Hi &amp; bye</text>\n"
            "    </panel>\n"
            "    <panel name=\"after\"/>\n"
            "</panel>\n");
    }

    // What a field reads after a step is what the parser reads, since it
    // is put onto a view: a value with an ampersand in it was handed over
    // with the entity still spelt out.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: fieldText reads a value as the parser does with entities decoded", "[alxuiedit]")
    {
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(
            "<panel name=\"root\">\n"
            "    <button name=\"a\" label=\"Save\"/>\n"
            "</panel>\n"));
        CHECK(edit.setAttribute({ "a" }, "label", "Save & Close"));
        UNSCOPED_INFO("escaped in the file");
        CHECK(edit.text() ==
            "<panel name=\"root\">\n"
            "    <button name=\"a\" label=\"Save &amp; Close\"/>\n"
            "</panel>\n");
        std::string value;
        CHECK(edit.fieldText({ "a" }, "label", value));
        UNSCOPED_INFO("as the parser reads it");
        CHECK(value == std::string("Save & Close"));
    }

    // A move that fails part way -- the element taken out, nowhere to put
    // it -- leaves the file as it was, no step behind it, and whatever
    // could be redone before it still there. It used to leave the file
    // without the element and a step to undo that nobody took.
    TEST_CASE_METHOD(alxuiedit_data, "alxuiedit: a move that fails part way leaves the file and the redo stack as they were", "[alxuiedit]")
    {
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(
            "<panel name=\"root\">\n"
            "    <button name=\"a\"/>\n"
            "    <panel name=\"b\"/>\n"
            "</panel>\n"));
        // Something to redo, which a failed step must not take away.
        CHECK(edit.setAttribute({ "a" }, "width", "40"));
        CHECK(edit.undo());
        CHECK(edit.canRedo());
        const std::string before = edit.text();
        const size_t taken = edit.stepsTaken();

        UNSCOPED_INFO("a move into nowhere fails");
        CHECK_FALSE(edit.moveElement({ "a" }, { "no_such" }));
        UNSCOPED_INFO("and says so");
        CHECK_FALSE(edit.error().empty());
        UNSCOPED_INFO("the file is as it was");
        CHECK(edit.text() == before);
        UNSCOPED_INFO("no step was taken");
        CHECK_FALSE(edit.canUndo());
        UNSCOPED_INFO("and none is counted");
        CHECK(edit.stepsTaken() == taken);
        UNSCOPED_INFO("and what could be redone still can");
        CHECK(edit.canRedo());

        UNSCOPED_INFO("a move beside nowhere fails");
        CHECK_FALSE(edit.moveBefore({ "a" }, { "no_such" }));
        UNSCOPED_INFO("and leaves the file as it was");
        CHECK(edit.text() == before);
        UNSCOPED_INFO("with nothing to undo");
        CHECK_FALSE(edit.canUndo());
    }
}
