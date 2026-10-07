/**
 * @file altextgotoline.h
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

#pragma once

#include "llstring.h"
#include "stdtypes.h"

#include <functional>
#include <string>

class ALQuickOpen;
class ALTextView;
struct ALTextPos;

// Go to Line over a text: a line, or a line and a column, typed into a
// quick open by the numbers the text shows, the text going there as it is
// typed. What a studio's Go to Line is, and the notecard window's.
namespace ALTextGoToLine
{
    // The text, while it is there: null once the window or the tab it is
    // in has gone.
    typedef std::function<ALTextView*()> text_t;
    // A word it says, by name, with [LINE], [COL], [FIRST] and [COUNT]:
    // GoToLineHint while nothing is typed; GoToLineGo and GoToLineGoColumn
    // where what is typed is a place of the text, GoToLineNone where not.
    typedef std::function<std::string(const std::string& name, const LLStringUtil::format_map_t& args)> words_t;
    // Gone somewhere from `was`, where the caret stood as it was asked:
    // by Return, or looked away from with the text somewhere else.
    typedef std::function<void(const ALTextPos& was)> went_t;
    // The quick open shown, with what answers it -- Return, Escape, a
    // look away -- and given back, or null where it could not be shown:
    // through the window's ALQuickAsk, as its other quick opens are.
    typedef std::function<ALQuickOpen*(std::function<void(const std::string&)> chose, std::function<void()> escaped,
                                       std::function<void()> left)>
        ask_t;

    // Asked: the text at the place typed while it is typed, and back
    // where it was while nothing is, or where Escape is pressed; Return
    // keeps it and gives the text the keyboard, and so does a look away.
    // By the numbers the text shows: `base` added to a line counted from
    // one (ALTextView::lineNumberBase).
    void ask(const ask_t& ask, text_t text, S32 base, words_t words, went_t went = {});

    // "12" or "12:5", as a person types a place: the line and the column
    // from one, zero where there is none or it is not a number -- nor one
    // where it is longer than a number holds. False where no line's number
    // is typed at all.
    bool placeTyped(const std::string& text, S32& line, S32& column);
}
