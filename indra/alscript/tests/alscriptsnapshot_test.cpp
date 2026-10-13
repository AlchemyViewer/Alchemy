/**
 * @file alscriptsnapshot_test.cpp
 * @brief Tests for ALScriptSnapshot: a run over answers taken ahead, what it lacked noted.
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
 */

#include "linden_common.h"

#include "../preprocessor/alscriptsnapshot.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alscriptsnapshot_data
    {
        static ALPreprocessor::Ask ask(const std::string& name, const std::string& from = std::string(), bool require = false)
        {
            ALPreprocessor::Ask out;
            out.name    = name;
            out.from    = from;
            out.require = require;
            return out;
        }
    };

    TEST_CASE_METHOD(alscriptsnapshot_data, "alscriptsnapshot: a run uses answers and reports misses and notes each unknown name once", "[alscriptsnapshot]")
    {
        ALScriptSnapshot snapshot;
        snapshot.options().fileName = "main.lsl";
        ALPreprocessor::Include lib;
        lib.text = "integer helper() { return 1; }\n";
        lib.path = "disk:/lib.lsl";
        lib.name = "lib.lsl";
        snapshot.answer(ask("lib.lsl"), ALPreprocessor::Found::Yes, lib);
        snapshot.answer(ask("gone.lsl"), ALPreprocessor::Found::No);
        const ALPreprocessor::Result made = snapshot.run(
            "#include \"lib.lsl\"\n#include \"gone.lsl\"\n#include \"new.lsl\"\n#include \"new.lsl\"\ndefault { state_entry() { helper(); } }\n");
        UNSCOPED_INFO("the answered one in");
        CHECK(made.text.find("integer helper()") != std::string::npos);
        UNSCOPED_INFO("the missing one said");
        CHECK(std::any_of(made.problems.begin(), made.problems.end(), [](const ALScriptProblem& p) { return p.key == "PreprocIncludeNotFound"; }));
        const std::vector<ALPreprocessor::Ask> missed = snapshot.missed();
        {
            INFO("the unknown one noted once");
            REQUIRE(missed.size() == 1);
            CHECK(missed[0].name == "new.lsl");
        }
        UNSCOPED_INFO("taken away by the asking");
        CHECK(snapshot.missed().empty());
    }

    TEST_CASE_METHOD(alscriptsnapshot_data, "alscriptsnapshot: keyOf is the name and who asks and whether it is a require", "[alscriptsnapshot]")
    {
        UNSCOPED_INFO("the same");
        CHECK(ALScriptSnapshot::keyOf(ask("a")) == ALScriptSnapshot::keyOf(ask("a")));
        UNSCOPED_INFO("another asking");
        CHECK(ALScriptSnapshot::keyOf(ask("a", "disk:/x.lsl")) != ALScriptSnapshot::keyOf(ask("a")));
        UNSCOPED_INFO("a require");
        CHECK(ALScriptSnapshot::keyOf(ask("a", std::string(), true)) != ALScriptSnapshot::keyOf(ask("a")));
    }
}
