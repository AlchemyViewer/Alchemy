/**
 * @file alquickopen.cpp
 * @brief One field, a ranked list, and the thing you were thinking of at the top.
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

#include "alquickopen.h"

#include "lllineeditor.h"
#include "llscrolllistctrl.h"
#include "lluictrlfactory.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALQuickOpen> r("quick_open");

namespace
{
    constexpr S32 FIELD_HEIGHT = 24;
    constexpr S32 GAP = 4;

    // What each degree of meaning it is worth. The gaps are wide because
    // these are kinds and not amounts: no number of scattered letters adds up
    // to a name that starts with what you typed.
    constexpr S32 SCORE_WHOLE       = 10000;
    constexpr S32 SCORE_PREFIX      = 5000;
    constexpr S32 SCORE_INITIALS    = 3000;
    constexpr S32 SCORE_WORD_START  = 2000;
    constexpr S32 SCORE_RUN         = 1000;
    constexpr S32 SCORE_SCATTERED   = 100;

    char lower(char c)
    {
        return LLStringOps::toLower(c);
    }

    bool isBreak(char c)
    {
        return c == '_' || c == '-' || c == '.' || c == '/' || c == '\\' || c == ' ';
    }

    // The first letter of each word: `floater_buy.xml` gives f, b, x.
    std::string initialsOf(std::string_view label)
    {
        std::string letters;
        bool at_start = true;
        for (char c : label)
        {
            if (isBreak(c))
            {
                at_start = true;
                continue;
            }
            if (at_start)
            {
                letters.push_back(lower(c));
                at_start = false;
            }
        }
        return letters;
    }

    // Whether every letter of the query appears in order, and how far into
    // the label the last of them is: a match that finishes early is a better
    // answer than one that straggles to the end.
    bool inOrder(std::string_view label, std::string_view query, size_t& reach)
    {
        size_t at = 0;
        for (char want : query)
        {
            const char c = lower(want);
            while (at < label.size() && lower(label[at]) != c)
            {
                ++at;
            }
            if (at == label.size())
            {
                return false;
            }
            ++at;
        }
        reach = at;
        return true;
    }
}

// static
S32 ALQuickOpen::score(std::string_view label, std::string_view query)
{
    if (query.empty())
    {
        return 1;   // everything answers nothing, equally
    }
    if (label.empty() || query.size() > label.size())
    {
        return 0;
    }

    std::string lowered;
    lowered.reserve(label.size());
    for (char c : label)
    {
        lowered.push_back(lower(c));
    }
    std::string wanted;
    wanted.reserve(query.size());
    for (char c : query)
    {
        wanted.push_back(lower(c));
    }

    // A shorter label answering the same query answered it better: `panel`
    // beats `panel_preferences_advanced` for `pan`, and it is the one meant.
    const S32 brevity = (S32)llmax(0, 200 - (S32)label.size());

    if (lowered == wanted)
    {
        return SCORE_WHOLE + brevity;
    }
    if (lowered.rfind(wanted, 0) == 0)
    {
        return SCORE_PREFIX + brevity;
    }

    const std::string letters = initialsOf(label);
    if (letters.rfind(wanted, 0) == 0)
    {
        return SCORE_INITIALS + brevity;
    }

    // A word inside the name starting with it: `buy` in `floater_buy.xml`.
    for (size_t i = 1; i < lowered.size(); ++i)
    {
        if (isBreak(lowered[i - 1]) && lowered.compare(i, wanted.size(), wanted) == 0)
        {
            return SCORE_WORD_START + brevity;
        }
    }

    if (const size_t at = lowered.find(wanted); at != std::string::npos)
    {
        // A run anywhere, worth less the further in it starts.
        return SCORE_RUN + brevity - (S32)at;
    }

    size_t reach = 0;
    if (inOrder(lowered, wanted, reach))
    {
        return SCORE_SCATTERED + brevity - (S32)reach;
    }
    return 0;
}

// static
std::vector<size_t> ALQuickOpen::rank(const std::vector<Candidate>& candidates,
                                      std::string_view query)
{
    std::vector<std::pair<S32, size_t> > scored;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (const S32 how = score(candidates[i].label, query); how > 0)
        {
            scored.emplace_back(how, i);
        }
    }
    // Best first; among equals, the order they were given, which is the
    // caller's own idea of which matters more.
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<size_t> order;
    order.reserve(scored.size());
    for (const auto& [how, at] : scored)
    {
        order.push_back(at);
    }
    return order;
}

ALQuickOpen::Params::Params()
:   placeholder("placeholder"),
    rows("rows", 12)
{
}

ALQuickOpen::ALQuickOpen(const Params& p)
:   LLPanel(p),
    mPlaceholder(p.placeholder),
    mRows(p.rows)
{
    LLLineEditor::Params fp(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
    fp.name = "query";
    fp.rect = LLRect(0, getRect().getHeight(), getRect().getWidth(),
                     getRect().getHeight() - FIELD_HEIGHT);
    fp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
    fp.label = mPlaceholder;
    mField = LLUICtrlFactory::create<LLLineEditor>(fp);
    mField->setKeystrokeCallback([this](LLLineEditor* editor, void*)
    {
        setQuery(editor->getText());
    }, nullptr);
    mField->setCommitCallback([this](LLUICtrl*, const LLSD&) { chooseSelected(); });
    addChild(mField);

    LLScrollListCtrl::Params lp(LLUICtrlFactory::getDefaultParams<LLScrollListCtrl>());
    lp.name = "matches";
    lp.rect = LLRect(0, getRect().getHeight() - FIELD_HEIGHT - GAP, getRect().getWidth(), 0);
    lp.follows.flags = FOLLOWS_ALL;
    lp.draw_heading = false;
    lp.multi_select = false;
    lp.column_padding = 0;
    mList = LLUICtrlFactory::create<LLScrollListCtrl>(lp);
    addChild(mList);

    LLScrollListColumn::Params what;
    what.name = "label";
    what.width.dynamic_width = true;
    mList->addColumn(what);
    LLScrollListColumn::Params detail;
    detail.name = "detail";
    detail.width.relative_width = 0.4f;
    mList->addColumn(detail);

    mList->setDoubleClickCallback([this]() { chooseSelected(); });
    layout();
}

void ALQuickOpen::setCandidates(std::vector<Candidate> candidates)
{
    mCandidates = std::move(candidates);
    fill();
}

void ALQuickOpen::setQuery(const std::string& query)
{
    mQuery = query;
    if (mField && mField->getText() != query)
    {
        mField->setText(query);
    }
    fill();
}

void ALQuickOpen::fill()
{
    mRanked = rank(mCandidates, mQuery);
    mList->deleteAllItems();
    for (size_t at : mRanked)
    {
        LLSD row;
        row["value"] = mCandidates[at].value;
        LLSD& columns = row["columns"];
        columns[0]["column"] = "label";
        columns[0]["value"] = mCandidates[at].label;
        columns[1]["column"] = "detail";
        columns[1]["value"] = mCandidates[at].detail;
        mList->addElement(row);
    }
    // The best answer, chosen: return takes it without an arrow key first,
    // which is the whole gesture this widget is.
    if (!mRanked.empty())
    {
        mList->selectFirstItem();
    }
}

void ALQuickOpen::chooseSelected()
{
    if (const LLScrollListItem* item = mList->getFirstSelected())
    {
        mChose(item->getValue().asString());
    }
}

void ALQuickOpen::takeFocus()
{
    if (mField)
    {
        mField->setFocus(true);
        mField->selectAll();
    }
}

// The arrows walk the list while the keyboard stays in the field, because
// taking a hand off the letters to point at the answer is the thing this
// exists to avoid.
bool ALQuickOpen::handleKeyHere(KEY key, MASK mask)
{
    if ((key == KEY_UP || key == KEY_DOWN) && mask == MASK_NONE && mList->getItemCount() > 0)
    {
        const S32 at = mList->getFirstSelectedIndex();
        const S32 want = llclamp(at + (key == KEY_DOWN ? 1 : -1), 0, mList->getItemCount() - 1);
        mList->selectNthItem(want);
        mList->scrollToShowSelected();
        return true;
    }
    return LLPanel::handleKeyHere(key, mask);
}

void ALQuickOpen::layout()
{
    if (!mField || !mList)
    {
        return;
    }
    const S32 width = getRect().getWidth();
    const S32 height = getRect().getHeight();
    mField->setShape(LLRect(0, height, width, height - FIELD_HEIGHT));
    mList->setShape(LLRect(0, height - FIELD_HEIGHT - GAP, width, 0));
}

void ALQuickOpen::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    layout();
}
