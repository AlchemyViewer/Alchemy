/**
 * @file alscriptstudiodoc_test.cpp
 * @brief A Script Studio tab's own logic, and the fake of the window's services units are tested with.
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

#include "../alscriptstudiodoc.h"

#include "../alnotecardembedded.h"
#include "aluploadheader.h"
#include "aldiffview.h"

#include "alscriptstudio_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

namespace
{
    // The world as a notecard's items see it, answering nothing: what a
    // tab's items need to be made, and no more.
    class NoWorld final : public ALNotecardEmbedded::World
    {
    public:
        std::string iconOf(const LLInventoryItem&) const override { return std::string(); }
        bool        draggedFromNotecard() const override { return false; }
        bool        carriesSettings() const override { return false; }
        bool        mayCopy(const LLInventoryItem&) const override { return false; }
        U32         frame() const override { return 0; }
        bool        open(const LLPointer<LLInventoryItem>&, const ALScriptRef&, std::function<void(const LLUUID&, U32)>) override { return true; }
        void        confirmCopy(std::function<void()>) override {}
        bool        askCopy(const ALScriptRef&, const LLUUID&, const LLUUID&, U32, std::function<void(const std::string&)>) override { return false; }
        void        pressedAt(S32, S32) override {}
        bool        pastDragStart(S32, S32) override { return false; }
        void        dragOut(const LLInventoryItem&, const ALScriptRef&) override {}
    };
}

namespace
{
    struct alscriptstudiodoc_data
    {
        using Doc = ALScriptStudioDoc;

        static ALScriptFix fix(ALScriptFix::Kind kind)
        {
            ALScriptFix one;
            one.kind = kind;
            return one;
        }
    };

    TEST_CASE_METHOD(alscriptstudiodoc_data, "ALScriptStudioDoc: what counts as unsaved and when a save is under way", "[ALScriptStudioDoc]")
    {
        Doc doc;
        UNSCOPED_INFO("nothing changed");
        CHECK_FALSE(doc.unsaved());
        doc.targetChosen = true;
        UNSCOPED_INFO("a compile target picked");
        CHECK(doc.unsaved());
        doc.targetChosen     = false;
        doc.experienceChosen = true;
        UNSCOPED_INFO("an experience picked");
        CHECK(doc.unsaved());

        UNSCOPED_INFO("no save");
        CHECK_FALSE(doc.saveUnderway());
        doc.preprocessing = true;
        UNSCOPED_INFO("the preprocessor running, which a save may wait on");
        CHECK(doc.saveUnderway());
        doc.preprocessing = false;
        ALScriptSaveFlow::Tab tab;
        tab.holdOnErrors = true;
        doc.save.route(tab);
        UNSCOPED_INFO("waiting on a check");
        CHECK(doc.saveUnderway());
        doc.save.checked();
        UNSCOPED_INFO("and not once it has come");
        CHECK_FALSE(doc.saveUnderway());
        doc.save.sent(ALTextUndo::SavePoint(), std::nullopt, {});
        UNSCOPED_INFO("sent");
        CHECK(doc.saveUnderway());
    }

    TEST_CASE_METHOD(alscriptstudiodoc_data, "ALScriptStudioDoc: the expansion is in front only where there is one", "[ALScriptStudioDoc]")
    {
        Doc doc;
        doc.view = Doc::View::Expanded;
        UNSCOPED_INFO("nothing expanded yet: the source");
        CHECK(doc.shownView() == Doc::View::Source);
        UNSCOPED_INFO("and its text");
        CHECK(doc.shownText() == doc.editor);
    }

    TEST_CASE_METHOD(alscriptstudiodoc_data, "ALScriptStudioDoc: levelOf reads compiler words and severities and ProblemTraits a row", "[ALScriptStudioDoc]")
    {
        CHECK(Doc::levelOf("WARNING") == Doc::Level::Warning);
        CHECK(Doc::levelOf("WARN") == Doc::Level::Warning);
        {
            INFO("anything else is an error, as the server has two");
            CHECK(Doc::levelOf("ERROR") == Doc::Level::Error);
            CHECK(Doc::levelOf("oops") == Doc::Level::Error);
        }
        CHECK(Doc::levelOf(ALScriptProblem::Severity::Note) == Doc::Level::Note);
        CHECK(std::string(Doc::levelName(Doc::Level::Warning)) == "WARNING");

        Doc::Shown shown;
        shown.level    = Doc::Level::Warning;
        shown.origin   = "Luau";
        shown.message  = "unused local";
        shown.fileName = "util.luau";
        shown.fixes    = { fix(ALScriptFix::Kind::Suppress) };
        UNSCOPED_INFO("a suppression is no fix");
        CHECK_FALSE(Doc::ProblemTraits::fixable(shown));
        shown.fixes.push_back(fix(ALScriptFix::Kind::Fix));
        UNSCOPED_INFO("a change is");
        CHECK(Doc::ProblemTraits::fixable(shown));
        UNSCOPED_INFO("found by its level's name");
        CHECK(Doc::ProblemTraits::mentions(shown, "warn"));
        UNSCOPED_INFO("by its include");
        CHECK(Doc::ProblemTraits::mentions(shown, "util"));
        UNSCOPED_INFO("not by what it does not say");
        CHECK_FALSE(Doc::ProblemTraits::mentions(shown, "state"));
        UNSCOPED_INFO("its rule is where it came from");
        CHECK(Doc::ProblemTraits::rule(shown) == "Luau");
        CHECK(Doc::ProblemTraits::level(shown) == ALFindingLevel::Warning);
    }

    TEST_CASE_METHOD(alscriptstudiodoc_data, "ALScriptStudioDoc: the fake services say skin words and counts and find tabs", "[ALScriptStudioDoc]")
    {
        al_studio_test::StudioWindow window;
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        al_studio_test::FakeServices services(window.floater);
        UNSCOPED_INFO("a word of the skin's");
        CHECK(services.words("ObjectNameComing") == std::string("\xE2\x80\xA6"));
        CHECK(services.counted("Fixes", 1) == std::string("1 fix"));
        CHECK(services.counted("Fixes", 3) == std::string("3 fixes"));

        LLUUID object, item;
        object.generate();
        item.generate();
        const ALScriptRef ref(object, item);
        Doc& script = services.addDoc("script", ref, "Door");
        Doc& file   = services.addDoc("disk:/lib.lsl");
        file.file   = "/lib.lsl";
        UNSCOPED_INFO("the first in front");
        CHECK(services.frontDoc() == &script);
        UNSCOPED_INFO("by id");
        CHECK(services.findDoc("disk:/lib.lsl") == &file);
        UNSCOPED_INFO("by what it holds");
        CHECK(services.findDoc(ref) == &script);
        CHECK(services.findDoc("nothing") == nullptr);

        services.report("Saved", false, &script, { "retry" });
        {
            INFO("said of the tab");
            REQUIRE(services.reports.size() == 1);
            CHECK(services.reports.front().doc == "script");
            CHECK(services.reports.front().actions.size() == 1);
        }
        services.openScript(ref, "Door", std::nullopt, 12);
        {
            INFO("asked to open at a line");
            REQUIRE(services.opened.size() == 1);
            CHECK(services.opened.front().line == 12);
        }
    }

    TEST_CASE_METHOD(alscriptstudiodoc_data, "ALScriptStudioDoc: namedIn finds an include line or a require call and foundAs a run's find", "[ALScriptStudioDoc]")
    {
        const ALTextDocument lsl("default {}\n  #  include \"lib/util.lsl\" // the helpers\n#include <sys.lsl>\n#define X \"y.lsl\"\n");
        const std::vector<ALPreprocessor::Required> none;
        const std::optional<Doc::Named> util = Doc::namedIn(lsl, ALTextPos(1, 0), false, none);
        {
            INFO("on its line, before the #");
            REQUIRE(util.has_value());
            CHECK(util->name == "lib/util.lsl");
            CHECK_FALSE(util->require);
        }
        UNSCOPED_INFO("the whole line");
        CHECK(util->range == ALTextRange(ALTextPos(1, 0), ALTextPos(1, lsl.lineLength(1))));
        const std::optional<Doc::Named> sys = Doc::namedIn(lsl, ALTextPos(2, 12), false, none);
        {
            INFO("angled");
            REQUIRE(sys.has_value());
            CHECK(sys->name == "sys.lsl");
        }
        {
            INFO("not a string elsewhere");
            CHECK_FALSE(Doc::namedIn(lsl, ALTextPos(3, 12), false, none));
            CHECK_FALSE(Doc::namedIn(lsl, ALTextPos(0, 3), false, none));
        }

        const std::string                           source = "local m = require(\"mod\")\nlocal n = 1\n";
        const ALTextDocument                        lua(source);
        const std::vector<ALPreprocessor::Required> calls  = ALPreprocessor::requiresIn(source);
        const std::optional<Doc::Named>             mod    = Doc::namedIn(lua, ALTextPos(0, 12), true, calls);
        {
            INFO("in a require call");
            REQUIRE(mod.has_value());
            CHECK(mod->name == "mod");
            CHECK(mod->require);
        }
        UNSCOPED_INFO("over the call");
        CHECK(mod->range == ALTextRange(ALTextPos(0, 10), ALTextPos(0, 24)));
        UNSCOPED_INFO("not before it");
        CHECK_FALSE(Doc::namedIn(lua, ALTextPos(0, 3), true, calls));
        UNSCOPED_INFO("not in LSL");
        CHECK_FALSE(Doc::namedIn(lua, ALTextPos(0, 12), false, calls));

        Doc doc;
        UNSCOPED_INFO("no run, nothing found");
        CHECK(doc.foundAs("lib/util.lsl").empty());
        doc.expanded.valid    = true;
        doc.expanded.resolved = { { "", "lib/util.lsl", false, "disk:/s/lib/util.lsl" },
                                  { "disk:/s/lib/util.lsl", "inner.lsl", false, "disk:/s/inner.lsl" },
                                  { "", "mod", true, "inv:mod" } };
        UNSCOPED_INFO("the script's own");
        CHECK(doc.foundAs("lib/util.lsl") == std::string("disk:/s/lib/util.lsl"));
        UNSCOPED_INFO("not an include's own");
        CHECK(doc.foundAs("inner.lsl").empty());
        {
            INFO("a module as a module");
            CHECK(doc.foundAs("mod", true) == "inv:mod");
            CHECK(doc.foundAs("mod", false).empty());
        }
        doc.expanded.valid  = false;
        doc.uploaded.valid  = true;
        doc.uploaded.resolved = { { "", "lib/util.lsl", false, "disk:/s/other.lsl" } };
        UNSCOPED_INFO("or the last save's run");
        CHECK(doc.foundAs("lib/util.lsl") == std::string("disk:/s/other.lsl"));
    }

    TEST_CASE_METHOD(alscriptstudiodoc_data, "ALScriptStudioDoc: run-time errors are held until the map is known then placed and counted", "[ALScriptStudioDoc]")
    {
        Doc doc;
        // Six lines run: the first two the include's, the rest the
        // script's from its first line on.
        ALSourceMap map;
        map.addFile("script", "script");
        map.addFile("lib.lsl", "disk:/s/lib.lsl");
        for (S32 line = 0; line < 6; ++line)
        {
            ALSourceMap::Segment segment;
            segment.outLine = line;
            segment.length  = 20;
            segment.file    = line < 2 ? 1 : 0;
            segment.line    = line < 2 ? line : line - 2;
            map.add(segment);
        }
        map.finish();

        Doc::RuntimeProblem said;
        said.line    = 4;
        said.message = "Math Error";
        UNSCOPED_INFO("no map: as the region counts it");
        CHECK(doc.placeOfRunning(4, -1).line == 4);
        doc.heardRuntime(said, true);
        {
            INFO("held");
            CHECK(doc.runtime.empty());
            CHECK(doc.runtimeHeld.size() == 1);
        }
        doc.heardRuntime(said, true);

        doc.uploaded.valid = true;
        doc.uploaded.map   = map;
        const Doc::RunningPlace place = doc.placeOfRunning(1, 3);
        {
            INFO("an include's line, by its path and name");
            CHECK(place.line == 1);
            CHECK(place.column == 3);
            CHECK(place.file == "disk:/s/lib.lsl");
            CHECK(place.fileName == "lib.lsl");
        }
        doc.placeHeldRuntime();
        {
            INFO("placed, and let go of");
            CHECK(doc.runtimeHeld.empty());
            REQUIRE(doc.runtime.size() == 1);
        }
        {
            INFO("at the source's line, said twice");
            CHECK(doc.runtime[0].line == 2);
            CHECK(doc.runtime[0].file.empty());
            CHECK(doc.runtime[0].count == 2);
        }
        doc.heardRuntime(said, false);
        UNSCOPED_INFO("said again: counted");
        CHECK(doc.runtime[0].count == 3);
        said.line = 0;
        doc.heardRuntime(said, false);
        {
            INFO("in the include");
            REQUIRE(doc.runtime.size() == 2);
            CHECK(doc.runtime[1].file == "disk:/s/lib.lsl");
            CHECK(doc.runtime[1].line == 0);
        }
        const Doc::RunningPlace made = doc.placeOfRunning(9, -1);
        {
            INFO("past the map: code the preprocessor made, at its own line");
            CHECK(made.generated);
            CHECK(made.line == 9);
            CHECK(made.file.empty());
        }
        said.line = 9;
        doc.heardRuntime(said, false);
        {
            INFO("said to be there");
            REQUIRE(doc.runtime.size() == 3);
            CHECK(doc.runtime[2].file == Doc::GENERATED);
            CHECK(doc.runtime[2].line == 9);
        }
    }

    TEST_CASE_METHOD(alscriptstudiodoc_data, "ALScriptStudioDoc: setShown counts errors and warnings but not notes", "[ALScriptStudioDoc]")
    {
        Doc                    doc;
        std::vector<Doc::Shown> rows(4);
        rows[0].level = Doc::Level::Error;
        rows[1].level = Doc::Level::Warning;
        rows[2].level = Doc::Level::Warning;
        rows[3].level = Doc::Level::Note;
        doc.setShown(rows);
        CHECK(doc.shown().size() == 4);
        CHECK(doc.shownErrors == 1);
        CHECK(doc.shownWarnings == 2);
        doc.setShown({});
        CHECK(doc.shown().empty());
        CHECK(doc.shownErrors == 0);
        CHECK(doc.shownWarnings == 0);
    }

    TEST_CASE_METHOD(alscriptstudiodoc_data, "ALScriptStudioDoc: a notecard's items carry into another tab but a script tab has none", "[ALScriptStudioDoc]")
    {
        al_studio_test::StudioWindow window;
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name = "editor";
        p.rect = LLRect(0, 200, 400, 0);
        Doc card;
        card.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
        window.floater->addChild(card.editor);
        NoWorld world;
        card.items = std::make_shared<ALNotecardEmbedded>(*card.editor, ALNotecardEmbedded::Holder(), world);
        LLPointer<LLInventoryItem> a = new LLInventoryItem(), b = new LLInventoryItem();
        card.items->loaded({ a, b });
        Doc there;
        card.carryItemsTo(there);
        {
            INFO("carried");
            REQUIRE(there.carriedEmbedded);
            REQUIRE(there.carriedEmbedded->size() == 2);
            CHECK((*there.carriedEmbedded)[1] == b);
        }
        Doc script, after;
        script.carryItemsTo(after);
        UNSCOPED_INFO("a script's tab carries nothing");
        CHECK_FALSE(after.carriedEmbedded);
    }

    TEST_CASE_METHOD(alscriptstudiodoc_data, "ALScriptStudioDoc: undo follows the view in front but the tab's own for a comparison of it", "[ALScriptStudioDoc]")
    {
        al_studio_test::StudioWindow window;
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name = "editor";
        p.rect = LLRect(0, 200, 400, 0);
        Doc doc;
        doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
        window.floater->addChild(doc.editor);
        ALDiffView::Params dp(LLUICtrlFactory::getDefaultParams<ALDiffView>());
        dp.name         = "compare";
        dp.rect         = LLRect(0, 200, 400, 0);
        doc.compareView = LLUICtrlFactory::create<ALDiffView>(dp);
        window.floater->addChild(doc.compareView);
        UNSCOPED_INFO("the source's");
        CHECK(doc.undoText() == doc.editor);
        doc.view = Doc::View::Compare;
        UNSCOPED_INFO("a comparison of texts of no tab's: its side's, which has none");
        CHECK(doc.undoText() == doc.compareView->shown());
        doc.compareTitles = Doc::CompareTitles{ "Saved", "Now" };
        UNSCOPED_INFO("one that follows the tab: the tab's");
        CHECK(doc.undoText() == doc.editor);
    }

    TEST_CASE_METHOD(alscriptstudiodoc_data, "ALScriptStudioDoc: a save's envelope carries an upload header only where one is asked for", "[ALScriptStudioDoc]")
    {
        al_studio_test::StudioWindow window;
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name = "editor";
        p.rect = LLRect(0, 200, 400, 0);
        Doc doc;
        doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
        window.floater->addChild(doc.editor);
        doc.language.compileTarget = "mono";
        doc.editor->setText("#define N 1\ndefault { state_entry() { llOwnerSay((string)N); } }\n");
        const std::string expanded = "default { state_entry() { llOwnerSay((string)1); } }\n";

        const ALScriptEnvelope plain = doc.envelopeFor(expanded, "Test 1.0");
        UNSCOPED_INFO("none unasked");
        CHECK(plain.header.empty());

        ALScriptHeaderOptions asked;
        asked.on      = true;
        asked.creator = "Some One";
        const ALScriptEnvelope              with = doc.envelopeFor(expanded, "Test 1.0", asked);
        const std::optional<ALUploadHeader> said = ALUploadHeader::parse(with.header, false);
        {
            INFO("ours, read back: " + with.header);
            REQUIRE(said.has_value());
            CHECK(said->ours);
        }
        CHECK(said->hash == ALUploadHeader::hashOf("mono", doc.editor->text(), expanded));
        CHECK(said->creator == std::string("Some One"));
        UNSCOPED_INFO("no file: a tab has no link");
        CHECK(said->file.empty());
        const int lines = static_cast<int>(std::count(with.header.begin(), with.header.end(), '\n'));
        UNSCOPED_INFO("the code its lines down");
        CHECK(with.codeLine() == plain.codeLine() + lines);
        const std::optional<ALScriptEnvelope> back = ALScriptEnvelope::parse(with.wrap());
        {
            INFO("parsed back with it");
            REQUIRE(back.has_value());
            CHECK(back->header == with.header);
            CHECK(back->expanded == expanded);
        }
    }
}
