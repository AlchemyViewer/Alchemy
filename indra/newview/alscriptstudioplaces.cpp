/**
 * @file alscriptstudioplaces.cpp
 * @brief Script Studio's places in scripts: spans as ranges, lines of a text, names, and places read back through an expansion.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptstudioplaces.h"

namespace ALScriptPlaces
{
    ALTextRange rangeOf(const ALScriptSpan& span)
    {
        return ALTextRange(ALTextPos(span.line, span.column), ALTextPos(span.endLine, span.endColumn));
    }

    // A line of a text, as it is, for a row of a pane.
    std::string lineOf(const std::string& text, S32 line)
    {
        size_t begin = 0;
        for (S32 l = 0; l < line && begin != std::string::npos; ++l)
        {
            begin = text.find('\n', begin);
            if (begin != std::string::npos)
            {
                ++begin;
            }
        }
        if (begin == std::string::npos)
        {
            return std::string();
        }
        size_t end = text.find('\n', begin);
        if (end != std::string::npos && end > begin && text[end - 1] == '\r')
        {
            --end;
        }
        return text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    }

    std::string lineOf(const ALTextDocument& text, S32 line)
    {
        if (line < 0 || line >= text.lineCount())
        {
            return std::string();
        }
        return text.line(line);
    }

    // A name as both languages spell one: a letter or an underscore, then
    // letters, digits and underscores.
    bool isIdentifier(const std::string& text)
    {
        if (text.empty() || (!isalpha(static_cast<unsigned char>(text[0])) && text[0] != '_'))
        {
            return false;
        }
        for (const char c : text)
        {
            if (!isalnum(static_cast<unsigned char>(c)) && c != '_')
            {
                return false;
            }
        }
        return true;
    }

    S32 mapSpan(const ALSourceMap& map, ALScriptSpan& span)
    {
        const ALSourceMap::Loc begin = map.toSource(span.line, span.column);
        if (!begin.found())
        {
            return -1;
        }
        const ALSourceMap::Loc end = map.toSource(span.endLine, span.endColumn);
        span.line                  = begin.line;
        span.column                = begin.column;
        if (end.found() && end.file == begin.file && (end.line > begin.line || (end.line == begin.line && end.column > begin.column)))
        {
            span.endLine   = end.line;
            span.endColumn = end.column;
        }
        else
        {
            span.endLine   = begin.line;
            span.endColumn = begin.column;
        }
        return begin.file;
    }

    void placeText(ALScriptStudioDoc::Place& place, const std::string& line)
    {
        // Trimmed for the row, and the name's place moved with the trimming.
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos)
        {
            place.text.clear();
            place.at = -1;
            return;
        }
        const size_t last = line.find_last_not_of(" \t\r");
        place.text        = line.substr(first, last - first + 1);
        const S32 at      = place.span.column - static_cast<S32>(first);
        place.at          = at >= 0 && at < static_cast<S32>(place.text.size()) ? at : -1;
    }
}
