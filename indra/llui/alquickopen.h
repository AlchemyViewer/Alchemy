/**
 * @file alquickopen.h
 * @brief One field, a ranked list, and the thing you were thinking of at the top.
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

class LLLineEditor;
class LLScrollListCtrl;

// Type a few letters, get the thing you were thinking of at the top, press
// return.
//
// The whole of it is the ranking. A list that filters shows you everything
// that contains what you typed, in whatever order it was already in, and
// leaves you to read it; a list that ranks puts `flbuy` above every file
// whose name merely contains those letters, because `floater_buy.xml` is what
// somebody typing that meant. Initials, word starts, a run in the middle and
// scattered letters in order are four different degrees of meaning it, and
// they are what the score is made of.
//
// The candidates are the caller's, and so is what choosing one does. This
// holds no list of files, no history and no idea what it is opening.
class ALQuickOpen : public LLPanel
{
public:
    AL_VIEW_TYPE(ALQuickOpen, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Optional<std::string> placeholder;
        Optional<S32>         rows;       // how many to offer at once
        Params();
    };

    struct Candidate
    {
        std::string label;
        // Said quietly beside the label: which folder, which file, which
        // skin. Not matched against, because a person types the name.
        std::string detail;
        std::string value;
        std::string icon;
    };

    void setCandidates(std::vector<Candidate> candidates);

    // What is typed. Set it to seed the field; it ranks either way.
    void setQuery(const std::string& query);
    const std::string& query() const { return mQuery; }

    // The ranking, in order, best first. Public because it is the whole of
    // this widget and the only part worth testing without a screen.
    static std::vector<size_t> rank(const std::vector<Candidate>& candidates,
                                    std::string_view query);

    // How well one label answers a query, higher being better; zero is not a
    // match at all. Exposed for the same reason.
    static S32 score(std::string_view label, std::string_view query);

    // The field, focused, and whatever is in it selected: opening this is
    // asking for something, and the last thing asked for is a poor start on
    // the next one.
    void takeFocus();

    // Return, or a row chosen. Nothing is sent for a query that matched
    // nothing, since there is nothing to send.
    typedef boost::signals2::signal<void(const std::string&)> chose_signal_t;
    boost::signals2::connection onChose(const chose_signal_t::slot_type& cb)
    {
        return mChose.connect(cb);
    }

    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    bool handleKeyHere(KEY key, MASK mask) override;

protected:
    friend class LLUICtrlFactory;
    ALQuickOpen(const Params& p);

private:
    void fill();
    void chooseSelected();
    void layout();

    std::vector<Candidate>  mCandidates;
    std::vector<size_t>     mRanked;
    std::string             mQuery;
    std::string             mPlaceholder;
    S32                     mRows;
    LLLineEditor*           mField = nullptr;
    LLScrollListCtrl*       mList = nullptr;
    chose_signal_t          mChose;
};
