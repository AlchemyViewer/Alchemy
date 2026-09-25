/**
 * @file alsnippetsession.cpp
 * @brief A snippet or a call being filled in, in a code editor: its stops, their mirrors, and where the caret lands.
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

#include "alsnippetsession.h"

#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <optional>

// static
size_t ALSnippetSession::parameterListAt(std::string_view detail, std::string_view name)
{
    if (!name.empty())
    {
        const std::string called = std::string(name) + "(";
        if (const size_t at = detail.find(called); at != std::string_view::npos)
        {
            return at + name.size();
        }
    }
    return detail.find('(');
}

// static
std::vector<std::string> ALSnippetSession::parameterNames(std::string_view detail, std::string_view name)
{
    std::vector<std::string> names;
    const size_t             open = parameterListAt(detail, name);
    if (open == std::string::npos)
    {
        return names;
    }
    // To the bracket that closes it, minding the ones inside.
    size_t close = open + 1;
    for (S32 depth = 1; close < detail.size() && depth > 0; ++close)
    {
        if (detail[close] == '(')
        {
            ++depth;
        }
        else if (detail[close] == ')')
        {
            if (--depth == 0)
            {
                break;
            }
        }
    }
    if (close >= detail.size())
    {
        return names;
    }
    const std::string_view inside = detail.substr(open + 1, close - open - 1);
    size_t                 at     = 0;
    S32                    depth  = 0;
    std::string            piece;
    auto take = [&]() {
        // "integer channel", "channel: number", "...any" or "channel".
        size_t a = piece.find_first_not_of(' ');
        size_t z = piece.find_last_not_of(' ');
        if (a == std::string::npos)
        {
            return;
        }
        std::string one = piece.substr(a, z - a + 1);
        if (const size_t colon = one.find(':'); colon != std::string::npos)
        {
            one = one.substr(0, colon);
        }
        else if (const size_t space = one.rfind(' '); space != std::string::npos)
        {
            one = one.substr(space + 1);
        }
        while (!one.empty() && one.back() == '?')
        {
            one.pop_back();
        }
        if (one.rfind("...", 0) == 0)
        {
            one = "...";
        }
        if (!one.empty())
        {
            names.push_back(one);
        }
    };
    for (; at < inside.size(); ++at)
    {
        const char c = inside[at];
        if (c == '(' || c == '<' || c == '{' || c == '[')
        {
            ++depth;
        }
        else if (c == ')' || c == '>' || c == '}' || c == ']')
        {
            --depth;
        }
        if (c == ',' && depth == 0)
        {
            take();
            piece.clear();
        }
        else
        {
            piece.push_back(c);
        }
    }
    take();
    return names;
}

// static
ALSnippetSession::Expansion ALSnippetSession::expand(std::string_view body, const ALTextPos& at, const std::string& indent)
{
    // Each placeholder's place in the text, by number, with what it held
    // as written.
    struct Place
    {
        S32         number;
        ALTextRange range;
    };
    Expansion                                   out;
    std::string&                                text = out.text;
    std::vector<Place>                          places;
    boost::unordered_flat_map<S32, std::string> held;
    std::optional<ALTextRange>                  end;
    ALTextPos                                   pos = at;
    auto                                        put = [&](char ch) {
        text += ch;
        if (ch == '\n')
        {
            ++pos.line;
            pos.column = 0;
            text += indent;
            pos.column += static_cast<S32>(indent.size());
        }
        else
        {
            ++pos.column;
        }
    };
    // Where the brace closing a placeholder opened at `open` is, minding
    // the ones inside it and the escaped; npos where it is never closed.
    auto closing = [&body](size_t open) {
        S32 depth = 0;
        for (size_t k = open; k < body.size(); ++k)
        {
            if (body[k] == '\\' && k + 1 < body.size() && (body[k + 1] == '$' || body[k + 1] == '}'))
            {
                ++k;
            }
            else if (body[k] == '$' && k + 1 < body.size() && body[k + 1] == '{')
            {
                ++depth;
                ++k;
            }
            else if (body[k] == '}' && --depth == 0)
            {
                return k;
            }
        }
        return std::string_view::npos;
    };
    // The body from `i` to `stop`, placeholders within placeholders read
    // the same way.
    std::function<void(size_t, size_t)> read = [&](size_t i, size_t stop) {
        while (i < stop)
        {
            const char ch = body[i];
            if (ch == '\\' && i + 1 < stop && (body[i + 1] == '$' || body[i + 1] == '}'))
            {
                put(body[i + 1]);
                i += 2;
                continue;
            }
            if (ch != '$' || i + 1 >= stop)
            {
                put(ch);
                ++i;
                continue;
            }
            if (body[i + 1] == '$')
            {
                put('$');
                i += 2;
                continue;
            }
            // $n, ${n} or ${n:text}
            const bool braced = body[i + 1] == '{';
            size_t     digits = i + (braced ? 2 : 1);
            size_t     past   = digits;
            while (past < stop && isdigit(static_cast<unsigned char>(body[past])))
            {
                ++past;
            }
            const size_t close = braced ? closing(i) : past;
            if (past == digits || (braced && (close == std::string_view::npos || close > stop || (body[past] != ':' && body[past] != '}'))))
            {
                put(ch);
                ++i;
                continue;
            }
            const S32       number = atoi(std::string(body.substr(digits, past - digits)).c_str());
            const ALTextPos from   = pos;
            const size_t    began  = text.size();
            if (braced && body[past] == ':')
            {
                read(past + 1, close);
            }
            else if (const auto first = held.find(number); first != held.end() && number != 0)
            {
                // A mirror written bare shows what its first holds, as it
                // stands: indented already.
                for (const char c : first->second)
                {
                    text += c;
                    if (c == '\n')
                    {
                        ++pos.line;
                        pos.column = 0;
                    }
                    else
                    {
                        ++pos.column;
                    }
                }
            }
            if (number == 0)
            {
                end = ALTextRange(from, pos);
            }
            else
            {
                places.push_back(Place{ number, ALTextRange(from, pos) });
                held.emplace(number, text.substr(began));
            }
            i = braced ? close + 1 : past;
        }
    };
    read(0, body.size());
    // Each number's first place the stop, the rest its mirrors; the stops
    // in the order of their numbers.
    std::stable_sort(places.begin(), places.end(), [](const Place& a, const Place& b) { return a.number < b.number; });
    for (size_t k = 0; k < places.size(); ++k)
    {
        if (k > 0 && places[k].number == places[k - 1].number)
        {
            out.mirrors.push_back(Mirror{ static_cast<S32>(out.stops.size()) - 1, places[k].range });
            continue;
        }
        out.stops.push_back(places[k].range);
    }
    out.landing = end.value_or(ALTextRange(pos, pos));
    return out;
}

void ALSnippetSession::start(std::vector<ALTextRange> stops, const ALTextPos& after, std::vector<Mirror> mirrors, S32 landing)
{
    mStops   = std::move(stops);
    mAfter   = after;
    mAt      = mStops.empty() ? -1 : 0;
    mMirrors = std::move(mirrors);
    mLanding = landing;
    mSyncing = -1;
}

void ALSnippetSession::clear()
{
    mMirrors.clear();
    mLanding = 0;
    mStops.clear();
    mAt = -1;
}

void ALSnippetSession::slide(const ALTextDocument::Edit& edit)
{
    if (mStops.empty())
    {
        return;
    }
    const ALTextRange removed = edit.range.normalised();
    for (S32 k = 0; k < static_cast<S32>(mMirrors.size());)
    {
        Mirror& mirror = mMirrors[static_cast<size_t>(k)];
        if (k == mSyncing)
        {
            mirror.range = edit.rangeAfter();
            ++k;
        }
        else if (edit.slide(mirror.range))
        {
            ++k;
        }
        else
        {
            mMirrors.erase(mMirrors.begin() + k);
            if (mSyncing > k)
            {
                --mSyncing;
            }
        }
    }
    for (S32 i = 0; i < static_cast<S32>(mStops.size());)
    {
        ALTextRange& r = mStops[i];
        if (i == mAt && r.begin <= removed.begin && removed.end <= r.end)
        {
            r.end = edit.slidPast(r.end);
            ++i;
        }
        else if (edit.slide(r))
        {
            ++i;
        }
        else
        {
            mStops.erase(mStops.begin() + i);
            mMirrors.erase(std::remove_if(mMirrors.begin(), mMirrors.end(), [i](const Mirror& m) { return m.of == i; }), mMirrors.end());
            for (Mirror& m : mMirrors)
            {
                if (m.of > i)
                {
                    --m.of;
                }
            }
            if (mAt > i)
            {
                --mAt;
            }
            else if (mAt == i)
            {
                mAt = -1;
            }
        }
    }
    if (mAfter.line == removed.end.line || mAfter.line > removed.end.line)
    {
        if (removed.end <= mAfter)
        {
            mAfter = edit.slidPast(mAfter);
        }
    }
    if (mStops.empty() || mAt < 0)
    {
        clear();
    }
}

bool ALSnippetSession::reaches(S32 line) const
{
    // A snippet's stops may be on several lines.
    S32 first = mAfter.line;
    S32 last  = mAfter.line;
    for (const ALTextRange& r : mStops)
    {
        first = llmin(first, r.begin.line);
        last  = llmax(last, r.end.line);
    }
    return line >= first && line <= last;
}

std::vector<S32> ALSnippetSession::staleMirrors(S32 index, const ALTextDocument& text, std::string& wanted) const
{
    std::vector<S32> order;
    if (index < 0 || index >= static_cast<S32>(mStops.size()) || mMirrors.empty())
    {
        return order;
    }
    wanted = text.text(mStops[static_cast<size_t>(index)]);
    for (S32 k = 0; k < static_cast<S32>(mMirrors.size()); ++k)
    {
        if (mMirrors[static_cast<size_t>(k)].of == index && text.text(mMirrors[static_cast<size_t>(k)].range) != wanted)
        {
            order.push_back(k);
        }
    }
    std::sort(order.begin(), order.end(), [this](S32 a, S32 b) { return mMirrors[static_cast<size_t>(b)].range.begin < mMirrors[static_cast<size_t>(a)].range.begin; });
    return order;
}
