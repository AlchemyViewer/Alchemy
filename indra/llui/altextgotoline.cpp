/**
 * @file altextgotoline.cpp
 * @brief Go to Line over a text: a line, or a line and a column, as it is typed
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

#include "altextgotoline.h"

#include "alquickopen.h"
#include "altextview.h"

#include <cctype>
#include <cstdlib>
#include <string_view>

namespace
{
    // What is typed, as a line of the text counted from one, and a
    // column: false where it is no line of it.
    bool placeOf(const ALTextView& text, S32 base, const std::string& typed, S32& line, S32& column)
    {
        ALTextGoToLine::placeTyped(typed, line, column);
        line -= base;
        return line >= 1 && line <= text.document().lineCount();
    }

    ALTextPos posOf(const ALTextView& text, S32 line, S32 column)
    {
        return column > 0 ? text.document().posAtDisplayColumn(line - 1, column - 1, text.getTabWidth()) : ALTextPos(line - 1, 0);
    }
}

void ALTextGoToLine::ask(const ask_t& ask, text_t text_of, S32 base, words_t words, went_t went)
{
    ALTextView* shown = text_of ? text_of() : nullptr;
    if (!shown || !ask)
    {
        return;
    }
    const ALTextPos was = shown->caret();
    // The text at the place typed, while it is typed; return leaves it
    // there, and so does looking away, escape puts it back.
    ALQuickOpen* quick = ask(
        [text_of, base, was, went](const std::string& typed) {
            ALTextView* text = text_of();
            if (!text)
            {
                return;
            }
            S32 line, column;
            if (placeOf(*text, base, typed, line, column))
            {
                // Gone from where the caret was before the line was typed,
                // which the preview has moved it from since.
                if (went)
                {
                    went(was);
                }
                text->goTo(posOf(*text, line, column));
            }
            else
            {
                text->goTo(was);
            }
            text->setFocus(true);
        },
        [text_of, was]() {
            if (ALTextView* text = text_of())
            {
                text->goTo(was);
            }
        },
        // Looked away from: the line it went to stands, since that is
        // what was looked at, and the way back from it is kept, as a
        // line gone to by Return keeps it.
        [text_of, was, went]() {
            ALTextView* text = text_of();
            if (text && went && text->caret() != was)
            {
                went(was);
            }
        });
    if (!quick)
    {
        return;
    }
    quick->onQueryChanged([text_of, base, words, quick, was](const std::string& typed) {
        ALTextView* text = text_of();
        if (!text || !words)
        {
            return;
        }
        S32                        line, column;
        const bool                 there = placeOf(*text, base, typed, line, column);
        LLStringUtil::format_map_t args;
        // In the numbers the text shows.
        args["[COUNT]"] = std::to_string(text->document().lineCount() + base);
        args["[LINE]"]  = std::to_string(line + base);
        args["[COL]"]   = std::to_string(column);
        std::string trimmed = typed;
        LLStringUtil::trim(trimmed);
        if (trimmed.empty())
        {
            quick->setHint(words("GoToLineHint", args));
            text->goTo(was);
        }
        else if (there)
        {
            quick->setHint(words(column > 0 ? "GoToLineGoColumn" : "GoToLineGo", args));
            text->goTo(posOf(*text, line, column));
        }
        else
        {
            quick->setHint(words("GoToLineNone", args));
        }
    });
    quick->setQuery(std::string());
}

void ALTextGoToLine::placeTyped(const std::string& text, S32& line, S32& column)
{
    line = column = 0;
    std::string_view rest(text);
    while (!rest.empty() && rest.front() == ' ')
    {
        rest.remove_prefix(1);
    }
    while (!rest.empty() && rest.front() == ':')
    {
        rest.remove_prefix(1);
    }
    size_t digits = 0;
    while (digits < rest.size() && isdigit(static_cast<unsigned char>(rest[digits])))
    {
        ++digits;
    }
    if (digits == 0)
    {
        return;
    }
    line = static_cast<S32>(std::strtol(std::string(rest.substr(0, digits)).c_str(), nullptr, 10));
    rest.remove_prefix(digits);
    if (rest.empty() || (rest.front() != ':' && rest.front() != ','))
    {
        return;
    }
    rest.remove_prefix(1);
    while (!rest.empty() && rest.front() == ' ')
    {
        rest.remove_prefix(1);
    }
    digits = 0;
    while (digits < rest.size() && isdigit(static_cast<unsigned char>(rest[digits])))
    {
        ++digits;
    }
    if (digits > 0)
    {
        column = static_cast<S32>(std::strtol(std::string(rest.substr(0, digits)).c_str(), nullptr, 10));
    }
}
