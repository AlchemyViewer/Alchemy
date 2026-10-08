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

#include "allinebreaks.h"
#include "alscriptenvelope.h"
#include "alscriptlexicon.h"

#include <algorithm>

namespace ALScriptPlaces
{
    ALTextRange rangeOf(const ALScriptSpan& span)
    {
        return ALTextRange(ALTextPos(span.line, span.column), ALTextPos(span.endLine, span.endColumn));
    }

    Lines::Lines(std::shared_ptr<const std::string> held) : mHeld(std::move(held))
    {
        if (mHeld)
        {
            mLines = ALLineBreaks::views(*mHeld);
        }
    }

    Lines::Lines(const std::string& text) : mLines(ALLineBreaks::views(text)) {}

    bool Lines::has(S32 line) const
    {
        return line >= 0 && (mOpen ? line < mOpen->lineCount() : static_cast<size_t>(line) < mLines.size());
    }

    std::string Lines::line(S32 line) const
    {
        if (!has(line))
        {
            return std::string();
        }
        if (mOpen)
        {
            return mOpen->line(line);
        }
        return std::string(mLines[static_cast<size_t>(line)]);
    }

    bool holds(const ALScriptSpan& span, const ALTextPos& pos)
    {
        const ALTextRange range = rangeOf(span);
        return range.begin <= pos && pos <= range.end;
    }

    bool within(const ALScriptSpan& inner, const ALScriptSpan& outer)
    {
        const ALTextRange range = rangeOf(inner);
        return holds(outer, range.begin) && holds(outer, range.end);
    }

    std::vector<size_t> pathAt(const std::vector<ALScriptOutlineEntry>& outline, const ALTextPos& at)
    {
        std::vector<size_t> path;
        size_t              parent = NONE;
        for (S32 depth = 0;; ++depth)
        {
            size_t found = NONE;
            for (size_t i = 0; i < outline.size(); ++i)
            {
                const ALScriptOutlineEntry& entry = outline[i];
                if (entry.depth == depth && holds(entry.span, at) && (parent == NONE || within(entry.span, outline[parent].span)))
                {
                    found = i;
                }
            }
            if (found == NONE)
            {
                return path;
            }
            path.push_back(found);
            parent = found;
        }
    }

    // A name as both languages spell one.
    bool isIdentifier(const std::string& text)
    {
        return ALScriptLexicon::isName(text);
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

    bool mapModuleSpan(const std::vector<std::pair<std::string, ALSourceMap>>& maps, const std::string& key, ALScriptSpan& span,
                       std::string& path, std::string& name)
    {
        const auto own = std::find_if(maps.begin(), maps.end(), [&key](const auto& module) { return module.first == key; });
        if (own == maps.end())
        {
            return false;
        }
        ALScriptSpan mapped = span;
        const S32    file   = mapSpan(own->second, mapped);
        if (file < 0)
        {
            return false;
        }
        span = mapped;
        path = own->second.files()[file].path;
        name = own->second.files()[file].name;
        return true;
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

    // What a loaded script's author wrote: the source out of the
    // envelope where one wrapped it, the text as it came otherwise, and
    // nothing where it could not be read.
    std::string sourceOf(const ALScriptLoaded& loaded)
    {
        if (!loaded.error.empty() || loaded.notecard)
        {
            return std::string();
        }
        std::string text = loaded.text;
        if (std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(text))
        {
            text = envelope->source;
        }
        return text;
    }

    std::string outlineValue(const ALScriptStudioDoc& doc, size_t index)
    {
        return std::to_string(index) + '\n' + doc.outline[index].name;
    }

    size_t outlineEntryOf(const ALScriptStudioDoc& doc, const std::string& value)
    {
        // Where it was, if what is there now has its name; else the first of
        // its name; else nothing.
        const size_t      cut   = value.find('\n');
        const std::string name  = cut == std::string::npos ? std::string() : value.substr(cut + 1);
        const size_t      index = static_cast<size_t>(atoi(value.c_str()));
        if (index < doc.outline.size() && doc.outline[index].name == name)
        {
            return index;
        }
        for (size_t i = 0; i < doc.outline.size(); ++i)
        {
            if (doc.outline[i].name == name)
            {
                return i;
            }
        }
        return NONE;
    }

    LLSD Declared::value() const
    {
        LLSD out;
        out["line"]   = line;
        out["column"] = column;
        out["path"]   = path;
        out["name"]   = name;
        return out;
    }

    Declared declaredOf(const ALScriptStudioDoc& doc, const ALScriptAnalysis::Result& result, bool preprocessed)
    {
        // Where the analyzer read it declared: in the expansion, where the
        // preprocessor ran, and so back to the source -- this script's, or an
        // include's.
        Declared declared;
        if (!result.hover.found || !result.hover.hasDefinition)
        {
            return declared;
        }
        declared.line   = result.hover.definitionLine;
        declared.column = result.hover.definitionColumn;
        if (!result.hover.definitionFile.empty())
        {
            // In a module the script requires, read apart: through the
            // module's own map, to the file it came of.
            ALScriptSpan span;
            span.line = span.endLine = declared.line;
            span.column = span.endColumn = declared.column;
            if (!preprocessed || !mapModuleSpan(doc.expanded.moduleMaps, result.hover.definitionFile, span, declared.path, declared.name))
            {
                return Declared();
            }
            declared.line   = span.line;
            declared.column = span.column;
            return declared;
        }
        if (preprocessed)
        {
            const ALSourceMap::Loc loc = doc.expanded.map.toSource(declared.line, declared.column);
            declared.line              = loc.found() ? loc.line : -1;
            declared.column            = loc.found() ? loc.column : -1;
            if (loc.found() && loc.file > 0)
            {
                declared.path = doc.expanded.map.files()[loc.file].path;
                declared.name = doc.expanded.map.files()[loc.file].name;
            }
        }
        return declared;
    }
}
