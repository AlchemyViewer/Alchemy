/**
 * @file alspecimenlist.h
 * @brief A list whose rows are the widgets themselves rather than words about them.
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

#include <functional>
#include <string>
#include <vector>

#include <boost/signals2.hpp>

class ALEmptyState;
class LLScrollContainer;
class LLTextBox;

// A list of things, where each row carries the thing rather than its name.
//
// Where a catalogue of widgets would ship a thumbnail, we can build the
// widget: it is in this process, it draws itself, and it is a truer picture
// of what a file will get than any screenshot of an older version of it. The
// same applies to anything a viewer can make one of -- a colour, a font, a
// notification, an outfit.
//
// A specimen is a picture of a widget rather than a widget: a click on a row
// chooses the row, and never reaches what is in it. A button in a list of
// buttons is not there to be pressed.
//
// Rows sit under headings, in the order the headings first appear, because
// which things belong together is a fact about them and not about their
// names. A filter narrows to the rows that carry the words, and a heading
// left with nothing under it goes with them.
class ALSpecimenList : public LLPanel
{
public:
    AL_VIEW_TYPE(ALSpecimenList, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Optional<S32>           row_height;
        Optional<S32>           label_width;
        Optional<std::string>   empty_headline;
        Optional<std::string>   empty_sentence;
        Params();
    };

    struct Specimen
    {
        // The heading this row sits under. Rows with none sit at the top,
        // under no heading at all.
        std::string group;
        std::string label;
        // What the caller calls it, answered back when it is chosen.
        std::string value;
        std::string toolTip;
        // The thing itself, built by the caller because only the caller
        // knows how. Taken, and deleted with the list. Null leaves the row
        // its label, which is what a thing that would not build looks like.
        LLView*     view = nullptr;
    };

    // Takes them, views and all.
    void setSpecimens(std::vector<Specimen> specimens);
    size_t count() const { return mSpecimens.size(); }
    // How many rows the filter leaves.
    size_t shown() const { return mShown; }

    void filter(const std::string& text);
    const std::string& filterText() const { return mFilter; }

    const std::string& chosen() const { return mChosen; }
    void choose(const std::string& value);

    typedef boost::signals2::signal<void(const std::string&)> chose_signal_t;
    boost::signals2::connection onChose(const chose_signal_t::slot_type& cb)
    {
        return mChose.connect(cb);
    }

    // A row carried off the list. The list holds nothing a drop could
    // use and starts no drag of its own: it says which specimen was
    // picked up, once the press has moved far enough to be a drag, and
    // the caller with the drag tool starts the one. A list given no
    // starter has rows that are chosen and never carried.
    typedef std::function<bool(const std::string& value)> drag_fn_t;
    void setDragStarter(drag_fn_t fn) { mDragStart = std::move(fn); }
    bool startDrag(const std::string& value) { return mDragStart && mDragStart(value); }

    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    ~ALSpecimenList() override;

protected:
    friend class LLUICtrlFactory;
    ALSpecimenList(const Params& p);

private:
    class Row;

    void layout();
    bool passes(const Specimen& specimen) const;

    std::vector<Specimen>   mSpecimens;
    // One per specimen, in the same order; a row for a filtered-out specimen
    // is hidden rather than destroyed, because building a widget is the
    // expensive part and a filter is typed a letter at a time.
    std::vector<Row*>       mRows;
    std::vector<LLTextBox*> mHeadings;      // one per group, in first order
    std::vector<std::string> mGroups;
    LLScrollContainer*      mScroller = nullptr;
    LLPanel*                mContent = nullptr;
    ALEmptyState*           mEmpty = nullptr;
    std::string             mFilter;
    std::string             mChosen;
    std::string             mEmptyHeadline;
    std::string             mEmptySentence;
    S32                     mRowHeight;
    S32                     mLabelWidth;
    size_t                  mShown = 0;
    chose_signal_t          mChose;
    drag_fn_t               mDragStart;
};
