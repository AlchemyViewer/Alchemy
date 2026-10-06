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

class ALSourceMap;

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
        // What the script calls it; for what the target spends on the
        // script as a whole, which the script has no name for, a word to
        // say it by -- "registers", "assembly", "globals", "script",
        // "strings" -- and nothing for a function with no name.
        std::string name;
        // What holds it: a handler's state.
        std::string within;
        size_t      bytes     = 0;
        // Where it is in the text weighed; -1 where it is nowhere in it.
        S32         line      = -1;
        S32         column    = -1;
        S32         endLine   = -1;
        S32         endColumn = -1;
        // Once in the source's places (inSource), the included file it is
        // in, by the identity the map's files carry; empty for the script
        // itself.
        std::string file;
    };
    // In the order the compiler made them. A state's bytes are its own --
    // the table of its handlers -- and each handler's are its own part:
    // no byte is counted twice, so the parts come to no more than the
    // total, the rest being what the target spends that no part is.
    std::vector<Part> parts;

    // The bytes of code each line made, by line: for the Luau targets as
    // the compiler says which instruction came of which line; for LSO and
    // Mono what the compiler wrote while it stood at the line's statement
    // or expression, walking the tree -- a loop's jump back is the loop's
    // line, the return a function does not write is its closing brace's --
    // and for LSO a global's value as well, which the image holds. What the
    // target spends on a function as such, and the strings SLua keeps once
    // for the script, are nobody's line. Only lines that made something.
    struct Line
    {
        S32         line  = 0;
        size_t      bytes = 0;
        // As a part's.
        std::string file;
    };
    std::vector<Line> lines;

    // The strings a Luau target keeps once for the script, in the table at
    // the head of its bytecode, in the table's order: each one's text; what
    // it takes up there, its length and itself; how many instructions name
    // it -- a load of it, a field or a global it names, a comparison with
    // it -- and how many of those load it as a value, which alone could be
    // made of two strings; whether it is a function's name, which the line
    // information keeps and no instruction names; and the first line naming
    // it, as a part's. Empty for LSO and Mono.
    struct String
    {
        std::string text;
        size_t      bytes = 0;
        size_t      uses  = 0;
        size_t      loads = 0;
        bool        name  = false;
        S32         line  = -1;
        std::string file;
    };
    std::vector<String> strings;

    // Strings that start alike, where keeping the start once and joining it
    // back on as each is loaded would weigh less: the start, the strings
    // sharing it by their places in `strings`, and the bytes it would save
    // (ALScriptWeigh::sharedStarts). Best first; a string in one at most.
    struct SharedStart
    {
        std::string         start;
        std::vector<size_t> strings;
        size_t              saved = 0;
    };
    std::vector<SharedStart> sharedStarts;

    // The same weight in the places of the source a map came from: each
    // part where its place maps, in the file it maps to, and each line the
    // bytes of the source line it came of, lines that came of one added up.
    // What maps nowhere -- a macro's making with no line of its own -- keeps
    // its part without a place, and its line's bytes are nobody's. Each
    // string's first line likewise.
    ALScriptWeight inSource(const ALSourceMap& map) const;

    static size_t      limitOf(Target target);
    static const char* nameOf(Target target);
};

namespace ALScriptWeigh
{
    // The compile options the server uses for SLua: Luau's defaults at
    // optimisation level 1 and debug level 1 (line information), with the
    // upstream flags of slua's SLUA_REQUIRED_FFLAGS set, as
    // ALLuauService::setUpProcess sets them. Named here so that there is
    // one place to change.
    constexpr int SLUA_OPTIMIZATION_LEVEL = 1;
    constexpr int SLUA_DEBUG_LEVEL        = 1;

    // SLua compiled as the server compiles it, and the bytecode read back
    // for what each function, each line and each string comes to.
    ALScriptWeight slua(std::string_view source);

    // What keeping a start of several strings once costs, in bytes of
    // bytecode at the grid's debug level, beside what the table saves. At
    // each load of one of them: the start moved beside the rest, and a
    // CONCAT, each with its byte of line information. Once: the start's own
    // local, kept apart from folding -- `local start; start = "..."` -- since
    // Luau joins constant strings as it compiles, a local never assigned
    // again among them.
    constexpr size_t SHARED_START_PER_LOAD = 10;
    constexpr size_t SHARED_START_ONCE     = 12;
    // What saves less is not worth saying.
    constexpr size_t SHARED_START_LEAST = 8;
    // The starts among a script's strings worth keeping once: of those
    // loaded only as values -- a field's name, a global's, a comparison's,
    // a function's, are each one string whatever they share -- each start
    // two of them share, cut where no character is, with every string
    // having it; what keeping it once would save, the table's bytes before
    // less after, less the joining at each load and the local once; the
    // best first, and each string in the best start it has. No more than
    // a few dozen.
    std::vector<ALScriptWeight::SharedStart> sharedStarts(const std::vector<ALScriptWeight::String>& strings);
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
    // Any assembly's text sized as mono() sizes Tailslide's -- LL's
    // compiler's, to hold the two against each other -- each method a part
    // by the assembly's name, and no line anybody's.
    ALScriptWeight monoOfCIL(std::string_view cil);

    // What Tailslide makes of a script, as lso() and mono() have it before
    // they weigh it: the 16 KB LSO image, and the CIL text. False, with why,
    // where it makes nothing. As lso() for the builtins and the lock.
    bool tailslideLSO(std::string_view source, std::vector<U8>& image, std::string& error);
    bool tailslideCIL(std::string_view source, std::string& cil, std::string& error);
}
