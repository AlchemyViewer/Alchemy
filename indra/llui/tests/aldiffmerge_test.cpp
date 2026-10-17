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
#include "aldifflexer.h"
#include "aldiffsplice.h"
#include "alsyntaxgrammar.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace
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
        void require_settled(S32 line, ALTextMerge::Take take)
        {
            const std::optional<ALDiffMerge::Settling> settling = merge->settle(inOurs(line, 1), take);
            INFO("the conflict at line " << line << " of ours settles");
            REQUIRE(settling.has_value());
            merge->settled(*settling);
            if (settling->edits)
            {
                becomes(settling->made);
            }
        }
    };

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: replaceLines puts lines in place of others at either end or between", "[aldiffmerge]")
    {
        UNSCOPED_INFO("changed");
        CHECK(replaced("a\nb\nc", 1, 1, { "B", "B2" }) == std::string("a\nB\nB2\nc"));
        UNSCOPED_INFO("taken out, with the break after");
        CHECK(replaced("a\nb\nc", 1, 1, {}) == std::string("a\nc"));
        UNSCOPED_INFO("taken out at the end, with the break before");
        CHECK(replaced("a\nb\nc", 1, 2, {}) == std::string("a"));
        UNSCOPED_INFO("all taken out");
        CHECK(replaced("a\nb", 0, 2, {}) == std::string());
        UNSCOPED_INFO("put in before a line");
        CHECK(replaced("a\nc", 1, 0, { "b" }) == std::string("a\nb\nc"));
        UNSCOPED_INFO("put in at the start");
        CHECK(replaced("b\nc", 0, 0, { "a" }) == std::string("a\nb\nc"));
        UNSCOPED_INFO("put in after the last");
        CHECK(replaced("a\nb", 2, 0, { "c", "d" }) == std::string("a\nb\nc\nd"));
        UNSCOPED_INFO("nothing out, nothing in");
        CHECK(replaced("a\nb", 1, 0, {}) == std::string("none"));
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: start takes each side's own changes and ours where both changed", "[aldiffmerge]")
    {
        const std::string b = "alpha\nbeta\ngamma\ndelta\nepsilon\nzeta\neta";
        const std::string o = "alpha\nbeta\ngamma mine\ndelta\nepsilon\nzeta\neta mine";
        const std::string t = "alpha theirs\nbeta\ngamma\ndelta\nepsilon\nzeta\neta theirs";
        CHECK(ALDiffMerge::start(b, o, t) == std::string("alpha theirs\nbeta\ngamma mine\ndelta\nepsilon\nzeta\neta mine"));
        UNSCOPED_INFO("nothing of theirs, ours");
        CHECK(ALDiffMerge::start(b, o, b) == o);
        UNSCOPED_INFO("nothing of ours, theirs");
        CHECK(ALDiffMerge::start(b, b, t) == t);
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: conflicts are found by either side's line and again as ours changes", "[aldiffmerge]")
    {
        const std::string b = "alpha\nbeta\ngamma\ndelta\nepsilon\nzeta\neta";
        const std::string t = "alpha theirs\nbeta\ngamma\ndelta\nepsilon\nzeta\neta theirs";
        begin(b, ALDiffMerge::start(b, "alpha\nbeta\ngamma mine\ndelta\nepsilon\nzeta\neta mine", t), t);
        CHECK(merge->conflictCount() == 1);
        UNSCOPED_INFO("by ours's line");
        CHECK(inOurs(6, 1).size() == 1U);
        UNSCOPED_INFO("by theirs's line");
        CHECK(merge->conflictsIn(6, 1, 0, 0).size() == 1U);
        UNSCOPED_INFO("not by a line of ours's own change");
        CHECK(inOurs(2, 1).empty());
        UNSCOPED_INFO("nor by theirs taken");
        CHECK(inOurs(0, 1).empty());
        UNSCOPED_INFO("nor by a stretch of nothing");
        CHECK(merge->conflictsIn(6, 0, 6, 0).empty());

        // Ours made theirs there by hand: no conflict.
        becomes("alpha theirs\nbeta\ngamma mine\ndelta\nepsilon\nzeta\neta theirs");
        UNSCOPED_INFO("settled by hand");
        CHECK(merge->conflictCount() == 0);
        // And something else changed by both: another.
        becomes("alpha theirs\nbeta\ngamma mine\ndelta\nepsilon\nzeta\neta mine again");
        UNSCOPED_INFO("again");
        CHECK(merge->conflictCount() == 1);
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: a conflict settled any way leaves none however ours is edited after", "[aldiffmerge]")
    {
        const std::string b = "one\ntwo\nthree";
        const std::string o = "one\ntwo mine\nthree";
        const std::string t = "one\ntwo theirs\nthree";

        begin(b, o, t);
        CHECK(merge->conflictCount() == 1);
        require_settled(1, ALTextMerge::Take::Theirs);
        UNSCOPED_INFO("theirs");
        CHECK(ours == t);
        CHECK(merge->conflictCount() == 0);

        begin(b, o, t);
        const std::optional<ALDiffMerge::Settling> kept = merge->settle(inOurs(1, 1), ALTextMerge::Take::Ours);
        {
            INFO("ours: no edit");
            REQUIRE(kept);
            CHECK_FALSE(kept->edits);
        }
        {
            INFO("kept as it was and as it will be, ours both");
            REQUIRE(kept->settled.size() == 1);
            CHECK(joined(kept->settled[0].before) == "two mine");
            CHECK(joined(kept->settled[0].after) == "two mine");
        }
        merge->settled(*kept);
        CHECK(merge->conflictCount() == 0);
        becomes("one\ntwo mine, and more\nthree");
        UNSCOPED_INFO("ours's own change, edited");
        CHECK(merge->conflictCount() == 0);

        begin(b, o, t);
        require_settled(1, ALTextMerge::Take::OursThenTheirs);
        UNSCOPED_INFO("both");
        CHECK(ours == std::string("one\ntwo mine\ntwo theirs\nthree"));
        CHECK(merge->conflictCount() == 0);
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: two settled together keep the lines between and none settles nothing", "[aldiffmerge]")
    {
        const std::string b = "a\nb\nc\nd\ne";
        const std::string o = "a mine\nb\nc\nd\ne mine";
        const std::string t = "a theirs\nb\nc\nd\ne theirs";
        begin(b, o, t);
        CHECK(merge->conflictCount() == 2);
        const std::vector<size_t> both = inOurs(0, 5);
        UNSCOPED_INFO("both by a stretch over them");
        REQUIRE(both.size() == 2U);
        const std::optional<ALDiffMerge::Settling> settling = merge->settle(both, ALTextMerge::Take::Theirs);
        {
            INFO("an edit");
            REQUIRE(settling);
            CHECK(settling->edits);
        }
        UNSCOPED_INFO("each theirs, the lines between kept");
        CHECK(settling->made == t);
        merge->settled(*settling);
        becomes(settling->made);
        CHECK(merge->conflictCount() == 0);
        UNSCOPED_INFO("nothing to settle");
        CHECK_FALSE(merge->settle({ 0 }, ALTextMerge::Take::Theirs));
        UNSCOPED_INFO("nor past the hunks");
        CHECK_FALSE(merge->settle({ 99 }, ALTextMerge::Take::Theirs));
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: with blanks let go of a change of blanks alone is no conflict", "[aldiffmerge]")
    {
        const std::string b = "x = 1;\ny = 2;";
        const std::string o = "x = 1;\ny  =  3;";
        const std::string t = "x = 1;\ny = 3;";
        begin(b, o, t);
        UNSCOPED_INFO("as they are, a conflict");
        CHECK(merge->conflictCount() == 1);
        ALTextDiff::Options loose;
        loose.like.ignoreWhitespace = true;
        merge->setOptions(loose);
        UNSCOPED_INFO("blanks let go of, none");
        CHECK(merge->conflictCount() == 0);
        // By structure is by lines, here.
        loose.algorithm = ALTextDiff::Algorithm::Structural;
        merge->setOptions(loose);
        UNSCOPED_INFO("still none");
        CHECK(merge->conflictCount() == 0);
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: becoming edits between the edges in LF and options find theirs again", "[aldiffmerge]")
    {
        const lines_t was = ALTextDiff::split("a\nb\nc");
        ALTextRange   range;
        std::string   put;
        std::string   made;
        UNSCOPED_INFO("lines put in between");
        REQUIRE(ALDiffEdit::becoming(was, ALTextDiff::split("a\nx\ny\nc"), range, put, made));
        {
            INFO("in place of the line between");
            CHECK(range == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 1)));
            CHECK(put == "x\ny");
            CHECK(made == "a\nx\ny\nc");
        }
        UNSCOPED_INFO("the same: nothing");
        CHECK_FALSE(ALDiffEdit::becoming(was, ALTextDiff::split("a\nb\nc"), range, put, made));
        const ALDiffEdit::Edges self = ALDiffEdit::edgesOf(was, was);
        const ALDiffEdit::Edges copy = ALDiffEdit::edgesOf(was, lines_t(was));
        {
            INFO("a text with itself as with its copy");
            CHECK(self.head == 3);
            CHECK(self.tail == 0);
            CHECK(copy.head == 3);
            CHECK(copy.tail == 0);
        }
        const ALDiffEdit::Edges ends = ALDiffEdit::edgesOf(ALTextDiff::split("a\nb\nc\nd"), ALTextDiff::split("a\nx\nd"));
        {
            INFO("the lines shared at either end");
            CHECK(ends.head == 1);
            CHECK(ends.tail == 1);
        }
        UNSCOPED_INFO("lines a CR ends: LF made");
        CHECK(replaced("a\r\nb\rc", 1, 1, { "x" }) == std::string("a\nx\nc"));

        // Theirs changed only blanks, ours the line: a conflict as they are,
        // none with blanks let go of -- theirs's change found again.
        begin("y = 2;", "y = 5;", "y  =  2;");
        UNSCOPED_INFO("as they are, a conflict");
        CHECK(merge->conflictCount() == 1);
        ALTextDiff::Options loose;
        loose.like.ignoreWhitespace = true;
        merge->setOptions(loose);
        UNSCOPED_INFO("theirs no change: none");
        CHECK(merge->conflictCount() == 0);

        // Each side reworded the comment on a line: a conflict as they are,
        // none with comments let go of -- the merge reads where they are by
        // the grammar, as a compare does.
        std::string                                  error;
        const std::shared_ptr<const ALSyntaxGrammar> lsl = ALSyntaxGrammar::fromFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/lsl.xml", error);
        UNSCOPED_INFO("the LSL grammar");
        REQUIRE(lsl != nullptr);
        begin("x = 1; // one", "x = 1; // uno", "x = 1; // eins");
        UNSCOPED_INFO("comments as they are, a conflict");
        CHECK(merge->conflictCount() == 1);
        ALTextDiff::Options commented;
        commented.like.ignoreComments = true;
        commented.lexer               = ALDiffLexer::lexerOf(std::make_shared<ALDiffLexer>(lsl));
        merge->setOptions(commented);
        UNSCOPED_INFO("comments let go of: none");
        CHECK(merge->conflictCount() == 0);
        {
            INFO("and no change by either");
            REQUIRE(merge->hunks().size() == 1);
            CHECK(merge->hunks()[0].kind == ALTextMerge::Kind::Same);
        }
        // The same change of the code by both, each with a comment of its
        // own: alike, but for the comments.
        begin("x = 1; // one", "x = 2; // uno", "x = 2; // eins", commented);
        UNSCOPED_INFO("alike but for comments: no conflict");
        CHECK(merge->conflictCount() == 0);
        {
            INFO("both took the one change");
            REQUIRE(merge->hunks().size() == 1);
            CHECK(merge->hunks()[0].kind == ALTextMerge::Kind::Both);
        }
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: a settling undone is a conflict again and redone is settled again", "[aldiffmerge]")
    {
        const std::string b = "one\ntwo\nthree";
        const std::string o = "one\ntwo mine\nthree";
        const std::string t = "one\ntwo theirs\nthree";
        for (const ALTextMerge::Take take : { ALTextMerge::Take::Theirs, ALTextMerge::Take::OursThenTheirs })
        {
            CAPTURE(take);
            begin(b, o, t);
            require_settled(1, take);
            const std::string made = ours;
            UNSCOPED_INFO("an edit");
            REQUIRE(made != o);
            REQUIRE(merge->conflictCount() == 0);
            UNSCOPED_INFO("the base as it was");
            REQUIRE(joined(merge->base()) == b);
            becomes(o);
            UNSCOPED_INFO("undone: a conflict again");
            REQUIRE(merge->conflictCount() == 1);
            UNSCOPED_INFO("found by ours's line");
            REQUIRE(inOurs(1, 1).size() == 1U);
            becomes(made);
            UNSCOPED_INFO("redone: settled again");
            REQUIRE(merge->conflictCount() == 0);
            becomes(o);
            UNSCOPED_INFO("and undone again");
            REQUIRE(merge->conflictCount() == 1);
        }

        // Two settled at once, undone together: both back.
        const std::string b2 = "a\nb\nc\nd\ne";
        const std::string o2 = "a mine\nb\nc\nd\ne mine";
        const std::string t2 = "a theirs\nb\nc\nd\ne theirs";
        begin(b2, o2, t2);
        const std::optional<ALDiffMerge::Settling> settling = merge->settle(inOurs(0, 5), ALTextMerge::Take::OursThenTheirs);
        {
            INFO("both settled together");
            REQUIRE(settling);
            REQUIRE(settling->edits);
            REQUIRE(settling->settled.size() == 2);
        }
        merge->settled(*settling);
        becomes(settling->made);
        CHECK(merge->conflictCount() == 0);
        becomes(o2);
        UNSCOPED_INFO("undone: both again");
        CHECK(merge->conflictCount() == 2);
        becomes(settling->made);
        UNSCOPED_INFO("redone: none");
        CHECK(merge->conflictCount() == 0);
        // One left as settled, the other put back by hand alone: that one a
        // conflict again.
        becomes("a mine\na theirs\nb\nc\nd\ne mine");
        UNSCOPED_INFO("one undone by hand");
        CHECK(merge->conflictCount() == 1);
        UNSCOPED_INFO("the one");
        CHECK(inOurs(5, 1).size() == 1U);
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: joining a settled conflict to an unsettled one leaves one to settle", "[aldiffmerge]")
    {
        const std::string b = "a\nb\nc";
        const std::string o = "A1\nb\nC1";
        const std::string t = "A2\nb\nC2";
        begin(b, o, t);
        CHECK(merge->conflictCount() == 2);
        require_settled(0, ALTextMerge::Take::Ours);
        UNSCOPED_INFO("the first settled");
        CHECK(merge->conflictCount() == 1);
        // The line between edited: one stretch both changed, which holds
        // theirs's C2 that no settling was of.
        becomes("A1\nB\nC1");
        UNSCOPED_INFO("joined: a conflict still");
        CHECK(merge->conflictCount() == 1);
        UNSCOPED_INFO("found by the line not settled");
        CHECK(inOurs(2, 1).size() == 1U);
        becomes(o);
        UNSCOPED_INFO("apart again: the one not settled");
        CHECK(merge->conflictCount() == 1);
        {
            INFO("the first settled still");
            CHECK(inOurs(0, 1).empty());
            CHECK(inOurs(2, 1).size() == 1U);
        }

        begin(b, o, t);
        require_settled(0, ALTextMerge::Take::Ours);
        require_settled(2, ALTextMerge::Take::Ours);
        UNSCOPED_INFO("both settled");
        CHECK(merge->conflictCount() == 0);
        becomes("A1\nB\nC1");
        UNSCOPED_INFO("both settled, joined: none");
        CHECK(merge->conflictCount() == 0);
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: start with blanks let go of puts in theirs where ours did not change", "[aldiffmerge]")
    {
        // Theirs re-indented lines 2 to 5 and changed line 3; ours changed
        // line 5 and re-indented line 7.
        const std::string b = "line 0\nline 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7";
        const std::string t = "line 0\nline 1\n  line 2\n  three\n  line 4\n  line 5\nline 6\nline 7";
        const std::string o = "line 0\nline 1\nline 2\nline 3\nline 4\nmine 5\nline 6\n    line 7";
        UNSCOPED_INFO("as they are: both changed lines 2 to 5, and ours kept there");
        CHECK(ALDiffMerge::start(b, o, t) == o);
        ALTextDiff::Options loose;
        loose.like.ignoreWhitespace = true;
        const std::string begun = ALDiffMerge::start(b, o, t, loose);
        UNSCOPED_INFO("blanks let go of: theirs's line 3 put in, ours's indent kept");
        CHECK(begun ==
                      std::string("line 0\nline 1\nline 2\n  three\nline 4\nmine 5\nline 6\n    line 7"));
        // Merged so, no conflict, as a merge letting blanks go finds.
        begin(b, begun, t, loose);
        CHECK(merge->conflictCount() == 0);
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: ours spliced in between its edges merges as ours given whole", "[aldiffmerge]")
    {
        // Theirs changed lines 10 and 100, ours lines 50 and 150.
        lines_t base_lines;
        for (S32 n = 0; n < 200; ++n)
        {
            base_lines.push_back("line " + std::to_string(n));
        }
        lines_t theirs_lines = base_lines;
        theirs_lines[10]     = "theirs 10";
        theirs_lines[100]    = "theirs 100";
        lines_t ours_lines   = base_lines;
        ours_lines[50]       = "mine 50";
        ours_lines[150]      = "mine 150";
        ALDiffMerge spliced(base_lines, theirs_lines);
        ALDiffMerge whole(base_lines, theirs_lines);
        spliced.setOurs(ours_lines);
        whole.setOurs(ours_lines);
        const auto edit = [&](const auto& made, const std::string& where, S32 conflicts) {
            CAPTURE(where);
            lines_t now = ours_lines;
            made(now);
            spliced.setOurs(now, ALDiffEdit::edgesOf(ours_lines, now));
            {
                INFO("compared again only about the edit");
                REQUIRE(ALDiffSplice::lastCompared() > 0);
                REQUIRE(ALDiffSplice::lastCompared() <= 6);
            }
            whole.setOurs(now);
            UNSCOPED_INFO("as with ours whole");
            REQUIRE(spliced.hunks() == whole.hunks());
            REQUIRE(spliced.conflictCount() == conflicts);
            ours_lines = std::move(now);
        };
        edit([](lines_t& lines) { lines[120] = "mine 120"; }, "a line typed into", 0);
        edit([](lines_t& lines) { lines.insert(lines.begin() + 30, "put in"); }, "a line put in", 0);
        edit([](lines_t& lines) { lines.erase(lines.begin() + 181); }, "a line taken out", 0);
        // The base's line 100 is ours's 101 now.
        edit([](lines_t& lines) { lines[101] = "mine 100"; }, "theirs's line changed otherwise", 1);
        edit([](lines_t& lines) { lines[101] = "line 100"; }, "and back", 0);
        UNSCOPED_INFO("the base as it was");
        CHECK(spliced.base() == base_lines);
    }

    TEST_CASE_METHOD(aldiffmerge_data, "aldiffmerge: with comments let go of ours spliced in merges as ours given whole", "[aldiffmerge]")
    {
        std::string                                  error;
        const std::shared_ptr<const ALSyntaxGrammar> lsl = ALSyntaxGrammar::fromFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/lsl.xml", error);
        UNSCOPED_INFO("the LSL grammar");
        REQUIRE(lsl != nullptr);
        // Theirs changed lines 10 and 100, ours lines 50 and 150; theirs's
        // and ours's comments on line 120 each their own, which comments
        // let go of are not a conflict.
        lines_t base_lines;
        for (S32 n = 0; n < 200; ++n)
        {
            base_lines.push_back("x" + std::to_string(n) + " = " + std::to_string(n) + "; // line " + std::to_string(n));
        }
        lines_t theirs_lines = base_lines;
        theirs_lines[10]     = "x10 = 1010;";
        theirs_lines[100]    = "x100 = 1100;";
        theirs_lines[120]    = "x120 = 120; // theirs";
        lines_t ours_lines   = base_lines;
        ours_lines[50]       = "x50 = 2050;";
        ours_lines[150]      = "x150 = 2150;";
        ours_lines[120]      = "x120 = 120; // ours";
        // Each with a lexer of its own, as a comparison gives a merge one:
        // the one made anew told what an edit knows.
        ALTextDiff::Options commented;
        commented.like.ignoreComments            = true;
        const std::shared_ptr<ALDiffLexer> lexer = std::make_shared<ALDiffLexer>(lsl);
        commented.lexer                          = ALDiffLexer::lexerOf(lexer);
        ALDiffMerge spliced(base_lines, theirs_lines, commented);
        spliced.setLexing(ALDiffLexer::rereadOf(lexer), ALDiffLexer::toldOf(lexer));
        commented.lexer = ALDiffLexer::lexerOf(std::make_shared<ALDiffLexer>(lsl));
        ALDiffMerge whole(base_lines, theirs_lines, commented);
        spliced.setOurs(ours_lines);
        whole.setOurs(ours_lines);
        UNSCOPED_INFO("comments let go of: no conflict");
        CHECK(spliced.conflictCount() == 0);
        const auto edit = [&](const auto& made, const std::string& where, S32 most, S32 conflicts) {
            CAPTURE(where);
            lines_t now = ours_lines;
            made(now);
            const U64 hashed = ALTextDiff::hashed();
            const U64 looked = lexer->linesCompared();
            spliced.setOurs(now, ALDiffEdit::edgesOf(ours_lines, now));
            const U64 cost     = ALTextDiff::hashed() - hashed;
            const S32 compared = ALDiffSplice::lastCompared();
            UNSCOPED_INFO("ours read by what the edit knows, none of its lines compared with those the lexer holds");
            REQUIRE(lexer->linesCompared() == looked);
            // A most of none: more than half the text read otherwise, which
            // is compared whole.
            if (most >= 0)
            {
                {
                    INFO("compared again only about the edit");
                    REQUIRE(compared > 0);
                    REQUIRE(compared <= most);
                }
                UNSCOPED_INFO("as many lines' regions made a number");
                REQUIRE(cost <= static_cast<U64>(2 * most));
            }
            whole.setOurs(now);
            UNSCOPED_INFO("as with ours whole");
            REQUIRE(spliced.hunks() == whole.hunks());
            REQUIRE(spliced.conflictCount() == conflicts);
            ours_lines = std::move(now);
        };
        edit([](lines_t& lines) { lines[130] = "x130 = 2130;"; }, "a line typed into", 6, 0);
        edit([](lines_t& lines) { lines[130] = "x130 = 2130; // and said so"; }, "a comment typed", 6, 0);
        edit([](lines_t& lines) { lines.insert(lines.begin() + 30, "y = 1;"); }, "a line put in", 6, 0);
        edit([](lines_t& lines) { lines.erase(lines.begin() + 181); }, "a line taken out", 6, 0);
        // The base's line 100 is ours's 101 now.
        edit([](lines_t& lines) { lines[101] = "x100 = 2100;"; }, "theirs's line changed otherwise", 6, 1);
        edit([&base_lines](lines_t& lines) { lines[101] = base_lines[100]; }, "and back", 6, 0);
        // A block comment opened in the code of line 70 and closed on line
        // 80: the lines after each read otherwise, and are compared again
        // with the edit -- to the end once it is opened, as far as it went
        // once it is closed. Theirs's line 100 commented out by ours is a
        // conflict while it is.
        const auto code = [](const std::string& line) { return line.substr(0, line.find(" //")); };
        edit([&code](lines_t& lines) { lines[70] = code(lines[70]) + " /* opened"; }, "a comment opened", -1, 1);
        edit([](lines_t& lines) { lines[80] = "closed */ " + lines[80]; }, "and closed", -1, 0);
        // Inside the lines commented out, a change of the base's all: that
        // change compared again, about it.
        edit([](lines_t& lines) { lines[75] = "x75 = 2075;"; }, "a line in the comment changed", 24, 0);
        // Given whole, closed further on: read as the text it is, not as the
        // one the lexer last read for it.
        lines_t moved = ours_lines;
        moved[80]     = moved[80].substr(std::string("closed */ ").size());
        moved[90]     = "closed */ " + moved[90];
        spliced.setOurs(moved);
        whole.setOurs(moved);
        UNSCOPED_INFO("given whole: as with ours whole");
        CHECK(spliced.hunks() == whole.hunks());
        UNSCOPED_INFO("and the lines it closed over changes");
        CHECK(spliced.hunks().size() > 3);
        UNSCOPED_INFO("the base as it was");
        CHECK(spliced.base() == base_lines);
    }
}
