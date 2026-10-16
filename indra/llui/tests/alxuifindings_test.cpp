/**
 * @file alxuifindings_test.cpp
 * @brief A store of findings answers questions a report cannot be asked.
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

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alxuifindings_data
    {
        static ALXUILint::Finding one(ALXUILint::Rule rule, ALXUILint::Severity severity,
                                      const std::string& what, const std::string& message,
                                      ALXUILint::Fix::Do fix = ALXUILint::Fix::Do::Nothing)
        {
            ALXUILint::Finding finding;
            finding.rule = rule;
            finding.severity = severity;
            finding.path = { what };
            finding.what = what;
            finding.message = message;
            finding.fix.did = fix;
            return finding;
        }

        // Two files with something to say about each of them.
        static void fill(ALXUIFindings& store)
        {
            store.replace("one.xml", {
                one(ALXUILint::Rule::UnknownAttribute, ALXUILint::Severity::Warning,
                    "dynamicwidth", "no parameter of that name",
                    ALXUILint::Fix::Do::SpellAttribute),
                one(ALXUILint::Rule::FileMissing, ALXUILint::Severity::Error,
                    "filename", "no XUI file is named that"),
                one(ALXUILint::Rule::WroteTheDefault, ALXUILint::Severity::Note,
                    "height", "written as what it already is",
                    ALXUILint::Fix::Do::TakeAttributeOut) });
            store.replace("two.xml", {
                one(ALXUILint::Rule::UnknownAttribute, ALXUILint::Severity::Warning,
                    "labl", "no parameter of that name",
                    ALXUILint::Fix::Do::SpellAttribute) });
        }
    };

    // The questions a report cannot be asked: how many, of what, and where.
    TEST_CASE_METHOD(alxuifindings_data, "alxuifindings: the store counts findings by severity and by file and by fix", "[alxuifindings]")
    {
        ALXUIFindings store;
        UNSCOPED_INFO("nothing yet");
        CHECK(store.size() == 0u);
        fill(store);

        CHECK(store.size() == 4u);
        CHECK(store.count(ALXUILint::Severity::Error) == 1);
        CHECK(store.count(ALXUILint::Severity::Warning) == 2);
        CHECK(store.count(ALXUILint::Severity::Note) == 1);
        CHECK(store.countIn("one.xml") == 3);
        CHECK(store.countIn("two.xml") == 1);
        UNSCOPED_INFO("the ones that offer a fix");
        CHECK(store.countFixable() == 3);
        CHECK(store.files().size() == 2u);

        // A file with nothing to say is still a file that was looked at, and
        // that is not the same answer as one nobody has checked.
        UNSCOPED_INFO("one nobody checked");
        CHECK_FALSE(store.checked("three.xml"));
        store.replace("three.xml", std::vector<ALXUILint::Finding>());
        UNSCOPED_INFO("one that was");
        CHECK(store.checked("three.xml"));
        UNSCOPED_INFO("with nothing in it");
        CHECK(store.countIn("three.xml") == 0);
        UNSCOPED_INFO("and nothing added to the whole");
        CHECK(store.size() == 4u);
    }

    // Checking a file again replaces what it said before and nothing else,
    // which is what lets the file in front of somebody be re-checked on every
    // rebuild while a pass over the rest of the tree fills in around it.
    TEST_CASE_METHOD(alxuifindings_data, "alxuifindings: checking a file again replaces only what it said before", "[alxuifindings]")
    {
        ALXUIFindings store;
        fill(store);

        store.replace("one.xml", {
            one(ALXUILint::Rule::Overlap, ALXUILint::Severity::Warning, "a", "overlaps b") });

        UNSCOPED_INFO("its own are gone");
        CHECK(store.countIn("one.xml") == 1);
        UNSCOPED_INFO("the other file is untouched");
        CHECK(store.countIn("two.xml") == 1);
        UNSCOPED_INFO("and the whole is the two of them");
        CHECK(store.size() == 2u);
        UNSCOPED_INFO("the error went with the file that said it");
        CHECK(store.count(ALXUILint::Severity::Error) == 0);
        UNSCOPED_INFO("as did the fix it offered");
        CHECK(store.countFixable() == 1);
        UNSCOPED_INFO("and it is still one of two files");
        CHECK(store.files().size() == 2u);

        store.forget("one.xml");
        CHECK_FALSE(store.checked("one.xml"));
        CHECK(store.files().size() == 1u);
        CHECK(store.size() == 1u);

        store.clear();
        CHECK(store.size() == 0u);
        CHECK(store.files().size() == 0u);
        UNSCOPED_INFO("and no count of anything");
        CHECK(store.count(ALXUILint::Severity::Warning) == 0);
    }

    // Every field of a query widens rather than narrows, so one nobody
    // filled in selects the lot -- and each field named takes a slice.
    TEST_CASE_METHOD(alxuifindings_data, "alxuifindings: an empty query selects everything and each field named takes a slice", "[alxuifindings]")
    {
        ALXUIFindings store;
        fill(store);

        ALXUIFindings::Query all;
        UNSCOPED_INFO("a query nobody filled in");
        CHECK(store.select(all).found.size() == 4u);

        ALXUIFindings::Query here;
        here.file = "one.xml";
        CHECK(store.select(here).found.size() == 3u);

        ALXUIFindings::Query loud;
        loud.notes = false;
        UNSCOPED_INFO("without the quiet ones");
        CHECK(store.select(loud).found.size() == 3u);

        ALXUIFindings::Query broken;
        broken.warnings = false;
        broken.notes = false;
        UNSCOPED_INFO("only what does not work");
        REQUIRE(store.select(broken).found.size() == 1u);
        UNSCOPED_INFO("which is the one that names a file");
        CHECK(store.select(broken).found.front()->what == std::string("filename"));

        ALXUIFindings::Query named;
        named.rule = ALXUILint::ruleName(ALXUILint::Rule::UnknownAttribute);
        CHECK(store.select(named).found.size() == 2u);

        ALXUIFindings::Query mendable;
        mendable.fixable = true;
        UNSCOPED_INFO("only the ones that offer something");
        CHECK(store.select(mendable).found.size() == 3u);

        // The filter reads the message, the name at fault, the element and
        // the file, because any of the four is what somebody has in mind.
        ALXUIFindings::Query word;
        word.text = "DYNAMIC";
        UNSCOPED_INFO("a word, whatever case it is typed in");
        CHECK(store.select(word).found.size() == 1u);
        word.text = "two";
        UNSCOPED_INFO("or the file it is in");
        CHECK(store.select(word).found.size() == 1u);

        // Narrowed to nothing is an answer too.
        ALXUIFindings::Query none;
        none.file = "two.xml";
        none.rule = ALXUILint::ruleName(ALXUILint::Rule::FileMissing);
        UNSCOPED_INFO("and nothing is what it says");
        CHECK(store.select(none).found.size() == 0u);
    }

    // A tree's worth of findings is more rows than any list can hold, so a
    // query says how many it will take -- and answers how many there were,
    // since a list showing the first of something has to say so.
    TEST_CASE_METHOD(alxuifindings_data, "alxuifindings: a query's limit caps the rows it returns but not the total", "[alxuifindings]")
    {
        ALXUIFindings store;
        fill(store);

        ALXUIFindings::Query capped;
        capped.limit = 2;
        const ALXUIFindings::Selected selected = store.select(capped);
        UNSCOPED_INFO("as many as were asked for");
        CHECK(selected.found.size() == 2u);
        UNSCOPED_INFO("out of how many there are");
        CHECK(selected.total == 4u);
    }

    // Which rules the findings in hand are of, and how many of each: what a
    // filter offering them has to be built from, most first so that the
    // biggest heap is the first thing offered.
    TEST_CASE_METHOD(alxuifindings_data, "alxuifindings: byRule counts the findings of each rule with the most first", "[alxuifindings]")
    {
        ALXUIFindings store;
        fill(store);

        const std::vector<std::pair<std::string, S32> > rules = store.byRule();
        UNSCOPED_INFO("three rules said something");
        REQUIRE(rules.size() == 3u);
        UNSCOPED_INFO("the most said first");
        CHECK(rules.front().first ==
                      std::string(ALXUILint::ruleName(ALXUILint::Rule::UnknownAttribute)));
        UNSCOPED_INFO("twice");
        CHECK(rules.front().second == 2);

        S32 total = 0;
        for (const auto& [rule, count] : rules)
        {
            total += count;
        }
        UNSCOPED_INFO("and they add up to what is held");
        CHECK(total == 4);
    }
    // Checked again, a file keeps its place among the files, and its
    // findings theirs among everyone's: the file in front is re-checked on
    // every rebuild, and its rows do not jump to the end of the list.
    TEST_CASE_METHOD(alxuifindings_data, "alxuifindings: a file checked again keeps its place and so do its findings", "[alxuifindings]")
    {
        ALXUIFindings store;
        fill(store);
        store.replace("three.xml", {
            one(ALXUILint::Rule::Overlap, ALXUILint::Severity::Warning, "c", "overlaps d") });

        store.replace("one.xml", {
            one(ALXUILint::Rule::Overlap, ALXUILint::Severity::Warning, "a", "overlaps b") });
        UNSCOPED_INFO("still first of the files");
        CHECK(store.files() == std::vector<std::string>({ "one.xml", "two.xml", "three.xml" }));
        const auto selected = store.select(ALXUIFindings::Query());
        REQUIRE(selected.found.size() == size_t(3));
        UNSCOPED_INFO("its own first");
        CHECK(selected.found.front()->what == std::string("a"));

        // Let go of and checked again, it is a file checked anew, at the end.
        store.forget("one.xml");
        store.replace("one.xml", std::vector<ALXUILint::Finding>());
        UNSCOPED_INFO("at the end");
        CHECK(store.files() == std::vector<std::string>({ "two.xml", "three.xml", "one.xml" }));
        UNSCOPED_INFO("counted once");
        CHECK(store.files().size() == size_t(3));
    }
}
