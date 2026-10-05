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

#include <algorithm>

namespace
{
    typedef ALTextDiff::Run  Run;
    typedef ALTextDiff::Kind Kind;

    // Some lines' tokens, blanks left out: each's line, where it is on it,
    // and its id as compared.
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
        for (const S32 n : which)
        {
            const std::string& text = lines[static_cast<size_t>(n)];
            ALDiffSame::cut(text, regions && regions->size() == lines.size() ? &(*regions)[static_cast<size_t>(n)] : nullptr, options.same.get(), words);
            for (const ALDiffTokens::Token& token : words)
            {
                if (ALDiffTokens::isBlank(text, token))
                {
                    continue;
                }
                out.line.push_back(n);
                out.token.push_back(token);
                out.id.push_back(ALDiffSame::idOf(ids, std::string_view(text).substr(static_cast<size_t>(token.begin), static_cast<size_t>(token.end - token.begin)),
                                                  options.same.get(), options.like.ignoreCase));
            }
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

}

ALStructuralDiff::Result ALStructuralDiff::compare(const std::vector<std::string>& left, const std::vector<std::string>& right, const ALTextDiff::Options& options,
                                                   const std::vector<ALTextDiff::regions_t>* left_regions,
                                                   const std::vector<ALTextDiff::regions_t>* right_regions)
{
    ALTextDiff::Options by_lines = options;
    by_lines.algorithm           = ALTextDiff::Algorithm::Histogram;
    return read(left, right, ALTextDiff::lines(left, right, by_lines), options, left_regions, right_regions);
}

ALStructuralDiff::Result ALStructuralDiff::read(const std::vector<std::string>& left, const std::vector<std::string>& right, std::vector<ALTextDiff::Run> runs,
                                                const ALTextDiff::Options& options, const std::vector<ALTextDiff::regions_t>* left_regions,
                                                const std::vector<ALTextDiff::regions_t>* right_regions)
{
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
        ALDiffIds ids;
        Tokens    a;
        Tokens    b;
        tokensOf(left, gone, left_regions, options, ids, a);
        tokensOf(right, made, right_regions, options, ids, b);
        if (static_cast<S32>(a.id.size()) > MOST_TOKENS || static_cast<S32>(b.id.size()) > MOST_TOKENS)
        {
            result.tooLarge = true;
            continue;
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
    }
    return result;
}
