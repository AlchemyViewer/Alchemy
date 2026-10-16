/**
 * @file alcolortablepanel.h
 * @brief The colour table as rows to edit: every named colour, whose it is, and the way back
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

#include "llframetimer.h"
#include "llpanel.h"
#include "v4color.h"

#include <functional>
#include <string>
#include <vector>

#include <boost/signals2.hpp>

class ALPropertyGrid;
class LLCheckBoxCtrl;
class LLFilterEditor;

// The colour table as rows: every name the skins declare and every one a
// person added, with the colour it comes to, the file that declared it
// and the colour it refers to where it refers to one. A row a person
// changed is theirs, and the way back on it puts the skin's colour in
// force again. Under headings by the file that declares each, with the
// changed ones first, so a theme's own colours read as a list.
//
// The same rows wherever a theme's colours are edited: the colour
// settings window is this panel.
// The table is the document: this reads it, writes it one colour at a
// time, and says which one it wrote, so a window keeping undo can. And
// while the panel is up, a change to the table reaches the person's
// colours file a moment after the changing stops, so a crash loses
// nothing but the last moment.
class ALColorTablePanel : public LLPanel
{
public:
    AL_VIEW_TYPE(ALColorTablePanel, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Optional<S32> label_width;
        Params();
    };

    // The rows read from the table again. Called by the panel itself when
    // the table has changed since it last read; a caller with a reason to
    // read sooner calls it.
    void refresh() override;

    // The rows narrowed to the names carrying this, and to the ones a
    // person changed.
    void setFilter(const std::string& text);
    void setOnlyChanged(bool only);
    bool onlyChanged() const;
    // A colour brought into view: the filter is set to its name.
    void showColor(const std::string& name);

    // A colour set or put back by this panel, said after the table has it.
    typedef boost::signals2::signal<void(const std::string&)> changed_signal_t;
    boost::signals2::connection onChanged(const changed_signal_t::slot_type& cb) { return mChanged.connect(cb); }

    // Who paints with each colour, by the colour's name, for a caller
    // that knows: the row says how many and marks them in its gutter,
    // and the mark pressed is said, with the name, to whoever would show
    // them.
    typedef std::function<std::vector<std::string>(const std::string&)> users_t;
    void setUsers(users_t users);
    typedef boost::signals2::signal<void(const std::string&)> gutter_signal_t;
    boost::signals2::connection onGutter(const gutter_signal_t::slot_type& cb) { return mGutter.connect(cb); }
    // The mark beside a colour's row, for a menu to open beside.
    LLView* gutterFor(const std::string& name) const;

    // How a colour is written as a row's value, and read back from one:
    // four numbers with commas between, which is what the picker gives.
    static std::string valueText(const LLColor4& color);

    void draw() override;

protected:
    friend class LLUICtrlFactory;
    ALColorTablePanel(const Params& p);

private:
    void fill();
    void onFieldCommit(const std::string& name, const std::string& value);
    void onFieldRemove(const std::string& name);

    ALPropertyGrid*     mGrid = nullptr;
    LLFilterEditor*     mFilter = nullptr;
    LLCheckBoxCtrl*     mOnlyChanged = nullptr;
    // The table as it was when the rows were last read.
    U32                 mSeenGeneration = 0;
    // A change waiting to be written to disk, and since when.
    bool                mUnwritten = false;
    LLFrameTimer        mSinceChange;
    // The headings, which are the files: read again when the files do.
    std::vector<std::string> mFiles;
    changed_signal_t    mChanged;
    gutter_signal_t     mGutter;
    users_t             mUsers;
};
