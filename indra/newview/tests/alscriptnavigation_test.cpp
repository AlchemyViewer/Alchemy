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

#include <catch2/catch_test_macros.hpp>

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

namespace
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
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

    // A view of a line is a place of its own, and a place noted drops the
    // way forward.
    TEST_CASE_METHOD(alscriptnavigation_data, "alscriptnavigation: the history notes a line once and keeps the fifty latest places", "[alscriptnavigation]")
    {
        ALNavHistory history;
        const auto   open = [](const std::string&) { return true; };
        history.note(at("a", 1));
        history.note(at("a", 1));
        {
            INFO("once a line");
            CHECK(lines(history.take(false, std::nullopt, open)) == "a:1");
            CHECK_FALSE(history.canGo(false));
        }
        history.note(at("a", 1));
        history.note(at("a", 1, Doc::View::Expanded));
        std::optional<Place> expanded = history.take(false, std::nullopt, open);
        {
            INFO("the expansion's line its own place");
            REQUIRE(expanded);
            CHECK(expanded->view == Doc::View::Expanded);
            CHECK(history.canGo(false));
        }
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
        {
            INFO("fifty, the oldest dropped");
            CHECK(count == 50);
            CHECK(lines(oldest) == "a:10");
        }
        history.note(at("a", 1));
        history.take(false, at("b", 2), open);
        UNSCOPED_INFO("where it went from, forward");
        CHECK(history.canGo(true));
        history.note(at("c", 3));
        UNSCOPED_INFO("a place noted: the way forward gone");
        CHECK_FALSE(history.canGo(true));
    }

    // Where it goes from is put on the other way.
    TEST_CASE_METHOD(alscriptnavigation_data, "alscriptnavigation: back and forward pass closed tabs over and follow renamed ones", "[alscriptnavigation]")
    {
        ALNavHistory history;
        std::set<std::string> closed;
        const auto            open = [&closed](const std::string& id) { return closed.count(id) == 0; };
        history.note(at("a", 1));
        history.note(at("gone", 2));
        history.note(at("b", 3));
        closed.insert("gone");
        UNSCOPED_INFO("back");
        CHECK(lines(history.take(false, at("c", 4), open)) == "b:3");
        {
            INFO("back past the closed one");
            CHECK(lines(history.take(false, at("b", 3), open)) == "a:1");
            CHECK_FALSE(history.canGo(false));
        }
        UNSCOPED_INFO("forward again");
        CHECK(lines(history.take(true, at("a", 1), open)) == "b:3");
        history.rekey("c", "renamed");
        UNSCOPED_INFO("the renamed tab followed");
        CHECK(lines(history.take(true, std::nullopt, open)) == "renamed:4");
        UNSCOPED_INFO("nowhere left");
        CHECK_FALSE(history.take(true, std::nullopt, open));
    }

    TEST_CASE_METHOD(alscriptnavigation_data, "alscriptnavigation: a walk is noted as one jump and back and forward are shown by the window", "[alscriptnavigation]")
    {
        ALScriptNavigation& unit = make();
        Doc&                a    = tab("a", 1);
        services.front           = 0;
        unit.noteJump(true);
        a.editor->setCaret(ALTextPos(3, 0));
        unit.noteJump(true);
        UNSCOPED_INFO("a walk: one jump");
        CHECK(unit.walking());
        unit.goBack(false);
        {
            INFO("back where the walk began");
            CHECK(studio.shown == Names{ "a:1" });
            CHECK_FALSE(unit.walking());
            CHECK_FALSE(unit.canGo(false));
        }
        unit.goBack(true);
        UNSCOPED_INFO("and forward to where it was then");
        CHECK(studio.shown == Names{ "a:1", "a:3" });
        unit.noteJump(true);
        unit.walked();
        a.editor->setCaret(ALTextPos(2, 0));
        unit.noteJump(true);
        unit.goBack(false);
        REQUIRE(studio.shown.size() == 3);
        UNSCOPED_INFO("a walk ended: the next is a jump of its own");
        CHECK(studio.shown.back() == "a:2");
        a.loaded = false;
        unit.noteJump();
        {
            // Noting a place drops the way forward, a:2 here.
            INFO("a tab not loaded: nothing noted");
            CHECK(unit.places(false) == 1);
            CHECK(unit.canGo(true));
        }
    }

    TEST_CASE_METHOD(alscriptnavigation_data, "alscriptnavigation: a row stayed on opens as a preview unless already open or passed or left", "[alscriptnavigation]")
    {
        ALScriptNavigation& unit = make();
        ALPaneList*         l    = list();
        studio.open.insert("open path");
        UNSCOPED_INFO("open already: at once");
        CHECK_FALSE(unit.deferOpen(l, "open path"));
        UNSCOPED_INFO("nowhere: at once");
        CHECK_FALSE(unit.deferOpen(l, ""));
        UNSCOPED_INFO("put off");
        CHECK(unit.deferOpen(l, "a path"));
        unit.pumpSettle();
        UNSCOPED_INFO("not yet");
        CHECK(studio.chosen == 0);
        waitToSettle();
        unit.pumpSettle();
        {
            INFO("stayed on: opened, as a preview");
            CHECK(studio.chosen == 1);
            CHECK(studio.previewing);
            CHECK_FALSE(unit.openingPreview());
        }
        unit.pumpSettle();
        UNSCOPED_INFO("once");
        CHECK(studio.chosen == 1);
        unit.deferOpen(l, "a path");
        l->selectByValue(LLSD("b"));
        waitToSettle();
        unit.pumpSettle();
        UNSCOPED_INFO("walked past: not");
        CHECK(studio.chosen == 1);
        unit.deferOpen(l, "a path");
        gFocusMgr.setKeyboardFocus(nullptr);
        waitToSettle();
        unit.pumpSettle();
        UNSCOPED_INFO("left: not");
        CHECK(studio.chosen == 1);
    }

    TEST_CASE_METHOD(alscriptnavigation_data, "alscriptnavigation: the preview is let go of unless typed in or worked from or revealed", "[alscriptnavigation]")
    {
        ALScriptNavigation& unit = make();
        Doc&                a    = tab("a");
        a.preview                = true;
        unit.closePreview();
        UNSCOPED_INFO("let go of");
        CHECK(studio.letGo == Names{ "a" });
        Doc& b    = tab("b");
        b.preview = true;
        b.editor->insertText("typed");
        unit.closePreview();
        {
            INFO("typed in: held");
            CHECK_FALSE(b.preview);
            CHECK(studio.tabs == 1);
            CHECK(studio.letGo.size() == 1);
        }
        Doc& c        = tab("c");
        c.preview     = true;
        studio.worked = "c";
        unit.closePreview();
        {
            INFO("worked from: held");
            CHECK_FALSE(c.preview);
            CHECK(studio.letGo.size() == 1);
        }
        Doc& d         = tab("d");
        d.preview      = true;
        services.front = 3;
        unit.revealed(nullptr, true);
        {
            INFO("taken to the editor: held, and given the keyboard");
            CHECK_FALSE(d.preview);
            CHECK(studio.focused == Names{ "d" });
        }
        ALPaneList* l = list();
        gFocusMgr.setKeyboardFocus(nullptr);
        unit.revealed(l, false);
        UNSCOPED_INFO("back to the list");
        CHECK(l->hasFocus());
    }
}
