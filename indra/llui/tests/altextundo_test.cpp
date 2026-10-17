/**
 * @file tests/altextundo_test.cpp
 * @brief The steps back and forward through a document's edits.
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

#include "altextundo.h"

#include <catch2/catch_test_macros.hpp>

#include "llsdserialize.h"

#include <sstream>
#include <string>

namespace
{
    struct altextundo_data
    {
        ALTextDocument doc;
        ALTextUndo     undo{ doc };
        F64            now = 100.0;

        // Type one character where the caret is, the way an editor would.
        ALTextPos type(ALTextPos at, const char* text, F64 later = 0.1)
        {
            now += later;
            ALTextDocument::Edit edit = doc.insert(at, text);
            const ALTextPos      after = edit.endAfter();
            undo.record(edit, at, after, now);
            return after;
        }

        ALTextPos backspace(ALTextPos at, F64 later = 0.1)
        {
            now += later;
            const ALTextPos      before = doc.prevCluster(at);
            ALTextDocument::Edit edit   = doc.remove(ALTextRange(before, at));
            undo.record(edit, at, before, now);
            return before;
        }
    };

    TEST_CASE_METHOD(altextundo_data, "altextundo: a run of typing is one step and is undone and redone as one", "[altextundo]")
    {
        ALTextPos at = type(ALTextPos(0, 0), "a");
        at           = type(at, "b");
        at           = type(at, "c");
        CHECK(doc.text() == std::string("abc"));
        CHECK(undo.canUndo());
        std::optional<ALTextRange> caret = undo.undo();
        REQUIRE(caret.has_value());
        UNSCOPED_INFO("all of it");
        CHECK(doc.text() == std::string());
        {
            INFO("caret back at the start, nothing selected");
            CHECK(caret->end == ALTextPos(0, 0));
            CHECK(caret->empty());
        }
        CHECK_FALSE(undo.canUndo());
        caret = undo.redo();
        REQUIRE(caret.has_value());
        UNSCOPED_INFO("redone");
        CHECK(doc.text() == std::string("abc"));
        {
            INFO("caret after the run");
            CHECK(caret->end == ALTextPos(0, 3));
            CHECK(caret->empty());
        }
        CHECK_FALSE(undo.redo().has_value());
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a pause or a move elsewhere or a line break ends the run", "[altextundo]")
    {
        ALTextPos at = type(ALTextPos(0, 0), "a");
        at           = type(at, "b", 5.0);  // a pause
        undo.undo();
        UNSCOPED_INFO("only the last");
        CHECK(doc.text() == std::string("a"));
        at = type(ALTextPos(0, 1), "c");
        type(ALTextPos(0, 0), "z");  // somewhere else
        undo.undo();
        UNSCOPED_INFO("the move started a new step");
        CHECK(doc.text() == std::string("ac"));
        at = type(ALTextPos(0, 2), "\n");
        type(ALTextPos(1, 0), "d");
        undo.undo();
        UNSCOPED_INFO("a line break is its own step");
        CHECK(doc.text() == std::string("ac\n"));
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: backspaces join into one step going backwards", "[altextundo]")
    {
        doc.setText("abcd");
        undo.markSaved();
        ALTextPos at = backspace(ALTextPos(0, 4));
        at           = backspace(at);
        UNSCOPED_INFO("two gone");
        CHECK(doc.text() == std::string("ab"));
        undo.undo();
        UNSCOPED_INFO("both back");
        CHECK(doc.text() == std::string("abcd"));
        UNSCOPED_INFO("pristine again");
        CHECK(undo.isPristine());
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a group is one step however many edits it holds", "[altextundo]")
    {
        doc.setText("one two");
        undo.beginGroup();
        ALTextDocument::Edit first = doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)), "1");
        undo.record(first, ALTextPos(0, 0), first.endAfter(), now);
        ALTextDocument::Edit second = doc.replace(ALTextRange(ALTextPos(0, 2), ALTextPos(0, 5)), "2");
        undo.record(second, ALTextPos(0, 2), second.endAfter(), now + 30.0);
        undo.endGroup();
        UNSCOPED_INFO("both applied");
        CHECK(doc.text() == std::string("1 2"));
        undo.undo();
        UNSCOPED_INFO("both undone at once");
        CHECK(doc.text() == std::string("one two"));
        undo.redo();
        UNSCOPED_INFO("both redone at once");
        CHECK(doc.text() == std::string("1 2"));
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: the saved mark survives undo and redo and a save ends the run", "[altextundo]")
    {
        doc.setText("x");
        undo.markSaved();
        UNSCOPED_INFO("saved is pristine");
        CHECK(undo.isPristine());
        type(ALTextPos(0, 1), "y");
        UNSCOPED_INFO("changed");
        CHECK_FALSE(undo.isPristine());
        undo.undo();
        UNSCOPED_INFO("undone to the saved text");
        CHECK(undo.isPristine());
        undo.redo();
        UNSCOPED_INFO("redone away from it");
        CHECK_FALSE(undo.isPristine());
        undo.markSaved();
        UNSCOPED_INFO("saved here now");
        CHECK(undo.isPristine());
        type(ALTextPos(0, 2), "z");
        UNSCOPED_INFO("typed on after the save is a change");
        CHECK_FALSE(undo.isPristine());
        undo.undo();
        UNSCOPED_INFO("and its own step, so undoing it is the saved text");
        CHECK(undo.isPristine());
        CHECK(doc.text() == std::string("xy"));
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a change after an undo drops the redo steps and the saved mark in them", "[altextundo]")
    {
        doc.setText("a");
        type(ALTextPos(0, 1), "b");
        undo.markSaved();
        undo.undo();
        UNSCOPED_INFO("can redo to the saved text");
        CHECK(undo.canRedo());
        type(ALTextPos(0, 1), "c", 5.0);
        UNSCOPED_INFO("redo gone");
        CHECK_FALSE(undo.canRedo());
        UNSCOPED_INFO("saved text unreachable");
        CHECK_FALSE(undo.isPristine());
        undo.undo();
        UNSCOPED_INFO("still not, since that step was not the saved one");
        CHECK_FALSE(undo.isPristine());
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a label names the step for undo and redo and an empty edit is no step", "[altextundo]")
    {
        type(ALTextPos(0, 0), "a");
        undo.label("Type");
        CHECK(undo.undoLabel() == std::string("Type"));
        undo.undo();
        CHECK(undo.redoLabel() == std::string("Type"));
        undo.record(ALTextDocument::Edit(), ALTextPos(), ALTextPos(), now);
        UNSCOPED_INFO("an empty edit is not a step");
        CHECK_FALSE(undo.canUndo());
    }

    // Steps of 300 KB, three of which the steps back hold: a fourth lets the
    // oldest go, and the saved mark, counted from the bottom, moves down
    // with it -- until the step that led to the saved text is let go too,
    // and stepping back can never reach it, and the journal must not say it
    // has.
    TEST_CASE_METHOD(altextundo_data, "altextundo: the saved mark follows forgotten steps and is lost once out of reach", "[altextundo]")
    {
        const std::string part(300 * 1024, 'a');
        ALTextPos         at(0, 0);
        at = type(at, part.c_str(), 2.0);
        at = type(at, part.c_str(), 2.0);
        undo.markSaved();
        UNSCOPED_INFO("saved");
        CHECK(undo.isPristine());
        at = type(at, part.c_str(), 2.0);
        at = type(at, part.c_str(), 2.0);
        UNSCOPED_INFO("changed since");
        CHECK_FALSE(undo.isPristine());
        undo.undo();
        UNSCOPED_INFO("two steps in force again, but not the saved two");
        CHECK_FALSE(undo.isPristine());
        undo.undo();
        UNSCOPED_INFO("back to the saved text, the mark having moved down");
        CHECK(undo.isPristine());
        undo.undo();
        UNSCOPED_INFO("and one step back past it, the oldest having gone");
        CHECK_FALSE(undo.canUndo());

        // Forward again, and on until the step that led to the saved text is
        // let go too.
        for (S32 i = 0; i < 3; ++i)
        {
            undo.redo();
        }
        at = type(at, part.c_str(), 2.0);
        UNSCOPED_INFO("the saved text at the bottom of the stack, still reached");
        CHECK(undo.savedText().has_value());
        type(at, part.c_str(), 2.0);
        UNSCOPED_INFO("the step that led to it gone, and the saved text with it");
        CHECK_FALSE(undo.savedText().has_value());
        while (undo.undo())
        {
        }
        UNSCOPED_INFO("stepping back as far as it goes never finds it");
        CHECK_FALSE(undo.isPristine());
    }

    // A save point is taken when a text is sent to be saved, and marks that
    // text saved when the answer comes, whatever was typed or undone
    // meanwhile.
    TEST_CASE_METHOD(altextundo_data, "altextundo: a save point marks the sent text saved whatever was typed or undone since", "[altextundo]")
    {
        ALTextPos       at    = type(ALTextPos(0, 0), "a");
        at                    = type(at, "b");
        const ALTextUndo::SavePoint sent = undo.savePoint();
        // Typed while the save was on its way, within the run's window:
        // a step of its own all the same.
        at = type(at, "c");
        undo.markSaved(sent);
        UNSCOPED_INFO("what was typed meanwhile is unsaved");
        CHECK_FALSE(undo.isPristine());
        undo.undo();
        UNSCOPED_INFO("the step back is only what came after");
        CHECK(doc.text() == std::string("ab"));
        UNSCOPED_INFO("and that is the saved text");
        CHECK(undo.isPristine());
        undo.redo();
        UNSCOPED_INFO("forward again, unsaved");
        CHECK_FALSE(undo.isPristine());

        // Undone past before the answer came: the saved text is a step
        // forward.
        const ALTextUndo::SavePoint again = undo.savePoint();
        undo.undo();
        undo.markSaved(again);
        UNSCOPED_INFO("a step short of the saved text");
        CHECK_FALSE(undo.isPristine());
        undo.redo();
        UNSCOPED_INFO("the redo reaches it");
        CHECK(undo.isPristine());

        // Undone, then changed: the saved text can no longer be reached.
        const ALTextUndo::SavePoint lost = undo.savePoint();
        undo.undo();
        type(ALTextPos(0, 2), "x", 5.0);
        undo.markSaved(lost);
        UNSCOPED_INFO("unreachable, so never pristine");
        CHECK_FALSE(undo.isPristine());
        undo.undo();
        UNSCOPED_INFO("not even stepping back");
        CHECK_FALSE(undo.isPristine());

        // A point before any step at all.
        undo.clear();
        doc.setText("");
        const ALTextUndo::SavePoint empty = undo.savePoint();
        type(ALTextPos(0, 0), "z", 5.0);
        undo.markSaved(empty);
        UNSCOPED_INFO("the empty text was saved, the z is not");
        CHECK_FALSE(undo.isPristine());
        undo.undo();
        UNSCOPED_INFO("back to it");
        CHECK(undo.isPristine());
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a text marked never saved is unsaved at every step until a save", "[altextundo]")
    {
        ALTextPos at = type(ALTextPos(0, 0), "a");
        undo.markSaved();
        UNSCOPED_INFO("saved");
        CHECK(undo.isPristine());
        undo.markNeverSaved();
        UNSCOPED_INFO("never saved: unsaved as it stands");
        CHECK_FALSE(undo.isPristine());
        at = type(at, "b", 5.0);
        undo.undo();
        UNSCOPED_INFO("and stepping back does not find a saved text");
        CHECK_FALSE(undo.isPristine());
        undo.redo();
        undo.markSaved();
        UNSCOPED_INFO("a save marks it again");
        CHECK(undo.isPristine());
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a history written out reads back over the same text step for step", "[altextundo]")
    {
        ALTextPos at = type(ALTextPos(0, 0), "one");
        at           = type(at, "\n", 5.0);
        at           = type(at, "two", 5.0);
        undo.markSaved();
        at = type(at, " three", 5.0);
        at = backspace(at, 5.0);
        undo.label("rename");
        undo.undo();
        CHECK(doc.text() == std::string("one\ntwo three"));
        const LLSD written = undo.asLLSD();

        // Another session: the same text, a journal of its own.
        ALTextDocument later(doc.text());
        ALTextUndo     again(later);
        CHECK(again.fromLLSD(written));
        UNSCOPED_INFO("unsaved, as it was");
        CHECK_FALSE(again.isPristine());
        UNSCOPED_INFO("a step forward still to take");
        CHECK(again.canRedo());
        again.redo();
        UNSCOPED_INFO("the step forward");
        CHECK(later.text() == std::string("one\ntwo thre"));
        UNSCOPED_INFO("with its name");
        CHECK(again.undoLabel() == std::string("rename"));
        again.undo();
        again.undo();
        UNSCOPED_INFO("back past the typing");
        CHECK(later.text() == std::string("one\ntwo"));
        UNSCOPED_INFO("to the saved text");
        CHECK(again.isPristine());
        again.undo();
        again.undo();
        again.undo();
        UNSCOPED_INFO("back to the start");
        CHECK(later.text() == std::string());
        UNSCOPED_INFO("and no further");
        CHECK_FALSE(again.canUndo());
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a history is refused over another text and savedText steps to the mark", "[altextundo]")
    {
        ALTextPos at = type(ALTextPos(0, 0), "alpha");
        undo.markSaved();
        at = type(at, " beta", 5.0);
        const LLSD written = undo.asLLSD();
        const std::optional<std::string> saved = undo.savedText();
        {
            INFO("the saved text is found");
            REQUIRE(saved.has_value());
            CHECK(*saved == "alpha");
        }

        ALTextDocument other("alpha gamma");
        ALTextUndo     wrong(other);
        UNSCOPED_INFO("refused over a text it was not written with");
        CHECK_FALSE(wrong.fromLLSD(written));
        {
            INFO("and that journal left as it was");
            CHECK_FALSE(wrong.canUndo());
            CHECK_FALSE(wrong.canRedo());
        }
        UNSCOPED_INFO("and the text as it was");
        CHECK(other.text() == std::string("alpha gamma"));

        // A saved text among the steps forward is found forward.
        undo.markSaved();
        undo.undo();
        UNSCOPED_INFO("saved a step forward");
        CHECK(undo.savedText() == std::optional<std::string>("alpha beta"));
        undo.markSaved();
        undo.redo();
        UNSCOPED_INFO("saved a step back");
        CHECK(undo.savedText() == std::optional<std::string>("alpha"));
        undo.undo();
        UNSCOPED_INFO("the saved text is the one standing");
        CHECK(undo.savedText() == std::optional<std::string>("alpha"));
        undo.markSaved();
        undo.redo();
        undo.undo();
        undo.markNeverSaved();
        UNSCOPED_INFO("never saved has none");
        CHECK_FALSE(undo.savedText().has_value());
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a history past its budget keeps the newest steps and the mark they reach", "[altextundo]")
    {
        ALTextPos at = type(ALTextPos(0, 0), std::string(100, 'a').c_str());
        undo.markSaved();
        at = type(at, std::string(100, 'b').c_str(), 5.0);
        at = type(at, std::string(100, 'c').c_str(), 5.0);
        // Each step about a hundred and sixty written: its hundred
        // characters, and its carets, label and edit's places.
        const LLSD kept = undo.asLLSD(400);
        UNSCOPED_INFO("the two newest steps");
        CHECK(kept["undo"].size() == 2);
        UNSCOPED_INFO("the saved mark at the oldest kept");
        CHECK(kept["saved"].asInteger() == 0);
        ALTextDocument later(doc.text());
        ALTextUndo     again(later);
        CHECK(again.fromLLSD(kept));
        again.undo();
        again.undo();
        UNSCOPED_INFO("to the saved text");
        CHECK(again.isPristine());
        UNSCOPED_INFO("and no further");
        CHECK_FALSE(again.canUndo());
        const LLSD tight = undo.asLLSD(200);
        UNSCOPED_INFO("only the newest");
        CHECK(tight["undo"].size() == 1);
        UNSCOPED_INFO("the saved text out of reach");
        CHECK(tight["saved"].asInteger() == -1);
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a run of typing or erasing is written as the one edit it amounts to", "[altextundo]")
    {
        ALTextPos at = type(ALTextPos(0, 0), "one two");
        at           = type(at, "\n", 5.0);
        // Typed a character at a time: one step, an edit a character.
        for (const char* c : { "t", "h", "r", "e", "e" })
        {
            at = type(at, c);
        }
        // Backspaced over two of them.
        at = backspace(at, 5.0);
        at = backspace(at);
        // Deleted forward twice from the middle of the first line: each
        // takes the character that moved into the place of the last.
        now += 5.0;
        ALTextDocument::Edit first = doc.remove(ALTextRange(ALTextPos(0, 3), ALTextPos(0, 4)));
        undo.record(first, ALTextPos(0, 3), ALTextPos(0, 3), now);
        now += 0.1;
        ALTextDocument::Edit second = doc.remove(ALTextRange(ALTextPos(0, 3), ALTextPos(0, 4)));
        undo.record(second, ALTextPos(0, 3), ALTextPos(0, 3), now);
        CHECK(doc.text() == std::string("onewo\nthr"));

        const LLSD written = undo.asLLSD();
        REQUIRE(written["undo"].size() == 5);
        UNSCOPED_INFO("the typing one edit");
        REQUIRE(written["undo"][2]["edits"].size() == 1);
        UNSCOPED_INFO("with all it typed");
        CHECK(written["undo"][2]["edits"][0][5].asString() == std::string("three"));
        UNSCOPED_INFO("the backspacing one edit");
        REQUIRE(written["undo"][3]["edits"].size() == 1);
        UNSCOPED_INFO("with all it took, in order");
        CHECK(written["undo"][3]["edits"][0][4].asString() == std::string("ee"));
        UNSCOPED_INFO("the deleting one edit");
        REQUIRE(written["undo"][4]["edits"].size() == 1);
        UNSCOPED_INFO("with all it took, in order");
        CHECK(written["undo"][4]["edits"][0][4].asString() == std::string(" t"));

        ALTextDocument later(doc.text());
        ALTextUndo     again(later);
        CHECK(again.fromLLSD(written));
        again.undo();
        UNSCOPED_INFO("the deletes back");
        CHECK(later.text() == std::string("one two\nthr"));
        again.undo();
        UNSCOPED_INFO("the backspaces back");
        CHECK(later.text() == std::string("one two\nthree"));
        again.undo();
        UNSCOPED_INFO("the typing back");
        CHECK(later.text() == std::string("one two\n"));
        again.redo();
        again.redo();
        again.redo();
        UNSCOPED_INFO("and forward again");
        CHECK(later.text() == doc.text());
        LLSD older       = written;
        older["version"] = 1;
        UNSCOPED_INFO("a history of another version is refused");
        CHECK_FALSE(again.fromLLSD(older));
    }

    // A group's edits fold where each carries on from the last, across lines
    // too, and a run of erasing both ways folds as one.
    TEST_CASE_METHOD(altextundo_data, "altextundo: edits that carry on from the last fold into one in a group or a run", "[altextundo]")
    {
        doc.setText("a\nbc\ndef");
        const std::string before = doc.text();
        const auto        edit   = [this](ALTextDocument::Edit made) {
            undo.record(made, made.range.begin, made.endAfter(), now);
            return made;
        };
        now += 5.0;
        undo.beginGroup();
        // Two lines put in, and typed on at their end: one insertion.
        ALTextDocument::Edit made = edit(doc.insert(ALTextPos(0, 1), "x\ny"));
        edit(doc.insert(made.endAfter(), "z"));
        CHECK(doc.text() == std::string("ax\nyz\nbc\ndef"));
        // A line break taken, then the letter that moved into its place,
        // then the letter before them: one removal, across a line.
        edit(doc.remove(ALTextRange(ALTextPos(1, 2), ALTextPos(2, 0))));
        edit(doc.remove(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 3))));
        edit(doc.remove(ALTextRange(ALTextPos(1, 1), ALTextPos(1, 2))));
        CHECK(doc.text() == std::string("ax\nyc\ndef"));
        // And somewhere else: an edit of its own.
        edit(doc.insert(ALTextPos(2, 3), "!"));
        undo.endGroup();
        const std::string after = doc.text();

        const LLSD written = undo.asLLSD();
        const LLSD edits   = written["undo"][0]["edits"];
        UNSCOPED_INFO("three edits written for six made");
        REQUIRE(edits.size() == 3);
        UNSCOPED_INFO("the insertion whole");
        CHECK(edits[0][5].asString() == std::string("x\nyz"));
        UNSCOPED_INFO("the removal whole, in the order it stood");
        CHECK(edits[1][4].asString() == std::string("z\nb"));

        ALTextDocument later(after);
        ALTextUndo     again(later);
        CHECK(again.fromLLSD(written));
        again.undo();
        UNSCOPED_INFO("stepped back to where it began");
        CHECK(later.text() == before);
        again.redo();
        UNSCOPED_INFO("and forward to where it ended");
        CHECK(later.text() == after);

        // A backspace and then a delete where the caret stood: one run, and
        // one edit written, taking what stood on both sides.
        doc.setText("abcdefg");
        undo.clear();
        now += 5.0;
        ALTextDocument::Edit back = doc.remove(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 5)));
        undo.record(back, ALTextPos(0, 5), ALTextPos(0, 4), now);
        now += 0.1;
        ALTextDocument::Edit ahead = doc.remove(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 5)));
        undo.record(ahead, ALTextPos(0, 4), ALTextPos(0, 4), now);
        const LLSD run = undo.asLLSD();
        REQUIRE(run["undo"].size() == 1);
        REQUIRE(run["undo"][0]["edits"].size() == 1);
        UNSCOPED_INFO("both letters");
        CHECK(run["undo"][0]["edits"][0][4].asString() == std::string("ef"));
        ALTextDocument erased(doc.text());
        ALTextUndo     restored(erased);
        CHECK(restored.fromLLSD(run));
        restored.undo();
        UNSCOPED_INFO("stepped back whole");
        CHECK(erased.text() == std::string("abcdefg"));
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a step keeps its selection before and the one settle gave it after", "[altextundo]")
    {
        doc.setText("one two three");
        now += 5.0;
        // "two" selected from its end back to its start, and typed over.
        ALTextDocument::Edit over = doc.replace(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)), "2");
        undo.record(over, ALTextRange(ALTextPos(0, 7), ALTextPos(0, 4)), over.endAfter(), now);
        // Where the change put the selection once it was done: the new text
        // selected.
        undo.settle(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 5)));

        std::optional<ALTextRange> back = undo.undo();
        UNSCOPED_INFO("undone");
        CHECK(doc.text() == std::string("one two three"));
        {
            INFO("the selection it was made over, anchor and caret as they were");
            REQUIRE(back);
            CHECK(*back == ALTextRange(ALTextPos(0, 7), ALTextPos(0, 4)));
        }
        // Settled after a step back: nothing, since the step settled is not
        // the one last recorded.
        undo.settle(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
        std::optional<ALTextRange> ahead = undo.redo();
        UNSCOPED_INFO("redone");
        CHECK(doc.text() == std::string("one 2 three"));
        {
            INFO("the selection it was left with");
            REQUIRE(ahead);
            CHECK(*ahead == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 5)));
        }

        // Through the history written out and read back.
        const LLSD written = undo.asLLSD();
        ALTextDocument later(doc.text());
        ALTextUndo     again(later);
        CHECK(again.fromLLSD(written));
        back = again.undo();
        {
            INFO("the selection before, from the history");
            REQUIRE(back);
            CHECK(*back == ALTextRange(ALTextPos(0, 7), ALTextPos(0, 4)));
        }
        ahead = again.redo();
        {
            INFO("and after");
            REQUIRE(ahead);
            CHECK(*ahead == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 5)));
        }
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a save while a group is open -- vim's insert mode -- ends the step there", "[altextundo]")
    {
        undo.beginGroup();
        ALTextPos at = type(ALTextPos(0, 0), "a");
        at           = type(at, "b");
        undo.markSaved();
        UNSCOPED_INFO("saved");
        CHECK(undo.isPristine());
        at = type(at, "c");
        UNSCOPED_INFO("typed on: changed");
        CHECK_FALSE(undo.isPristine());
        undo.endGroup();
        undo.undo();
        UNSCOPED_INFO("the step after the save undone alone");
        CHECK(doc.text() == std::string("ab"));
        UNSCOPED_INFO("which is the saved text");
        CHECK(undo.isPristine());
        undo.undo();
        UNSCOPED_INFO("then the step before it");
        CHECK(doc.text() == std::string());
        UNSCOPED_INFO("nothing open");
        CHECK_FALSE(undo.inGroup());
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: a run joins into one edit as it goes and is undone as one change", "[altextundo]")
    {
        S32 changes = 0;
        doc.onChanged([&changes](const ALTextDocument::Edit&) { ++changes; });
        ALTextPos at(0, 0);
        for (const char* c : { "h", "e", "l", "l", "o", " ", "w", "o", "r", "l", "d" })
        {
            at = type(at, c);
        }
        at = backspace(at);
        at = backspace(at);
        UNSCOPED_INFO("typed, and two taken back");
        CHECK(doc.text() == std::string("hello wor"));
        changes = 0;
        undo.undo();
        UNSCOPED_INFO("the erasing, as one change");
        CHECK(changes == 1);
        UNSCOPED_INFO("both back");
        CHECK(doc.text() == std::string("hello world"));
        changes = 0;
        undo.undo();
        UNSCOPED_INFO("the typing, as one change");
        CHECK(changes == 1);
        UNSCOPED_INFO("all of it");
        CHECK(doc.text() == std::string());
        changes = 0;
        undo.redo();
        UNSCOPED_INFO("redone as one change");
        CHECK(changes == 1);
        UNSCOPED_INFO("redone");
        CHECK(doc.text() == std::string("hello world"));
    }

    // Hundreds of small steps are kept; of big ones the oldest are let go,
    // and the saved mark with them.
    TEST_CASE_METHOD(altextundo_data, "altextundo: the steps back are held by what they weigh and not by how many", "[altextundo]")
    {
        ALTextPos at(0, 0);
        for (S32 i = 0; i < 300; ++i)
        {
            // Each later than the window: a step of its own.
            at = type(at, "x", 2.0);
        }
        S32 steps = 0;
        while (undo.undo())
        {
            ++steps;
        }
        UNSCOPED_INFO("every one of three hundred kept");
        CHECK(steps == 300);
        UNSCOPED_INFO("back to nothing");
        CHECK(doc.text() == std::string());

        ALTextDocument big_doc;
        ALTextUndo     big{ big_doc };
        const std::string part(400 * 1024, 'a');
        ALTextPos         end(0, 0);
        const auto        put = [&](F64 when) {
            ALTextDocument::Edit edit = big_doc.insert(end, part);
            big.record(edit, end, edit.endAfter(), when);
            end = edit.endAfter();
        };
        put(1.0);
        big.markSaved();
        put(3.0);
        big.undo();
        UNSCOPED_INFO("two of 400 KB kept: one still back past the newest");
        CHECK(big.canUndo());
        big.redo();
        put(5.0);
        UNSCOPED_INFO("three written");
        CHECK(big_doc.text().size() == part.size() * 3);
        UNSCOPED_INFO("past the budget, the oldest went: the saved text still reached");
        CHECK(big.savedText().value_or(std::string()).size() == part.size());
        big.undo();
        big.undo();
        UNSCOPED_INFO("two back, and no more");
        CHECK_FALSE(big.canUndo());
        UNSCOPED_INFO("the first's text stands");
        CHECK(big_doc.text().size() == part.size());
        UNSCOPED_INFO("which is the saved one");
        CHECK(big.isPristine());

        const std::string huge(ALTextUndo::BUDGET * 2, 'b');
        ALTextDocument::Edit edit = big_doc.insert(big_doc.end(), huge);
        big.record(edit, big_doc.end(), edit.endAfter(), 9.0);
        UNSCOPED_INFO("one step past the budget alone is kept, the newest always");
        CHECK(big.canUndo());
        big.undo();
        UNSCOPED_INFO("and it alone");
        CHECK_FALSE(big.canUndo());
    }

    // What one key typed does -- a pair with the caret between, a closer
    // typed over, a line broken and indented, an outdent -- is one with the
    // typing run.
    TEST_CASE_METHOD(altextundo_data, "altextundo: all one key does joins the typing run", "[altextundo]")
    {
        // A key typed where `at` is: its text put in, the caret where the
        // key leaves it.
        const auto key = [&](ALTextPos at, const char* text, std::optional<ALTextPos> caret = std::nullopt) {
            undo.beginTyping(ALTextRange(at, at));
            ALTextPos after = type(at, text);
            if (caret)
            {
                after = *caret;
                undo.settle(ALTextRange(after, after));
            }
            undo.endTyping();
            return after;
        };
        ALTextPos at = key(ALTextPos(0, 0), "f");
        at           = key(at, "()", ALTextPos(0, 2));
        at           = key(at, "x");
        // ")" typed over the closer: nothing put in, the caret past it.
        undo.beginTyping(ALTextRange(at, at));
        at = ALTextPos(0, 4);
        undo.settle(ALTextRange(at, at));
        undo.endTyping();
        at = key(at, "\n    ");
        at = key(at, "y");
        // "}" typed, and the line's indentation taken back by the same key.
        undo.beginTyping(ALTextRange(at, at));
        type(at, "}");
        now += 0.01;
        const ALTextDocument::Edit outdent = doc.remove(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 4)));
        undo.record(outdent, ALTextPos(1, 6), ALTextPos(1, 2), now);
        undo.endTyping();
        CHECK(doc.text() == std::string("f(x)\ny}"));
        undo.undo();
        UNSCOPED_INFO("one step takes all of it back");
        CHECK(doc.text() == std::string());
        UNSCOPED_INFO("nothing before it");
        CHECK_FALSE(undo.canUndo());
        undo.redo();
        UNSCOPED_INFO("and puts it back");
        CHECK(doc.text() == std::string("f(x)\ny}"));

        // A key typed somewhere else is a step of its own.
        key(ALTextPos(0, 0), "z");
        UNSCOPED_INFO("typed at the start");
        CHECK(doc.text() == std::string("zf(x)\ny}"));
        undo.undo();
        UNSCOPED_INFO("undone alone");
        CHECK(doc.text() == std::string("f(x)\ny}"));
    }

    TEST_CASE_METHOD(altextundo_data, "altextundo: historyFrom checks a history against its text and restore puts it back", "[altextundo]")
    {
        ALTextPos at = type(ALTextPos(0, 0), "one", 2.0);
        type(at, " two", 2.0);
        const LLSD written = undo.asLLSD();
        UNSCOPED_INFO("another text's is no history");
        CHECK_FALSE(ALTextUndo::historyFrom(written, "one six").has_value());
        std::optional<ALTextUndo::History> read = ALTextUndo::historyFrom(written, doc.text());
        UNSCOPED_INFO("its own text's is");
        REQUIRE(read.has_value());
        UNSCOPED_INFO("both steps back");
        CHECK(read->undo.size() == size_t(2));
        ALTextDocument again(doc.text());
        ALTextUndo     journal{ again };
        journal.restore(std::move(*read));
        journal.undo();
        journal.undo();
        UNSCOPED_INFO("stepped back through it");
        CHECK(again.text() == std::string());
    }

    // The newest step is written again as it changes, and the whole still
    // reads back as asLLSD's.
    TEST_CASE_METHOD(altextundo_data, "altextundo: asNotation reads back as asLLSD writes it as the newest step changes", "[altextundo]")
    {
        const auto require_notation_as_llsd = [this](const char* what) {
            INFO(what);
            std::istringstream in(undo.asNotation());
            LLSD               read;
            REQUIRE(LLSDSerialize::fromNotation(read, in, LLSDSerialize::SIZE_UNLIMITED) > 0);
            std::ostringstream a, b;
            LLSDSerialize::toNotation(read, a);
            LLSDSerialize::toNotation(undo.asLLSD(), b);
            UNSCOPED_INFO("as asLLSD writes it");
            REQUIRE(a.str() == b.str());
        };
        ALTextPos at = type(ALTextPos(0, 0), "one", 2.0);
        at           = type(at, " two", 2.0);
        undo.label("Typing");
        require_notation_as_llsd("two steps, one named");
        at = type(at, "s");
        require_notation_as_llsd("the newest joined");
        undo.settle(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        require_notation_as_llsd("its selection settled");
        undo.markSaved();
        undo.undo();
        require_notation_as_llsd("one taken back, the saved mark past it");
        std::optional<ALTextUndo::History> read = ALTextUndo::historyFrom(undo.asLLSD(), doc.text());
        UNSCOPED_INFO("and it is a history of the text");
        CHECK(read.has_value());
    }

    // A group closed to wait goes on with its step where nothing came
    // between: not after a change, a save, an undo or another's group.
    TEST_CASE_METHOD(altextundo_data, "altextundo: resumeGroup carries on a group's step only where nothing came between", "[altextundo]")
    {
        doc.setText("abc");
        // One group, closed, then gone on with: one step.
        undo.beginGroup();
        type(ALTextPos(0, 0), "1");
        undo.endGroup();
        const U64 step = undo.groupStep();
        UNSCOPED_INFO("the group's step");
        CHECK(step != 0);
        undo.resumeGroup(step);
        type(ALTextPos(0, 1), "2");
        undo.endGroup();
        UNSCOPED_INFO("still that step");
        CHECK(undo.groupStep() == step);
        undo.undo();
        UNSCOPED_INFO("both undone as one");
        CHECK(doc.text() == std::string("abc"));
        undo.redo();
        UNSCOPED_INFO("undone and redone, nothing to go on with");
        CHECK(undo.groupStep() == 0);

        // A change between: a step of its own, and the next group's own.
        undo.beginGroup();
        type(ALTextPos(0, 0), "x");
        undo.endGroup();
        const U64 before = undo.groupStep();
        type(ALTextPos(0, 6), "y", 10.0);
        UNSCOPED_INFO("a change after it");
        CHECK(undo.groupStep() == 0);
        undo.resumeGroup(before);
        type(ALTextPos(0, 0), "z");
        undo.endGroup();
        UNSCOPED_INFO("a step of its own");
        CHECK(undo.groupStep() != before);
        undo.undo();
        UNSCOPED_INFO("only the last");
        CHECK(doc.text() == std::string("x12abcy"));

        // Saved between: another step.
        undo.beginGroup();
        type(ALTextPos(0, 0), "s");
        undo.endGroup();
        const U64 saved = undo.groupStep();
        undo.savePoint();
        UNSCOPED_INFO("a save");
        CHECK(undo.groupStep() == 0);
        undo.resumeGroup(saved);
        type(ALTextPos(0, 0), "t");
        undo.endGroup();
        undo.undo();
        UNSCOPED_INFO("the save kept apart");
        CHECK(doc.text() == std::string("sx12abcy"));

        // Another group's step between.
        undo.beginGroup();
        type(ALTextPos(0, 0), "p");
        undo.endGroup();
        const U64 mine = undo.groupStep();
        undo.beginGroup();
        type(ALTextPos(0, 0), "q");
        undo.endGroup();
        undo.resumeGroup(mine);
        type(ALTextPos(0, 0), "r");
        undo.endGroup();
        undo.undo();
        UNSCOPED_INFO("not the other's");
        CHECK(doc.text() == std::string("qpsx12abcy"));
    }

    // A step keeps the other selections before and after it: a run the
    // first edit's before and the last's after; written and read back.
    TEST_CASE_METHOD(altextundo_data, "altextundo: a step keeps the other selections before and after and writes them out", "[altextundo]")
    {
        doc.setText("abc\nabc\nabc");
        const auto at = [](S32 line, S32 column) { return ALTextRange(ALTextPos(line, column), ALTextPos(line, column)); };
        const auto said = [](const std::vector<ALTextRange>& all) {
            std::string out;
            for (const ALTextRange& r : all)
            {
                out += llformat("%d:%d-%d:%d ", r.begin.line, r.begin.column, r.end.line, r.end.column);
            }
            return out;
        };
        // Typed at the main caret, with others on the lines below: each
        // edit noted with the others as they stood, and settled with them
        // as they stand after.
        now += 0.1;
        ALTextDocument::Edit first = doc.insert(ALTextPos(0, 1), "x");
        undo.record(first, at(0, 1), first.endAfter(), now, { at(1, 1), ALTextRange(ALTextPos(2, 3), ALTextPos(2, 0)) });
        undo.settle(at(0, 2), { at(1, 2), ALTextRange(ALTextPos(2, 4), ALTextPos(2, 0)) });
        now += 0.1;
        ALTextDocument::Edit second = doc.insert(ALTextPos(0, 2), "y");
        undo.record(second, at(0, 2), second.endAfter(), now, { at(1, 2) });
        undo.settle(at(0, 3), { at(1, 3) });
        // Said again without them: they stay as they were said.
        undo.settle(at(0, 3));

        std::vector<ALTextRange> others;
        std::optional<ALTextRange> back = undo.undo(&others);
        {
            INFO("one step");
            CHECK(back.has_value());
            CHECK(doc.text() == "abc\nabc\nabc");
        }
        UNSCOPED_INFO("the others as they stood before the run");
        CHECK(said(others) == said({ at(1, 1), ALTextRange(ALTextPos(2, 3), ALTextPos(2, 0)) }));
        std::optional<ALTextRange> ahead = undo.redo(&others);
        {
            INFO("and forward");
            REQUIRE(ahead.has_value());
            CHECK(*ahead == at(0, 3));
        }
        UNSCOPED_INFO("as they stood after it");
        CHECK(said(others) == said({ at(1, 3) }));

        // Written out and read back over the same text.
        ALTextDocument copy(doc.text());
        ALTextUndo     again(copy);
        CHECK(again.fromLLSD(undo.asLLSD()));
        std::istringstream notation(undo.asNotation());
        LLSD               written;
        {
            INFO("and in notation");
            CHECK(LLSDSerialize::fromNotation(written, notation, LLSDSerialize::SIZE_UNLIMITED) > 0);
            CHECK(written["undo"][0]["others_after"].size() == 1);
        }
        again.undo(&others);
        UNSCOPED_INFO("the others before, read back");
        CHECK(said(others) == said({ at(1, 1), ALTextRange(ALTextPos(2, 3), ALTextPos(2, 0)) }));
        again.redo(&others);
        UNSCOPED_INFO("and after");
        CHECK(said(others) == said({ at(1, 3) }));

        // A step whose others after were never said: a redo leaves the
        // caret alone; and one with none has none.
        now += 5.0;
        ALTextDocument::Edit third = doc.insert(ALTextPos(0, 0), "z");
        undo.record(third, at(0, 0), third.endAfter(), now, { at(2, 0) });
        undo.settle(at(0, 1));
        undo.undo(&others);
        UNSCOPED_INFO("before, as noted");
        CHECK(said(others) == said({ at(2, 0) }));
        undo.redo(&others);
        UNSCOPED_INFO("after, none");
        CHECK(others.empty());
        now += 5.0;
        type(ALTextPos(0, 0), "w");
        undo.undo(&others);
        UNSCOPED_INFO("none where there were none");
        CHECK(others.empty());
    }

    // A batch is kept by its stretches' own text, not the whole of the text
    // from the first to the last: keys typed at carets far apart keep the
    // steps before them, and step back and forward as batches.
    TEST_CASE_METHOD(altextundo_data, "altextundo: a batch keeps its stretches' own text and not the text between them", "[altextundo]")
    {
        // A name on the first line and on the last, forty thousand bytes
        // apart.
        std::string text = "name = 1;\n";
        for (S32 i = 0; i < 1000; ++i)
        {
            text += std::string(39, 'x') + "\n";
        }
        text += "name = 2;";
        doc.setText(text);
        const S32 last = doc.lineCount() - 1;
        type(ALTextPos(1, 0), "a", 5.0);
        undo.markSaved();
        const std::string saved = doc.text();
        type(ALTextPos(2, 0), "b", 5.0);
        const std::string before = doc.text();

        // A key typed at both, thirty times: one run.
        now += 5.0;
        const std::string typed = "abcdefghijklmnopqrstuvwxyz0123";
        for (size_t k = 0; k < typed.size(); ++k)
        {
            const S32         column = 4 + static_cast<S32>(k);
            const ALTextPos   at(0, column);
            const ALTextPos   other(last, column);
            const std::string key(1, typed[k]);
            undo.beginTyping(ALTextRange(at, at));
            const ALTextDocument::Edit edit = doc.replaceMany({ { ALTextRange(at, at), key }, { ALTextRange(other, other), key } });
            undo.record(edit, ALTextRange(at, at), ALTextPos(0, column + 1), now, { ALTextRange(other, other) });
            undo.endTyping();
            now += 0.1;
        }
        const std::string after = doc.text();
        UNSCOPED_INFO("typed at both");
        CHECK(doc.line(last) == std::string("name") + typed + " = 2;");
        UNSCOPED_INFO("the saved text still reached, past the run");
        CHECK(undo.savedText() == std::optional<std::string>(saved));
        UNSCOPED_INFO("the history written a few bytes a key");
        CHECK(undo.asNotation().size() < 16 * 1024);

        // Read back over the text as it stands, in another journal: the run
        // steps back to where it began.
        std::optional<ALTextUndo::History> read = ALTextUndo::historyFrom(undo.asLLSD(), after);
        UNSCOPED_INFO("the history read back");
        REQUIRE(read.has_value());
        ALTextDocument later(after);
        ALTextUndo     again(later);
        again.restore(std::move(*read));
        again.undo();
        UNSCOPED_INFO("and stepped back through");
        CHECK(later.text() == before);

        // Stepped back in its own journal: each key put back as the batch it
        // was, the run one step, and the steps before it still there.
        S32 changes = 0;
        S32 batches = 0;
        doc.onChanged([&changes, &batches](const ALTextDocument::Edit& edit) {
            ++changes;
            batches += edit.parts.size() == 2 ? 1 : 0;
        });
        undo.undo();
        UNSCOPED_INFO("the run one step");
        CHECK(doc.text() == before);
        UNSCOPED_INFO("each key put back as one change");
        CHECK(changes == static_cast<S32>(typed.size()));
        UNSCOPED_INFO("each a batch of both stretches");
        CHECK(batches == changes);
        UNSCOPED_INFO("the steps before it kept");
        CHECK(undo.canUndo());
        undo.undo();
        {
            INFO("back to the saved text");
            CHECK(undo.isPristine());
            CHECK(doc.text() == saved);
        }
        undo.redo();
        undo.redo();
        UNSCOPED_INFO("and forward to where the run ended");
        CHECK(doc.text() == after);

        // Stretches across lines, taking text and putting it, and one that
        // only takes: each with its own text, as it was and as it is after.
        undo.clear();
        doc.setText("alpha\nbeta\ngamma\ndelta");
        now += 5.0;
        const ALTextDocument::Edit many = doc.replaceMany({ { ALTextRange(ALTextPos(0, 1), ALTextPos(1, 2)), "X\nY" },
                                                            { ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "Z" },
                                                            { ALTextRange(ALTextPos(3, 2), ALTextPos(3, 5)), "" } });
        undo.record(many, ALTextPos(0, 1), many.endAfter(), now);
        CHECK(doc.text() == std::string("aX\nYta\nZgamma\nde"));
        const LLSD written = undo.asLLSD();
        const LLSD edits   = written["undo"][0]["edits"];
        UNSCOPED_INFO("written an edit a stretch");
        REQUIRE(edits.size() == 3);
        UNSCOPED_INFO("the last first, with what it took");
        CHECK(edits[0][4].asString() == std::string("lta"));
        UNSCOPED_INFO("the first last, with what it took and put");
        CHECK(edits[2][4].asString() + "|" + edits[2][5].asString() == std::string("lpha\nbe|X\nY"));
        undo.undo();
        UNSCOPED_INFO("stepped back");
        CHECK(doc.text() == std::string("alpha\nbeta\ngamma\ndelta"));
        undo.redo();
        UNSCOPED_INFO("and forward");
        CHECK(doc.text() == std::string("aX\nYta\nZgamma\nde"));
        ALTextDocument copy(doc.text());
        ALTextUndo     restored(copy);
        CHECK(restored.fromLLSD(written));
        restored.undo();
        UNSCOPED_INFO("and stepped back from the history");
        CHECK(copy.text() == std::string("alpha\nbeta\ngamma\ndelta"));
    }

    // The key that would take a run's step past the budget begins a step of
    // its own, and the history is written within the budget.
    TEST_CASE_METHOD(altextundo_data, "altextundo: a run stays in its step only while the step is within the budget", "[altextundo]")
    {
        // Ten keys typed one after the other, each putting in 300 KB: three
        // to a step.
        const std::string put(300 * 1024, 'k');
        ALTextPos         at(0, 0);
        for (S32 i = 0; i < 10; ++i)
        {
            undo.beginTyping(ALTextRange(at, at));
            at = type(at, put.c_str());
            undo.endTyping();
        }
        UNSCOPED_INFO("all typed");
        CHECK(doc.text().size() == 10 * put.size());
        UNSCOPED_INFO("the history written within the budget");
        CHECK(undo.asNotation().size() <= ALTextUndo::BUDGET);
        undo.undo();
        UNSCOPED_INFO("the last key a step of its own");
        CHECK(doc.text().size() == 9 * put.size());
        undo.redo();
        UNSCOPED_INFO("and put back");
        CHECK(doc.text().size() == 10 * put.size());
    }
}
