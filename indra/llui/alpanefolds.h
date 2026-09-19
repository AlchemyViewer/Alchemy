/**
 * @file alpanefolds.h
 * @brief The regions of a window that fold away, and the buttons that fold them
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

#include "llsd.h"

#include <string>
#include <string_view>
#include <vector>

#include <boost/signals2.hpp>

class ALDockPanel;
class LLButton;
class LLLayoutPanel;
class LLView;

// The regions of a window that fold away and are dragged to a size, each
// with the button in the window's bar that folds it and the key its state
// is kept under, and that can be taken out into a window of their own. A
// window laid out as XUI Studio is has three of them and the same
// questions about each: fold it, is it folded, how wide is it, press the
// button in while it shows, take it out and put it back, write all that
// down and read it back. One answer to each, for every such window.
//
// A region taken out is moved in the tree, not copied, so every pointer
// into it keeps working; what stops working is every getChild that goes
// through it from the window, so a window with such regions holds
// pointers to what is in them.
class ALPaneFolds
{
public:
    struct Pane
    {
        // What the state calls it: fold_<key>, dim_<key>, out_<key>.
        std::string mKey;
        // The layout panel, and the toggle button that folds it, if any.
        std::string mPanel;
        std::string mButton;
        // What the window is called while the region is out; empty for a
        // region that stays put.
        std::string mTitle;
    };

    // The window the panes live in, searched once; each button's press
    // folds its region from then on, and each region with a title is put
    // in a pane that can leave.
    void bind(LLView* window, std::vector<Pane> panes);

    // A region by its key or its panel's name.
    void toggle(std::string_view pane);
    void setCollapsed(std::string_view pane, bool collapsed);
    bool collapsed(std::string_view pane) const;
    // The size a drag on the bar gives it, and the same asked for.
    S32 dim(std::string_view pane) const;
    void setDim(std::string_view pane, S32 dim);
    // Every region's size, in the order given, for a window that watches
    // for a drag ending.
    std::vector<S32> dims() const;

    // A region out in a window of its own, and back; and every one back,
    // which a window does before it closes, since a pane left in a window
    // it does not own goes down with it.
    bool out(std::string_view pane) const;
    void toggleOut(std::string_view pane);
    void dockAll();

    // The buttons pressed in while their regions show.
    void refreshButtons();

    // Into and out of a saved state.
    void save(LLSD& state) const;
    void load(const LLSD& state);

    // A region folded or unfolded, from a button or a call.
    typedef boost::signals2::signal<void()> changed_signal_t;
    boost::signals2::connection onChanged(const changed_signal_t::slot_type& cb) { return mChanged.connect(cb); }

private:
    struct Bound
    {
        Pane mPane;
        LLLayoutPanel* mPanel = nullptr;
        LLButton* mButton = nullptr;
        ALDockPanel* mDock = nullptr;
    };

    const Bound* find(std::string_view pane) const;
    Bound* find(std::string_view pane);

    std::vector<Bound> mPanes;
    changed_signal_t mChanged;
};
