/**
 * @file aldiffmerge_test.cpp
 * @brief Tests for ALDiffMerge and ALDiffEdit: a merge begun, its conflicts found and settled; lines put in place of others.
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

#include "aldiffmerge.h"

#include "aldiffedit.h"

#include "../test/lltut.h"

#include <memory>
#include <string>
#include <vector>

namespace tut
{
    struct aldiffmerge_data
    {
        typedef ALDiffMerge::lines_t lines_t;

        // Lines put in place of others: the text as it will be, or "none"
        // where nothing would change.
        static std::string replaced(const std::string& text, S32 first, S32 count, const lines_t& with)
        {
            ALTextRange range;
            std::string put;
            std::string made;
            if (!ALDiffEdit::replaceLines(ALTextDiff::split(text), first, count, with, range, put, made))
            {
                return "none";
            }
            return made;
        }

        // A text's lines joined again.
        static std::string joined(const lines_t& lines)
        {
            std::string out;
            for (size_t n = 0; n < lines.size(); ++n)
            {
                out += (n ? "\n" : "") + lines[n];
            }
            return out;
        }

        // A merge of these, ours as given, and the text ours stands as.
        std::string                  base;
        std::string                  theirs;
        std::string                  ours;
        std::unique_ptr<ALDiffMerge> merge;
        void begin(const std::string& b, const std::string& o, const std::string& t, const ALTextDiff::Options& options = {})
        {
            base   = b;
            theirs = t;
            ours   = o;
            merge  = std::make_unique<ALDiffMerge>(ALTextDiff::split(b), ALTextDiff::split(t), options);
            merge->setOurs(ALTextDiff::split(o));
        }
        // Ours made anew.
        void becomes(const std::string& o)
        {
            ours = o;
            merge->setOurs(ALTextDiff::split(o));
        }
        // The conflicts a stretch of ours is in.
        std::vector<size_t> inOurs(S32 first, S32 count) const { return merge->conflictsIn(0, 0, first, count); }
        // The one conflict a line of ours is in settled: ours as it is
        // made, the base as it is left.
        void settle(S32 line, ALTextMerge::Take take)
        {
            const std::optional<ALDiffMerge::Settling> settling = merge->settle(inOurs(line, 1), take);
            ensure("settled", settling.has_value());
            merge->settled(settling->base);
            if (settling->edits)
            {
                becomes(settling->made);
            }
        }
    };

    typedef test_group<aldiffmerge_data> aldiffmerge_group;
    typedef aldiffmerge_group::object    aldiffmerge_object;
    tut::aldiffmerge_group               aldiffmerge_test("aldiffmerge");

    template<> template<>
    void aldiffmerge_object::test<1>()
    {
        set_test_name("lines put in place of others: changed, taken out, put in, at the start, the middle and the end");
        ensure_equals("changed", replaced("a\nb\nc", 1, 1, { "B", "B2" }), std::string("a\nB\nB2\nc"));
        ensure_equals("taken out, with the break after", replaced("a\nb\nc", 1, 1, {}), std::string("a\nc"));
        ensure_equals("taken out at the end, with the break before", replaced("a\nb\nc", 1, 2, {}), std::string("a"));
        ensure_equals("all taken out", replaced("a\nb", 0, 2, {}), std::string());
        ensure_equals("put in before a line", replaced("a\nc", 1, 0, { "b" }), std::string("a\nb\nc"));
        ensure_equals("put in at the start", replaced("b\nc", 0, 0, { "a" }), std::string("a\nb\nc"));
        ensure_equals("put in after the last", replaced("a\nb", 2, 0, { "c", "d" }), std::string("a\nb\nc\nd"));
        ensure_equals("nothing out, nothing in", replaced("a\nb", 1, 0, {}), std::string("none"));
    }

    template<> template<>
    void aldiffmerge_object::test<2>()
    {
        set_test_name("a merge begun: what only theirs changed put in, what only ours changed kept, and ours where both changed");
        const std::string b = "alpha\nbeta\ngamma\ndelta\nepsilon\nzeta\neta";
        const std::string o = "alpha\nbeta\ngamma mine\ndelta\nepsilon\nzeta\neta mine";
        const std::string t = "alpha theirs\nbeta\ngamma\ndelta\nepsilon\nzeta\neta theirs";
        ensure_equals("merged", ALDiffMerge::start(b, o, t), std::string("alpha theirs\nbeta\ngamma mine\ndelta\nepsilon\nzeta\neta mine"));
        ensure_equals("nothing of theirs, ours", ALDiffMerge::start(b, o, b), o);
        ensure_equals("nothing of ours, theirs", ALDiffMerge::start(b, b, t), t);
    }

    template<> template<>
    void aldiffmerge_object::test<3>()
    {
        set_test_name("conflicts found where both changed otherwise, by a line of theirs or of ours in one; found again as ours changes");
        const std::string b = "alpha\nbeta\ngamma\ndelta\nepsilon\nzeta\neta";
        const std::string t = "alpha theirs\nbeta\ngamma\ndelta\nepsilon\nzeta\neta theirs";
        begin(b, ALDiffMerge::start(b, "alpha\nbeta\ngamma mine\ndelta\nepsilon\nzeta\neta mine", t), t);
        ensure_equals("one", merge->conflictCount(), 1);
        ensure_equals("by ours's line", inOurs(6, 1).size(), 1U);
        ensure_equals("by theirs's line", merge->conflictsIn(6, 1, 0, 0).size(), 1U);
        ensure("not by a line of ours's own change", inOurs(2, 1).empty());
        ensure("nor by theirs taken", inOurs(0, 1).empty());
        ensure("nor by a stretch of nothing", merge->conflictsIn(6, 0, 6, 0).empty());

        // Ours made theirs there by hand: no conflict.
        becomes("alpha theirs\nbeta\ngamma mine\ndelta\nepsilon\nzeta\neta theirs");
        ensure_equals("settled by hand", merge->conflictCount(), 0);
        // And something else changed by both: another.
        becomes("alpha theirs\nbeta\ngamma mine\ndelta\nepsilon\nzeta\neta mine again");
        ensure_equals("again", merge->conflictCount(), 1);
    }

    template<> template<>
    void aldiffmerge_object::test<4>()
    {
        set_test_name("a conflict settled as theirs, as ours, or ours then theirs: ours edited, and no conflict left however ours is edited after");
        const std::string b = "one\ntwo\nthree";
        const std::string o = "one\ntwo mine\nthree";
        const std::string t = "one\ntwo theirs\nthree";

        begin(b, o, t);
        ensure_equals("a conflict", merge->conflictCount(), 1);
        settle(1, ALTextMerge::Take::Theirs);
        ensure_equals("theirs", ours, t);
        ensure_equals("settled", merge->conflictCount(), 0);

        begin(b, o, t);
        const std::optional<ALDiffMerge::Settling> kept = merge->settle(inOurs(1, 1), ALTextMerge::Take::Ours);
        ensure("ours: no edit", kept && !kept->edits);
        ensure_equals("the base taken as theirs there", joined(kept->base), t);
        merge->settled(kept->base);
        ensure_equals("settled", merge->conflictCount(), 0);
        becomes("one\ntwo mine, and more\nthree");
        ensure_equals("ours's own change, edited", merge->conflictCount(), 0);

        begin(b, o, t);
        settle(1, ALTextMerge::Take::OursThenTheirs);
        ensure_equals("both", ours, std::string("one\ntwo mine\ntwo theirs\nthree"));
        ensure_equals("settled", merge->conflictCount(), 0);
    }

    template<> template<>
    void aldiffmerge_object::test<5>()
    {
        set_test_name("conflicts settled together keep the lines between them; one that is none is not settled");
        const std::string b = "a\nb\nc\nd\ne";
        const std::string o = "a mine\nb\nc\nd\ne mine";
        const std::string t = "a theirs\nb\nc\nd\ne theirs";
        begin(b, o, t);
        ensure_equals("two", merge->conflictCount(), 2);
        const std::vector<size_t> both = inOurs(0, 5);
        ensure_equals("both by a stretch over them", both.size(), 2U);
        const std::optional<ALDiffMerge::Settling> settling = merge->settle(both, ALTextMerge::Take::Theirs);
        ensure("an edit", settling && settling->edits);
        ensure_equals("each theirs, the lines between kept", settling->made, t);
        merge->settled(settling->base);
        becomes(settling->made);
        ensure_equals("none left", merge->conflictCount(), 0);
        ensure("nothing to settle", !merge->settle({ 0 }, ALTextMerge::Take::Theirs));
        ensure("nor past the hunks", !merge->settle({ 99 }, ALTextMerge::Take::Theirs));
    }

    template<> template<>
    void aldiffmerge_object::test<6>()
    {
        set_test_name("lines told the same as the comparison tells them: blanks let go of, a change of blanks alone is both's, not a conflict");
        const std::string b = "x = 1;\ny = 2;";
        const std::string o = "x = 1;\ny  =  3;";
        const std::string t = "x = 1;\ny = 3;";
        begin(b, o, t);
        ensure_equals("as they are, a conflict", merge->conflictCount(), 1);
        ALTextDiff::Options loose;
        loose.like.ignoreWhitespace = true;
        merge->setOptions(loose);
        ensure_equals("blanks let go of, none", merge->conflictCount(), 0);
        // By structure is by lines, here.
        loose.algorithm = ALTextDiff::Algorithm::Structural;
        merge->setOptions(loose);
        ensure_equals("still none", merge->conflictCount(), 0);
    }

    template<> template<>
    void aldiffmerge_object::test<7>()
    {
        set_test_name("the edit that makes one text another, between their edges, made with LF whatever the text's line ends; theirs's changes found again with the options");
        const lines_t was = ALTextDiff::split("a\nb\nc");
        ALTextRange   range;
        std::string   put;
        std::string   made;
        ensure("lines put in between", ALDiffEdit::becoming(was, ALTextDiff::split("a\nx\ny\nc"), range, put, made));
        ensure("in place of the line between", range == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 1)) && put == "x\ny" && made == "a\nx\ny\nc");
        ensure("the same: nothing", !ALDiffEdit::becoming(was, ALTextDiff::split("a\nb\nc"), range, put, made));
        const ALDiffEdit::Edges self = ALDiffEdit::edgesOf(was, was);
        const ALDiffEdit::Edges copy = ALDiffEdit::edgesOf(was, lines_t(was));
        ensure("a text with itself as with its copy", self.head == 3 && self.tail == 0 && copy.head == 3 && copy.tail == 0);
        const ALDiffEdit::Edges ends = ALDiffEdit::edgesOf(ALTextDiff::split("a\nb\nc\nd"), ALTextDiff::split("a\nx\nd"));
        ensure("the lines shared at either end", ends.head == 1 && ends.tail == 1);
        ensure_equals("lines a CR ends: LF made", replaced("a\r\nb\rc", 1, 1, { "x" }), std::string("a\nx\nc"));

        // Theirs changed only blanks, ours the line: a conflict as they are,
        // none with blanks let go of -- theirs's change found again.
        begin("y = 2;", "y = 5;", "y  =  2;");
        ensure_equals("as they are, a conflict", merge->conflictCount(), 1);
        ALTextDiff::Options loose;
        loose.like.ignoreWhitespace = true;
        merge->setOptions(loose);
        ensure_equals("theirs no change: none", merge->conflictCount(), 0);
    }
}
