/**
 * @file alfixlistmodel.cpp
 * @brief What a code editor's quick-fix list offers, in what order, and what each would make of the text.
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

#include "alfixlistmodel.h"

#include "alsaid.h"

#include <algorithm>
#include <optional>
#include <string_view>

// static
void ALFixListModel::rank(std::vector<ALCodeFix>& fixes)
{
    const auto rank = [](const ALCodeFix& fix) { return fix.suppress ? 3 : fix.refactor ? 2 : fix.preferred ? 0 : 1; };
    std::stable_sort(fixes.begin(), fixes.end(), [&rank](const ALCodeFix& a, const ALCodeFix& b) { return rank(a) < rank(b); });
}

// static
std::string ALFixListModel::fixedLines(const ALTextDocument& text, const ALCodeFix& fix, S32& first, S32& last)
{
    first = S32_MAX;
    last  = -1;
    for (const auto& [range, with] : fix.edits)
    {
        const ALTextRange ordered = range.normalised();
        first                     = llmin(first, ordered.begin.line);
        last                      = llmax(last, ordered.end.line);
    }
    if (last < 0 || first >= text.lineCount())
    {
        first = last = 0;
        return std::string();
    }
    first = llmax(0, first);
    last  = llmin(last, text.lineCount() - 1);
    std::string         block;
    std::vector<size_t> starts;
    for (S32 line = first; line <= last; ++line)
    {
        starts.push_back(block.size());
        block += text.line(line);
        if (line < last)
        {
            block += "\n";
        }
    }
    // In the order the editor makes them (ALTextView::replaceAll), from
    // the last back, so that each one's places are still the text's as
    // it was.
    std::vector<std::pair<ALTextRange, std::string>> edits = fix.edits;
    for (auto& one : edits)
    {
        one.first = one.first.normalised();
    }
    std::stable_sort(edits.begin(), edits.end(),
                     [](const auto& a, const auto& b) { return a.first.begin < b.first.begin || (a.first.begin == b.first.begin && a.first.end < b.first.end); });
    const auto offset = [&](const ALTextPos& at) {
        const S32 line = llclamp(at.line, first, last);
        return starts[line - first] + static_cast<size_t>(llclamp(at.column, 0, static_cast<S32>(text.line(line).size())));
    };
    for (auto it = edits.rbegin(); it != edits.rend(); ++it)
    {
        const auto& [range, with] = *it;
        const ALTextRange ordered = range.normalised();
        const size_t      from    = offset(ordered.begin);
        const size_t      to      = offset(ordered.end);
        if (to >= from && to <= block.size())
        {
            block.replace(from, to - from, with);
        }
    }
    return block;
}

// static
std::string ALFixListModel::previewOf(const ALTextDocument& text, const ALCodeFix& fix, std::vector<char>& kinds)
{
    // The lines it touches as they read and as they would, as a diff has
    // them: what goes in the error colour, what comes coloured as code. A
    // stretch for each place it changes, edits a few lines apart or less
    // together, and an ellipsis for what lies between -- a global put in
    // at the top for a use two hundred lines down is two short stretches,
    // not the two hundred lines.
    std::vector<std::pair<ALTextRange, std::string>> edits = fix.edits;
    for (auto& one : edits)
    {
        one.first = one.first.normalised();
    }
    std::stable_sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.first.begin < b.first.begin; });
    std::vector<ALCodeFix> stretches;
    S32              reach = -1;
    for (const auto& one : edits)
    {
        if (stretches.empty() || one.first.begin.line > reach + 2)
        {
            stretches.emplace_back();
        }
        stretches.back().edits.push_back(one);
        reach = llmax(reach, one.first.end.line);
    }
    // Each line with its kind, the lines' indentation in common taken off
    // once they are all in: a fix deep in a block reads at the left, not
    // past the box's edge.
    std::vector<std::pair<char, std::string>> shown;
    const auto add = [&shown](char kind, const std::string& line) { shown.emplace_back(kind, line); };
    for (size_t k = 0; k < stretches.size(); ++k)
    {
        if (k > 0)
        {
            add(' ', "\u2026");
        }
        S32               first = 0, last = 0;
        const std::string after = fixedLines(text, stretches[k], first, last);
        for (S32 line = first; line <= last && line < text.lineCount(); ++line)
        {
            add('-', text.line(line));
        }
        std::string_view rest = after;
        while (true)
        {
            const size_t cut = rest.find('\n');
            add('+', std::string(rest.substr(0, cut)));
            if (cut == std::string_view::npos)
            {
                break;
            }
            rest.remove_prefix(cut + 1);
        }
    }
    std::optional<std::string_view> common;
    for (const auto& [kind, line] : shown)
    {
        const size_t lead = line.find_first_not_of(" \t");
        if (kind == ' ' || lead == std::string::npos)
        {
            continue;
        }
        const std::string_view indent(line.data(), lead);
        if (!common)
        {
            common = indent;
        }
        else
        {
            const size_t same = static_cast<size_t>(std::mismatch(common->begin(), common->end(), indent.begin(), indent.end()).first - common->begin());
            common            = common->substr(0, same);
        }
    }
    const size_t cut = common ? common->size() : 0;
    std::string  says;
    kinds.clear();
    for (const auto& [kind, line] : shown)
    {
        const std::string text = kind != ' ' && line.size() >= cut ? line.substr(cut) : line;
        says += (says.empty() ? "" : "\n") + (kind == ' ' ? text : std::string(1, kind) + " " + text);
        kinds.push_back(kind);
    }
    return says;
}

U32 ALFixListModel::show(S32 line, std::vector<ALCodeFix> fixes)
{
    mFixes = std::move(fixes);
    mLine  = line;
    return ++mShowing;
}

void ALFixListModel::close()
{
    mFixes.clear();
    mLine    = -1;
    mAwaited = false;
}

bool ALFixListModel::note(U32 shown, const std::vector<std::string>& notes)
{
    if (shown != mShowing || notes.size() != mFixes.size())
    {
        return false;
    }
    for (size_t i = 0; i < notes.size(); ++i)
    {
        mFixes[i].note = notes[i];
    }
    return true;
}

void ALFixListModel::ask(const ALTextRange& at, const ALTextPos& caret)
{
    mAskedAt    = at;
    mAskedCaret = caret;
    mAwaited    = true;
}

std::optional<ALFixListModel::Joined> ALFixListModel::join(const ALTextRange& at, const ALTextPos& caret, std::vector<ALCodeFix> actions, bool open, S32 chosen)
{
    if (!mAwaited || !(at == mAskedAt) || !(caret == mAskedCaret))
    {
        return std::nullopt;
    }
    mAwaited = false;
    Joined      joined;
    std::string was;
    if (open)
    {
        joined.fixes = mFixes;
        if (chosen >= 0 && chosen < static_cast<S32>(joined.fixes.size()))
        {
            was = joined.fixes[chosen].title;
        }
    }
    for (ALCodeFix& action : actions)
    {
        action.refactor = true;
        joined.fixes.push_back(std::move(action));
    }
    if (joined.fixes.empty())
    {
        // Asked for, and nothing to offer: said, rather than nothing
        // happening at all.
        ALCodeFix none;
        none.title = alSaid("CodeFixNone", "Nothing to fix or refactor here");
        joined.fixes.push_back(std::move(none));
    }
    else if (open && joined.fixes.size() == mFixes.size())
    {
        return std::nullopt;
    }
    rank(joined.fixes);
    // The choice stays on what was chosen: the refactors come in while the
    // list is already being read.
    for (size_t i = 0; i < joined.fixes.size() && !was.empty(); ++i)
    {
        if (joined.fixes[i].title == was)
        {
            joined.chosen = static_cast<S32>(i);
            break;
        }
    }
    return joined;
}
