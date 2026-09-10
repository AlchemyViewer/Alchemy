/**
 * @file alpopover.cpp
 * @brief A small panel shown beside the thing it is about, until it is not wanted.
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

#include "alpopover.h"

#include "llpanel.h"
#include "lluictrlfactory.h"

ALPopover::ALPopover(const LLFloater::Params& p)
:   LLFloater(LLSD(), p)
{
}

// static
ALPopover* ALPopover::show(LLView* anchor, LLPanel* content, const std::string& title)
{
    if (!anchor || !content)
    {
        delete content;
        return nullptr;
    }

    const LLRect wanted = content->getRect();

    LLFloater::Params p(LLFloater::getDefaultParams());
    p.can_close = false;
    p.can_minimize = false;
    p.can_resize = false;
    p.can_tear_off = false;
    p.save_rect = false;
    p.save_visibility = false;
    p.title = title;
    p.rect = LLRect(0, wanted.getHeight(), wanted.getWidth(), 0);

    ALPopover* popover = new ALPopover(p);
    content->setOrigin(0, 0);
    content->setFollows(FOLLOWS_ALL);
    popover->addChild(content);

    // Under the control, its left edge with the control's, and flipped above
    // it where under would put it off the bottom: a panel a person cannot see
    // the whole of is a panel that has not opened.
    const LLRect screen = anchor->calcScreenRect();
    LLRect where = popover->getRect();
    where.setLeftTopAndSize(screen.mLeft, screen.mBottom, where.getWidth(), where.getHeight());
    if (where.mBottom < 0)
    {
        where.translate(0, screen.getHeight() + where.getHeight());
    }
    popover->setRect(where);
    popover->openFloater();
    popover->setFocus(true);
    return popover;
}

void ALPopover::settle()
{
    mEscaped = false;
    closeFloater();
}

void ALPopover::escape()
{
    mEscaped = true;
    closeFloater();
}

void ALPopover::onClose(bool app_quitting)
{
    // Once, whichever way it went: closeFloater can be reached from more
    // than one of them, and a caller told twice applies twice.
    if (!mSaidSo)
    {
        mSaidSo = true;
        mClosed(mEscaped);
    }
    LLFloater::onClose(app_quitting);
}

// Looking somewhere else is one of the ways out, and it is the usual one:
// what was chosen stands, because a person who has chosen and moved on has
// finished.
void ALPopover::onFocusLost()
{
    closeFloater();
}

bool ALPopover::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_ESCAPE && mask == MASK_NONE)
    {
        escape();
        return true;
    }
    if (key == KEY_RETURN && mask == MASK_NONE)
    {
        settle();
        return true;
    }
    return LLFloater::handleKeyHere(key, mask);
}
