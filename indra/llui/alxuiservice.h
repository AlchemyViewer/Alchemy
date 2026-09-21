/**
 * @file alxuiservice.h
 * @brief What an editor of a XUI file can be told as it is typed: what could go here, what this is, whether it parses.
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

#include "alcodeeditor.h"
#include "altextdocument.h"

#include <string>
#include <string_view>
#include <vector>

// The XUI language service, over the schema: the answers a code editor
// asks for while a XUI file is typed, worked out from the text before the
// caret rather than from a tree, since a file being typed is seldom one.
// Tags after a `<` -- the ones the enclosing tag takes as children, its
// parameter elements among them, or every tag at the top -- and the
// enclosing tag's name after a `</`; the attributes a tag takes and does
// not carry yet, inside the tag; and an enumeration's names, or true and
// false, inside an attribute's quotes. What a tag or an attribute under
// the caret is, from the schema's notes. And whether the text parses,
// with where it stops.
class ALXUIService
{
public:
    // Where a position is, as far as the text before it tells.
    struct Context
    {
        enum class Where : U8
        {
            Text,           // between elements
            TagName,        // `<na|`
            ClosingTag,     // `</na|`
            AttributeName,  // `<tag a|` or `<tag attr="x" |`
            AttributeValue, // `<tag attr="va|`
            Comment,
            Other           // a declaration, an instruction, CDATA
        };
        Where                    where = Where::Text;
        // The tag the position is in or being typed, and the attribute
        // whose value is being typed.
        std::string              tag;
        std::string              attribute;
        // The attributes the tag carries already, in order.
        std::vector<std::string> present;
        // The elements open around the position, outermost first; the
        // innermost is the parent of a tag being typed.
        std::vector<std::string> parents;
        // Where the word being typed starts.
        ALTextPos                wordStart;
    };
    static Context contextAt(const ALTextDocument& doc, const ALTextPos& at);

    // What could go at a position, given the identifier typed so far;
    // added to `out`, narrowed to the prefix.
    static void complete(const ALTextDocument& doc, const ALTextPos& at, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out);

    // What is under a position: a tag by its note, an attribute by its
    // type, its default, its alias and whether it is deprecated. Empty
    // where the schema has nothing to say.
    static std::string hover(const ALTextDocument& doc, const ALTextPos& at);

    // Whether the text parses as XML; where it does not, where the parser
    // stopped and what it said.
    static bool parses(std::string_view text, ALTextPos& where, std::string& message);
};
