/**
 * @file alscriptweight.h
 * @brief What a script weighs for a target: the code its compiler makes, by part and by line.
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

#pragma once

#include "stdtypes.h"

#include <string>
#include <string_view>
#include <vector>

// What a script weighs for a target: the code the target's compiler makes of
// it, in bytes, against the memory the target runs it in -- 16 KB under LSO,
// 64 KB under Mono, 128 KB for SLua and for LSL on Luau -- and which parts
// of it weigh what. What is weighed is the text given: what would be
// uploaded, expanded and optimised, which the caller maps back to what the
// author wrote. Places are zero-based, as everything in the library's are.
//
// This is the code, what can be known before a script runs; what it
// allocates running is not, and is often what stops it.
struct ALScriptWeight
{
    enum class Target : U8
    {
        SLua,
        LSO,
        Mono,
        LSLLuau
    };
    Target      target   = Target::SLua;
    // Whether the compiler made anything of it: false, with its words,
    // where the script does not compile for the target.
    bool        compiled = false;
    std::string error;
    // The bytes the target counts against its limit: for SLua, the
    // bytecode the server charges; for LSO, the image up to the top of its
    // heap, which the stack shares the rest of the 16 KB with.
    size_t      total    = 0;
    size_t      limit    = 0;
    // Whether the total is a measure of the target's own or an estimate of
    // it: Mono counts what the runtime makes of the IL.
    bool        estimate = false;

    struct Part
    {
        enum class Kind : U8
        {
            Function,
            Handler,
            State,
            Global,
            Constant,
            // What the target spends on the script as a whole: a header, a
            // table, the registers.
            Frame
        };
        Kind        kind = Kind::Function;
        std::string name;
        // What holds it: a handler's state.
        std::string within;
        size_t      bytes     = 0;
        // Where it is in the text weighed; -1 where it is nowhere in it.
        S32         line      = -1;
        S32         column    = -1;
        S32         endLine   = -1;
        S32         endColumn = -1;
    };
    // In the order the compiler made them. A handler's bytes are in its
    // state's as well, so the parts do not add up to the total.
    std::vector<Part> parts;

    // The bytes of code each line made, where the compiler says which
    // instruction came of which line: exact for the Luau targets.
    struct Line
    {
        S32    line  = 0;
        size_t bytes = 0;
    };
    std::vector<Line> lines;

    static size_t      limitOf(Target target);
    static const char* nameOf(Target target);
};

namespace ALScriptWeigh
{
    // The compile options the server is taken to use for SLua: Luau's
    // defaults, optimisation level 1 and line information, until Linden Lab
    // says otherwise. Named here so that there is one place to change.
    constexpr int SLUA_OPTIMIZATION_LEVEL = 1;
    constexpr int SLUA_DEBUG_LEVEL        = 1;

    // SLua compiled as the server compiles it, and the bytecode read back
    // for what each function, and each line, comes to.
    ALScriptWeight slua(std::string_view source);
    // LSL compiled for LSO by Tailslide: the 16 KB image, a function, a
    // state and a handler at a time. Needs the builtins loaded (ALLSLService)
    // and holds the engine lock while it works.
    ALScriptWeight lso(std::string_view source);

    // LSL compiled for Luau's VM by the SLua fork's own compiler for it, as
    // the server compiles such an asset, and its bytecode read back as
    // SLua's is. As lso() for the builtins and the lock: the compiler parses
    // with Tailslide's table, numbered as the runtime numbers its events.
    ALScriptWeight lslLuau(std::string_view source);

    // What a Mono assembly is taken to cost before a line of the script is
    // in it -- the class, its references, the headers -- which the
    // region's own numbers for an empty script are to calibrate.
    constexpr size_t MONO_BASE_BYTES = 1536;
    // LSL compiled for Mono by Tailslide, and the IL it makes sized by the
    // opcode table with the metadata it declares -- methods, fields,
    // strings, what it calls -- over the base: an estimate, since what Mono
    // counts against 64 KB is the runtime's business rather than the IL's.
    // As lso() for the builtins and the lock.
    ALScriptWeight mono(std::string_view source);
}
