/**
 * @file alpanelist.h
 * @brief A list whose rows are places: the keys that go to one and back.
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

#include "aldraggesture.h"
#include "llframetimer.h"
#include "llscrolllistcell.h"
#include "llscrolllistctrl.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A studio pane's list -- problems, references, places found, an outline --
// whose rows are places to go. Choosing a row shows its place and the arrow
// keys walk on through them; these are the keys past that: return and a
// double-click go to the place chosen (setGo), escape goes back to where the
// typing was (setBack), and left and right fold and open a row that holds
// others. A panel above the list takes escape to mean nothing is to have
// the keyboard before its window hears of it, so the list is asked first.
//
// And what is in it can be taken away (setCopyable): a right-click's menu
// copies the cell under the pointer, the rows chosen or every row, and
// Control-C the rows chosen -- as a table to read, the columns lined up
// under their headings.
//
// Rows in groups, under a heading each: a sort by a column sorts within
// each group, the headings staying over their own, where the caller says
// which group a row is of.
class ALEmptyState;

class ALPaneList : public LLScrollListCtrl
{
public:
    AL_VIEW_TYPE(ALPaneList, LLScrollListCtrl);

    struct Params : public LLInitParam::Block<Params, LLScrollListCtrl::Params>
    {
        Params();
    };

    // Rows that hold others fold, by the owner's rule, since the tree is
    // the owner's to lay out: left on the row chosen asks it folded, right
    // asks it open, and a press on its arrow -- wherever the owner says a
    // row's arrow is -- asks it turned.
    typedef std::function<void(const LLSD& value, std::optional<bool> folded)> fold_t;
    typedef std::function<bool(const LLScrollListItem* row, S32 x)>         arrow_t;
    void setFold(fold_t fold, arrow_t arrow_at)
    {
        mFold    = std::move(fold);
        mArrowAt = std::move(arrow_at);
    }

    // Where the row chosen goes: return and a double-click; and back to
    // where the typing was: escape. Unset, those keys are the list's own.
    void setGo(std::function<void()> go);
    void setBack(std::function<void()> back) { mBack = std::move(back); }
    // What Space does to the row chosen -- ticks its box, where the rows
    // have one -- rather than find a row by its first letter.
    void setSpace(std::function<void()> space) { mSpace = std::move(space); }

    // What the list says while it has no rows: a headline and a sentence
    // where the rows would be (ALEmptyState). Empty words, nothing.
    void setEmpty(const std::string& headline, const std::string& sentence);
    // Whether it says so now: no rows, and words to say; and its headline.
    bool               saysEmpty() const;
    const std::string& emptyWords() const { return mEmptyHeadline; }

    // Copying from the list, off unless asked for: a list with a menu of
    // its own would bring up both.
    void setCopyable(bool copyable) { mCopyable = copyable; }
    // What the rows are, said over them as they are copied: asked then,
    // since what the list shows may have changed since it was set.
    void setCopyCaption(std::function<std::string()> caption) { mCopyCaption = std::move(caption); }
    // The rows as the copy has them: a line each, the columns lined up
    // under their headings, a column nothing is in left out; rows nobody
    // can choose -- a heading, a count of more -- left out too.
    std::string asText(const std::vector<LLScrollListItem*>& rows);
    // What the menu's items do, by name: "copy_cell" the cell last
    // right-clicked, "copy" the rows chosen, "copy_all" every row,
    // "select_all"; and whether each can be done now.
    void copyAction(const std::string& action);
    bool copyActionEnabled(const std::string& action) const;

    // Which group a row is of, and whether it is the group's heading, for
    // a sort that keeps them together; nothing, and the list sorts as any
    // other. A group's number orders the groups whichever way a column
    // sorts.
    typedef std::function<void(const LLScrollListItem*, S32& group, bool& heading)> group_t;
    void setGrouping(group_t grouping);
    // How two rows of a group compare by a column: below zero for the
    // first before the second, as the column ascends. Nothing compares the
    // cells' words.
    typedef std::function<S32(S32 column, const LLScrollListItem*, const LLScrollListItem*)> compare_t;
    void setComparison(compare_t comparison);

    // A press on a row that moves on with the button held, past the
    // viewer's dead zone for a drag: offered as a drag of what is chosen,
    // with the value of the row pressed -- the rows may be made again
    // while the button is held -- which the starter begins with the viewer's
    // drag tool and says it did. Not begun, or with no starter, the list
    // chooses the rows the pointer passes, as any list does. With a
    // starter, a plain press on one of several rows chosen keeps them all
    // chosen, for a drag of them all; let go without one, it chooses its
    // row alone, as a press would have.
    typedef std::function<bool(const LLSD& pressed)> drag_t;
    void setDragStarter(drag_t starter) { mDragStarter = std::move(starter); }

    // What the viewer's drag tool carries over a row, or drops on it: the
    // handler is given the row's value -- undefined over no row -- says in
    // `accept` what it would do, and does it on the drop; and it answers
    // the value of the row the drop would go to, which is lit, or an
    // undefined one for none. Near the list's top or bottom the list
    // scrolls, for a row out of sight; the light goes with the pointer.
    typedef std::function<LLSD(const LLSD& row, MASK mask, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip)>
        drop_t;
    void setDropHandler(drop_t handler) { mDropHandler = std::move(handler); }

    // A row its owner knows by a key: what stays the same row while its
    // words change -- a problem whose line moved, a place whose line was
    // edited. Its value, which choosing it reads; whether it can be
    // chosen; and its cells, by column.
    struct Row
    {
        std::string                           key;
        LLSD                                  value;
        bool                                  enabled = true;
        std::vector<LLScrollListCell::Params> cells;
    };
    // The rows afresh, in order, each key once. A row whose key was there
    // is the same row, its cells made again only where they say something
    // else and put where the order puts it; a key not there before is a
    // row made, and one not given any more a row taken out. So a check
    // that moved some lines, a filter narrowed or widened, a symbol added,
    // makes only what changed. Only a list holding rows setRows did not
    // put in, or given a key twice, is made again whole. Either way the
    // rows chosen, and the row at the top of the view, are kept by key.
    void setRows(std::vector<Row> rows);
    // A row's key, empty for one setRows did not put in; and the row with
    // a key, or null.
    const std::string& keyOf(const LLScrollListItem* item) const;
    LLScrollListItem*  rowWithKey(std::string_view key) const;
    // A row's tip, made as the pointer rests on it rather than carried by
    // every cell of every row. Where it says nothing, a cell's own tip.
    void setRowTip(std::function<std::string(const LLScrollListItem*)> tip) { mRowTip = std::move(tip); }

    ~ALPaneList() override;

    void clearRows() override;
    bool handleKeyHere(KEY key, MASK mask) override;
    bool handleUnicodeCharHere(llwchar uni_char) override;
    // Copyable, the row under the pointer is chosen, unless the click is in
    // a choice already made, and the copying menu opens.
    bool handleRightMouseDown(S32 x, S32 y, MASK mask) override;
    // The Edit menu's commands are the list's while it has the keyboard,
    // as they are a text's: else they are whatever last took them, and
    // with a prim chosen in the build tools that is the world's selection,
    // whose Delete -- on the plain key, which the menu hears before the
    // list does -- deletes the prim.
    void onFocusReceived() override;
    void onFocusLost() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    void onMouseCaptureLost() override;
    bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept,
                           std::string& tooltip) override;
    // Over the rows, a row's own tip; over none of them, the list's own,
    // which a plain list only ever shows as its hidden column heading's.
    bool handleToolTip(S32 x, S32 y, MASK mask) override;
    void draw() override;

protected:
    friend class LLUICtrlFactory;
    ALPaneList(const Params& p);

private:
    S32 compare(S32 column, const LLScrollListItem* a, const LLScrollListItem* b);
    void connectSort();

    void copyRows(const std::vector<LLScrollListItem*>& rows);

    // What a cell says, as one number to compare: its kind, its words, its
    // tip, its face and colours.
    static size_t saidBy(const LLScrollListCell::Params& cell);
    // A row put in, and one made again where it stands.
    LLScrollListItem* addKeyed(Row& row);
    void              rewrite(LLScrollListItem* item, Row& row, std::vector<size_t>& said);

    struct Keyed
    {
        LLScrollListItem*   item = nullptr;
        // What each cell said when last made.
        std::vector<size_t> said;
    };
    boost::unordered_flat_map<std::string, Keyed, ll::string_hash, std::equal_to<>> mKeyed;
    boost::unordered_flat_map<const LLScrollListItem*, std::string>                    mKeyOf;
    // The keys in the order they were given.
    std::vector<std::string>                                                           mOrder;
    std::function<std::string(const LLScrollListItem*)>                                mRowTip;

    ALEmptyState*               mEmpty = nullptr;
    std::string                 mEmptyHeadline;
    fold_t                      mFold;
    arrow_t                     mArrowAt;
    std::function<void()>       mGo;
    std::function<void()>       mBack;
    std::function<void()>       mSpace;
    // A double-click went, and its release is not a choice of its own.
    bool                        mWentByClick = false;
    bool                        mCopyable = false;
    std::function<std::string()> mCopyCaption;
    // The cell last right-clicked, for the copy meant to be pasted into a
    // line of code rather than read; and the menu that copies it.
    std::string                 mMenuCell;
    LLHandle<LLView>            mCopyMenu;
    group_t                     mGrouping;
    compare_t                   mComparison;
    boost::signals2::connection mSortConnection;
    drag_t                      mDragStarter;
    ALDragGesture               mPress;
    LLSD                        mPressed;
    drop_t                      mDropHandler;
    // A row a step, and no faster, while a drag rests at an edge; and
    // whether a row is lit for a drop, until the pointer leaves.
    LLFrameTimer                mDropScroll;
    bool                        mDropLit = false;
};
