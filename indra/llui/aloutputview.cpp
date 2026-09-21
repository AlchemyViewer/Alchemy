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
#include "llurlmatch.h"
#include "llurlregistry.h"
#include "lluicolortable.h"

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
    const Laid laid  = layEntry(entry);
    S32        first = document().lineCount();
    if (document().empty() && mShown.empty())
    {
        document().replace(ALTextRange(document().start(), document().end()), laid.text);
        first = 0;
    }
    else
    {
        document().append("\n" + laid.text);
    }
    Shown shown;
    shown.serial = serial;
    shown.lines  = document().lineCount() - first;
    shown.stamp  = laid.stamp;
    mShown.push_back(shown);

    if (entry.link && laid.sourceEnd > laid.sourceBegin)
    {
        Substitution source;
        source.range   = ALTextRange(ALTextPos(first, laid.sourceBegin), ALTextPos(first, laid.sourceEnd));
        source.link    = true;
        source.tooltip = entry.tooltip;
        source.value   = LLSD(static_cast<S32>(serial));
        addSubstitution(std::move(source));
    }
    linkUrls(first, laid.textBegin);
    for (S32 line = first + 1; line < first + shown.lines; ++line)
    {
        linkUrls(line, 0);
    }
}

void ALOutputView::linkUrls(S32 line, S32 from)
{
    const std::string& text = document().line(line);
    if (from >= static_cast<S32>(text.size()))
    {
        return;
    }
    const LLHandle<ALOutputView> self = getDerivedHandle<ALOutputView>();
    // A name that arrives later goes to every link of the URL it is for.
    const auto relabelled = [self](const std::string& url, const std::string& label, const std::string&) {
        ALOutputView* view = self.get();
        if (!view)
        {
            return;
        }
        for (const Substitution& sub : view->substitutions())
        {
            if (sub.link && sub.value.isMap() && sub.value["url"].asStringRef() == url)
            {
                view->relabel(sub.range, label);
            }
        }
    };
    std::string rest = text.substr(static_cast<size_t>(from));
    S32         at   = from;
    LLUrlMatch  match;
    while (!rest.empty() && LLUrlRegistry::instance().findUrl(rest, match, relabelled))
    {
        const S32 begin = at + static_cast<S32>(match.getStart());
        const S32 end   = at + static_cast<S32>(match.getEnd()) + 1;
        if (end <= begin)
        {
            break;
        }
        Substitution link;
        link.range        = ALTextRange(ALTextPos(line, begin), ALTextPos(line, end));
        link.link         = true;
        link.tooltip      = match.getTooltip();
        link.value["url"] = match.getUrl();
        const std::string matched = text.substr(static_cast<size_t>(begin), static_cast<size_t>(end - begin));
        if (!match.getLabel().empty() && match.getLabel() != matched)
        {
            link.shown = match.getLabel();
        }
        addSubstitution(std::move(link));
        rest = rest.substr(match.getEnd() + 1);
        at   = end;
    }
}

void ALOutputView::followed(const Substitution& link)
{
    if (link.value.isMap())
    {
        const std::string url = link.value["url"].asString();
        if (url.empty())
        {
            return;
        }
        if (mUrlChosen.empty())
        {
            LLUrlAction::clickAction(url, false);
        }
        else
        {
            mUrlChosen(url);
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
    if (static_cast<S32>(mEntries.size()) > mCapacity)
    {
        while (static_cast<S32>(mEntries.size()) > mCapacity)
        {
            mEntries.pop_front();
            mSerials.pop_front();
        }
        refill();
    }
}

void ALOutputView::setFilter(filter_t filter)
{
    mFilter = std::move(filter);
    refill();
}

void ALOutputView::refill()
{
    const bool follow = atTail();
    mShown.clear();
    setText("");
    for (size_t i = 0; i < mEntries.size(); ++i)
    {
        if (passes(mEntries[i]))
        {
            show(mEntries[i], mSerials[i]);
        }
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
