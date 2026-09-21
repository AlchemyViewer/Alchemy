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
    // The name and its type or signature, as a declaration reads: `local
    // count: number`, `(parameter) n: number`, `function half(n: number):
    // number`, `integer count`.
    std::string label;
    // The type in full where the label gives a glance of it: a table's
    // fields each on a line, a function's overloads each on a line.
    // Empty where the label says it all.
    std::string typeDetail;
    // What is wanted where the position is -- the parameter an argument
    // is checked against, the annotation a value must meet -- where the
    // analyzer knows and it is not what is there.
    std::string expected;
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

// A stretch of the script coloured by what the analyzer knows it to be,
// over what the grammar could tell from the words alone: a parameter
// against a local against a global, a field, a type, a call to something
// the definitions mark deprecated.
struct ALScriptSemanticToken
{
    enum Modifier : U8
    {
        // Where the name is bound, rather than where it is used.
        Declaration = 1,
        // A name of the whole script, not of a block.
        Global      = 2,
        // From the definitions, not the script.
        Builtin     = 4,
        Deprecated  = 8,
        // Never assigned again: a constant, a `local` that is one.
        ReadOnly    = 16
    };
    ALScriptSpan       span;
    ALScriptSymbolKind kind      = ALScriptSymbolKind::Variable;
    U8                 modifiers = 0;

    friend bool operator<(const ALScriptSemanticToken& a, const ALScriptSemanticToken& b) { return a.span < b.span; }
};

// A word the editor shows beside the text without putting it in: a
// parameter's name before the argument it is given, a type after a name
// declared without one.
struct ALScriptInlayHint
{
    enum class Kind : U8
    {
        Parameter,
        Type
    };
    S32         line   = 0;
    S32         column = 0;
    Kind        kind   = Kind::Parameter;
    // As shown: `channel:` before an argument, `: number` after a name.
    std::string text;

    friend bool operator<(const ALScriptInlayHint& a, const ALScriptInlayHint& b)
    {
        return a.line != b.line ? a.line < b.line : a.column < b.column;
    }
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
