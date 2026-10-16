/**
 * @file allsolibrary.h
 * @brief The LSL library as LL's LSO VM runs it in a test
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#pragma once

#include "../lscript_library.h"

#include <string>
#include <vector>

namespace ll_test_lso
{
    // What scripts said, as the chat functions heard it: llSay, llShout,
    // llWhisper and llRegionSay their message, llOwnerSay its own.
    extern std::vector<std::string> gSaid;

    // The library a test runs scripts with, while one lives. Every function
    // LL's table has does something, where LL's own leave the value they
    // return unset: the math, vector, rotation, string and list functions a
    // script reaches the VM's heap and types through answer as LSL says
    // they do (llListSort and llListRandomize through the allocator's own
    // lsa_bubble_sort and lsa_randomize); the chat functions are heard into
    // gSaid; and every other answers with nothing of the type it returns.
    // The table is put back as it was when the outermost one goes, and one
    // made while another lives changes nothing, so a test's own stand-ins,
    // put in after the outermost, stay.
    class TestLibrary
    {
    public:
        TestLibrary();
        ~TestLibrary();
        TestLibrary(const TestLibrary&)            = delete;
        TestLibrary& operator=(const TestLibrary&) = delete;

    private:
        std::vector<decltype(LLScriptLibraryFunction::mExecFunc)> mWere;
    };

    // A library function, by its name; null where none has it.
    LLScriptLibraryFunction* libraryFunction(const char* name);
}
