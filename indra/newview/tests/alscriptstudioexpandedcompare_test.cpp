/**
 * @file alscriptstudioexpandedcompare_test.cpp
 * @brief Tests for ALScriptStudioExpandedCompare: a source beside what a save sends of it, lined up by the map.
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

#include "../alscriptstudioexpandedcompare.h"

#include "aldiffview.h"
#include "alpreprocessor.h"
#include "alscriptstudio_fixture.h"
#include "alsourcemap.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
{
    typedef ALScriptStudioDoc Doc;

    // The comparison's window, faked: a comparison of the view's own made
    // in the tab's place, the runs of the preprocessor asked for counted,
    // and the places typing was sent to in the source.
    struct FakeExpandedWindow : public ALScriptStudioExpandedCompare::Window
    {
        explicit FakeExpandedWindow(LLView* host) : host(host) {}

        void compareRanged(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                           const std::string& right_title, const ALTextDiff::ranges_t& ranges) override
        {
            titles = left_title + " | " + right_title;
            given  = ranges;
            if (!doc.compareView)
            {
                ALDiffView::Params p(LLUICtrlFactory::getDefaultParams<ALDiffView>());
                p.name          = "compare_" + doc.id;
                p.rect          = LLRect(0, 300, 600, 0);
                doc.compareView = LLUICtrlFactory::create<ALDiffView>(p);
                host->addChild(doc.compareView);
            }
            doc.compareView->setOnEdit(nullptr);
            doc.compareView->setTexts(left, right, ranges);
            ++compares;
        }
        void    preprocess(Doc&) override { ++runs; }
        LLView* typeInSource(Doc& doc, const ALTextPos& at) override
        {
            typedAt.push_back(at);
            doc.editor->goTo(at);
            return doc.editor;
        }

        LLView*                host = nullptr;
        std::string            titles;
        ALTextDiff::ranges_t   given;
        S32                    compares = 0;
        S32                    runs     = 0;
        std::vector<ALTextPos> typedAt;
    };

    ALSourceMap::Segment segment(S32 out_line, S32 out_column, S32 length, S32 file, S32 line, S32 column, bool verbatim = true)
    {
        ALSourceMap::Segment one;
        one.outLine   = out_line;
        one.outColumn = out_column;
        one.length    = length;
        one.file      = file;
        one.line      = line;
        one.column    = column;
        one.verbatim  = verbatim;
        return one;
    }
}

namespace
{
    struct alscriptstudioexpandedcompare_data
    {
        al_studio_test::StudioWindow                   window;
        FakeExpandedWindow                             studio{ window.floater };
        std::unique_ptr<ALScriptStudioExpandedCompare> unit = std::make_unique<ALScriptStudioExpandedCompare>(window.services(), studio);

        al_studio_test::FakeServices& services() { return window.services(); }

        ALCodeEditor* editor(const std::string& name, const std::string& text)
        {
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name             = name;
            p.rect             = LLRect(0, 200, 400, 0);
            p.syntax           = "lsl";
            ALCodeEditor* made = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(made);
            made->setText(text);
            return made;
        }
        // A tab a save preprocesses: an include's line put before its two
        // lines, as the last run made of the text as it is.
        Doc& tab(const std::string& id)
        {
            Doc& doc           = services().addDoc(id, ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), id);
            doc.loaded         = true;
            doc.modifiable     = true;
            doc.editor         = editor("editor_" + id, "integer a;\ninteger b;");
            doc.expandedEditor = editor("expanded_" + id, "");
            ALSourceMap map;
            map.addFile(id, std::string());
            map.addFile("include", "include.lsl");
            map.add(segment(0, 0, 15, 1, 0, 0));
            map.add(segment(1, 0, 10, 0, 0, 0));
            map.add(segment(2, 0, 10, 0, 1, 0));
            map.finish();
            doc.uploaded.valid   = true;
            doc.uploaded.version = doc.editor->document().version();
            doc.uploaded.text    = std::make_shared<const std::string>("// from include\ninteger a;\ninteger b;");
            doc.uploaded.map     = map;
            return doc;
        }
        static std::string said(const ALTextDiff::ranges_t& ranges)
        {
            std::string out;
            for (const ALTextDiff::Range& range : ranges)
            {
                out += "[" + std::to_string(range.leftFirst) + "-" + std::to_string(range.leftLast) + " " + std::to_string(range.rightFirst) + "-" +
                       std::to_string(range.rightLast) + "]";
            }
            return out;
        }
    };

    TEST_CASE_METHOD(alscriptstudioexpandedcompare_data, "alscriptstudioexpandedcompare: rangesOf pairs each output line with the source lines it came from", "[alscriptstudioexpandedcompare]")
    {
        ALSourceMap map;
        map.addFile("script", std::string());
        map.addFile("include", "include.lsl");
        map.add(segment(0, 0, 9, 1, 0, 0));
        map.add(segment(1, 0, 6, 0, 0, 0));
        // Two source lines written on one: what the optimizer joins.
        map.add(segment(2, 0, 6, 0, 1, 0));
        map.add(segment(2, 7, 6, 0, 2, 4));
        // A macro's two lines, both of the line that invoked it.
        map.add(segment(3, 0, 8, 0, 3, 2, false));
        map.add(segment(4, 0, 8, 0, 3, 2, false));
        map.add(segment(6, 0, 4, 0, 5, 0));
        map.finish();
        CHECK(said(ALScriptStudioExpandedCompare::rangesOf(map)) == std::string("[0-0 1-1][1-2 2-2][3-3 3-4][5-5 6-6]"));
        CHECK(ALScriptStudioExpandedCompare::rangesOf(ALSourceMap()).empty());
    }

    TEST_CASE_METHOD(alscriptstudioexpandedcompare_data, "alscriptstudioexpandedcompare: the source is lined up beside what a save sends and typing maps back", "[alscriptstudioexpandedcompare]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door");
        CHECK(ALScriptStudioExpandedCompare::canCompare(door));
        unit->compare(door);
        {
            INFO("shown at once, no run asked");
            CHECK(studio.compares == 1);
            CHECK(studio.runs == 0);
        }
        UNSCOPED_INFO("under the tab's name and the expansion's");
        CHECK(studio.titles == "door | " + services().words("ComparePreprocessed"));
        UNSCOPED_INFO("lined up by line");
        CHECK(said(studio.given) == std::string("[0-0 1-1][1-1 2-2]"));
        REQUIRE(door.compareView);
        ALDiffView& view = *door.compareView;
        UNSCOPED_INFO("the source's first line beside the expansion's second");
        CHECK(view.left()->layout().lineTop(0) == view.right()->layout().lineTop(1));
        UNSCOPED_INFO("the include's line put in, nothing else changed");
        CHECK(view.changeCount() == 1);

        view.right()->setFocus(true);
        view.right()->goTo(ALTextPos(2, 8));
        view.right()->handleUnicodeChar('x', false);
        {
            INFO("typed where the line came from");
            REQUIRE(studio.typedAt.size() == 1);
            CHECK(studio.typedAt[0] == ALTextPos(1, 8));
        }
        UNSCOPED_INFO("in the source");
        CHECK(door.editor->wholeText() == std::string("integer a;\ninteger xb;"));

        unit->compare(door);
        view.right()->setFocus(true);
        view.right()->goTo(ALTextPos(0, 3));
        view.right()->handleUnicodeChar('y', false);
        UNSCOPED_INFO("an include's line: nowhere");
        CHECK(studio.typedAt.size() == 1);
    }

    TEST_CASE_METHOD(alscriptstudioexpandedcompare_data, "alscriptstudioexpandedcompare: a stale expansion is run again first and shown once current", "[alscriptstudioexpandedcompare]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door");
        door.editor->goTo(ALTextPos(0, 0));
        door.editor->insertText("// ");
        unit->compare(door);
        {
            INFO("a run asked, nothing shown");
            CHECK(studio.runs == 1);
            CHECK(studio.compares == 0);
        }
        unit->expanded(door);
        UNSCOPED_INFO("a run of the older text: still waiting");
        CHECK(studio.compares == 0);
        door.uploaded.version = door.editor->document().version();
        unit->expanded(door);
        CHECK(studio.compares == 1);
        unit->expanded(door);
        UNSCOPED_INFO("once");
        CHECK(studio.compares == 1);

        // A tab closed while it waited is let go of as the next is asked.
        Doc& gate = tab("gate");
        gate.editor->insertText("// ");
        unit->compare(gate);
        CHECK(unit->waiting("gate"));
        services().docs.erase(std::remove_if(services().docs.begin(), services().docs.end(),
                                             [](const std::unique_ptr<ALScriptStudioDoc>& doc) { return doc->id == "gate"; }),
                              services().docs.end());
        unit->expanded(door);
        UNSCOPED_INFO("closed: let go of");
        CHECK_FALSE(unit->waiting("gate"));

        Doc& lamp = tab("lamp");
        lamp.expandedEditor = nullptr;
        UNSCOPED_INFO("not one a save preprocesses");
        CHECK_FALSE(ALScriptStudioExpandedCompare::canCompare(lamp));
        lamp.expandedEditor = editor("expanded_again", "");
        lamp.loaded         = false;
        UNSCOPED_INFO("nor before it has loaded");
        CHECK_FALSE(ALScriptStudioExpandedCompare::canCompare(lamp));
    }

    TEST_CASE_METHOD(alscriptstudioexpandedcompare_data, "alscriptstudioexpandedcompare: a real preprocessor run lines each source line up with its output", "[alscriptstudioexpandedcompare]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const std::string source = "#include \"lib.lsl\"\n"
                                   "#define GREET(x) llOwnerSay(x)\n"
                                   "default\n"
                                   "{\n"
                                   "    state_entry()\n"
                                   "    {\n"
                                   "        GREET(\"hi\");\n"
                                   "    }\n"
                                   "}\n";
        ALPreprocessor::Options options;
        options.fileName = "main.lsl";
        options.resolve  = [](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
            if (ask.name != "lib.lsl")
            {
                return ALPreprocessor::Found::No;
            }
            out.name = ask.name;
            out.text = "integer gLib = 1;\nlib()\n{\n}\n";
            return ALPreprocessor::Found::Yes;
        };
        const ALPreprocessor::Result result = ALPreprocessor::run(source, options);
        UNSCOPED_INFO("expanded");
        REQUIRE(result.text.find("llOwnerSay(\"hi\")") != std::string::npos);

        Doc& door = tab("door");
        door.editor->setText(source);
        door.uploaded.version = door.editor->document().version();
        door.uploaded.text    = std::make_shared<const std::string>(result.text);
        door.uploaded.map     = result.map;
        unit->compare(door);
        REQUIRE(door.compareView);
        ALDiffView&                     view  = *door.compareView;
        const std::vector<std::string>  out   = ALTextDiff::split(result.text);
        const auto                      where = [&out](std::string_view what) {
            for (size_t n = 0; n < out.size(); ++n)
            {
                if (out[n].find(what) != std::string::npos)
                {
                    return static_cast<S32>(n);
                }
            }
            return -1;
        };
        const auto beside = [&view](S32 left, S32 right) { return right >= 0 && view.left()->layout().lineTop(left) == view.right()->layout().lineTop(right); };
        UNSCOPED_INFO("the include's lines in the output");
        CHECK(where("gLib") >= 0);
        UNSCOPED_INFO("default beside default");
        CHECK(beside(2, where("default")));
        UNSCOPED_INFO("state_entry beside state_entry");
        CHECK(beside(4, where("state_entry")));
        UNSCOPED_INFO("the macro's call beside what it made");
        CHECK(beside(6, where("llOwnerSay")));
        UNSCOPED_INFO("the last brace beside the last brace");
        CHECK(beside(8, static_cast<S32>(out.size()) - (out.back().empty() ? 2 : 1)));
        // What the map alone says: the call's line stands for what it made.
        const S32 call = view.model().rangeAt(ALDiffModel::Column::Left, 6);
        UNSCOPED_INFO("the call's line in a range");
        REQUIRE(call >= 0);
        UNSCOPED_INFO("whose output is what it made");
        CHECK(view.model().ranges()[static_cast<size_t>(call)].rightFirst == where("llOwnerSay"));
        {
            INFO("the directives in none");
            CHECK(view.model().rangeAt(ALDiffModel::Column::Left, 0) < 0);
            CHECK(view.model().rangeAt(ALDiffModel::Column::Left, 1) < 0);
        }
    }

    TEST_CASE_METHOD(alscriptstudioexpandedcompare_data, "alscriptstudioexpandedcompare: a stale run's answer asks again and a wait is dropped when the tab moves on", "[alscriptstudioexpandedcompare]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door");
        door.editor->goTo(ALTextPos(0, 0));
        door.editor->insertText("// ");
        unit->compare(door);
        CHECK(studio.runs == 1);
        REQUIRE_FALSE(services().statuses.empty());
        UNSCOPED_INFO("said while it waits");
        CHECK(services().statuses.back() == services().words("Preprocessing", { { "[NAME]", "door" } }));
        // The window was busy: the run that answers is the one before.
        unit->expanded(door);
        {
            INFO("asked again rather than left waiting");
            CHECK(studio.runs == 2);
            CHECK(studio.compares == 0);
            CHECK(unit->waiting("door"));
        }
        door.uploaded.version = door.editor->document().version();
        unit->expanded(door);
        {
            INFO("shown once the run of the text now answers");
            CHECK(studio.compares == 1);
            CHECK_FALSE(unit->waiting("door"));
        }

        // Another comparison started while it waits: let go of, not put
        // over it.
        door.view = Doc::View::Source;
        door.editor->insertText("// ");
        unit->compare(door);
        CHECK(unit->waiting("door"));
        studio.compareRanged(door, "other", door.editor->wholeText(), "other", "door", {});
        door.view = Doc::View::Compare;
        door.uploaded.version = door.editor->document().version();
        const S32 runs = studio.runs;
        unit->expanded(door);
        UNSCOPED_INFO("let go of");
        CHECK_FALSE(unit->waiting("door"));
        {
            INFO("the other comparison left as it is");
            CHECK(studio.compares == 2);
            CHECK(studio.runs == runs);
            CHECK(studio.titles == "other | door");
        }

        // Asked from a comparison, which is then closed: let go of.
        door.editor->insertText("// ");
        unit->compare(door);
        CHECK(unit->waiting("door"));
        door.view = Doc::View::Source;
        door.uploaded.version = door.editor->document().version();
        unit->expanded(door);
        {
            INFO("closed: let go of");
            CHECK_FALSE(unit->waiting("door"));
            CHECK(studio.compares == 2);
        }

        // Still in the comparison it was asked from, which follows the
        // tab: shown over it.
        door.view = Doc::View::Compare;
        door.editor->insertText("// ");
        unit->compare(door);
        door.compareView->setRightText(door.editor->wholeText());
        door.uploaded.version = door.editor->document().version();
        unit->expanded(door);
        CHECK(studio.compares == 3);
        CHECK_FALSE(unit->waiting("door"));
    }
}
