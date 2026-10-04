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
#include "lltimer.h"

#include "../test/lltut.h"

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

    // What a script said on a channel, as llSay's stand-in heard it.
    std::vector<std::string> gSaid;
    void heardSay(LLScriptLibData* retval, LLScriptLibData* args, const LLUUID& id)
    {
        gSaid.emplace_back(args[1].mString ? args[1].mString : "");
    }

    // Where LSO keeps the version, the top of memory and the default
    // state's handlers (gLSCRIPTRegisterAddresses).
    constexpr size_t TM  = 0;
    constexpr size_t VN  = 8;
    constexpr size_t NER = 92;
}

namespace tut
{
    struct allslreference_data
    {
    };

    typedef test_group<allslreference_data> allslreference_group;
    typedef allslreference_group::object    allslreference_object;
    allslreference_group                    allslreference_instance("allslreference");

    template<> template<>
    void allslreference_object::test<1>()
    {
        set_test_name("a script compiles to a whole LSO image, and to CIL whose class is named from the script's id");
        const std::string text = "integer g = 3;\n"
                                 "default\n"
                                 "{\n"
                                 "    state_entry()\n"
                                 "    {\n"
                                 "        llSay(0, \"hello \" + (string)g);\n"
                                 "    }\n"
                                 "}\n";
        const ALLSLReference::Result lso = ALLSLReference::compile(text, Target::LSO);
        ensure("LSO: " + lso.messages, lso.ok);
        ensure_equals("the image fills LSO's memory", lso.image.size(), (size_t)16384);
        ensure_equals("top of memory", reg32(lso.image, TM), 16384);
        ensure_equals("LSO2", reg32(lso.image, VN), 0x0200);
        ensure_equals("the default state handles state_entry", reg64(lso.image, NER), (U64)0x1);
        // LL's compiler calls every library function by its two-byte form.
        ensure("llSay is library function 23", holds(lso.image, { 0xd1, 0x00, 23 }));

        const ALLSLReference::Result cil = ALLSLReference::compile(text, Target::CIL);
        ensure("CIL: " + cil.messages, cil.ok);
        ensure("the class: " + cil.cil, has(cil.cil, ".class public auto ansi serializable beforefieldinit LSL_00000000_0000_0000_0000_000000000000 extends"));
        ensure("the global a field: " + cil.cil, has(cil.cil, ".field public int32 'g'"));
        ensure("the handler a method: " + cil.cil, has(cil.cil, "instance default void edefaultstate_entry() cil managed"));
        ensure("llSay called by name: " + cil.cil, has(cil.cil, "Library::'llSay'(int32, string)"));
    }

    template<> template<>
    void allslreference_object::test<2>()
    {
        set_test_name("the grid's definitions: a function newer than the 2015 table at its own number, and constants newer than the 2015 lexer");
        const std::string text = "default\n"
                                 "{\n"
                                 "    state_entry()\n"
                                 "    {\n"
                                 "        llLinksetDataWrite(\"k\", (string)(PRIM_GLTF_BASE_COLOR + OBJECT_DAMAGE));\n"
                                 "        llOwnerSay(JSON_ARRAY + (string)PI);\n"
                                 "    }\n"
                                 "}\n";
        const ALLSLReference::Result lso = ALLSLReference::compile(text, Target::LSO);
        ensure("LSO: " + lso.messages, lso.ok);
        ensure("llLinksetDataWrite is library function 650", holds(lso.image, { 0xd1, 0x02, 0x8a }));

        const ALLSLReference::Result cil = ALLSLReference::compile(text, Target::CIL);
        ensure("CIL: " + cil.messages, cil.ok);
        ensure("llLinksetDataWrite: " + cil.cil, has(cil.cil, "Library::'llLinksetDataWrite'(string, string)"));
        ensure("PRIM_GLTF_BASE_COLOR is 48: " + cil.cil, has(cil.cil, "ldc.i4 48"));
        ensure("OBJECT_DAMAGE is 51: " + cil.cil, has(cil.cil, "ldc.i4 51"));
    }

    template<> template<>
    void allslreference_object::test<3>()
    {
        set_test_name("the events new since 2015 compile, and LSO's default state carries each one's bit");
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
        ensure("LSO: " + lso.messages, lso.ok);
        // Events 35 to 43, bits 34 to 42.
        ensure_equals("their bits", reg64(lso.image, NER), (U64)0x1ff << 34);

        const ALLSLReference::Result cil = ALLSLReference::compile(text, Target::CIL);
        ensure("CIL: " + cil.messages, cil.ok);
        for (const char* handler : { "edefaultexperience_permissions(", "edefaulttransaction_result(", "edefaultpath_update(",
                                     "edefaultexperience_permissions_denied(", "edefaultlinkset_data(", "edefaultgame_control(",
                                     "edefaulton_death(", "edefaulton_damage(", "edefaultfinal_damage(" })
        {
            ensure(std::string(handler) + ": " + cil.cil, has(cil.cil, handler));
        }
    }

    template<> template<>
    void allslreference_object::test<4>()
    {
        set_test_name("a global read in a function and an event, and a remote_data handler, compile and free cleanly");
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
            ensure("compiles: " + r.messages, r.ok);
        }
    }

    template<> template<>
    void allslreference_object::test<5>()
    {
        set_test_name("what LL's compiler rejects: a syntax error; and, building LSO's image alone, a list or an unset global in a global's list");
        const ALLSLReference::Result syntax = ALLSLReference::compile("default { state_entry() { integer = 1; } }\n", Target::LSO);
        ensure("a syntax error: " + syntax.messages, !syntax.ok && has(syntax.messages, "ERROR : Syntax error"));

        // A global list's elements are checked as LSO's image lays them out
        // (LSCP_LIST_BUILD_SIMPLE), a pass the CIL never runs.
        const std::string nested = "list a = [1];\nlist b = [a];\ndefault { state_entry() { llOwnerSay((string)b); } }\n";
        const ALLSLReference::Result nested_lso = ALLSLReference::compile(nested, Target::LSO);
        ensure("LSO rejects a list in a global's list: " + nested_lso.messages,
               !nested_lso.ok && has(nested_lso.messages, "Lists can't be included in lists"));
        const ALLSLReference::Result nested_cil = ALLSLReference::compile(nested, Target::CIL);
        ensure("CIL takes it: " + nested_cil.messages, nested_cil.ok);

        const std::string unset = "string s;\nlist l = [s];\ndefault { state_entry() { llOwnerSay((string)l); } }\n";
        const ALLSLReference::Result unset_lso = ALLSLReference::compile(unset, Target::LSO);
        ensure("LSO rejects an unset global in a global's list: " + unset_lso.messages,
               !unset_lso.ok && has(unset_lso.messages, "Unitialized variables can't be included in lists"));
        const ALLSLReference::Result unset_cil = ALLSLReference::compile(unset, Target::CIL);
        ensure("CIL takes it: " + unset_cil.messages, unset_cil.ok);
    }

    template<> template<>
    void allslreference_object::test<6>()
    {
        set_test_name("LL's VM runs a compiled image's state_entry, a global, a function and a cast in it, its library call answered by a stand-in");
        const std::string text = "integer g = 6;\n"
                                 "integer twice(integer n) { return n * 2; }\n"
                                 "default { state_entry() { llSay(0, (string)(twice(g) * 3 + 6)); } }\n";
        const ALLSLReference::Result lso = ALLSLReference::compile(text, Target::LSO);
        ensure("LSO: " + lso.messages, lso.ok);

        // llSay is function 23: heard here for the test, as it was.
        LLScriptLibraryFunction& say  = gScriptLibrary.mFunctions[23];
        const auto               was  = say.mExecFunc;
        say.mExecFunc                 = heardSay;
        gSaid.clear();
        LLScriptExecuteLSL2 vm(lso.image.data(), static_cast<U32>(lso.image.size()));
        LLTimer             timer;
        const char*         error  = nullptr;
        U32                 events = 0;
        for (S32 quanta = 0; quanta < 100 && gSaid.empty() && !error; ++quanta)
        {
            vm.runQuanta(FALSE, LLUUID::null, &error, 1.f, events, timer);
        }
        say.mExecFunc = was;
        ensure("no fault: " + std::string(error ? error : ""), !error);
        ensure_equals("one handler run", events, (U32)1);
        ensure_equals("said once", gSaid.size(), (size_t)1);
        ensure_equals("what it worked out", gSaid[0], std::string("42"));
    }
}
