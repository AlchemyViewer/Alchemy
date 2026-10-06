/**
 * @file alstructuraldiff.cpp
 * @brief Two texts of code compared by their tokens, whatever lines they are on.
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

#include "alstructuraldiff.h"

#include "aldiffids.h"
#include "aldiffsame.h"
#include "aldifftokens.h"
#include "allinediff.h"

#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>

namespace
{
    typedef ALTextDiff::Run  Run;
    typedef ALTextDiff::Kind Kind;

    // A test's hook, of the thread that read.
    thread_local S32 sLastRead = 0;

    // Some lines' tokens, blanks left out and those let go of as words are
    // (ALDiffSame::idsOf): each's line, where it is on it, and its id as
    // compared.
    struct Tokens
    {
        std::vector<S32>                 line;
        std::vector<ALDiffTokens::Token> token;
        std::vector<S32>                 id;
    };

    void tokensOf(const std::vector<std::string>& lines, const std::vector<S32>& which, const std::vector<ALTextDiff::regions_t>* regions,
                  const ALTextDiff::Options& options, ALDiffIds& ids, Tokens& out)
    {
        ALDiffTokens::tokens_t words;
        std::vector<S32>       line_ids;
        for (const S32 n : which)
        {
            const std::string& text = lines[static_cast<size_t>(n)];
            ALDiffSame::idsOf(text, regions && regions->size() == lines.size() ? &(*regions)[static_cast<size_t>(n)] : nullptr, options.same.get(),
                              options.like, true, ids, words, line_ids);
            out.line.insert(out.line.end(), words.size(), n);
            out.token.insert(out.token.end(), words.begin(), words.end());
            out.id.insert(out.id.end(), line_ids.begin(), line_ids.end());
        }
    }

    // Each code bracket's other among the tokens, by its place; -1 for
    // none there, or not a bracket.
    std::vector<S32> bracketsOf(const std::vector<std::string>& lines, const Tokens& tokens)
    {
        std::vector<S32> other(tokens.id.size(), -1);
        std::vector<S32> open;
        for (size_t i = 0; i < tokens.id.size(); ++i)
        {
            const ALDiffTokens::Token& token = tokens.token[i];
            if (token.region != ALTextDiff::Region::Code || token.end - token.begin != 1)
            {
                continue;
            }
            const char c = lines[static_cast<size_t>(tokens.line[i])][static_cast<size_t>(token.begin)];
            if (c == '(' || c == '[' || c == '{')
            {
                open.push_back(static_cast<S32>(i));
            }
            else if ((c == ')' || c == ']' || c == '}') && !open.empty())
            {
                const S32 o = open.back();
                open.pop_back();
                other[static_cast<size_t>(o)] = static_cast<S32>(i);
                other[i]                      = o;
            }
        }
        return other;
    }


    // A change's lines read as tokens into a result the texts' size: those
    // not kept marked, each said to be read so. False, and nothing said,
    // where it has too many tokens to read.
    bool readChange(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<S32>& gone, const std::vector<S32>& made,
                    const ALTextDiff::Options& options, const std::vector<ALTextDiff::regions_t>* left_regions,
                    const std::vector<ALTextDiff::regions_t>* right_regions, ALStructuralDiff::Result& result)
    {
        ALDiffIds ids;
        Tokens    a;
        Tokens    b;
        tokensOf(left, gone, left_regions, options, ids, a);
        tokensOf(right, made, right_regions, options, ids, b);
        if (static_cast<S32>(a.id.size()) > ALStructuralDiff::MOST_TOKENS || static_cast<S32>(b.id.size()) > ALStructuralDiff::MOST_TOKENS)
        {
            return false;
        }
        // Which token of the lines put in each of those taken out was kept
        // beside.
        std::vector<S32> kept_a(a.id.size(), -1);
        std::vector<S32> kept_b(b.id.size(), -1);
        for (const Run& run : ALLineDiff::histogram(a.id, b.id))
        {
            for (S32 k = 0; run.kind == Kind::Same && k < run.count; ++k)
            {
                kept_a[static_cast<size_t>(run.left + k)]  = run.right + k;
                kept_b[static_cast<size_t>(run.right + k)] = run.left + k;
            }
        }
        // A bracket kept only where its other in the change is kept beside
        // the other's own; one whose other is outside the change on both
        // sides, as it is.
        const std::vector<S32> a_other = bracketsOf(left, a);
        const std::vector<S32> b_other = bracketsOf(right, b);
        for (size_t t = 0; t < kept_a.size(); ++t)
        {
            const S32 j = kept_a[t];
            if (j < 0)
            {
                continue;
            }
            const S32 mine   = a_other[t];
            const S32 theirs = b_other[static_cast<size_t>(j)];
            if ((mine < 0) != (theirs < 0) || (mine >= 0 && kept_a[static_cast<size_t>(mine)] != theirs))
            {
                kept_a[t]                      = -1;
                kept_b[static_cast<size_t>(j)] = -1;
            }
        }
        for (const S32 line : gone)
        {
            result.leftByTokens[static_cast<size_t>(line)] = true;
        }
        for (const S32 line : made)
        {
            result.rightByTokens[static_cast<size_t>(line)] = true;
        }
        for (size_t t = 0; t < kept_a.size(); ++t)
        {
            if (kept_a[t] < 0)
            {
                const size_t line = static_cast<size_t>(a.line[t]);
                ALDiffTokens::mark(result.leftMarks[line], left[line], a.token[t].begin, a.token[t].end);
            }
        }
        for (size_t t = 0; t < kept_b.size(); ++t)
        {
            if (kept_b[t] < 0)
            {
                const size_t line = static_cast<size_t>(b.line[t]);
                ALDiffTokens::mark(result.rightMarks[line], right[line], b.token[t].begin, b.token[t].end);
            }
        }
        return true;
    }

    // A change's lines on each side: from its first to past its last, or,
    // where it has none on a side, the place it stands there.
    struct Extent
    {
        S32  from[2] = { 0, 0 };
        S32  to[2]   = { 0, 0 };
        bool operator==(const Extent& other) const = default;
    };

    struct ExtentHash
    {
        size_t operator()(const Extent& extent) const
        {
            size_t hash = 0;
            for (size_t s = 0; s < 2; ++s)
            {
                boost::hash_combine(hash, extent.from[s]);
                boost::hash_combine(hash, extent.to[s]);
            }
            return hash;
        }
    };

    Extent extentOf(const std::vector<Run>& runs, size_t at, const std::vector<S32>& gone, const std::vector<S32>& made)
    {
        Extent out;
        out.from[0] = gone.empty() ? runs[at].left : gone.front();
        out.to[0]   = gone.empty() ? out.from[0] : gone.back() + 1;
        out.from[1] = made.empty() ? runs[at].right : made.front();
        out.to[1]   = made.empty() ? out.from[1] : made.back() + 1;
        return out;
    }

    // Whether a side's lines from..to lie wholly before or after the lines
    // edited there, [head, end): a place with no lines strictly so, since
    // one at the edit's edge could be on either side of it.
    bool clearOf(S32 from, S32 to, S32 head, S32 end) { return from < to ? (to <= head || from >= end) : (from < head || from > end); }

    // A list kept a line each, the lines edited replaced: [head, was_end)
    // now [head, now_end), those after moved along, and the lines edited
    // said nothing of.
    template <typename List>
    void replaceEdited(List& list, S32 head, S32 was_end, S32 now_end)
    {
        using T = typename List::value_type;
        const auto at = [&list](S32 line) { return list.begin() + static_cast<std::ptrdiff_t>(line); };
        if (now_end < was_end)
        {
            list.erase(at(now_end), at(was_end));
        }
        else if (now_end > was_end)
        {
            list.insert(at(was_end), static_cast<size_t>(now_end - was_end), T());
        }
        std::fill(at(head), at(now_end), T());
    }
}

ALStructuralDiff::Result ALStructuralDiff::compare(const std::vector<std::string>& left, const std::vector<std::string>& right, const ALTextDiff::Options& options,
                                                   const std::vector<ALTextDiff::regions_t>* left_regions,
                                                   const std::vector<ALTextDiff::regions_t>* right_regions)
{
    return read(left, right, ALTextDiff::linesBy(ALTextDiff::Algorithm::Histogram, left, right, options), options, left_regions, right_regions);
}

ALStructuralDiff::Result ALStructuralDiff::read(const std::vector<std::string>& left, const std::vector<std::string>& right, std::vector<ALTextDiff::Run> runs,
                                                const ALTextDiff::Options& options, const std::vector<ALTextDiff::regions_t>* left_regions,
                                                const std::vector<ALTextDiff::regions_t>* right_regions)
{
    sLastRead = 0;
    Result result;
    result.leftMarks.resize(left.size());
    result.rightMarks.resize(right.size());
    result.leftByTokens.assign(left.size(), false);
    result.rightByTokens.assign(right.size(), false);
    result.runs = std::move(runs);
    // Each change: the runs between two the same, which a parting of none
    // also ends.
    for (size_t i = 0; i < result.runs.size();)
    {
        if (result.runs[i].kind == Kind::Same)
        {
            ++i;
            continue;
        }
        std::vector<S32> gone;
        std::vector<S32> made;
        i = ALTextDiff::changeAt(result.runs, i, gone, made);
        ++sLastRead;
        if (!readChange(left, right, gone, made, options, left_regions, right_regions, result))
        {
            result.tooLarge = true;
        }
    }
    return result;
}

void ALStructuralDiff::readAgain(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<Run>& was,
                                 const std::vector<Run>& runs, const Edited& edited, const ALTextDiff::Options& options,
                                 const std::vector<ALTextDiff::regions_t>* left_regions, const std::vector<ALTextDiff::regions_t>* right_regions,
                                 Result& result)
{
    result.tooLarge = false;
    // Each side's lines edited, as they were and as they are; what was said
    // of each line moved along with them.
    S32 head[2];
    S32 was_end[2];
    S32 now_end[2];
    S32 shift[2];
    for (size_t s = 0; s < 2; ++s)
    {
        const S32 now = static_cast<S32>((s ? right : left).size());
        head[s]       = edited.edges[s].head;
        was_end[s]    = edited.was[s] - edited.edges[s].tail;
        now_end[s]    = now - edited.edges[s].tail;
        shift[s]      = now - edited.was[s];
        replaceEdited(s ? result.rightMarks : result.leftMarks, head[s], was_end[s], now_end[s]);
        replaceEdited(s ? result.rightByTokens : result.leftByTokens, head[s], was_end[s], now_end[s]);
    }
    const auto wipe = [&result](const Extent& extent) {
        for (S32 line = extent.from[0]; line < extent.to[0]; ++line)
        {
            result.leftMarks[static_cast<size_t>(line)].clear();
            result.leftByTokens[static_cast<size_t>(line)] = false;
        }
        for (S32 line = extent.from[1]; line < extent.to[1]; ++line)
        {
            result.rightMarks[static_cast<size_t>(line)].clear();
            result.rightByTokens[static_cast<size_t>(line)] = false;
        }
    };
    // The changes there were outside the lines edited, where their lines
    // now are: whether one is a change now too.
    boost::unordered_flat_map<Extent, bool, ExtentHash> before;
    std::vector<S32>                                    gone;
    std::vector<S32>                                    made;
    for (size_t i = 0; i < was.size();)
    {
        if (was[i].kind == Kind::Same)
        {
            ++i;
            continue;
        }
        const size_t at = i;
        i               = ALTextDiff::changeAt(was, i, gone, made);
        Extent extent   = extentOf(was, at, gone, made);
        bool   clear    = true;
        for (size_t s = 0; s < 2; ++s)
        {
            clear = clear && clearOf(extent.from[s], extent.to[s], head[s], was_end[s]);
        }
        if (!clear)
        {
            continue;
        }
        for (size_t s = 0; s < 2; ++s)
        {
            const bool after = extent.from[s] >= was_end[s] && (extent.from[s] < extent.to[s] || extent.from[s] > was_end[s]);
            extent.from[s] += after ? shift[s] : 0;
            extent.to[s] += after ? shift[s] : 0;
        }
        before.emplace(extent, false);
    }
    // The changes now: one that was, outside the lines edited, as it was.
    std::vector<std::pair<size_t, Extent>> again;
    for (size_t i = 0; i < runs.size();)
    {
        if (runs[i].kind == Kind::Same)
        {
            ++i;
            continue;
        }
        const size_t at     = i;
        i                   = ALTextDiff::changeAt(runs, i, gone, made);
        const Extent extent = extentOf(runs, at, gone, made);
        bool         clear  = true;
        for (size_t s = 0; s < 2; ++s)
        {
            clear = clear && clearOf(extent.from[s], extent.to[s], head[s], now_end[s]);
        }
        const auto was_one = clear ? before.find(extent) : before.end();
        if (was_one != before.end())
        {
            was_one->second = true;
            continue;
        }
        again.emplace_back(at, extent);
    }
    // What was said of the lines of those there were and are not, let go
    // of; then those now that are not as they were read.
    for (const auto& [extent, still] : before)
    {
        if (!still)
        {
            wipe(extent);
        }
    }
    sLastRead = static_cast<S32>(again.size());
    for (const auto& [at, extent] : again)
    {
        wipe(extent);
        ALTextDiff::changeAt(runs, at, gone, made);
        if (!readChange(left, right, gone, made, options, left_regions, right_regions, result))
        {
            result.tooLarge = true;
        }
    }
}

S32 ALStructuralDiff::lastRead()
{
    return sLastRead;
}
