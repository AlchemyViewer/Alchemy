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

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>

namespace
{
    typedef ALScriptRecompile        Recompile;
    typedef Recompile::Window        Window;
    typedef ALScriptCompileResult Result;

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
                       ALScriptCompileCallback told) override
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
            ALScriptCompileCallback told;
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
        ALScriptDiagnostic said;
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

namespace
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

    // SLua goes up as SLua, and what is known of whether each runs is given.
    TEST_CASE_METHOD(alscriptrecompile_data, "alscriptrecompile: a prim's scripts and one chosen go up a few at a time each once", "[alscriptrecompile]")
    {
        house();
        window.runs = { { id(11), false } };
        run.recompile({ one(10, "A") }, { { prim, "House" } }, "mono");
        REQUIRE(window.listedFor.size() == 1);
        REQUIRE(window.listedFor[0].size() == 1);
        CHECK(window.listedFor[0].front().first == prim);
        CHECK(run.running());
        REQUIRE(window.asked.size() == size_t(Recompile::AT_ONCE));
        {
            INFO("A once, chosen and in its prim");
            CHECK(window.asked[0].ref.item == id(10));
            CHECK(window.asked[1].ref.item == id(11));
        }
        CHECK(window.asked[0].target == std::string("mono"));
        UNSCOPED_INFO("the SLua one as it compiles");
        CHECK(window.asked[2].target == std::string("auto"));
        UNSCOPED_INFO("B known stopped, stays so");
        CHECK(window.asked[1].running == false);
        UNSCOPED_INFO("A not known: the save asks");
        CHECK_FALSE(window.asked[0].running.has_value());

        for (size_t i = 0; i < size_t(Recompile::AT_ONCE); ++i)
        {
            window.asked[i].told(compiled());
        }
        UNSCOPED_INFO("the rest begun");
        REQUIRE(window.asked.size() == size_t(6));
        window.asked[4].told(compiled());
        UNSCOPED_INFO("not done yet");
        CHECK(window.done.empty());
        window.asked[5].told(compiled());
        REQUIRE(window.done.size() == size_t(1));
        CHECK(window.done[0].compiled == 6);
        CHECK(window.done[0].failed == 0);
        CHECK(window.done[0].target == "mono");
        CHECK_FALSE(run.running());
        UNSCOPED_INFO("each said");
        CHECK(window.scripts.size() == size_t(6));
    }

    // Changes not saved are said too, and a closed script's result carries
    // its rows.
    TEST_CASE_METHOD(alscriptrecompile_data, "alscriptrecompile: a script being saved is skipped and each result is said in Output", "[alscriptrecompile]")
    {
        house();
        window.sending = { id(11) };
        window.changed = { id(12) };
        window.open    = { id(14) };
        run.recompile({}, { { prim, "House" } }, "auto");
        UNSCOPED_INFO("B left alone");
        CHECK(std::none_of(window.asked.begin(), window.asked.end(), [](const auto& a) { return a.ref.item == id(11); }));
        UNSCOPED_INFO("B's save on its way said");
        CHECK(said("RecompileSkippedSaving", true));
        UNSCOPED_INFO("C's unsaved changes said");
        CHECK(said("RecompileUnsaved", true));
        UNSCOPED_INFO("four begun past B");
        REQUIRE(window.asked.size() == size_t(4));

        window.asked[0].told(compiled());                    // A
        window.asked[1].told(failed(2, "syntax error"));     // C
        window.asked[2].told(notSent("no such object"));     // D
        window.asked[3].told(failed(0, "wrong"));            // E, open
        {
            INFO("the last begun");
            REQUIRE(window.asked.size() == 5);
            CHECK(window.asked[4].ref.item == id(15));
        }
        window.asked[4].told(compiled());                    // F

        CHECK(said("Compiled", false));
        CHECK(said("CompileFailed", true));
        UNSCOPED_INFO("one not sent said with why");
        CHECK(said("SaveFailed", true));
        UNSCOPED_INFO("each but B said");
        REQUIRE(window.scripts.size() == 5);
        const Recompile::Script& c = window.scripts[1];
        {
            INFO("C's rows, by the compiler");
            CHECK(c.one.name == "C");
            REQUIRE(c.rows.size() == 1);
            CHECK(c.rows[0].line == 2);
            CHECK(c.rows[0].level == ALScriptStudioDoc::Level::Error);
        }
        {
            INFO("E is open: its tab says it");
            CHECK(window.scripts[3].open);
            CHECK(window.scripts[3].rows.empty());
        }
        UNSCOPED_INFO("D's why");
        CHECK(window.scripts[2].error == "no such object");
        const Recompile::Done& done = window.done.at(0);
        CHECK(done.compiled == 2);
        CHECK(done.failed == 2);
        CHECK(done.notSent == 1);
        CHECK(done.skipped == 1);
    }

    TEST_CASE_METHOD(alscriptrecompile_data, "alscriptrecompile: rowsOf reads a compiler's lines back to the source and its includes", "[alscriptrecompile]")
    {
        ALPreprocessor::Options options;
        options.fileName = "main.lsl";
        options.resolve  = [](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
            out.name = ask.name;
            out.text = "integer a;\ninteger b;\n";
            return ALPreprocessor::Found::Yes;
        };
        const ALPreprocessor::Result expanded = ALPreprocessor::run("#include \"lib.lsl\"\ndefault\n{\nstate_entry() { x; }\n}\n", options);
        REQUIRE(expanded.problems.empty());
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
        {
            INFO("found in the expansion");
            CHECK(entry >= 0);
            CHECK(second >= 0);
        }

        Result result    = failed(entry + 7, "undefined x");
        result.codeLine  = 7;
        result.sourceMap = std::make_shared<const ALSourceMap>(expanded.map);
        result.diagnostics.push_back(failed(second + 7, "b again").diagnostics[0]);
        result.diagnostics.push_back(failed(5000, "somewhere made").diagnostics[0]);
        const std::vector<ALScriptStudioDoc::Shown> rows = Recompile::rowsOf(result, "Compiler", "made");
        REQUIRE(rows.size() == size_t(3));
        {
            INFO("the script's own line");
            CHECK(rows[0].line == 3);
            CHECK(rows[0].file.empty());
            CHECK(rows[0].origin == "Compiler");
        }
        {
            INFO("the include's, by its name");
            CHECK(rows[1].line == 1);
            CHECK(rows[1].fileName == "lib.lsl");
        }
        {
            INFO("none: made by the preprocessor");
            CHECK(rows[2].file == ALScriptStudioDoc::GENERATED);
            CHECK(rows[2].fileName == "made");
        }

        // Nothing expanded: as the compiler said it.
        const std::vector<ALScriptStudioDoc::Shown> plain = Recompile::rowsOf(failed(4, "as is"), "Compiler", "made");
        {
            INFO("as the compiler said it");
            REQUIRE(plain.size() == 1);
            CHECK(plain[0].line == 4);
            CHECK(plain[0].file.empty());
        }
    }

    TEST_CASE_METHOD(alscriptrecompile_data, "alscriptrecompile: a recompile begun again drops the last and an empty one is done at once", "[alscriptrecompile]")
    {
        house();
        run.recompile({}, { { prim, "House" } }, "auto");
        const size_t first = window.asked.size();
        window.listing.scripts.resize(1);
        run.recompile({}, { { prim, "House" } }, "lsl2");
        UNSCOPED_INFO("the new one begun");
        REQUIRE(window.asked.size() == first + 1);
        window.asked[0].told(compiled());
        {
            INFO("the old one's answer passed over");
            CHECK(window.scripts.empty());
            CHECK(window.done.empty());
        }
        window.asked[first].told(compiled());
        {
            INFO("the new one done");
            REQUIRE(window.done.size() == 1);
            CHECK(window.done[0].target == "lsl2");
            CHECK(window.done[0].compiled == 1);
        }

        window.listing.scripts.clear();
        run.recompile({}, {}, "auto");
        {
            INFO("nothing: done at once");
            REQUIRE(window.done.size() == 2);
            CHECK(window.done[1].compiled == 0);
            CHECK_FALSE(run.running());
        }
    }

    TEST_CASE_METHOD(alscriptrecompile_data, "alscriptrecompile: places named in a compiler's message are read back as its line is", "[alscriptrecompile]")
    {
        ALPreprocessor::Options options;
        options.fileName = "main.lsl";
        options.resolve  = [](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
            out.name = ask.name;
            out.text = "integer a;\ninteger b;\n";
            return ALPreprocessor::Found::Yes;
        };
        const ALPreprocessor::Result expanded = ALPreprocessor::run("#include \"lib.lsl\"\ndefault\n{\nstate_entry() { x; }\n}\n", options);
        REQUIRE(expanded.problems.empty());
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
        {
            INFO("found in the expansion");
            CHECK(entry >= 0);
            CHECK(second >= 0);
        }

        // As Tailslide says it, one-based, under the envelope's seven lines.
        LLSD said;
        said.append(llformat("Line %d: WARN: x shadows previous declaration at (%d, 1)", entry + 8, entry + 8));
        said.append(llformat("Line %d: ERROR: b previously declared at (%d, 9). Again at (5000, 1).", entry + 8, second + 8));
        said.append("Math Error");
        Result result;
        result.codeLine  = 7;
        result.sourceMap = std::make_shared<const ALSourceMap>(expanded.map);
        for (const ALScriptMessages::Place& place : ALScriptMessages::readDiagnostics(said, false))
        {
            ALScriptDiagnostic one;
            one.line     = place.line;
            one.column   = place.column;
            one.hasLine  = place.hasLine;
            one.level    = place.level;
            one.message  = place.message;
            one.mentions = place.mentions;
            result.diagnostics.push_back(one);
        }
        const std::vector<ALScriptStudioDoc::Shown> rows = Recompile::rowsOf(result, "Compiler", "made");
        REQUIRE(rows.size() == size_t(3));
        {
            INFO("the line, the source's");
            CHECK(rows[0].line == 3);
            CHECK(rows[0].level == ALScriptStudioDoc::Level::Warning);
        }
        UNSCOPED_INFO("the place named, the source's");
        CHECK(rows[0].message == std::string("x shadows previous declaration at (4, 1)"));
        UNSCOPED_INFO("an include's, by its name; one made, as said");
        CHECK(rows[1].message ==
                      std::string("b previously declared at (2, 9) in lib.lsl. Again at (5000, 1)."));
        {
            INFO("no line named, none");
            CHECK(rows[2].line == -1);
            CHECK(rows[2].file.empty());
            CHECK(rows[2].message == "Math Error");
        }

        // A tab's, the same.
        const std::vector<ALScriptStudioDoc::Compiled> tab = ALScriptStudioDoc::compiledOf(result.diagnostics, result.sourceMap.get(), 7);
        {
            INFO("a tab's");
            REQUIRE(tab.size() == 3);
            CHECK(tab[0].line == 3);
            CHECK(tab[0].message == rows[0].message);
            CHECK(tab[2].line == -1);
        }
    }
}
