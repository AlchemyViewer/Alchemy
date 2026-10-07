/**
 * @file alscriptweightspane_test.cpp
 * @brief The Weights tab over the Script Studio's own window.
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

#include "../alscriptweightspane.h"
#include "llbutton.h"

#include "alpanelist.h"
#include "alrecoverystore.h"
#include "llfloater.h"
#include "llpanel.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"

#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace tut
{
    struct alscriptweightspane_data
    {
        ll_test::HeadlessUI&        ui      = ll_test::HeadlessUI::get();
        // The studio's window as the skin has it, which is where the tab's
        // widgets and words are.
        al_studio_test::StudioWindow window;
        LLFloater*                   floater = window.floater;

        LLPanel* tab() const { return floater ? floater->findChild<LLPanel>("weights_tab", true) : nullptr; }
        // The tab as the skin built it, in its window's words.
        ALScriptWeightsPane& weights() const
        {
            ALScriptWeightsPane* pane = floater->findChild<ALScriptWeightsPane>("weights_tab", true);
            ensure("the skin builds the tab as the pane", pane != nullptr);
            return *pane;
        }
        ALPaneList* list(const char* name) const { return floater ? floater->findChild<ALPaneList>(name, true) : nullptr; }

        static std::string cell(const LLScrollListItem* item, S32 column)
        {
            const LLScrollListCell* at = item ? item->getColumn(column) : nullptr;
            return at ? at->getValue().asString() : std::string();
        }
        static std::vector<std::string> column(ALPaneList* list, S32 at)
        {
            std::vector<std::string> out;
            for (const LLScrollListItem* item : list->getAllData())
            {
                out.push_back(cell(item, at));
            }
            return out;
        }
        static std::string joined(const std::vector<std::string>& words)
        {
            std::string out;
            for (const std::string& word : words)
            {
                out += "|" + word;
            }
            return out;
        }

        static ALScriptWeight::Part part(ALScriptWeight::Part::Kind kind, const std::string& name, size_t bytes, S32 line = -1,
                                         const std::string& within = std::string(), const std::string& file = std::string())
        {
            ALScriptWeight::Part p;
            p.kind   = kind;
            p.name   = name;
            p.bytes  = bytes;
            p.line   = line;
            p.column = line >= 0 ? 4 : -1;
            p.within = within;
            p.file   = file;
            return p;
        }

        // An LSL script weighed for its own target, LSO, and the other two:
        // a function from an include, a state with a handler, a global and
        // the registers.
        static ALScriptWeightsPane::Shown lsl()
        {
            using Kind = ALScriptWeight::Part::Kind;
            ALScriptWeightsPane::Shown shown;
            shown.id   = "script-1";
            shown.name = "Door";
            ALScriptWeight lso;
            lso.target   = ALScriptWeight::Target::LSO;
            lso.compiled = true;
            lso.limit    = 16384;
            lso.total    = 4096;
            lso.parts    = { part(Kind::Frame, "registers", 100), part(Kind::Global, "gCount", 10, 0), part(Kind::Function, "twice", 28, 0, "", "disk:/lib.lsl"),
                             part(Kind::State, "default", 217, 5), part(Kind::Handler, "touch_start", 204, 7, "default") };
            ALScriptWeight mono;
            mono.target   = ALScriptWeight::Target::Mono;
            mono.compiled = true;
            mono.estimate = true;
            mono.limit    = 65536;
            mono.total    = 6000;
            mono.parts    = { part(Kind::Frame, "assembly", 2000), part(Kind::Handler, "touch_start", 400, 7, "default") };
            ALScriptWeight luau;
            luau.target = ALScriptWeight::Target::LSLLuau;
            luau.limit  = 131072;
            luau.error  = "it does not compile";
            shown.weights                        = { lso, mono, luau };
            shown.fileNames["disk:/lib.lsl"]     = "lib.lsl";
            return shown;
        }
    };
    typedef test_group<alscriptweightspane_data> alscriptweightspane_group;
    typedef alscriptweightspane_group::object    alscriptweightspane_object;
    alscriptweightspane_group                    alscriptweightspane_instance("ALScriptWeightsPane");

    // The targets side by side, the script's own first and chosen; its
    // parts listed with their bytes and shares, in the viewer's words; a
    // target that does not compile says so rather than a number.
    template<> template<>
    void alscriptweightspane_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ensure("the studio's window builds with its Weights tab", tab() != nullptr);
        ALScriptWeightsPane& pane = weights();
        pane.show(lsl());
        ALPaneList* targets = list("weights_targets");
        ALPaneList* parts   = list("weights_parts");
        ensure_equals("three targets", targets->getItemCount(), 3);
        ensure_equals("its own first", cell(targets->getAllData()[0], 0), std::string("LSL (LSO)"));
        ensure_equals("its code", cell(targets->getAllData()[0], 1), std::string("4.0 KB"));
        ensure_equals("its share", cell(targets->getAllData()[0], 2), std::string("25.0%"));
        ensure_equals("an estimate said as one", cell(targets->getAllData()[1], 1), std::string("~5.9 KB"));
        ensure_equals("one that does not compile has no number", cell(targets->getAllData()[2], 1), std::string("--"));
        ensure("its own chosen", targets->getFirstSelected() && targets->getFirstSelected()->getValue().asInteger() == S32(ALScriptWeight::Target::LSO));

        ensure_equals("its parts", parts->getItemCount(), 5);
        const std::vector<std::string> names = column(parts, 0);
        ensure("the registers in words" + joined(names), std::find(names.begin(), names.end(), "Registers") != names.end());
        ensure("a handler in its state" + joined(names), std::find(names.begin(), names.end(), "touch_start in default") != names.end());
        const std::vector<std::string> lines = column(parts, 5);
        ensure("an include's part at the include's line" + joined(lines), std::find(lines.begin(), lines.end(), "lib.lsl:1") != lines.end());
        ensure("the script's own at its line" + joined(lines), std::find(lines.begin(), lines.end(), "8") != lines.end());
        const std::string head = floater->findChild<LLTextBox>("weights_head", true)->getText();
        ensure("the head says whose and on what: " + head, head.find("Door") != std::string::npos && head.find("LSO") != std::string::npos);
    }

    // Another target chosen lists its parts; a part chosen says where it
    // is, in the include where it is one's; one with no place says nothing.
    template<> template<>
    void alscriptweightspane_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane& pane = weights();
        pane.show(lsl());
        ALPaneList* targets = list("weights_targets");
        ALPaneList* parts   = list("weights_parts");

        parts->selectByValue(LLSD(2));
        std::optional<ALScriptWeightsPane::Place> place = pane.chosenPlace(parts);
        ensure("the include's function has a place", place.has_value());
        ensure_equals("in the include", place->file, std::string("disk:/lib.lsl"));
        ensure_equals("by its name", place->fileName, std::string("lib.lsl"));
        ensure_equals("at its line", place->line, 0);
        ensure_equals("and column", place->column, 4);
        parts->selectByValue(LLSD(0));
        ensure("the registers are nowhere", !pane.chosenPlace(parts));

        targets->selectByValue(LLSD(S32(ALScriptWeight::Target::Mono)));
        targets->onCommit();
        ensure_equals("Mono's parts", parts->getItemCount(), 2);
        // The same script again keeps the target chosen; another starts at
        // its own.
        pane.show(lsl());
        ensure_equals("kept through a refill", parts->getItemCount(), 2);
        ALScriptWeightsPane::Shown other = lsl();
        other.id                         = "script-2";
        pane.show(other);
        ensure_equals("another script at its own", parts->getItemCount(), 5);
    }

    // Since the save: a part heavier or lighter by so much, one new since,
    // and the target's whole; nothing where it was not weighed then.
    template<> template<>
    void alscriptweightspane_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane& pane = weights();
        ALScriptWeightsPane::Shown shown = lsl();
        ALScriptWeight             saved = shown.weights[0];
        saved.total                      = 4000;
        // The handler was lighter, the function was not there, and the
        // global has moved down a line, which is no change.
        saved.parts[4].bytes = 180;
        saved.parts[1].line  = 1;
        saved.parts.erase(saved.parts.begin() + 2);
        shown.saved = { saved };
        pane.show(shown);
        ALPaneList* targets = list("weights_targets");
        ALPaneList* parts   = list("weights_parts");
        ensure_equals("the target's whole", cell(targets->getAllData()[0], 3), std::string("+96"));
        ensure_equals("nothing for a target not weighed then", cell(targets->getAllData()[1], 3), std::string());
        const std::vector<std::string> changes = column(parts, 4);
        ensure_equals("the handler heavier" + joined(changes), changes[4], std::string("+24"));
        ensure_equals("the function new" + joined(changes), changes[2], std::string("new"));
        ensure_equals("the global the same where it moved" + joined(changes), changes[1], std::string());

        // Heaviest first, by the number and not the words.
        parts->sortByColumn("bytes", false);
        parts->updateSort();
        ensure_equals("the state first", cell(parts->getAllData()[0], 2), std::string("217"));
        ensure_equals("the global last", cell(parts->getAllData()[4], 2), std::string("10"));
        parts->sortByColumn("change", false);
        parts->updateSort();
        ensure_equals("the new function moved most", cell(parts->getAllData()[0], 0), std::string("twice"));
        // By name, as the names are shown.
        parts->sortByColumn("part", true);
        parts->updateSort();
        const std::vector<std::string> names = column(parts, 0);
        for (size_t i = 1; i < names.size(); ++i)
        {
            ensure("in the order of their names" + joined(names), LLStringUtil::compareDict(names[i - 1], names[i]) <= 0);
        }
    }

    // Nothing to show says why, and holds nothing to choose.
    template<> template<>
    void alscriptweightspane_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane& pane = weights();
        pane.show(lsl());
        pane.showNothing("Nothing open to weigh.");
        ensure_equals("no targets", list("weights_targets")->getItemCount(), 0);
        ensure_equals("no parts", list("weights_parts")->getItemCount(), 0);
        ensure("nothing chosen", !pane.chosenPlace(pane.partsList()));
        ensure_equals("why", floater->findChild<LLTextBox>("weights_head", true)->getText(), std::string("Nothing open to weigh."));
        ensure("and whose it is forgotten", pane.shownId().empty());
    }

    // Anonymous functions share an empty name: each is matched to the one
    // in its place among them then, not all to their sum.
    template<> template<>
    void alscriptweightspane_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        using Kind = ALScriptWeight::Part::Kind;
        ALScriptWeightsPane&       pane = weights();
        ALScriptWeightsPane::Shown shown;
        shown.id   = "script-3";
        shown.name = "Handlers";
        ALScriptWeight slua;
        slua.target   = ALScriptWeight::Target::SLua;
        slua.compiled = true;
        slua.limit    = 131072;
        slua.total    = 600;
        slua.parts    = { part(Kind::Function, "", 100, 1), part(Kind::Function, "", 200, 5), part(Kind::Function, "", 300, 9) };
        shown.weights = { slua };
        shown.saved   = { slua };
        pane.show(shown);
        std::vector<std::string> changes = column(list("weights_parts"), 4);
        ensure_equals("nothing changed" + joined(changes), joined(changes), std::string("|||"));
        // The middle one heavier, and one added after them.
        ALScriptWeight now = slua;
        now.parts[1].bytes = 250;
        now.parts.push_back(part(Kind::Function, "", 40, 12));
        shown.weights = { now };
        pane.show(shown);
        changes = column(list("weights_parts"), 4);
        ensure_equals("the middle one, and one new" + joined(changes), joined(changes), std::string("||+50||new"));
    }

    template<> template<>
    void alscriptweightspane_object::test<6>()
    {
        set_test_name("a function with no name of its own called by the event it handles, where the outline says so; else said to have none");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        using Kind = ALScriptWeight::Part::Kind;
        ALScriptWeightsPane&       pane = weights();
        ALScriptWeightsPane::Shown shown;
        shown.id   = "script-6";
        shown.name = "Handlers";
        ALScriptWeight slua;
        slua.target   = ALScriptWeight::Target::SLua;
        slua.compiled = true;
        slua.limit    = 131072;
        slua.total    = 300;
        slua.parts    = { part(Kind::Function, "", 200, 0), part(Kind::Function, "", 100, 4) };
        shown.weights = { slua };
        shown.handlers[0] = "touch_start";
        pane.show(shown);
        const std::string names = joined(column(list("weights_parts"), 0));
        ensure("the handler by its event: " + names, names.find("touch_start") != std::string::npos);
        ensure("the other with none: " + names, names.find(window.find<LLPanel>("weights_tab")->getString("WeightsPartUnnamed")) != std::string::npos);
    }

    // What the region reserves for the script's object, where it has said:
    // after the code's size, for all of the object's scripts, with its URLs
    // where it has any; its time, where it told an estate manager; and the
    // tip saying how the region counts each.
    template<> template<>
    void alscriptweightspane_object::test<7>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane&       pane  = weights();
        ALScriptWeightsPane::Shown shown = lsl();
        LLTextBox*                 head  = floater->findChild<LLTextBox>("weights_head", true);
        pane.show(shown);
        ensure("nothing of the region", head->getText().find("region") == std::string::npos);
        ALScriptRegionUsage::Usage reserved;
        reserved.memory = 131072;
        reserved.when   = LLDate(1.8e9);
        shown.region    = reserved;
        shown.id        = "script-2";
        pane.show(shown);
        const std::string said = head->getText();
        ensure("reserved, in KB" + said, said.find("reserves 128 KB for all its scripts") != std::string::npos);
        ensure("as of when" + said, said.find(ALRecoveryEntry::sayWhen(reserved.when)) != std::string::npos);
        ensure("no URLs said where there are none" + said, said.find("URLs") == std::string::npos);
        ensure("the tip says how it counts", head->getToolTip().find("64 KB for Mono") != std::string::npos);
        ensure("and nothing of time unasked", head->getToolTip().find("Top Scripts") == std::string::npos);
        shown.region->urls = 3;
        shown.id           = "script-3";
        pane.show(shown);
        ensure("and URLs where there are" + head->getText(), head->getText().find("128 KB and 3 URLs") != std::string::npos);
        ensure("no time where the region said none" + head->getText(), head->getText().find("ms of the region") == std::string::npos);
        shown.region->time     = 0.125f;
        shown.region->timeWhen = LLDate(1.8e9);
        shown.id               = "script-4";
        pane.show(shown);
        ensure("its time, said to an estate manager" + head->getText(), head->getText().find("take 0.125 ms of the region's time") != std::string::npos);
        ensure("the tip says how both are counted", head->getToolTip().find("64 KB for Mono") != std::string::npos && head->getToolTip().find("Top Scripts") != std::string::npos);
        ALScriptRegionUsage::Usage timed;
        timed.time     = 0.5f;
        timed.timeWhen = LLDate(1.8e9);
        shown.region   = timed;
        shown.id       = "script-5";
        pane.show(shown);
        ensure("time alone: no memory said" + head->getText(),
               head->getText().find("reserves") == std::string::npos && head->getText().find("take 0.500 ms") != std::string::npos);
        ensure("and the tip of the time alone", head->getToolTip().find("64 KB for Mono") == std::string::npos && head->getToolTip().find("Top Scripts") != std::string::npos);
    }

    // LSL on Luau's compiler records no lines, so its lines are not
    // weighed one by one: said, where its heat would show nothing; not of
    // a target whose lines merely weigh nothing.
    template<> template<>
    void alscriptweightspane_object::test<8>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ensure("the studio's window builds with its Weights tab", tab() != nullptr);
        ALScriptWeightsPane& pane = weights();
        ALScriptWeightsPane::Shown shown = lsl();
        shown.weights[2].compiled = true;
        shown.weights[2].error.clear();
        shown.weights[2].total = 2048;
        std::swap(shown.weights[0], shown.weights[2]);
        pane.show(shown);
        const std::string luau = floater->findChild<LLTextBox>("weights_head", true)->getText();
        ensure("said for LSL on Luau: " + luau, luau.find("records no lines") != std::string::npos);
        pane.show(lsl());
        const std::string lso = floater->findChild<LLTextBox>("weights_head", true)->getText();
        ensure("not for LSO: " + lso, lso.find("records no lines") == std::string::npos);
    }

    // A Luau target's strings beside its parts: the starts several share
    // first, in italics, with what they would save; then each string, the
    // heaviest first, its uses, and a function's name said as one; each
    // chosen says where it is first named. No other target has a table.
    template<> template<>
    void alscriptweightspane_object::test<9>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane&       pane = weights();
        ALScriptWeightsPane::Shown shown;
        shown.id   = "script-2";
        shown.name = "Greeter";
        ALScriptWeight slua;
        slua.target   = ALScriptWeight::Target::SLua;
        slua.compiled = true;
        slua.limit    = 131072;
        slua.total    = 900;
        slua.parts    = { part(ALScriptWeight::Part::Kind::Constant, "strings", 120) };
        const auto string = [](const char* text, size_t bytes, size_t uses, size_t loads, S32 line, bool name = false) {
            ALScriptWeight::String one;
            one.text  = text;
            one.bytes = bytes;
            one.uses  = uses;
            one.loads = loads;
            one.line  = line;
            one.name  = name;
            return one;
        };
        slua.strings = { string("greet", 6, 0, 0, 0, true), string("You have touched me, alpha", 27, 1, 1, 4), string("You have touched me, bravo", 27, 2, 2, 2),
                         string("x\ny", 4, 1, 1, 6) };
        ALScriptWeight::SharedStart shared;
        shared.start   = "You have touched me, ";
        shared.strings = { 1, 2 };
        shared.saved   = 9;
        slua.sharedStarts = { shared };
        shown.weights     = { slua };
        pane.show(shown);

        ALPaneList* strings = list("weights_strings");
        ensure("the list there", strings && strings->getParent()->getVisible());
        ensure_equals("a start, then each string", strings->getItemCount(), 5);
        const std::vector<std::string> texts = column(strings, 0);
        ensure("the start first: " + texts[0], texts[0].find("Shared start") == 0 && texts[0].find("\"You have touched me, \"") != std::string::npos &&
                                                   texts[0].find("2 strings") != std::string::npos);
        ensure_equals("what it saves, as less", column(strings, 1)[0], std::string("-9"));
        ensure_equals("its strings' loads", column(strings, 2)[0], std::string("3"));
        ensure_equals("at the first of them", column(strings, 4)[0], std::string("3"));
        ensure_equals("then the heaviest first, a break shown as one", joined(texts).substr(joined({ texts[0] }).size()),
                      std::string("|\"You have touched me, alpha\"|\"You have touched me, bravo\"|\"greet\"|\"x\\ny\""));
        ensure_equals("each one's uses, a function's name said as one", joined(column(strings, 2)), std::string("|3|1|2|name|1"));

        strings->selectByValue(LLSD(2));
        std::optional<ALScriptWeightsPane::Place> place = pane.chosenPlace(strings);
        ensure("a string chosen: where it is first named", place && place->file.empty() && place->line == 2);
        strings->selectByValue(LLSD(0));
        place = pane.chosenPlace(strings);
        ensure("the start chosen: the first of its strings", place && place->line == 2);
        ensure("not the parts' choice", !pane.chosenPlace(list("weights_parts")));

        // Kept chosen through a refill, by what it is.
        strings->selectByValue(LLSD(1));
        pane.show(shown);
        ensure("still chosen", strings->getFirstSelected() && strings->getFirstSelected()->getValue().asInteger() == 1);

        // A start's tip names its strings; each of them says it shares one.
        const auto tip = [&](S32 row) {
            const LLScrollListItem* item = strings->getAllData()[static_cast<size_t>(row)];
            return item->getColumn(0)->getToolTip();
        };
        ensure("the start's strings named: " + tip(0), tip(0).find("\"You have touched me, alpha\"") != std::string::npos &&
                                                          tip(0).find("\"You have touched me, bravo\"") != std::string::npos);
        ensure("a string of it says so: " + tip(1), tip(1).find("shares its start") != std::string::npos);
        ensure("one that is not, nothing: " + tip(4), tip(4).find("shares its start") == std::string::npos);

        // Since the save: a string not there then, new; a start by how its
        // saving moved, said as its bytes are, as less: saving more since,
        // it is lighter.
        ALScriptWeight then = slua;
        then.strings.pop_back();
        then.sharedStarts[0].saved = 4;
        shown.saved                = { then };
        pane.show(shown);
        ensure_equals("since the save", joined(column(strings, 3)), std::string("|-5||||new"));

        // Keeping the start once asks the window, with its strings.
        std::string              kept;
        std::vector<std::string> of;
        pane.setKeepStart([&](const std::string& start, const std::vector<std::string>& texts) {
            kept = start;
            of   = texts;
        });
        strings->selectByValue(LLSD(0));
        floater->findChild<LLButton>("weights_keep_start", true)->onCommit();
        ensure("asked with the start and its strings", kept == "You have touched me, " && of.size() == 2 && of[1] == "You have touched me, bravo");
        kept.clear();
        strings->selectByValue(LLSD(1));
        floater->findChild<LLButton>("weights_keep_start", true)->onCommit();
        ensure("not for a string", kept.empty());
        // Nor in a script that cannot be written to.
        pane.setKeepStart([&](const std::string& start, const std::vector<std::string>&) { kept = start; }, []() { return false; });
        strings->selectByValue(LLSD(0));
        floater->findChild<LLButton>("weights_keep_start", true)->onCommit();
        ensure("not where the script cannot be written to", kept.empty());

        // Nothing to show hides the list with the rest.
        pane.showNothing("Nothing open to weigh.");
        ensure("hidden with nothing shown", !strings->getParent()->getVisible() && strings->getItemCount() == 0);
        pane.show(shown);
        ensure("shown again", strings->getParent()->getVisible());

        // LSO has no table: no list.
        pane.show(lsl());
        ensure("hidden for LSO", !strings->getParent()->getVisible() && strings->getItemCount() == 0);
    }
}
