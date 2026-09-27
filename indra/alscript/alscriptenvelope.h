/**
 * @file alscriptenvelope.h
 * @brief The preprocessor's envelope: the source kept in a comment ahead of what it expanded to.
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

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// A script saved with Firestorm's preprocessor on is uploaded as the
// author's source in a comment block, then a few lines saying which
// preprocessor, which viewer and when, then the compile target, then the
// expanded code the server compiled. Byte for byte:
//
//   //start_unprocessed_text
//   /*<the source, every / or * before a /, * or | given a | after it>*/
//   //end_unprocessed_text
//   //nfo_preprocessor_version 0
//   //program_version <viewer>
//   //last_compiled <date>
//   //mono                          (or //lsl2)
//   <the expanded code>
//
// SLua gets the same in its own comment syntax: `--` lines, and the source
// in a long comment at a bracket level the source does not contain, so
// nothing needs escaping. `--luau` is its target.
//
// Reading one is what the studio does to show a script the way its author
// saw it; writing one is what it does so that Firestorm opens what we
// save. Markers and escaping are Firestorm's, kept exactly, because a
// script goes back and forth between the two viewers.
struct ALScriptEnvelope
{
    // The most a script's text may be as it is sent, in bytes: the whole
    // asset, the envelope and both its halves where there is one. The
    // simulator refuses what is longer.
    static constexpr size_t MAX_ASSET_BYTES = 262144;

    std::string source;
    std::string expanded;
    // "mono", "lsl2" or "luau", or nothing where the envelope said none.
    std::string compileTarget;
    std::string programVersion;
    std::string lastCompiled;
    bool        lua = false;

    // The envelope in an asset, or nothing where the asset has none: it
    // is then plain source. The comment block must be the very first
    // thing in the asset, as the preprocessor writes it.
    static std::optional<ALScriptEnvelope> parse(std::string_view asset);
    static bool                            looksWrapped(std::string_view asset);

    // The asset this envelope makes.
    std::string wrap() const;
    // The line of that asset, from zero, its expanded code begins on: the
    // envelope's own lines come first, and the region counts them in every
    // line it names -- a compile error's, a runtime error's.
    int         codeLine() const;

    // The escaping of the LSL comment block, both ways.
    static std::string encodeSource(std::string_view source);
    static std::string decodeSource(std::string_view encoded);

    // The compile target a text asks for with a `//mono`, `//lsl2` or
    // `//luau` line of its own (`--luau` in Lua), or nothing.
    static std::string directiveOf(std::string_view text, bool lua);

    // Whether the compiled half could have come of the source as it
    // expands now: the same tokens, spacing and comments aside -- in LSL,
    // with whole globals, functions or states of the expansion left out,
    // as an optimizer leaves out what is not used, and every other one
    // the same to the token. False where it could not: changed outside
    // the preprocessor, or what the source includes has changed since.
    static bool compiledFrom(std::string_view expansion, std::string_view compiled, bool lua);
    // Whether that can be told at all. Where this viewer compiled it, it
    // can. Where another did -- Firestorm -- only where the expansion used
    // none of the transforms whose made names differ from one
    // preprocessor to another: switch, lazy lists, the extensions. And
    // never where the source asks for what differs from one expansion to
    // the next: the date, the time, who compiled it, the asset, the file.
    static bool comparable(std::string_view source, bool lua, bool compiled_here, bool transformed);
};
