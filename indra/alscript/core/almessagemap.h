/**
 * @file almessagemap.h
 * @brief An engine's message taken apart again, for another language to say.
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

#include <string>
#include <string_view>
#include <vector>

// A message an engine wrote from a template it keeps, taken apart again:
// the template's literal stretches found in the message in order, and
// what stands between them the words the message is about -- so that the
// studio may say it in another language with the words put back in. For
// Tailslide's messages by their error code, for Luau's lints by their
// name, and for Luau's commonest type errors by their shape; anything
// else stays as the engine said it.
class ALMessageMap
{
public:
    struct Match
    {
        std::string              key;
        std::vector<std::string> args;
    };

    // A template's literal stretches around [1], [2] ... marks matched
    // against a message: the first must begin it, the last must end it,
    // the rest follow in order; what stands where a mark is becomes that
    // mark's word. A mark used twice must stand for the same word.
    static bool match(std::string_view text, std::string_view message, std::vector<std::string>& args);

    // Tailslide's message by its error code.
    static bool lsl(int code, std::string_view message, Match& out);
    // A Luau lint by its name.
    static bool luauLint(std::string_view name, std::string_view message, Match& out);
    // A Luau type error by its words.
    static bool luauError(std::string_view message, Match& out);
};
