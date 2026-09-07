/**
 * @file alxuiselection.cpp
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

#include "linden_common.h"

#include "alxuiselection.h"

#include "llview.h"

#include <algorithm>

// static
std::string ALXUISelection::step(std::string_view name, S32 ordinal)
{
    std::string text(name);
    if (ordinal > 0)
    {
        text += '#';
        text += std::to_string(ordinal);
    }
    return text;
}

// static
bool ALXUISelection::pathOf(const LLView* view, const LLView* root, path_t& path)
{
    path.clear();
    for (const LLView* cur = view; cur != root; cur = cur->getParent())
    {
        const LLView* parent = cur ? cur->getParent() : nullptr;
        if (!parent)
        {
            path.clear();
            return false;
        }
        // Children sit at the front of the list as they are added, so the
        // ones created before this view are the ones after it.
        S32 ordinal = 0;
        const LLView::child_list_t& siblings = *parent->getChildList();
        auto it = std::find(siblings.begin(), siblings.end(), cur);
        for (++it; it != siblings.end(); ++it)
        {
            if ((*it)->getName() == cur->getName())
            {
                ++ordinal;
            }
        }
        path.push_back(step(cur->getName(), ordinal));
    }
    std::reverse(path.begin(), path.end());
    return true;
}

// static
LLView* ALXUISelection::resolve(LLView* root, const path_t& path)
{
    LLView* cur = root;
    for (const std::string& s : path)
    {
        if (!cur)
        {
            return nullptr;
        }
        std::string_view name(s);
        S32 wanted = 0;
        if (const size_t hash = name.rfind('#'); hash != std::string_view::npos)
        {
            wanted = (S32)std::atoi(s.c_str() + hash + 1);
            name = name.substr(0, hash);
        }
        LLView* found = nullptr;
        S32 seen = 0;
        const LLView::child_list_t& children = *cur->getChildList();
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            if ((*it)->getName() == name && seen++ == wanted)
            {
                found = *it;
                break;
            }
        }
        cur = found;
    }
    return cur;
}

// static
std::string ALXUISelection::toString(const path_t& path)
{
    std::string text;
    for (const std::string& s : path)
    {
        if (!text.empty())
        {
            text += '/';
        }
        text += s;
    }
    return text;
}

// static
ALXUISelection::path_t ALXUISelection::fromString(std::string_view text)
{
    path_t path;
    while (!text.empty())
    {
        const size_t slash = text.find('/');
        path.emplace_back(text.substr(0, slash));
        if (slash == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(slash + 1);
    }
    return path;
}

void ALXUISelection::select(const path_t& path)
{
    if (mHasSelection && mSelection == path)
    {
        return;
    }
    mSelection = path;
    mHasSelection = true;
    mSelectionChanged();
}

void ALXUISelection::clearSelection()
{
    if (!mHasSelection)
    {
        return;
    }
    mSelection.clear();
    mHasSelection = false;
    mSelectionChanged();
}

void ALXUISelection::setHover(const path_t& path)
{
    if (mHasHover && mHover == path)
    {
        return;
    }
    mHover = path;
    mHasHover = true;
    mHoverChanged();
}

void ALXUISelection::clearHover()
{
    if (!mHasHover)
    {
        return;
    }
    mHover.clear();
    mHasHover = false;
    mHoverChanged();
}
