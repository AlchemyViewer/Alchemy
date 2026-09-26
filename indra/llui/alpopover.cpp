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

#include "alplace.h"
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
ALPopover* ALPopover::showAt(const LLRect& screen, LLView* anchor, LLPanel* content, const std::string& title)
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
    popover->openBeside(screen, anchor);
    return popover;
}

// static
ALPopover* ALPopover::showOver(LLView* anchor, LLPanel* content, const std::string& title)
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
    popover->openOver(anchor);
    return popover;
}

// static
LLFloater::Params ALPopover::paramsFor(S32 width, S32 height, const std::string& title, bool resizable)
{
    LLFloater::Params p(LLFloater::getDefaultParams());
    // A part of the window it is over, which comes and goes as a menu does:
    // not a window of its own, to be heard opening and closing.
    p.sound_flags = LLView::SILENT;
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
// where under would put it off the bottom and above would not (ALPlace): a
// panel a person cannot see the whole of is a panel that has not opened. A
// side off the screen is put back on it for the same reason, which is what
// the floater view does for every window it holds.
void ALPopover::adopt(const LLView* anchor)
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
}

void ALPopover::openAt(LLRect where)
{
    // One of its home's dependents, and so gone when its home goes: a
    // popover left over a window that has closed is about nothing. Its
    // home put away or minimised hides it, and it goes as the keyboard
    // leaves it. Before it is placed, since the placing is its own.
    if (LLFloater* home = mHome.get())
    {
        home->addDependentFloater(this, /*reposition*/ false);
    }
    setRect(where);
    if (gFloaterView && getParent() == gFloaterView)
    {
        gFloaterView->adjustToFitScreen(this, false);
    }
    if (LLView* keys = dynamic_cast<LLView*>(gFocusMgr.getKeyboardFocus()); keys && keys != this && !keys->hasAncestor(this))
    {
        mKeysBefore = keys->getHandle();
    }
    openFloater();
    setFocus(true);
}

void ALPopover::openBeside(const LLView* anchor)
{
    if (!anchor)
    {
        openAt(getRect());
        return;
    }
    openBeside(anchor->calcScreenRect(), anchor);
}

void ALPopover::openBeside(const LLRect& screen, const LLView* anchor)
{
    if (anchor)
    {
        adopt(anchor);
    }
    // The floater view is the screen a popover has; without one, only the
    // screen's bottom is known.
    const LLRect bounds = gFloaterView ? gFloaterView->getLocalRect() : LLRect(S32_MIN / 2, S32_MAX / 2, S32_MAX / 2, 0);
    openAt(ALPlace::under(screen, getRect().getWidth(), getRect().getHeight(), bounds));
}

void ALPopover::openOver(const LLView* anchor)
{
    constexpr S32 BELOW_TOP = 12;
    LLRect where = getRect();
    if (anchor)
    {
        adopt(anchor);
        const LLRect screen = anchor->calcScreenRect();
        where.setLeftTopAndSize(screen.getCenterX() - where.getWidth() / 2, screen.mTop - BELOW_TOP, where.getWidth(), where.getHeight());
    }
    openAt(where);
}

void ALPopover::settle()
{
    mEscaped = false;
    closeFloater();
    giveKeysBack();
}

void ALPopover::escape()
{
    mEscaped = true;
    closeFloater();
    giveKeysBack();
}

void ALPopover::giveKeysBack()
{
    if (gFocusMgr.getKeyboardFocus())
    {
        // Taken by what was chosen, or by the window it was over.
        return;
    }
    LLView* before = mKeysBefore.get();
    if (before && before->isInEnabledChain() && before->isInVisibleChain())
    {
        before->setFocus(true);
    }
    else if (LLFloater* home = mHome.get(); home && home->getVisible())
    {
        home->setFocus(true);
    }
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

// --- the slot ---------------------------------------------------------------------

ALPopoverSlot::~ALPopoverSlot()
{
    drop();
}

void ALPopoverSlot::hold(ALPopover* popover, closed_t closed)
{
    if (popover != mHeld.get())
    {
        close(true);
    }
    mClosed.disconnect();
    mHeld = popover ? popover->getDerivedHandle<ALPopover>() : LLHandle<ALPopover>();
    if (!popover)
    {
        return;
    }
    mClosed = popover->onClosed([this, closed = std::move(closed)](bool escaped) {
        // Empty before whoever is told hears it, so that one who opens the
        // next from here finds nothing up.
        mHeld.markDead();
        if (closed)
        {
            closed(escaped);
        }
    });
}

void ALPopoverSlot::close(bool escape)
{
    ALPopover* popover = mHeld.get();
    if (!popover)
    {
        return;
    }
    if (escape)
    {
        popover->escape();
    }
    else
    {
        popover->settle();
    }
    // Emptied as it said so; one that had said so already is let go of
    // here. Not one the telling opened in its place.
    if (mHeld.get() == popover)
    {
        mClosed.disconnect();
        mHeld.markDead();
    }
}

void ALPopoverSlot::drop()
{
    mClosed.disconnect();
    if (ALPopover* popover = mHeld.get())
    {
        popover->die();
    }
    mHeld.markDead();
}
