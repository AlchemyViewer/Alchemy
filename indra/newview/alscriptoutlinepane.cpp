/**
 * @file alscriptoutlinepane.cpp
 * @brief Script Studio's outline: what a script declares, as a tree, filtered, sorted and folded, following the caret.
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

#include "llviewerprecompiledheaders.h"

#include "alfloaterscriptstudio.h"

#include "alpanelist.h"
#include "alscriptstudioplaces.h"
#include "alstringmatch.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfontgl.h"
#include "llscrolllistcell.h"
#include "llscrolllistcolumn.h"
#include "llscrolllistitem.h"

#include <functional>

using ALScriptPlaces::rangeOf;

void ALFloaterScriptStudio::refreshOutline(Doc& doc)
{
    if (&doc != active())
    {
        return;
    }
    // The symbols as a tree, each under what holds it -- the outline is
    // flat, each entry after its holder one deeper -- keyed by the names
    // down to it, so that a fold outlives a check that numbers them anew.
    const size_t count = doc.outline.size();
    std::vector<std::vector<size_t>> children(count);
    std::vector<size_t>              roots;
    mOutlineKeys.assign(count, std::string());
    mOutlineParents.assign(count, false);
    {
        std::vector<size_t> holders;
        for (size_t i = 0; i < count; ++i)
        {
            const S32 depth = doc.outline[i].depth;
            while (!holders.empty() && doc.outline[holders.back()].depth >= depth)
            {
                holders.pop_back();
            }
            if (holders.empty())
            {
                roots.push_back(i);
                mOutlineKeys[i] = doc.outline[i].name;
            }
            else
            {
                children[holders.back()].push_back(i);
                mOutlineParents[holders.back()] = true;
                mOutlineKeys[i]                 = mOutlineKeys[holders.back()] + "\x1f" + doc.outline[i].name;
            }
            holders.push_back(i);
        }
    }
    // In the order asked for, under each holder: as written, by name, or
    // by kind and then name.
    const std::string sort = mOutlineSort ? mOutlineSort->getValue().asString() : std::string("order");
    const auto        before = [&](size_t a, size_t b) {
        const ALScriptOutlineEntry& x = doc.outline[a];
        const ALScriptOutlineEntry& y = doc.outline[b];
        if (sort == "kind" && x.kind != y.kind)
        {
            return static_cast<S32>(x.kind) < static_cast<S32>(y.kind);
        }
        if (sort == "name" || sort == "kind")
        {
            const S32 said = LLStringUtil::compareDict(x.name, y.name);
            if (said != 0)
            {
                return said < 0;
            }
        }
        return a < b;
    };
    std::string filter = mOutlineFilter ? mOutlineFilter->getText() : std::string();
    LLStringUtil::trim(filter);
    // The rows: through the filter, every symbol with the letters, flat;
    // else the tree, down to what is folded shut.
    struct Row
    {
        size_t index = 0;
        S32    depth = 0;
    };
    std::vector<Row> rows;
    if (!filter.empty())
    {
        std::vector<size_t> matched;
        for (size_t i = 0; i < count; ++i)
        {
            if (ALStringMatch::containsNoCase(doc.outline[i].name, filter))
            {
                matched.push_back(i);
            }
        }
        std::stable_sort(matched.begin(), matched.end(), before);
        for (const size_t i : matched)
        {
            rows.push_back({ i, 0 });
        }
    }
    else
    {
        std::function<void(std::vector<size_t>, S32)> walk = [&](std::vector<size_t> level, S32 depth) {
            std::stable_sort(level.begin(), level.end(), before);
            for (const size_t i : level)
            {
                rows.push_back({ i, depth });
                if (!children[i].empty() && !doc.outlineFolded.contains(mOutlineKeys[i]))
                {
                    walk(children[i], depth + 1);
                }
            }
        };
        walk(roots, 0);
    }
    // What the rows say. A check comes at every pause in typing and most
    // change nothing the outline shows; the list is only made again where
    // something did, and then keeps its scroll, so that whoever is
    // reading down it is not sent back to the top.
    const std::string open   = getString("ArrowOpen");
    const std::string folded = getString("ArrowFolded");
    std::vector<std::string> said;
    said.reserve(rows.size() + 1);
    for (const Row& row : rows)
    {
        const ALScriptOutlineEntry& entry = doc.outline[row.index];
        const bool parent = filter.empty() && mOutlineParents[row.index];
        const std::string arrow = !parent ? std::string("   ") : doc.outlineFolded.contains(mOutlineKeys[row.index]) ? folded : open;
        said.push_back(std::string(static_cast<size_t>(row.depth) * 4, ' ') + arrow + entry.name + "|" +
                       std::to_string(static_cast<S32>(entry.kind)) + "|" + std::to_string(row.index) + "|" + entry.detail);
    }
    said.push_back(doc.id);
    if (said != mOutlineSaid)
    {
        mOutlineSaid     = said;
        const S32 scroll = mOutline->getScrollPos();
        mOutline->deleteAllItems();
        for (size_t r = 0; r < rows.size(); ++r)
        {
            const ALScriptOutlineEntry& entry = doc.outline[rows[r].index];
            LLSD                        row;
            row["value"]                = static_cast<S32>(rows[r].index);
            row["columns"][0]["column"] = "icon";
            row["columns"][0]["type"]   = "icon";
            row["columns"][0]["value"]  = ALScriptStudioWords::imageNameOf(entry.kind);
            row["columns"][0]["tool_tip"] = kindName(entry.kind);
            row["columns"][1]["column"] = "symbol";
            // Nested under what holds it, with the arrow that folds what
            // it holds; what it is, and its declaration, on the mouse.
            row["columns"][1]["value"]    = said[r].substr(0, said[r].find('|'));
            row["columns"][1]["tool_tip"] = entry.detail.empty() ? kindName(entry.kind) : kindName(entry.kind) + "\n" + entry.detail;
            mOutline->addElement(row);
        }
        mOutline->setScrollPos(scroll);
    }
    mOutline->setCommentText(doc.outline.empty() ? getString(doc.loaded ? "NoOutline" : "NoOutlineYet")
                             : rows.empty()      ? getString("OutlineNoMatch")
                                                 : LLStringUtil::null);
    refreshBreadcrumb(doc);
    followCaretInOutline(doc);
}

bool ALFloaterScriptStudio::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // An arrow in the outline folds its symbol.
    if (mask == MASK_NONE && mOutline && mOutline->isInVisibleChain())
    {
        S32 lx = 0, ly = 0;
        localPointToOtherView(x, y, &lx, &ly, mOutline);
        size_t index = 0;
        if (mOutline->pointInView(lx, ly) && outlineArrowAt(lx, ly, index))
        {
            foldOutline(index);
            return true;
        }
    }
    return ALStudioFloater::handleMouseDown(x, y, mask);
}

bool ALFloaterScriptStudio::outlineArrowAt(S32 x, S32 y, size_t& index)
{
    LLScrollListItem* item = mOutline->hitItem(x, y);
    if (!item)
    {
        return false;
    }
    index = static_cast<size_t>(item->getValue().asInteger());
    if (index >= mOutlineParents.size() || !mOutlineParents[index])
    {
        return false;
    }
    // The arrow comes after the symbol's indent, from the name column's
    // edge: as far as the indent and the arrow go.
    const LLScrollListCell* name = item->getColumn(1);
    const std::string       text = name ? name->getValue().asString() : std::string();
    const size_t            arrow_end = text.find_first_not_of(' ') == std::string::npos ? 0 : text.find_first_not_of(' ') + getString("ArrowOpen").size();
    const LLScrollListColumn* icon = mOutline->getColumn("icon");
    const S32 left  = mOutline->getItemListRect().mLeft + (icon ? icon->getWidth() : 0) + mOutline->getColumnPadding();
    const S32 right = left + LLFontGL::getFontSansSerifSmall()->getWidth(text.substr(0, arrow_end)) + 4;
    return x >= left - 2 && x <= right;
}

void ALFloaterScriptStudio::foldOutline(size_t index, std::optional<bool> folded)
{
    Doc* doc = active();
    if (!doc || index >= mOutlineKeys.size())
    {
        return;
    }
    const std::string& key   = mOutlineKeys[index];
    const bool         shut  = doc->outlineFolded.contains(key);
    const bool         want  = folded.value_or(!shut);
    if (!mOutlineParents[index] || want == shut)
    {
        // Left on a symbol with nothing to fold: to what holds it, shown
        // as any row walked to is.
        if (folded.has_value() && *folded)
        {
            const size_t at = key.rfind('\x1f');
            if (at != std::string::npos)
            {
                const std::string holder = key.substr(0, at);
                for (size_t i = 0; i < mOutlineKeys.size(); ++i)
                {
                    if (mOutlineKeys[i] == holder && mOutline->selectByValue(LLSD(static_cast<S32>(i))))
                    {
                        mOutline->scrollToShowSelected();
                        onOutlineChosen(false);
                        break;
                    }
                }
            }
        }
        return;
    }
    if (want)
    {
        doc->outlineFolded.insert(key);
    }
    else
    {
        doc->outlineFolded.erase(key);
    }
    refreshOutline(*doc);
    mOutline->selectByValue(LLSD(static_cast<S32>(index)));
}

void ALFloaterScriptStudio::followCaretInOutline(Doc& doc)
{
    if (&doc != active())
    {
        return;
    }
    // The innermost symbol the caret is in, as the breadcrumb found it.
    if (doc.crumbPath.empty())
    {
        mOutline->deselectAllItems(true);
        return;
    }
    LLScrollListItem* now = mOutline->getFirstSelected();
    for (auto step = doc.crumbPath.rbegin(); step != doc.crumbPath.rend(); ++step)
    {
        const S32 index = static_cast<S32>(*step);
        if (now && now->getValue().asInteger() == index)
        {
            return;
        }
        if (mOutline->selectByValue(LLSD(index)))
        {
            mOutline->scrollToShowSelected();
            return;
        }
    }
    mOutline->deselectAllItems(true);
}

void ALFloaterScriptStudio::onOutlineChosen(bool to_editor)
{
    Doc*              doc  = active();
    LLScrollListItem* item = mOutline->getFirstSelected();
    if (!doc || !item)
    {
        return;
    }
    const size_t index = static_cast<size_t>(item->getValue().asInteger());
    if (index < doc->outline.size())
    {
        mNavigation.noteJump(!to_editor);
        sourceInFront(*doc).goTo(rangeOf(doc->outline[index].nameSpan));
        revealed(mOutline, to_editor);
    }
}
