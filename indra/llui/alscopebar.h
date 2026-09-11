/**
 * @file alscopebar.h
 * @brief A query said as a sentence: fixed words, dropdowns, and what is looked for.
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

// A query written as a sentence rather than as a form:
//
//     Find [Attribute name] [starting with] `close` in [every skin]
//
// A row of dropdowns and fixed words, reading left to right, with one field
// for what is being looked for and a slot at the right end for whatever the
// caller wants beside it -- a count, a button, a spinner.
//
// A form of the same query is five labelled boxes and a reader working out
// which of them apply to which. A sentence says how they compose by being one,
// and it is narrower, because a dropdown is as wide as the word in it rather
// than as wide as its column.
//
// The bar holds no query and runs nothing. It says what has been composed and
// which part changed; what that means is the caller's.
class ALScopeBar : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScopeBar, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Optional<S32> control_height;
        Optional<S32> gap;              // between one part and the next
        Params();
    };

    // One part of the sentence.
    struct Segment
    {
        enum class Kind : U8
        {
            Word,       // a fixed word, which is not part of the query
            Choice,     // a dropdown, whose chosen value is
            Field       // what is being looked for, which takes the room left
        };

        Kind        kind = Kind::Word;
        // What this part is called when the query is read back. A Word needs
        // none, since a word chooses nothing.
        std::string name;
        // A Word's word, or a Field's placeholder.
        std::string text;
        // A Choice's options, each a label and the value it stands for. The
        // first is chosen unless `value` names another.
        std::vector<std::pair<std::string, std::string> > choices;
        std::string value;
        std::string toolTip;
    };

    // The sentence, in the order it reads. Building it again keeps the values
    // of the parts that are still in it, so a caller may rewrite the sentence
    // as the answer to one part changes.
    void setSentence(std::vector<Segment> segments);

    std::string valueOf(std::string_view name) const;
    void setValue(std::string_view name, const std::string& value);
    // The whole of it, for a caller passing it on rather than reading parts.
    LLSD query() const;

    // A view at the right end. Taken, and deleted with the bar.
    void setAdornment(LLView* view);

    // Something was chosen or typed.
    typedef boost::signals2::signal<void()> changed_signal_t;
    boost::signals2::connection onChanged(const changed_signal_t::slot_type& cb)
    {
        return mChanged.connect(cb);
    }

    // The field was committed -- return, which is what runs a search. A bar
    // whose caller narrows as you type has no use for this and connects the
    // one above instead.
    typedef boost::signals2::signal<void()> run_signal_t;
    boost::signals2::connection onRun(const run_signal_t::slot_type& cb)
    {
        return mRun.connect(cb);
    }

    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    ~ALScopeBar() override;

protected:
    friend class LLUICtrlFactory;
    ALScopeBar(const Params& p);

private:
    // The parts, made and placed. The field is the one that flexes: every
    // other part is as wide as what is in it, which is what makes this read
    // as a sentence rather than as a row of columns.
    //
    // A sentence rewritten from inside one of its own parts' callbacks --
    // which is what a caller answering one part by changing the others does
    // -- is built again once the part has finished, since building deletes
    // the part that is still on the stack.
    void build();
    void layout();
    S32 widthOf(const Segment& segment) const;

    std::vector<Segment>    mSegments;
    std::vector<Segment>    mNext;          // the sentence to build from
    std::vector<LLView*>    mParts;         // one per segment, in order
    LLView*                 mAdornment = nullptr;
    S32                     mControlHeight;
    S32                     mGap;
    changed_signal_t        mChanged;
    run_signal_t            mRun;
    ALDeferredRebuild       mRebuild;
};
