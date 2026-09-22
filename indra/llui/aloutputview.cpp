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
    // An entry's first line, piece by piece: where the source starts and
    // ends, and where what was said starts, as byte offsets of the line.
    struct Laid
    {
        std::string text;
        S32         stamp       = 0;
        S32         sourceBegin = 0;
        S32         sourceEnd   = 0;
        S32         kindBegin   = 0;
        S32         kindEnd     = 0;
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
        laid.kindBegin = laid.sourceEnd;
        laid.kindEnd   = laid.sourceEnd;
        if (!entry.kind.empty())
        {
            laid.text += " ";
            laid.kindBegin = static_cast<S32>(laid.text.size());
            laid.text += "(" + entry.kind + ")";
            laid.kindEnd = static_cast<S32>(laid.text.size());
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
            from = nl + 1;
        }
        return laid;
    }
}

ALOutputView::Params::Params()
:   capacity("capacity", 500),
    time_color("time_color"),
    time_font("time_font"),
    source_color("source_color"),
    kind_color("kind_color"),
    narrow_columns("narrow_columns", 48)
{
    changeDefault(read_only, true);
    changeDefault(word_wrap, true);
}

ALOutputView::ALOutputView(const Params& p)
:   ALTextView(p),
    mCapacity(llmax(1, p.capacity())),
    mNarrowColumns(llmax(1, p.narrow_columns()))
{
    // Unless the skin says: the stamps a little dimmer and smaller than
    // the words, the sources a little brighter, the kinds as the stamps.
    mTimeColor   = p.time_color.isProvided() ? p.time_color() : LLUIColor(lerp(backgroundColor(), textColor(), 0.55f));
    mTimeFont    = p.time_font.isProvided() ? p.time_font() : LLFontGL::getFontSansSerifSmall();
    mSourceColor = p.source_color.isProvided() ? p.source_color() : LLUIColor(lerp(textColor(), LLColor4::white, 0.6f));
    mKindColor   = p.kind_color.isProvided() ? p.kind_color() : mTimeColor;
    onLinkClicked([this](const Substitution& link) { followed(link); });
    // What was said hangs under where it began: an entry's first line
    // wraps under the message's start, and its lines after the first
    // start there -- in a pane wide enough for that; in a narrow one
    // they hang under the source instead, past the time stamp, so that
    // a long source name does not leave a message a few words wide. An
    // entry may say otherwise: under the source come what may, or not
    // at all.
    layout().setIndentProvider([this](S32 line) {
        ALTextLayout::Indent indent;
        S32                  first = 0;
        const Shown*         shown = shownAt(line, &first);
        if (!shown || shown->text <= 0 || shown->hang == Hang::None)
        {
            return indent;
        }
        F32       hang = 0.f;
        const S32 wrap = layout().wrapWidth();
        if (shown->hang == Hang::Source || (wrap > 0 && wrap < narrowWidth()))
        {
            hang = stampWidth(*shown, first);
        }
        else
        {
            hang = prefixWidth(*shown, first);
        }
        if (wrap > 0)
        {
            hang = llmin(hang, static_cast<F32>(wrap) * 0.4f);
        }
        indent.rest  = hang;
        indent.first = line == first ? 0.f : hang;
        return indent;
    });
}

S32 ALOutputView::narrowWidth() const
{
    const LLFontGL* font = getFont();
    return font ? mNarrowColumns * llmax(1, font->getWidth("0")) : 0;
}

F32 ALOutputView::stampWidth(const Shown& shown, S32 first) const
{
    const std::string& line = document().line(first);
    const LLFontGL*    font = mTimeFont ? mTimeFont : getFont();
    if (!font || shown.stamp <= 0 || shown.stamp > static_cast<S32>(line.size()))
    {
        return 0.f;
    }
    return static_cast<F32>(font->getWidth(line.substr(0, static_cast<size_t>(shown.stamp))));
}

F32 ALOutputView::prefixWidth(const Shown& shown, S32 first) const
{
    // The first line up to what was said, measured piece by piece in
    // the faces the pieces are shown in, once: the layout asks for every
    // row it lays out, which is every scroll.
    const LLFontGL* font = getFont();
    if (shown.measured == font && shown.prefix >= 0.f)
    {
        return shown.prefix;
    }
    const std::string& line = document().line(first);
    if (!font || shown.text > static_cast<S32>(line.size()))
    {
        return 0.f;
    }
    const LLFontGL* stamp_font = mTimeFont ? mTimeFont : font;
    const LLFontGL* bold       = font->faceFor(LLFontGL::BOLD);
    F32             width      = 0.f;
    width += static_cast<F32>(stamp_font->getWidth(line.substr(0, static_cast<size_t>(shown.stamp))));
    width += static_cast<F32>(font->getWidth(line.substr(static_cast<size_t>(shown.stamp), static_cast<size_t>(shown.sourceBegin - shown.stamp))));
    width += static_cast<F32>((bold ? bold : font)->getWidth(line.substr(static_cast<size_t>(shown.sourceBegin), static_cast<size_t>(shown.sourceEnd - shown.sourceBegin))));
    width += static_cast<F32>(font->getWidth(line.substr(static_cast<size_t>(shown.sourceEnd), static_cast<size_t>(shown.text - shown.sourceEnd))));
    shown.prefix   = width;
    shown.measured = font;
    return width;
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
    mShown.push_back(showAt(entry, serial, document().lineCount(), !mShown.empty()));
    ++mShownGeneration;
}

ALOutputView::Shown ALOutputView::showAt(const Entry& entry, U32 serial, S32 line, bool among_others)
{
    const Laid laid  = layEntry(entry);
    S32        first = line;
    const S32  lines = 1 + static_cast<S32>(std::count(laid.text.begin(), laid.text.end(), '\n'));
    if (!among_others)
    {
        // The first: the text whole, in place of the one empty line.
        document().replace(ALTextRange(document().start(), document().end()), laid.text);
        first = 0;
    }
    else if (line < document().lineCount())
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
    shown.serial      = serial;
    shown.lines       = lines;
    shown.stamp       = laid.stamp;
    shown.sourceBegin = laid.sourceBegin;
    shown.sourceEnd   = laid.sourceEnd;
    shown.kindBegin   = laid.kindBegin;
    shown.kindEnd     = laid.kindEnd;
    shown.text        = laid.textBegin;
    shown.hang        = entry.hang;

    // The stamp in its smaller face, the source bold; the colours are
    // laid on as the rows are drawn.
    if (laid.stamp > 0 && mTimeFont && mTimeFont != getFont())
    {
        Style stamp;
        stamp.range = ALTextRange(ALTextPos(first, 0), ALTextPos(first, laid.stamp));
        stamp.font  = mTimeFont;
        addStyle(std::move(stamp));
    }
    if (laid.sourceEnd > laid.sourceBegin)
    {
        Style source;
        source.range = ALTextRange(ALTextPos(first, laid.sourceBegin), ALTextPos(first, laid.sourceEnd));
        source.flags = LLFontGL::BOLD;
        addStyle(std::move(source));
    }

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
    for (S32 l = first + 1; l < first + shown.lines; ++l)
    {
        linkUrlsOn(l);
    }
    return shown;
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
    // The serials go up as the entries came, so the one wanted is found
    // by halving.
    const auto at = std::lower_bound(mSerials.begin(), mSerials.end(), serial);
    if (at == mSerials.end() || *at != serial)
    {
        return nullptr;
    }
    return &mEntries[static_cast<size_t>(at - mSerials.begin())];
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
            ++mShownGeneration;
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
    ++mShownGeneration;
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
            ++mShownGeneration;
        }
    }
}

void ALOutputView::setFilter(filter_t filter)
{
    mFilter = std::move(filter);
    refill();
}

void ALOutputView::hideAt(S32 line, S32 lines)
{
    if (lines >= document().lineCount())
    {
        document().replace(ALTextRange(document().start(), document().end()), std::string());
    }
    else if (line + lines < document().lineCount())
    {
        // Its lines and the break after them.
        document().remove(ALTextRange(document().lineStart(line), document().lineStart(line + lines)));
    }
    else
    {
        // The last: the break before it and its lines.
        document().remove(ALTextRange(document().lineEnd(line - 1), document().end()));
    }
}

void ALOutputView::refill()
{
    // What is shown brought to what the filter takes now, entry by
    // entry against what was shown: the ones that stay keep their lines
    // and their links, the ones the filter drops lose them, the ones it
    // takes in get theirs -- rather than the whole text again, which a
    // long log would feel. The shown are gathered anew in one pass.
    const bool        follow = atTail();
    std::deque<Shown> next;
    size_t            old  = 0;
    S32               line = 0;
    for (size_t i = 0; i < mEntries.size(); ++i)
    {
        const bool was = old < mShown.size() && mShown[old].serial == mSerials[i];
        const bool now = passes(mEntries[i]);
        if (was && now)
        {
            next.push_back(mShown[old]);
            line += mShown[old++].lines;
        }
        else if (was)
        {
            hideAt(line, mShown[old++].lines);
        }
        else if (now)
        {
            // Among others where any are shown before it, or any of the
            // old are still ahead of it in the text.
            next.push_back(showAt(mEntries[i], mSerials[i], line, !next.empty() || old < mShown.size()));
            line += next.back().lines;
        }
    }
    while (old < mShown.size())
    {
        // Shown, but no entry's any more.
        hideAt(line, mShown[old++].lines);
    }
    mShown.swap(next);
    ++mShownGeneration;
    if (follow)
    {
        followTail();
    }
}

const ALOutputView::Shown* ALOutputView::shownAt(S32 line, S32* first_line) const
{
    if (mFirstsOf != mShownGeneration)
    {
        mFirsts.clear();
        mFirsts.reserve(mShown.size());
        S32 first = 0;
        for (const Shown& shown : mShown)
        {
            mFirsts.push_back(first);
            first += shown.lines;
        }
        mFirstsOf = mShownGeneration;
    }
    // The last entry whose first line is at or before the line.
    const auto after = std::upper_bound(mFirsts.begin(), mFirsts.end(), line);
    if (after == mFirsts.begin())
    {
        return nullptr;
    }
    const size_t index = static_cast<size_t>(after - mFirsts.begin()) - 1;
    if (line >= mFirsts[index] + mShown[index].lines)
    {
        return nullptr;
    }
    if (first_line)
    {
        *first_line = mFirsts[index];
    }
    return &mShown[index];
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
    const bool   head  = line == first;
    if (!head && (!entry || !entry->color))
    {
        return;
    }
    // The first line piece by piece -- the stamp, the source, the kind,
    // then what was said -- and the lines under it as what was said.
    const LLColor4U time(mTimeColor.get() % alpha);
    const LLColor4U source(mSourceColor.get() % alpha);
    const LLColor4U kind((entry && entry->color ? *entry->color : mKindColor.get()) % alpha);
    const LLColor4U ink((entry && entry->color ? *entry->color : textColor()) % alpha);
    for (size_t k = 0; k < colors.size(); ++k)
    {
        const S32 cluster = laid.glyphs[row.glyphBegin + k].cluster;
        // A link keeps its colour.
        if (const Substitution* sub = substitutionAt(ALTextPos(line, cluster)); sub && sub->link)
        {
            continue;
        }
        if (!head || cluster >= shown->text)
        {
            colors[k] = ink;
        }
        else if (cluster < shown->stamp)
        {
            colors[k] = time;
        }
        else if (cluster >= shown->sourceBegin && cluster < shown->sourceEnd)
        {
            colors[k] = source;
        }
        else if (cluster >= shown->kindBegin && cluster < shown->kindEnd)
        {
            colors[k] = kind;
        }
        else
        {
            colors[k] = time;
        }
    }
}
