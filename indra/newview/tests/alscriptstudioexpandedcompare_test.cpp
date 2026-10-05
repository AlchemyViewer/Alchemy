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

#include "../test/lltut.h"

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

namespace tut
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

    typedef test_group<alscriptstudioexpandedcompare_data> alscriptstudioexpandedcompare_group;
    typedef alscriptstudioexpandedcompare_group::object    alscriptstudioexpandedcompare_object;
    tut::alscriptstudioexpandedcompare_group               alscriptstudioexpandedcompare_test("alscriptstudioexpandedcompare");

    template<> template<>
    void alscriptstudioexpandedcompare_object::test<1>()
    {
        set_test_name("each output line beside the script's own lines it came from; lines standing for the same joined; an include's and nothing's in none");
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
        ensure_equals("the ranges", said(ALScriptStudioExpandedCompare::rangesOf(map)), std::string("[0-0 1-1][1-2 2-2][3-3 3-4][5-5 6-6]"));
        ensure("none for nothing", ALScriptStudioExpandedCompare::rangesOf(ALSourceMap()).empty());
    }

    template<> template<>
    void alscriptstudioexpandedcompare_object::test<2>()
    {
        set_test_name("the source beside what a save sends, lined up by the map, the include's line alone; typing goes on in the source where the map says");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door");
        ensure("it can be compared", ALScriptStudioExpandedCompare::canCompare(door));
        unit->compare(door);
        ensure("shown at once, no run asked", studio.compares == 1 && studio.runs == 0);
        ensure_equals("under the tab's name and the expansion's", studio.titles, "door | " + services().words("ComparePreprocessed"));
        ensure_equals("lined up by line", said(studio.given), std::string("[0-0 1-1][1-1 2-2]"));
        ALDiffView& view = *door.compareView;
        ensure("the source's first line beside the expansion's second",
               view.left()->layout().lineTop(0) == view.right()->layout().lineTop(1));
        ensure_equals("the include's line put in, nothing else changed", view.changeCount(), 1);

        view.right()->setFocus(true);
        view.right()->goTo(ALTextPos(2, 8));
        view.right()->handleUnicodeChar('x', false);
        ensure("typed where the line came from", studio.typedAt.size() == 1 && studio.typedAt[0] == ALTextPos(1, 8));
        ensure_equals("in the source", door.editor->wholeText(), std::string("integer a;\ninteger xb;"));

        unit->compare(door);
        view.right()->setFocus(true);
        view.right()->goTo(ALTextPos(0, 3));
        view.right()->handleUnicodeChar('y', false);
        ensure("an include's line: nowhere", studio.typedAt.size() == 1);
    }

    template<> template<>
    void alscriptstudioexpandedcompare_object::test<3>()
    {
        set_test_name("an expansion older than the text made again first, and the comparison shown once one of the text as it is comes");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door");
        door.editor->goTo(ALTextPos(0, 0));
        door.editor->insertText("// ");
        unit->compare(door);
        ensure("a run asked, nothing shown", studio.runs == 1 && studio.compares == 0);
        unit->expanded(door);
        ensure("a run of the older text: still waiting", studio.compares == 0);
        door.uploaded.version = door.editor->document().version();
        unit->expanded(door);
        ensure("shown", studio.compares == 1);
        unit->expanded(door);
        ensure("once", studio.compares == 1);

        Doc& lamp = tab("lamp");
        lamp.expandedEditor = nullptr;
        ensure("not one a save preprocesses", !ALScriptStudioExpandedCompare::canCompare(lamp));
        lamp.expandedEditor = editor("expanded_again", "");
        lamp.loaded         = false;
        ensure("nor before it has loaded", !ALScriptStudioExpandedCompare::canCompare(lamp));
    }

    template<> template<>
    void alscriptstudioexpandedcompare_object::test<4>()
    {
        set_test_name("over a real run: an include's lines alone on the right, every line of the script's own beside what it became, a macro's call beside what it made");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("expanded", result.text.find("llOwnerSay(\"hi\")") != std::string::npos);

        Doc& door = tab("door");
        door.editor->setText(source);
        door.uploaded.version = door.editor->document().version();
        door.uploaded.text    = std::make_shared<const std::string>(result.text);
        door.uploaded.map     = result.map;
        unit->compare(door);
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
        ensure("the include's lines in the output", where("gLib") >= 0);
        ensure("default beside default", beside(2, where("default")));
        ensure("state_entry beside state_entry", beside(4, where("state_entry")));
        ensure("the macro's call beside what it made", beside(6, where("llOwnerSay")));
        ensure("the last brace beside the last brace", beside(8, static_cast<S32>(out.size()) - (out.back().empty() ? 2 : 1)));
        // What the map alone says: the call's line stands for what it made.
        const S32 call = view.model().rangeAt(ALDiffModel::Column::Left, 6);
        ensure("the call's line in a range", call >= 0);
        ensure_equals("whose output is what it made", view.model().ranges()[static_cast<size_t>(call)].rightFirst, where("llOwnerSay"));
        ensure("the directives in none", view.model().rangeAt(ALDiffModel::Column::Left, 0) < 0 && view.model().rangeAt(ALDiffModel::Column::Left, 1) < 0);
    }
}
