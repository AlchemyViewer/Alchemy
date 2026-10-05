/**
 * @file altextdiff.cpp
 * @brief How two texts differ: by lines, and within a changed line by words.
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

#include "altextdiff.h"

#include "aldiffids.h"
#include "aldifftokens.h"
#include "allinediff.h"
#include "alworddiff.h"

#include <algorithm>
#include <limits>
#include <span>

namespace
{
    typedef ALTextDiff::Run  Run;
    typedef ALTextDiff::Kind Kind;
}

const char* ALTextDiff::algorithmName(Algorithm algorithm)
{
    switch (algorithm)
    {
        case Algorithm::Patience:
            return "patience";
        case Algorithm::Minimal:
            return "minimal";
        default:
            return "histogram";
    }
}

std::optional<ALTextDiff::Algorithm> ALTextDiff::algorithmFromName(std::string_view name)
{
    for (const Algorithm algorithm : { Algorithm::Histogram, Algorithm::Patience, Algorithm::Minimal })
    {
        if (name == algorithmName(algorithm))
        {
            return algorithm;
        }
    }
    return std::nullopt;
}

std::string ALTextDiff::likenessOf(std::string_view text, const Likeness& like, const regions_t* regions)
{
    std::string out;
    out.reserve(text.size());
    // What of it is compared: all of it, or what no comment covers.
    const auto commented = [&](size_t at) {
        if (!like.ignoreComments || !regions)
        {
            return false;
        }
        for (const Piece& piece : *regions)
        {
            if (piece.region == Region::Comment && static_cast<S32>(at) >= piece.begin && static_cast<S32>(at) < piece.end)
            {
                return true;
            }
        }
        return false;
    };
    bool blanks = false;
    bool cut    = false;
    for (size_t at = 0; at < text.size(); ++at)
    {
        const char c = text[at];
        if (commented(at))
        {
            cut = true;
            continue;
        }
        if (like.ignoreWhitespace && ALDiffTokens::blank(c))
        {
            blanks = true;
            continue;
        }
        if (blanks && !out.empty())
        {
            out += ' ';
        }
        blanks = false;
        out += like.ignoreCase && c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    // Blanks at its end let go of where they are, or where a comment after
    // them was; a line of blanks alone nothing where blank lines are.
    const size_t end = out.find_last_not_of(" \t");
    if (end == std::string::npos)
    {
        if (like.ignoreBlankLines || like.ignoreTrailing || cut)
        {
            out.clear();
        }
    }
    else if (like.ignoreTrailing || cut)
    {
        out.resize(end + 1);
    }
    return out;
}

bool ALTextDiff::ignorable(std::string_view line, const Likeness& like, const regions_t* regions)
{
    if (!like.ignoreBlankLines && !like.ignoreComments)
    {
        return false;
    }
    const bool blank = line.find_first_not_of(" \t") == std::string_view::npos;
    if (blank)
    {
        return like.ignoreBlankLines;
    }
    if (!like.ignoreComments || !regions)
    {
        return false;
    }
    // Nothing but a comment and blanks.
    const bool has_comment = std::any_of(regions->begin(), regions->end(), [](const Piece& piece) { return piece.region == Region::Comment; });
    return has_comment && likenessOf(line, Likeness{ true, false, true, true, true }, regions).empty();
}

namespace
{
    // The lines of a stretch of each text, from `l` to `l_end` of the left
    // and `r` to `r_end` of the right, by their ids: found the way asked,
    // slid, and counted in the whole texts.
    std::vector<Run> stretch(const std::vector<S32>& a, const std::vector<S32>& b, const std::vector<std::string>& left, const std::vector<std::string>& right,
                             S32 l, S32 l_end, S32 r, S32 r_end, ALTextDiff::Algorithm algorithm)
    {
        const std::vector<S32> some_a(a.begin() + l, a.begin() + l_end);
        const std::vector<S32> some_b(b.begin() + r, b.begin() + r_end);
        std::vector<Run>       out = algorithm == ALTextDiff::Algorithm::Patience  ? ALLineDiff::patience(some_a, some_b)
                                     : algorithm == ALTextDiff::Algorithm::Minimal ? ALLineDiff::minimal(some_a, some_b)
                                                                                   : ALLineDiff::histogram(some_a, some_b);
        ALLineDiff::slide(out, some_a, some_b, std::span<const std::string>(left).subspan(static_cast<size_t>(l), static_cast<size_t>(l_end - l)),
                          std::span<const std::string>(right).subspan(static_cast<size_t>(r), static_cast<size_t>(r_end - r)));
        if (l != 0 || r != 0)
        {
            for (Run& run : out)
            {
                run.left += l;
                run.right += r;
            }
        }
        return out;
    }

    // The anchors that can be kept: within both texts, by the right then
    // the left the other way, so that of two on one line of the right the
    // longest rising run takes one at most; then that run, rising on the
    // left.
    ALTextDiff::anchors_t keptAnchors(const ALTextDiff::anchors_t& anchors, S32 left_size, S32 right_size)
    {
        ALTextDiff::anchors_t given;
        for (const std::pair<S32, S32>& pair : anchors)
        {
            if (pair.first >= 0 && pair.second >= 0 && pair.first < left_size && pair.second < right_size)
            {
                given.push_back(pair);
            }
        }
        std::sort(given.begin(), given.end(), [](const std::pair<S32, S32>& a, const std::pair<S32, S32>& b) {
            return a.second != b.second ? a.second < b.second : a.first > b.first;
        });
        std::vector<size_t> tails;  // the pair ending the best run of each length
        std::vector<size_t> before(given.size(), std::numeric_limits<size_t>::max());
        for (size_t i = 0; i < given.size(); ++i)
        {
            const auto at = std::lower_bound(tails.begin(), tails.end(), given[i].first,
                                             [&given](size_t tail, S32 first) { return given[tail].first < first; });
            if (at != tails.begin())
            {
                before[i] = *(at - 1);
            }
            if (at == tails.end())
            {
                tails.push_back(i);
            }
            else
            {
                *at = i;
            }
        }
        ALTextDiff::anchors_t kept;
        for (size_t i = tails.empty() ? std::numeric_limits<size_t>::max() : tails.back(); i != std::numeric_limits<size_t>::max(); i = before[i])
        {
            kept.push_back(given[i]);
        }
        std::reverse(kept.begin(), kept.end());
        return kept;
    }
}

std::vector<ALTextDiff::Run> ALTextDiff::lines(const std::vector<std::string>& left, const std::vector<std::string>& right, const Options& options)
{
    const Likeness& like = options.like;
    // Each line's regions, where comments are let go of and a grammar says
    // where they are.
    const std::vector<regions_t>* left_regions  = nullptr;
    const std::vector<regions_t>* right_regions = nullptr;
    if (like.ignoreComments && options.lexer)
    {
        left_regions  = &options.lexer(left);
        right_regions = &options.lexer(right);
        if (left_regions->size() != left.size() || right_regions->size() != right.size())
        {
            left_regions  = nullptr;
            right_regions = nullptr;
        }
    }
    // Each line as compared, kept while its id is; the ids of both texts
    // made once, whatever stretches they are then compared in.
    std::vector<std::string> keys;
    if (like.any())
    {
        keys.reserve(left.size() + right.size());
        for (size_t i = 0; i < left.size(); ++i)
        {
            keys.push_back(likenessOf(left[i], like, left_regions ? &(*left_regions)[i] : nullptr));
        }
        for (size_t i = 0; i < right.size(); ++i)
        {
            keys.push_back(likenessOf(right[i], like, right_regions ? &(*right_regions)[i] : nullptr));
        }
    }
    ALDiffIds        ids;
    std::vector<S32> a;
    std::vector<S32> b;
    a.reserve(left.size());
    b.reserve(right.size());
    for (size_t i = 0; i < left.size(); ++i)
    {
        a.push_back(ids.idOf(like.any() ? keys[i] : left[i]));
    }
    for (size_t i = 0; i < right.size(); ++i)
    {
        b.push_back(ids.idOf(like.any() ? keys[left.size() + i] : right[i]));
    }
    const S32 n = static_cast<S32>(left.size());
    const S32 m = static_cast<S32>(right.size());
    if (options.anchors.empty())
    {
        return stretch(a, b, left, right, 0, n, 0, m, options.algorithm);
    }
    // Lined up at the anchors: each stretch between two on its own.
    const anchors_t  kept = keptAnchors(options.anchors, n, m);
    std::vector<Run> out;
    const auto       push = [&out](const Run& run) {
        if (run.count <= 0 && run.kind != Kind::Same)
        {
            return;
        }
        if (run.count > 0 && !out.empty() && out.back().kind == run.kind && out.back().count > 0)
        {
            Run&       last = out.back();
            const bool next = run.kind == Kind::Same      ? last.left + last.count == run.left && last.right + last.count == run.right
                              : run.kind == Kind::Removed ? last.left + last.count == run.left && last.right == run.right
                                                          : last.right + last.count == run.right && last.left == run.left;
            if (next)
            {
                last.count += run.count;
                return;
            }
        }
        out.push_back(run);
    };
    S32 l = 0;
    S32 r = 0;
    for (size_t i = 0; i <= kept.size(); ++i)
    {
        const S32 to_left  = i < kept.size() ? kept[i].first : n;
        const S32 to_right = i < kept.size() ? kept[i].second : m;
        // The stretch before the pair, on its own.
        for (const Run& run : stretch(a, b, left, right, l, to_left, r, to_right, options.algorithm))
        {
            push(run);
        }
        if (i == kept.size())
        {
            break;
        }
        if (a[static_cast<size_t>(to_left)] == b[static_cast<size_t>(to_right)])
        {
            push(Run{ Kind::Same, to_left, to_right, 1 });
        }
        else
        {
            // Parted from a change just before, so that the pair stands
            // first in its own.
            if (!out.empty() && out.back().kind != Kind::Same)
            {
                out.push_back(Run{ Kind::Same, to_left, to_right, 0 });
            }
            push(Run{ Kind::Removed, to_left, to_right, 1 });
            push(Run{ Kind::Added, to_left + 1, to_right, 1 });
        }
        l = to_left + 1;
        r = to_right + 1;
    }
    return out;
}

ALTextDiff::anchors_t ALTextDiff::anchorsOf(const ranges_t& ranges)
{
    // By their first lines on the left, the widest first: those open as
    // each is come to, kept on a stack, are the ones its first line is in.
    std::vector<size_t> order(ranges.size());
    for (size_t n = 0; n < ranges.size(); ++n)
    {
        order[n] = n;
    }
    std::sort(order.begin(), order.end(), [&ranges](size_t a, size_t b) {
        return ranges[a].leftFirst != ranges[b].leftFirst ? ranges[a].leftFirst < ranges[b].leftFirst : ranges[a].leftLast > ranges[b].leftLast;
    });
    anchors_t           out;
    std::vector<size_t> open;
    out.reserve(ranges.size() * 2);
    for (const size_t n : order)
    {
        const Range& range = ranges[n];
        while (!open.empty() && ranges[open.back()].leftLast < range.leftFirst)
        {
            open.pop_back();
        }
        out.emplace_back(range.leftFirst, range.rightFirst);
        // Its end, but where a range around it holds one of the two lines
        // after it and not the other.
        const S32  left  = range.leftLast + 1;
        const S32  right = range.rightLast + 1;
        const bool cuts  = std::any_of(open.begin(), open.end(), [&](size_t o) {
            const Range& around = ranges[o];
            const bool   inside = around.leftLast >= range.leftLast && around.rightFirst <= range.rightFirst && around.rightLast >= range.rightLast;
            return inside && (left <= around.leftLast) != (right <= around.rightLast);
        });
        if (!cuts)
        {
            out.emplace_back(left, right);
        }
        open.push_back(n);
    }
    // One anchor a line each way: on a line of the left, the earliest of
    // the right; then on a line of the right, the latest of the left.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), out.end());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first > b.first; });
    out.erase(std::unique(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second == b.second; }), out.end());
    return out;
}

std::vector<std::string> ALTextDiff::split(std::string_view text)
{
    std::vector<std::string> out;
    size_t                   start = 0;
    while (true)
    {
        const size_t end  = text.find('\n', start);
        std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        out.emplace_back(line);
        if (end == std::string_view::npos)
        {
            return out;
        }
        start = end + 1;
    }
}

void ALTextDiff::words(std::string_view left, std::string_view right, spans_t& left_out, spans_t& right_out, const Options& options,
                       const regions_t* left_regions, const regions_t* right_regions)
{
    ALWordDiff::diff(left, right, left_out, right_out, options, left_regions, right_regions);
}
