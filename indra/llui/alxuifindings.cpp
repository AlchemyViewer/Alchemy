/**
 * @file alxuifindings.cpp
 * @brief Every finding the rules have made, kept and asked questions of.
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

#include "alxuifindings.h"

#include "alstringmatch.h"
#include "llstring.h"

#include <algorithm>

void ALXUIFindings::Counts::take(const ALXUILint::Finding& finding, S32 sign)
{
    all += sign;
    switch (finding.severity)
    {
    case ALXUILint::Severity::Error:    errors += sign;     break;
    case ALXUILint::Severity::Warning:  warnings += sign;   break;
    case ALXUILint::Severity::Note:     notes += sign;      break;
    }
    if (finding.fix.did != ALXUILint::Fix::Do::Nothing)
    {
        fixable += sign;
    }
}

void ALXUIFindings::replace(const std::string& file, std::vector<ALXUILint::Finding> found)
{
    if (file.empty())
    {
        return;
    }
    forget(file);

    Counts counts;
    for (const ALXUILint::Finding& finding : found)
    {
        counts.take(finding, 1);
        mTotal.take(finding, 1);
    }
    mFiles.push_back(file);
    mCountByFile.emplace(file, counts);
    mByFile.emplace(file, std::move(found));
}

void ALXUIFindings::forget(std::string_view file)
{
    const auto held = mByFile.find(file);
    if (held == mByFile.end())
    {
        return;
    }
    for (const ALXUILint::Finding& finding : held->second)
    {
        mTotal.take(finding, -1);
    }
    mByFile.erase(held);
    mCountByFile.erase(std::string(file));
    mFiles.erase(std::remove(mFiles.begin(), mFiles.end(), file), mFiles.end());
}

void ALXUIFindings::clear()
{
    mByFile.clear();
    mCountByFile.clear();
    mFiles.clear();
    mTotal = Counts();
}

S32 ALXUIFindings::count(ALXUILint::Severity severity) const
{
    switch (severity)
    {
    case ALXUILint::Severity::Error:    return mTotal.errors;
    case ALXUILint::Severity::Warning:  return mTotal.warnings;
    case ALXUILint::Severity::Note:     return mTotal.notes;
    }
    return 0;
}

S32 ALXUIFindings::countIn(std::string_view file) const
{
    const auto counted = mCountByFile.find(file);
    return counted == mCountByFile.end() ? 0 : counted->second.all;
}

std::vector<std::pair<std::string, S32> > ALXUIFindings::byRule() const
{
    boost::unordered_flat_map<std::string, S32, ll::string_hash, std::equal_to<> > tally;
    for (const auto& [file, found] : mByFile)
    {
        for (const ALXUILint::Finding& finding : found)
        {
            ++tally[ALXUILint::ruleName(finding.rule)];
        }
    }

    std::vector<std::pair<std::string, S32> > rules(tally.begin(), tally.end());
    // Most first, since the biggest heap is what somebody filtering by rule
    // is usually going after; ties by name, so the list does not shuffle
    // itself between two runs that found the same things.
    std::sort(rules.begin(), rules.end(), [](const auto& a, const auto& b)
    {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    return rules;
}

ALXUIFindings::Selected ALXUIFindings::select(const Query& query) const
{
    Selected selected;
    for (const std::string& file : mFiles)
    {
        if (!query.file.empty() && file != query.file)
        {
            continue;
        }
        const auto held = mByFile.find(file);
        if (held == mByFile.end())
        {
            continue;
        }
        for (const ALXUILint::Finding& finding : held->second)
        {
            switch (finding.severity)
            {
            case ALXUILint::Severity::Error:    if (!query.errors)   { continue; } break;
            case ALXUILint::Severity::Warning:  if (!query.warnings) { continue; } break;
            case ALXUILint::Severity::Note:     if (!query.notes)    { continue; } break;
            }
            if (query.fixable && finding.fix.did == ALXUILint::Fix::Do::Nothing)
            {
                continue;
            }
            if (!query.rule.empty() && query.rule != ALXUILint::ruleName(finding.rule))
            {
                continue;
            }
            if (!query.text.empty()
                && !ALStringMatch::containsNoCase(finding.message, query.text)
                && !ALStringMatch::containsNoCase(finding.what, query.text)
                && !ALStringMatch::containsNoCase(file, query.text)
                && !ALStringMatch::containsNoCase(ALXUISelection::toString(finding.path), query.text))
            {
                continue;
            }
            // Counted whether or not it is answered with, so that a list
            // showing the first thousand of something can say so.
            ++selected.total;
            if (!query.limit || selected.found.size() < query.limit)
            {
                selected.found.push_back(&finding);
            }
        }
    }
    return selected;
}
