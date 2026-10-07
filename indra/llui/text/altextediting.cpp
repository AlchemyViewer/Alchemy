/**
 * @file altextediting.cpp
 * @brief A text view's commands over whole lines: duplicate, move, delete, comment and join them.
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

#include "altextediting.h"

#include <algorithm>

namespace
{
    bool isBlank(char c)
    {
        return c == ' ' || c == '\t';
    }

    // Where a line's text begins, past its blanks.
    S32 indentationOf(const std::string& line)
    {
        S32 n = 0;
        while (n < static_cast<S32>(line.size()) && isBlank(line[n]))
        {
            ++n;
        }
        return n;
    }

    // Lines first through last again, under them.
    ALTextEditing::Replacement duplicated(const ALTextDocument& doc, S32 first, S32 last)
    {
        const std::string block = doc.text(ALTextRange(doc.lineStart(first), doc.lineEnd(last)));
        return { ALTextRange(doc.lineEnd(last), doc.lineEnd(last)), "\n" + block };
    }

    // What deleting lines first through last takes -- the last line of the
    // text with the break before it -- and the line a caret lands on once
    // they are gone, and how long that is.
    ALTextRange deleted(const ALTextDocument& doc, S32 first, S32 last, S32& line, S32& length)
    {
        const S32   count = doc.lineCount();
        ALTextRange range(doc.lineStart(first), last + 1 < count ? doc.lineStart(last + 1) : doc.lineEnd(last));
        line   = first;
        length = last + 1 < count ? doc.lineLength(last + 1) : 0;
        if (last + 1 >= count && first > 0)
        {
            range.begin = doc.lineEnd(first - 1);
            line        = first - 1;
            length      = doc.lineLength(first - 1);
        }
        return range;
    }

    // Whether every line first through last that says anything is
    // commented with the token; nothing where none says anything.
    std::optional<bool> commented(const ALTextDocument& doc, S32 first, S32 last, const std::string& token)
    {
        bool all_commented = true;
        bool any_text      = false;
        for (S32 l = first; l <= last; ++l)
        {
            const std::string& line = doc.line(l);
            const S32          n    = indentationOf(line);
            if (n == static_cast<S32>(line.size()))
            {
                continue;
            }
            any_text = true;
            if (line.compare(n, token.size(), token) != 0)
            {
                all_commented = false;
            }
        }
        if (!any_text)
        {
            return std::nullopt;
        }
        return all_commented;
    }

    // Each line first through last that says anything commented out, the
    // token and the space after it taken from where its text begins; or in,
    // the token and a space put there.
    std::vector<ALTextEditing::Replacement> commentLines(const ALTextDocument& doc, S32 first, S32 last, const std::string& token, bool out)
    {
        std::vector<ALTextEditing::Replacement> replacements;
        for (S32 l = first; l <= last; ++l)
        {
            const std::string& line = doc.line(l);
            const S32          n    = indentationOf(line);
            if (n == static_cast<S32>(line.size()))
            {
                continue;
            }
            if (out)
            {
                S32 taken = static_cast<S32>(token.size());
                if (n + taken < static_cast<S32>(line.size()) && line[n + taken] == ' ')
                {
                    ++taken;
                }
                replacements.push_back({ ALTextRange(ALTextPos(l, n), ALTextPos(l, n + taken)), std::string() });
            }
            else
            {
                replacements.push_back({ ALTextRange(ALTextPos(l, n), ALTextPos(l, n)), token + " " });
            }
        }
        return replacements;
    }

    // A selection's lines whole, from the first's start to the next line's
    // start, or to the end of the text's last line as the replacements
    // leave it.
    ALTextRange wholeLines(const ALTextDocument& doc, const ALTextRange& selection, const std::vector<ALTextEditing::Replacement>& replacements)
    {
        const auto [first, last] = ALTextEditing::selectedLines(selection);
        return ALTextRange(ALTextPos(first, 0), last + 1 < doc.lineCount() ? ALTextPos(last + 1, 0)
                                                                            : ALTextEditing::placedThrough(replacements, doc.lineEnd(last)));
    }

    ALTextRange lineShifted(const ALTextRange& selection, S32 lines)
    {
        return ALTextRange(ALTextPos(selection.begin.line + lines, selection.begin.column), ALTextPos(selection.end.line + lines, selection.end.column));
    }
}

namespace ALTextEditing
{
std::pair<S32, S32> selectedLines(const ALTextRange& selection)
{
    const ALTextRange range = selection.normalised();
    S32               last  = range.end.line;
    if (last > range.begin.line && range.end.column == 0)
    {
        --last;
    }
    return { range.begin.line, last };
}

Change duplicateLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret)
{
    const auto [first, last] = selectedLines(ALTextRange(anchor, caret));
    const S32         count  = last - first + 1;
    Change            change;
    change.replacements.push_back(duplicated(doc, first, last));
    // The caret and the selection go with the copy.
    change.selects = true;
    change.anchor  = ALTextPos(anchor.line + count, anchor.column);
    change.caret   = ALTextPos(caret.line + count, caret.column);
    return change;
}

std::optional<Change> moveLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, S32 direction)
{
    const auto [first, last] = selectedLines(ALTextRange(anchor, caret));
    if ((direction < 0 && first == 0) || (direction > 0 && last + 1 >= doc.lineCount()))
    {
        return std::nullopt;
    }
    std::optional<Change> change = moveLinesTo(doc, first, last, direction < 0 ? first - 2 : last + 1);
    if (change)
    {
        change->selects = true;
        change->anchor  = ALTextPos(anchor.line + direction, anchor.column);
        change->caret   = ALTextPos(caret.line + direction, caret.column);
    }
    return change;
}

std::optional<Change> moveLinesTo(const ALTextDocument& doc, S32 first, S32 last, S32 below)
{
    const S32 count = doc.lineCount();
    first           = llclamp(first, 0, count - 1);
    last            = llclamp(last, first, count - 1);
    below           = llclamp(below, -1, count - 1);
    if (below >= first - 1 && below <= last)
    {
        return std::nullopt;
    }
    const std::string block = doc.text(ALTextRange(doc.lineStart(first), doc.lineEnd(last)));
    const S32         lines = last - first + 1;
    Change            change;
    if (below < first)
    {
        // Up: the lines between go under them.
        const std::string between = doc.text(ALTextRange(doc.lineStart(below + 1), doc.lineEnd(first - 1)));
        change.replacements.push_back({ ALTextRange(doc.lineStart(below + 1), doc.lineEnd(last)), block + "\n" + between });
        change.caret = ALTextPos(below + lines, 0);
    }
    else
    {
        // Down: the lines between go above them.
        const std::string between = doc.text(ALTextRange(doc.lineStart(last + 1), doc.lineEnd(below)));
        change.replacements.push_back({ ALTextRange(doc.lineStart(first), doc.lineEnd(below)), between + "\n" + block });
        change.caret = ALTextPos(below, 0);
    }
    change.anchor = change.caret;
    return change;
}

Change copyLinesTo(const ALTextDocument& doc, S32 first, S32 last, S32 below)
{
    const S32 count = doc.lineCount();
    first           = llclamp(first, 0, count - 1);
    last            = llclamp(last, first, count - 1);
    below           = llclamp(below, -1, count - 1);
    const std::string block = doc.text(ALTextRange(doc.lineStart(first), doc.lineEnd(last)));
    Change            change;
    if (below < 0)
    {
        change.replacements.push_back({ ALTextRange(doc.lineStart(0), doc.lineStart(0)), block + "\n" });
    }
    else
    {
        change.replacements.push_back({ ALTextRange(doc.lineEnd(below), doc.lineEnd(below)), "\n" + block });
    }
    change.caret  = ALTextPos(below + last - first + 1, 0);
    change.anchor = change.caret;
    return change;
}

Change deleteLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret)
{
    const auto [first, last] = selectedLines(ALTextRange(anchor, caret));
    // The line the caret lands on, and how long it is, once they are gone.
    S32               line   = first;
    S32               length = 0;
    const ALTextRange range  = deleted(doc, first, last, line, length);
    Change            change;
    change.replacements.push_back({ range, std::string() });
    change.caret  = ALTextPos(line, llmin(caret.column, length));
    change.anchor = change.caret;
    return change;
}

std::optional<Change> toggleComment(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, const std::string& token)
{
    const auto [first, last] = selectedLines(ALTextRange(anchor, caret));
    // Out where every line that says anything is commented; in otherwise.
    const std::optional<bool> out = commented(doc, first, last, token);
    if (!out)
    {
        return std::nullopt;
    }
    Change change;
    change.replacements = commentLines(doc, first, last, token, *out);
    if (anchor != caret)
    {
        // The lines whole.
        const ALTextRange lines = wholeLines(doc, ALTextRange(anchor, caret), change.replacements);
        change.selects          = true;
        change.anchor           = lines.begin;
        change.caret            = lines.end;
    }
    else
    {
        // The caret where it was in its text: pushed along by the token put
        // in where it stands, and back to where the token was taken from
        // where it stood in it.
        change.caret  = placedThrough(change.replacements, caret);
        change.anchor = change.caret;
    }
    return change;
}

std::optional<Change> joinLines(const ALTextDocument& doc, S32 first, S32 last, bool keep_blanks)
{
    last = llmin(last, doc.lineCount() - 1);
    if (first < 0 || last <= first)
    {
        return std::nullopt;
    }
    // From the bottom up, so each join's place is the text's as it was.
    Change change;
    for (S32 line = last - 1; line >= first; --line)
    {
        if (keep_blanks)
        {
            change.replacements.push_back({ ALTextRange(doc.lineEnd(line), doc.lineStart(line + 1)), std::string() });
            continue;
        }
        const std::string& next  = doc.line(line + 1);
        const size_t       text  = next.find_first_not_of(" \t");
        const bool         blank = text == std::string::npos || next[text] == ')';
        const S32          at    = text == std::string::npos ? static_cast<S32>(next.size()) : static_cast<S32>(text);
        const std::string  space = blank ? std::string() : std::string(" ");
        change.replacements.push_back({ ALTextRange(doc.lineEnd(line), ALTextPos(line + 1, at)), space });
    }
    change.caret = doc.lineEnd(first);
    return change;
}

ALTextPos endOf(const ALTextPos& at, const std::string& text)
{
    return alTextEnd(at, text);
}

ALTextPos placedThrough(const std::vector<Replacement>& replacements, const ALTextPos& pos, bool pushed)
{
    // From the last, so that each is measured on the text as it was before
    // any: those before it have not moved yet.
    ALTextPos at = pos;
    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it)
    {
        ALTextDocument::Edit edit;
        edit.range    = it->range.normalised();
        edit.inserted = it->text;
        at            = edit.placed(at, pushed);
    }
    return at;
}

Combined combine(std::vector<Group> groups, size_t count)
{
    Combined out;
    out.selections.resize(count);
    const auto by_begin = [](const Replacement& a, const Replacement& b) { return a.range.normalised().begin < b.range.normalised().begin; };
    for (Group& group : groups)
    {
        std::stable_sort(group.replacements.begin(), group.replacements.end(), by_begin);
    }
    // In the order of the text: where a group's first replacement begins,
    // or for one that replaces nothing, where its first selection does --
    // before a group that replaces from the same place, its places being
    // the text's as it stands.
    const auto key = [](const Group& group) {
        if (!group.replacements.empty())
        {
            return group.replacements.front().range.normalised().begin;
        }
        ALTextPos first(S32_MAX, S32_MAX);
        for (const auto& [index, selection] : group.placed)
        {
            first = std::min(first, selection.normalised().begin);
        }
        return first;
    };
    std::vector<size_t> order(groups.size());
    for (size_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const ALTextPos ka = key(groups[a]);
        const ALTextPos kb = key(groups[b]);
        if (ka != kb)
        {
            return ka < kb;
        }
        return groups[a].replacements.empty() && !groups[b].replacements.empty();
    });

    // The replacements kept so far, as the stretches of the batch they will
    // be, each as it is in the text before and in the text after: a place
    // at or past one moves by the last of them as the place where it ended
    // does, as the document slides one past a batch.
    ALTextDocument::Edit kept;
    for (const size_t g : order)
    {
        Group& group = groups[g];
        if (!group.replacements.empty() && !kept.parts.empty() && group.replacements.front().range.normalised().begin < kept.parts.back().before.end)
        {
            // Over a replacement before it: left out.
            continue;
        }
        // Its selections, by the groups before it, which is all that has
        // moved what its own replacements left.
        for (const auto& [index, selection] : group.placed)
        {
            if (index < count)
            {
                out.selections[index] = ALTextRange(kept.slidPast(selection.begin), kept.slidPast(selection.end));
            }
        }
        for (Replacement& replacement : group.replacements)
        {
            const ALTextRange range = replacement.range.normalised();
            const ALTextPos   begin = kept.slidPast(range.begin);
            kept.parts.push_back({ range, ALTextRange(begin, alTextEnd(begin, replacement.text)) });
            out.replacements.push_back({ range, std::move(replacement.text) });
        }
    }
    return out;
}

std::vector<LineRun> lineRuns(const std::vector<ALTextRange>& selections, bool touching)
{
    std::vector<LineRun> runs;
    for (size_t i = 0; i < selections.size(); ++i)
    {
        const auto [first, last] = selectedLines(selections[i]);
        if (!runs.empty() && first <= runs.back().last + (touching ? 1 : 0))
        {
            runs.back().last = std::max(runs.back().last, last);
            runs.back().selections.push_back(i);
            continue;
        }
        LineRun run;
        run.first = first;
        run.last  = last;
        run.selections.push_back(i);
        runs.push_back(std::move(run));
    }
    return runs;
}

std::vector<Group> duplicateLines(const ALTextDocument& doc, const std::vector<ALTextRange>& selections)
{
    std::vector<Group> groups;
    for (const LineRun& run : lineRuns(selections, false))
    {
        Group group;
        group.replacements.push_back(duplicated(doc, run.first, run.last));
        // Each selection with the copy.
        for (const size_t i : run.selections)
        {
            group.placed.emplace_back(i, lineShifted(selections[i], run.last - run.first + 1));
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<Group> moveLines(const ALTextDocument& doc, const std::vector<ALTextRange>& selections, S32 direction)
{
    std::vector<Group>         groups;
    const std::vector<LineRun> runs = lineRuns(selections, true);
    for (const LineRun& run : runs)
    {
        if ((direction < 0 && run.first == 0) || (direction > 0 && run.last + 1 >= doc.lineCount()))
        {
            return {};
        }
    }
    for (const LineRun& run : runs)
    {
        std::optional<Change> change = moveLinesTo(doc, run.first, run.last, direction < 0 ? run.first - 2 : run.last + 1);
        if (!change)
        {
            continue;
        }
        Group group;
        group.replacements = std::move(change->replacements);
        for (const size_t i : run.selections)
        {
            group.placed.emplace_back(i, lineShifted(selections[i], direction));
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<Group> deleteLines(const ALTextDocument& doc, const std::vector<ALTextRange>& selections)
{
    std::vector<Group> groups;
    for (const LineRun& run : lineRuns(selections, true))
    {
        S32   line   = run.first;
        S32   length = 0;
        Group group;
        group.replacements.push_back({ deleted(doc, run.first, run.last, line, length), std::string() });
        // Each caret on the line that took their place, in its column where
        // that line has it.
        for (const size_t i : run.selections)
        {
            const ALTextPos caret(line, llmin(selections[i].end.column, length));
            group.placed.emplace_back(i, ALTextRange(caret, caret));
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<Group> toggleComment(const ALTextDocument& doc, const std::vector<ALTextRange>& selections, const std::string& token)
{
    const std::vector<LineRun> runs = lineRuns(selections, false);
    // Out where every line that says anything is commented, in all of them.
    bool out      = true;
    bool any_text = false;
    for (const LineRun& run : runs)
    {
        if (const std::optional<bool> all = commented(doc, run.first, run.last, token))
        {
            any_text = true;
            out      = out && *all;
        }
    }
    if (!any_text)
    {
        return {};
    }
    std::vector<Group> groups;
    for (const LineRun& run : runs)
    {
        Group group;
        group.replacements = commentLines(doc, run.first, run.last, token, out);
        if (group.replacements.empty())
        {
            continue;
        }
        // A caret where it was in its text; a selection the lines whole.
        for (const size_t i : run.selections)
        {
            const ALTextRange& selection = selections[i];
            if (selection.empty())
            {
                const ALTextPos caret = placedThrough(group.replacements, selection.end);
                group.placed.emplace_back(i, ALTextRange(caret, caret));
            }
            else
            {
                group.placed.emplace_back(i, wholeLines(doc, selection, group.replacements));
            }
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<Group> joinLines(const ALTextDocument& doc, const std::vector<ALTextRange>& selections)
{
    // Each selection's lines, or a caret's line and the next, gathered
    // where they share a line.
    std::vector<LineRun> runs;
    for (size_t i = 0; i < selections.size(); ++i)
    {
        const auto [first, selected] = selectedLines(selections[i]);
        const S32 last               = llmax(selected, first + 1);
        if (!runs.empty() && first <= runs.back().last)
        {
            runs.back().last = std::max(runs.back().last, last);
            runs.back().selections.push_back(i);
            continue;
        }
        LineRun run;
        run.first = first;
        run.last  = last;
        run.selections.push_back(i);
        runs.push_back(std::move(run));
    }
    std::vector<Group> groups;
    for (const LineRun& run : runs)
    {
        std::optional<Change> change = joinLines(doc, run.first, run.last, false);
        if (!change)
        {
            continue;
        }
        Group group;
        group.replacements = std::move(change->replacements);
        for (const size_t i : run.selections)
        {
            group.placed.emplace_back(i, ALTextRange(change->caret, change->caret));
        }
        groups.push_back(std::move(group));
    }
    return groups;
}
}
