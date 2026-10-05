/**
 * @file alquickask.cpp
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

#include "linden_common.h"

#include "alquickask.h"

#include "lluictrlfactory.h"

#include <memory>
#include <utility>

ALQuickOpen* ALQuickAsk::ask(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                             std::function<void(const std::string&)> chose, LLView* anchor, S32 width, S32 height,
                             std::function<void()> escaped, std::function<void(const std::string&)> hold, std::function<void()> left)
{
    // Asked for again while it is up -- the same key pressed twice -- it is
    // what was typed that is wanted back, not a fresh field, and the answer
    // goes to whoever asked last. Another question is asked afresh: the
    // list up answers whoever asked it, and a command picked from a list
    // of symbols is a jump to the first symbol. A freeform one is another
    // question whatever it asks.
    const std::string question = title + "\n" + placeholder;
    if (ALPopover* popover = mPopover.get())
    {
        ALQuickOpen* quick = popover->findChild<ALQuickOpen>("quick_open");
        if (quick && !quick->freeform() && question == mQuestion)
        {
            quick->setCandidates(std::move(candidates));
            answer(popover, quick, std::move(chose), std::move(escaped), std::move(hold), std::move(left));
            quick->takeFocus();
            return quick;
        }
    }
    mPopover.close();

    constexpr S32 WIDTH = 460;
    constexpr S32 HEIGHT = 300;

    ALQuickOpen::Params qp(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
    qp.name = "quick_open";
    qp.rect = LLRect(0, height > 0 ? height : HEIGHT, width > 0 ? width : WIDTH, 0);
    qp.placeholder = placeholder;
    ALQuickOpen* quick = LLUICtrlFactory::create<ALQuickOpen>(qp);
    quick->setCandidates(std::move(candidates));

    // The popover takes the content, and takes it even when it cannot
    // show. Over the top of the anchor, centred, which is where a person
    // typing a name into a window looks.
    ALPopover* popover = ALPopover::showOver(anchor, quick, title);
    if (!popover)
    {
        return nullptr;
    }
    mQuestion = question;
    answer(popover, quick, std::move(chose), std::move(escaped), std::move(hold), std::move(left));
    quick->takeFocus();
    return quick;
}

void ALQuickAsk::answer(ALPopover* popover, ALQuickOpen* quick, std::function<void(const std::string&)> chose,
                        std::function<void()> escaped, std::function<void(const std::string&)> hold, std::function<void()> left)
{
    // Whether an answer came: gone without one, whoever asked is told how
    // -- escaped, and what it previewed is put back; looked away from, and
    // what it previewed stands. Not put back there: the look away is a
    // click, on the editor as often as not, and a view put back under the
    // click lands it on another line than the one it was aimed at.
    const auto          answered = std::make_shared<bool>(false);
    LLHandle<ALPopover> held     = popover->getDerivedHandle<ALPopover>();
    const auto          settled  = [held, answered]()
    {
        // Settled first, so the keyboard comes back to the window before
        // what was chosen is acted on -- a choice that puts the keyboard
        // somewhere needs it back to give.
        *answered = true;
        if (ALPopover* up = held.get())
        {
            up->settle();
        }
    };
    mChose = quick->onChose([settled, chose = std::move(chose)](const std::string& value)
    {
        settled();
        chose(value);
    });
    mHold.disconnect();
    if (hold)
    {
        mHold = quick->onChoseToHold([settled, hold = std::move(hold)](const std::string& value) {
            settled();
            hold(value);
        });
    }
    mPopover.hold(popover, [answered, escaped = std::move(escaped), left = std::move(left)](bool was_escaped) {
        if (was_escaped && escaped)
        {
            escaped();
        }
        else if (!was_escaped && !*answered && left)
        {
            left();
        }
    });
}
