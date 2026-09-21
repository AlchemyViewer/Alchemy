/**
 * @file aloutputview.cpp
 * @brief A log a pane shows as text: what things said, when, in order, with links in it.
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

#include "aloutputview.h"

#include "alviewtype.h"
#include "llurlaction.h"
#include "lluicolortable.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALOutputView> r("output_view");

namespace
{
    // The lines of what was said after the first, under it.
    const char* const CONTINUATION = "    ";

    // An entry's first line, piece by piece: where the source starts and
    // ends, and where what was said starts, as byte offsets of the line.
    struct Laid
    {
        std::string text;
        S32         stamp       = 0;
        S32         sourceBegin = 0;
        S32         sourceEnd   = 0;
        S32         textBegin   = 0;
    };

    Laid layEntry(const ALOutputView::Entry& entry)
    {
        Laid laid;
        if (!entry.time.empty())
        {
            laid.text = "[" + entry.time + "] ";
        }
        laid.stamp       = static_cast<S32>(laid.text.size());
        laid.sourceBegin = laid.stamp;
        laid.text += entry.source;
        laid.sourceEnd = static_cast<S32>(laid.text.size());
        if (!entry.kind.empty())
        {
            laid.text += " (" + entry.kind + ")";
        }
        if (!entry.source.empty() || !entry.kind.empty())
        {
            laid.text += ": ";
        }
        laid.textBegin = static_cast<S32>(laid.text.size());
        // What was said, its lines after the first indented under it.
        size_t from = 0;
        while (true)
        {
            const size_t nl = entry.text.find('\n', from);
            laid.text.append(entry.text, from, nl == std::string::npos ? std::string::npos : nl - from);
            if (nl == std::string::npos)
            {
                break;
            }
            laid.text += '\n';
            laid.text += CONTINUATION;
            from = nl + 1;
        }
        return laid;
    }
}

ALOutputView::Params::Params()
:   capacity("capacity", 500),
    time_color("time_color")
{
    changeDefault(read_only, true);
    changeDefault(word_wrap, true);
}

ALOutputView::ALOutputView(const Params& p)
:   ALTextView(p),
    mCapacity(llmax(1, p.capacity()))
{
    mTimeColor = p.time_color.isProvided() ? p.time_color() : LLUIColor(lerp(backgroundColor(), textColor(), 0.55f));
    onLinkClicked([this](const Substitution& link) { followed(link); });
}

// static
std::string ALOutputView::format(const Entry& entry)
{
    return layEntry(entry).text;
}

bool ALOutputView::atTail()
{
    const S32 page = llmax(1, textRect().getHeight());
    return scrollY() + page >= layout().totalHeight() - layout().rowHeight();
}

void ALOutputView::followTail()
{
    setScrollY(layout().totalHeight());
}

void ALOutputView::show(const Entry& entry, U32 serial)
{
    showAt(entry, serial, mShown.size(), document().lineCount());
}

void ALOutputView::showAt(const Entry& entry, U32 serial, size_t index, S32 line)
{
    const Laid laid  = layEntry(entry);
    S32        first = line;
    const S32  lines = 1 + static_cast<S32>(std::count(laid.text.begin(), laid.text.end(), '\n'));
    if (mShown.empty())
    {
        // The first: the text whole, in place of the one empty line.
        document().replace(ALTextRange(document().start(), document().end()), laid.text);
        first = 0;
    }
    else if (index < mShown.size())
    {
        // Before another: its lines and the break after them.
        document().insert(document().lineStart(first), laid.text + "\n");
    }
    else
    {
        document().append("\n" + laid.text);
        first = document().lineCount() - lines;
    }
    Shown shown;
    shown.serial = serial;
    shown.lines  = lines;
    shown.stamp  = laid.stamp;
    mShown.insert(mShown.begin() + static_cast<std::ptrdiff_t>(index), shown);

    if (entry.link && laid.sourceEnd > laid.sourceBegin)
    {
        Substitution source;
        source.range   = ALTextRange(ALTextPos(first, laid.sourceBegin), ALTextPos(first, laid.sourceEnd));
        source.link    = true;
        source.tooltip = entry.tooltip;
        source.value   = LLSD(static_cast<S32>(serial));
        addSubstitution(std::move(source));
    }
    linkUrlsOn(first, laid.textBegin);
    for (S32 line = first + 1; line < first + shown.lines; ++line)
    {
        linkUrlsOn(line);
    }
}

void ALOutputView::followed(const Substitution& link)
{
    if (!link.url.empty())
    {
        if (mUrlChosen.empty())
        {
            LLUrlAction::clickAction(link.url, false);
        }
        else
        {
            mUrlChosen(link.url);
        }
        return;
    }
    if (const Entry* entry = entryOf(static_cast<U32>(link.value.asInteger())))
    {
        const Entry chosen = *entry;
        mEntryChosen(chosen);
    }
}

const ALOutputView::Entry* ALOutputView::entryOf(U32 serial) const
{
    for (size_t i = 0; i < mSerials.size(); ++i)
    {
        if (mSerials[i] == serial)
        {
            return &mEntries[i];
        }
    }
    return nullptr;
}

void ALOutputView::append(Entry entry)
{
    const bool follow = atTail();
    const U32  serial = mNextSerial++;
    mEntries.push_back(std::move(entry));
    mSerials.push_back(serial);
    while (static_cast<S32>(mEntries.size()) > mCapacity)
    {
        const U32 oldest = mSerials.front();
        mEntries.pop_front();
        mSerials.pop_front();
        if (!mShown.empty() && mShown.front().serial == oldest)
        {
            document().removeFirstLines(mShown.front().lines);
            mShown.pop_front();
        }
    }
    if (passes(mEntries.back()))
    {
        show(mEntries.back(), serial);
        if (follow)
        {
            followTail();
        }
    }
}

void ALOutputView::clearEntries()
{
    mEntries.clear();
    mSerials.clear();
    mShown.clear();
    setText("");
}

void ALOutputView::setCapacity(S32 capacity)
{
    mCapacity = llmax(1, capacity);
    while (static_cast<S32>(mEntries.size()) > mCapacity)
    {
        const U32 oldest = mSerials.front();
        mEntries.pop_front();
        mSerials.pop_front();
        if (!mShown.empty() && mShown.front().serial == oldest)
        {
            document().removeFirstLines(mShown.front().lines);
            mShown.pop_front();
        }
    }
}

void ALOutputView::setFilter(filter_t filter)
{
    mFilter = std::move(filter);
    refill();
}

void ALOutputView::hideAt(size_t index, S32 line)
{
    const S32 lines = mShown[index].lines;
    if (mShown.size() == 1)
    {
        document().replace(ALTextRange(document().start(), document().end()), std::string());
    }
    else if (index + 1 < mShown.size())
    {
        // Its lines and the break after them.
        document().remove(ALTextRange(document().lineStart(line), document().lineStart(line + lines)));
    }
    else
    {
        // The last: the break before it and its lines.
        document().remove(ALTextRange(document().lineEnd(line - 1), document().end()));
    }
    mShown.erase(mShown.begin() + static_cast<std::ptrdiff_t>(index));
}

void ALOutputView::refill()
{
    // What is shown brought to what the filter takes now, entry by
    // entry against what was shown: the ones that stay keep their lines
    // and their links, the ones the filter drops lose them, the ones it
    // takes in get theirs -- rather than the whole text again, which a
    // long log would feel.
    const bool follow = atTail();
    size_t     shown  = 0;
    S32        line   = 0;
    for (size_t i = 0; i < mEntries.size(); ++i)
    {
        const bool was = shown < mShown.size() && mShown[shown].serial == mSerials[i];
        const bool now = passes(mEntries[i]);
        if (was && now)
        {
            line += mShown[shown++].lines;
        }
        else if (was)
        {
            hideAt(shown, line);
        }
        else if (now)
        {
            showAt(mEntries[i], mSerials[i], shown, line);
            line += mShown[shown++].lines;
        }
    }
    while (shown < mShown.size())
    {
        // Shown, but no entry's any more.
        hideAt(shown, line);
    }
    if (follow)
    {
        followTail();
    }
}

const ALOutputView::Shown* ALOutputView::shownAt(S32 line, S32* first_line) const
{
    S32 first = 0;
    for (const Shown& shown : mShown)
    {
        if (line < first + shown.lines)
        {
            if (first_line)
            {
                *first_line = first;
            }
            return &shown;
        }
        first += shown.lines;
    }
    return nullptr;
}

void ALOutputView::tintRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha, std::vector<LLColor4U>& colors)
{
    S32          first = 0;
    const Shown* shown = shownAt(line, &first);
    if (!shown)
    {
        return;
    }
    const Entry* entry = entryOf(shown->serial);
    const bool   stamp = line == first && shown->stamp > 0;
    if (!stamp && (!entry || !entry->color))
    {
        return;
    }
    const LLColor4U time(mTimeColor.get() % alpha);
    const LLColor4U ink(entry && entry->color ? *entry->color % alpha : textColor() % alpha);
    for (size_t k = 0; k < colors.size(); ++k)
    {
        const S32 cluster = laid.glyphs[row.glyphBegin + k].cluster;
        // A link keeps its colour.
        if (const Substitution* sub = substitutionAt(ALTextPos(line, cluster)); sub && sub->link)
        {
            continue;
        }
        if (stamp && cluster < shown->stamp)
        {
            colors[k] = time;
        }
        else if (entry && entry->color)
        {
            colors[k] = ink;
        }
    }
}
