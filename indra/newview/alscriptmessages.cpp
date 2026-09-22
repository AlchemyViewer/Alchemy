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

#include "llstring.h"

#include <boost/regex.hpp>

#include <cstdlib>

namespace
{
    // What the compilers say about where a problem is. Luau's names the
    // chunk and a one-based line; LSL's gives a zero-based line and column
    // as the viewer scrolls to them.
    const boost::regex LUAU_LOCATION(R"(^([^:]*):([0-9]+):\s*(.*)$)");
    const boost::regex LSL_LOCATION(R"(\((\d+), (\d+)\) : ([^:]+) : (.+))");
    const boost::regex DEFAULT_STATE(R"(\s*default\s*\{)");
    // A frame of a Luau traceback: the chunk, bare -- a script's name,
    // spaces and all -- or as Lua quotes one, and its one-based line, then
    // nothing, the function, or a colon and where -- but not the words of
    // an error, `chunk:12: attempt to`, which is the error's own line.
    const boost::regex STACK_FRAME(R"re(^\s*(?:\[string "([^"]*)"\]|([^:\[\]"]*[^:\[\]"\s])):([0-9]+)(?:$|\s+\S.*$|:\s+in\s.*$))re");

    // How a script's run-time error starts: the object, the script, and
    // the words.
    const boost::regex RUNTIME_ERROR_HEADER(R"(^(.+?)\s+\[script:([^\]]+)\]\s+Script run-time error)");
    const char* const  RUNTIME_ERROR_MARKER = "Script run-time error";
}

namespace ALScriptMessages
{
    Place readDiagnostic(const std::string& line_in, bool lua)
    {
        std::string line = line_in;
        LLStringUtil::stripNonprintable(line);
        Place         place;
        boost::smatch found;
        if (lua && boost::regex_match(line, found, LUAU_LOCATION))
        {
            place.line    = llmax(0, std::atoi(found[2].str().c_str()) - 1);
            place.level   = "ERROR";
            place.message = found[3].str();
        }
        else if (!lua && boost::regex_search(line, found, LSL_LOCATION))
        {
            place.line      = std::atoi(found[1].str().c_str());
            place.column    = std::atoi(found[2].str().c_str());
            place.hasColumn = true;
            place.level     = found[3].str();
            place.message   = found[4].str();
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
        return !boost::regex_search(content.begin(), content.end(), DEFAULT_STATE);
    }

    bool readRuntimeHeader(const std::string& line, Header& out)
    {
        boost::smatch match;
        if (!boost::regex_match(line, match, RUNTIME_ERROR_HEADER))
        {
            return false;
        }
        out.object = match[1].str();
        out.script = match[2].str();
        return true;
    }

    bool endsRuntimeError(const std::string& line)
    {
        const size_t n = strlen(RUNTIME_ERROR_MARKER);
        return line.size() >= n && line.compare(line.size() - n, n, RUNTIME_ERROR_MARKER) == 0;
    }

    bool readStackFrame(const std::string& line, Frame& out)
    {
        boost::smatch match;
        if (!boost::regex_match(line, match, STACK_FRAME))
        {
            return false;
        }
        out.chunk = match[1].matched ? match[1].str() : match[2].str();
        out.line  = static_cast<S32>(std::strtol(match[3].str().c_str(), nullptr, 10)) - 1;
        return out.line >= 0;
    }

    bool readRuntimeLocation(const std::vector<std::string>& lines, bool lua, Location& out)
    {
        for (const std::string& line : lines)
        {
            boost::smatch match;
            if (lua && boost::regex_match(line, match, LUAU_LOCATION))
            {
                out.line    = static_cast<S32>(std::strtol(match[2].str().c_str(), nullptr, 10)) - 1;
                out.column  = -1;
                out.message = match[3].str();
                return true;
            }
            if (!lua && boost::regex_match(line, match, LSL_LOCATION))
            {
                out.line    = static_cast<S32>(std::strtol(match[1].str().c_str(), nullptr, 10));
                out.column  = static_cast<S32>(std::strtol(match[2].str().c_str(), nullptr, 10));
                out.message = match[4].str();
                return true;
            }
        }
        return false;
    }
}
