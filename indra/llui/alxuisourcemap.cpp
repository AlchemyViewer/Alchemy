/**
 * @file alxuisourcemap.cpp
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

#include "linden_common.h"

#include "alxuisourcemap.h"

#include "alxuicatalog.h"
#include "llview.h"

#include <vector>

void ALXUISourceMap::clear()
{
    mOrigins.clear();
    mByLine.clear();
}

void ALXUISourceMap::build(const LLView* root, LLXMLNodePtr root_node)
{
    clear();
    if (!root || root_node.isNull())
    {
        return;
    }
    record(root, root_node);
    pairChildren(root, root_node.get());
}

void ALXUISourceMap::record(const LLView* view, const LLXMLNodePtr& node)
{
    Origin& origin = mOrigins[view];
    origin.node = node;
    origin.line = node->getLineNumber();
    origin.tag = node->getName()->mString;
    LLStringUtil::toLower(origin.tag);
    mByLine.emplace(origin.line, view);
}

void ALXUISourceMap::pairChildren(const LLView* parent, const LLXMLNode* node)
{
    // The views in the order they were created, which is the reverse of
    // the list they are kept in.
    std::vector<const LLView*> created;
    const LLView::child_list_t& children = *parent->getChildList();
    created.assign(children.rbegin(), children.rend());
    std::vector<bool> claimed(created.size(), false);

    size_t cursor = 0;
    for (LLXMLNodePtr child = node->getFirstChild(); child.notNull(); child = child->getNextSibling())
    {
        std::string tag = child->getName()->mString;
        LLStringUtil::toLower(tag);
        // A dotted element left behind is a parameter the parser refused,
        // and a tag no registry builds made nothing either way.
        if (tag.find('.') != std::string::npos || !ALXUICatalog::isWidgetTag(tag))
        {
            continue;
        }
        std::string name;
        if (!child->getAttributeString("name", name))
        {
            name = "unnamed";
        }

        // The next unclaimed view of that name from where the last pair
        // left off, else from the start: a widget built out of order is
        // still that widget.
        size_t found = created.size();
        for (size_t i = cursor; i < created.size(); ++i)
        {
            if (!claimed[i] && created[i]->getName() == name)
            {
                found = i;
                break;
            }
        }
        if (found == created.size())
        {
            for (size_t i = 0; i < cursor; ++i)
            {
                if (!claimed[i] && created[i]->getName() == name)
                {
                    found = i;
                    break;
                }
            }
        }
        if (found == created.size())
        {
            continue;
        }
        claimed[found] = true;
        cursor = found + 1;
        record(created[found], child);
        pairChildren(created[found], child.get());
    }
}

const ALXUISourceMap::Origin* ALXUISourceMap::find(const LLView* view) const
{
    auto it = mOrigins.find(view);
    return it == mOrigins.end() ? nullptr : &it->second;
}

const LLView* ALXUISourceMap::viewAtLine(S32 line) const
{
    auto it = mByLine.upper_bound(line);
    if (it == mByLine.begin())
    {
        return nullptr;
    }
    --it;
    return it->second;
}
