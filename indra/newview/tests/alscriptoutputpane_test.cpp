/**
 * @file alscriptoutputpane_test.cpp
 * @brief Script Studio's Output tab, over the studio's own window: its filters, the objects it offers, what is unread, and its links.
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

#include "../alscriptoutputpane.h"

#include "llbutton.h"
#include "llcombobox.h"
#include "llfiltereditor.h"

#include "alscriptstudio_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
    typedef ALScriptRuntimeEvent Event;

    // The window's side, answered as a test says, and what was asked of it
    // kept.
    class FakeWindow : public ALScriptOutputPane::Window
    {
    public:
        struct Went
        {
            ALScriptRef ref;
            std::string name;
            std::string file;
            S32         line   = -1;
            S32         column = -1;
            bool        running = false;
        };

        bool outputInSight() const override { return inSight; }
        void outputUnreadChanged() override { ++unreadChanges; }
        bool ownsObject(const LLUUID& root) const override { return owned.count(root) > 0; }
        void outputAction(ALScriptStudioDoc& doc, const std::string& action) override { actions.push_back(doc.id + ":" + action); }
        void outputShowDoc(ALScriptStudioDoc& doc, bool problems) override { shown.push_back(doc.id + (problems ? ":problems" : "")); }
        void outputGoTo(const ALScriptRef& ref, const std::string& name, S32 line, S32 column, bool running) override
        {
            went.push_back({ ref, name, std::string(), line, column, running });
        }
        void outputGoToInclude(const std::string& file, const std::string& file_name, S32 line, S32 column) override
        {
            went.push_back({ ALScriptRef(), file_name, file, line, column });
        }

        bool                     inSight       = false;
        S32                      unreadChanges = 0;
        std::set<LLUUID>         owned;
        std::vector<std::string> actions;
        std::vector<std::string> shown;
        std::vector<Went>        went;
    };

    LLUUID fresh()
    {
        LLUUID id;
        id.generate();
        return id;
    }
}

namespace
{
    struct alscriptoutputpane_data
    {
        // The window the pane finds through the view tree: the fakes.
        al_studio_test::StudioWindowOf<FakeWindow> window;
        al_studio_test::FakeServices&             services = window.services();
        FakeWindow&                               studio   = window.pane();
        ALScriptOutputPane*                 pane = nullptr;

        ALScriptOutputPane& require_pane()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            // The tab as the skin built it.
            pane = window.find<ALScriptOutputPane>("output_tab");
            INFO("the skin builds the tab as the pane");
            REQUIRE(pane != nullptr);
            return *pane;
        }

        // What an object's script said, on a channel; or a run-time error
        // it hit, at a line.
        static Event said(const LLUUID& root, const std::string& object, const std::string& message, Event::Channel channel = Event::Channel::Debug,
                          const LLUUID& item = LLUUID::null, const std::string& script = std::string())
        {
            Event event;
            event.root       = root;
            event.prim       = root;
            event.item       = item;
            event.objectName = object;
            event.scriptName = script;
            event.channel    = channel;
            event.message    = message;
            return event;
        }
        static Event failed(const LLUUID& root, const LLUUID& item, const std::string& script, const std::string& error, S32 line)
        {
            Event event = said(root, "Thing", error, Event::Channel::Debug, item, script);
            event.isError = true;
            event.error   = error;
            event.line    = line;
            return event;
        }

        // The texts the pane shows through its filters, in order.
        std::vector<std::string> shownTexts() const
        {
            std::vector<std::string> out;
            for (const ALOutputView::Entry& entry : pane->view()->entries())
            {
                if (pane->view()->shows(entry))
                {
                    out.push_back(entry.text);
                }
            }
            return out;
        }
        std::string shown() const
        {
            std::string out;
            for (const std::string& text : shownTexts())
            {
                out += "|" + text;
            }
            return out;
        }
        // A filter chosen, as a click on it would.
        void require_whose(const std::string& value)
        {
            LLComboBox* combo = window.find<LLComboBox>("output_filter");
            CAPTURE(value);
            REQUIRE(combo != nullptr);
            REQUIRE(combo->selectByValue(LLSD(value)));
            combo->onCommit();
        }
        // Words typed in the find box, as a commit of it would take them.
        void require_find(const std::string& words)
        {
            LLFilterEditor* editor = window.find<LLFilterEditor>("output_find");
            REQUIRE(editor != nullptr);
            editor->setText(words);
            editor->onCommit();
        }
        // What the pane said last.
        const ALOutputView::Entry& require_last() const
        {
            const auto& entries = pane->view()->entries();
            REQUIRE_FALSE(entries.empty());
            return entries.back();
        }
    };

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: the kind filter picks errors or owner words or debug or the studio's", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out  = require_pane();
        const LLUUID        door = fresh();
        out.heard(said(door, "Door", "opening", Event::Channel::OwnerSay));
        out.heard(said(door, "Door", "tick\n"));
        out.heard(failed(door, fresh(), "door.lsl", "Stack-Heap Collision", -1));
        out.said("Saved.", false, nullptr, {});
        UNSCOPED_INFO("everything, a trailing newline gone");
        CHECK(shown() == std::string("|opening|tick|Stack-Heap Collision|Saved."));
        out.showKind("error");
        CHECK(shown() == std::string("|Stack-Heap Collision"));
        UNSCOPED_INFO("kept as chosen");
        CHECK(out.kind() == std::string("error"));
        out.showKind("owner");
        CHECK(shown() == std::string("|opening"));
        out.showKind("debug");
        CHECK(shown() == std::string("|tick"));
        out.showKind("studio");
        CHECK(shown() == std::string("|Saved."));
        out.showKind("");
        UNSCOPED_INFO("everything again");
        CHECK(shownTexts().size() == size_t(4));
    }

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: filters by whose it is and by words in either case", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out    = require_pane();
        const LLUUID        door   = fresh();
        const LLUUID        lamp   = fresh();
        const LLUUID        crate  = fresh();
        const LLUUID        script = fresh();
        studio.owned.insert(lamp);
        services.addDoc("door-script", ALScriptRef(door, script), "door.lsl");
        // As the region says it: the debug channel names no script.
        out.heard(said(door, "Door", "door speaks", Event::Channel::Debug));
        out.heard(said(lamp, "Lamp", "lamp speaks"));
        out.heard(said(crate, "Crate", "crate tells its owner", Event::Channel::OwnerSay));
        out.said("Saved door.lsl.", false, nullptr, {});

        require_whose(lamp.asString());
        UNSCOPED_INFO("one object's alone, not the studio's");
        CHECK(shown() == std::string("|lamp speaks"));
        require_whose("open");
        UNSCOPED_INFO("the open script's, and the studio's");
        CHECK(shown() == std::string("|door speaks|Saved door.lsl."));
        require_whose("mine");
        UNSCOPED_INFO("the agent's object's, what an owner was told, and the studio's");
        CHECK(shown() ==
                      std::string("|lamp speaks|crate tells its owner|Saved door.lsl."));

        require_whose("");
        require_find("DOOR");
        UNSCOPED_INFO("the words, in the text or whose it is");
        CHECK(shown() == std::string("|door speaks|Saved door.lsl."));
        require_find("  lamp  ");
        UNSCOPED_INFO("trimmed");
        CHECK(shown() == std::string("|lamp speaks"));
        require_find("");
        UNSCOPED_INFO("everything");
        CHECK(shownTexts().size() == size_t(4));

        // The script closed: the filter by open scripts follows.
        require_whose("open");
        services.docs.clear();
        out.openChanged();
        UNSCOPED_INFO("the studio's alone");
        CHECK(shown() == std::string("|Saved door.lsl."));
    }

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: the objects offered stop at forty dropping the stalest but the chosen", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out = require_pane();
        std::vector<LLUUID> roots;
        for (S32 i = 0; i < 45; ++i)
        {
            roots.push_back(fresh());
            out.heard(said(roots.back(), llformat("Object %02d", i), "hello"));
        }
        REQUIRE(out.objects().size() == size_t(40));
        UNSCOPED_INFO("the first five gone");
        CHECK(out.objects().front().first == roots[5]);
        // Heard from again: the last to go.
        out.heard(said(roots[5], "Object 05", "again"));
        UNSCOPED_INFO("heard again, last");
        CHECK(out.objects().back().first == roots[5]);
        REQUIRE(out.objects().size() == size_t(40));
        // Renamed, and heard from under its new name.
        out.heard(said(roots[9], "Renamed", "again"));
        CHECK(out.objects().back().second == std::string("Renamed"));
        // The one chosen stays, least lately heard from as it is.
        require_whose(roots[6].asString());
        out.heard(said(fresh(), "Newcomer", "hello"));
        REQUIRE(out.objects().size() == size_t(40));
        UNSCOPED_INFO("the one chosen stayed");
        CHECK(out.objects().front().first == roots[6]);
        UNSCOPED_INFO("the next went");
        CHECK(std::none_of(out.objects().begin(), out.objects().end(), [&](const auto& one) { return one.first == roots[7]; }));
        const LLComboBox* filter = window.find<LLComboBox>("output_filter");
        REQUIRE(filter != nullptr);
        UNSCOPED_INFO("and is still chosen");
        CHECK(filter->getValue().asString() == roots[6].asString());
    }

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: an error or failure out of sight is unread until seen or cleared", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out  = require_pane();
        const LLUUID        door = fresh();
        out.heard(said(door, "Door", "tick"));
        out.said("Saved.", false, nullptr, {});
        UNSCOPED_INFO("words alone are not unread");
        CHECK_FALSE(out.unread());
        UNSCOPED_INFO("and nothing to say of it");
        CHECK(studio.unreadChanges == 0);
        out.heard(failed(door, fresh(), "door.lsl", "Math Error", -1));
        UNSCOPED_INFO("an error is");
        CHECK(out.unread());
        UNSCOPED_INFO("said on the title");
        CHECK(studio.unreadChanges == 1);
        out.pump();
        UNSCOPED_INFO("still, out of sight");
        CHECK(out.unread());
        studio.inSight = true;
        out.pump();
        UNSCOPED_INFO("looked at");
        CHECK_FALSE(out.unread());
        UNSCOPED_INFO("said again");
        CHECK(studio.unreadChanges == 2);
        out.said("Could not save.", true, nullptr, {});
        UNSCOPED_INFO("a failure in sight is read");
        CHECK_FALSE(out.unread());
        studio.inSight = false;
        out.said("Could not save.", true, nullptr, {});
        UNSCOPED_INFO("a failure out of sight is not");
        CHECK(out.unread());
        LLButton* clear = window.find<LLButton>("output_clear");
        REQUIRE(clear != nullptr);
        clear->onCommit();
        UNSCOPED_INFO("cleared");
        CHECK_FALSE(out.unread());
        UNSCOPED_INFO("with what was said");
        CHECK(out.view()->entries().empty());
    }

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: the studio's words link to the tab they name and to its actions", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out = require_pane();
        ALScriptStudioDoc&  doc = services.addDoc("door-script", ALScriptRef(fresh(), fresh()), "door.lsl");
        out.said("Could not save door.lsl.", true, &doc, { "retry", "copy" });
        const ALOutputView::Entry& entry = require_last();
        UNSCOPED_INFO("the name and two things to do");
        REQUIRE(entry.links.size() == size_t(3));
        UNSCOPED_INFO("the name, where the words say it");
        CHECK(entry.links[0].begin == 15);
        UNSCOPED_INFO("to its end");
        CHECK(entry.links[0].end == 23);
        UNSCOPED_INFO("each thing after the words");
        CHECK(entry.text.find(services.words("ActionRetry")) > entry.text.find("door.lsl."));
        UNSCOPED_INFO("where it is");
        CHECK(entry.text.substr(entry.links[1].begin, entry.links[1].end - entry.links[1].begin) == services.words("ActionRetry"));

        ALOutputView::Entry chosen = entry;
        for (const ALOutputView::Entry::Link& link : entry.links)
        {
            chosen.value = link.value;
            out.choose(chosen);
        }
        UNSCOPED_INFO("the tab, with its problems");
        REQUIRE(studio.shown.size() == size_t(1));
        UNSCOPED_INFO("the tab named");
        CHECK(studio.shown[0] == std::string("door-script:problems"));
        UNSCOPED_INFO("what to do, by name");
        REQUIRE(studio.actions.size() == size_t(2));
        CHECK(studio.actions[0] == std::string("door-script:retry"));
        CHECK(studio.actions[1] == std::string("door-script:copy"));

        // Said of no tab: nothing to link.
        out.said("Nothing open.", false, nullptr, { "retry" });
        const ALOutputView::Entry& unlinked = require_last();
        CHECK(unlinked.links.empty());
        // The tab closed since: nothing asked of the window.
        services.docs.clear();
        out.choose(chosen);
        UNSCOPED_INFO("nothing more");
        CHECK(studio.actions.size() == size_t(2));
    }

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: a run's lines map back to the script or an include for each frame", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out    = require_pane();
        const LLUUID        object = fresh();
        const LLUUID        item   = fresh();
        // Ten lines out: the first four the include's, the rest the
        // script's from its second line on.
        ALSourceMap map;
        map.addFile("script", "script");
        map.addFile("lib.luau", "disk:/scripts/lib.luau");
        for (S32 line = 0; line < 10; ++line)
        {
            ALSourceMap::Segment segment;
            segment.outLine = line;
            segment.length  = 20;
            segment.file    = line < 4 ? 1 : 0;
            segment.line    = line < 4 ? line : line - 3;
            map.add(segment);
        }
        map.finish();
        ALScriptStudioDoc& doc = services.addDoc("counter", ALScriptRef(object, item), "Counter");
        doc.uploaded.valid     = true;
        doc.uploaded.map       = map;

        Event error = failed(object, item, "Counter", "attempt to index nil", 7);
        error.stack = { "lua_script:8: attempt to index nil", "lua_script:3 function helper", "lua_script:8", "[C] function error" };
        const ALScriptOutputPane::Place at = out.heard(error);
        UNSCOPED_INFO("the error's line, the script's");
        CHECK(at.line == 4);
        UNSCOPED_INFO("in the script");
        CHECK(at.file.empty());
        const ALOutputView::Entry& entry = require_last();
        UNSCOPED_INFO("a link to it");
        CHECK(entry.link);
        UNSCOPED_INFO("at that line");
        CHECK(entry.value["line"].asInteger() == 4);
        UNSCOPED_INFO("a link for each frame of the script's");
        REQUIRE(entry.links.size() == size_t(2));
        UNSCOPED_INFO("the first in the include");
        CHECK(entry.links[0].value["file"].asString() == std::string("disk:/scripts/lib.luau"));
        UNSCOPED_INFO("at its line");
        CHECK(entry.links[0].value["line"].asInteger() == 2);
        UNSCOPED_INFO("the second in the script");
        CHECK(entry.links[1].value["line"].asInteger() == 4);

        ALOutputView::Entry chosen = entry;
        out.choose(chosen);
        chosen.value = entry.links[0].value;
        out.choose(chosen);
        UNSCOPED_INFO("two places gone to");
        REQUIRE(studio.went.size() == size_t(2));
        {
            INFO("the script");
            CHECK(studio.went[0].ref == ALScriptRef(object, item));
            CHECK(studio.went[0].file.empty());
        }
        UNSCOPED_INFO("at the error's line");
        CHECK(studio.went[0].line == 4);
        UNSCOPED_INFO("read back already");
        CHECK_FALSE(studio.went[0].running);
        UNSCOPED_INFO("the include");
        CHECK(studio.went[1].file == std::string("disk:/scripts/lib.luau"));
        UNSCOPED_INFO("by its name");
        CHECK(studio.went[1].name == std::string("lib.luau"));
        UNSCOPED_INFO("at the frame's line");
        CHECK(studio.went[1].line == 2);

        // Not open here: the line the run names, as it named it, and a
        // link that says so, for the window to read back once it can.
        services.docs.clear();
        UNSCOPED_INFO("as said");
        CHECK(out.heard(error).line == 7);
        chosen = require_last();
        out.choose(chosen);
        {
            INFO("gone to as the region counts it");
            CHECK(studio.went.back().line == 7);
            CHECK(studio.went.back().running);
        }
    }

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: a run's line past the envelope's lines maps back through the code", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out    = require_pane();
        const LLUUID        object = fresh();
        const LLUUID        item   = fresh();
        ALSourceMap map;
        map.addFile("script", "script");
        for (S32 line = 0; line < 10; ++line)
        {
            ALSourceMap::Segment segment;
            segment.outLine = line;
            segment.length  = 20;
            segment.line    = line + 100;
            map.add(segment);
        }
        map.finish();
        ALScriptStudioDoc& doc = services.addDoc("counter", ALScriptRef(object, item), "Counter");
        doc.uploaded.valid     = true;
        doc.uploaded.map       = map;
        ALScriptEnvelope envelope;
        envelope.lua           = true;
        envelope.source        = "one\ntwo\nthree";
        envelope.expanded      = "code\n";
        envelope.compileTarget = "luau";
        doc.envelope           = envelope;
        const S32 under        = envelope.codeLine();
        UNSCOPED_INFO("the envelope has lines of its own");
        CHECK(under > 3);
        UNSCOPED_INFO("its code's third line, where the region names it");
        CHECK(out.heard(failed(object, item, "Counter", "attempt to index nil", under + 2)).line == 102);
        UNSCOPED_INFO("a line in the envelope is none of the code's: as said");
        CHECK(out.heard(failed(object, item, "Counter", "boom", 1)).line == 1);
    }

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: an error the filters hide is not unread", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out   = require_pane();
        const LLUUID        lamp  = fresh();
        const LLUUID        crate = fresh();
        out.heard(said(lamp, "Lamp", "lamp speaks"));
        out.heard(said(crate, "Crate", "crate speaks"));
        require_whose(lamp.asString());
        out.heard(failed(crate, fresh(), "crate.lsl", "Math Error", -1));
        {
            INFO("a stranger's, hidden: not unread");
            CHECK_FALSE(out.unread());
            CHECK(studio.unreadChanges == 0);
        }
        out.heard(failed(lamp, fresh(), "lamp.lsl", "Math Error", -1));
        UNSCOPED_INFO("the chosen one's is");
        CHECK(out.unread());
        const S32 said = studio.unreadChanges;
        out.heard(failed(lamp, fresh(), "lamp.lsl", "Another Error", -1));
        UNSCOPED_INFO("unread already: not said again");
        CHECK(studio.unreadChanges == said);
    }

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: what objects say aloud or to the agent or in IMs gets its own kind", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out  = require_pane();
        const LLUUID        lamp = fresh();
        Event               aloud = said(lamp, "Lamp", "hello", Event::Channel::Said);
        aloud.primName            = "Bulb";
        out.heard(aloud);
        out.heard(said(lamp, "Lamp", "just you", Event::Channel::SaidTo));
        out.heard(said(lamp, "Lamp", "an IM", Event::Channel::Instant));
        const auto& entries = out.view()->entries();
        REQUIRE(entries.size() == size_t(3));
        UNSCOPED_INFO("the prim that spoke, after its object");
        CHECK(entries[0].source == std::string("Lamp \xE2\x96\xB8 Bulb"));
        CHECK(entries[0].kind == window.services().words("KindSaid"));
        UNSCOPED_INFO("said to the agent");
        CHECK(entries[1].kind == window.services().words("KindSaidTo"));
        UNSCOPED_INFO("an IM");
        CHECK(entries[2].kind == window.services().words("KindInstant"));
        require_whose("mine");
        UNSCOPED_INFO("the agent's own, all of them");
        CHECK(shownTexts().size() == size_t(3));
        require_whose("");
        out.showKind("said");
        UNSCOPED_INFO("aloud and to the agent");
        CHECK(shown() == std::string("|hello|just you"));
        out.showKind("im");
        CHECK(shown() == std::string("|an IM"));
    }

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: the controls row keeps buttons right and narrows the lists first", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out   = require_pane();
        const LLRect        whose = out.getChild<LLView>("output_filter")->getRect();
        const LLRect        kind  = out.getChild<LLView>("output_kind")->getRect();
        const auto          row   = [&]() {
            std::vector<LLRect> rects;
            for (const char* name : { "output_filter", "output_kind", "output_find", "output_clear", "output_copy" })
            {
                rects.push_back(out.getChild<LLView>(name)->getRect());
            }
            return rects;
        };
        const auto apart = [](const std::vector<LLRect>& rects, S32 width) {
            for (size_t i = 0; i + 1 < rects.size(); ++i)
            {
                if (rects[i].mRight > rects[i + 1].mLeft)
                {
                    return false;
                }
            }
            return rects.back().mRight <= width;
        };
        out.reshape(800, out.getRect().getHeight());
        std::vector<LLRect> wide = row();
        {
            INFO("wide: the lists as the skin made them");
            CHECK(wide[0].getWidth() == whose.getWidth());
            CHECK(wide[1].getWidth() == kind.getWidth());
        }
        {
            INFO("the copy against the right edge, the box up to the clear");
            CHECK(wide[4].mRight == 798);
            CHECK(wide[2].mRight + 6 == wide[3].mLeft);
        }
        UNSCOPED_INFO("in order, apart");
        CHECK(apart(wide, 800));
        out.reshape(420, out.getRect().getHeight());
        std::vector<LLRect> narrow = row();
        {
            INFO("narrow: the lists narrower");
            CHECK(narrow[0].getWidth() < whose.getWidth());
            CHECK(narrow[1].getWidth() <= kind.getWidth());
        }
        {
            INFO("the buttons as they were");
            CHECK(narrow[3].getWidth() == wide[3].getWidth());
            CHECK(narrow[4].getWidth() == wide[4].getWidth());
        }
        UNSCOPED_INFO("in order, apart");
        CHECK(apart(narrow, 420));
        UNSCOPED_INFO("the box still there");
        CHECK(narrow[2].getWidth() >= 60);
    }

    TEST_CASE_METHOD(alscriptoutputpane_data, "ALScriptOutputPane: a lone CR breaks a line and links follow it to the shown line", "[ALScriptOutputPane]")
    {
        ALScriptOutputPane& out    = require_pane();
        const LLUUID        object = fresh();
        const LLUUID        item   = fresh();
        Event               error  = failed(object, item, "Counter", "bad\rvalue", -1);
        error.stack                = { "lua_script:8: bad\rvalue", "lua_script:3 function helper" };
        out.heard(error);
        const ALOutputView::Entry& entry = require_last();
        UNSCOPED_INFO("its breaks as the log reads them");
        CHECK(entry.text == std::string("bad\nvalue\nlua_script:3 function helper"));
        UNSCOPED_INFO("a link for the frame");
        REQUIRE(entry.links.size() == size_t(1));
        UNSCOPED_INFO("on the frame's line");
        CHECK(entry.links[0].line == 2);
        const ALTextDocument& shown_text = out.view()->document();
        UNSCOPED_INFO("the line the log shows it on");
        CHECK(shown_text.line(shown_text.lineCount() - 1) == std::string("lua_script:3 function helper"));

        // The studio's own words likewise: a name after a lone CR linked on
        // the line it is shown on, and what to do after it.
        ALScriptStudioDoc& doc = services.addDoc("door-script", ALScriptRef(fresh(), fresh()), "door.lsl");
        out.said("Could not save\rdoor.lsl.", true, &doc, { "retry" });
        const ALOutputView::Entry& studio_said = require_last();
        UNSCOPED_INFO("the name and what to do");
        REQUIRE(studio_said.links.size() == size_t(2));
        {
            INFO("the name at the start of the second line");
            CHECK(studio_said.links[0].line == 1);
            CHECK(studio_said.links[0].begin == 0);
            CHECK(studio_said.links[0].end == 8);
        }
        {
            INFO("what to do on it too");
            CHECK(studio_said.links[1].line == 1);
            CHECK(studio_said.links[1].begin == 12);
        }
    }
}
