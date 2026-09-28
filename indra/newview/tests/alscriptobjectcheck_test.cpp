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

#include "../test/lltut.h"

#include <map>
#include <set>
#include <sstream>

// The lints as a scripter chose them are the viewer's settings: here, every
// warning named "off" dropped.
void ALScriptLints::apply(ALScriptProblems& problems)
{
    problems.erase(std::remove_if(problems.begin(), problems.end(), [](const ALScriptProblem& p) { return p.code == "off"; }), problems.end());
}
ALLuauConfig ALScriptLints::luauBase()
{
    return ALLuauConfig();
}
std::string alScriptKeyedWords(const std::string&, const std::vector<std::string>&, const std::string& english)
{
    return english;
}
bool ALScriptPreprocessor::fileOf(const std::string& path, std::string& file)
{
    if (path.rfind("disk:", 0) != 0)
    {
        return false;
    }
    file = path.substr(5);
    return !file.empty();
}
std::string ALScriptPreprocessor::pathOf(const ALScriptRef& ref)
{
    return "object:" + ref.object.asString() + ":" + ref.item.asString();
}
std::string ALScriptModules::identity(const std::string& path)
{
    return path;
}

namespace
{
    typedef ALScriptObjectCheck Check;
    typedef Check::Window       Window;

    // The window and the world, answered as a test says: the object's
    // scripts; which are open; each script's text, or none; the
    // expansions and questions asked, held for the test to answer.
    struct FakeWindow : public Window
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

namespace tut
{
    struct alscriptobjectcheck_data
    {
        al_studio_test::FakeServices services;
        FakeWindow                   window;
        Check                        check{ services, window };
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
    typedef test_group<alscriptobjectcheck_data> alscriptobjectcheck_group;
    typedef alscriptobjectcheck_group::object    alscriptobjectcheck_object;
    alscriptobjectcheck_group                    alscriptobjectcheck_instance("alscriptobjectcheck");

    template<> template<>
    void alscriptobjectcheck_object::test<1>()
    {
        set_test_name("an object's scripts checked a few at a time, the open ones left to their tabs; one not read and the prims that did not answer said when done");
        house();
        check.check(root, "House");
        ensure("listed", window.listedFor == std::vector<LLUUID>{ root });
        ensure("running", check.running());
        ensure_equals("two at once", window.asks.size(), size_t(2));
        ensure("the open one not read", window.reads.size() == 2 && window.reads[0].item == id(11) && window.reads[1].item == id(12));
        ensure("a check of what was read", window.asks[0].request.kind == ALScriptAnalysis::Kind::Check && *window.asks[0].request.text == "default {}\n");
        ensure("for its problems alone", !window.asks[0].request.semantics && !window.asks[0].request.hintTypes);
        ensure("not expanded, the preprocessor off", window.expands.empty());

        window.asks[0].answered(answer({ problem(0, "wrong"), problem(1, "unwanted", ALScriptProblem::Severity::Warning, "off") }));
        ensure_equals("B checked", window.checked.size(), size_t(1));
        const Check::Script& b = window.checked[0];
        ensure("by its name", b.name == "B" && b.ref.item == id(11));
        ensure_equals("the lints as chosen applied", b.rows.size(), size_t(1));
        ensure("said as a tab's row", b.rows[0].message == "wrong" && b.rows[0].level == ALScriptStudioDoc::Level::Error && b.rows[0].line == 0);
        ensure("the next begun, which could not be read", window.reads.size() == 3 && window.reads[2].item == id(13));
        ensure("not done while C is out", window.done.empty());

        window.asks[1].answered(answer({}));
        ensure_equals("done", window.done.size(), size_t(1));
        const Check::Done& done = window.done[0];
        ensure("of the house", done.root == root && done.name == "House" && done.present);
        ensure_equals("checked", done.checked, 2);
        ensure_equals("left to its tab", done.open, 1);
        ensure_equals("prims that did not answer", done.unlisted, 1);
        ensure("the one not read, by name", done.unread == std::vector<std::string>{ "D" });
        ensure("no longer running", !check.running());
    }

    template<> template<>
    void alscriptobjectcheck_object::test<2>()
    {
        set_test_name("with the preprocessor on, each script expanded first and its problems read back through the expansion to the script or its include");
        window.preprocess = true;
        window.listing.scripts.push_back({ ALScriptRef(root, id(20)), "Main" });
        window.texts = { { id(20), "#include \"lib\"\ndefault {}\n" } };
        check.check(root, "Shed");
        ensure_equals("expanded", window.expands.size(), size_t(1));
        ensure("apart, as a tab's check", window.expands[0].request.apart && window.expands[0].request.sourceText() == "#include \"lib\"\ndefault {}\n");

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
        ensure_equals("asked", window.asks.size(), size_t(1));
        ensure_equals("about the expansion", *window.asks[0].request.text, std::string("integer x;\ndefault {}\n"));

        window.asks[0].answered(answer({ problem(0, "in the library"), problem(1, "in the script") }));
        ensure_equals("checked", window.checked.size(), size_t(1));
        const std::vector<ALScriptStudioDoc::Shown>& rows = window.checked[0].rows;
        ensure_equals("the preprocessor's and the analyzers'", rows.size(), size_t(3));
        ensure("the preprocessor's first", rows[0].message == "from the preprocessor" && rows[0].origin == "OriginPreprocessor");
        ensure("the include's, under its name", rows[1].file == "disk:/lib.lsl" && rows[1].fileName == "lib" && rows[1].line == 0);
        ensure("the script's own, at its line", rows[2].file.empty() && rows[2].line == 1);
        ensure_equals("done", window.done.size(), size_t(1));
    }

    template<> template<>
    void alscriptobjectcheck_object::test<3>()
    {
        set_test_name("a check begun again lets the last one's answers go; an object out of sight is said so at once");
        house();
        check.check(root, "House");
        std::function<void(const ALScriptAnalysis::Result&)> stale = std::move(window.asks[0].answered);
        window.asks.clear();
        window.checked.clear();
        check.check(root, "House");
        stale(answer({ problem(0, "old") }));
        ensure("the earlier answer dropped", window.checked.empty());
        ensure_equals("the new one's asked", window.asks.size(), size_t(2));

        FakeWindow  away;
        away.listing.present = false;
        Check       gone(services, away);
        gone.check(id(99), "Gone");
        ensure("said at once, as not in sight", away.done.size() == 1 && !away.done[0].present && away.done[0].checked == 0);
    }
}
