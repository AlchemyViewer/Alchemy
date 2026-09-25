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

#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

#include <string>

namespace tut
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
    typedef test_group<alscriptstudiodoc_data> alscriptstudiodoc_group;
    typedef alscriptstudiodoc_group::object    alscriptstudiodoc_object;
    alscriptstudiodoc_group                    alscriptstudiodoc_instance("ALScriptStudioDoc");

    template<> template<>
    void alscriptstudiodoc_object::test<1>()
    {
        set_test_name("unsaved is the text, or a compile target or an experience picked; a save is under way at any stage of it, or while the preprocessor runs");
        Doc doc;
        ensure("nothing changed", !doc.unsaved());
        doc.targetChosen = true;
        ensure("a compile target picked", doc.unsaved());
        doc.targetChosen     = false;
        doc.experienceChosen = true;
        ensure("an experience picked", doc.unsaved());

        ensure("no save", !doc.saveUnderway());
        doc.preprocessing = true;
        ensure("the preprocessor running, which a save may wait on", doc.saveUnderway());
        doc.preprocessing = false;
        ALScriptSaveFlow::Tab tab;
        tab.holdOnErrors = true;
        doc.save.route(tab);
        ensure("waiting on a check", doc.saveUnderway());
        doc.save.checked();
        ensure("and not once it has come", !doc.saveUnderway());
        doc.save.sent(ALTextUndo::SavePoint(), std::nullopt, {});
        ensure("sent", doc.saveUnderway());
    }

    template<> template<>
    void alscriptstudiodoc_object::test<2>()
    {
        set_test_name("the expansion is in front only where there is one");
        Doc doc;
        doc.view = Doc::View::Expanded;
        ensure("nothing expanded yet: the source", doc.shownView() == Doc::View::Source);
        ensure("and its text", doc.shownText() == doc.editor);
    }

    template<> template<>
    void alscriptstudiodoc_object::test<3>()
    {
        set_test_name("levels: the compiler's words, the checkers' severities, the marks, and how the findings store reads a problem");
        ensure("WARNING", Doc::levelOf("WARNING") == Doc::Level::Warning);
        ensure("WARN", Doc::levelOf("WARN") == Doc::Level::Warning);
        ensure("anything else is an error, as the server has two", Doc::levelOf("ERROR") == Doc::Level::Error && Doc::levelOf("oops") == Doc::Level::Error);
        ensure("a note", Doc::levelOf(ALScriptProblem::Severity::Note) == Doc::Level::Note);
        ensure("named", std::string(Doc::levelName(Doc::Level::Warning)) == "WARNING");
        ensure("marked", Doc::markOf(Doc::Level::Error) == ALCodeEditor::Mark::Error && Doc::markOf(Doc::Level::Note) == ALCodeEditor::Mark::Note);

        Doc::Shown shown;
        shown.level    = Doc::Level::Warning;
        shown.origin   = "Luau";
        shown.message  = "unused local";
        shown.fileName = "util.luau";
        shown.fixes    = { fix(ALScriptFix::Kind::Suppress) };
        ensure("a suppression is no fix", !Doc::ProblemTraits::fixable(shown));
        shown.fixes.push_back(fix(ALScriptFix::Kind::Fix));
        ensure("a change is", Doc::ProblemTraits::fixable(shown));
        ensure("found by its level's name", Doc::ProblemTraits::mentions(shown, "warn"));
        ensure("by its include", Doc::ProblemTraits::mentions(shown, "util"));
        ensure("not by what it does not say", !Doc::ProblemTraits::mentions(shown, "state"));
        ensure("its rule is where it came from", Doc::ProblemTraits::rule(shown) == "Luau");
        ensure("its level", Doc::ProblemTraits::level(shown) == ALFindingLevel::Warning);
    }

    template<> template<>
    void alscriptstudiodoc_object::test<4>()
    {
        set_test_name("the fake of the window's services: the studio's own words, counted, and its tabs found");
        al_studio_test::StudioWindow window;
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        al_studio_test::FakeServices services(window.floater);
        ensure_equals("a word of the skin's", services.words("ObjectNameComing"), std::string("\xE2\x80\xA6"));
        ensure_equals("one", services.counted("Fixes", 1), std::string("1 fix"));
        ensure_equals("many", services.counted("Fixes", 3), std::string("3 fixes"));

        LLUUID object, item;
        object.generate();
        item.generate();
        const ALScriptRef ref(object, item);
        Doc& script = services.addDoc("script", ref, "Door");
        Doc& file   = services.addDoc("disk:/lib.lsl");
        file.file   = "/lib.lsl";
        ensure("the first in front", services.frontDoc() == &script);
        ensure("by id", services.findDoc("disk:/lib.lsl") == &file);
        ensure("by what it holds", services.findDoc(ref) == &script);
        ensure("none", services.findDoc("nothing") == nullptr);

        services.report("Saved", false, &script, { "retry" });
        ensure("said of the tab", services.reports.size() == 1 && services.reports.front().doc == "script" && services.reports.front().actions.size() == 1);
        services.openScript(ref, "Door", std::nullopt, 12);
        ensure("asked to open at a line", services.opened.size() == 1 && services.opened.front().line == 12);
    }
}
