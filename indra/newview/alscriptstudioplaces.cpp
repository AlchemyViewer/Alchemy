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

#include "alscriptenvelope.h"

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

    Lines::Lines(std::shared_ptr<const std::string> held) : mHeld(std::move(held)), mText(mHeld.get())
    {
        index();
    }

    Lines::Lines(const std::string& text) : mText(&text)
    {
        index();
    }

    void Lines::index()
    {
        if (!mText)
        {
            return;
        }
        mStarts.push_back(0);
        for (size_t at = mText->find('\n'); at != std::string::npos; at = mText->find('\n', at + 1))
        {
            mStarts.push_back(at + 1);
        }
    }

    bool Lines::has(S32 line) const
    {
        return line >= 0 && (mOpen ? line < mOpen->lineCount() : static_cast<size_t>(line) < mStarts.size());
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
        const size_t begin = mStarts[static_cast<size_t>(line)];
        size_t       end   = static_cast<size_t>(line) + 1 < mStarts.size() ? mStarts[static_cast<size_t>(line) + 1] - 1 : mText->size();
        if (end > begin && (*mText)[end - 1] == '\r')
        {
            --end;
        }
        return mText->substr(begin, end - begin);
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

    // What a loaded script's author wrote: the source out of the
    // envelope where one wrapped it, the text as it came otherwise, and
    // nothing where it could not be read.
    std::string sourceOf(const ALScriptWorkspace::Loaded& loaded)
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
