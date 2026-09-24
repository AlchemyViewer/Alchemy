/**
 * @file alscriptmessages.cpp
 * @brief What the compilers and the running scripts say, read.
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

#include "linden_common.h"

#include "alscriptmessages.h"

#include "alregex.h"
#include "llstring.h"

#include <cstdlib>

namespace
{
    // What the compilers say about where a problem is. Luau's names the
    // chunk and a one-based line; LSL's gives a zero-based line and column
    // as the viewer scrolls to them.
    const ALRegex LUAU_LOCATION(R"(^([^:]*):([0-9]+):\s*(.*)$)");
    const ALRegex LSL_LOCATION(R"(\((\d+), (\d+)\) : ([^:]+) : (.+))");
    const ALRegex DEFAULT_STATE(R"(\s*default\s*\{)");
    // A frame of a Luau traceback: the chunk, bare -- a script's name,
    // spaces and all -- or as Lua quotes one, and its one-based line, then
    // nothing, the function, or a colon and where -- but not the words of
    // an error, `chunk:12: attempt to`, which is the error's own line.
    const ALRegex STACK_FRAME(R"re(^\s*(?:\[string "([^"]*)"\]|([^:\[\]"]*[^:\[\]"\s])):([0-9]+)(?:$|\s+\S.*$|:\s+in\s.*$))re");

    // How a script's run-time error starts: the object, the script, and
    // the words.
    const ALRegex RUNTIME_ERROR_HEADER(R"(^(.+?)\s+\[script:([^\]]+)\]\s+Script run-time error)");
    const char* const RUNTIME_ERROR_MARKER = "Script run-time error";
}

namespace ALScriptMessages
{
    Place readDiagnostic(const std::string& line_in, bool lua)
    {
        std::string line = line_in;
        LLStringUtil::stripNonprintable(line);
        Place        place;
        ALRegexMatch found;
        if (lua && LUAU_LOCATION.match(line, &found))
        {
            place.line    = llmax(0, std::atoi(found.str(2).c_str()) - 1);
            place.level   = "ERROR";
            place.message = found.str(3);
        }
        else if (!lua && LSL_LOCATION.search(line, &found))
        {
            place.line      = std::atoi(found.str(1).c_str());
            place.column    = std::atoi(found.str(2).c_str());
            place.hasColumn = true;
            place.level     = found.str(3);
            place.message   = found.str(4);
        }
        else
        {
            place.level   = "ERROR";
            place.message = line;
        }
        return place;
    }

    std::vector<Place> readDiagnostics(const LLSD& errors, bool lua)
    {
        std::vector<Place> out;
        for (LLSD::array_const_iterator it = errors.beginArray(); it != errors.endArray(); ++it)
        {
            out.push_back(readDiagnostic(it->asString(), lua));
        }
        return out;
    }

    bool looksLikeLua(std::string_view content)
    {
        return !DEFAULT_STATE.search(content);
    }

    bool readRuntimeHeader(const std::string& line, Header& out)
    {
        ALRegexMatch match;
        if (!RUNTIME_ERROR_HEADER.match(line, &match))
        {
            return false;
        }
        out.object = match.str(1);
        out.script = match.str(2);
        return true;
    }

    bool endsRuntimeError(const std::string& line)
    {
        const size_t n = strlen(RUNTIME_ERROR_MARKER);
        return line.size() >= n && line.compare(line.size() - n, n, RUNTIME_ERROR_MARKER) == 0;
    }

    bool readStackFrame(const std::string& line, Frame& out)
    {
        ALRegexMatch match;
        if (!STACK_FRAME.match(line, &match))
        {
            return false;
        }
        out.chunk = match.matched(1) ? match.str(1) : match.str(2);
        out.line  = static_cast<S32>(std::strtol(match.str(3).c_str(), nullptr, 10)) - 1;
        return out.line >= 0;
    }

    bool readRuntimeLocation(const std::vector<std::string>& lines, bool lua, Location& out)
    {
        for (const std::string& line : lines)
        {
            ALRegexMatch match;
            if (lua && LUAU_LOCATION.match(line, &match))
            {
                out.line    = static_cast<S32>(std::strtol(match.str(2).c_str(), nullptr, 10)) - 1;
                out.column  = -1;
                out.message = match.str(3);
                return true;
            }
            if (!lua && LSL_LOCATION.match(line, &match))
            {
                out.line    = static_cast<S32>(std::strtol(match.str(1).c_str(), nullptr, 10));
                out.column  = static_cast<S32>(std::strtol(match.str(2).c_str(), nullptr, 10));
                out.message = match.str(4);
                return true;
            }
        }
        return false;
    }
}
