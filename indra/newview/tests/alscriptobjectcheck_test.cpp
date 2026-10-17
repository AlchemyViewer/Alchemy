/**
 * @file alscriptobjectcheck_test.cpp
 * @brief Every script of an object checked, over a fake of the window and the world.
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

#include "../alscriptobjectcheck.h"

#include "../alscriptmodules.h"

#include "alscriptstudio_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <sstream>

namespace
{
    // The lints as a scripter chose them are the viewer's settings: here,
    // every warning named "off" dropped.
    void applyLints(ALScriptProblems& problems)
    {
        problems.erase(std::remove_if(problems.begin(), problems.end(), [](const ALScriptProblem& p) { return p.code == "off"; }), problems.end());
    }

    typedef ALScriptObjectCheck Check;
    typedef Check::Window       Window;

    // The window and the world, answered as a test says: the object's
    // scripts; which are open; each script's text, or none; the
    // expansions and questions asked, held for the test to answer.
    struct FakeWindow : public Window, public al_studio_test::QuietAnalysis
    {
        void listScripts(const LLUUID& root, std::function<void(Listed)> told) override
        {
            listedFor.push_back(root);
            told(listing);
        }
        bool isOpen(const ALScriptRef& ref) override { return open.contains(ref.item); }
        void read(const ALScriptRef& ref, std::function<void(std::optional<Read>)> told) override
        {
            reads.push_back(ref);
            const auto found = texts.find(ref.item);
            if (found == texts.end())
            {
                told(std::nullopt);
                return;
            }
            Read one;
            one.text          = found->second;
            one.compileTarget = "mono";
            told(one);
        }
        bool preprocessing() const override { return preprocess; }
        void expand(ALScriptPreprocessor::Request request, std::function<void(const ALPreprocessor::Result&)> expanded) override
        {
            expands.push_back({ std::move(request), std::move(expanded) });
        }
        void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) override
        {
            asks.push_back({ std::move(request), std::move(answered) });
        }
        bool luauConfig(const ALScriptPreprocessor::Request&, ALLuauConfig&) const override { return false; }
        void scriptChecked(const Check::Script& script) override { checked.push_back(script); }
        void objectChecked(const Check::Done& done) override { this->done.push_back(done); }

        Listed                                     listing;
        std::set<LLUUID>                           open;
        std::map<LLUUID, std::string>              texts;
        bool                                       preprocess = false;
        std::vector<LLUUID>                        listedFor;
        std::vector<ALScriptRef>                   reads;
        struct Expand
        {
            ALScriptPreprocessor::Request                      request;
            std::function<void(const ALPreprocessor::Result&)> expanded;
        };
        struct Ask
        {
            ALScriptAnalysis::Request                            request;
            std::function<void(const ALScriptAnalysis::Result&)> answered;
        };
        std::vector<Expand>              expands;
        std::vector<Ask>                 asks;
        std::vector<Check::Script>       checked;
        std::vector<Check::Done>         done;
    };

    LLUUID id(U32 n)
    {
        LLUUID out;
        out.mData[0]  = static_cast<U8>(n);
        out.mData[15] = 3;
        return out;
    }

    ALScriptProblem problem(S32 line, const std::string& message, ALScriptProblem::Severity severity = ALScriptProblem::Severity::Error,
                            const std::string& code = std::string())
    {
        ALScriptProblem out;
        out.line      = line;
        out.column    = 2;
        out.endLine   = line;
        out.endColumn = 5;
        out.severity  = severity;
        out.source    = ALScriptProblem::Source::Parser;
        out.message   = message;
        out.code      = code;
        return out;
    }

    ALScriptAnalysis::Result answer(std::vector<ALScriptProblem> problems)
    {
        ALScriptAnalysis::Result result;
        result.kind     = ALScriptAnalysis::Kind::Check;
        result.problems = std::move(problems);
        return result;
    }
}

namespace
{
    struct alscriptobjectcheck_data
    {
        al_studio_test::ChosenLints  lints{ applyLints };
        al_studio_test::FakeServices services;
        FakeWindow                   window;
        Check                        check{ services, window, window };
        const LLUUID                 root = id(1);

        // An object of scripts A to D: A open, D not to be read.
        void house()
        {
            window.listing.unlisted = 1;
            for (U32 i = 0; i < 4; ++i)
            {
                window.listing.scripts.push_back({ ALScriptRef(root, id(10 + i)), std::string(1, static_cast<char>('A' + i)) });
            }
            window.open  = { id(10) };
            window.texts = { { id(11), "default {}\n" }, { id(12), "default {}\n" } };
        }
    };

    TEST_CASE_METHOD(alscriptobjectcheck_data, "alscriptobjectcheck: a check reads scripts two at a time and reports open and unread ones", "[alscriptobjectcheck]")
    {
        house();
        check.check(root, "House");
        CHECK(window.listedFor == std::vector<LLUUID>{ root });
        CHECK(check.running());
        UNSCOPED_INFO("two at once");
        REQUIRE(window.asks.size() == size_t(2));
        {
            INFO("the open one not read");
            REQUIRE(window.reads.size() == 2);
            CHECK(window.reads[0].item == id(11));
            CHECK(window.reads[1].item == id(12));
        }
        {
            INFO("a check of what was read");
            REQUIRE(window.asks[0].request.kind == ALScriptAnalysis::Kind::Check);
            REQUIRE(window.asks[0].request.text);
            CHECK(*window.asks[0].request.text == "default {}\n");
        }
        {
            INFO("for its problems alone");
            CHECK_FALSE(window.asks[0].request.semantics);
            CHECK_FALSE(window.asks[0].request.hintTypes);
        }
        UNSCOPED_INFO("not expanded, the preprocessor off");
        REQUIRE(window.expands.empty());

        window.asks[0].answered(answer({ problem(0, "wrong"), problem(1, "unwanted", ALScriptProblem::Severity::Warning, "off") }));
        UNSCOPED_INFO("B checked");
        REQUIRE(window.checked.size() == size_t(1));
        const Check::Script& b = window.checked[0];
        CHECK(b.name == "B");
        CHECK(b.ref.item == id(11));
        UNSCOPED_INFO("the lints as chosen applied");
        REQUIRE(b.rows.size() == size_t(1));
        {
            INFO("said as a tab's row");
            CHECK(b.rows[0].message == "wrong");
            CHECK(b.rows[0].level == ALScriptStudioDoc::Level::Error);
            CHECK(b.rows[0].line == 0);
        }
        {
            INFO("the next begun, which could not be read");
            REQUIRE(window.reads.size() == 3);
            CHECK(window.reads[2].item == id(13));
        }
        UNSCOPED_INFO("not done while C is out");
        REQUIRE(window.done.empty());

        window.asks[1].answered(answer({}));
        REQUIRE(window.done.size() == size_t(1));
        const Check::Done& done = window.done[0];
        CHECK(done.root == root);
        CHECK(done.name == "House");
        CHECK(done.present);
        CHECK(done.checked == 2);
        UNSCOPED_INFO("left to its tab");
        CHECK(done.open == 1);
        UNSCOPED_INFO("prims that did not answer");
        CHECK(done.unlisted == 1);
        CHECK(done.unread == std::vector<std::string>{ "D" });
        CHECK_FALSE(check.running());
    }

    TEST_CASE_METHOD(alscriptobjectcheck_data, "alscriptobjectcheck: a preprocessed script's problems map back to it or its include", "[alscriptobjectcheck]")
    {
        window.preprocess = true;
        window.listing.scripts.push_back({ ALScriptRef(root, id(20)), "Main" });
        window.texts = { { id(20), "#include \"lib\"\ndefault {}\n" } };
        check.check(root, "Shed");
        REQUIRE(window.expands.size() == size_t(1));
        {
            INFO("apart, as a tab's check");
            CHECK(window.expands[0].request.apart);
            CHECK(window.expands[0].request.sourceText() == "#include \"lib\"\ndefault {}\n");
        }

        // The include's line, then the script's own.
        ALPreprocessor::Result result;
        result.map.addFile("Main", "object:main");
        const S32            lib = result.map.addFile("lib", "disk:/lib.lsl");
        ALSourceMap::Segment inc;
        inc.outLine = 0;
        inc.length  = 12;
        inc.file    = lib;
        inc.line    = 0;
        result.map.add(inc);
        ALSourceMap::Segment own;
        own.outLine = 1;
        own.length  = 10;
        own.file    = 0;
        own.line    = 1;
        result.map.add(own);
        result.map.finish();
        result.text = "integer x;\ndefault {}\n";
        ALScriptProblem said;
        said.line     = 0;
        said.severity = ALScriptProblem::Severity::Warning;
        said.message  = "from the preprocessor";
        result.problems.push_back(said);
        window.expands[0].expanded(result);
        REQUIRE(window.asks.size() == size_t(1));
        REQUIRE(window.asks[0].request.text);
        UNSCOPED_INFO("about the expansion");
        CHECK(*window.asks[0].request.text == std::string("integer x;\ndefault {}\n"));

        window.asks[0].answered(answer({ problem(0, "in the library"), problem(1, "in the script") }));
        REQUIRE(window.checked.size() == size_t(1));
        const std::vector<ALScriptStudioDoc::Shown>& rows = window.checked[0].rows;
        UNSCOPED_INFO("the preprocessor's and the analyzers'");
        REQUIRE(rows.size() == size_t(3));
        {
            INFO("the preprocessor's first");
            CHECK(rows[0].message == "from the preprocessor");
            CHECK(rows[0].origin == "OriginPreprocessor");
        }
        {
            INFO("the include's, under its name");
            CHECK(rows[1].file == "disk:/lib.lsl");
            CHECK(rows[1].fileName == "lib");
            CHECK(rows[1].line == 0);
        }
        {
            INFO("the script's own, at its line");
            REQUIRE(rows[2].file.empty());
            CHECK(rows[2].line == 1);
        }
        REQUIRE(window.done.size() == size_t(1));
    }

    TEST_CASE_METHOD(alscriptobjectcheck_data, "alscriptobjectcheck: a restarted check drops stale answers and an absent object ends at once", "[alscriptobjectcheck]")
    {
        house();
        check.check(root, "House");
        REQUIRE_FALSE(window.asks.empty());
        std::function<void(const ALScriptAnalysis::Result&)> stale = std::move(window.asks[0].answered);
        window.asks.clear();
        window.checked.clear();
        check.check(root, "House");
        stale(answer({ problem(0, "old") }));
        UNSCOPED_INFO("the earlier answer dropped");
        REQUIRE(window.checked.empty());
        UNSCOPED_INFO("the new one's asked");
        REQUIRE(window.asks.size() == size_t(2));

        FakeWindow  away;
        away.listing.present = false;
        Check       gone(services, away, away);
        gone.check(id(99), "Gone");
        {
            INFO("said at once, as not in sight");
            REQUIRE(away.done.size() == 1);
            CHECK_FALSE(away.done[0].present);
            CHECK(away.done[0].checked == 0);
        }
    }
}
