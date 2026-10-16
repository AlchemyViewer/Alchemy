/**
 * @file alscriptreferencespane_test.cpp
 * @brief Script Studio's References tab over the studio's own window, the window's side faked.
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

#include "../alscriptreferencespane.h"

#include "alpanelist.h"
#include "alscriptstudio_fixture.h"
#include "lltabcontainer.h"
#include "llbutton.h"
#include "llclipboard.h"
#include "llfocusmgr.h"
#include "llscrolllistcell.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    typedef ALScriptStudioDoc              Doc;
    typedef ALScriptReferencesPane::Found  Found;
    typedef std::vector<std::string>       Names;

    // The window's side of the tab, faked: a record of what it was told.
    struct FakeRefsWindow : public ALScriptReferencesPane::Window
    {
        void referenceChosen(const Found& found, const Doc::Place& place, bool to_editor) override
        {
            chosen.push_back(found.from + "|" + place.file + "|" + std::to_string(place.span.line) + (to_editor ? " editor" : ""));
        }
        void referencesCounted() override { ++counts; }

        Names chosen;
        S32   counts = 0;
    };

    ALScriptSpan span(S32 line, S32 column, S32 length)
    {
        ALScriptSpan out;
        out.line      = line;
        out.column    = column;
        out.endLine   = line;
        out.endColumn = column + length;
        return out;
    }
    Doc::Place place(const std::string& file, S32 line, S32 column, const std::string& text, const std::string& file_name = "")
    {
        Doc::Place out;
        out.span     = span(line, column, 5);
        out.file     = file;
        out.fileName = file_name;
        out.text     = text;
        out.at       = column;
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
    std::string cell(const LLScrollListItem* item, S32 column)
    {
        return item && item->getColumn(column) ? item->getColumn(column)->getValue().asString() : std::string();
    }
}

namespace
{
    struct alscriptreferencespane_data
    {
        al_studio_test::StudioWindowOf<FakeRefsWindow> window;
        const std::string                              B = "object:1:b";

        ~alscriptreferencespane_data() { gFocusMgr.setKeyboardFocus(nullptr); }
        ALScriptReferencesPane* pane()
        {
            ALScriptReferencesPane* found = window.find<ALScriptReferencesPane>("references_tab");
            if (!found)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            // In sight, its tab the one chosen, as when it is worked in.
            window.find<LLTabContainer>("bottom_tabs")->selectTabByName("references_tab");
            return found;
        }
        std::string head() { return window.find<LLTextBox>("references_head")->getText(); }
        // Each row's script and place, in the list's order.
        std::string lines()
        {
            Names out;
            for (const LLScrollListItem* item : pane()->list()->getAllData())
            {
                out.push_back(cell(item, 0) + " " + cell(item, 1));
            }
            return joined(out);
        }
        // What `count` was found at: twice in A, where it is declared, and
        // once in B.
        Found found()
        {
            Found out;
            out.from          = "a";
            out.fromName      = "A";
            out.name          = "count";
            out.places        = { place("", 0, 8, "integer count;"), place("", 1, 4, "x = count;"),
                                  place(B, 0, 8, "foo() { count; }", "B") };
            out.hasDefinition = true;
            out.definition    = span(0, 8, 5);
            return out;
        }
        Doc& tab(const std::string& id, const std::string& text)
        {
            Doc& doc = window.services().addDoc(id);
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name     = "editor_" + id;
            p.rect     = LLRect(0, 200, 400, 0);
            doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(doc.editor);
            doc.editor->setText(text);
            return doc;
        }
    };

    TEST_CASE_METHOD(alscriptreferencespane_data, "alscriptreferencespane: show lists places and marks the declaration and forget clears them", "[alscriptreferencespane]")
    {
        ALScriptReferencesPane* refs = pane();
        refs->show(found());
        CHECK(head() == std::string("3 occurrences of count in 2 scripts."));
        UNSCOPED_INFO("the window told");
        CHECK(window.pane().counts == 1);
        CHECK(lines() == joined(Names{ "A 1:9", "A 2:5", "B 1:9" }));
        const std::vector<LLScrollListItem*> rows = refs->list()->getAllData();
        REQUIRE(rows.size() == size_t(3));
        CHECK(cell(rows[0], 2) == std::string("declaration"));
        UNSCOPED_INFO("in the script it is declared in only");
        CHECK(cell(rows[2], 2) == std::string());
        UNSCOPED_INFO("its text");
        CHECK(cell(rows[1], 3) == std::string("x = count;"));
        UNSCOPED_INFO("kept");
        CHECK(refs->found().places.size() == size_t(3));
        Found twice = found();
        twice.places.push_back(place("", 0, 20, "integer count; count"));
        refs->show(twice);
        REQUIRE(refs->list()->getItemCount() == 4);
        UNSCOPED_INFO("on the declaration's line, not it");
        CHECK(cell(refs->list()->getAllData()[3], 2).empty());

        Found in_b = found();
        in_b.home  = B;
        refs->show(in_b);
        REQUIRE(refs->list()->getItemCount() == 3);
        UNSCOPED_INFO("declared in B: A's first line not it");
        CHECK(cell(refs->list()->getAllData()[0], 2).empty());
        refs->forget();
        CHECK(head() == std::string("Nothing to show yet. Put the caret on a name and choose Find References."));
        CHECK(refs->list()->getItemCount() == 0);
        CHECK(refs->found().from.empty());
        UNSCOPED_INFO("told each time");
        CHECK(window.pane().counts == 4);
    }

    TEST_CASE_METHOD(alscriptreferencespane_data, "alscriptreferencespane: places slide with their script's edits and go where the name is edited", "[alscriptreferencespane]")
    {
        ALScriptReferencesPane* refs = pane();
        Doc&                    a    = tab("a", "integer count;\nx = count;\n");
        Doc&                    b    = tab("b", "foo() { count; }\n");
        refs->show(found());
        const auto slid = [&](Doc& doc, const std::string& path) {
            return doc.editor->document().onChanged(
                [refs, &doc, path](const ALTextDocument::Edit& edit) { refs->slide(doc, path, edit); });
        };
        Doc&                               c = tab("c", "integer other;\n");
        boost::signals2::scoped_connection in_a(slid(a, "object:1:a"));
        boost::signals2::scoped_connection in_b(slid(b, B));
        boost::signals2::scoped_connection in_c(slid(c, "object:1:c"));
        refs->list()->selectByValue(LLSD(1));

        c.editor->replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "// one\n" } });
        a.editor->replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "// one\n" } });
        UNSCOPED_INFO("not listed again yet");
        CHECK(lines() == joined(Names{ "A 1:9", "A 2:5", "B 1:9" }));
        refs->pump();
        UNSCOPED_INFO("A's slid, B's not");
        CHECK(lines() == joined(Names{ "A 2:9", "A 3:5", "B 1:9" }));
        {
            INFO("the row chosen kept");
            REQUIRE(refs->list()->getFirstSelected());
            CHECK(refs->list()->getFirstSelected()->getValue().asInteger() == 1);
        }
        UNSCOPED_INFO("the declaration too");
        CHECK(refs->found().definition.line == 1);

        b.editor->replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "  " } });
        refs->pump();
        UNSCOPED_INFO("B's by its path");
        CHECK(lines() == joined(Names{ "A 2:9", "A 3:5", "B 1:11" }));
        REQUIRE(refs->found().places.size() == size_t(3));
        UNSCOPED_INFO("its line's words");
        CHECK(refs->found().places[2].text == std::string("foo() { count; }"));

        a.editor->replaceAll({ { ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)), "total" } });
        refs->pump();
        UNSCOPED_INFO("the declaration edited: gone");
        CHECK(lines() == joined(Names{ "A 3:5", "B 1:11" }));
        UNSCOPED_INFO("not declared any more");
        CHECK_FALSE(refs->found().hasDefinition);

        a.editor->replaceAll({ { ALTextRange(ALTextPos(2, 0), ALTextPos(2, 1)), "yy" } });
        refs->pump();
        UNSCOPED_INFO("a place on the line edited");
        CHECK(lines() == joined(Names{ "A 3:6", "B 1:11" }));
        REQUIRE_FALSE(refs->found().places.empty());
        UNSCOPED_INFO("its words again");
        CHECK(refs->found().places[0].text == std::string("yy = count;"));
        const S32 counts = window.pane().counts;
        refs->pump();
        UNSCOPED_INFO("not listed again for nothing");
        CHECK(window.pane().counts == counts);

        a.editor->replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "\n" } });
        a.editor->replaceAll({ { ALTextRange(ALTextPos(4, 0), ALTextPos(4, 0)), "z" } });
        refs->pump();
        UNSCOPED_INFO("moved, not on a line edited; then an edit that moves nothing");
        CHECK(lines() == joined(Names{ "A 4:6", "B 1:11" }));
    }

    TEST_CASE_METHOD(alscriptreferencespane_data, "alscriptreferencespane: choosing a row goes to its place and follows its tab's new id", "[alscriptreferencespane]")
    {
        ALScriptReferencesPane* refs = pane();
        refs->show(found());
        refs->choose(false);
        UNSCOPED_INFO("nothing chosen, nothing gone to");
        CHECK(window.pane().chosen.empty());
        // A row is known by its place's number, counted from one.
        refs->list()->selectByValue(LLSD(3));
        refs->list()->onCommit();
        refs->choose(true);
        UNSCOPED_INFO("gone to");
        REQUIRE(joined(window.pane().chosen) == joined(Names{ "a|" + B + "|0", "a|" + B + "|0 editor" }));
        refs->rekey("elsewhere", "z");
        UNSCOPED_INFO("another's id");
        CHECK(refs->found().from == std::string("a"));
        refs->rekey("a", "a2");
        refs->list()->selectByValue(LLSD(1));
        refs->choose(false);
        UNSCOPED_INFO("followed");
        CHECK(window.pane().chosen.back() == std::string("a2||0"));

        // The list's keys: return goes to the place chosen, to type there,
        // escape back to the script; and the rows copy.
        CHECK(refs->list()->handleKeyHere(KEY_RETURN, MASK_NONE));
        CHECK(window.pane().chosen.back() == "a2||0 editor");
        const size_t gone = window.pane().chosen.size();
        CHECK(refs->list()->handleKeyHere(KEY_ESCAPE, MASK_NONE));
        REQUIRE_FALSE(window.services().reveals.empty());
        CHECK(window.services().reveals.back());
        CHECK(window.pane().chosen.size() == gone);
        CHECK(refs->list()->handleKeyHere('C', MASK_CONTROL));
        std::string copied;
        LLClipboard::instance().pasteFromClipboard(copied);
        {
            INFO("the row chosen copied, and no other: " + copied);
            CHECK(copied.find("integer count;") != std::string::npos);
            CHECK(copied.find("x = count;") == std::string::npos);
            CHECK(copied.find("foo() { count; }") == std::string::npos);
        }
    }

    TEST_CASE_METHOD(alscriptreferencespane_data, "alscriptreferencespane: rows sort by a column and then by script and place", "[alscriptreferencespane]")
    {
        ALScriptReferencesPane* refs = pane();
        Found                   many = found();
        many.places.push_back(place("", 2, 0, "count = 0;"));
        refs->show(many);
        ALPaneList* list = refs->list();
        list->sortByColumn("text", true);
        list->updateSort();
        CHECK(lines() == joined(Names{ "A 3:1", "B 1:9", "A 1:9", "A 2:5" }));
        list->sortByColumn("where", false);
        list->updateSort();
        UNSCOPED_INFO("by script, then place, the other way");
        CHECK(lines() == joined(Names{ "B 1:9", "A 3:1", "A 2:5", "A 1:9" }));
        list->sortByColumn("role", true);
        list->updateSort();
        UNSCOPED_INFO("the declaration first, then by script");
        CHECK(lines() == joined(Names{ "A 1:9", "A 2:5", "A 3:1", "B 1:9" }));
        many.home = B;
        refs->show(many);
        list->sortByColumn("role", true);
        list->updateSort();
        UNSCOPED_INFO("the declaration first, then by script");
        CHECK(lines() == joined(Names{ "B 1:9", "A 1:9", "A 2:5", "A 3:1" }));
    }

    TEST_CASE_METHOD(alscriptreferencespane_data, "alscriptreferencespane: a row picked before relisting after a place is removed still goes to its place", "[alscriptreferencespane]")
    {
        ALScriptReferencesPane* refs = pane();
        Doc&                    a    = tab("a", "integer count;\nx = count;\n");
        refs->show(found());
        boost::signals2::scoped_connection in_a(a.editor->document().onChanged(
            [refs, &a](const ALTextDocument::Edit& edit) { refs->slide(a, "object:1:a", edit); }));
        refs->list()->selectFirstItem();
        refs->list()->selectNextItem(false);
        UNSCOPED_INFO("A's second chosen");
        CHECK(cell(refs->list()->getFirstSelected(), 1) == std::string("2:5"));
        // The declaration's name edited: the first place goes, not listed
        // again until the frame is drawn.
        a.editor->replaceAll({ { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), "total" } });
        UNSCOPED_INFO("one place fewer");
        CHECK(refs->found().places.size() == size_t(2));
        window.pane().chosen.clear();
        refs->choose(false);
        UNSCOPED_INFO("A's second, not what took its index");
        CHECK(joined(window.pane().chosen) == std::string("a||1"));
        refs->pump();
        UNSCOPED_INFO("listed again, the row still chosen");
        CHECK(cell(refs->list()->getFirstSelected(), 1) == std::string("2:5"));
    }

    TEST_CASE_METHOD(alscriptreferencespane_data, "alscriptreferencespane: a rename preview checks each place and renames only those kept", "[alscriptreferencespane]")
    {
        ALScriptReferencesPane* refs = pane();
        std::vector<size_t>     made;
        bool                    applied = false;
        refs->preview(found(), "total", "Rename 3 places", [&](const std::vector<size_t>& kept) {
            made    = kept;
            applied = true;
        });
        CHECK(refs->previewing());
        UNSCOPED_INFO("what it will do over them");
        CHECK(head() == std::string("Rename 3 places"));
        LLButton* rename = window.find<LLButton>("references_rename");
        {
            INFO("its buttons shown");
            REQUIRE(rename);
            CHECK(rename->getVisible());
            CHECK(window.find<LLButton>("references_cancel")->getVisible());
        }
        const std::vector<LLScrollListItem*> rows = refs->list()->getAllData();
        {
            INFO("each checked");
            REQUIRE(rows.size() == 3);
            CHECK(std::all_of(rows.begin(), rows.end(), [](const LLScrollListItem* row) {
                   return row->getColumn(2)->getValue().asBoolean();
               }));
        }
        UNSCOPED_INFO("the line as it would read");
        CHECK(cell(rows[1], 3) == std::string("x = total;"));

        // Left out by Space on the row chosen: its box clear, its line as it is.
        refs->list()->selectNthItem(1);
        refs->list()->handleUnicodeCharHere(' ');
        REQUIRE(refs->list()->getItemCount() == 3);
        UNSCOPED_INFO("left out");
        CHECK_FALSE(refs->list()->getAllData()[1]->getColumn(2)->getValue().asBoolean());
        UNSCOPED_INFO("its line as it is");
        CHECK(cell(refs->list()->getAllData()[1], 3) == std::string("x = count;"));
        refs->setKept(2, false);
        refs->setKept(2, true);
        rename->onCommit();
        {
            INFO("made at the rest, by their order in what was found");
            CHECK(applied);
            CHECK(made == std::vector<size_t>({ 0, 2 }));
        }
        {
            INFO("done previewing");
            CHECK_FALSE(refs->previewing());
            CHECK_FALSE(rename->getVisible());
        }
        UNSCOPED_INFO("listed as found again");
        CHECK(head().find("total") == std::string::npos);

        applied = false;
        refs->preview(found(), "total", "Rename 3 places", [&](const std::vector<size_t>&) { applied = true; });
        UNSCOPED_INFO("no Show Changes where none is given");
        CHECK_FALSE(window.find<LLButton>("references_changes")->getVisible());
        window.find<LLButton>("references_cancel")->onCommit();
        {
            INFO("Cancel makes nothing");
            CHECK_FALSE(applied);
            CHECK_FALSE(refs->previewing());
        }

        // Show Changes: the chosen row's file, or the script's own with
        // none chosen, and the places kept.
        std::vector<std::string> files;
        std::vector<size_t>      shown;
        refs->preview(found(), "total", "Rename 3 places", [](const std::vector<size_t>&) {},
                      [&](const std::string& file, const std::vector<size_t>& kept) {
                          files.push_back(file);
                          shown = kept;
                      });
        LLButton* changes = window.find<LLButton>("references_changes");
        {
            INFO("Show Changes shown");
            REQUIRE(changes);
            CHECK(changes->getVisible());
        }
        changes->onCommit();
        refs->list()->selectNthItem(2);
        refs->setKept(1, false);
        refs->list()->selectNthItem(2);
        changes->onCommit();
        UNSCOPED_INFO("its own, then the chosen row's");
        CHECK(files == std::vector<std::string>({ std::string(), B }));
        UNSCOPED_INFO("with the places kept");
        CHECK(shown == std::vector<size_t>({ 0, 2 }));
        window.find<LLButton>("references_cancel")->onCommit();
        UNSCOPED_INFO("gone with the preview");
        CHECK_FALSE(changes->getVisible());
        {
            INFO("each row as found");
            REQUIRE(refs->list()->getItemCount() == 3);
            CHECK(cell(refs->list()->getAllData()[0], 3) == "integer count;");
            CHECK(refs->list()->getAllData()[0]->getColumn(2)->getValue().asString() != "1");
        }
    }

    TEST_CASE_METHOD(alscriptreferencespane_data, "alscriptreferencespane: places slid out of sight are listed again once the list is seen", "[alscriptreferencespane]")
    {
        ALScriptReferencesPane* refs = pane();
        Doc&                    a    = tab("a", "integer count;\nx = count;\n");
        tab("b", "foo() { count; }\n");
        refs->show(found());
        boost::signals2::scoped_connection in_a(
            a.editor->document().onChanged([refs, &a](const ALTextDocument::Edit& edit) { refs->slide(a, "object:1:a", edit); }));
        LLTabContainer* tabs = window.find<LLTabContainer>("bottom_tabs");
        tabs->selectTabByName("problems_tab");
        a.editor->replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "// one\n" } });
        refs->pump();
        UNSCOPED_INFO("slid, not listed again unseen");
        CHECK(lines() == joined(Names{ "A 1:9", "A 2:5", "B 1:9" }));
        REQUIRE_FALSE(refs->found().places.empty());
        UNSCOPED_INFO("the places themselves slid");
        CHECK(refs->found().places[0].span.line == 1);
        tabs->selectTabByName("references_tab");
        refs->pump();
        UNSCOPED_INFO("seen: listed again");
        CHECK(lines() == joined(Names{ "A 2:9", "A 3:5", "B 1:9" }));
    }

    TEST_CASE_METHOD(alscriptreferencespane_data, "alscriptreferencespane: a slid place keeps its row and at most a thousand places are listed", "[alscriptreferencespane]")
    {
        ALScriptReferencesPane* refs = pane();
        Doc&                    a    = tab("a", "integer count;\nx = count;\n");
        tab("b", "foo() { count; }\n");
        refs->show(found());
        boost::signals2::scoped_connection in_a(
            a.editor->document().onChanged([refs, &a](const ALTextDocument::Edit& edit) { refs->slide(a, "object:1:a", edit); }));
        LLScrollListItem* in_b     = refs->list()->rowWithKey("3");
        REQUIRE(in_b);
        LLScrollListCell* b_line   = in_b->getColumn(1);
        LLScrollListItem* second   = refs->list()->rowWithKey("2");
        CHECK(second);
        refs->list()->selectByValue(LLSD(2));
        a.editor->replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "// one\n" } });
        refs->pump();
        UNSCOPED_INFO("slid");
        CHECK(lines() == joined(Names{ "A 2:9", "A 3:5", "B 1:9" }));
        {
            INFO("the same rows");
            CHECK(refs->list()->rowWithKey("2") == second);
            CHECK(refs->list()->rowWithKey("3") == in_b);
        }
        UNSCOPED_INFO("one not moved, its cells as they were");
        CHECK(in_b->getColumn(1) == b_line);
        UNSCOPED_INFO("the row chosen kept");
        CHECK(refs->list()->getFirstSelected() == second);

        Found many;
        many.from     = "a";
        many.fromName = "A";
        many.name     = "count";
        for (S32 i = 0; i < 1003; ++i)
        {
            many.places.push_back(place("", i, 0, "count;"));
        }
        refs->show(many);
        UNSCOPED_INFO("a thousand and one row saying the rest");
        REQUIRE(refs->list()->getItemCount() == 1001);
        CHECK(cell(refs->list()->getAllData().back(), 3) == window.services().counted("ReferencesUnlisted", 3));
        UNSCOPED_INFO("all of them counted over the list");
        CHECK(head().find("1003") != std::string::npos);
    }
}
