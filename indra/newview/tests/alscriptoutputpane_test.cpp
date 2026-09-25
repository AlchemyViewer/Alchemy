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

#include "../test/lltut.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
    typedef ALScriptWorkspace::RuntimeEvent Event;

    // The window's side, answered as a test says, and what was asked of it
    // kept.
    class FakeWindow final : public ALScriptOutputPane::Window
    {
    public:
        struct Went
        {
            ALScriptRef ref;
            std::string name;
            std::string file;
            S32         line   = -1;
            S32         column = -1;
        };

        bool outputInSight() const override { return inSight; }
        void outputUnreadChanged() override { ++unreadChanges; }
        bool ownsObject(const LLUUID& root) const override { return owned.count(root) > 0; }
        void outputAction(ALScriptStudioDoc& doc, const std::string& action) override { actions.push_back(doc.id + ":" + action); }
        void outputShowDoc(ALScriptStudioDoc& doc, bool problems) override { shown.push_back(doc.id + (problems ? ":problems" : "")); }
        void outputGoTo(const ALScriptRef& ref, const std::string& name, S32 line, S32 column) override
        {
            went.push_back({ ref, name, std::string(), line, column });
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

namespace tut
{
    struct alscriptoutputpane_data
    {
        al_studio_test::StudioWindow        window;
        al_studio_test::FakeServices        services{ window.floater };
        FakeWindow                          studio;
        std::unique_ptr<ALScriptOutputPane> pane;

        ALScriptOutputPane& make()
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            pane = std::make_unique<ALScriptOutputPane>(*window.tab("output_tab"), services, studio);
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
        void whose(const std::string& value)
        {
            LLComboBox* combo = window.find<LLComboBox>("output_filter");
            ensure("offered: " + value, combo->selectByValue(LLSD(value)));
            combo->onCommit();
        }
        void find(const std::string& words)
        {
            LLFilterEditor* editor = window.find<LLFilterEditor>("output_find");
            editor->setText(words);
            editor->onCommit();
        }
        const ALOutputView::Entry& last() const { return pane->view()->entries().back(); }
    };
    typedef test_group<alscriptoutputpane_data> alscriptoutputpane_group;
    typedef alscriptoutputpane_group::object    alscriptoutputpane_object;
    alscriptoutputpane_group                    alscriptoutputpane_instance("ALScriptOutputPane");

    template <>
    template <>
    void alscriptoutputpane_object::test<1>()
    {
        set_test_name("by kind: errors, what owners were told, the debug channel, the studio's own words");
        ALScriptOutputPane& out  = make();
        const LLUUID        door = fresh();
        out.heard(said(door, "Door", "opening", Event::Channel::OwnerSay));
        out.heard(said(door, "Door", "tick\n"));
        out.heard(failed(door, fresh(), "door.lsl", "Stack-Heap Collision", -1));
        out.said("Saved.", false, nullptr, {});
        ensure_equals("everything, a trailing newline gone", shown(), std::string("|opening|tick|Stack-Heap Collision|Saved."));
        out.showKind("error");
        ensure_equals("the error", shown(), std::string("|Stack-Heap Collision"));
        ensure_equals("kept as chosen", out.kind(), std::string("error"));
        out.showKind("owner");
        ensure_equals("what the owner was told", shown(), std::string("|opening"));
        out.showKind("debug");
        ensure_equals("the debug channel", shown(), std::string("|tick"));
        out.showKind("studio");
        ensure_equals("the studio", shown(), std::string("|Saved."));
        out.showKind("");
        ensure_equals("everything again", shownTexts().size(), size_t(4));
    }

    template <>
    template <>
    void alscriptoutputpane_object::test<2>()
    {
        set_test_name("by whose: one object's alone, the scripts open here, the agent's own; and by the words, in either case");
        ALScriptOutputPane& out    = make();
        const LLUUID        door   = fresh();
        const LLUUID        lamp   = fresh();
        const LLUUID        crate  = fresh();
        const LLUUID        script = fresh();
        studio.owned.insert(lamp);
        services.addDoc("door-script", ALScriptRef(door, script), "door.lsl");
        out.heard(said(door, "Door", "door speaks", Event::Channel::Debug, script, "door.lsl"));
        out.heard(said(lamp, "Lamp", "lamp speaks"));
        out.heard(said(crate, "Crate", "crate tells its owner", Event::Channel::OwnerSay));
        out.said("Saved door.lsl.", false, nullptr, {});

        whose(lamp.asString());
        ensure_equals("one object's alone, not the studio's", shown(), std::string("|lamp speaks"));
        whose("open");
        ensure_equals("the open script's, and the studio's", shown(), std::string("|door speaks|Saved door.lsl."));
        whose("mine");
        ensure_equals("the agent's object's, what an owner was told, and the studio's", shown(),
                      std::string("|lamp speaks|crate tells its owner|Saved door.lsl."));

        whose("");
        find("DOOR");
        ensure_equals("the words, in the text or whose it is", shown(), std::string("|door speaks|Saved door.lsl."));
        find("  lamp  ");
        ensure_equals("trimmed", shown(), std::string("|lamp speaks"));
        find("");
        ensure_equals("everything", shownTexts().size(), size_t(4));

        // The script closed: the filter by open scripts follows.
        whose("open");
        services.docs.clear();
        out.openChanged();
        ensure_equals("the studio's alone", shown(), std::string("|Saved door.lsl."));
    }

    template <>
    template <>
    void alscriptoutputpane_object::test<3>()
    {
        set_test_name("the objects offered stop at forty, the one least lately heard from going first, unless it is the one chosen");
        ALScriptOutputPane& out = make();
        std::vector<LLUUID> roots;
        for (S32 i = 0; i < 45; ++i)
        {
            roots.push_back(fresh());
            out.heard(said(roots.back(), llformat("Object %02d", i), "hello"));
        }
        ensure_equals("forty", out.objects().size(), size_t(40));
        ensure("the first five gone", out.objects().front().first == roots[5]);
        // Heard from again: the last to go.
        out.heard(said(roots[5], "Object 05", "again"));
        ensure("heard again, last", out.objects().back().first == roots[5]);
        ensure_equals("still forty", out.objects().size(), size_t(40));
        // Renamed, and heard from under its new name.
        out.heard(said(roots[9], "Renamed", "again"));
        ensure_equals("its new name", out.objects().back().second, std::string("Renamed"));
        // The one chosen stays, least lately heard from as it is.
        whose(roots[6].asString());
        out.heard(said(fresh(), "Newcomer", "hello"));
        ensure_equals("forty still", out.objects().size(), size_t(40));
        ensure("the one chosen stayed", out.objects().front().first == roots[6]);
        ensure("the next went", std::none_of(out.objects().begin(), out.objects().end(), [&](const auto& one) { return one.first == roots[7]; }));
        ensure_equals("and is still chosen", window.find<LLComboBox>("output_filter")->getValue().asString(), roots[6].asString());
    }

    template <>
    template <>
    void alscriptoutputpane_object::test<4>()
    {
        set_test_name("an error or a failure said out of sight is unread until the tab is looked at, or cleared");
        ALScriptOutputPane& out  = make();
        const LLUUID        door = fresh();
        out.heard(said(door, "Door", "tick"));
        out.said("Saved.", false, nullptr, {});
        ensure("words alone are not unread", !out.unread());
        ensure_equals("and nothing to say of it", studio.unreadChanges, 0);
        out.heard(failed(door, fresh(), "door.lsl", "Math Error", -1));
        ensure("an error is", out.unread());
        ensure_equals("said on the title", studio.unreadChanges, 1);
        out.pump();
        ensure("still, out of sight", out.unread());
        studio.inSight = true;
        out.pump();
        ensure("looked at", !out.unread());
        ensure_equals("said again", studio.unreadChanges, 2);
        out.said("Could not save.", true, nullptr, {});
        ensure("a failure in sight is read", !out.unread());
        studio.inSight = false;
        out.said("Could not save.", true, nullptr, {});
        ensure("a failure out of sight is not", out.unread());
        window.find<LLButton>("output_clear")->onCommit();
        ensure("cleared", !out.unread());
        ensure("with what was said", out.view()->entries().empty());
    }

    template <>
    template <>
    void alscriptoutputpane_object::test<5>()
    {
        set_test_name("the studio's words link to the tab they name and to what can be done about it, each handed to the window");
        ALScriptOutputPane& out = make();
        ALScriptStudioDoc&  doc = services.addDoc("door-script", ALScriptRef(fresh(), fresh()), "door.lsl");
        out.said("Could not save door.lsl.", true, &doc, { "retry", "copy" });
        const ALOutputView::Entry& entry = last();
        ensure_equals("the name and two things to do", entry.links.size(), size_t(3));
        ensure_equals("the name, where the words say it", entry.links[0].begin, 15);
        ensure_equals("to its end", entry.links[0].end, 23);
        ensure("each thing after the words", entry.text.find(services.words("ActionRetry")) > entry.text.find("door.lsl."));
        ensure_equals("where it is", entry.text.substr(entry.links[1].begin, entry.links[1].end - entry.links[1].begin), services.words("ActionRetry"));

        ALOutputView::Entry chosen = entry;
        for (const ALOutputView::Entry::Link& link : entry.links)
        {
            chosen.value = link.value;
            out.choose(chosen);
        }
        ensure_equals("the tab, with its problems", studio.shown.size(), size_t(1));
        ensure_equals("the tab named", studio.shown[0], std::string("door-script:problems"));
        ensure_equals("what to do, by name", studio.actions.size(), size_t(2));
        ensure_equals("try again", studio.actions[0], std::string("door-script:retry"));
        ensure_equals("a copy", studio.actions[1], std::string("door-script:copy"));

        // Said of no tab: nothing to link.
        out.said("Nothing open.", false, nullptr, { "retry" });
        ensure("no links", last().links.empty());
        // The tab closed since: nothing asked of the window.
        services.docs.clear();
        out.choose(chosen);
        ensure_equals("nothing more", studio.actions.size(), size_t(2));
    }

    template <>
    template <>
    void alscriptoutputpane_object::test<6>()
    {
        set_test_name("a run's lines read back through the text it runs: the script's own, an include's, and the frames under an error");
        ALScriptOutputPane& out    = make();
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
        ensure_equals("the error's line, the script's", at.line, 4);
        ensure("in the script", at.file.empty());
        const ALOutputView::Entry& entry = last();
        ensure("a link to it", entry.link);
        ensure_equals("at that line", entry.value["line"].asInteger(), 4);
        ensure_equals("a link for each frame of the script's", entry.links.size(), size_t(2));
        ensure_equals("the first in the include", entry.links[0].value["file"].asString(), std::string("disk:/scripts/lib.luau"));
        ensure_equals("at its line", entry.links[0].value["line"].asInteger(), 2);
        ensure_equals("the second in the script", entry.links[1].value["line"].asInteger(), 4);

        ALOutputView::Entry chosen = entry;
        out.choose(chosen);
        chosen.value = entry.links[0].value;
        out.choose(chosen);
        ensure_equals("two places gone to", studio.went.size(), size_t(2));
        ensure("the script", studio.went[0].ref == ALScriptRef(object, item) && studio.went[0].file.empty());
        ensure_equals("at the error's line", studio.went[0].line, 4);
        ensure_equals("the include", studio.went[1].file, std::string("disk:/scripts/lib.luau"));
        ensure_equals("by its name", studio.went[1].name, std::string("lib.luau"));
        ensure_equals("at the frame's line", studio.went[1].line, 2);

        // Not open here: the line the run names, as it named it.
        services.docs.clear();
        ensure_equals("as said", out.heard(error).line, 7);
    }
}
