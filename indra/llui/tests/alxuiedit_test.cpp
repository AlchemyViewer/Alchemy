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

#include "../alxuiedit.h"

#include "../alxuicatalog.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "lldir.h"
#include "llxmlnode.h"

#include <iostream>

#include "../test/lltut.h"

class LLAvatarName;
const std::string gEditTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gEditTestAnonName;
}

namespace tut
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

    typedef test_group<alxuiedit_data> alxuiedit_test;
    typedef alxuiedit_test::object     alxuiedit_object;
    tut::alxuiedit_test alxuiedit_testgroup("alxuiedit");

    // The property: whichever form the file used, the element ends up
    // exactly the delta away from where it was.
    template<> template<>
    void alxuiedit_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
            const std::string xml = file(form.layout, form.attributes);
            const std::string what = std::string(form.layout) + " " + form.attributes;

            Build before;
            ensure("builds: " + what, before.run(xml));
            const LLRect was = before.rectOf("target");
            ensure("the element is there: " + what, was.getWidth() > 0);

            ALXUIEdit edit;
            ensure("loads: " + what, edit.loadBuffer(xml));
            ensure("translates: " + what + ": " + edit.error(),
                   edit.translate({ "target" }, dx, dy, anchorOf(was, std::string(form.layout) == "topleft")));

            Build after;
            ensure("rebuilds: " + what, after.run(edit.text()));
            LLRect expected(was);
            expected.translate(dx, dy);
            ensure_equals("moved by the delta and nothing else: " + what + " " + describe(after.rectOf("target"))
                          + " wanted " + describe(expected) + "\n" + edit.text(),
                          after.rectOf("target"), expected);
        }
    }

    // An element the file does not position at all is given the one
    // attribute its layout counts from.
    template<> template<>
    void alxuiedit_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const std::string xml = file("topleft", "width=\"40\" height=\"20\"");

        Build before;
        ensure("builds", before.run(xml));
        const LLRect was = before.rectOf("target");

        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(xml));
        ensure("translates: " + edit.error(), edit.translate({ "target" }, 7, 3, anchorOf(was, true)));
        ensure("wrote left and top: " + edit.text(), edit.text().find("left=") != std::string::npos
                                                  && edit.text().find("top=") != std::string::npos);

        Build after;
        ensure("rebuilds", after.run(edit.text()));
        LLRect expected(was);
        expected.translate(7, 3);
        ensure_equals("moved by the delta: " + describe(after.rectOf("target")) + "\n" + edit.text(),
                      after.rectOf("target"), expected);
    }

    // A resize moves the far edge and leaves the edge the element is
    // positioned from where it was.
    template<> template<>
    void alxuiedit_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const std::string xml = file("topleft", "left=\"20\" top=\"30\" width=\"40\" height=\"20\"");

        Build before;
        ensure("builds", before.run(xml));
        const LLRect was = before.rectOf("target");

        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(xml));
        ensure("resizes: " + edit.error(), edit.resize({ "target" }, 10, 5, anchorOf(was, true)));

        Build after;
        ensure("rebuilds", after.run(edit.text()));
        const LLRect now = after.rectOf("target");
        ensure_equals("wider by the delta: " + describe(now), now.getWidth(), was.getWidth() + 10);
        ensure_equals("taller by the delta: " + describe(now), now.getHeight(), was.getHeight() + 5);
        ensure_equals("the left edge stayed", now.mLeft, was.mLeft);
        ensure_equals("the top edge stayed", now.mTop, was.mTop);
    }

    // The round trip: a file read and written with no edit is the file.
    template<> template<>
    void alxuiedit_object::test<4>()
    {
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(raw()));
        ensure_equals("byte for byte", edit.text(), raw());
        ensure("nothing to save", !edit.dirty());
    }

    // One attribute changed changes its own bytes and no others.
    template<> template<>
    void alxuiedit_object::test<5>()
    {
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(raw()));
        ensure("sets", edit.setAttribute({ "target" }, "left", "25"));
        ensure_equals("only the value moved", edit.text(), replaced(raw(), "left=\"20\"", "left=\"25\""));
        ensure("dirty", edit.dirty());

        // A value with a character XML spells differently is written
        // spelled that way.
        ALXUIEdit second;
        ensure("loads", second.loadBuffer(raw()));
        ensure("sets", second.setAttribute({}, "label", "Tom & Jerry & Co"));
        ensure_equals("escaped as it is read", second.text(),
                      replaced(raw(), "label=\"Tom &amp; Jerry\"", "label=\"Tom &amp; Jerry &amp; Co\""));
    }

    // A removed attribute takes the whitespace before it, which is the
    // line it was written on.
    template<> template<>
    void alxuiedit_object::test<6>()
    {
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(raw()));
        ensure("removes", edit.removeAttribute({ "target" }, "top"));
        ensure_equals("its line and nothing else", edit.text(), replaced(raw(), "\r\n     top=\"10\"", ""));
        ensure("an attribute that is not there is not an edit", !edit.removeAttribute({ "target" }, "top"));
    }

    // Text is written into the element that has it, and into one that has
    // none by opening the tag that was closing itself.
    template<> template<>
    void alxuiedit_object::test<9>()
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <text name=\"a\">Hello</text>\n"
            "    <text name=\"b\" />\n"
            "</panel>\n";
        {
            ALXUIEdit edit;
            ensure("loads", edit.loadBuffer(source));
            ensure("sets", edit.setText({ "a" }, "Bonjour"));
            ensure_equals("the text and nothing else", edit.text(), replaced(source, ">Hello<", ">Bonjour<"));
        }
        {
            ALXUIEdit edit;
            ensure("loads", edit.loadBuffer(source));
            ensure("sets", edit.setText({ "b" }, "Bonjour"));
            ensure_equals("the tag opens for it", edit.text(),
                          replaced(source, "<text name=\"b\" />", "<text name=\"b\">Bonjour</text>"));
        }
        {
            // What XML spells differently is spelled that way.
            ALXUIEdit edit;
            ensure("loads", edit.loadBuffer(source));
            ensure("sets", edit.setText({ "a" }, "Salt & Pepper"));
            ensure_equals("escaped", edit.text(), replaced(source, ">Hello<", ">Salt &amp; Pepper<"));
        }
    }

    // An element is written as the last child of its parent, on its own
    // line, indented the way the children already there are.
    template<> template<>
    void alxuiedit_object::test<10>()
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "\t<panel name=\"inner\">\n"
            "\t\t<text name=\"a\">Hello</text>\n"
            "\t</panel>\n"
            "</panel>\n";
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(source));
        ensure("inserts", edit.insertElement({ "inner" }, "<text name=\"b\">Bonjour</text>"));
        ensure_equals("beside its sibling, indented like it", edit.text(),
                      replaced(source, "</text>\n\t</panel>",
                                       "</text>\n\t\t<text name=\"b\">Bonjour</text>\n\t</panel>"));

        // Into a parent with no children at all, which is where an
        // ancestor chain ends.
        const std::string bare =
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\"/>\n"
            "</panel>\n";
        ALXUIEdit second;
        ensure("loads", second.loadBuffer(bare));
        ensure("inserts", second.insertElement({ "inner" }, "<text name=\"b\"/>"));
        ensure_equals("the tag opens for it", second.text(),
                      replaced(bare, "<panel name=\"inner\"/>",
                                     "<panel name=\"inner\">\n        <text name=\"b\"/>\n    </panel>"));
    }

    // An element moved under another parent leaves the rest of the file
    // as it was, and arrives indented for where it lands.
    template<> template<>
    void alxuiedit_object::test<11>()
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <text name=\"a\">Hello</text>\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"c\">Here</text>\n"
            "    </panel>\n"
            "</panel>\n";
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(source));
        ensure("moves", edit.moveElement({ "a" }, { "inner" }));
        ensure_equals("under its new parent, and gone from where it was", edit.text(),
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"c\">Here</text>\n"
            "        <text name=\"a\">Hello</text>\n"
            "    </panel>\n"
            "</panel>\n");
    }

    // An element removed takes its own line with it, and its children.
    template<> template<>
    void alxuiedit_object::test<12>()
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"c\">Here</text>\n"
            "    </panel>\n"
            "    <text name=\"a\">Hello</text>\n"
            "</panel>\n";
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(source));
        ensure("removes", edit.removeElement({ "inner" }));
        ensure_equals("with everything under it", edit.text(),
            "<panel name=\"root\">\n"
            "    <text name=\"a\">Hello</text>\n"
            "</panel>\n");
        ensure("an element that is not there is not an edit", !edit.removeElement({ "inner" }));
    }

    // The extent of an element is the whole of it: a comment, a quoted
    // angle bracket and a child of the same tag are all inside it.
    template<> template<>
    void alxuiedit_object::test<13>()
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
        ensure("loads", edit.loadBuffer(source));
        ensure("removes", edit.removeElement({ "a" }));
        ensure_equals("all of it and no more", edit.text(),
            "<panel name=\"root\">\n"
            "    <text name=\"z\">End</text>\n"
            "</panel>\n");
    }

    // The scan that finds an attribute's bytes, against every attribute of
    // every file the viewer ships: an offset off by one anywhere would
    // write over the wrong text.
    template<> template<>
    void alxuiedit_object::test<8>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALXUICatalog catalog;
        catalog.scan(gDirUtilp->getSkinBaseDir());
        ensure("the skins are there", !catalog.entries().empty());

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
        ensure("every file was read", files > 100);
        ensure_equals("every attribute found where it is written: " + wrong, wrong, std::string());
    }

    // A move that would take a positive edge past zero would re-anchor
    // the element to the other side of its parent, which is a jump and
    // not a move.
    template<> template<>
    void alxuiedit_object::test<7>()
    {
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(raw()));
        ALXUIEdit::Anchor now;
        now.left = 20;
        now.top = 10;
        now.width = 40;
        now.height = 20;
        ensure("refused", !edit.translate({ "target" }, -25, 0, now));
        ensure("said why", !edit.error().empty());
        ensure_equals("and wrote nothing", edit.text(), raw());
    }
}
