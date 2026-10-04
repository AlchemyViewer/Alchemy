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

#include "../test/lltut.h"

namespace tut
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

    typedef test_group<alscriptsnapshot_data> alscriptsnapshot_group;
    typedef alscriptsnapshot_group::object    alscriptsnapshot_object;
    alscriptsnapshot_group                    alscriptsnapshot_instance("alscriptsnapshot");

    template<> template<>
    void alscriptsnapshot_object::test<1>()
    {
        set_test_name("a run takes what was answered, says what was not found, and notes each name nobody looked up once");
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
        ensure("the answered one in", made.text.find("integer helper()") != std::string::npos);
        ensure("the missing one said", std::any_of(made.problems.begin(), made.problems.end(), [](const ALScriptProblem& p) { return p.key == "PreprocIncludeNotFound"; }));
        const std::vector<ALPreprocessor::Ask> missed = snapshot.missed();
        ensure("the unknown one noted once", missed.size() == 1 && missed[0].name == "new.lsl");
        ensure("taken away by the asking", snapshot.missed().empty());
    }

    template<> template<>
    void alscriptsnapshot_object::test<2>()
    {
        set_test_name("a name's key is its name, who asks, and whether it is a require");
        ensure("the same", ALScriptSnapshot::keyOf(ask("a")) == ALScriptSnapshot::keyOf(ask("a")));
        ensure("another asking", ALScriptSnapshot::keyOf(ask("a", "disk:/x.lsl")) != ALScriptSnapshot::keyOf(ask("a")));
        ensure("a require", ALScriptSnapshot::keyOf(ask("a", std::string(), true)) != ALScriptSnapshot::keyOf(ask("a")));
    }
}
