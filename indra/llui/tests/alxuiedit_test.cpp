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

#include "../alxuiselection.h"

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

    // Order is what a menu is, so an element arrives beside a sibling and
    // takes that sibling's own indentation.
    template<> template<>
    void alxuiedit_object::test<14>()
    {
        const std::string source =
            "<menu name=\"root\">\n"
            "    <item name=\"a\"/>\n"
            "    <item name=\"b\"/>\n"
            "</menu>\n";
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(source));
        ensure("before", edit.insertBefore({ "b" }, "<item name=\"x\"/>"));
        ensure("after", edit.insertAfter({ "a" }, "<item name=\"y\"/>"));
        ensure_equals("each where it was asked for", edit.text(),
            "<menu name=\"root\">\n"
            "    <item name=\"a\"/>\n"
            "    <item name=\"y\"/>\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"b\"/>\n"
            "</menu>\n");
    }

    // A reorder among siblings, which is a removal and an insertion, and
    // the sibling it lands beside is named as the tree stood before it.
    template<> template<>
    void alxuiedit_object::test<15>()
    {
        const std::string source =
            "<menu name=\"root\">\n"
            "    <item name=\"a\"/>\n"
            "    <item name=\"b\"/>\n"
            "    <item name=\"c\"/>\n"
            "</menu>\n";
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(source));
        ensure("last goes first", edit.moveBefore({ "c" }, { "a" }));
        ensure_equals("in that order", edit.text(),
            "<menu name=\"root\">\n"
            "    <item name=\"c\"/>\n"
            "    <item name=\"a\"/>\n"
            "    <item name=\"b\"/>\n"
            "</menu>\n");
        ensure("and not beside itself", !edit.moveAfter({ "a" }, { "a" }));
    }

    // Siblings of one name are told apart by which of them they are, so
    // taking one away renumbers the ones after it -- including the one the
    // move is going to land beside.
    template<> template<>
    void alxuiedit_object::test<16>()
    {
        const std::string source =
            "<menu name=\"root\">\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"x\"/>\n"
            "    <item name=\"last\"/>\n"
            "</menu>\n";
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(source));

        // The ordinal is the index counted from zero, so x#2 is the third
        // of them. Without the renumbering it would land beside itself.
        ensure("the first goes after the third", edit.moveAfter({ "x" }, { "x#2" }));
        ensure_equals("three of them still, in the order asked for", edit.text(),
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
        ensure_equals("the one after it counts one fewer", after[0], std::string("x#1"));
        ensure_equals("and so does a path through it", deeper[0], std::string("x#1"));
        ensure_equals("the one that was removed keeps its own step", second[0], std::string("x#1"));
        ensure_equals("another name is untouched", elsewhere[0], std::string("other#2"));
        ensure_equals("and so is the one before it", earlier[0], std::string("x"));
    }

    // A step is one operation as a caller asked for it, whatever it is
    // made of, and undoing it puts the file back byte for byte.
    template<> template<>
    void alxuiedit_object::test<17>()
    {
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(raw()));
        ensure("nothing to undo yet", !edit.canUndo());
        ensure("and it is not dirty", !edit.dirty());

        ensure("one", edit.setAttribute({ "target" }, "left", "30"));
        ensure("two", edit.setAttribute({ "target" }, "top", "40"));
        ensure_equals("two steps", edit.undoDepth(), 2u);
        ensure("dirty", edit.dirty());

        ensure("undo", edit.undo());
        ensure("undo", edit.undo());
        ensure_equals("back to the file it was", edit.text(), raw());
        ensure("and clean again", !edit.dirty());
        ensure("with nothing left to undo", !edit.canUndo());

        ensure("redo", edit.redo());
        ensure("redo", edit.redo());
        ensure("both back", edit.text().find("left=\"30\"") != std::string::npos
                         && edit.text().find("top=\"40\"") != std::string::npos);

        // A move is a removal and an insertion; one undo is the whole move.
        ensure("undo the redos", edit.undo() && edit.undo());
        ensure("a fresh edit clears what was undone", edit.setAttribute({ "target" }, "left", "5"));
        ensure("nothing to redo", !edit.canRedo());
    }

    // One undo for one move, however many splices it took.
    template<> template<>
    void alxuiedit_object::test<18>()
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
        ensure_equals("one step for the move", edit.undoDepth(), 1u);
        ensure("undo", edit.undo());
        ensure_equals("the whole move came back", edit.text(), source);

        // An operation that fails before it writes leaves no step.
        ensure("no such element", !edit.removeElement({ "nowhere" }));
        ensure("and nothing to undo", !edit.canUndo());
    }

    // Every other form of position comes off. A delta is read before the
    // edge it overrides and a padding is measured from a sibling the
    // element has left, so leaving one behind puts the element somewhere
    // nobody asked for.
    template<> template<>
    void alxuiedit_object::test<19>()
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <panel name=\"a\" left_delta=\"5\" top_pad=\"3\" right=\"90\" bottom=\"10\""
            " left=\"1\" top=\"2\" width=\"9\" height=\"8\" follows=\"left|top\" />\n"
            "</panel>\n";
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(source));

        ALXUIEdit::Anchor want;
        want.left = 30;
        want.top = 40;
        want.width = 50;
        want.height = 60;
        want.topLeft = true;
        ensure("re-authors", edit.reauthor({ "a" }, want));

        const pugi::xml_node node = edit.resolve({ "a" });
        ensure_equals("left", node.attribute("left").as_int(), 30);
        ensure_equals("top", node.attribute("top").as_int(), 40);
        ensure_equals("width", node.attribute("width").as_int(), 50);
        ensure_equals("height", node.attribute("height").as_int(), 60);
        ensure("left_delta is gone", !node.attribute("left_delta"));
        ensure("top_pad is gone", !node.attribute("top_pad"));
        ensure("right is gone", !node.attribute("right"));
        ensure("bottom is gone", !node.attribute("bottom"));
        ensure("what is not a position is untouched",
               std::string_view(node.attribute("follows").value()) == "left|top");

        // And it is one step, however many splices it took.
        ensure_equals("one step", edit.undoDepth(), 1u);
        ensure("undo", edit.undo());
        ensure_equals("the file it was", edit.text(), source);
    }

    // A parent that lays its children out itself is given a size and
    // nothing else, and a parent that counts up from its bottom is given
    // the edge it counts from.
    template<> template<>
    void alxuiedit_object::test<20>()
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
            ensure("loads", edit.loadBuffer(source));
            want.topLeft = true;
            ensure("re-authors as a size", edit.reauthor({ "a" }, want, ALXUIEdit::AUTHOR_SIZE));
            const pugi::xml_node node = edit.resolve({ "a" });
            ensure_equals("width", node.attribute("width").as_int(), 50);
            ensure_equals("height", node.attribute("height").as_int(), 60);
            ensure("no left", !node.attribute("left"));
            ensure("no top", !node.attribute("top"));
        }
        {
            ALXUIEdit edit;
            ensure("loads", edit.loadBuffer(source));
            want.topLeft = false;
            ensure("re-authors from the bottom", edit.reauthor({ "a" }, want));
            const pugi::xml_node node = edit.resolve({ "a" });
            ensure_equals("bottom", node.attribute("bottom").as_int(), 70);
            ensure("and not top", !node.attribute("top"));
        }
    }

    // A step says what it did, so a caller holding something built from this
    // document can put one field of one element onto what it has rather than
    // building it all again. A step that did more than that says so by
    // saying nothing, since there is no one field for the caller to write.
    template<> template<>
    void alxuiedit_object::test<21>()
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <panel name=\"a\" left=\"1\" top=\"2\" width=\"9\" height=\"8\" />\n"
            "</panel>\n";
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(source));

        ensure("writes", edit.setAttribute({ "a" }, "width", "40"));
        ensure("undo", edit.undo());
        ensure("the step wrote one field", edit.lastChange().oneField);
        ensure_equals("and says which", edit.lastChange().field, std::string("width"));
        std::string value;
        ensure("the element writes it still", edit.fieldText({ "a" }, "width", value));
        ensure_equals("with what it read before the step", value, std::string("9"));

        ensure("redo", edit.redo());
        ensure("the same step, the other way", edit.lastChange().oneField);
        ensure("and the field reads what the step wrote",
               edit.fieldText({ "a" }, "width", value));
        ensure_equals("which is what was written", value, std::string("40"));

        // An attribute the file did not carry: undoing its arrival leaves
        // the element writing nothing, and nothing is not a value to put
        // onto anything.
        ensure("adds one", edit.setAttribute({ "a" }, "tool_tip", "hello"));
        ensure("undo", edit.undo());
        ensure("still one field", edit.lastChange().oneField);
        ensure("and the element no longer writes it",
               !edit.fieldText({ "a" }, "tool_tip", value));

        // A name is what a path is made of, so writing one moves the element
        // as far as anything holding a path to it is concerned. A step says
        // where it was and where it now is, and an undo and a redo each say
        // which of the two the element is at.
        {
            ALXUIEdit named;
            ensure("loads", named.loadBuffer(
                "<panel name=\"root\">\n"
                "    <panel name=\"a\" width=\"9\" height=\"8\" />\n"
                "    <panel name=\"b\" width=\"9\" height=\"8\" />\n"
                "</panel>\n"));
            ensure("renames", named.setAttribute({ "b" }, "name", "a"));
            ensure("undo", named.undo());
            ensure_equals("undone, it is where it was",
                          ALXUISelection::toString(named.lastPath()), std::string("b"));
            ensure("redo", named.redo());
            // Its new name is one a sibling before it already carries, so it
            // is the second of that name and the step says so.
            ensure_equals("done again, it is where the step put it",
                          ALXUISelection::toString(named.lastPath()),
                          ALXUISelection::step("a", 1));
        }

        // Renaming as an operation: it says where the element went, and the
        // paths a caller was holding are brought along -- the element's own,
        // one under it, a later sibling of the name it left, and a later
        // sibling of the name it took.
        {
            ALXUIEdit doc;
            ensure("loads", doc.loadBuffer(
                "<panel name=\"root\">\n"
                "    <panel name=\"a\" width=\"9\" height=\"8\" />\n"
                "    <panel name=\"b\" width=\"9\" height=\"8\">\n"
                "        <panel name=\"inner\" width=\"4\" height=\"4\" />\n"
                "    </panel>\n"
                "    <panel name=\"b\" width=\"9\" height=\"8\" />\n"
                "    <panel name=\"a\" width=\"9\" height=\"8\" />\n"
                "</panel>\n"));

            ALXUIEdit::path_t moved;
            ensure("renames", doc.rename({ "b" }, "a", moved));
            ensure_equals("and says where it went", ALXUISelection::toString(moved),
                          ALXUISelection::step("a", 1));

            const auto brought = [&moved](ALXUIEdit::path_t held)
            {
                ALXUIEdit::afterRenaming({ "b" }, moved, held);
                return ALXUISelection::toString(held);
            };
            ensure_equals("a path under it comes along",
                          brought({ "b", "inner" }),
                          ALXUISelection::toString({ ALXUISelection::step("a", 1), "inner" }));
            ensure_equals("the other of the name it left is now the first of them",
                          brought({ ALXUISelection::step("b", 1) }), std::string("b"));
            ensure_equals("and the later of the name it took counts one more",
                          brought({ ALXUISelection::step("a", 1) }),
                          ALXUISelection::step("a", 2));
            ensure_equals("the one before it does not", brought({ "a" }), std::string("a"));
            ensure_equals("nor does another branch entirely",
                          brought({ "elsewhere", "b" }), std::string("elsewhere/b"));
        }

        // A step says what kind of thing it was, so that a caller can say so
        // in its own words -- and says it before the step is taken, since
        // afterwards it has moved to the other stack.
        {
            ALXUIEdit doc;
            ensure("loads", doc.loadBuffer(
                "<panel name=\"root\">\n"
                "    <panel name=\"a\" width=\"9\" height=\"8\">Hello</panel>\n"
                "</panel>\n"));

            ensure("writes", doc.setAttribute({ "a" }, "width", "40"));
            ensure("takes one out", doc.removeAttribute({ "a" }, "height"));
            ensure("writes text", doc.setText({ "a" }, "Goodbye"));
            ensure("adds one", doc.insertElement({ "a" }, "<panel name=\"b\"/>"));
            ensure("takes one out again", doc.removeElement({ "a", "b" }));

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
                ensure("there is a step to take back", next != nullptr);
                ensure("and it says what it was", next->did == did);
                ensure("taken back", doc.undo());
                ensure("which is what was taken back", doc.lastChange().did == did);
            }
            ensure("and then there is not", doc.nextUndo() == nullptr);
            ensure("but there is one to do again", doc.nextRedo() != nullptr);
        }

        // A whole element arriving is not one field of one element.
        const std::string before = edit.text();
        ensure("inserts", edit.insertElement({}, "<panel name=\"b\" width=\"4\" height=\"4\"/>"));
        ensure("undo", edit.undo());
        ensure("which the step does not claim to be", !edit.lastChange().oneField);
        ensure_equals("and the file is what it was", edit.text(), before);
    }

    // An attribute spelt wrongly holds the author's value under a name
    // nothing reads. Taking it out and putting it back would move it to the
    // end of the element and rewrite whatever the file had between the
    // quotes, so only the name is spliced.
    template<> template<>
    void alxuiedit_object::test<22>()
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <!-- a comment nobody may touch -->\n"
            "    <panel name=\"a\"  tool_tp='say &amp; do'  width=\"9\" />\n"
            "</panel>\n";
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(source));

        ensure("spelt again", edit.renameAttribute({ "a" }, "tool_tp", "tool_tip"));
        ensure_equals("only the name moved", edit.text(),
            "<panel name=\"root\">\n"
            "    <!-- a comment nobody may touch -->\n"
            "    <panel name=\"a\"  tool_tip='say &amp; do'  width=\"9\" />\n"
            "</panel>\n");

        // One step, because it is one thing done.
        ensure("says what it did", edit.nextUndo() != nullptr);
        ensure("which is a name written again",
               edit.nextUndo()->did == ALXUIEdit::Did::SpeltFieldAgain);
        ensure_equals("under the name it now has", edit.nextUndo()->field, std::string("tool_tip"));
        ensure("put back", edit.undo());
        ensure_equals("the whole of it", edit.text(), source);
        ensure("and there is nothing else to put back", !edit.canUndo());

        // Two of a name on one element is one value chosen by which the
        // parser reads last, which is not a state to leave a file in.
        ensure("refuses to make a second width",
               !edit.renameAttribute({ "a" }, "tool_tp", "width"));
        ensure("and says why", !edit.error().empty());
        ensure_equals("having changed nothing", edit.text(), source);
        ensure("a name the element does not carry",
               !edit.renameAttribute({ "a" }, "height", "width"));
        ensure("the name it has", !edit.renameAttribute({ "a" }, "width", "width"));
        ensure("no element", !edit.renameAttribute({ "nowhere" }, "width", "height"));
    }

    // An element cannot be moved into itself or beside anything under it.
    // The removal that starts a move used to take the destination with it,
    // and the element, lifted and with nowhere to land, was gone from the
    // file with the step half done; the answer is no, and the file as it was.
    template<> template<>
    void alxuiedit_object::test<23>()
    {
        const std::string source =
            "<panel name=\"root\">\n"
            "    <panel name=\"outer\">\n"
            "        <text name=\"c\">Here</text>\n"
            "    </panel>\n"
            "</panel>\n";
        ALXUIEdit edit;
        ensure("loads", edit.loadBuffer(source));
        ensure("not into itself", !edit.moveElement({ "outer" }, { "outer" }));
        ensure("not into its own child", !edit.moveElement({ "outer" }, { "outer", "c" }));
        ensure("not beside its own child", !edit.moveBefore({ "outer" }, { "outer", "c" }));
        ensure("nor after it", !edit.moveAfter({ "outer" }, { "outer", "c" }));
        ensure_equals("and the file is as it was", edit.text(), source);
        ensure("with nothing to undo", !edit.canUndo());
        ensure("and a reason given", !edit.error().empty());
    }
}
