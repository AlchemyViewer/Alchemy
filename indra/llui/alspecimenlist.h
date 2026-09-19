/**
 * @file alspecimenlist.h
 * @brief A list whose rows are the things themselves rather than words about them.
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
#include "lluiimage.h"

#include <functional>
#include <string>
#include <vector>

#include <boost/signals2.hpp>

class ALEmptyState;
class LLScrollContainer;
class LLTextBox;

// A list of things, where each entry carries the thing rather than its
// name.
//
// Where a catalogue of widgets would ship a thumbnail, we can build the
// widget: it is in this process, it draws itself, and it is a truer picture
// of what a file will get than any screenshot of an older version of it. The
// same applies to anything a viewer can make one of -- a colour, a font, a
// notification, an outfit -- and to a picture the skin draws, which is
// known by sight before it is known by name.
//
// A specimen is a picture of a widget rather than a widget: a click on an
// entry chooses the entry, and never reaches what is in it. A button in a
// list of buttons is not there to be pressed.
//
// Two layouts, chosen by the list and changed at will: rows, one under
// the other, each with a label beside the thing; or cells, across and
// down, each a thing with its name under it. A thing is a built view or
// a picture by name; a picture is drawn rather than built, since there
// may be hundreds. Entries sit under headings, in the order the headings
// first appear, because which things belong together is a fact about
// them and not about their names. A filter narrows to the entries that
// carry the words, and a heading left with nothing under it goes with
// them.
class ALSpecimenList : public LLPanel
{
public:
    AL_VIEW_TYPE(ALSpecimenList, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Optional<S32>           row_height;
        Optional<S32>           label_width;
        // Given, the list is cells of this size rather than rows.
        Optional<S32>           cell_width;
        Optional<S32>           cell_height;
        Optional<std::string>   empty_headline;
        Optional<std::string>   empty_sentence;
        Params();
    };

    struct Specimen
    {
        // The heading this entry sits under. Entries with none sit at the
        // top, under no heading at all.
        std::string group;
        std::string label;
        // What the caller calls it, answered back when it is chosen.
        std::string value;
        std::string toolTip;
        // The thing itself, built by the caller because only the caller
        // knows how. Taken, and deleted with the list. Null leaves the row
        // its label, which is what a thing that would not build looks like.
        LLView*     view = nullptr;
        // Or the picture, by the name the skin knows it by: drawn in a
        // cell, and shown beside the label in a row.
        std::string image;
        // Stretched across the cell, as chrome is drawn, rather than fitted
        // at its own proportions, as an icon is.
        bool        stretch = false;
        // Carries the person's changes; drawn with a mark.
        bool        marked = false;
        // Does not resolve; its name drawn as a warning.
        bool        broken = false;
    };

    // Takes them, views and all.
    void setSpecimens(std::vector<Specimen> specimens);
    size_t count() const { return mSpecimens.size(); }
    // How many entries the filter leaves.
    size_t shown() const { return mShown; }
    // Cells of this size, or rows for none; the entries stay, views and
    // all, and move to the other layout, which is kept built: switching
    // back and forth costs a frame and no rebuilding.
    void setCellSize(S32 width, S32 height);
    bool cells() const { return mCellWidth > 0 && mCellHeight > 0; }

    void filter(const std::string& text);
    const std::string& filterText() const { return mFilter; }

    // What the list says while it holds nothing the filter lets through.
    void setEmpty(const std::string& headline, const std::string& sentence);

    const std::string& chosen() const { return mChosen; }
    // Chosen, and said; or only marked as such and brought into view,
    // for a caller that chose it somewhere else. Either way it is the
    // whole of the selection then.
    void choose(const std::string& value);
    void setChosen(const std::string& value);

    typedef boost::signals2::signal<void(const std::string&)> chose_signal_t;
    boost::signals2::connection onChose(const chose_signal_t::slot_type& cb)
    {
        return mChose.connect(cb);
    }

    // In cells, control and a click adds an entry to the selection or
    // takes it out again, and shift and a click takes everything between
    // the chosen entry and the clicked one: the chosen entry is what the
    // list is about, and the selection is what an operation over several
    // is about. The chosen entry is always among the selected.
    const std::vector<std::string>& selection() const { return mSelection; }
    void setSelection(std::vector<std::string> values);
    typedef boost::signals2::signal<void()> selection_signal_t;
    boost::signals2::connection onSelectionChanged(const selection_signal_t::slot_type& cb)
    {
        return mSelectionChanged.connect(cb);
    }
    // A cell pressed with the modifiers held, which the tiles pass on.
    void cellPressed(S32 index, MASK mask);

    // An entry carried off the list. The list holds nothing a drop could
    // use and starts no drag of its own: it says which specimen was
    // picked up, once the press has moved far enough to be a drag, and
    // the caller with the drag tool starts the one. A list given no
    // starter has entries that are chosen and never carried.
    typedef std::function<bool(const std::string& value)> drag_fn_t;
    void setDragStarter(drag_fn_t fn) { mDragStart = std::move(fn); }
    bool startDrag(const std::string& value) { return mDragStart && mDragStart(value); }

    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    // In cells, the arrows move the choice a cell across or a row up or
    // down.
    bool handleKeyHere(KEY key, MASK mask) override;
    ~ALSpecimenList() override;

protected:
    friend class LLUICtrlFactory;
    ALSpecimenList(const Params& p);

private:
    class Row;
    class Tiles;

    // A cell, or a heading across the cells.
    struct Cell
    {
        LLRect      rect;
        // Which specimen, or none for a heading.
        S32         index = -1;
        std::string heading;
    };

    // The rows made for the entries held, once; the tiles need none.
    void buildRows();
    // Every row and heading taken down, and every view out of wherever it
    // sat; the views are the caller's until they are placed again.
    void unbuild();
    // The views into the rows or the tiles, whichever is showing.
    void placeViews();
    void layout();
    void layoutRows();
    void layoutCells();
    bool passes(const Specimen& specimen) const;
    // What is shown, in the order it is shown: the entries under no
    // heading, then each heading's.
    std::vector<S32> shownOrder() const;
    S32 cellAt(S32 x, S32 y) const;
    // The cell of the chosen entry, or -1.
    S32 chosenCell() const;
    void drawCell(const Cell& cell, bool hovered) const;
    void showChosen();

    std::vector<Specimen>   mSpecimens;
    // One per specimen, in the same order; a row for a filtered-out specimen
    // is hidden rather than destroyed, because building a widget is the
    // expensive part and a filter is typed a letter at a time.
    std::vector<Row*>       mRows;
    std::vector<LLTextBox*> mHeadings;      // one per group, in first order
    std::vector<std::string> mGroups;
    // The pictures, one per specimen, and the cells they are drawn in.
    std::vector<LLPointer<LLUIImage>> mImages;
    std::vector<Cell>       mCells;
    LLScrollContainer*      mScroller = nullptr;
    // The two layouts, both kept; one is what the container scrolls.
    LLPanel*                mRowsPane = nullptr;
    Tiles*                  mTiles = nullptr;
    LLPanel*                mContent = nullptr;
    bool                    mRowsBuilt = false;
    ALEmptyState*           mEmpty = nullptr;
    std::string             mFilter;
    std::string             mChosen;
    std::vector<std::string> mSelection;
    std::string             mEmptyHeadline;
    std::string             mEmptySentence;
    S32                     mRowHeight;
    S32                     mLabelWidth;
    S32                     mCellWidth;
    S32                     mCellHeight;
    size_t                  mShown = 0;
    chose_signal_t          mChose;
    selection_signal_t      mSelectionChanged;
    drag_fn_t               mDragStart;
};
