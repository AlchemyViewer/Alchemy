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
#include "llfocusmgr.h"
#include "llscrolllistcell.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"

#include "../test/lltut.h"

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

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

    typedef test_group<alscriptreferencespane_data> alscriptreferencespane_group;
    typedef alscriptreferencespane_group::object    alscriptreferencespane_object;
    alscriptreferencespane_group                    alscriptreferencespane_instance("alscriptreferencespane");

    template<> template<>
    void alscriptreferencespane_object::test<1>()
    {
        set_test_name("what was found listed under what was looked up, the declaration marked; nothing, with the question it answers");
        ALScriptReferencesPane* refs = pane();
        refs->show(found());
        ensure_equals("the head", head(), std::string("3 places where count stands, in 2 scripts."));
        ensure("the window told", window.pane().counts == 1);
        ensure_equals("each place", lines(), joined(Names{ "A 1:9", "A 2:5", "B 1:9" }));
        const std::vector<LLScrollListItem*> rows = refs->list()->getAllData();
        ensure_equals("the declaration", cell(rows[0], 2), std::string("declaration"));
        ensure_equals("in the script it is declared in only", cell(rows[2], 2), std::string());
        ensure_equals("its text", cell(rows[1], 3), std::string("x = count;"));
        ensure_equals("kept", refs->found().places.size(), size_t(3));
        Found twice = found();
        twice.places.push_back(place("", 0, 20, "integer count; count"));
        refs->show(twice);
        ensure("on the declaration's line, not it", cell(refs->list()->getAllData()[3], 2).empty());

        Found in_b = found();
        in_b.home  = B;
        refs->show(in_b);
        ensure("declared in B: A's first line not it", cell(refs->list()->getAllData()[0], 2).empty());
        refs->forget();
        ensure_equals("nothing", head(), std::string("Nothing looked up yet. Put the caret on a name and press Find References."));
        ensure("no rows", refs->list()->getItemCount() == 0 && refs->found().from.empty());
        ensure("told each time", window.pane().counts == 4);
    }

    template<> template<>
    void alscriptreferencespane_object::test<2>()
    {
        set_test_name("places slid with the edits to what they are in, listed again once the frame is drawn; gone where the name was edited");
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
        ensure_equals("not listed again yet", lines(), joined(Names{ "A 1:9", "A 2:5", "B 1:9" }));
        refs->pump();
        ensure_equals("A's slid, B's not", lines(), joined(Names{ "A 2:9", "A 3:5", "B 1:9" }));
        ensure("the row chosen kept", refs->list()->getFirstSelected() && refs->list()->getFirstSelected()->getValue().asInteger() == 1);
        ensure_equals("the declaration too", refs->found().definition.line, 1);

        b.editor->replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "  " } });
        refs->pump();
        ensure_equals("B's by its path", lines(), joined(Names{ "A 2:9", "A 3:5", "B 1:11" }));
        ensure_equals("its line's words", refs->found().places[2].text, std::string("foo() { count; }"));

        a.editor->replaceAll({ { ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)), "total" } });
        refs->pump();
        ensure_equals("the declaration edited: gone", lines(), joined(Names{ "A 3:5", "B 1:11" }));
        ensure("not declared any more", !refs->found().hasDefinition);

        a.editor->replaceAll({ { ALTextRange(ALTextPos(2, 0), ALTextPos(2, 1)), "yy" } });
        refs->pump();
        ensure_equals("a place on the line edited", lines(), joined(Names{ "A 3:6", "B 1:11" }));
        ensure_equals("its words again", refs->found().places[0].text, std::string("yy = count;"));
        const S32 counts = window.pane().counts;
        refs->pump();
        ensure_equals("not listed again for nothing", window.pane().counts, counts);

        a.editor->replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "\n" } });
        a.editor->replaceAll({ { ALTextRange(ALTextPos(4, 0), ALTextPos(4, 0)), "z" } });
        refs->pump();
        ensure_equals("moved, not on a line edited; then an edit that moves nothing", lines(), joined(Names{ "A 4:6", "B 1:11" }));
    }

    template<> template<>
    void alscriptreferencespane_object::test<3>()
    {
        set_test_name("a row chosen gone to, walked or taken to the editor; the tab it was looked up from followed to its new id");
        ALScriptReferencesPane* refs = pane();
        refs->show(found());
        refs->choose(false);
        ensure("nothing chosen, nothing gone to", window.pane().chosen.empty());
        // A row is known by its place's number, counted from one.
        refs->list()->selectByValue(LLSD(3));
        refs->list()->onCommit();
        refs->choose(true);
        ensure_equals("gone to", joined(window.pane().chosen), joined(Names{ "a|" + B + "|0", "a|" + B + "|0 editor" }));
        refs->rekey("elsewhere", "z");
        ensure_equals("another's id", refs->found().from, std::string("a"));
        refs->rekey("a", "a2");
        refs->list()->selectByValue(LLSD(1));
        refs->choose(false);
        ensure_equals("followed", window.pane().chosen.back(), std::string("a2||0"));

        // The list's keys: return goes to the place chosen, to type there,
        // escape back to the script; and the rows copy.
        ensure("return", refs->list()->handleKeyHere(KEY_RETURN, MASK_NONE) && window.pane().chosen.back() == "a2||0 editor");
        const size_t gone = window.pane().chosen.size();
        ensure("escape", refs->list()->handleKeyHere(KEY_ESCAPE, MASK_NONE) && window.services().reveals.back()
                             && window.pane().chosen.size() == gone);
        ensure("copied", refs->list()->handleKeyHere('C', MASK_CONTROL));
    }

    template<> template<>
    void alscriptreferencespane_object::test<4>()
    {
        set_test_name("sorted by a column, then by script and place: by text, by script the other way, the declaration first");
        ALScriptReferencesPane* refs = pane();
        Found                   many = found();
        many.places.push_back(place("", 2, 0, "count = 0;"));
        refs->show(many);
        ALPaneList* list = refs->list();
        list->sortByColumn("text", true);
        list->updateSort();
        ensure_equals("by text", lines(), joined(Names{ "A 3:1", "B 1:9", "A 1:9", "A 2:5" }));
        list->sortByColumn("where", false);
        list->updateSort();
        ensure_equals("by script, then place, the other way", lines(), joined(Names{ "B 1:9", "A 3:1", "A 2:5", "A 1:9" }));
        list->sortByColumn("role", true);
        list->updateSort();
        ensure_equals("the declaration first, then by script", lines(), joined(Names{ "A 1:9", "A 2:5", "A 3:1", "B 1:9" }));
        many.home = B;
        refs->show(many);
        list->sortByColumn("role", true);
        list->updateSort();
        ensure_equals("the declaration first, then by script", lines(), joined(Names{ "B 1:9", "A 1:9", "A 2:5", "A 3:1" }));
    }

    template<> template<>
    void alscriptreferencespane_object::test<5>()
    {
        set_test_name("a row picked between an edit that took a place away and the listing again is still its own place");
        ALScriptReferencesPane* refs = pane();
        Doc&                    a    = tab("a", "integer count;\nx = count;\n");
        refs->show(found());
        boost::signals2::scoped_connection in_a(a.editor->document().onChanged(
            [refs, &a](const ALTextDocument::Edit& edit) { refs->slide(a, "object:1:a", edit); }));
        refs->list()->selectFirstItem();
        refs->list()->selectNextItem(false);
        ensure_equals("A's second chosen", cell(refs->list()->getFirstSelected(), 1), std::string("2:5"));
        // The declaration's name edited: the first place goes, not listed
        // again until the frame is drawn.
        a.editor->replaceAll({ { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), "total" } });
        ensure_equals("one place fewer", refs->found().places.size(), size_t(2));
        window.pane().chosen.clear();
        refs->choose(false);
        ensure_equals("A's second, not what took its index", joined(window.pane().chosen), std::string("a||1"));
        refs->pump();
        ensure_equals("listed again, the row still chosen", cell(refs->list()->getFirstSelected(), 1), std::string("2:5"));
    }

    template<> template<>
    void alscriptreferencespane_object::test<6>()
    {
        set_test_name("a rename previewed: a box by each place and its line as renamed, Rename with the places kept, Space and a box leaving one out, Cancel listing them as found");
        ALScriptReferencesPane* refs = pane();
        std::vector<size_t>     made;
        bool                    applied = false;
        refs->preview(found(), "total", "Rename 3 places", [&](const std::vector<size_t>& kept) {
            made    = kept;
            applied = true;
        });
        ensure("previewing", refs->previewing());
        ensure_equals("what it will do over them", head(), std::string("Rename 3 places"));
        LLButton* rename = window.find<LLButton>("references_rename");
        ensure("its buttons shown", rename && rename->getVisible() && window.find<LLButton>("references_cancel")->getVisible());
        const std::vector<LLScrollListItem*> rows = refs->list()->getAllData();
        ensure("each checked", rows.size() == 3 && std::all_of(rows.begin(), rows.end(), [](const LLScrollListItem* row) {
                   return row->getColumn(2)->getValue().asBoolean();
               }));
        ensure_equals("the line as it would read", cell(rows[1], 3), std::string("x = total;"));

        // Left out by Space on the row chosen: its box clear, its line as it is.
        refs->list()->selectNthItem(1);
        refs->list()->handleUnicodeCharHere(' ');
        ensure("left out", !refs->list()->getAllData()[1]->getColumn(2)->getValue().asBoolean());
        ensure_equals("its line as it is", cell(refs->list()->getAllData()[1], 3), std::string("x = count;"));
        refs->setKept(2, false);
        refs->setKept(2, true);
        rename->onCommit();
        ensure("made at the rest, by their order in what was found", applied && made == std::vector<size_t>({ 0, 2 }));
        ensure("done previewing", !refs->previewing() && !rename->getVisible());
        ensure_equals("listed as found again", head().find("total"), std::string::npos);

        applied = false;
        refs->preview(found(), "total", "Rename 3 places", [&](const std::vector<size_t>&) { applied = true; });
        window.find<LLButton>("references_cancel")->onCommit();
        ensure("Cancel makes nothing", !applied && !refs->previewing());
        ensure("each row as found", cell(refs->list()->getAllData()[0], 3) == "integer count;" && refs->list()->getAllData()[0]->getColumn(2)->getValue().asString() != "1");
    }

    template<> template<>
    void alscriptreferencespane_object::test<7>()
    {
        set_test_name("places slid out of sight are listed again once the list is seen, not before");
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
        ensure_equals("slid, not listed again unseen", lines(), joined(Names{ "A 1:9", "A 2:5", "B 1:9" }));
        ensure_equals("the places themselves slid", refs->found().places[0].span.line, 1);
        tabs->selectTabByName("references_tab");
        refs->pump();
        ensure_equals("seen: listed again", lines(), joined(Names{ "A 2:9", "A 3:5", "B 1:9" }));
    }

    template<> template<>
    void alscriptreferencespane_object::test<8>()
    {
        set_test_name("a place slid is the same row, made again where it says something else; a thousand places listed at most, the rest counted");
        ALScriptReferencesPane* refs = pane();
        Doc&                    a    = tab("a", "integer count;\nx = count;\n");
        tab("b", "foo() { count; }\n");
        refs->show(found());
        boost::signals2::scoped_connection in_a(
            a.editor->document().onChanged([refs, &a](const ALTextDocument::Edit& edit) { refs->slide(a, "object:1:a", edit); }));
        LLScrollListItem* in_b     = refs->list()->rowWithKey("3");
        LLScrollListCell* b_line   = in_b ? in_b->getColumn(1) : nullptr;
        LLScrollListItem* second   = refs->list()->rowWithKey("2");
        refs->list()->selectByValue(LLSD(2));
        a.editor->replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "// one\n" } });
        refs->pump();
        ensure_equals("slid", lines(), joined(Names{ "A 2:9", "A 3:5", "B 1:9" }));
        ensure("the same rows", refs->list()->rowWithKey("2") == second && refs->list()->rowWithKey("3") == in_b);
        ensure("one not moved, its cells as they were", in_b->getColumn(1) == b_line);
        ensure("the row chosen kept", refs->list()->getFirstSelected() == second);

        Found many;
        many.from     = "a";
        many.fromName = "A";
        many.name     = "count";
        for (S32 i = 0; i < 1003; ++i)
        {
            many.places.push_back(place("", i, 0, "count;"));
        }
        refs->show(many);
        ensure_equals("a thousand and one row saying the rest", refs->list()->getItemCount(), 1001);
        ensure_equals("the rest", cell(refs->list()->getAllData().back(), 3), window.services().counted("ReferencesUnlisted", 3));
        ensure("all of them counted over the list", head().find("1003") != std::string::npos);
    }
}
