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

#include "alcodeeditor.h"
#include "aldockpanel.h"
#include "aljumpbar.h"
#include "aloutputview.h"
#include "alpanelist.h"
#include "alscopebar.h"
#include "altabstrip.h"
#include "llfloater.h"
#include "llpanel.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"
#include "llxmlnode.h"

#include "../../llui/tests/alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <algorithm>
#include <string>
#include <vector>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gWeightsTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gWeightsTestAnonName;
}

namespace tut
{
    struct alscriptweightspane_data
    {
        ll_test::HeadlessUI& ui      = ll_test::HeadlessUI::get();
        LLPanel*             stage   = nullptr;
        LLFloater*           floater = nullptr;

        // The studio's window as the skin has it, which is where the tab's
        // widgets and words are.
        alscriptweightspane_data()
        {
            // Its widgets made reachable: a static library links a widget's
            // registrar only with the object that holds its block.
            ALCodeEditor::Params editor;
            ALDockPanel::Params  dock;
            ALJumpBar::Params    jump;
            ALOutputView::Params output;
            ALPaneList::Params   pane;
            ALScopeBar::Params   scope;
            ALTabStrip::Params   tabs;
            (void)editor.name;
            (void)dock.name;
            (void)jump.name;
            (void)output.name;
            (void)pane.name;
            (void)scope.name;
            (void)tabs.name;
            if (!ui.ok())
            {
                return;
            }
            LLXMLNodePtr node;
            if (!LLUICtrlFactory::getLayeredXMLNode("floater_script_studio.xml", node))
            {
                return;
            }
            LLPanel::Params sp(LLUICtrlFactory::getDefaultParams<LLPanel>());
            sp.name = "stage";
            sp.rect = LLRect(0, 1080, 1920, 0);
            stage   = LLUICtrlFactory::create<LLPanel>(sp);
            floater = new LLFloater(LLSD(), LLFloater::getDefaultParams());
            stage->addChild(floater);
            if (!floater->initFloaterXML(node, stage, "floater_script_studio.xml"))
            {
                floater = nullptr;
            }
        }
        ~alscriptweightspane_data() { delete stage; }

        LLPanel* tab() const { return floater ? floater->findChild<LLPanel>("weights_tab", true) : nullptr; }
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
        ALScriptWeightsPane pane(*tab(), *floater);
        pane.show(lsl());
        ALPaneList* targets = list("weights_targets");
        ALPaneList* parts   = list("weights_parts");
        ensure_equals("three targets", targets->getItemCount(), 3);
        ensure_equals("its own first", cell(targets->getAllData()[0], 0), std::string("LSO"));
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
        ALScriptWeightsPane pane(*tab(), *floater);
        pane.show(lsl());
        ALPaneList* targets = list("weights_targets");
        ALPaneList* parts   = list("weights_parts");

        parts->selectByValue(LLSD(2));
        std::optional<ALScriptWeightsPane::Place> place = pane.chosenPlace();
        ensure("the include's function has a place", place.has_value());
        ensure_equals("in the include", place->file, std::string("disk:/lib.lsl"));
        ensure_equals("by its name", place->fileName, std::string("lib.lsl"));
        ensure_equals("at its line", place->line, 0);
        ensure_equals("and column", place->column, 4);
        parts->selectByValue(LLSD(0));
        ensure("the registers are nowhere", !pane.chosenPlace());

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
        ALScriptWeightsPane pane(*tab(), *floater);
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
    }

    // Nothing to show says why, and holds nothing to choose.
    template<> template<>
    void alscriptweightspane_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScriptWeightsPane pane(*tab(), *floater);
        pane.show(lsl());
        pane.showNothing("Nothing open to weigh.");
        ensure_equals("no targets", list("weights_targets")->getItemCount(), 0);
        ensure_equals("no parts", list("weights_parts")->getItemCount(), 0);
        ensure("nothing chosen", !pane.chosenPlace());
        ensure_equals("why", floater->findChild<LLTextBox>("weights_head", true)->getText(), std::string("Nothing open to weigh."));
        ensure("and whose it is forgotten", pane.shownId().empty());
    }
}
