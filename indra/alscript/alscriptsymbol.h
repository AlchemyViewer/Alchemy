/**
 * @file alscriptsymbol.h
 * @brief What the analyzers say about a place in a script: what could go there, what is there, what a call takes, where a name lives.
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
#include <vector>

// Positions are zero-based lines and byte columns, as everything the
// analyzers answer is.

enum class ALScriptSymbolKind : U8
{
    Keyword,
    Variable,
    Parameter,
    Function,
    Field,
    Type,
    Constant,
    Event,
    State,
    Label,
    Module
};

// One thing that could go at a position.
struct ALScriptCompletion
{
    std::string        text;
    // Its type, or its signature: what the list shows beside it.
    std::string        detail;
    ALScriptSymbolKind kind       = ALScriptSymbolKind::Variable;
    bool               deprecated = false;
    std::string        documentation;
};

// What is at a position.
struct ALScriptHover
{
    bool        found = false;
    // The name and its type or signature, as a declaration reads.
    std::string label;
    std::string documentation;
    std::string link;
    // Where it was declared, when it was declared in the script.
    bool        hasDefinition = false;
    S32         definitionLine   = 0;
    S32         definitionColumn = 0;
};

// The call a position is inside, and which of its parameters the
// position is at.
struct ALScriptSignature
{
    bool                     found = false;
    std::string              label;
    // Each parameter as it appears in the label, in order.
    std::vector<std::string> parameters;
    // Which one the position is at, or past the end.
    S32                      active = 0;
    std::string              documentation;
};

// A stretch of the script, the end exclusive.
struct ALScriptSpan
{
    S32 line      = 0;
    S32 column    = 0;
    S32 endLine   = 0;
    S32 endColumn = 0;

    friend bool operator==(const ALScriptSpan& a, const ALScriptSpan& b)
    {
        return a.line == b.line && a.column == b.column && a.endLine == b.endLine && a.endColumn == b.endColumn;
    }
    friend bool operator<(const ALScriptSpan& a, const ALScriptSpan& b)
    {
        return a.line != b.line ? a.line < b.line : a.column < b.column;
    }
};

// The name at a position: where the script declares it, if it does, and
// every place it stands.
struct ALScriptReferences
{
    bool               found = false;
    std::string        name;
    ALScriptSymbolKind kind = ALScriptSymbolKind::Variable;
    // Where the script declares it. A builtin, or anything from the
    // definitions, has no declaration here.
    bool               hasDefinition = false;
    ALScriptSpan       definition;
    // Whether every place it stands is the script's to change: declared
    // here, and not a name the language fixes, as LSL's `default` is.
    bool               renamable = false;
    // Every place, the declaration among them, in order; each is the name
    // alone, which is what a rename replaces.
    std::vector<ALScriptSpan> references;
};

// One symbol of the script's outline: what it declares at the top, and
// what those hold, in the order written.
struct ALScriptOutlineEntry
{
    std::string        name;
    // Its type or its parameters, as a declaration reads.
    std::string        detail;
    ALScriptSymbolKind kind = ALScriptSymbolKind::Variable;
    // The name itself, and everything the symbol spans.
    ALScriptSpan       nameSpan;
    ALScriptSpan       span;
    // How far in: a state's events are one deeper than the state, and a
    // function inside a function one deeper than its holder.
    S32                depth = 0;
};
