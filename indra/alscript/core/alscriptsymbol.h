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

#include "alscriptspan.h"
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
    // Where a call's brackets go once it is taken: as the editor guesses
    // from its kind, where the analyzer does not say; none, for a function
    // passed rather than called or one called already; after an empty
    // pair, for one that takes nothing; or between them.
    enum class Brackets : U8
    {
        Guess,
        None,
        After,
        Inside
    };
    // What the position is, as the analyzer reads it, which every one of
    // an answer shares: where an expression goes, a statement, a member
    // after its table, a type, a keyword alone, a string, a comment that
    // says how the script is checked; or nothing it could tell.
    enum class Context : U8
    {
        Unknown,
        Expression,
        Statement,
        Property,
        Type,
        Keyword,
        String,
        HotComment
    };

    std::string        text;
    // Its type, or its signature: what the list shows beside it.
    std::string        detail;
    ALScriptSymbolKind kind       = ALScriptSymbolKind::Variable;
    bool               deprecated = false;
    std::string        documentation;
    // What goes in its place where it is more than its name: a body with
    // stops to tab through, as ALSnippetSession reads one.
    std::string        snippet;
    // Of the type wanted where it goes: first among those that match what
    // was typed as well.
    bool               fits     = false;
    Brackets           brackets = Brackets::Guess;
    Context            context  = Context::Unknown;
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
    // Where it was declared, when it was declared in the script or in a
    // module it requires.
    bool        hasDefinition = false;
    S32         definitionLine   = 0;
    S32         definitionColumn = 0;
    // The module it was declared in, where it is not the script: by the
    // key a problem's file has (ALLuauService::setModules), the place in
    // the module's own lines.
    std::string definitionFile;
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
    // Every form the function has, where it has more than one: the label
    // and parameters above are the one the call's arguments fit first,
    // `overload` of them.
    struct Overload
    {
        std::string              label;
        std::vector<std::string> parameters;
    };
    std::vector<Overload> overloads;
    S32                   overload = 0;
};

// The name at a position: where the script declares it, if it does, and
// every place it stands.
struct ALScriptReferences
{
    bool               found = false;
    std::string        name;
    ALScriptSymbolKind kind = ALScriptSymbolKind::Variable;
    // Where the script, or a module it requires, declares it. A builtin,
    // or anything from the definitions, has no declaration here.
    bool               hasDefinition = false;
    ALScriptSpan       definition;
    // The module it is declared in, where it is not the script: by the key
    // a problem's file has (ALLuauService::setModules), the span in the
    // module's own lines.
    std::string        definitionFile;
    // Whether every place it stands is the script's to change: declared
    // here or in a module it requires, and not a name the language fixes,
    // as LSL's `default` is.
    bool               renamable = false;
    // Every place in the script, the declaration among them, in order;
    // each is the name alone, which is what a rename replaces.
    std::vector<ALScriptSpan> references;
    // Every place in the modules the script requires, which the analyzer
    // read apart: each module's in order, by its key, the declaration
    // among them where it is in one.
    struct Elsewhere
    {
        std::string  file;
        ALScriptSpan span;
    };
    std::vector<Elsewhere> elsewhere;
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
    // Whether the text may be written in where it stands, as it reads: a
    // type printed whole -- not cut short, not a cycle, nothing Luau made
    // up for what it could not name. Never a parameter's name, which the
    // language has no way to say.
    bool        writable = false;

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
