/**
 * @file alsmartselect.h
 * @brief What a selection grows to, step by step, as code reads it: a name, a string, a bracket's inside and the bracket, the lines, the text.
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

#include "altextdocument.h"

#include <optional>
#include <string_view>

class ALBracketIndex;
class ALSyntaxHighlighter;

// What Expand Selection grows a selection to: the smallest of these that
// holds it and is more than it -- the name it is in, and that name with
// what it is a member of (`Say`, then `ll.Say`); a string's inside, then
// the string with its quotes, or a comment; a bracket pair's inside, then
// the pair; the lines it is on, whole; the whole text. Brackets and what
// is a string or a comment are the index's and the highlighter's.
namespace ALSmartSelect
{
    std::optional<ALTextRange> grow(const ALTextDocument& doc, ALSyntaxHighlighter& highlighter, ALBracketIndex& brackets,
                                    const ALTextRange& selection, std::string_view member_separators);
}
