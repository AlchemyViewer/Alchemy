/**
 * @file alcvargenjson.h
 * @brief The little JSON the generator reads and writes: a released schema,
 *        the schema it writes for --where, and LLSD defaults.
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

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ALCVarGen
{
    struct Json
    {
        enum class Kind
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object
        };

        // A class of its own, defined after Json, rather than a std::pair:
        // the pair asks, as it is instantiated, whether Json can be default
        // constructed, and Json's constructor asks for the pair's destructor
        // before the pair is complete.
        struct Member;

        Kind                        kind    = Kind::Null;
        bool                        boolean = false;
        double                      number  = 0.0;
        std::string                 text;
        std::vector<Json>           items;
        // In the order written, for an object.
        std::vector<Member>         members;

        const Json* find(std::string_view name) const;
        std::string_view string(std::string_view name) const;
        double numberOr(std::string_view name, double fallback) const;
    };

    struct Json::Member
    {
        std::string name;
        Json        value;
    };

    // Parses `text`, nesting no deeper than `depth`; false with `error`
    // saying where it stopped.
    bool parseJson(std::string_view text, Json& out, std::string& error, int depth = 64);

    // The text quoted as a JSON string.
    std::string jsonString(std::string_view text);
}
