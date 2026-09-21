/**
 * @file alfindings.h
 * @brief A queryable store of what checkers found, over as many files as were checked, whatever the checker.
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

#pragma once

#include "alstringmatch.h"
#include "llstl.h"
#include "stdtypes.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// What the rules found, over as many files as have been checked.
//
// A lint run is a report: it says what it found and the words go somewhere
// to be read. That is the right shape for a batch and the wrong shape for a
// tool, because a report cannot be asked anything. How many errors are there
// in this file. Which files write an attribute nothing reads. Which of these
// offer to put themselves right, and how many of those are in front of me.
// Each of those is a scan of a text file, or it is a question this answers.
//
// A file is the unit: checking one replaces what it said before and nothing
// else, so the file in front of the developer can be re-checked on every
// rebuild while a pass over the rest of the tree fills in around it.
//
// What a finding is belongs to whoever found it -- XUI Studio's lint, Script
// Studio's checkers and compiler -- so the store takes the type and a
// Traits that reads each finding: its level, its rule's name, whether it
// offers a fix, and whether a word is anywhere in it.
//
//     struct Traits
//     {
//         static ALFindingLevel level(const Finding&);
//         static std::string    rule(const Finding&);
//         static bool           fixable(const Finding&);
//         // Whether the text, matched without regard to case, is in the
//         // finding's words: its message, what it is about, where it is.
//         static bool           mentions(const Finding&, std::string_view text);
//     };
enum class ALFindingLevel : U8
{
    Error,      // this does not work
    Warning,    // this probably does not do what was written
    Note        // worth a look, and often on purpose
};

template <class Finding, class Traits>
class ALFindings
{
public:
    typedef Finding        finding_t;
    typedef ALFindingLevel Level;

    // Everything one file said, in place of whatever it said before. A file
    // that said nothing is still a file that was checked, and is kept as one
    // -- "no findings" and "not looked at" are different answers.
    void replace(const std::string& file, std::vector<Finding> found)
    {
        if (file.empty())
        {
            return;
        }
        forget(file);

        Counts counts;
        for (const Finding& finding : found)
        {
            counts.take(finding, 1);
            mTotal.take(finding, 1);
        }
        mFiles.push_back(file);
        mCountByFile.emplace(file, counts);
        mByFile.emplace(file, std::move(found));
    }

    void forget(std::string_view file)
    {
        const auto held = mByFile.find(file);
        if (held == mByFile.end())
        {
            return;
        }
        for (const Finding& finding : held->second)
        {
            mTotal.take(finding, -1);
        }
        mByFile.erase(held);
        mCountByFile.erase(std::string(file));
        mFiles.erase(std::remove(mFiles.begin(), mFiles.end(), file), mFiles.end());
    }

    void clear()
    {
        mByFile.clear();
        mCountByFile.clear();
        mFiles.clear();
        mTotal = Counts();
    }

    // Which of them to look at. Every field left alone widens rather than
    // narrows, so a Query nobody filled in selects the lot.
    struct Query
    {
        std::string file;           // that one file; empty is all of them
        std::string rule;           // by the name Traits::rule gives
        bool        errors   = true;
        bool        warnings = true;
        bool        notes    = true;
        bool        fixable  = false;   // only the ones that offer a fix
        // A word in the finding, wherever Traits looks for it. Matched
        // without regard to case, since nobody typing a filter is thinking
        // about it.
        std::string text;
        // How many to answer with at most, since a tree's worth of findings
        // is more rows than any list can be asked to hold. Zero is all.
        size_t      limit = 0;
    };

    // Pointers into what is held, in file order and then in the order the
    // rules made them. Good until the next replace or clear, which is the
    // life of a list being filled from them.
    struct Selected
    {
        std::vector<const Finding*> found;
        size_t                      total = 0;  // before the limit
    };

    Selected select(const Query& query) const
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
            for (const Finding& finding : held->second)
            {
                switch (Traits::level(finding))
                {
                    case Level::Error:   if (!query.errors)   { continue; } break;
                    case Level::Warning: if (!query.warnings) { continue; } break;
                    case Level::Note:    if (!query.notes)    { continue; } break;
                }
                if (query.fixable && !Traits::fixable(finding))
                {
                    continue;
                }
                if (!query.rule.empty() && query.rule != Traits::rule(finding))
                {
                    continue;
                }
                if (!query.text.empty() && !Traits::mentions(finding, query.text) && !ALStringMatch::containsNoCase(file, query.text))
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

    // The files that have been checked, in the order they first were.
    const std::vector<std::string>& files() const { return mFiles; }
    bool                            checked(std::string_view file) const { return mByFile.find(file) != mByFile.end(); }

    size_t size() const { return mTotal.all; }
    S32    count(Level level) const
    {
        switch (level)
        {
            case Level::Error:   return mTotal.errors;
            case Level::Warning: return mTotal.warnings;
            case Level::Note:    return mTotal.notes;
        }
        return 0;
    }
    S32 countIn(std::string_view file) const
    {
        const auto counted = mCountByFile.find(file);
        return counted == mCountByFile.end() ? 0 : counted->second.all;
    }
    // Of one file: how many of each level.
    void countIn(std::string_view file, S32& errors, S32& warnings, S32& notes) const
    {
        const auto counted = mCountByFile.find(file);
        errors   = counted == mCountByFile.end() ? 0 : counted->second.errors;
        warnings = counted == mCountByFile.end() ? 0 : counted->second.warnings;
        notes    = counted == mCountByFile.end() ? 0 : counted->second.notes;
    }
    S32 countFixable() const { return mTotal.fixable; }

    // How many of each rule, by name, most first and then by name, which is
    // the order a filter offering them wants to be in.
    std::vector<std::pair<std::string, S32>> byRule() const
    {
        boost::unordered_flat_map<std::string, S32, ll::string_hash, std::equal_to<>> tally;
        for (const auto& [file, found] : mByFile)
        {
            for (const Finding& finding : found)
            {
                ++tally[Traits::rule(finding)];
            }
        }
        std::vector<std::pair<std::string, S32>> rules(tally.begin(), tally.end());
        // Most first, since the biggest heap is what somebody filtering by
        // rule is usually going after; ties by name, so the list does not
        // shuffle itself between two runs that found the same things.
        std::sort(rules.begin(), rules.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        return rules;
    }

private:
    // What a file's findings add up to, kept beside them so that a count
    // over a tree is arithmetic rather than a walk.
    struct Counts
    {
        S32 all      = 0;
        S32 errors   = 0;
        S32 warnings = 0;
        S32 notes    = 0;
        S32 fixable  = 0;

        void take(const Finding& finding, S32 sign)
        {
            all += sign;
            switch (Traits::level(finding))
            {
                case Level::Error:   errors += sign;   break;
                case Level::Warning: warnings += sign; break;
                case Level::Note:    notes += sign;    break;
            }
            if (Traits::fixable(finding))
            {
                fixable += sign;
            }
        }
    };

    boost::unordered_flat_map<std::string, std::vector<Finding>, ll::string_hash, std::equal_to<>> mByFile;
    boost::unordered_flat_map<std::string, Counts, ll::string_hash, std::equal_to<>>               mCountByFile;
    std::vector<std::string>                                                                        mFiles;
    Counts                                                                                          mTotal;
};
