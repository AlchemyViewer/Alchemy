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

#include "alscriptoutlinepane.h"

#include "alpanefolds.h"
#include "alpanelist.h"
#include "alscriptstudioservices.h"
#include "alscriptstudiowords.h"
#include "alstringmatch.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfloater.h"
#include "llfontgl.h"
#include "llscrolllistcell.h"
#include "llscrolllistcolumn.h"
#include "llscrolllistitem.h"

#include <functional>

static LLPanelInjector<ALScriptOutlinePane> t_script_studio_outline("script_studio_outline");

ALScriptOutlinePane::ALScriptOutlinePane(const LLPanel::Params& params) : LLPanel(params) {}

bool ALScriptOutlinePane::postBuild()
{
    mList   = getChild<ALPaneList>("outline");
    mFilter = getChild<LLFilterEditor>("outline_filter");
    mSort   = getChild<LLComboBox>("outline_sort");
    mSort->selectFirstItem();
    // The window this is a pane of, found through the view tree, as what
    // the pane asks of it.
    LLFloater* window = getParentByType<LLFloater>();
    mServices         = dynamic_cast<ALScriptStudioServices*>(window);
    mWindow           = dynamic_cast<Window*>(window);
    if (!mServices || !mWindow)
    {
        LL_WARNS() << "The outline is not in a Script Studio window" << LL_ENDL;
        return true;
    }
    mFilter->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (Doc* doc = mServices->frontDoc())
        {
            show(*doc);
        }
    });
    mSort->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (Doc* doc = mServices->frontDoc())
        {
            show(*doc);
        }
        mWindow->outlineSortChanged();
    });
    mList->setCommitCallback([this](LLUICtrl*, const LLSD&) { choose(false); });
    // Return and a double-click go to the symbol chosen, to type there, and
    // escape back to the script without going anywhere, as in every pane's
    // list; left and right fold and open, as a tree's do.
    mList->setGo([this]() { choose(true); });
    mList->setBack([this]() { mServices->revealed(mList, true); });
    mList->setFold([this](const LLSD& value, std::optional<bool> folded) { fold(static_cast<size_t>(value.asInteger()), folded); },
                   [this](const LLScrollListItem* item, S32 x) { return arrowAt(item, x); });
    return true;
}

std::string ALScriptOutlinePane::kindName(ALScriptSymbolKind kind) const
{
    const char* word = ALScriptStudioWords::kindWordOf(kind);
    return word ? mServices->words(word) : std::string();
}

void ALScriptOutlinePane::show(Doc& doc)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!mServices || &doc != mServices->frontDoc())
    {
        return;
    }
    // Listed only while it can be seen, and once it is (pump); the window
    // told all the same, whose bar at the bottom walks the same outline.
    if (!ALPaneFolds::inSight(this))
    {
        mUnseen = true;
        mWindow->outlineShown(doc);
        return;
    }
    mUnseen = false;
    // The symbols as a tree, each under what holds it -- the outline is
    // flat, each entry after its holder one deeper -- keyed by the names
    // down to it, so that a fold outlives a check that numbers them anew.
    const size_t count = doc.outline.size();
    std::vector<std::vector<size_t>> children(count);
    std::vector<size_t>              roots;
    mKeys.assign(count, std::string());
    mParents.assign(count, false);
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
                mKeys[i] = doc.outline[i].name;
            }
            else
            {
                children[holders.back()].push_back(i);
                mParents[holders.back()] = true;
                mKeys[i]                 = mKeys[holders.back()] + "\x1f" + doc.outline[i].name;
            }
            holders.push_back(i);
        }
    }
    // In the order asked for, under each holder: as written, by name, or
    // by kind and then name.
    const std::string sort   = sortOrder();
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
    std::string filter = mFilter->getText();
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
                if (!children[i].empty() && !doc.caret.outlineFolded.contains(mKeys[i]))
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
    const std::string open   = mServices->words("ArrowOpen");
    const std::string folded = mServices->words("ArrowFolded");
    std::vector<std::string> said;
    said.reserve(rows.size() + 1);
    for (const Row& row : rows)
    {
        const ALScriptOutlineEntry& entry  = doc.outline[row.index];
        const bool                  parent = filter.empty() && mParents[row.index];
        const std::string arrow = !parent ? std::string("   ") : doc.caret.outlineFolded.contains(mKeys[row.index]) ? folded : open;
        said.push_back(std::string(static_cast<size_t>(row.depth) * 4, ' ') + arrow + entry.name + "|" +
                       std::to_string(static_cast<S32>(entry.kind)) + "|" + std::to_string(row.index) + "|" + entry.detail);
    }
    said.push_back(doc.id);
    if (said != mSaid)
    {
        mSaid            = said;
        const S32 scroll = mList->getScrollPos();
        mList->deleteAllItems();
        for (size_t r = 0; r < rows.size(); ++r)
        {
            const ALScriptOutlineEntry& entry = doc.outline[rows[r].index];
            LLSD                        row;
            row["value"]                  = static_cast<S32>(rows[r].index);
            row["columns"][0]["column"]   = "icon";
            row["columns"][0]["type"]     = "icon";
            row["columns"][0]["value"]    = ALScriptStudioWords::imageNameOf(entry.kind);
            row["columns"][0]["tool_tip"] = kindName(entry.kind);
            row["columns"][1]["column"]   = "symbol";
            // Nested under what holds it, with the arrow that folds what
            // it holds; what it is, and its declaration, on the mouse.
            row["columns"][1]["value"]    = said[r].substr(0, said[r].find('|'));
            row["columns"][1]["tool_tip"] = entry.detail.empty() ? kindName(entry.kind) : kindName(entry.kind) + "\n" + entry.detail;
            mList->addElement(row);
        }
        mList->setScrollPos(scroll);
    }
    mList->setEmpty(doc.outline.empty() ? mServices->words(doc.loaded ? "NoOutline" : "NoOutlineYet")
                    : rows.empty()      ? mServices->words("OutlineNoMatch")
                                        : LLStringUtil::null,
                    LLStringUtil::null);
    mWindow->outlineShown(doc);
    followCaret(doc);
}

bool ALScriptOutlinePane::arrowAt(const LLScrollListItem* item, S32 x) const
{
    const size_t index = static_cast<size_t>(item->getValue().asInteger());
    if (index >= mParents.size() || !mParents[index])
    {
        return false;
    }
    // The arrow comes after the symbol's indent, from the name column's
    // edge: as far as the indent and the arrow go.
    const LLScrollListCell*   name      = item->getColumn(1);
    const std::string         text      = name ? name->getValue().asString() : std::string();
    const size_t              indent    = text.find_first_not_of(' ');
    const size_t              arrow_end = indent == std::string::npos ? 0 : indent + mServices->words("ArrowOpen").size();
    const LLScrollListColumn* icon      = mList->getColumn("icon");
    const S32 left  = mList->getItemListRect().mLeft + (icon ? icon->getWidth() : 0) + mList->getColumnPadding();
    const S32 right = left + LLFontGL::getFontSansSerifSmall()->getWidth(text.substr(0, arrow_end)) + 4;
    return x >= left - 2 && x <= right;
}

void ALScriptOutlinePane::fold(size_t index, std::optional<bool> folded)
{
    Doc* doc = mServices ? mServices->frontDoc() : nullptr;
    if (!doc || index >= mKeys.size())
    {
        return;
    }
    const std::string& key  = mKeys[index];
    const bool         shut = doc->caret.outlineFolded.contains(key);
    const bool         want = folded.value_or(!shut);
    if (!mParents[index] || want == shut)
    {
        // Left on a symbol with nothing to fold: to what holds it, shown
        // as any row walked to is.
        if (folded.has_value() && *folded)
        {
            const size_t at = key.rfind('\x1f');
            if (at != std::string::npos)
            {
                const std::string holder = key.substr(0, at);
                for (size_t i = 0; i < mKeys.size(); ++i)
                {
                    if (mKeys[i] == holder && mList->selectByValue(LLSD(static_cast<S32>(i))))
                    {
                        mList->scrollToShowSelected();
                        choose(false);
                        break;
                    }
                }
            }
        }
        return;
    }
    if (want)
    {
        doc->caret.outlineFolded.insert(key);
    }
    else
    {
        doc->caret.outlineFolded.erase(key);
    }
    show(*doc);
    mList->selectByValue(LLSD(static_cast<S32>(index)));
}

void ALScriptOutlinePane::followCaret(Doc& doc)
{
    if (!mServices || &doc != mServices->frontDoc())
    {
        return;
    }
    if (mUnseen || !ALPaneFolds::inSight(this))
    {
        mUnseen = true;
        return;
    }
    // The innermost symbol the caret is in, as the breadcrumb found it.
    if (doc.caret.crumbPath.empty())
    {
        mList->deselectAllItems(true);
        return;
    }
    LLScrollListItem* now = mList->getFirstSelected();
    for (auto step = doc.caret.crumbPath.rbegin(); step != doc.caret.crumbPath.rend(); ++step)
    {
        const S32 index = static_cast<S32>(*step);
        if (now && now->getValue().asInteger() == index)
        {
            return;
        }
        if (mList->selectByValue(LLSD(index)))
        {
            mList->scrollToShowSelected();
            return;
        }
    }
    mList->deselectAllItems(true);
}

void ALScriptOutlinePane::choose(bool to_editor)
{
    Doc*              doc  = mServices ? mServices->frontDoc() : nullptr;
    LLScrollListItem* item = mList->getFirstSelected();
    if (!doc || !item)
    {
        return;
    }
    const size_t index = static_cast<size_t>(item->getValue().asInteger());
    if (index < doc->outline.size())
    {
        mWindow->outlineChosen(*doc, doc->outline[index], to_editor);
        mServices->revealed(mList, to_editor);
    }
}

void ALScriptOutlinePane::pump()
{
    Doc* front = mServices ? mServices->frontDoc() : nullptr;
    if (mUnseen && front && ALPaneFolds::inSight(this))
    {
        show(*front);
    }
}

void ALScriptOutlinePane::forget()
{
    mUnseen = false;
    mList->deleteAllItems();
    // What the list says is nothing now: the next tab's is put in
    // whatever it says, the same rows as the last one's or not.
    mSaid.clear();
}

std::string ALScriptOutlinePane::sortOrder() const
{
    return mSort ? mSort->getValue().asString() : std::string("order");
}

void ALScriptOutlinePane::setSortOrder(const std::string& order)
{
    if (mSort)
    {
        mSort->selectByValue(order);
    }
}
