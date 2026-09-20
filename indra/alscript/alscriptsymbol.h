/**
 * @file alscriptsymbol.h
 * @brief What the analyzers say about a place in a script: what could go there, what is there, what a call takes.
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
// analyzers answer is (doc/SCRIPT_STUDIO.md section 3.8).

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
