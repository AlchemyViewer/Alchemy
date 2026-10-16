/**
 * @file alquickask.h
 * @brief The one quick open a window has up, in a popover over part of it
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

#include "alpopover.h"
#include "alquickopen.h"

#include <boost/signals2.hpp>

#include <functional>
#include <string>
#include <vector>

class LLView;

// The one quick open (ALQuickOpen) a window has up, in a popover over a
// part of it; the question it asks, and whom its answer goes to.
class ALQuickAsk
{
public:
    // Open quickly: the candidates against a few letters, centred over the
    // top of `anchor`, gone as soon as one is chosen or the person looks
    // away; as wide and as tall as given, where given. Asked the same
    // question again while it is up -- the same title and placeholder --
    // it keeps what was typed, takes the keyboard back and answers the
    // latest asking; another question puts the one up away, escaped, and
    // is asked afresh. The widget comes back for a caller with more to
    // say to it -- a hint that follows the typing -- or null where it
    // could not be shown. `escaped` is told when it goes by Escape, or
    // put away for another question -- whatever it previewed to be put
    // back -- and `left` when it goes by the person looking away with
    // nothing chosen, where what it previewed stands, the reader having
    // looked at it and moved on; and `hold`, where given, of a choice made
    // with Shift-Return -- the pick to be held rather than taken, for a
    // caller with two things to do with one.
    ALQuickOpen* ask(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                     std::function<void(const std::string&)> chose, LLView* anchor, S32 width = 0, S32 height = 0,
                     std::function<void()> escaped = {}, std::function<void(const std::string&)> hold = {},
                     std::function<void()> left = {});

private:
    // The quick open up told whom its answer goes to: the latest asking.
    void answer(ALPopover* popover, ALQuickOpen* quick, std::function<void(const std::string&)> chose, std::function<void()> escaped,
                std::function<void(const std::string&)> hold, std::function<void()> left);

    ALPopoverSlot mPopover;
    // The question the quick open up asks, and what carries its answer.
    std::string                        mQuestion;
    boost::signals2::scoped_connection mChose;
    boost::signals2::scoped_connection mHold;
};
