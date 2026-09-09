/**
 * @file alemptystate.h
 * @brief What a pane says when there is nothing in it.
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

#include <boost/signals2.hpp>

class LLButton;
class LLIconCtrl;
class LLTextBox;

// A pane with nothing in it is still saying something, and "Nothing
// selected." is a true sentence that does no work. The same line doing a job
// is a headline that says what is missing and a sentence that says how to
// get some -- "Nothing selected. Choose an element in the outline, or
// control-click one on the canvas." -- and, where there is one thing that
// would fix it, a button that does that thing.
//
// Four parts, all of them optional: an icon, a headline, a sentence, and an
// action. What is given is laid out down the middle of whatever room there
// is; what is not is not there at all, so a state with only a sentence is
// one line of text and not a line of text under two empty rows.
//
// The words are the caller's. This library has no file for a translator to
// open, and a pane knows what it is empty of.
class ALEmptyState : public LLPanel
{
public:
    AL_VIEW_TYPE(ALEmptyState, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Optional<std::string> icon;         // a named image, or nothing
        Optional<std::string> headline;
        Optional<std::string> sentence;
        Optional<std::string> action;       // the button's label, or nothing
        Params();
    };

    // All four at once, which is how a pane changes what it is empty of.
    void say(const std::string& headline, const std::string& sentence,
             const std::string& action = LLStringUtil::null);
    void setIcon(const std::string& name);

    const std::string& headline() const { return mHeadline; }
    const std::string& sentence() const { return mSentence; }

    // The button, pressed. A state with no action never sends this.
    typedef boost::signals2::signal<void()> action_signal_t;
    boost::signals2::connection onAction(const action_signal_t::slot_type& cb)
    {
        return mAction.connect(cb);
    }

    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

protected:
    friend class LLUICtrlFactory;
    ALEmptyState(const Params& p);

private:
    // Down the middle of the room there is, with only the parts that were
    // given: the whole block is centred, so it reads as one thing rather
    // than as three that happen to be near each other.
    void layout();

    LLIconCtrl*     mIcon = nullptr;
    LLTextBox*      mHeadlineText = nullptr;
    LLTextBox*      mSentenceText = nullptr;
    LLButton*       mButton = nullptr;
    std::string     mHeadline;
    std::string     mSentence;
    std::string     mActionLabel;
    action_signal_t mAction;
};
