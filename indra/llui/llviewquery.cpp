/**
 * @file llviewquery.cpp
 * @brief Implementation of view query class.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llview.h"
#include "lluictrl.h"
#include "llviewquery.h"

void LLQuerySorter::sort(LLView * parent, viewList_t &children) const {}

filterResult_t LLLeavesFilter::operator() (const LLView* const view, bool has_children) const
{
    return filterResult_t(!has_children, true);
}

filterResult_t LLRootsFilter::operator() (const LLView* const view, bool has_children) const
{
    return filterResult_t(true, false);
}

filterResult_t LLVisibleFilter::operator() (const LLView* const view, bool has_children) const
{
    return filterResult_t(view->getVisible(), view->getVisible());
}
filterResult_t LLEnabledFilter::operator() (const LLView* const view, bool has_children) const
{
    return filterResult_t(view->getEnabled(), view->getEnabled());
}
filterResult_t LLTabStopFilter::operator() (const LLView* const view, bool has_children) const
{
    return filterResult_t(view->isCtrl() && static_cast<const LLUICtrl*>(view)->hasTabStop(),
                        view->canFocusChildren());
}

filterResult_t LLCtrlFilter::operator() (const LLView* const view, bool has_children) const
{
    return filterResult_t(view->isCtrl(),true);
}

//
// LLViewQuery
//

viewList_t LLViewQuery::run(LLView* view) const
{
    viewList_t result;
    runInto(view, result);
    return result;
}

void LLViewQuery::runInto(LLView* view, viewList_t& result) const
{
    // prefilter gets immediate children of view
    filterResult_t pre = runFilters(view, !view->getChildList()->empty(), mPreFilters);
    if(!pre.first && !pre.second)
    {
        // not including ourselves or the children
        // nothing more to do
        return;
    }

    viewList_t filtered_children;
    filterResult_t post(true, true);
    if(pre.second)
    {
        // run filters on children
        filterChildren(view, filtered_children);
        // only run post filters if this element passed pre filters
        // so if you failed to pass the pre filter, you can't filter out children in post
        if (pre.first)
        {
            post = runFilters(view, !filtered_children.empty(), mPostFilters);
        }
    }

    if(pre.first && post.first)
    {
        result.push_back(view);
    }

    if(pre.second && post.second)
    {
        result.insert(result.end(), filtered_children.begin(), filtered_children.end());
    }
}

void LLViewQuery::filterChildren(LLView* parent_view, viewList_t & filtered_children) const
{
    // The copy exists so a sorter can reorder without disturbing the view's own
    // child list. Without a sorter there is nothing to reorder, and this walks
    // every node of the tree, so the list is only copied when it is going to be
    // sorted.
    const LLView::child_list_t* views = parent_view->getChildList();
    if (mSorterp)
    {
        viewList_t sorted(views->begin(), views->end());
        mSorterp->sort(parent_view, sorted);
        for (LLView* child : sorted)
        {
            runInto(child, filtered_children);
        }
        return;
    }

    for (LLView* child : *views)
    {
        runInto(child, filtered_children);
    }
}

filterResult_t LLViewQuery::runFilters(LLView * view, bool has_children, const filterList_t& filters) const
{
    filterResult_t result = filterResult_t(true, true);
    for (const LLQueryFilter* filter : filters)
    {
        filterResult_t filtered = (*filter)(view, has_children);
        result.first = result.first && filtered.first;
        result.second = result.second && filtered.second;
    }
    return result;
}
