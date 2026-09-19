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

#include <boost/unordered/unordered_flat_map.hpp>

namespace
{
// The size each kind of popover was last left at, for the session.
boost::unordered_flat_map<std::string, std::pair<S32, S32>>& rememberedSizes()
{
    static boost::unordered_flat_map<std::string, std::pair<S32, S32>> sizes;
    return sizes;
}
} // namespace

ALPopover::ALPopover(const LLFloater::Params& p)
:   LLFloater(LLSD(), p)
{
    // What paramsRemembered wrote into the name, which is the one place a
    // params block has to carry it.
    static const std::string prefix = "popover:";
    if (p.name().compare(0, prefix.size(), prefix) == 0)
    {
        mSizeKind = p.name().substr(prefix.size());
    }
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
    ALPopover* popover = new ALPopover(paramsFor(wanted.getWidth(), wanted.getHeight(), title));
    content->setOrigin(0, 0);
    content->setFollows(FOLLOWS_ALL);
    popover->addChild(content);
    popover->openBeside(anchor);
    return popover;
}

// static
LLFloater::Params ALPopover::paramsFor(S32 width, S32 height, const std::string& title, bool resizable)
{
    LLFloater::Params p(LLFloater::getDefaultParams());
    p.can_close = false;
    p.can_minimize = false;
    p.can_resize = resizable;
    p.can_tear_off = false;
    p.save_rect = false;
    p.save_visibility = false;
    p.title = title;
    p.rect = LLRect(0, height, width, 0);
    return p;
}

// static
LLFloater::Params ALPopover::paramsRemembered(const std::string& kind, S32 width, S32 height,
                                              const std::string& title, bool resizable)
{
    const auto found = rememberedSizes().find(kind);
    if (found != rememberedSizes().end())
    {
        width = found->second.first;
        height = found->second.second;
    }
    LLFloater::Params p = paramsFor(width, height, title, resizable);
    // Carried on the name, since a params block has nowhere else to say it;
    // the constructor reads it back.
    p.name = "popover:" + kind;
    return p;
}

// Under the control, its left edge with the control's, and flipped above it
// where under would put it off the bottom: a panel a person cannot see the
// whole of is a panel that has not opened. A side off the screen is put
// back on it for the same reason, which is what the floater view does for
// every window it holds.
void ALPopover::openBeside(const LLView* anchor)
{
    if (anchor)
    {
        // Where keys go: the window the anchor is in, or the anchor itself
        // where it is one.
        const LLFloater* home = ALViewType::as<LLFloater>(anchor);
        if (!home)
        {
            home = anchor->getParentByType<LLFloater>();
        }
        if (home && home != this)
        {
            mHome = home->getHandle();
        }

        const LLRect screen = anchor->calcScreenRect();
        LLRect where = getRect();
        where.setLeftTopAndSize(screen.mLeft, screen.mBottom, where.getWidth(), where.getHeight());
        if (where.mBottom < 0)
        {
            where.translate(0, screen.getHeight() + where.getHeight());
        }
        setRect(where);
    }
    if (gFloaterView && getParent() == gFloaterView)
    {
        gFloaterView->adjustToFitScreen(this, false);
    }
    openFloater();
    setFocus(true);
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
    if (!mSizeKind.empty())
    {
        rememberedSizes()[mSizeKind] = { getRect().getWidth(), getRect().getHeight() };
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
    if (LLFloater::handleKeyHere(key, mask))
    {
        return true;
    }
    if (LLFloater* home = mHome.get())
    {
        return home->handleKeyHere(key, mask);
    }
    return false;
}
