/**
 * @file alxuiselection.h
 * @brief The one selection every pane of the XUI tool agrees about, keyed by name path.
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

#include "stdtypes.h"

#include <boost/signals2.hpp>

#include <string>
#include <string_view>
#include <vector>

class LLView;

// A selection and a hover, each a path of names from a root view down. A
// path holds no view, so it survives the tree it was taken from being
// rebuilt, and resolves against the new tree by the same names. The
// observers are the panes; none of them owns it.
class ALXUISelection
{
public:
    using path_t = std::vector<std::string>;
    using signal_t = boost::signals2::signal<void()>;

    // A step is the view's name, with "#n" appended for the nth sibling of
    // that name after the first, counting in creation order.
    static std::string step(std::string_view name, S32 ordinal);

    // The path of a view under a root; false when it is not under it. The
    // root's own path is empty.
    static bool pathOf(const LLView* view, const LLView* root, path_t& path);
    static LLView* resolve(LLView* root, const path_t& path);

    static std::string toString(const path_t& path);
    static path_t fromString(std::string_view text);

    bool hasSelection() const { return mHasSelection; }
    const path_t& selection() const { return mSelection; }
    void select(const path_t& path);
    void clearSelection();

    bool hasHover() const { return mHasHover; }
    const path_t& hover() const { return mHover; }
    void setHover(const path_t& path);
    void clearHover();

    boost::signals2::connection onSelectionChanged(const signal_t::slot_type& slot)
    {
        return mSelectionChanged.connect(slot);
    }
    boost::signals2::connection onHoverChanged(const signal_t::slot_type& slot)
    {
        return mHoverChanged.connect(slot);
    }

private:
    path_t      mSelection;
    path_t      mHover;
    signal_t    mSelectionChanged;
    signal_t    mHoverChanged;
    bool        mHasSelection = false;
    bool        mHasHover = false;
};
