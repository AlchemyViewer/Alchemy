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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace
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
        ALScriptWeightsPane& require_pane() const
        {
            ALScriptWeightsPane* pane = floater->findChild<ALScriptWeightsPane>("weights_tab", true);
            INFO("the skin builds the tab as the pane");
            REQUIRE(pane != nullptr);
            return *pane;
        }
        // One of the tab's lists, by its name in the skin.
        ALPaneList* require_list(const char* name) const
        {
            ALPaneList* found = floater ? floater->findChild<ALPaneList>(name, true) : nullptr;
            CAPTURE(name);
            REQUIRE(found != nullptr);
            return found;
        }
        // The head over the lists, which says whose weights they are.
        LLTextBox* require_head() const
        {
            LLTextBox* head = floater ? floater->findChild<LLTextBox>("weights_head", true) : nullptr;
            REQUIRE(head != nullptr);
            return head;
        }

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

    // The targets side by side, the script's own first and chosen; its
    // parts listed with their bytes and shares, in the viewer's words; a
    // target that does not compile says so rather than a number.
    TEST_CASE_METHOD(alscriptweightspane_data, "ALScriptWeightsPane: targets are listed with the script's own first and chosen", "[ALScriptWeightsPane]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        UNSCOPED_INFO("the studio's window builds with its Weights tab");
        REQUIRE(tab() != nullptr);
        ALScriptWeightsPane& pane = require_pane();
        pane.show(lsl());
        ALPaneList* targets = require_list("weights_targets");
        ALPaneList* parts   = require_list("weights_parts");
        REQUIRE(targets->getItemCount() == 3);
        UNSCOPED_INFO("its own first");
        CHECK(cell(targets->getAllData()[0], 0) == std::string("LSL (LSO)"));
        UNSCOPED_INFO("its code");
        CHECK(cell(targets->getAllData()[0], 1) == std::string("4.0 KB"));
        UNSCOPED_INFO("its share");
        CHECK(cell(targets->getAllData()[0], 2) == std::string("25.0%"));
        UNSCOPED_INFO("an estimate said as one");
        CHECK(cell(targets->getAllData()[1], 1) == std::string("~5.9 KB"));
        UNSCOPED_INFO("one that does not compile has no number");
        CHECK(cell(targets->getAllData()[2], 1) == std::string("--"));
        {
            INFO("its own chosen");
            REQUIRE(targets->getFirstSelected());
            CHECK(targets->getFirstSelected()->getValue().asInteger() == S32(ALScriptWeight::Target::LSO));
        }

        CHECK(parts->getItemCount() == 5);
        const std::vector<std::string> names = column(parts, 0);
        UNSCOPED_INFO("the registers in words" + joined(names));
        CHECK(std::find(names.begin(), names.end(), "Registers") != names.end());
        UNSCOPED_INFO("a handler in its state" + joined(names));
        CHECK(std::find(names.begin(), names.end(), "touch_start in default") != names.end());
        const std::vector<std::string> lines = column(parts, 5);
        UNSCOPED_INFO("an include's part at the include's line" + joined(lines));
        CHECK(std::find(lines.begin(), lines.end(), "lib.lsl:1") != lines.end());
        UNSCOPED_INFO("the script's own at its line" + joined(lines));
        CHECK(std::find(lines.begin(), lines.end(), "8") != lines.end());
        const std::string head = require_head()->getText();
        {
            INFO("the head says whose and on what: " + head);
            CHECK(head.find("Door") != std::string::npos);
            CHECK(head.find("LSO") != std::string::npos);
        }
    }

    // Another target chosen lists its parts; a part chosen says where it
    // is, in the include where it is one's; one with no place says nothing.
    TEST_CASE_METHOD(alscriptweightspane_data, "ALScriptWeightsPane: another target chosen lists its parts and a part says where it is", "[ALScriptWeightsPane]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane& pane = require_pane();
        pane.show(lsl());
        ALPaneList* targets = require_list("weights_targets");
        ALPaneList* parts   = require_list("weights_parts");

        parts->selectByValue(LLSD(2));
        std::optional<ALScriptWeightsPane::Place> place = pane.chosenPlace(parts);
        UNSCOPED_INFO("the include's function has a place");
        REQUIRE(place.has_value());
        CHECK(place->file == std::string("disk:/lib.lsl"));
        CHECK(place->fileName == std::string("lib.lsl"));
        CHECK(place->line == 0);
        CHECK(place->column == 4);
        parts->selectByValue(LLSD(0));
        UNSCOPED_INFO("the registers are nowhere");
        CHECK_FALSE(pane.chosenPlace(parts));

        targets->selectByValue(LLSD(S32(ALScriptWeight::Target::Mono)));
        targets->onCommit();
        UNSCOPED_INFO("Mono's parts");
        CHECK(parts->getItemCount() == 2);
        // The same script again keeps the target chosen; another starts at
        // its own.
        pane.show(lsl());
        UNSCOPED_INFO("kept through a refill");
        CHECK(parts->getItemCount() == 2);
        ALScriptWeightsPane::Shown other = lsl();
        other.id                         = "script-2";
        pane.show(other);
        UNSCOPED_INFO("another script at its own");
        CHECK(parts->getItemCount() == 5);
    }

    // Since the save: a part heavier or lighter by so much, one new since,
    // and the target's whole; nothing where it was not weighed then.
    TEST_CASE_METHOD(alscriptweightspane_data, "ALScriptWeightsPane: changes since the save show for each part and the target's whole", "[ALScriptWeightsPane]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane& pane = require_pane();
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
        ALPaneList* targets = require_list("weights_targets");
        ALPaneList* parts   = require_list("weights_parts");
        REQUIRE(targets->getItemCount() == 3);
        REQUIRE(parts->getItemCount() == 5);
        UNSCOPED_INFO("the target's whole");
        CHECK(cell(targets->getAllData()[0], 3) == std::string("+96"));
        UNSCOPED_INFO("nothing for a target not weighed then");
        CHECK(cell(targets->getAllData()[1], 3) == std::string());
        const std::vector<std::string> changes = column(parts, 4);
        UNSCOPED_INFO("the handler heavier" + joined(changes));
        CHECK(changes[4] == std::string("+24"));
        UNSCOPED_INFO("the function new" + joined(changes));
        CHECK(changes[2] == std::string("new"));
        UNSCOPED_INFO("the global the same where it moved" + joined(changes));
        CHECK(changes[1] == std::string());

        // Heaviest first, by the number and not the words.
        parts->sortByColumn("bytes", false);
        parts->updateSort();
        UNSCOPED_INFO("the state first");
        CHECK(cell(parts->getAllData()[0], 2) == std::string("217"));
        UNSCOPED_INFO("the global last");
        CHECK(cell(parts->getAllData()[4], 2) == std::string("10"));
        parts->sortByColumn("change", false);
        parts->updateSort();
        UNSCOPED_INFO("the new function moved most");
        CHECK(cell(parts->getAllData()[0], 0) == std::string("twice"));
        // By name, as the names are shown.
        parts->sortByColumn("part", true);
        parts->updateSort();
        const std::vector<std::string> names = column(parts, 0);
        for (size_t i = 1; i < names.size(); ++i)
        {
            UNSCOPED_INFO("in the order of their names" + joined(names));
            REQUIRE(LLStringUtil::compareDict(names[i - 1], names[i]) <= 0);
        }
    }

    TEST_CASE_METHOD(alscriptweightspane_data, "ALScriptWeightsPane: showNothing says why and holds nothing to choose", "[ALScriptWeightsPane]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane& pane = require_pane();
        pane.show(lsl());
        pane.showNothing("Nothing open to weigh.");
        ALPaneList* targets = require_list("weights_targets");
        ALPaneList* parts   = require_list("weights_parts");
        LLTextBox*  head    = require_head();
        CHECK(targets->getItemCount() == 0);
        CHECK(parts->getItemCount() == 0);
        CHECK_FALSE(pane.chosenPlace(pane.partsList()));
        CHECK(head->getText() == std::string("Nothing open to weigh."));
        UNSCOPED_INFO("and whose it is forgotten");
        CHECK(pane.shownId().empty());
    }

    // Anonymous functions share an empty name: each is matched to the one
    // in its place among them then, not all to their sum.
    TEST_CASE_METHOD(alscriptweightspane_data, "ALScriptWeightsPane: anonymous functions are matched to the saved ones by their place", "[ALScriptWeightsPane]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        using Kind = ALScriptWeight::Part::Kind;
        ALScriptWeightsPane&       pane = require_pane();
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
        std::vector<std::string> changes = column(require_list("weights_parts"), 4);
        UNSCOPED_INFO("nothing changed");
        CHECK(joined(changes) == std::string("|||"));
        // The middle one heavier, and one added after them.
        ALScriptWeight now = slua;
        now.parts[1].bytes = 250;
        now.parts.push_back(part(Kind::Function, "", 40, 12));
        shown.weights = { now };
        pane.show(shown);
        changes = column(require_list("weights_parts"), 4);
        UNSCOPED_INFO("the middle one, and one new");
        CHECK(joined(changes) == std::string("||+50||new"));
    }

    TEST_CASE_METHOD(alscriptweightspane_data, "ALScriptWeightsPane: an unnamed handler is named by its event and others said to be unnamed", "[ALScriptWeightsPane]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        using Kind = ALScriptWeight::Part::Kind;
        ALScriptWeightsPane&       pane = require_pane();
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
        const std::string names = joined(column(require_list("weights_parts"), 0));
        UNSCOPED_INFO("the handler by its event: " + names);
        CHECK(names.find("touch_start") != std::string::npos);
        UNSCOPED_INFO("the other with none: " + names);
        CHECK(names.find(window.find<LLPanel>("weights_tab")->getString("WeightsPartUnnamed")) != std::string::npos);
    }

    // What the region reserves for the script's object, where it has said:
    // after the code's size, for all of the object's scripts, with its URLs
    // where it has any; its time, where it told an estate manager; and the
    // tip saying how the region counts each.
    TEST_CASE_METHOD(alscriptweightspane_data, "ALScriptWeightsPane: the head says what the region reserves for the object where known", "[ALScriptWeightsPane]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane&       pane  = require_pane();
        ALScriptWeightsPane::Shown shown = lsl();
        LLTextBox*                 head  = require_head();
        pane.show(shown);
        UNSCOPED_INFO("nothing of the region");
        CHECK(head->getText().find("region") == std::string::npos);
        ALScriptRegionUsage::Usage reserved;
        reserved.memory = 131072;
        reserved.when   = LLDate(1.8e9);
        shown.region    = reserved;
        shown.id        = "script-2";
        pane.show(shown);
        const std::string said = head->getText();
        UNSCOPED_INFO("reserved, in KB" + said);
        CHECK(said.find("reserves 128 KB for all its scripts") != std::string::npos);
        UNSCOPED_INFO("as of when" + said);
        CHECK(said.find(ALRecoveryEntry::sayWhen(reserved.when)) != std::string::npos);
        UNSCOPED_INFO("no URLs said where there are none" + said);
        CHECK(said.find("URLs") == std::string::npos);
        UNSCOPED_INFO("the tip says how it counts");
        CHECK(head->getToolTip().find("64 KB for Mono") != std::string::npos);
        UNSCOPED_INFO("and nothing of time unasked");
        CHECK(head->getToolTip().find("Top Scripts") == std::string::npos);
        shown.region->urls = 3;
        shown.id           = "script-3";
        pane.show(shown);
        UNSCOPED_INFO("and URLs where there are" + head->getText());
        CHECK(head->getText().find("128 KB and 3 URLs") != std::string::npos);
        UNSCOPED_INFO("no time where the region said none" + head->getText());
        CHECK(head->getText().find("ms of the region") == std::string::npos);
        shown.region->time     = 0.125f;
        shown.region->timeWhen = LLDate(1.8e9);
        shown.id               = "script-4";
        pane.show(shown);
        UNSCOPED_INFO("its time, said to an estate manager" + head->getText());
        CHECK(head->getText().find("take 0.125 ms of the region's time") != std::string::npos);
        {
            INFO("the tip says how both are counted");
            CHECK(head->getToolTip().find("64 KB for Mono") != std::string::npos);
            CHECK(head->getToolTip().find("Top Scripts") != std::string::npos);
        }
        ALScriptRegionUsage::Usage timed;
        timed.time     = 0.5f;
        timed.timeWhen = LLDate(1.8e9);
        shown.region   = timed;
        shown.id       = "script-5";
        pane.show(shown);
        {
            INFO("time alone: no memory said" + head->getText());
            CHECK(head->getText().find("reserves") == std::string::npos);
            CHECK(head->getText().find("take 0.500 ms") != std::string::npos);
        }
        {
            INFO("and the tip of the time alone");
            CHECK(head->getToolTip().find("64 KB for Mono") == std::string::npos);
            CHECK(head->getToolTip().find("Top Scripts") != std::string::npos);
        }
    }

    // LSL on Luau's compiler records no lines, so its lines are not
    // weighed one by one: said, where its heat would show nothing; not of
    // a target whose lines merely weigh nothing.
    TEST_CASE_METHOD(alscriptweightspane_data, "ALScriptWeightsPane: LSL on Luau's compiler is said to record no lines and LSO is not", "[ALScriptWeightsPane]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        UNSCOPED_INFO("the studio's window builds with its Weights tab");
        REQUIRE(tab() != nullptr);
        ALScriptWeightsPane& pane = require_pane();
        ALScriptWeightsPane::Shown shown = lsl();
        shown.weights[2].compiled = true;
        shown.weights[2].error.clear();
        shown.weights[2].total = 2048;
        std::swap(shown.weights[0], shown.weights[2]);
        pane.show(shown);
        const std::string luau = require_head()->getText();
        UNSCOPED_INFO("said for LSL on Luau: " + luau);
        CHECK(luau.find("records no lines") != std::string::npos);
        pane.show(lsl());
        const std::string lso = require_head()->getText();
        UNSCOPED_INFO("not for LSO: " + lso);
        CHECK(lso.find("records no lines") == std::string::npos);
    }

    // A Luau target's strings beside its parts: the starts several share
    // first, in italics, with what they would save; then each string, the
    // heaviest first, its uses, and a function's name said as one; each
    // chosen says where it is first named. No other target has a table.
    TEST_CASE_METHOD(alscriptweightspane_data, "ALScriptWeightsPane: a Luau target lists its strings beside its parts", "[ALScriptWeightsPane]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane&       pane = require_pane();
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

        ALPaneList* strings = require_list("weights_strings");
        UNSCOPED_INFO("the list there");
        CHECK(strings->getParent()->getVisible());
        UNSCOPED_INFO("a start, then each string");
        REQUIRE(strings->getItemCount() == 5);
        const std::vector<std::string> texts = column(strings, 0);
        {
            INFO("the start first: " + texts[0]);
            CHECK(texts[0].find("Shared start") == 0);
            CHECK(texts[0].find("\"You have touched me, \"") != std::string::npos);
            CHECK(texts[0].find("2 strings") != std::string::npos);
        }
        UNSCOPED_INFO("what it saves, as less");
        CHECK(column(strings, 1)[0] == std::string("-9"));
        UNSCOPED_INFO("its strings' loads");
        CHECK(column(strings, 2)[0] == std::string("3"));
        UNSCOPED_INFO("at the first of them");
        CHECK(column(strings, 4)[0] == std::string("3"));
        UNSCOPED_INFO("then the heaviest first, a break shown as one");
        CHECK(joined(texts).substr(joined({ texts[0] }).size()) ==
                      std::string("|\"You have touched me, alpha\"|\"You have touched me, bravo\"|\"greet\"|\"x\\ny\""));
        UNSCOPED_INFO("each one's uses, a function's name said as one");
        CHECK(joined(column(strings, 2)) == std::string("|3|1|2|name|1"));

        strings->selectByValue(LLSD(2));
        std::optional<ALScriptWeightsPane::Place> place = pane.chosenPlace(strings);
        {
            INFO("a string chosen: where it is first named");
            REQUIRE(place);
            CHECK(place->file.empty());
            CHECK(place->line == 2);
        }
        strings->selectByValue(LLSD(0));
        place = pane.chosenPlace(strings);
        {
            INFO("the start chosen: the first of its strings");
            REQUIRE(place);
            CHECK(place->line == 2);
        }
        ALPaneList* parts = require_list("weights_parts");
        UNSCOPED_INFO("not the parts' choice");
        CHECK_FALSE(pane.chosenPlace(parts));

        // Kept chosen through a refill, by what it is.
        strings->selectByValue(LLSD(1));
        pane.show(shown);
        {
            INFO("still chosen");
            REQUIRE(strings->getFirstSelected());
            CHECK(strings->getFirstSelected()->getValue().asInteger() == 1);
        }

        // A start's tip names its strings; each of them says it shares one.
        const auto tip = [&](S32 row) {
            const LLScrollListItem* item = strings->getAllData()[static_cast<size_t>(row)];
            return item->getColumn(0)->getToolTip();
        };
        {
            INFO("the start's strings named: " + tip(0));
            CHECK(tip(0).find("\"You have touched me, alpha\"") != std::string::npos);
            CHECK(tip(0).find("\"You have touched me, bravo\"") != std::string::npos);
        }
        UNSCOPED_INFO("a string of it says so: " + tip(1));
        CHECK(tip(1).find("shares its start") != std::string::npos);
        UNSCOPED_INFO("one that is not, nothing: " + tip(4));
        CHECK(tip(4).find("shares its start") == std::string::npos);

        // Since the save: a string not there then, new; a start by how its
        // saving moved, said as its bytes are, as less: saving more since,
        // it is lighter.
        ALScriptWeight then = slua;
        then.strings.pop_back();
        then.sharedStarts[0].saved = 4;
        shown.saved                = { then };
        pane.show(shown);
        UNSCOPED_INFO("since the save");
        CHECK(joined(column(strings, 3)) == std::string("|-5||||new"));

        // Keeping the start once asks the window, with its strings.
        std::string              kept;
        std::vector<std::string> of;
        pane.setKeepStart([&](const std::string& start, const std::vector<std::string>& texts) {
            kept = start;
            of   = texts;
        });
        LLButton* keep = floater->findChild<LLButton>("weights_keep_start", true);
        REQUIRE(keep);
        strings->selectByValue(LLSD(0));
        keep->onCommit();
        {
            INFO("asked with the start and its strings");
            CHECK(kept == "You have touched me, ");
            REQUIRE(of.size() == 2);
            CHECK(of[1] == "You have touched me, bravo");
        }
        kept.clear();
        strings->selectByValue(LLSD(1));
        keep->onCommit();
        UNSCOPED_INFO("not for a string");
        CHECK(kept.empty());
        // Nor in a script that cannot be written to.
        pane.setKeepStart([&](const std::string& start, const std::vector<std::string>&) { kept = start; }, []() { return false; });
        strings->selectByValue(LLSD(0));
        keep->onCommit();
        UNSCOPED_INFO("not where the script cannot be written to");
        CHECK(kept.empty());

        // Nothing to show hides the list with the rest.
        pane.showNothing("Nothing open to weigh.");
        {
            INFO("hidden with nothing shown");
            CHECK_FALSE(strings->getParent()->getVisible());
            CHECK(strings->getItemCount() == 0);
        }
        pane.show(shown);
        UNSCOPED_INFO("shown again");
        CHECK(strings->getParent()->getVisible());

        // LSO has no table: no list.
        pane.show(lsl());
        {
            INFO("hidden for LSO");
            CHECK_FALSE(strings->getParent()->getVisible());
            CHECK(strings->getItemCount() == 0);
        }
    }
}
