/**
 * @file alscriptnavigation_test.cpp
 * @brief Script Studio's navigation: the history of places, back and forward, and previews opened as a list is walked.
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

#include "../alscriptnavigation.h"

#include "alpanelist.h"
#include "alscriptstudio_fixture.h"
#include "llfocusmgr.h"

#include "../test/lltut.h"

#include <chrono>
#include <thread>

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef ALNavHistory::Place      Place;
    typedef std::vector<std::string> Names;

    // The window, faked: a record of what navigation asked of it.
    struct FakeNavWindow : public ALScriptNavigation::Window, public al_studio_test::QuietTabs
    {
        void showPlace(Doc& doc, Doc::View view, const ALTextPos& at) override
        {
            shown.push_back(doc.id + ":" + std::to_string(at.line) + (view == Doc::View::Expanded ? " expanded" : ""));
        }
        bool pathOpen(const std::string& path) const override { return open.count(path) != 0; }
        void choosePreview(ALPaneList*) override
        {
            ++chosen;
            previewing = navigation && navigation->openingPreview();
        }
        bool workedFrom(const Doc& doc) const override { return doc.id == worked; }
        // Closed, as the window closes it: a preview no more.
        void letGoOf(Doc& doc) override
        {
            doc.preview = false;
            letGo.push_back(doc.id);
        }
        void fillTabs() override { ++tabs; }
        void focusDoc(Doc& doc) override { focused.push_back(doc.id); }

        const ALScriptNavigation* navigation = nullptr;
        std::set<std::string>     open;
        std::string               worked;
        Names                     shown, letGo, focused;
        S32                       chosen = 0, tabs = 0;
        bool                      previewing = false;
    };

    Place at(const std::string& doc, S32 line, Doc::View view = Doc::View::Source)
    {
        return Place{ doc, ALTextPos(line, 0), view };
    }
    std::string lines(const std::optional<Place>& place) { return place ? place->doc + ":" + std::to_string(place->at.line) : "none"; }
}

namespace tut
{
    struct alscriptnavigation_data
    {
        al_studio_test::StudioWindow        window;
        al_studio_test::FakeServices        services;
        FakeNavWindow                       studio;
        std::unique_ptr<ALScriptNavigation> unit;

        ~alscriptnavigation_data() { gFocusMgr.setKeyboardFocus(nullptr); }
        ALScriptNavigation& make()
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            unit              = std::make_unique<ALScriptNavigation>(services, studio, studio);
            studio.navigation = unit.get();
            return *unit;
        }
        Doc& tab(const std::string& id, S32 caret_line = 0)
        {
            Doc& doc       = services.addDoc(id);
            doc.loaded     = true;
            doc.modifiable = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name     = "editor_" + id;
            p.rect     = LLRect(0, 200, 400, 0);
            doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(doc.editor);
            doc.editor->setText("one\ntwo\nthree\nfour\n");
            doc.editor->resetDirty();
            doc.editor->setCaret(ALTextPos(caret_line, 0));
            return doc;
        }
        ALPaneList* list()
        {
            ALPaneList::Params p(LLUICtrlFactory::getDefaultParams<ALPaneList>());
            p.name        = "list";
            p.rect        = LLRect(0, 100, 200, 0);
            ALPaneList* l = LLUICtrlFactory::create<ALPaneList>(p);
            window.floater->addChild(l);
            l->addSimpleElement("a", ADD_BOTTOM, LLSD("a"));
            l->addSimpleElement("b", ADD_BOTTOM, LLSD("b"));
            l->selectByValue(LLSD("a"));
            l->setFocus(true);
            return l;
        }
        static void waitToSettle() { std::this_thread::sleep_for(std::chrono::milliseconds(400)); }
    };

    typedef test_group<alscriptnavigation_data> alscriptnavigation_group;
    typedef alscriptnavigation_group::object    alscriptnavigation_object;
    alscriptnavigation_group                    alscriptnavigation_instance("alscriptnavigation");

    template<> template<>
    void alscriptnavigation_object::test<1>()
    {
        set_test_name("the history: once a line, a view its own place, the fifty latest, and the way forward gone once a place is noted");
        ALNavHistory history;
        const auto   open = [](const std::string&) { return true; };
        history.note(at("a", 1));
        history.note(at("a", 1));
        ensure("once a line", lines(history.take(false, std::nullopt, open)) == "a:1" && !history.canGo(false));
        history.note(at("a", 1));
        history.note(at("a", 1, Doc::View::Expanded));
        std::optional<Place> expanded = history.take(false, std::nullopt, open);
        ensure("the expansion's line its own place", expanded && expanded->view == Doc::View::Expanded && history.canGo(false));
        history.take(false, std::nullopt, open);
        for (S32 i = 0; i < 60; ++i)
        {
            history.note(at("a", i));
        }
        S32 count = 0;
        std::optional<Place> oldest;
        while (history.canGo(false))
        {
            oldest = history.take(false, std::nullopt, open);
            ++count;
        }
        ensure("fifty, the oldest dropped", count == 50 && lines(oldest) == "a:10");
        history.note(at("a", 1));
        history.take(false, at("b", 2), open);
        ensure("where it went from, forward", history.canGo(true));
        history.note(at("c", 3));
        ensure("a place noted: the way forward gone", !history.canGo(true));
    }

    template<> template<>
    void alscriptnavigation_object::test<2>()
    {
        set_test_name("back and forward: where it goes from put on the other way, a closed tab passed over, a tab renamed followed");
        ALNavHistory history;
        std::set<std::string> closed;
        const auto            open = [&closed](const std::string& id) { return closed.count(id) == 0; };
        history.note(at("a", 1));
        history.note(at("gone", 2));
        history.note(at("b", 3));
        closed.insert("gone");
        ensure("back", lines(history.take(false, at("c", 4), open)) == "b:3");
        ensure("back past the closed one", lines(history.take(false, at("b", 3), open)) == "a:1" && !history.canGo(false));
        ensure("forward again", lines(history.take(true, at("a", 1), open)) == "b:3");
        history.rekey("c", "renamed");
        ensure("followed", lines(history.take(true, std::nullopt, open)) == "renamed:4");
        ensure("nowhere left", !history.take(true, std::nullopt, open));
    }

    template<> template<>
    void alscriptnavigation_object::test<3>()
    {
        set_test_name("a jump from the tab in front, a walk one jump until it ends; back and forward shown by the window");
        ALScriptNavigation& unit = make();
        Doc&                a    = tab("a", 1);
        services.front           = 0;
        unit.noteJump(true);
        a.editor->setCaret(ALTextPos(3, 0));
        unit.noteJump(true);
        ensure("a walk: one jump", unit.walking());
        unit.goBack(false);
        ensure("back where the walk began", studio.shown == Names{ "a:1" } && !unit.walking() && !unit.canGo(false));
        unit.goBack(true);
        ensure("and forward to where it was then", studio.shown == Names{ "a:1", "a:3" });
        unit.noteJump(true);
        unit.walked();
        a.editor->setCaret(ALTextPos(2, 0));
        unit.noteJump(true);
        unit.goBack(false);
        ensure("a walk ended: the next is a jump of its own", studio.shown.back() == "a:2");
        a.loaded = false;
        unit.noteJump();
        ensure("a tab not loaded: nothing noted", !unit.canGo(false) || studio.shown.size() == 3);
    }

    template<> template<>
    void alscriptnavigation_object::test<4>()
    {
        set_test_name("a row stayed on a moment opened as a preview; one open already, walked past, or left, not");
        ALScriptNavigation& unit = make();
        ALPaneList*         l    = list();
        studio.open.insert("open path");
        ensure("open already: at once", !unit.deferOpen(l, "open path"));
        ensure("nowhere: at once", !unit.deferOpen(l, ""));
        ensure("put off", unit.deferOpen(l, "a path"));
        unit.pumpSettle();
        ensure("not yet", studio.chosen == 0);
        waitToSettle();
        unit.pumpSettle();
        ensure("stayed on: opened, as a preview", studio.chosen == 1 && studio.previewing && !unit.openingPreview());
        unit.pumpSettle();
        ensure("once", studio.chosen == 1);
        unit.deferOpen(l, "a path");
        l->selectByValue(LLSD("b"));
        waitToSettle();
        unit.pumpSettle();
        ensure("walked past: not", studio.chosen == 1);
        unit.deferOpen(l, "a path");
        gFocusMgr.setKeyboardFocus(nullptr);
        waitToSettle();
        unit.pumpSettle();
        ensure("left: not", studio.chosen == 1);
    }

    template<> template<>
    void alscriptnavigation_object::test<5>()
    {
        set_test_name("the preview let go of for the next, held where anything was done in it or it is worked from; to the editor, held");
        ALScriptNavigation& unit = make();
        Doc&                a    = tab("a");
        a.preview                = true;
        unit.closePreview();
        ensure("let go of", studio.letGo == Names{ "a" });
        Doc& b    = tab("b");
        b.preview = true;
        b.editor->insertText("typed");
        unit.closePreview();
        ensure("typed in: held", !b.preview && studio.tabs == 1 && studio.letGo.size() == 1);
        Doc& c        = tab("c");
        c.preview     = true;
        studio.worked = "c";
        unit.closePreview();
        ensure("worked from: held", !c.preview && studio.letGo.size() == 1);
        Doc& d         = tab("d");
        d.preview      = true;
        services.front = 3;
        unit.revealed(nullptr, true);
        ensure("taken to the editor: held, and given the keyboard", !d.preview && studio.focused == Names{ "d" });
        ALPaneList* l = list();
        gFocusMgr.setKeyboardFocus(nullptr);
        unit.revealed(l, false);
        ensure("back to the list", l->hasFocus());
    }
}
