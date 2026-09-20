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
        std::optional<ALTextPos> caret = undo.undo();
        ensure("undone", caret.has_value());
        ensure_equals("all of it", doc.text(), std::string());
        ensure("caret back at the start", *caret == ALTextPos(0, 0));
        ensure("nothing more to undo", !undo.canUndo());
        caret = undo.redo();
        ensure_equals("redone", doc.text(), std::string("abc"));
        ensure("caret after the run", *caret == ALTextPos(0, 3));
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
}
