/**
 * @file alluauservice.h
 * @brief The SLua analyzer over Second Life's fork of Luau.
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

#include "alluauconfig.h"
#include "alscriptproblem.h"
#include "alscriptsymbol.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The SLua analyzer: Luau's front end from Second Life's fork, given the
// grid's definitions and asked about one script at a time: what is wrong
// with it, what could go at a position, what is at one, what a call there
// takes, where a name is bound and used, and what the script declares.
// The documentation comes from the docs JSON beside the definitions,
// keyed the way luau-lsp keys it.
//
// The Luau headers stay behind the implementation, so nothing that includes
// this pays for them. Not thread-safe: one of these belongs to one thread.
class ALLuauService
{
public:
    ALLuauService();
    ~ALLuauService();
    ALLuauService(const ALLuauService&) = delete;
    ALLuauService& operator=(const ALLuauService&) = delete;

    // The definitions, in luau-lsp's definition-file format, which is what
    // lsl-definitions generates as secondlife.d.luau. Replaces whatever
    // was loaded before. False, with the reason, when the file does not
    // parse or check; the globals are then Luau's own and nothing more.
    bool loadDefinitions(std::string_view source, std::string& error);
    bool hasDefinitions() const;

    // The documentation, as secondlife.docs.json has it: a map from a
    // symbol such as "@sl-slua/global/ll.Say" to its text and link.
    bool loadDocs(std::string_view json, std::string& error);
    bool hasDocs() const;

    // What the `.luaurc` governing the script says, for every question
    // asked until told otherwise: the mode a check runs in -- "strict",
    // "nonstrict" or "nocheck", else nonstrict, as the grid does, and a
    // `--!strict` comment in the script overrides either -- which lints
    // are on and which are errors, and the globals the script may use
    // without declaring. A script with no configuration is given a
    // default-constructed one.
    void setConfig(const ALLuauConfig& config);

    // Everything the front end has to say about one script: parse errors
    // and type errors, then the lints, each in the order it was found.
    ALScriptProblems check(std::string_view source);

    // What could go at a position of the script: the keywords, the
    // bindings in scope, the fields of what is being indexed.
    std::vector<ALScriptCompletion> complete(std::string_view source, S32 line, S32 column);
    // What is at a position: its name and type, its documentation, and
    // where in the script it was bound.
    ALScriptHover hover(std::string_view source, S32 line, S32 column);
    // The call a position is inside, if any.
    ALScriptSignature signature(std::string_view source, S32 line, S32 column);
    // The name at a position -- a local, a global, a field of something
    // -- with where the script binds it and every place it stands.
    ALScriptReferences references(std::string_view source, S32 line, S32 column);
    // The script's own shape: what it binds at the top and the functions
    // in it, each function's own one deeper.
    std::vector<ALScriptOutlineEntry> outline(std::string_view source);
    // Every name in the script by what the check found it to be: a
    // parameter, a local, a global, a field, a function, a type; whether
    // it is bound there, constant, from the definitions, or deprecated.
    // In order, each place once.
    std::vector<ALScriptSemanticToken> semanticTokens(std::string_view source);
    // What the editor may show beside the text: each argument's
    // parameter name, where the argument is not that name already, and
    // the type a `local` was given without saying.
    std::vector<ALScriptInlayHint> inlayHints(std::string_view source, bool parameters, bool types);
    // What could be done at a place -- the caret, or the stretch from it to
    // (endLine, endColumn) where one is chosen -- that no problem asks for,
    // each as the edits that make it: the type a local was given written
    // in; the stretch into a local of its own; a concatenation as an
    // interpolated string; an `if` with an `else` the other way round; a
    // handler for an event the script asks for and does not hear.
    std::vector<ALScriptFix> actions(std::string_view source, S32 line, S32 column, S32 endLine, S32 endColumn);

    // How many times a script has been type checked: a question asked
    // again of the same text, with the same configuration, is answered
    // from the check already made. For the test that says so.
    size_t typeChecks() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
