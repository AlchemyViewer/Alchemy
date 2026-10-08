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

#include "alanchoredranges.h"
#include "allinebreaks.h"
#include "alviewtype.h"
#include "llsdutil.h"
#include "llurlaction.h"
#include "lluicolortable.h"

#include <algorithm>
#include <string_view>

static LLDefaultChildRegistry::Register<ALOutputView> r("output_view");

namespace
{
    // One more link kept in order, as the view adds one: within a line,
    // and left out where it would lie over one already there.
    void addInOrder(std::vector<ALTextView::Substitution>& links, ALTextView::Substitution link)
    {
        link.range = link.range.normalised();
        if (link.range.begin.line != link.range.end.line || link.range.empty())
        {
            return;
        }
        if (const auto at = alDisjointPlace(links, link.range))
        {
            links.insert(*at, std::move(link));
        }
    }

    // A range laid on an entry's lines, moved down to where its first is.
    ALTextRange below(const ALTextRange& range, S32 first)
    {
        return ALTextRange(ALTextPos(range.begin.line + first, range.begin.column), ALTextPos(range.end.line + first, range.end.column));
    }
}

// static
ALOutputView::Laid ALOutputView::lay(const Entry& entry)
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
    // Said again and again: how many times, with the kind.
    const std::string kind = entry.times > 1 ? (entry.kind.empty() ? std::string() : entry.kind + " ") + "\xC3\x97" + std::to_string(entry.times)
                                             : entry.kind;
    if (!kind.empty())
    {
        laid.text += " ";
        laid.kindBegin = static_cast<S32>(laid.text.size());
        laid.text += "(" + kind + ")";
        laid.kindEnd = static_cast<S32>(laid.text.size());
    }
    if (!entry.source.empty() || !kind.empty())
    {
        laid.text += ": ";
    }
    laid.textBegin = static_cast<S32>(laid.text.size());
    // What was said, each of its breaks -- LF alone, as append() keeps
    // them -- a line more.
    laid.text += entry.text;
    laid.lines += static_cast<S32>(std::count(entry.text.begin(), entry.text.end(), '\n'));
    return laid;
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
    return lay(entry).text;
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

// static
ALOutputView::Shown ALOutputView::shownOf(const Laid& laid, U32 serial, Hang hang)
{
    Shown shown;
    shown.serial      = serial;
    shown.lines       = laid.lines;
    shown.stamp       = laid.stamp;
    shown.sourceBegin = laid.sourceBegin;
    shown.sourceEnd   = laid.sourceEnd;
    shown.kindBegin   = laid.kindBegin;
    shown.kindEnd     = laid.kindEnd;
    shown.text        = laid.textBegin;
    shown.hang        = hang;
    return shown;
}

const ALOutputView::Decor& ALOutputView::decorOf(size_t index, const Laid& laid)
{
    Decor& decor = mDecor[index];
    if (decor.made)
    {
        return decor;
    }
    decor.made         = true;
    const Entry& entry = mEntries[index];
    // The stamp in its smaller face, the source bold; the colours are
    // laid on as the rows are drawn.
    if (laid.stamp > 0 && mTimeFont && mTimeFont != getFont())
    {
        Style stamp;
        stamp.range = ALTextRange(ALTextPos(0, 0), ALTextPos(0, laid.stamp));
        stamp.font  = mTimeFont;
        decor.styles.push_back(std::move(stamp));
    }
    if (laid.sourceEnd > laid.sourceBegin)
    {
        Style source;
        source.range = ALTextRange(ALTextPos(0, laid.sourceBegin), ALTextPos(0, laid.sourceEnd));
        source.flags = LLFontGL::BOLD;
        decor.styles.push_back(std::move(source));
    }

    if (entry.link && laid.sourceEnd > laid.sourceBegin)
    {
        Substitution source;
        source.range   = ALTextRange(ALTextPos(0, laid.sourceBegin), ALTextPos(0, laid.sourceEnd));
        source.link    = true;
        source.tooltip = entry.tooltip;
        source.value   = LLSD(static_cast<S32>(mSerials[index]));
        addInOrder(decor.links, std::move(source));
    }
    // The lines as shown, for the links on them: split as the document
    // splits them.
    const std::vector<std::string_view> lines = ALLineBreaks::views(laid.text);
    // The entry's own links within what was said: a line's bytes as the
    // entry has them are the text's, but for the first line's, which
    // start after the stamp and the source.
    for (size_t i = 0; i < entry.links.size(); ++i)
    {
        const Entry::Link& link = entry.links[i];
        if (link.line < 0 || link.line >= static_cast<S32>(lines.size()))
        {
            continue;
        }
        const std::string_view text   = lines[static_cast<size_t>(link.line)];
        const S32              offset = link.line == 0 ? laid.textBegin : 0;
        const S32              length = static_cast<S32>(text.size());
        S32                    begin  = link.begin >= 0 ? offset + link.begin : offset;
        if (link.begin < 0)
        {
            while (begin < length && (text[begin] == ' ' || text[begin] == '\t'))
            {
                ++begin;
            }
        }
        const S32 end = link.end >= 0 ? llmin(length, offset + link.end) : length;
        if (begin >= end)
        {
            continue;
        }
        Substitution own;
        own.range          = ALTextRange(ALTextPos(link.line, begin), ALTextPos(link.line, end));
        own.link           = true;
        own.tooltip        = link.tooltip;
        own.value["entry"] = static_cast<S32>(mSerials[index]);
        own.value["link"]  = static_cast<S32>(i);
        addInOrder(decor.links, std::move(own));
    }
    for (size_t l = 0; l < lines.size(); ++l)
    {
        for (Substitution& url : urlLinks(std::string(lines[l]), static_cast<S32>(l), l == 0 ? laid.textBegin : 0,
                                          [this](const std::string& url, const std::string& label) { urlLabelled(url, label); }))
        {
            addInOrder(decor.links, std::move(url));
        }
    }
    return decor;
}

void ALOutputView::show(size_t index)
{
    const Laid   laid  = lay(mEntries[index]);
    const Decor& decor = decorOf(index, laid);
    const bool   alone = mShown.empty();
    const S32    first = alone ? 0 : document().lineCount();
    // Known before its lines go in, for the rows laid out as they do.
    mShown.push_back(shownOf(laid, mSerials[index], mEntries[index].hang));
    ++mShownGeneration;
    if (alone)
    {
        // The first: the text whole, in place of the one empty line.
        document().replace(ALTextRange(document().start(), document().end()), laid.text);
    }
    else
    {
        document().append("\n" + laid.text);
    }
    for (const Style& style : decor.styles)
    {
        Style at = style;
        at.range = below(style.range, first);
        addStyle(std::move(at));
    }
    for (const Substitution& link : decor.links)
    {
        Substitution at = link;
        at.range        = below(link.range, first);
        addSubstitution(std::move(at));
    }
}

void ALOutputView::urlLabelled(const std::string& url, const std::string& label)
{
    // So that an entry the filter shows again shows the name.
    for (Decor& decor : mDecor)
    {
        for (Substitution& link : decor.links)
        {
            if (link.link && link.url == url)
            {
                link.shown = label;
            }
        }
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
    if (link.value.isMap())
    {
        // One of the entry's own links: the entry, with the link's value.
        const Entry* entry = entryOf(static_cast<U32>(link.value["entry"].asInteger()));
        const size_t index = static_cast<size_t>(link.value["link"].asInteger());
        if (entry && index < entry->links.size())
        {
            Entry chosen = *entry;
            chosen.value = entry->links[index].value;
            mEntryChosen(chosen);
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
    entry.lane = llmin<U8>(entry.lane, LANES - 1);
    // What was said in the lines the document makes of it: a lone CR is a
    // break there as LF is, and CRLF one break, so that the lines counted
    // for an entry are the lines it is shown over.
    if (entry.text.find('\r') != std::string::npos)
    {
        entry.text = ALLineBreaks::withLineFeeds(entry.text);
    }
    // The same as the last thing said: that one, said once more.
    if (!mEntries.empty())
    {
        Entry&     last       = mEntries.back();
        const auto same_links = [&]() {
            if (last.links.size() != entry.links.size())
            {
                return false;
            }
            for (size_t i = 0; i < last.links.size(); ++i)
            {
                const Entry::Link& a = last.links[i];
                const Entry::Link& b = entry.links[i];
                if (a.line != b.line || a.begin != b.begin || a.end != b.end || a.tooltip != b.tooltip || !llsd_equals(a.value, b.value))
                {
                    return false;
                }
            }
            return true;
        };
        if (last.lane == entry.lane && last.source == entry.source && last.kind == entry.kind && last.text == entry.text &&
            llsd_equals(last.key, entry.key) && llsd_equals(last.value, entry.value) && same_links())
        {
            last.times += entry.times;
            last.time = entry.time;
            again(mEntries.size() - 1);
            return;
        }
    }
    const bool follow = atTail();
    const U32  serial = mNextSerial++;
    const U8   lane   = entry.lane;
    mEntries.push_back(std::move(entry));
    mSerials.push_back(serial);
    mDecor.emplace_back();
    ++mLaneCount[lane];
    trim(lane);
    if (!mEntries.empty() && mSerials.back() == serial && passes(mEntries.back()))
    {
        show(mEntries.size() - 1);
        if (follow)
        {
            followTail();
        }
    }
}

void ALOutputView::again(size_t index)
{
    // Shown last: its lines taken out and put back as it reads now, with
    // what lies on them worked out again for the count's width.
    mDecor[index] = Decor();
    if (mShown.empty() || mShown.back().serial != mSerials[index])
    {
        return;
    }
    const bool follow = atTail();
    const S32  lines  = mShown.back().lines;
    const S32  first  = document().lineCount() - lines;
    mShown.pop_back();
    ++mShownGeneration;
    if (first > 0)
    {
        document().replace(ALTextRange(document().lineEnd(first - 1), document().end()), std::string());
    }
    else
    {
        document().replace(ALTextRange(document().start(), document().end()), std::string());
    }
    show(index);
    if (follow)
    {
        followTail();
    }
}

void ALOutputView::clearEntries()
{
    mEntries.clear();
    mSerials.clear();
    mDecor.clear();
    mShown.clear();
    for (S32& count : mLaneCount)
    {
        count = 0;
    }
    ++mShownGeneration;
    setText("");
}

S32 ALOutputView::capacity(U8 lane) const
{
    return lane < LANES && mLaneCapacity[lane] > 0 ? mLaneCapacity[lane] : mCapacity;
}

void ALOutputView::setCapacity(S32 capacity, U8 lane)
{
    if (lane >= LANES)
    {
        return;
    }
    if (lane == 0)
    {
        mCapacity = llmax(1, capacity);
    }
    else
    {
        mLaneCapacity[lane] = llmax(1, capacity);
    }
    for (U8 each = 0; each < LANES; ++each)
    {
        trim(each);
    }
}

void ALOutputView::trim(U8 lane)
{
    const S32 fill = capacity(lane);
    if (mLaneCount[lane] <= fill)
    {
        return;
    }
    // Past its fill, a lane lets go of its oldest down to seven eighths of
    // it at once: the text's front taken out once in so many entries, not
    // once for each that comes, each time moving every line below it.
    S32                 drop = mLaneCount[lane] - (fill - fill / 8);
    std::vector<size_t> going;
    for (size_t i = 0; i < mEntries.size() && drop > 0; ++i)
    {
        if (mEntries[i].lane == lane)
        {
            going.push_back(i);
            --drop;
        }
    }
    removeEntries(going);
    if (drop > 0)
    {
        // Fewer than it counted: none of it is left.
        mLaneCount[lane] = 0;
    }
}

void ALOutputView::removeEntries(const std::vector<size_t>& going)
{
    if (going.empty())
    {
        return;
    }
    // The lines of those shown, in runs, and how much of them lies above
    // the top of the view -- the oldest are the text's first lines, most
    // of the time, but a lane's may lie anywhere.
    std::vector<std::pair<S32, S32>> runs;
    std::vector<bool>                hidden(mShown.size(), false);
    S32                              above = 0;
    {
        size_t g    = 0;
        S32    line = 0;
        for (size_t i = 0; i < mShown.size() && g < going.size(); ++i)
        {
            const U32 serial = mShown[i].serial;
            while (g < going.size() && mSerials[going[g]] < serial)
            {
                ++g;
            }
            const S32 lines = mShown[i].lines;
            if (g < going.size() && mSerials[going[g]] == serial)
            {
                // What is in sight stays in sight: lines let go of above
                // the top of the view take their height off the scroll, or
                // the text a reader has scrolled back to crawls up by an
                // entry for every entry that comes in.
                const S32 top    = layout().lineTop(line);
                const S32 height = layout().lineTop(line + lines) - top;
                above += llclamp(scrollY() - top, 0, height);
                if (!runs.empty() && runs.back().second == line)
                {
                    runs.back().second += lines;
                }
                else
                {
                    runs.emplace_back(line, line + lines);
                }
                hidden[i] = true;
            }
            line += lines;
        }
    }
    // The log without them, in one pass over each list.
    {
        size_t g    = 0;
        size_t kept = 0;
        for (size_t i = 0; i < mEntries.size(); ++i)
        {
            if (g < going.size() && going[g] == i)
            {
                --mLaneCount[mEntries[i].lane];
                ++g;
                continue;
            }
            if (kept != i)
            {
                mEntries[kept] = std::move(mEntries[i]);
                mSerials[kept] = mSerials[i];
                mDecor[kept]   = std::move(mDecor[i]);
            }
            ++kept;
        }
        mEntries.resize(kept);
        mSerials.resize(kept);
        mDecor.resize(kept);
    }
    if (runs.empty())
    {
        return;
    }
    {
        std::deque<Shown> left;
        for (size_t i = 0; i < mShown.size(); ++i)
        {
            if (!hidden[i])
            {
                left.push_back(mShown[i]);
            }
        }
        mShown.swap(left);
        ++mShownGeneration;
    }
    // Each run its lines and the break after them; the last, the break
    // before it; all of them, the whole.
    const S32                                        count = document().lineCount();
    std::vector<std::pair<ALTextRange, std::string>> cuts;
    for (const auto& [first, end] : runs)
    {
        if (end < count)
        {
            cuts.emplace_back(ALTextRange(document().lineStart(first), document().lineStart(end)), std::string());
        }
        else if (first > 0)
        {
            cuts.emplace_back(ALTextRange(document().lineEnd(first - 1), document().end()), std::string());
        }
        else
        {
            cuts.emplace_back(ALTextRange(document().start(), document().end()), std::string());
        }
    }
    document().replaceMany(std::move(cuts));
    if (above > 0)
    {
        setScrollY(scrollY() - above);
    }
}

void ALOutputView::setFilter(filter_t filter)
{
    mFilter = std::move(filter);
    refill();
}

void ALOutputView::refill()
{
    // What the filter takes now, laid out as one text and put in as one
    // edit: each entry with what lies on it as it was worked out the
    // first time it was shown, moved to where it lies now -- rather than
    // an edit for each entry the filter takes in or drops, each sliding
    // everything below it, and each taken in read for URLs again.
    const bool          follow = atTail();
    std::vector<size_t> taken;
    for (size_t i = 0; i < mEntries.size(); ++i)
    {
        if (passes(mEntries[i]))
        {
            taken.push_back(i);
        }
    }
    const bool same = taken.size() == mShown.size() &&
                      std::equal(taken.begin(), taken.end(), mShown.begin(), [this](size_t i, const Shown& shown) { return mSerials[i] == shown.serial; });
    if (same)
    {
        return;
    }
    // The selection, by the entry each end is in, to stay on what it was
    // on where that is still shown.
    struct Held
    {
        U32 serial = 0;
        S32 line   = 0;
        S32 column = 0;
    };
    const auto hold = [this](const ALTextPos& pos) {
        S32          first = 0;
        const Shown* shown = shownAt(pos.line, &first);
        return shown ? Held{ shown->serial, pos.line - first, pos.column } : Held{};
    };
    const ALTextRange was_selected = selection();
    const Held        anchor       = hold(was_selected.begin);
    const Held        caret        = hold(was_selected.end);
    std::optional<ALTextPos> anchor_at, caret_at;

    std::string               text;
    std::deque<Shown>         next;
    std::vector<Style>        styles;
    std::vector<Substitution> links;
    size_t                    old  = 0;
    S32                       line = 0;
    for (const size_t i : taken)
    {
        const Laid   laid  = lay(mEntries[i]);
        const Decor& decor = decorOf(i, laid);
        if (!next.empty())
        {
            text += '\n';
        }
        text += laid.text;
        Shown shown = shownOf(laid, mSerials[i], mEntries[i].hang);
        // One shown before keeps what was measured of it.
        while (old < mShown.size() && mShown[old].serial < mSerials[i])
        {
            ++old;
        }
        if (old < mShown.size() && mShown[old].serial == mSerials[i])
        {
            shown.prefix   = mShown[old].prefix;
            shown.measured = mShown[old].measured;
        }
        if (anchor.serial == mSerials[i])
        {
            anchor_at = ALTextPos(line + anchor.line, anchor.column);
        }
        if (caret.serial == mSerials[i])
        {
            caret_at = ALTextPos(line + caret.line, caret.column);
        }
        for (const Style& style : decor.styles)
        {
            styles.push_back(style);
            styles.back().range = below(style.range, line);
        }
        for (const Substitution& link : decor.links)
        {
            links.push_back(link);
            links.back().range = below(link.range, line);
        }
        line += shown.lines;
        next.push_back(shown);
    }
    // Known before the lines go in, for the rows laid out as they do.
    mShown.swap(next);
    ++mShownGeneration;
    document().replace(ALTextRange(document().start(), document().end()), text);
    setStyles(std::move(styles));
    setSubstitutions(std::move(links));
    if (!was_selected.empty() && anchor_at && caret_at)
    {
        placeSelection(document().clamp(*anchor_at), document().clamp(*caret_at));
    }
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
