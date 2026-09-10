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

#include "../alxuifindings.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gFindingsTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gFindingsTestAnonName;
}

namespace tut
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

    typedef test_group<alxuifindings_data> alxuifindings_test;
    typedef alxuifindings_test::object     alxuifindings_object;
    tut::alxuifindings_test alxuifindings_testgroup("alxuifindings");

    // The questions a report cannot be asked: how many, of what, and where.
    template<> template<>
    void alxuifindings_object::test<1>()
    {
        ALXUIFindings store;
        ensure_equals("nothing yet", store.size(), 0u);
        fill(store);

        ensure_equals("everything", store.size(), 4u);
        ensure_equals("errors", store.count(ALXUILint::Severity::Error), 1);
        ensure_equals("warnings", store.count(ALXUILint::Severity::Warning), 2);
        ensure_equals("notes", store.count(ALXUILint::Severity::Note), 1);
        ensure_equals("in one of them", store.countIn("one.xml"), 3);
        ensure_equals("and in the other", store.countIn("two.xml"), 1);
        ensure_equals("the ones that offer a fix", store.countFixable(), 3);
        ensure_equals("two files", store.files().size(), 2u);

        // A file with nothing to say is still a file that was looked at, and
        // that is not the same answer as one nobody has checked.
        ensure("one nobody checked", !store.checked("three.xml"));
        store.replace("three.xml", {});
        ensure("one that was", store.checked("three.xml"));
        ensure_equals("with nothing in it", store.countIn("three.xml"), 0);
        ensure_equals("and nothing added to the whole", store.size(), 4u);
    }

    // Checking a file again replaces what it said before and nothing else,
    // which is what lets the file in front of somebody be re-checked on every
    // rebuild while a pass over the rest of the tree fills in around it.
    template<> template<>
    void alxuifindings_object::test<2>()
    {
        ALXUIFindings store;
        fill(store);

        store.replace("one.xml", {
            one(ALXUILint::Rule::Overlap, ALXUILint::Severity::Warning, "a", "overlaps b") });

        ensure_equals("its own are gone", store.countIn("one.xml"), 1);
        ensure_equals("the other file is untouched", store.countIn("two.xml"), 1);
        ensure_equals("and the whole is the two of them", store.size(), 2u);
        ensure_equals("the error went with the file that said it",
                      store.count(ALXUILint::Severity::Error), 0);
        ensure_equals("as did the fix it offered", store.countFixable(), 1);
        ensure_equals("and it is still one of two files", store.files().size(), 2u);

        store.forget("one.xml");
        ensure("gone", !store.checked("one.xml"));
        ensure_equals("one file left", store.files().size(), 1u);
        ensure_equals("and one finding", store.size(), 1u);

        store.clear();
        ensure_equals("nothing at all", store.size(), 0u);
        ensure_equals("no files", store.files().size(), 0u);
        ensure_equals("and no count of anything", store.count(ALXUILint::Severity::Warning), 0);
    }

    // Every field of a query widens rather than narrows, so one nobody
    // filled in selects the lot -- and each field named takes a slice.
    template<> template<>
    void alxuifindings_object::test<3>()
    {
        ALXUIFindings store;
        fill(store);

        ALXUIFindings::Query all;
        ensure_equals("a query nobody filled in", store.select(all).found.size(), 4u);

        ALXUIFindings::Query here;
        here.file = "one.xml";
        ensure_equals("one file", store.select(here).found.size(), 3u);

        ALXUIFindings::Query loud;
        loud.notes = false;
        ensure_equals("without the quiet ones", store.select(loud).found.size(), 3u);

        ALXUIFindings::Query broken;
        broken.warnings = false;
        broken.notes = false;
        ensure_equals("only what does not work", store.select(broken).found.size(), 1u);
        ensure_equals("which is the one that names a file",
                      store.select(broken).found.front()->what, std::string("filename"));

        ALXUIFindings::Query named;
        named.rule = ALXUILint::ruleName(ALXUILint::Rule::UnknownAttribute);
        ensure_equals("by rule", store.select(named).found.size(), 2u);

        ALXUIFindings::Query mendable;
        mendable.fixable = true;
        ensure_equals("only the ones that offer something", store.select(mendable).found.size(), 3u);

        // The filter reads the message, the name at fault, the element and
        // the file, because any of the four is what somebody has in mind.
        ALXUIFindings::Query word;
        word.text = "DYNAMIC";
        ensure_equals("a word, whatever case it is typed in",
                      store.select(word).found.size(), 1u);
        word.text = "two";
        ensure_equals("or the file it is in", store.select(word).found.size(), 1u);

        // Narrowed to nothing is an answer too.
        ALXUIFindings::Query none;
        none.file = "two.xml";
        none.rule = ALXUILint::ruleName(ALXUILint::Rule::FileMissing);
        ensure_equals("and nothing is what it says", store.select(none).found.size(), 0u);
    }

    // A tree's worth of findings is more rows than any list can hold, so a
    // query says how many it will take -- and answers how many there were,
    // since a list showing the first of something has to say so.
    template<> template<>
    void alxuifindings_object::test<4>()
    {
        ALXUIFindings store;
        fill(store);

        ALXUIFindings::Query capped;
        capped.limit = 2;
        const ALXUIFindings::Selected selected = store.select(capped);
        ensure_equals("as many as were asked for", selected.found.size(), 2u);
        ensure_equals("out of how many there are", selected.total, 4u);
    }

    // Which rules the findings in hand are of, and how many of each: what a
    // filter offering them has to be built from, most first so that the
    // biggest heap is the first thing offered.
    template<> template<>
    void alxuifindings_object::test<5>()
    {
        ALXUIFindings store;
        fill(store);

        const std::vector<std::pair<std::string, S32> > rules = store.byRule();
        ensure_equals("three rules said something", rules.size(), 3u);
        ensure_equals("the most said first", rules.front().first,
                      std::string(ALXUILint::ruleName(ALXUILint::Rule::UnknownAttribute)));
        ensure_equals("twice", rules.front().second, 2);

        S32 total = 0;
        for (const auto& [rule, count] : rules)
        {
            total += count;
        }
        ensure_equals("and they add up to what is held", total, 4);
    }
}
