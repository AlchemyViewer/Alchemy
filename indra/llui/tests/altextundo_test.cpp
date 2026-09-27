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

#include "../altextundo.h"

#include "../test/lltut.h"

#include "llsdserialize.h"

#include <sstream>
#include <string>

namespace tut
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

    typedef test_group<altextundo_data> altextundo_group;
    typedef altextundo_group::object    altextundo_object;
    altextundo_group                    altextundo_instance("altextundo");

    template<> template<>
    void altextundo_object::test<1>()
    {
        set_test_name("a run of typing is one step, and comes back as one");
        ALTextPos at = type(ALTextPos(0, 0), "a");
        at           = type(at, "b");
        at           = type(at, "c");
        ensure_equals("typed", doc.text(), std::string("abc"));
        ensure("can undo", undo.canUndo());
        std::optional<ALTextRange> caret = undo.undo();
        ensure("undone", caret.has_value());
        ensure_equals("all of it", doc.text(), std::string());
        ensure("caret back at the start, nothing selected", caret->end == ALTextPos(0, 0) && caret->empty());
        ensure("nothing more to undo", !undo.canUndo());
        caret = undo.redo();
        ensure_equals("redone", doc.text(), std::string("abc"));
        ensure("caret after the run", caret->end == ALTextPos(0, 3) && caret->empty());
        ensure("nothing to redo", !undo.redo().has_value());
    }

    template<> template<>
    void altextundo_object::test<2>()
    {
        set_test_name("a pause, a move, or a line break ends the run");
        ALTextPos at = type(ALTextPos(0, 0), "a");
        at           = type(at, "b", 5.0);  // a pause
        undo.undo();
        ensure_equals("only the last", doc.text(), std::string("a"));
        at = type(ALTextPos(0, 1), "c");
        type(ALTextPos(0, 0), "z");  // somewhere else
        undo.undo();
        ensure_equals("the move started a new step", doc.text(), std::string("ac"));
        at = type(ALTextPos(0, 2), "\n");
        type(ALTextPos(1, 0), "d");
        undo.undo();
        ensure_equals("a line break is its own step", doc.text(), std::string("ac\n"));
    }

    template<> template<>
    void altextundo_object::test<3>()
    {
        set_test_name("backspaces join into one step going backwards");
        doc.setText("abcd");
        undo.markSaved();
        ALTextPos at = backspace(ALTextPos(0, 4));
        at           = backspace(at);
        ensure_equals("two gone", doc.text(), std::string("ab"));
        undo.undo();
        ensure_equals("both back", doc.text(), std::string("abcd"));
        ensure("pristine again", undo.isPristine());
    }

    template<> template<>
    void altextundo_object::test<4>()
    {
        set_test_name("a group is one step however many edits it holds");
        doc.setText("one two");
        undo.beginGroup();
        ALTextDocument::Edit first = doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)), "1");
        undo.record(first, ALTextPos(0, 0), first.endAfter(), now);
        ALTextDocument::Edit second = doc.replace(ALTextRange(ALTextPos(0, 2), ALTextPos(0, 5)), "2");
        undo.record(second, ALTextPos(0, 2), second.endAfter(), now + 30.0);
        undo.endGroup();
        ensure_equals("both applied", doc.text(), std::string("1 2"));
        undo.undo();
        ensure_equals("both undone at once", doc.text(), std::string("one two"));
        undo.redo();
        ensure_equals("both redone at once", doc.text(), std::string("1 2"));
    }

    template<> template<>
    void altextundo_object::test<5>()
    {
        set_test_name("the saved mark survives undo and redo, and a save ends the run");
        doc.setText("x");
        undo.markSaved();
        ensure("saved is pristine", undo.isPristine());
        ALTextPos at = type(ALTextPos(0, 1), "y");
        ensure("changed", !undo.isPristine());
        undo.undo();
        ensure("undone to the saved text", undo.isPristine());
        undo.redo();
        ensure("redone away from it", !undo.isPristine());
        undo.markSaved();
        ensure("saved here now", undo.isPristine());
        type(ALTextPos(0, 2), "z");
        ensure("typed on after the save is a change", !undo.isPristine());
        undo.undo();
        ensure("and its own step, so undoing it is the saved text", undo.isPristine());
        ensure_equals("text", doc.text(), std::string("xy"));
    }

    template<> template<>
    void altextundo_object::test<6>()
    {
        set_test_name("a change after an undo drops the redo steps, and the mark among them");
        doc.setText("a");
        type(ALTextPos(0, 1), "b");
        undo.markSaved();
        undo.undo();
        ensure("can redo to the saved text", undo.canRedo());
        type(ALTextPos(0, 1), "c", 5.0);
        ensure("redo gone", !undo.canRedo());
        ensure("saved text unreachable", !undo.isPristine());
        undo.undo();
        ensure("still not, since that step was not the saved one", !undo.isPristine());
    }

    template<> template<>
    void altextundo_object::test<7>()
    {
        set_test_name("labels name the next step either way, and nothing records nothing");
        type(ALTextPos(0, 0), "a");
        undo.label("Type");
        ensure_equals("undo says", undo.undoLabel(), std::string("Type"));
        undo.undo();
        ensure_equals("redo says", undo.redoLabel(), std::string("Type"));
        undo.record(ALTextDocument::Edit(), ALTextPos(), ALTextPos(), now);
        ensure("an empty edit is not a step", !undo.canUndo());
    }

    template<> template<>
    void altextundo_object::test<8>()
    {
        set_test_name("the saved mark goes down with the oldest step forgotten, and away once the saved text is out of reach");
        // Five steps, saved, then more than the stack keeps: the steps
        // that led to the saved text are gone, so stepping back can never
        // reach it, and the journal must not say it has.
        ALTextPos at = ALTextPos(0, 0);
        for (S32 i = 0; i < 5; ++i)
        {
            at = type(at, "a", 2.0);
        }
        undo.markSaved();
        ensure("saved", undo.isPristine());
        for (S32 i = 0; i < 100; ++i)
        {
            at = type(at, "b", 2.0);
        }
        ensure("changed since", !undo.isPristine());
        for (S32 i = 0; i < 95; ++i)
        {
            undo.undo();
        }
        ensure("five steps in force again, but not the saved five", !undo.isPristine());
        // Saved again with a full stack: another step drops one, and the
        // mark with it keeps counting from the bottom.
        for (S32 i = 0; i < 95; ++i)
        {
            undo.redo();
        }
        undo.markSaved();
        at = type(at, "c", 2.0);
        ensure("changed", !undo.isPristine());
        undo.undo();
        ensure("back to the saved text, the mark having moved down", undo.isPristine());
    }

    template<> template<>
    void altextundo_object::test<9>()
    {
        set_test_name("a save point taken when a text is sent marks that text saved when the answer comes, whatever was typed meanwhile");
        ALTextPos       at    = type(ALTextPos(0, 0), "a");
        at                    = type(at, "b");
        const ALTextUndo::SavePoint sent = undo.savePoint();
        // Typed while the save was on its way, within the run's window:
        // a step of its own all the same.
        at = type(at, "c");
        undo.markSaved(sent);
        ensure("what was typed meanwhile is unsaved", !undo.isPristine());
        undo.undo();
        ensure_equals("the step back is only what came after", doc.text(), std::string("ab"));
        ensure("and that is the saved text", undo.isPristine());
        undo.redo();
        ensure("forward again, unsaved", !undo.isPristine());

        // Undone past before the answer came: the saved text is a step
        // forward.
        const ALTextUndo::SavePoint again = undo.savePoint();
        undo.undo();
        undo.markSaved(again);
        ensure("a step short of the saved text", !undo.isPristine());
        undo.redo();
        ensure("the redo reaches it", undo.isPristine());

        // Undone, then changed: the saved text can no longer be reached.
        const ALTextUndo::SavePoint lost = undo.savePoint();
        undo.undo();
        type(ALTextPos(0, 2), "x", 5.0);
        undo.markSaved(lost);
        ensure("unreachable, so never pristine", !undo.isPristine());
        undo.undo();
        ensure("not even stepping back", !undo.isPristine());

        // A point before any step at all.
        undo.clear();
        doc.setText("");
        const ALTextUndo::SavePoint empty = undo.savePoint();
        type(ALTextPos(0, 0), "z", 5.0);
        undo.markSaved(empty);
        ensure("the empty text was saved, the z is not", !undo.isPristine());
        undo.undo();
        ensure("back to it", undo.isPristine());
    }

    template<> template<>
    void altextundo_object::test<10>()
    {
        set_test_name("a text marked never saved is unsaved at every step the journal can reach, until a save marks it");
        ALTextPos at = type(ALTextPos(0, 0), "a");
        undo.markSaved();
        ensure("saved", undo.isPristine());
        undo.markNeverSaved();
        ensure("never saved: unsaved as it stands", !undo.isPristine());
        at = type(at, "b", 5.0);
        undo.undo();
        ensure("and stepping back does not find a saved text", !undo.isPristine());
        undo.redo();
        undo.markSaved();
        ensure("a save marks it again", undo.isPristine());
    }

    template<> template<>
    void altextundo_object::test<11>()
    {
        set_test_name("a history written out comes back over the same text in another journal, every step back and forward as it was");
        ALTextPos at = type(ALTextPos(0, 0), "one");
        at           = type(at, "\n", 5.0);
        at           = type(at, "two", 5.0);
        undo.markSaved();
        at = type(at, " three", 5.0);
        at = backspace(at, 5.0);
        undo.label("rename");
        undo.undo();
        ensure_equals("where it stands", doc.text(), std::string("one\ntwo three"));
        const LLSD written = undo.asLLSD();

        // Another session: the same text, a journal of its own.
        ALTextDocument later(doc.text());
        ALTextUndo     again(later);
        ensure("put back", again.fromLLSD(written));
        ensure("unsaved, as it was", !again.isPristine());
        ensure("a step forward still to take", again.canRedo());
        again.redo();
        ensure_equals("the step forward", later.text(), std::string("one\ntwo thre"));
        ensure_equals("with its name", again.undoLabel(), std::string("rename"));
        again.undo();
        again.undo();
        ensure_equals("back past the typing", later.text(), std::string("one\ntwo"));
        ensure("to the saved text", again.isPristine());
        again.undo();
        again.undo();
        again.undo();
        ensure_equals("back to the start", later.text(), std::string());
        ensure("and no further", !again.canUndo());
    }

    template<> template<>
    void altextundo_object::test<12>()
    {
        set_test_name("a history is not put over another text; the saved text is found by stepping to it");
        ALTextPos at = type(ALTextPos(0, 0), "alpha");
        undo.markSaved();
        at = type(at, " beta", 5.0);
        const LLSD written = undo.asLLSD();
        const std::optional<std::string> saved = undo.savedText();
        ensure("the saved text is found", saved.has_value() && *saved == "alpha");

        ALTextDocument other("alpha gamma");
        ALTextUndo     wrong(other);
        ensure("refused over a text it was not written with", !wrong.fromLLSD(written));
        ensure("and that journal left as it was", !wrong.canUndo() && !wrong.canRedo());
        ensure_equals("and the text as it was", other.text(), std::string("alpha gamma"));

        // A saved text among the steps forward is found forward.
        undo.undo();
        undo.markSaved();
        undo.redo();
        ensure("saved a step back", undo.savedText() == std::optional<std::string>("alpha"));
        undo.undo();
        ensure("the saved text is the one standing", undo.savedText() == std::optional<std::string>("alpha"));
        undo.markSaved();
        undo.redo();
        undo.undo();
        undo.markNeverSaved();
        ensure("never saved has none", !undo.savedText().has_value());
    }

    template<> template<>
    void altextundo_object::test<13>()
    {
        set_test_name("a history past its budget keeps the newest steps, and the saved mark only where they reach it");
        ALTextPos at = type(ALTextPos(0, 0), std::string(100, 'a').c_str());
        undo.markSaved();
        at = type(at, std::string(100, 'b').c_str(), 5.0);
        at = type(at, std::string(100, 'c').c_str(), 5.0);
        // Each step about a hundred and sixty written: its hundred
        // characters, and its carets, label and edit's places.
        const LLSD kept = undo.asLLSD(400);
        ensure_equals("the two newest steps", kept["undo"].size(), 2);
        ensure_equals("the saved mark at the oldest kept", kept["saved"].asInteger(), 0);
        ALTextDocument later(doc.text());
        ALTextUndo     again(later);
        ensure("put back", again.fromLLSD(kept));
        again.undo();
        again.undo();
        ensure("to the saved text", again.isPristine());
        ensure("and no further", !again.canUndo());
        const LLSD tight = undo.asLLSD(200);
        ensure_equals("only the newest", tight["undo"].size(), 1);
        ensure_equals("the saved text out of reach", tight["saved"].asInteger(), -1);
    }

    template<> template<>
    void altextundo_object::test<14>()
    {
        set_test_name("a run of typing or erasing is written as the one edit it amounts to, and steps back and forward as before");
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
        ensure_equals("where it stands", doc.text(), std::string("onewo\nthr"));

        const LLSD written = undo.asLLSD();
        ensure_equals("five steps", written["undo"].size(), 5);
        ensure_equals("the typing one edit", written["undo"][2]["edits"].size(), 1);
        ensure_equals("with all it typed", written["undo"][2]["edits"][0][5].asString(), std::string("three"));
        ensure_equals("the backspacing one edit", written["undo"][3]["edits"].size(), 1);
        ensure_equals("with all it took, in order", written["undo"][3]["edits"][0][4].asString(), std::string("ee"));
        ensure_equals("the deleting one edit", written["undo"][4]["edits"].size(), 1);
        ensure_equals("with all it took, in order", written["undo"][4]["edits"][0][4].asString(), std::string(" t"));

        ALTextDocument later(doc.text());
        ALTextUndo     again(later);
        ensure("put back", again.fromLLSD(written));
        again.undo();
        ensure_equals("the deletes back", later.text(), std::string("one two\nthr"));
        again.undo();
        ensure_equals("the backspaces back", later.text(), std::string("one two\nthree"));
        again.undo();
        ensure_equals("the typing back", later.text(), std::string("one two\n"));
        again.redo();
        again.redo();
        again.redo();
        ensure_equals("and forward again", later.text(), doc.text());
        LLSD older       = written;
        older["version"] = 1;
        ensure("a history of another version is refused", !again.fromLLSD(older));
    }

    template<> template<>
    void altextundo_object::test<15>()
    {
        set_test_name("a group's edits fold where each carries on from the last, across lines too, and a run of erasing both ways folds as one");
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
        ensure_equals("put in", doc.text(), std::string("ax\nyz\nbc\ndef"));
        // A line break taken, then the letter that moved into its place,
        // then the letter before them: one removal, across a line.
        edit(doc.remove(ALTextRange(ALTextPos(1, 2), ALTextPos(2, 0))));
        edit(doc.remove(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 3))));
        edit(doc.remove(ALTextRange(ALTextPos(1, 1), ALTextPos(1, 2))));
        ensure_equals("taken", doc.text(), std::string("ax\nyc\ndef"));
        // And somewhere else: an edit of its own.
        edit(doc.insert(ALTextPos(2, 3), "!"));
        undo.endGroup();
        const std::string after = doc.text();

        const LLSD written = undo.asLLSD();
        const LLSD edits   = written["undo"][0]["edits"];
        ensure_equals("three edits written for six made", edits.size(), 3);
        ensure_equals("the insertion whole", edits[0][5].asString(), std::string("x\nyz"));
        ensure_equals("the removal whole, in the order it stood", edits[1][4].asString(), std::string("z\nb"));

        ALTextDocument later(after);
        ALTextUndo     again(later);
        ensure("put back", again.fromLLSD(written));
        again.undo();
        ensure_equals("stepped back to where it began", later.text(), before);
        again.redo();
        ensure_equals("and forward to where it ended", later.text(), after);

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
        ensure_equals("one step", run["undo"].size(), 1);
        ensure_equals("one edit", run["undo"][0]["edits"].size(), 1);
        ensure_equals("both letters", run["undo"][0]["edits"][0][4].asString(), std::string("ef"));
        ALTextDocument erased(doc.text());
        ALTextUndo     restored(erased);
        ensure("put back", restored.fromLLSD(run));
        restored.undo();
        ensure_equals("stepped back whole", erased.text(), std::string("abcdefg"));
    }

    template<> template<>
    void altextundo_object::test<16>()
    {
        set_test_name("a step keeps the selection it was made over and the one it left, which settle says, until a step is taken");
        doc.setText("one two three");
        now += 5.0;
        // "two" selected from its end back to its start, and typed over.
        ALTextDocument::Edit over = doc.replace(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)), "2");
        undo.record(over, ALTextRange(ALTextPos(0, 7), ALTextPos(0, 4)), over.endAfter(), now);
        // Where the change put the selection once it was done: the new text
        // selected.
        undo.settle(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 5)));

        std::optional<ALTextRange> back = undo.undo();
        ensure_equals("undone", doc.text(), std::string("one two three"));
        ensure("the selection it was made over, anchor and caret as they were", back && *back == ALTextRange(ALTextPos(0, 7), ALTextPos(0, 4)));
        // Settled after a step back: nothing, since the step settled is not
        // the one last recorded.
        undo.settle(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
        std::optional<ALTextRange> ahead = undo.redo();
        ensure_equals("redone", doc.text(), std::string("one 2 three"));
        ensure("the selection it was left with", ahead && *ahead == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 5)));

        // Through the history written out and read back.
        const LLSD written = undo.asLLSD();
        ALTextDocument later(doc.text());
        ALTextUndo     again(later);
        ensure("put back", again.fromLLSD(written));
        back = again.undo();
        ensure("the selection before, from the history", back && *back == ALTextRange(ALTextPos(0, 7), ALTextPos(0, 4)));
        ahead = again.redo();
        ensure("and after", ahead && *ahead == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 5)));
    }

    template<> template<>
    void altextundo_object::test<17>()
    {
        set_test_name("a save while a group is open -- vim's insert mode -- ends the step there: typed on after is a change, and a step of its own");
        undo.beginGroup();
        ALTextPos at = type(ALTextPos(0, 0), "a");
        at           = type(at, "b");
        undo.markSaved();
        ensure("saved", undo.isPristine());
        at = type(at, "c");
        ensure("typed on: changed", !undo.isPristine());
        undo.endGroup();
        undo.undo();
        ensure_equals("the step after the save undone alone", doc.text(), std::string("ab"));
        ensure("which is the saved text", undo.isPristine());
        undo.undo();
        ensure_equals("then the step before it", doc.text(), std::string());
        ensure("nothing open", !undo.inGroup());
    }

    template<> template<>
    void altextundo_object::test<18>()
    {
        set_test_name("a run is kept as the one edit it amounts to as it joins: undone and redone as one change, however long");
        S32 changes = 0;
        doc.onChanged([&changes](const ALTextDocument::Edit&) { ++changes; });
        ALTextPos at(0, 0);
        for (const char* c : { "h", "e", "l", "l", "o", " ", "w", "o", "r", "l", "d" })
        {
            at = type(at, c);
        }
        at = backspace(at);
        at = backspace(at);
        ensure_equals("typed, and two taken back", doc.text(), std::string("hello wor"));
        changes = 0;
        undo.undo();
        ensure_equals("the erasing, as one change", changes, 1);
        ensure_equals("both back", doc.text(), std::string("hello world"));
        changes = 0;
        undo.undo();
        ensure_equals("the typing, as one change", changes, 1);
        ensure_equals("all of it", doc.text(), std::string());
        changes = 0;
        undo.redo();
        ensure_equals("redone as one change", changes, 1);
        ensure_equals("redone", doc.text(), std::string("hello world"));
    }

    template<> template<>
    void altextundo_object::test<19>()
    {
        set_test_name("the steps back are held by what they weigh, not by how many: hundreds of small ones kept, the oldest of big ones let go, the saved mark with them");
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
        ensure_equals("every one of three hundred kept", steps, 300);
        ensure_equals("back to nothing", doc.text(), std::string());

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
        ensure("two of 400 KB kept", big.canUndo());
        put(5.0);
        ensure_equals("three written", big_doc.text().size(), part.size() * 3);
        ensure_equals("past the budget, the oldest went: the saved text still reached", big.savedText().value_or(std::string()).size(), part.size());
        big.undo();
        big.undo();
        ensure("two back, and no more", !big.canUndo());
        ensure_equals("the first's text stands", big_doc.text().size(), part.size());
        ensure("which is the saved one", big.isPristine());

        const std::string huge(ALTextUndo::BUDGET * 2, 'b');
        ALTextDocument::Edit edit = big_doc.insert(big_doc.end(), huge);
        big.record(edit, big_doc.end(), edit.endAfter(), 9.0);
        ensure("one step past the budget alone is kept, the newest always", big.canUndo());
        big.undo();
        ensure("and it alone", !big.canUndo());
    }

    template<> template<>
    void altextundo_object::test<20>()
    {
        set_test_name("what one key typed does -- a pair with the caret between, a closer typed over, a line broken and indented, an outdent -- is one with the typing run");
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
        ensure_equals("typed", doc.text(), std::string("f(x)\ny}"));
        undo.undo();
        ensure_equals("one step takes all of it back", doc.text(), std::string());
        ensure("nothing before it", !undo.canUndo());
        undo.redo();
        ensure_equals("and puts it back", doc.text(), std::string("f(x)\ny}"));

        // A key typed somewhere else is a step of its own.
        key(ALTextPos(0, 0), "z");
        ensure_equals("typed at the start", doc.text(), std::string("zf(x)\ny}"));
        undo.undo();
        ensure_equals("undone alone", doc.text(), std::string("f(x)\ny}"));
    }

    template<> template<>
    void altextundo_object::test<21>()
    {
        set_test_name("a history is read and checked against its text once, then put back as it was read");
        ALTextPos at = type(ALTextPos(0, 0), "one", 2.0);
        type(at, " two", 2.0);
        const LLSD written = undo.asLLSD();
        ensure("another text's is no history", !ALTextUndo::historyFrom(written, "one six").has_value());
        std::optional<ALTextUndo::History> read = ALTextUndo::historyFrom(written, doc.text());
        ensure("its own text's is", read.has_value());
        ensure_equals("both steps back", read->undo.size(), size_t(2));
        ALTextDocument again(doc.text());
        ALTextUndo     journal{ again };
        journal.restore(std::move(*read));
        journal.undo();
        journal.undo();
        ensure_equals("stepped back through it", again.text(), std::string());
    }

    template<> template<>
    void altextundo_object::test<22>()
    {
        set_test_name("the history written as notation reads back as asLLSD's, the newest step written again as it changes");
        const auto same = [this](const char* what) {
            std::istringstream in(undo.asNotation());
            LLSD               read;
            ensure(std::string(what) + ": reads back", LLSDSerialize::fromNotation(read, in, LLSDSerialize::SIZE_UNLIMITED) > 0);
            std::ostringstream a, b;
            LLSDSerialize::toNotation(read, a);
            LLSDSerialize::toNotation(undo.asLLSD(), b);
            ensure_equals(std::string(what) + ": as asLLSD writes it", a.str(), b.str());
        };
        ALTextPos at = type(ALTextPos(0, 0), "one", 2.0);
        at           = type(at, " two", 2.0);
        undo.label("Typing");
        same("two steps, one named");
        at = type(at, "s");
        same("the newest joined");
        undo.settle(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        same("its selection settled");
        undo.markSaved();
        undo.undo();
        same("one taken back, the saved mark past it");
        std::optional<ALTextUndo::History> read = ALTextUndo::historyFrom(undo.asLLSD(), doc.text());
        ensure("and it is a history of the text", read.has_value());
    }
}
