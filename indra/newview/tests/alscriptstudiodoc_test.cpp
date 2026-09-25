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

    template<> template<>
    void alscriptstudiodoc_object::test<5>()
    {
        set_test_name("an include or a module a place names: anywhere on an #include line, or in a require call; and what a run found it as");
        const ALTextDocument lsl("default {}\n  #  include \"lib/util.lsl\" // the helpers\n#include <sys.lsl>\n#define X \"y.lsl\"\n");
        const std::vector<ALPreprocessor::Required> none;
        const std::optional<Doc::Named> util = Doc::namedIn(lsl, ALTextPos(1, 0), false, none);
        ensure("on its line, before the #", util.has_value() && util->name == "lib/util.lsl" && !util->require);
        ensure("the whole line", util->range == ALTextRange(ALTextPos(1, 0), ALTextPos(1, lsl.lineLength(1))));
        ensure("angled", Doc::namedIn(lsl, ALTextPos(2, 12), false, none)->name == "sys.lsl");
        ensure("not a string elsewhere",
               !Doc::namedIn(lsl, ALTextPos(3, 12), false, none) && !Doc::namedIn(lsl, ALTextPos(0, 3), false, none));

        const std::string                           source = "local m = require(\"mod\")\nlocal n = 1\n";
        const ALTextDocument                        lua(source);
        const std::vector<ALPreprocessor::Required> calls  = ALPreprocessor::requiresIn(source);
        const std::optional<Doc::Named>             mod    = Doc::namedIn(lua, ALTextPos(0, 12), true, calls);
        ensure("in a require call", mod.has_value() && mod->name == "mod" && mod->require);
        ensure("over the call", mod->range == ALTextRange(ALTextPos(0, 10), ALTextPos(0, 24)));
        ensure("not before it", !Doc::namedIn(lua, ALTextPos(0, 3), true, calls));
        ensure("not in LSL", !Doc::namedIn(lua, ALTextPos(0, 12), false, calls));

        Doc doc;
        ensure("no run, nothing found", doc.foundAs("lib/util.lsl").empty());
        doc.expanded.valid    = true;
        doc.expanded.resolved = { { "", "lib/util.lsl", false, "disk:/s/lib/util.lsl" },
                                  { "disk:/s/lib/util.lsl", "inner.lsl", false, "disk:/s/inner.lsl" },
                                  { "", "mod", true, "inv:mod" } };
        ensure_equals("the script's own", doc.foundAs("lib/util.lsl"), std::string("disk:/s/lib/util.lsl"));
        ensure("not an include's own", doc.foundAs("inner.lsl").empty());
        ensure("a module as a module", doc.foundAs("mod", true) == "inv:mod" && doc.foundAs("mod", false).empty());
        doc.expanded.valid  = false;
        doc.uploaded.valid  = true;
        doc.uploaded.resolved = { { "", "lib/util.lsl", false, "disk:/s/other.lsl" } };
        ensure_equals("or the last save's run", doc.foundAs("lib/util.lsl"), std::string("disk:/s/other.lsl"));
    }
}
