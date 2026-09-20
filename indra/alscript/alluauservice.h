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

#include "alscriptproblem.h"

#include <memory>
#include <string>
#include <string_view>

// The SLua analyzer: Luau's front end from Second Life's fork, given the
// grid's definitions and asked about one script at a time. Phase 0 of
// doc/SCRIPT_STUDIO.md: it loads the definitions and checks a script.
// Completion, hover and the rest come with the language service.
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

    // Everything the front end has to say about one script: parse errors
    // and type errors, then the lints, each in the order it was found.
    ALScriptProblems check(std::string_view source);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
