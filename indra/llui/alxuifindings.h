/**
 * @file alxuifindings.h
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

#pragma once

#include "alxuilint.h"

#include "llstl.h"

#include <string>
#include <string_view>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

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
class ALXUIFindings
{
public:
    // Everything one file said, in place of whatever it said before. A file
    // that said nothing is still a file that was checked, and is kept as one
    // -- "no findings" and "not looked at" are different answers.
    void replace(const std::string& file, std::vector<ALXUILint::Finding> found);
    void forget(std::string_view file);
    void clear();

    // Which of them to look at. Every field left alone widens rather than
    // narrows, so a Query nobody filled in selects the lot.
    struct Query
    {
        std::string file;           // that one file; empty is all of them
        std::string rule;           // by the name ALXUILint::ruleName gives
        bool        errors = true;
        bool        warnings = true;
        bool        notes = true;
        bool        fixable = false;    // only the ones that offer a fix
        // A word in the message, in the attribute or tag at fault, in the
        // element path or in the file name. Matched without regard to case,
        // since nobody typing a filter is thinking about it.
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
        std::vector<const ALXUILint::Finding*>  found;
        size_t                                  total = 0;  // before the limit
    };
    Selected select(const Query& query) const;

    // The files that have been checked, in the order they first were.
    const std::vector<std::string>& files() const { return mFiles; }
    bool checked(std::string_view file) const { return mByFile.find(file) != mByFile.end(); }

    size_t size() const { return mTotal.all; }
    S32 count(ALXUILint::Severity severity) const;
    S32 countIn(std::string_view file) const;
    S32 countFixable() const { return mTotal.fixable; }

    // How many of each rule, by name, most first and then by name, which is
    // the order a filter offering them wants to be in.
    std::vector<std::pair<std::string, S32> > byRule() const;

private:
    // What a file's findings add up to, kept beside them so that a count
    // over a tree is arithmetic rather than a walk.
    struct Counts
    {
        S32     all = 0;
        S32     errors = 0;
        S32     warnings = 0;
        S32     notes = 0;
        S32     fixable = 0;

        void take(const ALXUILint::Finding& finding, S32 sign);
    };

    boost::unordered_flat_map<std::string, std::vector<ALXUILint::Finding>,
                              ll::string_hash, std::equal_to<> >    mByFile;
    boost::unordered_flat_map<std::string, Counts,
                              ll::string_hash, std::equal_to<> >    mCountByFile;
    std::vector<std::string>                                        mFiles;
    Counts                                                          mTotal;
};
