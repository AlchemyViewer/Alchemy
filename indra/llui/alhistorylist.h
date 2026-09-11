/**
 * @file alhistorylist.h
 * @brief A buffered document's undo stack as a place, rather than a keystroke.
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

#include "llpanel.h"

#include <string>
#include <vector>

#include <boost/signals2.hpp>

class ALEmptyState;
class LLScrollListCtrl;

// Everything done to a document, oldest first, with a line drawn where the
// present is. Control-Z is a keystroke somebody hopes worked; this is the
// same stack as somewhere to look, so that undoing four steps is one click
// on the fourth one back rather than four presses and a guess.
//
// Steps below the line have been taken; steps above it have been put back and
// are waiting to be done again. Clicking one asks for that many to be in
// force -- which the caller does, because only the caller knows how to undo
// and redo its own document. This holds no document and performs nothing.
//
// The words are the caller's, for the reason every widget here has the same
// reason: this library has no file for a translator to open, and what a step
// did is said in the vocabulary of whatever the document is.
class ALHistoryList : public LLPanel
{
public:
    AL_VIEW_TYPE(ALHistoryList, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        // What the list says when there is nothing in it. A pane with
        // nothing in it is still saying something.
        Optional<std::string> empty_headline;
        Optional<std::string> empty_sentence;
        Params();
    };

    // One thing done.
    struct Step
    {
        // What it did, in the caller's words: "width on close_btn".
        std::string what;
        // Which document it was done to, where the caller has more than one
        // open. Shown in its own column and left out entirely when no step
        // names one.
        std::string where;
    };

    // The steps oldest first, and how many of them are in force: everything
    // before that count has been taken, everything from it has been put back.
    // A count equal to the size is a stack with nothing to redo.
    void setSteps(std::vector<Step> steps, size_t in_force);

    size_t count() const { return mSteps.size(); }
    size_t inForce() const { return mInForce; }

    // A step chosen, said as how many should then be in force: choosing the
    // oldest step asks for one, and choosing one above the line asks for more
    // than there are now. Sent on a double click rather than a click, since a
    // click that rewinds four steps is a click nobody meant.
    typedef boost::signals2::signal<void(size_t)> goto_signal_t;
    boost::signals2::connection onGoTo(const goto_signal_t::slot_type& cb)
    {
        return mGoTo.connect(cb);
    }

    // The step now selected, asked for as the present: what a double click
    // does, and what a caller with a key bound to it calls.
    void goToSelected();

    // A row selected, by its index into the steps: for a caller that shows
    // the element a step was about while it is pointed at.
    typedef boost::signals2::signal<void(size_t)> chose_signal_t;
    boost::signals2::connection onStepChosen(const chose_signal_t::slot_type& cb)
    {
        return mChose.connect(cb);
    }


protected:
    friend class LLUICtrlFactory;
    ALHistoryList(const Params& p);

private:
    void fill();
    void onRowChosen();

    LLScrollListCtrl*   mList = nullptr;
    ALEmptyState*       mEmpty = nullptr;
    std::vector<Step>   mSteps;
    size_t              mInForce = 0;
    std::string         mEmptyHeadline;
    std::string         mEmptySentence;
    goto_signal_t       mGoTo;
    chose_signal_t      mChose;
};
