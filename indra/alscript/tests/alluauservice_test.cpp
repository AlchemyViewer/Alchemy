/**
 * @file tests/alluauservice_test.cpp
 * @brief The SLua analyzer against the grid's definitions.
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

#include "../alluauservice.h"

#include "../test/lltut.h"

#include <fstream>
#include <sstream>

namespace tut
{
    struct alluauservice_data
    {
        ALLuauService service;
        std::string   definitions;
        std::string   error;
        bool          loaded = false;

        alluauservice_data()
        {
            std::ifstream in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            definitions = text.str();
            loaded      = service.loadDefinitions(definitions, error);
        }

        // Every problem on a line, for a failure message that says what
        // the analyzer actually said.
        static std::string said(const ALScriptProblems& problems)
        {
            std::string out;
            for (const ALScriptProblem& problem : problems)
            {
                out += llformat("[%d:%d] %s %s\n", problem.line, problem.column, problem.code.c_str(), problem.message.c_str());
            }
            return out;
        }

        static size_t errors(const ALScriptProblems& problems)
        {
            size_t count = 0;
            for (const ALScriptProblem& problem : problems)
            {
                count += problem.severity == ALScriptProblem::Severity::Error;
            }
            return count;
        }

        static bool mentions(const ALScriptProblems& problems, const char* word)
        {
            for (const ALScriptProblem& problem : problems)
            {
                if (problem.message.find(word) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }
    };

    typedef test_group<alluauservice_data> alluauservice_group;
    typedef alluauservice_group::object    alluauservice_object;
    alluauservice_group                    alluauservice_instance("alluauservice");

    template<> template<>
    void alluauservice_object::test<1>()
    {
        set_test_name("the grid's definitions load");
        ensure("definitions file read", !definitions.empty());
        ensure("definitions load: " + error, loaded);
        ensure("service says so", service.hasDefinitions());
    }

    template<> template<>
    void alluauservice_object::test<2>()
    {
        set_test_name("a script using ll and the SL types checks clean");
        ensure("definitions load: " + error, loaded);
        ALScriptProblems problems = service.check(
            "local owner: uuid = ll.GetOwner()\n"
            "ll.Say(0, tostring(owner))\n"
            "local here: vector = vector.create(1, 2, 3)\n"
            "ll.SetPos(here + vector.one)\n");
        ensure_equals("no errors: " + said(problems), errors(problems), 0);
    }

    template<> template<>
    void alluauservice_object::test<3>()
    {
        set_test_name("a wrong argument type is a type error");
        ensure("definitions load: " + error, loaded);
        ALScriptProblems problems = service.check("ll.Say(\"zero\", 0)\n");
        ensure("an error: " + said(problems), errors(problems) > 0);
        ensure("from the type checker", problems.front().source == ALScriptProblem::Source::Types);
        ensure_equals("on the first line", problems.front().line, 0);
    }

    template<> template<>
    void alluauservice_object::test<4>()
    {
        set_test_name("an unknown global is reported by name");
        ensure("definitions load: " + error, loaded);
        ALScriptProblems problems = service.check("frobnicate(1)\n");
        ensure("an error: " + said(problems), errors(problems) > 0);
        ensure("naming it: " + said(problems), mentions(problems, "frobnicate"));
    }

    template<> template<>
    void alluauservice_object::test<5>()
    {
        set_test_name("a deprecated function is a lint");
        ensure("definitions load: " + error, loaded);
        // ll.Cloud is one of the few functions the ll table itself marks
        // deprecated; the legacy names live in llcompat.
        ALScriptProblems problems = service.check("local density = ll.Cloud(vector.zero)\n");
        ensure("something said: " + said(problems), !problems.empty());
        ensure("deprecated: " + said(problems), mentions(problems, "deprecated"));
    }

    template<> template<>
    void alluauservice_object::test<6>()
    {
        set_test_name("a syntax error is the parser's");
        ensure("definitions load: " + error, loaded);
        ALScriptProblems problems = service.check("local x = \n");
        ensure("an error: " + said(problems), errors(problems) > 0);
        ensure("from the parser", problems.front().source == ALScriptProblem::Source::Parser);
    }

    template<> template<>
    void alluauservice_object::test<7>()
    {
        set_test_name("without the definitions, ll is unknown");
        ALLuauService bare;
        ensure("nothing loaded", !bare.hasDefinitions());
        ALScriptProblems problems = bare.check("ll.Say(0, \"x\")\n");
        ensure("an error: " + said(problems), errors(problems) > 0);
        ensure("naming ll: " + said(problems), mentions(problems, "ll"));
    }

    template<> template<>
    void alluauservice_object::test<8>()
    {
        set_test_name("bad definitions are refused with a reason");
        ALLuauService bare;
        std::string   why;
        ensure("refused", !bare.loadDefinitions("declare ll: {\n", why));
        ensure("with a reason", !why.empty());
        ensure("and nothing loaded", !bare.hasDefinitions());
    }
}
