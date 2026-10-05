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

#include "alfuzzymatch.h"
#include "llpanel.h"

#include <string>
#include <vector>

#include <boost/signals2.hpp>

class ALChoiceList;
class LLLineEditor;

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
//
// Or, freeform: what is typed is the answer itself -- a line number, a new
// name -- and the one row under the field says what return will do with
// it, told again by the caller as the typing changes.
class ALQuickOpen : public LLPanel
{
public:
    AL_VIEW_TYPE(ALQuickOpen, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Optional<std::string> placeholder;
        // How many to offer at once: the best so many while something is
        // typed, and with nothing typed the first so many, in the order
        // given, to browse -- not a list of hundreds made at every key.
        Optional<S32>         rows;
        Params();
    };

    struct Candidate
    {
        std::string label;
        // Said quietly beside the label: which folder, which file, which
        // skin. Not matched against, because a person types the name.
        std::string detail;
        // Other words it answers to, scored a tier below what the label
        // would score for the same match: what a thing is called in plain
        // words, where the label is what a file calls it.
        std::string also;
        std::string value;
        std::string icon;
    };

    void setCandidates(std::vector<Candidate> candidates);

    // What is typed. Set it to seed the field; it ranks either way.
    void setQuery(const std::string& query);
    const std::string& query() const { return mQuery; }

    // What a query begins with to say which list it is asked of -- the
    // `>` before a command, as in Visual Studio Code -- and so is not
    // matched against the candidates: taken off, with the blanks after it,
    // before ranking, where the query begins with it. Who asked changes the
    // candidates as the query comes to begin with it or no longer does.
    void setPrefix(const std::string& prefix);

    // Freeform from here on: the candidates are put aside, the one row
    // says this, and return sends what was typed. Say it again as the
    // query changes, since what return will do has changed with it.
    void setHint(const std::string& hint);
    bool freeform() const { return mFreeform; }

    // What the list shows, by the candidates' values in order -- the one
    // row of a freeform question is what was typed -- and which of them is
    // chosen, or -1.
    const std::vector<std::string>& listed() const { return mListed; }
    S32                             chosenRow() const;

    // The query, as it changes under the keys or by setQuery.
    typedef boost::signals2::signal<void(const std::string&)> query_signal_t;
    boost::signals2::connection onQueryChanged(const query_signal_t::slot_type& cb)
    {
        return mQueryChanged.connect(cb);
    }

    // The ranking, in order, best first: by the label, or by the other
    // words a tier down. Public because it is the whole of this widget
    // and the only part worth testing without a screen.
    static std::vector<size_t> rank(const std::vector<Candidate>& candidates,
                                    std::string_view query);

    // How well one label answers a query, higher being better; zero is not a
    // match at all. Exposed for the same reason. By the shared matcher
    // (ALFuzzyMatch), each kind of answer worth its own tier here.
    static S32 score(std::string_view label, std::string_view query);
    static S32 score(const ALFuzzyMatch::Target& label, std::string_view query);

    // How tall to make one that is to show so many rows under its field
    // with nothing to scroll: what a freeform question, whose one row is
    // its hint, asks for.
    static S32 heightForRows(S32 rows);

    // The field, focused, and whatever is in it selected: opening this is
    // asking for something, and the last thing asked for is a poor start on
    // the next one.
    void takeFocus();

    // The colours of what it is over -- an editor's ground and ink -- on
    // its field, its list and its own back, so that it reads as part of
    // that rather than of the skin.
    void setColors(const LLColor4& background, const LLColor4& ink);

    // Return, or a row chosen; the query itself when freeform. Nothing is
    // sent for a query that matched nothing, since there is nothing to
    // send.
    typedef boost::signals2::signal<void(const std::string&)> chose_signal_t;
    boost::signals2::connection onChose(const chose_signal_t::slot_type& cb)
    {
        return mChose.connect(cb);
    }
    // Shift-Return: the row chosen to be held rather than taken -- for a
    // caller with a second thing to do with a pick, as vim's window puts
    // the line up to edit where Return runs it. Without a listener,
    // Shift-Return is Return.
    boost::signals2::connection onChoseToHold(const chose_signal_t::slot_type& cb)
    {
        return mChoseToHold.connect(cb);
    }

    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;

protected:
    friend class LLUICtrlFactory;
    ALQuickOpen(const Params& p);

private:
    // The ranking of the candidates as they were set, from what was made
    // ready of them.
    std::vector<size_t> ranked(std::string_view query) const;
    void fill();
    void chooseSelected(bool hold = false);
    void layout();

    std::vector<Candidate>  mCandidates;
    // Each candidate's label and other words made ready to be matched
    // once, as they are set, rather than at every key.
    std::vector<ALFuzzyMatch::Target> mLabels;
    std::vector<ALFuzzyMatch::Target> mAlso;
    std::vector<size_t>     mRanked;
    std::string             mQuery;
    std::string             mPlaceholder;
    std::string             mPrefix;
    // The query as it is matched: less the prefix it begins with.
    std::string_view        matched() const;
    S32                     mRows;
    bool                    mFreeform = false;
    std::string             mHint;
    // Given colours of its own: framed in them, its parts inset from the
    // frame, so that it reads as a card over the text and not as more
    // of the text.
    bool                    mThemed = false;
    LLColor4                mGround;
    LLColor4                mInk;
    LLLineEditor*           mField = nullptr;
    // The rows on the text engine, as the editor's completions are.
    ALChoiceList*           mList = nullptr;
    std::vector<std::string> mListed;
    chose_signal_t          mChose;
    chose_signal_t          mChoseToHold;
    query_signal_t          mQueryChanged;
};
