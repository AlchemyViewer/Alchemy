/**
 * @file alxuisourcemap.h
 * @brief Which XML element each built view came from, and which views came from none.
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

#include "llxmlnode.h"

#include <map>
#include <string>

#include <boost/unordered_map.hpp>

class LLView;

// A view does not remember the element it was built from. This walks a
// built tree and the layered document it was built from together: the
// factory creates a node's widget children in document order and names
// them from the name attribute, so a parallel walk pairs them by name.
// Views a widget builds for itself, which no element describes, pair with
// nothing, and that is how the tool tells the two apart.
class ALXUISourceMap
{
public:
    struct Origin
    {
        LLXMLNodePtr    node;
        S32             line = 0;
        std::string     tag;
    };

    // The parser has consumed the document's parameter elements by the
    // time a build returns, so what remains under each element is its
    // widget children, in the order they were created.
    void build(const LLView* root, LLXMLNodePtr root_node);
    void clear();

    const Origin* find(const LLView* view) const;
    bool isFromXML(const LLView* view) const { return find(view) != nullptr; }

    // The view whose element starts on that line, else the nearest one
    // starting before it; null when none does.
    const LLView* viewAtLine(S32 line) const;

    size_t size() const { return mOrigins.size(); }

private:
    void pairChildren(const LLView* parent, const LLXMLNode* node);
    void record(const LLView* view, const LLXMLNodePtr& node);

    boost::unordered_map<const LLView*, Origin> mOrigins;
    std::map<S32, const LLView*>                mByLine;
};
