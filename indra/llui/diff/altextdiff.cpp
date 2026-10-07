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
#include "allinebreaks.h"
#include "aldifftokens.h"
#include "allinediff.h"
#include "alstructuraldiff.h"
#include "alworddiff.h"

#include <algorithm>
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
        case Algorithm::Structural:
            return "structural";
        default:
            return "histogram";
    }
}

std::optional<ALTextDiff::Algorithm> ALTextDiff::algorithmFromName(std::string_view name)
{
    for (const Algorithm algorithm : { Algorithm::Histogram, Algorithm::Patience, Algorithm::Minimal, Algorithm::Structural })
    {
        if (name == algorithmName(algorithm))
        {
            return algorithm;
        }
    }
    return std::nullopt;
}

namespace
{
    // A line or a word as it is compared, put after what `out` holds: as
    // likenessOf() says, but kept with others in one string rather than a
    // string of its own each.
    void appendLikeness(std::string& out, std::string_view text, const ALTextDiff::Likeness& like, const ALTextDiff::regions_t* regions)
    {
        typedef ALTextDiff::Region Region;
        const size_t from = out.size();
        // The region each byte is in, the pieces in order: a comment's left
        // out where comments are let go of, a string's as it is.
        size_t     piece    = 0;
        const auto regionAt = [&](size_t at) {
            if (!regions)
            {
                return Region::Code;
            }
            while (piece < regions->size() && static_cast<S32>(at) >= (*regions)[piece].end)
            {
                ++piece;
            }
            return piece < regions->size() && static_cast<S32>(at) >= (*regions)[piece].begin ? (*regions)[piece].region : Region::Code;
        };
        bool blanks = false;
        bool cut    = false;
        for (size_t at = 0; at < text.size(); ++at)
        {
            const char   c      = text[at];
            const Region region = regionAt(at);
            if (region == Region::Comment && like.ignoreComments)
            {
                cut = true;
                continue;
            }
            const bool quoted = region == Region::String;
            if (!quoted && like.ignoreWhitespace && ALDiffTokens::blank(c))
            {
                blanks = true;
                continue;
            }
            if (blanks && out.size() > from)
            {
                out += ' ';
            }
            blanks = false;
            out += !quoted && like.ignoreCase && c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }
        // Blanks at its end let go of where they are, or where a comment after
        // them was; a line of blanks alone nothing where blank lines are.
        size_t end = out.size();
        while (end > from && ALDiffTokens::blank(out[end - 1]))
        {
            --end;
        }
        if (end == from ? like.ignoreBlankLines || like.ignoreTrailing || cut : like.ignoreTrailing || cut)
        {
            out.resize(end);
        }
    }
}

std::string ALTextDiff::likenessOf(std::string_view text, const Likeness& like, const regions_t* regions)
{
    std::string out;
    out.reserve(text.size());
    appendLikeness(out, text, like, regions);
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
        // Each text's ids as they are where the stretch is all of it, as it
        // is without anchors; else the stretch's own.
        const bool              whole = l == 0 && r == 0 && static_cast<size_t>(l_end) == a.size() && static_cast<size_t>(r_end) == b.size();
        std::vector<S32>        part_a;
        std::vector<S32>        part_b;
        if (!whole)
        {
            part_a.assign(a.begin() + l, a.begin() + l_end);
            part_b.assign(b.begin() + r, b.begin() + r_end);
        }
        const std::vector<S32>& some_a = whole ? a : part_a;
        const std::vector<S32>& some_b = whole ? b : part_b;
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

    // The runs that make the left the right by lines, found the way asked:
    // each line told the same by its regions, where those of every line of
    // both texts are given and they change how lines are told the same.
    std::vector<Run> byLines(ALTextDiff::Algorithm algorithm, const std::vector<std::string>& left, const std::vector<std::string>& right,
                             const ALTextDiff::Options& options, std::span<const ALTextDiff::regions_t> left_given,
                             std::span<const ALTextDiff::regions_t> right_given)
    {
        const ALTextDiff::Likeness&  like          = options.like;
        const bool                   regioned      = like.byRegions() && left_given.size() == left.size() && right_given.size() == right.size();
        const ALTextDiff::regions_t* left_regions  = regioned ? left_given.data() : nullptr;
        const ALTextDiff::regions_t* right_regions = regioned ? right_given.data() : nullptr;
        // Each line as compared, all of them in one string kept while their
        // ids are, each where the one before it ends; the ids of both texts
        // made once, whatever stretches they are then compared in.
        std::string         keys;
        std::vector<size_t> key_ends;
        if (like.any())
        {
            size_t bytes = 0;
            for (const std::vector<std::string>* text : { &left, &right })
            {
                for (const std::string& line : *text)
                {
                    bytes += line.size();
                }
            }
            keys.reserve(bytes);
            key_ends.reserve(left.size() + right.size());
            for (size_t i = 0; i < left.size(); ++i)
            {
                appendLikeness(keys, left[i], like, left_regions ? &left_regions[i] : nullptr);
                key_ends.push_back(keys.size());
            }
            for (size_t i = 0; i < right.size(); ++i)
            {
                appendLikeness(keys, right[i], like, right_regions ? &right_regions[i] : nullptr);
                key_ends.push_back(keys.size());
            }
        }
        const auto keyOf = [&keys, &key_ends](size_t at) {
            const size_t from = at == 0 ? 0 : key_ends[at - 1];
            return std::string_view(keys).substr(from, key_ends[at] - from);
        };
        ALDiffIds        ids;
        std::vector<S32> a;
        std::vector<S32> b;
        a.reserve(left.size());
        b.reserve(right.size());
        for (size_t i = 0; i < left.size(); ++i)
        {
            a.push_back(ids.idOf(like.any() ? keyOf(i) : std::string_view(left[i])));
        }
        for (size_t i = 0; i < right.size(); ++i)
        {
            b.push_back(ids.idOf(like.any() ? keyOf(left.size() + i) : std::string_view(right[i])));
        }
        const S32 n = static_cast<S32>(left.size());
        const S32 m = static_cast<S32>(right.size());
        if (options.anchors.empty())
        {
            return stretch(a, b, left, right, 0, n, 0, m, algorithm);
        }
        // Lined up at the anchors: each stretch between two on its own.
        const ALTextDiff::anchors_t kept = ALTextDiff::keptAnchors(options.anchors, n, m);
        std::vector<Run>            out;
        const auto                  push = [&out](const Run& run) { ALLineDiff::keep(out, run); };
        S32 l = 0;
        S32 r = 0;
        for (size_t i = 0; i <= kept.size(); ++i)
        {
            const S32 to_left  = i < kept.size() ? kept[i].first : n;
            const S32 to_right = i < kept.size() ? kept[i].second : m;
            // The stretch before the pair, on its own.
            for (const Run& run : stretch(a, b, left, right, l, to_left, r, to_right, algorithm))
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
}

// The anchors that can be kept: within both texts, by the right then the
// left the other way, so that of two on one line of the right the longest
// rising run takes one at most; then that run, rising on the left.
ALTextDiff::anchors_t ALTextDiff::keptAnchors(const anchors_t& anchors, S32 left_size, S32 right_size)
{
    // Those within both texts rising both ways already, as anchorsOf
    // leaves a conversion's: every one of them kept, as they are.
    ALTextDiff::anchors_t within;
    within.reserve(anchors.size());
    bool rising = true;
    for (const std::pair<S32, S32>& pair : anchors)
    {
        if (pair.first >= 0 && pair.second >= 0 && pair.first < left_size && pair.second < right_size)
        {
            rising = within.empty() || (pair.first > within.back().first && pair.second > within.back().second);
            if (!rising)
            {
                break;
            }
            within.push_back(pair);
        }
    }
    if (rising)
    {
        return within;
    }
    // Each as (right, left), to rise in the left.
    ALTextDiff::anchors_t given;
    for (const std::pair<S32, S32>& pair : anchors)
    {
        if (pair.first >= 0 && pair.second >= 0 && pair.first < left_size && pair.second < right_size)
        {
            given.emplace_back(pair.second, pair.first);
        }
    }
    std::sort(given.begin(), given.end(), [](const std::pair<S32, S32>& a, const std::pair<S32, S32>& b) {
        return a.first != b.first ? a.first < b.first : a.second > b.second;
    });
    ALTextDiff::anchors_t kept = ALLineDiff::longestRising(given);
    for (std::pair<S32, S32>& pair : kept)
    {
        std::swap(pair.first, pair.second);
    }
    return kept;
}

std::vector<ALTextDiff::Run> ALTextDiff::lines(const std::vector<std::string>& left, const std::vector<std::string>& right, const Options& options)
{
    return linesBy(options.algorithm, left, right, options);
}

std::vector<ALTextDiff::Run> ALTextDiff::lines(const std::vector<std::string>& left, const std::vector<std::string>& right, const Options& options,
                                               std::span<const regions_t> left_regions, std::span<const regions_t> right_regions)
{
    return byLines(options.algorithm == Algorithm::Structural ? Algorithm::Histogram : options.algorithm, left, right, options, left_regions, right_regions);
}

std::vector<ALTextDiff::Run> ALTextDiff::linesBy(Algorithm algorithm, const std::vector<std::string>& left, const std::vector<std::string>& right,
                                                 const Options& options)
{
    if (algorithm == Algorithm::Structural)
    {
        const std::vector<regions_t>* left_regions  = options.lexer ? &options.lexer(left) : nullptr;
        const std::vector<regions_t>* right_regions = options.lexer ? &options.lexer(right) : nullptr;
        return ALStructuralDiff::compare(left, right, options, left_regions, right_regions).runs;
    }
    // Each line's regions, where a grammar says where comments and strings
    // are and that changes how lines are told the same.
    std::span<const regions_t> left_regions;
    std::span<const regions_t> right_regions;
    if (options.like.byRegions() && options.lexer)
    {
        left_regions  = options.lexer(left);
        right_regions = options.lexer(right);
    }
    return byLines(algorithm, left, right, options, left_regions, right_regions);
}

size_t ALTextDiff::changeAt(const std::vector<Run>& runs, size_t from, std::vector<S32>& gone, std::vector<S32>& made)
{
    gone.clear();
    made.clear();
    size_t i = from;
    for (; i < runs.size() && runs[i].kind != Kind::Same; ++i)
    {
        const bool out = runs[i].kind == Kind::Removed;
        for (S32 n = 0; n < runs[i].count; ++n)
        {
            (out ? gone : made).push_back((out ? runs[i].left : runs[i].right) + n);
        }
    }
    return i;
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
    // Each sort passed over where what it sorts is in order already, as a
    // conversion's ranges are, one after another: a rebuild asks for them
    // all again.
    const auto by_first = [&ranges](size_t a, size_t b) {
        return ranges[a].leftFirst != ranges[b].leftFirst ? ranges[a].leftFirst < ranges[b].leftFirst : ranges[a].leftLast > ranges[b].leftLast;
    };
    if (!std::is_sorted(order.begin(), order.end(), by_first))
    {
        std::sort(order.begin(), order.end(), by_first);
    }
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
    if (!std::is_sorted(out.begin(), out.end()))
    {
        std::sort(out.begin(), out.end());
    }
    out.erase(std::unique(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), out.end());
    const auto by_second = [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first > b.first; };
    if (!std::is_sorted(out.begin(), out.end(), by_second))
    {
        std::sort(out.begin(), out.end(), by_second);
    }
    out.erase(std::unique(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second == b.second; }), out.end());
    return out;
}

std::vector<std::string> ALTextDiff::split(std::string_view text)
{
    return ALLineBreaks::split(text);
}

ALTextDiff::Options ALTextDiff::linesOnly(const Options& options)
{
    Options out;
    out.algorithm = options.algorithm == Algorithm::Structural ? Algorithm::Histogram : options.algorithm;
    out.like      = options.like;
    if (options.like.byRegions())
    {
        out.lexer = options.lexer;
    }
    return out;
}

void ALTextDiff::words(std::string_view left, std::string_view right, spans_t& left_out, spans_t& right_out, const Options& options,
                       const regions_t* left_regions, const regions_t* right_regions)
{
    ALWordDiff::diff(left, right, left_out, right_out, options, left_regions, right_regions);
}
