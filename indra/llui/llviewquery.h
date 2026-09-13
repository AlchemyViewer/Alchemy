/**
 * @file llviewquery.h
 * @brief Query algorithm for flattening and filtering the view hierarchy.
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

#ifndef LL_LLVIEWQUERY_H
#define LL_LLVIEWQUERY_H

#include <vector>

#include "llsingleton.h"
#include "llui.h"

class LLView;

// A vector: this is filled by a walk of the whole view tree, so the list it
// used to be paid a node allocation for every view that passed a filter, and
// a sentinel node for every view that was asked. A leaf now costs nothing.
using viewList_t = std::vector<LLView *>;
using filterResult_t = std::pair<bool, bool>;

// Abstract base class for all query filters.
//
// The second argument used to be the children themselves, and every filter in
// the tree asked it one question: are there any? Handing over the container to
// have emptiness read off it also tied the query's result type to the type of a
// view's child list, since a prefilter is passed one and a postfilter the
// other.
class LLQueryFilter
{
public:
    virtual ~LLQueryFilter() {};
    virtual filterResult_t operator() (const LLView* const view, bool has_children) const = 0;
};

class LLQuerySorter
{
public:
    virtual ~LLQuerySorter() {};
    virtual void sort(LLView * parent, viewList_t &children) const;
};

class LLLeavesFilter : public LLQueryFilter, public LLSingleton<LLLeavesFilter>
{
    LLSINGLETON_EMPTY_CTOR(LLLeavesFilter);
    /*virtual*/ filterResult_t operator() (const LLView* const view, bool has_children) const override;
};

class LLRootsFilter : public LLQueryFilter, public LLSingleton<LLRootsFilter>
{
    LLSINGLETON_EMPTY_CTOR(LLRootsFilter);
    /*virtual*/ filterResult_t operator() (const LLView* const view, bool has_children) const override;
};

class LLVisibleFilter : public LLQueryFilter, public LLSingleton<LLVisibleFilter>
{
    LLSINGLETON_EMPTY_CTOR(LLVisibleFilter);
    /*virtual*/ filterResult_t operator() (const LLView* const view, bool has_children) const override;
};

class LLEnabledFilter : public LLQueryFilter, public LLSingleton<LLEnabledFilter>
{
    LLSINGLETON_EMPTY_CTOR(LLEnabledFilter);
    /*virtual*/ filterResult_t operator() (const LLView* const view, bool has_children) const override;
};

class LLTabStopFilter : public LLQueryFilter, public LLSingleton<LLTabStopFilter>
{
    LLSINGLETON_EMPTY_CTOR(LLTabStopFilter);
    /*virtual*/ filterResult_t operator() (const LLView* const view, bool has_children) const override;
};

class LLCtrlFilter : public LLQueryFilter, public LLSingleton<LLCtrlFilter>
{
    LLSINGLETON_EMPTY_CTOR(LLCtrlFilter);
    /*virtual*/ filterResult_t operator() (const LLView* const view, bool has_children) const override;
};

// Algorithm for flattening
class LLViewQuery
{
public:
    using filterList_t = std::vector<const LLQueryFilter*>;
    using filterList_iter_t = filterList_t::iterator;
    using filterList_const_iter_t = filterList_t::const_iterator;

    LLViewQuery() : mPreFilters(), mPostFilters(), mSorterp() {}
    virtual ~LLViewQuery() {}

    void addPreFilter(const LLQueryFilter* prefilter) { mPreFilters.push_back(prefilter); }
    void addPostFilter(const LLQueryFilter* postfilter) { mPostFilters.push_back(postfilter); }
    const filterList_t & getPreFilters() const { return mPreFilters; }
    const filterList_t & getPostFilters() const { return mPostFilters; }

    void setSorter(const LLQuerySorter* sorter) { mSorterp = sorter; }
    const LLQuerySorter* getSorter() const { return mSorterp; }

    viewList_t run(LLView * view) const;
    // syntactic sugar
    viewList_t operator () (LLView * view) const { return run(view); }

    // override this method to provide iteration over other types of children
    virtual void filterChildren(LLView * view, viewList_t& filtered_children) const;

private:

    // The filter list by reference. It was taken by value, and this runs at
    // every node of a recursive walk over a whole view tree, so each node
    // allocated a copy of it to read it -- for nothing. A folder tree made that
    // hundreds of thousands of allocations.
    filterResult_t runFilters(LLView * view, bool has_children, const filterList_t& filters) const;

    // What run() is, appending rather than returning, so a node of the walk
    // does not build a container to have it emptied into its parent's.
    void runInto(LLView * view, viewList_t& result) const;

    filterList_t mPreFilters;
    filterList_t mPostFilters;
    const LLQuerySorter* mSorterp;
};


#endif // LL_LLVIEWQUERY_H
