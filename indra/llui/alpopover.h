/**
 * @file alpopover.h
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

#pragma once

#include "llfloater.h"

#include <string>

#include <boost/signals2.hpp>

// A panel put beside the control it is about, over whatever is under it, and
// gone as soon as it is not wanted.
//
// It is a floater because that is the only thing in this library that is not
// clipped by its parent: a list dropped from a row of a property grid inside a
// scroll container has nowhere to go. It carries none of a floater's chrome --
// no close box, no minimise, no drag bar unless a caller asks for a title --
// because it is not a window and should not read as one.
//
// Three ways out, and a caller applying on close needs to tell them apart:
// choosing something (a caller calls settle()), pressing escape, or looking
// somewhere else. Escape means the caller should keep what it had.
//
// The content is the caller's panel, given whole: this places it, shows it,
// and deletes it. What is in it, how big it is and what choosing means are
// none of this widget's business.
//
// A popover with more to it than a panel -- one that draws in its own
// header, or resizes -- derives from this, builds its own content, and
// opens with openBeside(): the placing and the three ways out are the same
// for every one of them.
class ALPopover : public LLFloater
{
public:
    AL_VIEW_TYPE(ALPopover, LLFloater);

    // Under `anchor` and left-aligned with it, or above it where under would
    // be off the bottom of the screen. The popover takes `content`, and takes
    // its size from it. Null where there is nothing to put it beside.
    //
    // A title is drawn where one is given; where none is, the panel is the
    // whole of it and there is no bar to drag it by.
    static ALPopover* show(LLView* anchor, LLPanel* content,
                           const std::string& title = LLStringUtil::null);

    // What a popover this size is built from: no close box, no minimise,
    // no tear-off, nothing saved, and a size a person may change only
    // where asked.
    static LLFloater::Params paramsFor(S32 width, S32 height,
                                       const std::string& title = LLStringUtil::null,
                                       bool resizable = false);

    // Opened beside the anchor: under it with their left edges together,
    // above it where under would run off the bottom, and shoved back on
    // screen where a side would run off, then shown and given the keyboard.
    void openBeside(const LLView* anchor);

    // Gone, having settled: what a caller calls when the thing in it has been
    // chosen. The closed signal says it was not escaped.
    void settle();

    // Gone, keeping nothing.
    void escape();

    // Told as it goes: true where it was escaped, so a caller that applies on
    // close knows to apply nothing.
    typedef boost::signals2::signal<void(bool)> closed_signal_t;
    boost::signals2::connection onClosed(const closed_signal_t::slot_type& cb)
    {
        return mClosed.connect(cb);
    }

    bool escaped() const { return mEscaped; }

    void onClose(bool app_quitting) override;
    void onFocusLost() override;
    bool handleKeyHere(KEY key, MASK mask) override;

protected:
    friend class LLUICtrlFactory;
    ALPopover(const LLFloater::Params& p);

private:
    bool            mEscaped = false;
    bool            mSaidSo = false;    // the signal is sent once
    closed_signal_t mClosed;
};
