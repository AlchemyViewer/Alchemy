/**
 * @file aljumpbar.h
 * @brief A path, where every step of it offers the steps it could have been.
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

#include "aldeferredrebuild.h"
#include "llpanel.h"

#include <string>
#include <utility>
#include <vector>

#include <boost/signals2.hpp>

class LLTextBox;

// Where you are, as the path that got you here -- and, on each step of it,
// what that step could have been instead.
//
// A breadcrumb that only walks back up is a label with buttons in it. The
// useful part is sideways: the crumb that says which panel you are in also
// knows the other panels in that parent, so getting to the one beside it is
// one gesture rather than up, look, and down again. Pressing a crumb goes
// there; its arrow offers what else it could have been.
//
// A path longer than the room folds from the front, because the end of a path
// says more about where you are than the start does, and the fold is itself a
// crumb offering what it swallowed.
//
// The bar holds no tree and walks nothing. It is given a path and says which
// step was asked for; what that means is the caller's.
class ALJumpBar : public LLPanel
{
public:
    AL_VIEW_TYPE(ALJumpBar, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        // Drawn at the right end, past the path: what a caller wants said
        // about where this path is, rather than about the path.
        Optional<std::string> trailer;
        Params();
    };

    struct Crumb
    {
        std::string label;
        // What the caller calls this step. Answered back when it is chosen.
        std::string value;
        // What this step could have been: its siblings, each a label and the
        // value it stands for. Empty leaves the crumb a plain button.
        std::vector<std::pair<std::string, std::string> > alternatives;
        std::string toolTip;
    };

    void setPath(std::vector<Crumb> crumbs);
    const std::vector<Crumb>& path() const { return mCrumbs; }

    // Said past the end of the path, in the quiet ink: not a step, so not
    // something to click.
    void setTrailer(const std::string& text);

    // A step chosen: the crumb it was chosen on, and the value chosen there.
    // Pressing a crumb chooses its own value, which is going there; opening
    // it and picking a sibling chooses that one, which is going sideways.
    // One signal, because they are one gesture with two answers.
    typedef boost::signals2::signal<void(size_t, const std::string&)> chose_signal_t;
    boost::signals2::connection onChose(const chose_signal_t::slot_type& cb)
    {
        return mChose.connect(cb);
    }

    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    ~ALJumpBar() override;

protected:
    friend class LLUICtrlFactory;
    ALJumpBar(const Params& p);

private:
    // A path set from inside a crumb's own press -- which is what going
    // there does -- is built once the press is over, since building deletes
    // the crumb that is still on the stack.
    void build();
    // How many crumbs from the front are swallowed so that the rest fit, and
    // the fold that offers them back.
    size_t folded() const;
    S32 widthOf(const Crumb& crumb) const;
    void chose(size_t at, std::string value);

    std::vector<Crumb>      mCrumbs;
    std::vector<LLView*>    mParts;
    LLTextBox*              mTrailer = nullptr;
    std::string             mTrailerText;
    chose_signal_t          mChose;
    ALDeferredRebuild       mRebuild;
};
