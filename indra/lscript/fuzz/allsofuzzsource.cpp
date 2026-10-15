/**
 * @file allsofuzzsource.cpp
 * @brief libFuzzer on LL's LSL compiler and VM: the input is a script's
 * source as its author wrote it
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

#include "allsofuzz.h"

#include <cstddef>
#include <cstdint>
#include <string>

// The bytes as LSL, whatever they hold: the lexer, the parser and the type
// checker take most of what a mutation makes, and what compiles is run.
// Longer than the about 256 KiB of source the grid takes is no script.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > 262144)
    {
        return -1;
    }
    ll_fuzz_lso::setUp();
    ll_fuzz_lso::compileAndRun(std::string(reinterpret_cast<const char*>(data), size));
    return 0;
}
