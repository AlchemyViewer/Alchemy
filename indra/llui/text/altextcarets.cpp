/**
 * @file altextcarets.cpp
 * @brief The selections a text view has besides its main one.
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

#include "altextcarets.h"

void ALTextCarets::assign(std::vector<ALTextRange> selections)
{
    mSelections.assign(std::move(selections));
    sweep(nullptr);
}

void ALTextCarets::add(const ALTextRange& selection)
{
    mSelections.insert(selection);
    sweep(nullptr);
}

bool ALTextCarets::merge(ALTextRange& main)
{
    return sweep(&main);
}

void ALTextCarets::apply(const ALTextDocument::Edit& edit)
{
    if (mSelections.empty())
    {
        return;
    }
    bool moved = false;
    mSelections.apply(
        edit,
        [&moved](ALTextRange& selection, const ALTextDocument::Edit& e) {
            const ALTextRange after = slid(selection, e);
            moved                   = moved || after != selection;
            selection               = after;
            return true;
        },
        [](ALTextRange&) {});
    // Only what moved can have come to meet another.
    if (moved)
    {
        sweep(nullptr);
    }
}

// static
ALTextRange ALTextCarets::slid(const ALTextRange& selection, const ALTextDocument::Edit& edit)
{
    if (selection.empty())
    {
        const ALTextPos at = edit.placed(selection.end);
        return ALTextRange(at, at);
    }
    const ALTextRange ordered = selection.normalised();
    const ALTextPos   begin   = edit.placed(ordered.begin);
    const ALTextPos   end     = std::max(begin, edit.placed(ordered.end, false));
    return selection.end < selection.begin ? ALTextRange(end, begin) : ALTextRange(begin, end);
}

// static
bool ALTextCarets::meet(const ALTextRange& first, const ALTextRange& second)
{
    const ALTextRange a = first.normalised();
    const ALTextRange b = second.normalised();
    return (a.empty() || b.empty()) ? b.begin <= a.end : b.begin < a.end;
}

// static
ALTextRange ALTextCarets::joined(const ALTextRange& winner, const ALTextRange& loser)
{
    const ALTextRange w     = winner.normalised();
    const ALTextRange l     = loser.normalised();
    const ALTextPos   begin = std::min(w.begin, l.begin);
    const ALTextPos   end   = std::max(w.end, l.end);
    const ALTextRange& way  = winner.empty() ? loser : winner;
    return way.end < way.begin ? ALTextRange(end, begin) : ALTextRange(begin, end);
}

bool ALTextCarets::sweep(ALTextRange* main)
{
    if (mSelections.empty())
    {
        return false;
    }
    // In the order they begin, the main selection before any that begins
    // where it does; each joined to the one kept before it where they meet,
    // the main one winning, and the one kept first otherwise.
    std::vector<ALTextRange> items = mSelections.take();
    const ALTextRange        was   = main ? *main : ALTextRange();
    std::vector<ALTextRange> kept;
    kept.reserve(items.size());
    bool       main_kept = false;
    // Which of those kept is the main selection, once it is.
    size_t     main_at   = 0;
    const auto take      = [&](const ALTextRange& next, bool is_main) {
        if (!kept.empty() && meet(kept.back(), next))
        {
            kept.back() = is_main ? joined(next, kept.back()) : joined(kept.back(), next);
        }
        else
        {
            kept.push_back(next);
        }
        if (is_main)
        {
            main_kept = true;
            main_at   = kept.size() - 1;
        }
    };
    const ALTextPos main_begin = was.normalised().begin;
    bool            main_due   = main != nullptr;
    for (const ALTextRange& item : items)
    {
        if (main_due && main_begin <= item.normalised().begin)
        {
            take(was, true);
            main_due = false;
        }
        take(item, false);
    }
    if (main_due)
    {
        take(was, true);
    }
    // Back as they were: the main one to the view, the rest here, still in
    // the order they begin.
    for (size_t i = 0; i < kept.size(); ++i)
    {
        if (main_kept && i == main_at)
        {
            continue;
        }
        mSelections.push_back(kept[i]);
    }
    if (!main)
    {
        return false;
    }
    *main = kept[main_at];
    return *main != was;
}
