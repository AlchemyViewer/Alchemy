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
        set_test_name("unsaved is the text, or a compile target or an experience picked; a save is under way while any of its stages is");
        Doc doc;
        ensure("nothing changed", !doc.unsaved());
        doc.targetChosen = true;
        ensure("a compile target picked", doc.unsaved());
        doc.targetChosen     = false;
        doc.experienceChosen = true;
        ensure("an experience picked", doc.unsaved());

        ensure("no save", !doc.saveUnderway());
        for (bool Doc::*stage : { &Doc::saving, &Doc::preprocessing, &Doc::saveAfterCheck })
        {
            Doc under;
            under.*stage = true;
            ensure("a stage of it", under.saveUnderway());
        }
    }

    template<> template<>
    void alscriptstudiodoc_object::test<2>()
    {
        set_test_name("a save stopped by one check is let past that check alone, and for that text alone");
        Doc doc;
        doc.stoppedBy(5, Doc::CheckAnalyzers);
        ensure("stopped", !doc.letsPast(5, Doc::CheckAnalyzers));
        doc.letPast(5);
        ensure("asked again: past the analyzers", doc.letsPast(5, Doc::CheckAnalyzers));
        ensure("but not the preprocessor", !doc.letsPast(5, Doc::CheckPreprocessor));
        ensure("nor an include to come", !doc.letsPast(5, Doc::CheckPending));
        ensure("nor for another text", !doc.letsPast(6, Doc::CheckAnalyzers));

        // Stopped again over the same text by the next check: each asked
        // past on its own, and what was let past stays let past.
        doc.stoppedBy(5, Doc::CheckPending);
        ensure("the include stops it", !doc.letsPast(5, Doc::CheckPending));
        doc.letPast(5);
        ensure("both past now", doc.letsPast(5, Doc::CheckAnalyzers | Doc::CheckPending));
        ensure("the preprocessor never asked about", !doc.letsPast(5, Doc::CheckPreprocessor));

        // A changed text asks every question afresh.
        doc.stoppedBy(6, Doc::CheckPreprocessor);
        ensure("nothing past for the new text", !doc.letsPast(6, Doc::CheckAnalyzers) && !doc.letsPast(6, Doc::CheckPreprocessor));
        doc.letPast(7);
        ensure("asked again over yet another text: nothing", !doc.letsPast(6, Doc::CheckPreprocessor) && !doc.letsPast(7, Doc::CheckPreprocessor));

        doc.letAllPast(8);
        ensure("a copy, or the external editor's save: everything", doc.letsPast(8, Doc::CheckAll));
        doc.clearSaveChecks();
        ensure("gone once it went up", !doc.letsPast(8, Doc::CheckAnalyzers));
    }

    template<> template<>
    void alscriptstudiodoc_object::test<3>()
    {
        set_test_name("the expansion is in front only where there is one");
        Doc doc;
        doc.view = Doc::View::Expanded;
        ensure("nothing expanded yet: the source", doc.shownView() == Doc::View::Source);
        ensure("and its text", doc.shownText() == doc.editor);
    }

    template<> template<>
    void alscriptstudiodoc_object::test<4>()
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
    void alscriptstudiodoc_object::test<5>()
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
