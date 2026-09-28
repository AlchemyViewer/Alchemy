/**
 * @file alscriptrecompile_test.cpp
 * @brief A recompile from the Explorer, the window and the world faked.
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

// The preprocessor's header reaches the inventory model's, which does not
// include what it uses.
#include <boost/unordered_map.hpp>

#include "../alscriptrecompile.h"

#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

#include <map>
#include <set>

namespace
{
    typedef ALScriptRecompile        Recompile;
    typedef Recompile::Window        Window;
    typedef ALScriptWorkspace::CompileResult Result;

    // The window and the world, answered as a test says: the prims'
    // scripts; which are open, saving or unsaved; what is known of whether
    // each runs; each recompile asked, held for the test to answer.
    struct FakeWindow : public Window
    {
        void listScripts(const std::vector<std::pair<LLUUID, std::string>>& prims, std::function<void(Listed)> told) override
        {
            listedFor.push_back(prims);
            told(listing);
        }
        bool                isOpen(const ALScriptRef& ref) override { return open.contains(ref.item); }
        bool                saving(const ALScriptRef& ref) override { return sending.contains(ref.item); }
        bool                unsaved(const ALScriptRef& ref) override { return changed.contains(ref.item); }
        std::optional<bool> knownRunning(const ALScriptRef& ref) override
        {
            const auto found = runs.find(ref.item);
            return found == runs.end() ? std::nullopt : std::optional<bool>(found->second);
        }
        void recompile(const ALScriptRef& ref, const std::string& target, std::optional<bool> running,
                       ALScriptWorkspace::compile_callback_t told) override
        {
            asked.push_back({ ref, target, running, std::move(told) });
        }
        void scriptRecompiled(const Recompile::Script& script) override { scripts.push_back(script); }
        void recompiled(const Recompile::Done& done) override { this->done.push_back(done); }

        Listed                     listing;
        std::set<LLUUID>           open, sending, changed;
        std::map<LLUUID, bool>     runs;
        std::vector<std::vector<std::pair<LLUUID, std::string>>> listedFor;
        struct Asked
        {
            ALScriptRef                         ref;
            std::string                         target;
            std::optional<bool>                 running;
            ALScriptWorkspace::compile_callback_t told;
        };
        std::vector<Asked>            asked;
        std::vector<Recompile::Script> scripts;
        std::vector<Recompile::Done>  done;
    };

    LLUUID id(U32 n)
    {
        LLUUID out;
        out.mData[0]  = static_cast<U8>(n);
        out.mData[15] = 5;
        return out;
    }

    Result compiled()
    {
        Result result;
        result.success = true;
        return result;
    }

    Result failed(S32 line, const std::string& message)
    {
        Result result;
        ALScriptWorkspace::Diagnostic said;
        said.line      = line;
        said.column    = 3;
        said.hasColumn = true;
        said.level     = "ERROR";
        said.message   = message;
        result.diagnostics.push_back(said);
        return result;
    }

    Result notSent(const std::string& why)
    {
        Result result;
        result.error = why;
        return result;
    }
}

namespace tut
{
    struct alscriptrecompile_data
    {
        al_studio_test::FakeServices services;
        FakeWindow                   window;
        Recompile                    run{ services, window };
        const LLUUID                 prim = id(1);

        Recompile::One one(U32 n, const std::string& name, bool lua = false) { return { ALScriptRef(prim, id(n)), name, "House", lua }; }

        // A prim of six scripts, the third SLua.
        void house()
        {
            for (U32 i = 0; i < 6; ++i)
            {
                window.listing.scripts.push_back(one(10 + i, std::string(1, static_cast<char>('A' + i)), i == 2));
            }
        }

        bool said(const std::string& word, bool failure) const
        {
            return std::any_of(services.reports.begin(), services.reports.end(),
                               [&](const auto& one) { return one.text.find(word) == 0 && one.failure == failure; });
        }
    };
    typedef test_group<alscriptrecompile_data> alscriptrecompile_group;
    typedef alscriptrecompile_group::object    alscriptrecompile_object;
    alscriptrecompile_group                    alscriptrecompile_instance("alscriptrecompile");

    template<> template<>
    void alscriptrecompile_object::test<1>()
    {
        set_test_name("a prim's scripts and a script chosen go up a few at a time, each once, for the target; SLua as SLua; what is known of running given");
        house();
        window.runs = { { id(11), false } };
        run.recompile({ one(10, "A") }, { { prim, "House" } }, "mono");
        ensure("listed", window.listedFor.size() == 1 && window.listedFor[0].front().first == prim);
        ensure("running", run.running());
        ensure_equals("four at once", window.asked.size(), size_t(Recompile::AT_ONCE));
        ensure("A once, chosen and in its prim", window.asked[0].ref.item == id(10) && window.asked[1].ref.item == id(11));
        ensure_equals("for the target", window.asked[0].target, std::string("mono"));
        ensure_equals("the SLua one as it compiles", window.asked[2].target, std::string("auto"));
        ensure("B known stopped, stays so", window.asked[1].running == false);
        ensure("A not known: the save asks", !window.asked[0].running.has_value());

        for (size_t i = 0; i < 4; ++i)
        {
            window.asked[i].told(compiled());
        }
        ensure_equals("the rest begun", window.asked.size(), size_t(6));
        window.asked[4].told(compiled());
        ensure("not done yet", window.done.empty());
        window.asked[5].told(compiled());
        ensure_equals("done once", window.done.size(), size_t(1));
        ensure("all compiled", window.done[0].compiled == 6 && window.done[0].failed == 0 && window.done[0].target == "mono");
        ensure("no longer running", !run.running());
        ensure_equals("each said", window.scripts.size(), size_t(6));
    }

    template<> template<>
    void alscriptrecompile_object::test<2>()
    {
        set_test_name("a save on its way is left alone and said, changes not saved are said, and each result said in Output with a closed one's rows");
        house();
        window.sending = { id(11) };
        window.changed = { id(12) };
        window.open    = { id(14) };
        run.recompile({}, { { prim, "House" } }, "auto");
        ensure("B left alone", std::none_of(window.asked.begin(), window.asked.end(), [](const auto& a) { return a.ref.item == id(11); }));
        ensure("and said", said("RecompileSkippedSaving", true));
        ensure("C's unsaved changes said", said("RecompileUnsaved", true));
        ensure_equals("four begun past B", window.asked.size(), size_t(4));

        window.asked[0].told(compiled());                    // A
        window.asked[1].told(failed(2, "syntax error"));     // C
        window.asked[2].told(notSent("no such object"));     // D
        window.asked[3].told(failed(0, "wrong"));            // E, open
        ensure("the last begun", window.asked.size() == 5 && window.asked[4].ref.item == id(15));
        window.asked[4].told(compiled());                    // F

        ensure("compiled said", said("Compiled", false));
        ensure("a failure said", said("CompileFailed", true));
        ensure("one not sent said with why", said("SaveFailed", true));
        const Recompile::Script& c = window.scripts[1];
        ensure("C's rows, by the compiler", c.one.name == "C" && c.rows.size() == 1 && c.rows[0].line == 2 && c.rows[0].level == ALScriptStudioDoc::Level::Error);
        ensure("E is open: its tab says it", window.scripts[3].open && window.scripts[3].rows.empty());
        ensure("D's why", window.scripts[2].error == "no such object");
        const Recompile::Done& done = window.done.at(0);
        ensure("counted", done.compiled == 2 && done.failed == 2 && done.notSent == 1 && done.skipped == 1);
    }

    template<> template<>
    void alscriptrecompile_object::test<3>()
    {
        set_test_name("what the compiler said is read back through the expansion that went up, under the envelope's lines, to the source and its includes");
        ALPreprocessor::Options options;
        options.fileName = "main.lsl";
        options.resolve  = [](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
            out.name = ask.name;
            out.text = "integer a;\ninteger b;\n";
            return ALPreprocessor::Found::Yes;
        };
        const ALPreprocessor::Result expanded = ALPreprocessor::run("#include \"lib.lsl\"\ndefault\n{\nstate_entry() { x; }\n}\n", options);
        ensure("expanded", expanded.problems.empty());
        // The lines of the expansion that hold each.
        S32 entry = -1, second = -1, line = 0;
        for (size_t at = 0, next; at < expanded.text.size(); at = next + 1, ++line)
        {
            next                    = expanded.text.find('\n', at);
            const std::string piece = expanded.text.substr(at, next == std::string::npos ? std::string::npos : next - at);
            entry                   = piece.find("state_entry") != std::string::npos ? line : entry;
            second                  = piece.find("integer b") != std::string::npos ? line : second;
            if (next == std::string::npos)
            {
                break;
            }
        }
        ensure("found in the expansion", entry >= 0 && second >= 0);

        Result result    = failed(entry + 7, "undefined x");
        result.codeLine  = 7;
        result.sourceMap = std::make_shared<const ALSourceMap>(expanded.map);
        result.diagnostics.push_back(failed(second + 7, "b again").diagnostics[0]);
        result.diagnostics.push_back(failed(5000, "somewhere made").diagnostics[0]);
        const std::vector<ALScriptStudioDoc::Shown> rows = Recompile::rowsOf(result, "Compiler", "made");
        ensure_equals("three", rows.size(), size_t(3));
        ensure("the script's own line", rows[0].line == 3 && rows[0].file.empty() && rows[0].origin == "Compiler");
        ensure("the include's, by its name", rows[1].line == 1 && rows[1].fileName == "lib.lsl");
        ensure("none: made by the preprocessor", rows[2].file == ALScriptStudioDoc::GENERATED && rows[2].fileName == "made");

        // Nothing expanded: as the compiler said it.
        const std::vector<ALScriptStudioDoc::Shown> plain = Recompile::rowsOf(failed(4, "as is"), "Compiler", "made");
        ensure("as said", plain.size() == 1 && plain[0].line == 4 && plain[0].file.empty());
    }

    template<> template<>
    void alscriptrecompile_object::test<4>()
    {
        set_test_name("a recompile begun again lets the last go; one with nothing to do is done at once");
        house();
        run.recompile({}, { { prim, "House" } }, "auto");
        const size_t first = window.asked.size();
        window.listing.scripts.resize(1);
        run.recompile({}, { { prim, "House" } }, "lsl2");
        ensure_equals("the new one begun", window.asked.size(), first + 1);
        window.asked[0].told(compiled());
        ensure("the old one's answer passed over", window.scripts.empty() && window.done.empty());
        window.asked[first].told(compiled());
        ensure("the new one done", window.done.size() == 1 && window.done[0].target == "lsl2" && window.done[0].compiled == 1);

        window.listing.scripts.clear();
        run.recompile({}, {}, "auto");
        ensure("nothing: done at once", window.done.size() == 2 && window.done[1].compiled == 0 && !run.running());
    }
}
