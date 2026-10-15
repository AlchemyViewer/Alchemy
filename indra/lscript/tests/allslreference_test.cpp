/**
 * @file allslreference_test.cpp
 * @brief LL's LSL compiler over a handful of scripts, each to LSO and to CIL: the grid's definitions, its new events, and what it rejects; and LL's VM running one.
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

#include "../allslreference.h"

#include "../lscript_execute.h"
#include "../lscript_library.h"
#include "allsovmrun.h"
#include "lltimer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
{
    using Target = ALLSLReference::Target;

    // An LSO register: four bytes, the high one first.
    S32 reg32(const std::vector<U8>& image, size_t at)
    {
        return (S32)((U32)image[at] << 24 | (U32)image[at + 1] << 16 | (U32)image[at + 2] << 8 | (U32)image[at + 3]);
    }

    U64 reg64(const std::vector<U8>& image, size_t at)
    {
        return (U64)(U32)reg32(image, at) << 32 | (U32)reg32(image, at + 4);
    }

    bool holds(const std::vector<U8>& image, std::initializer_list<U8> bytes)
    {
        return std::search(image.begin(), image.end(), bytes.begin(), bytes.end()) != image.end();
    }

    bool has(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // Where LSO keeps the version, the top of memory and the default
    // state's handlers (gLSCRIPTRegisterAddresses).
    constexpr size_t TM  = 0;
    constexpr size_t VN  = 8;
    constexpr size_t NER = 92;
}

namespace
{
    TEST_CASE("allslreference: a script compiles to a whole LSO image and to CIL named from its id", "[allslreference]")
    {
        const std::string text = "integer g = 3;\n"
                                 "default\n"
                                 "{\n"
                                 "    state_entry()\n"
                                 "    {\n"
                                 "        llSay(0, \"hello \" + (string)g);\n"
                                 "    }\n"
                                 "}\n";
        const ALLSLReference::Result lso = ALLSLReference::compile(text, Target::LSO);
        UNSCOPED_INFO("LSO: " + lso.messages);
        CHECK(lso.ok);
        UNSCOPED_INFO("the image fills LSO's memory");
        REQUIRE(lso.image.size() == (size_t)16384);
        UNSCOPED_INFO("top of memory");
        CHECK(reg32(lso.image, TM) == 16384);
        UNSCOPED_INFO("LSO2");
        CHECK(reg32(lso.image, VN) == 0x0200);
        UNSCOPED_INFO("the default state handles state_entry");
        CHECK(reg64(lso.image, NER) == (U64)0x1);
        // LL's compiler calls every library function by its two-byte form.
        UNSCOPED_INFO("llSay is library function 23");
        CHECK(holds(lso.image, { 0xd1, 0x00, 23 }));

        const ALLSLReference::Result cil = ALLSLReference::compile(text, Target::CIL);
        UNSCOPED_INFO("CIL: " + cil.messages);
        CHECK(cil.ok);
        UNSCOPED_INFO("the class: " + cil.cil);
        CHECK(has(cil.cil, ".class public auto ansi serializable beforefieldinit LSL_00000000_0000_0000_0000_000000000000 extends"));
        UNSCOPED_INFO("the global a field: " + cil.cil);
        CHECK(has(cil.cil, ".field public int32 'g'"));
        UNSCOPED_INFO("the handler a method: " + cil.cil);
        CHECK(has(cil.cil, "instance default void edefaultstate_entry() cil managed"));
        UNSCOPED_INFO("llSay called by name: " + cil.cil);
        CHECK(has(cil.cil, "Library::'llSay'(int32, string)"));
    }

    TEST_CASE("allslreference: the grid's definitions: a post-2015 function at its number and new constants", "[allslreference]")
    {
        const std::string text = "default\n"
                                 "{\n"
                                 "    state_entry()\n"
                                 "    {\n"
                                 "        llLinksetDataWrite(\"k\", (string)(PRIM_GLTF_BASE_COLOR + OBJECT_DAMAGE));\n"
                                 "        llOwnerSay(JSON_ARRAY + (string)PI);\n"
                                 "    }\n"
                                 "}\n";
        const ALLSLReference::Result lso = ALLSLReference::compile(text, Target::LSO);
        UNSCOPED_INFO("LSO: " + lso.messages);
        CHECK(lso.ok);
        UNSCOPED_INFO("llLinksetDataWrite is library function 650");
        CHECK(holds(lso.image, { 0xd1, 0x02, 0x8a }));

        const ALLSLReference::Result cil = ALLSLReference::compile(text, Target::CIL);
        UNSCOPED_INFO("CIL: " + cil.messages);
        CHECK(cil.ok);
        UNSCOPED_INFO("llLinksetDataWrite: " + cil.cil);
        CHECK(has(cil.cil, "Library::'llLinksetDataWrite'(string, string)"));
        UNSCOPED_INFO("PRIM_GLTF_BASE_COLOR is 48: " + cil.cil);
        CHECK(has(cil.cil, "ldc.i4 48"));
        UNSCOPED_INFO("OBJECT_DAMAGE is 51: " + cil.cil);
        CHECK(has(cil.cil, "ldc.i4 51"));
    }

    TEST_CASE("allslreference: the events new since 2015 compile and LSO's default state has each one's bit", "[allslreference]")
    {
        const std::string text = "default\n"
                                 "{\n"
                                 "    experience_permissions(key agent) {}\n"
                                 "    transaction_result(key id, integer success, string data) {}\n"
                                 "    path_update(integer type, list reserved) {}\n"
                                 "    experience_permissions_denied(key agent, integer reason) {}\n"
                                 "    linkset_data(integer action, string name, string value) {}\n"
                                 "    game_control(key id, integer buttons, list axes) {}\n"
                                 "    on_death() {}\n"
                                 "    on_damage(integer n) {}\n"
                                 "    final_damage(integer n) {}\n"
                                 "}\n";
        const ALLSLReference::Result lso = ALLSLReference::compile(text, Target::LSO);
        UNSCOPED_INFO("LSO: " + lso.messages);
        REQUIRE(lso.ok);
        // Events 35 to 43, bits 34 to 42.
        UNSCOPED_INFO("their bits");
        CHECK(reg64(lso.image, NER) == (U64)0x1ff << 34);

        const ALLSLReference::Result cil = ALLSLReference::compile(text, Target::CIL);
        UNSCOPED_INFO("CIL: " + cil.messages);
        CHECK(cil.ok);
        for (const char* handler : { "edefaultexperience_permissions(", "edefaulttransaction_result(", "edefaultpath_update(",
                                     "edefaultexperience_permissions_denied(", "edefaultlinkset_data(", "edefaultgame_control(",
                                     "edefaulton_death(", "edefaulton_damage(", "edefaultfinal_damage(" })
        {
            INFO(handler << ": " << cil.cil);
            REQUIRE(has(cil.cil, handler));
        }
    }

    TEST_CASE("allslreference: globals read in functions and events and remote_data compile and free cleanly", "[allslreference]")
    {
        const std::string text = "integer g = 1;\n"
                                 "integer f() { return g + 1; }\n"
                                 "default\n"
                                 "{\n"
                                 "    state_entry() { g = f(); }\n"
                                 "    remote_data(integer t, key c, key m, string s, integer i, string d) { g = i; }\n"
                                 "}\n";
        for (Target target : { Target::LSO, Target::CIL })
        {
            const ALLSLReference::Result r = ALLSLReference::compile(text, target);
            UNSCOPED_INFO("compiles: " + r.messages);
            REQUIRE(r.ok);
        }
    }

    TEST_CASE("allslreference: LL's compiler rejects bad syntax and on LSO a list or unset global in a list", "[allslreference]")
    {
        const ALLSLReference::Result syntax = ALLSLReference::compile("default { state_entry() { integer = 1; } }\n", Target::LSO);
        {
            INFO("a syntax error: " + syntax.messages);
            CHECK_FALSE(syntax.ok);
            CHECK(has(syntax.messages, "ERROR : Syntax error"));
        }

        // A global list's elements are checked as LSO's image lays them out
        // (LSCP_LIST_BUILD_SIMPLE), a pass the CIL never runs.
        const std::string nested = "list a = [1];\nlist b = [a];\ndefault { state_entry() { llOwnerSay((string)b); } }\n";
        const ALLSLReference::Result nested_lso = ALLSLReference::compile(nested, Target::LSO);
        {
            INFO("LSO rejects a list in a global's list: " + nested_lso.messages);
            CHECK_FALSE(nested_lso.ok);
            CHECK(has(nested_lso.messages, "Lists can't be included in lists"));
        }
        const ALLSLReference::Result nested_cil = ALLSLReference::compile(nested, Target::CIL);
        UNSCOPED_INFO("CIL takes it: " + nested_cil.messages);
        CHECK(nested_cil.ok);

        const std::string unset = "string s;\nlist l = [s];\ndefault { state_entry() { llOwnerSay((string)l); } }\n";
        const ALLSLReference::Result unset_lso = ALLSLReference::compile(unset, Target::LSO);
        {
            INFO("LSO rejects an unset global in a global's list: " + unset_lso.messages);
            CHECK_FALSE(unset_lso.ok);
            CHECK(has(unset_lso.messages, "Unitialized variables can't be included in lists"));
        }
        const ALLSLReference::Result unset_cil = ALLSLReference::compile(unset, Target::CIL);
        UNSCOPED_INFO("CIL takes it: " + unset_cil.messages);
        CHECK(unset_cil.ok);
    }

    TEST_CASE("allslreference: LL's VM runs compiled state_entry code with a stand-in answering llSay", "[allslreference]")
    {
        const ll_test_lso::Run run = ll_test_lso::runLSO("integer g = 6;\n"
                                                         "integer twice(integer n) { return n * 2; }\n"
                                                         "default { state_entry() { llSay(0, (string)(twice(g) * 3 + 6)); } }\n");
        CHECK(run.error.empty());
        CHECK(run.events == (U32)1);
        REQUIRE(run.said.size() == (size_t)1);
        CHECK(run.said[0] == std::string("42"));
    }
}
