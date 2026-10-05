/**
 * @file alscriptstudioselections_test.cpp
 * @brief Tests for ALScriptStudioSelections: a selection held, and compared with another.
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

#include "../alscriptstudioselections.h"

#include "aldiffview.h"
#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

namespace
{
    typedef ALScriptStudioDoc Doc;

    // The selections' window, faked: a comparison of the view's own made
    // in the tab's place, as the studio makes one -- nothing taken back,
    // typing handed to the source at the same line and column -- and the
    // places typing was sent to in the source.
    struct FakeSelectionsWindow : public ALScriptStudioSelections::Window
    {
        explicit FakeSelectionsWindow(LLView* host) : host(host) {}

        void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                     const std::string& right_title) override
        {
            did.push_back(left + " | " + right + " (" + left_title + " | " + right_title + ")");
            if (!doc.compareView)
            {
                ALDiffView::Params p(LLUICtrlFactory::getDefaultParams<ALDiffView>());
                p.name          = "compare_" + doc.id;
                p.rect          = LLRect(0, 300, 600, 0);
                doc.compareView = LLUICtrlFactory::create<ALDiffView>(p);
                host->addChild(doc.compareView);
            }
            doc.compareView->setOnTakeBack(nullptr);
            doc.compareView->setOnEdit([this, &doc](S32 line, S32 column) { return typeInSource(doc, ALTextPos(line, column)); });
            doc.compareView->setTexts(left, right);
        }
        LLView* typeInSource(Doc& doc, const ALTextPos& at) override
        {
            typedAt.push_back(at);
            doc.editor->goTo(at);
            return doc.editor;
        }

        LLView*                  host = nullptr;
        std::vector<std::string> did;
        std::vector<ALTextPos>   typedAt;
    };
}

namespace tut
{
    struct alscriptstudioselections_data
    {
        al_studio_test::StudioWindow              window;
        FakeSelectionsWindow                      studio{ window.floater };
        std::unique_ptr<ALScriptStudioSelections> unit = std::make_unique<ALScriptStudioSelections>(window.services(), studio);

        al_studio_test::FakeServices& services() { return window.services(); }
        std::string                   said(const std::string& name, const LLStringUtil::format_map_t& args = {})
        {
            return services().words(name, args);
        }

        // The lines "line 0" on, so many, with some of them said otherwise.
        static std::string lines(S32 count, std::initializer_list<std::pair<S32, const char*>> changed = {})
        {
            std::string text;
            for (S32 n = 0; n < count; ++n)
            {
                std::string line = "line " + std::to_string(n);
                for (const auto& [at, line_said] : changed)
                {
                    if (at == n)
                    {
                        line = line_said;
                    }
                }
                text += (n ? "\n" : "") + line;
            }
            return text;
        }

        // An editor of the test's, holding a text.
        ALCodeEditor* editor(const std::string& name, const std::string& text)
        {
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name               = name;
            p.rect               = LLRect(0, 200, 400, 0);
            p.syntax             = "lsl";
            ALCodeEditor* made   = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(made);
            made->setText(text);
            return made;
        }
        // A tab of a script in an object, loaded and changeable.
        Doc& tab(const std::string& id, const std::string& text)
        {
            Doc& doc       = services().addDoc(id, ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), id);
            doc.loaded     = true;
            doc.modifiable = true;
            doc.editor     = editor("editor_" + id, text);
            return doc;
        }
        std::string titled(const char* name, const std::string& doc, S32 first, S32 last = -1)
        {
            LLStringUtil::format_map_t args;
            args["[NAME]"]  = doc;
            args["[FIRST]"] = std::to_string(first);
            args["[LAST]"]  = std::to_string(last);
            return said(name, args);
        }
    };

    typedef test_group<alscriptstudioselections_data> alscriptstudioselections_group;
    typedef alscriptstudioselections_group::object    alscriptstudioselections_object;
    tut::alscriptstudioselections_group               alscriptstudioselections_test("alscriptstudioselections");

    template<> template<>
    void alscriptstudioselections_object::test<1>()
    {
        set_test_name("nothing selected, nothing held; a selection held by its tab's name and its lines, and said");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door", lines(8));
        ensure("nothing to hold", !ALScriptStudioSelections::canHold(door));
        unit->hold(door);
        ensure("none held", !unit->held());

        door.editor->goTo(ALTextRange(ALTextPos(4, 0), ALTextPos(2, 0)));
        ensure("something to hold", ALScriptStudioSelections::canHold(door));
        unit->hold(door);
        ensure("held", unit->held().has_value());
        ensure_equals("its text, whichever way it was selected", unit->held()->text, std::string("line 2\nline 3\n"));
        ensure_equals("its lines, the last ending at a line's start not counted", unit->held()->title, titled("SelectionLines", "door", 3, 4));
        LLStringUtil::format_map_t args;
        args["[TITLE]"] = unit->held()->title;
        ensure_equals("said", services().statuses.back(), said("SelectionHeld", args));

        door.editor->goTo(ALTextRange(ALTextPos(5, 0), ALTextPos(5, 4)));
        unit->hold(door);
        ensure("one line, in place of the other", unit->held()->text == "line" && unit->held()->title == titled("SelectionLine", "door", 6));
    }

    template<> template<>
    void alscriptstudioselections_object::test<2>()
    {
        set_test_name("the held compared with another tab's selection, in that tab's place: the held on the left, each under its title");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door", lines(8));
        Doc& lamp = tab("lamp", lines(8, { { 3, "lamp 3" } }));
        lamp.editor->goTo(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 0)));
        ensure("nothing held: nothing to compare", !unit->canCompare(lamp));
        door.editor->goTo(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 0)));
        unit->hold(door);
        ensure("held, and lamp's selected", unit->canCompare(lamp));
        lamp.editor->goTo(ALTextPos(0, 0));
        ensure("nothing selected in lamp", !unit->canCompare(lamp));
        lamp.editor->goTo(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 0)));
        unit->compare(lamp);
        ensure_equals("compared", studio.did.size(), 1U);
        ensure_equals("held against lamp's", studio.did[0],
                      "line 2\nline 3\n | line 2\nlamp 3\n (" + titled("SelectionLines", "door", 3, 4) + " | " + titled("SelectionLines", "lamp", 3, 4) + ")");
        ensure("in lamp's place", lamp.compareView && lamp.compareView->changeCount() == 1);
    }

    template<> template<>
    void alscriptstudioselections_object::test<3>()
    {
        set_test_name("in the source: typing goes on at the selection's place, a change taken back edits that stretch; once the tab moves, neither");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door", lines(10));
        door.editor->goTo(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)));
        unit->hold(door);
        door.editor->goTo(ALTextRange(ALTextPos(5, 0), ALTextPos(7, 0)));
        unit->compare(door);
        ALDiffView& view = *door.compareView;
        ensure("its changes may be taken back", view.canTakeBack());

        // Typed on the comparison's second line, two in: the source's
        // seventh line, two in.
        view.right()->setFocus(true);
        view.right()->goTo(ALTextPos(1, 2));
        view.right()->handleUnicodeChar('x', false);
        ensure("sent to the stretch's place", studio.typedAt.size() == 1 && studio.typedAt[0] == ALTextPos(6, 2));
        ensure_equals("typed there", door.editor->text(), lines(10, { { 6, "lixne 6" } }));

        // Compared again, a change taken back: the stretch made the held's.
        door.editor->goTo(ALTextRange(ALTextPos(5, 0), ALTextPos(7, 0)));
        unit->compare(door);
        ensure("taken back", view.takeBack(0));
        ensure_equals("the stretch made the held's", door.editor->text(), lines(10, { { 5, "line 1" }, { 6, "line 2" } }));
        ensure("the comparison with it", view.rightText() == "line 1\nline 2\n" && view.changeCount() == 0);

        // A stretch whose lines change in number: the next taken back
        // still lands within it.
        door.editor->goTo(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        unit->hold(door);
        door.editor->goTo(ALTextRange(ALTextPos(5, 0), ALTextPos(7, 0)));
        unit->compare(door);
        ensure("two lines made one", view.takeBack(0));
        ensure_equals("the source", door.editor->text(), lines(9, { { 5, "line 0" }, { 6, "line 7" }, { 7, "line 8" }, { 8, "line 9" } }));

        // The source moved under a comparison: nothing taken back, typing
        // at the stretch's start.
        door.editor->goTo(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)));
        unit->compare(door);
        door.editor->goTo(ALTextPos(0, 0));
        door.editor->insertText("new\n");
        const std::string moved = door.editor->text();
        ensure("nothing taken back", !view.takeBack(0) && door.editor->text() == moved);
        view.right()->setFocus(true);
        view.right()->goTo(ALTextPos(1, 3));
        view.right()->handleUnicodeChar('y', false);
        ensure("typing at the stretch's start", studio.typedAt.back() == ALTextPos(1, 0));
    }

    template<> template<>
    void alscriptstudioselections_object::test<4>()
    {
        set_test_name("a selection in what is not the source -- the preprocessed view -- compared as text alone: nothing taken back, typing as any comparison's");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door", lines(8));
        door.editor->goTo(ALTextRange(ALTextPos(0, 0), ALTextPos(2, 0)));
        unit->hold(door);
        door.expandedEditor = editor("expanded_door", lines(8, { { 1, "expanded 1" } }));
        door.view           = Doc::View::Expanded;
        door.expandedEditor->goTo(ALTextRange(ALTextPos(0, 0), ALTextPos(2, 0)));
        ensure("held from the view in sight", ALScriptStudioSelections::canHold(door) && unit->canCompare(door));
        unit->compare(door);
        ensure_equals("its text", studio.did.back(),
                      "line 0\nline 1\n | line 0\nexpanded 1\n (" + titled("SelectionLines", "door", 1, 2) + " | " + titled("SelectionLines", "door", 1, 2) + ")");
        ensure("nothing taken back", !door.compareView->canTakeBack());
        door.compareView->right()->setFocus(true);
        door.compareView->right()->goTo(ALTextPos(1, 2));
        door.compareView->right()->handleUnicodeChar('z', false);
        ensure("typing as the window has it: at the same line and column", studio.typedAt.back() == ALTextPos(1, 2));
    }

    template<> template<>
    void alscriptstudioselections_object::test<5>()
    {
        set_test_name("a selection starting within a line: its first line's places counted from where it starts, the rest from their lines' starts");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door", "integer a = alpha;\ninteger b = beta;\ninteger c = 1;");
        door.editor->goTo(ALTextRange(ALTextPos(0, 12), ALTextPos(0, 17)));
        unit->hold(door);
        door.editor->goTo(ALTextRange(ALTextPos(1, 12), ALTextPos(2, 9)));
        unit->compare(door);
        ALDiffView& view = *door.compareView;
        ensure_equals("the stretch", view.rightText(), std::string("beta;\ninteger c"));
        view.right()->setFocus(true);
        view.right()->goTo(ALTextPos(0, 2));
        view.right()->handleUnicodeChar('x', false);
        ensure("on its first line, from where it starts", studio.typedAt.back() == ALTextPos(1, 14));
        ensure_equals("typed there", door.editor->text(), std::string("integer a = alpha;\ninteger b = bexta;\ninteger c = 1;"));

        door.editor->goTo(ALTextRange(ALTextPos(1, 12), ALTextPos(2, 9)));
        unit->compare(door);
        view.right()->setFocus(true);
        view.right()->goTo(ALTextPos(1, 3));
        view.right()->handleUnicodeChar('y', false);
        ensure("on a line after, from its start", studio.typedAt.back() == ALTextPos(2, 3));
    }
}
