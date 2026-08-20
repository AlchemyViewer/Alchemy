/**
 * @file algradehistory_test.cpp
 * @brief Unit tests for the Lightbox undo stack
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 * The behaviour that matters is what counts as "one thing the user did": a
 * whole drag, a whole section reset. Get that wrong and Ctrl+Z looks broken
 * whichever way it errs.
 */

#include "linden_common.h"

#include "../test/lltut.h"

#include "../algradehistory.h"

namespace tut
{
    struct history_data
    {
        ALGradeHistory mHistory;

        /// Simulate a drag: `steps` commits to one control, `dt` apart.
        void drag(const std::string& name, S32 steps, F32 dt, F32 start_time = 1.f)
        {
            for (S32 i = 0; i < steps; ++i)
            {
                mHistory.record(name, LLSD((F32)i), LLSD((F32)(i + 1)), start_time + dt * (F32)i);
            }
        }
    };

    typedef test_group<history_data> history_group;
    typedef history_group::object    history_object;
    tut::history_group hg("ALGradeHistory");

    // The basic contract: one change, one step, and undo hands back the value
    // it started from.
    template<> template<>
    void history_object::test<1>()
    {
        ensure("nothing to undo yet", !mHistory.canUndo());
        ensure("nothing to redo yet", !mHistory.canRedo());

        mHistory.record("RenderColorGradeSaturation", LLSD(1.0), LLSD(1.5), 1.f);
        ensure("something to undo", mHistory.canUndo());
        ensure_equals("one step", mHistory.depth(), (size_t)1);

        const auto* t = mHistory.undo();
        ensure("undo returned a transaction", t != nullptr);
        ensure_equals("covering one control", t->size(), (size_t)1);
        ensure_equals("the right control", t->front().mName, std::string("RenderColorGradeSaturation"));
        ensure_equals("restoring the original", t->front().mBefore.asReal(), 1.0);
        ensure("nothing left to undo", !mHistory.canUndo());
        ensure("but something to redo", mHistory.canRedo());
    }

    // The one everybody notices. A wheel drag emits a commit per mouse-move;
    // without coalescing one drag is a hundred undo steps and Ctrl+Z looks
    // broken. The whole drag must collapse to a single step -- and that step
    // must start from where the drag started, not from its penultimate value.
    template<> template<>
    void history_object::test<2>()
    {
        drag("RenderColorGradeLift", 100, 0.01f);

        ensure_equals("a whole drag is one step", mHistory.depth(), (size_t)1);

        const auto* t = mHistory.undo();
        ensure("got the step", t != nullptr);
        ensure_equals("from where the drag began", t->front().mBefore.asReal(), 0.0);
        ensure_equals("to where it ended", t->front().mAfter.asReal(), 100.0);
    }

    // Pausing ends the gesture: two deliberate edits are two steps, however
    // much the same control they touch.
    template<> template<>
    void history_object::test<3>()
    {
        mHistory.record("RenderColorGradeGain", LLSD(1.0), LLSD(1.1), 1.f);
        mHistory.record("RenderColorGradeGain", LLSD(1.1), LLSD(1.2),
                        1.f + ALGradeHistory::COALESCE_SECONDS * 2.f);
        ensure_equals("two separate edits", mHistory.depth(), (size_t)2);
    }

    // A different control is always a different action, however fast the user
    // moved between them -- otherwise nudging saturation would swallow the
    // contrast change before it.
    template<> template<>
    void history_object::test<4>()
    {
        mHistory.record("RenderColorGradeContrast", LLSD(1.0), LLSD(1.2), 1.f);
        mHistory.record("RenderColorGradeSaturation", LLSD(1.0), LLSD(1.2), 1.01f);
        ensure_equals("two steps", mHistory.depth(), (size_t)2);
    }

    // A section's Reset All writes many controls; that is one thing the user
    // did, so it must undo in one go.
    template<> template<>
    void history_object::test<5>()
    {
        mHistory.beginGroup();
        mHistory.record("A", LLSD(1.0), LLSD(0.0), 1.f);
        mHistory.record("B", LLSD(2.0), LLSD(0.0), 1.f);
        mHistory.record("C", LLSD(3.0), LLSD(0.0), 1.f);
        mHistory.endGroup();

        ensure_equals("one step", mHistory.depth(), (size_t)1);
        const auto* t = mHistory.undo();
        ensure("got it", t != nullptr);
        ensure_equals("covering all three", t->size(), (size_t)3);
    }

    // Within a group a control may be written more than once. The group should
    // still describe one before and one after for it, or undo would restore an
    // intermediate value.
    template<> template<>
    void history_object::test<6>()
    {
        mHistory.beginGroup();
        mHistory.record("A", LLSD(1.0), LLSD(2.0), 1.f);
        mHistory.record("A", LLSD(2.0), LLSD(3.0), 1.f);
        mHistory.endGroup();

        const auto* t = mHistory.undo();
        ensure("got it", t != nullptr);
        ensure_equals("still one control", t->size(), (size_t)1);
        ensure_equals("from the original value", t->front().mBefore.asReal(), 1.0);
        ensure_equals("to the final one", t->front().mAfter.asReal(), 3.0);
    }

    // An edit made after undoing discards the redo tail: that future is no
    // longer reachable.
    template<> template<>
    void history_object::test<7>()
    {
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.record("B", LLSD(0.0), LLSD(1.0), 2.f);
        mHistory.undo();
        ensure("can redo before the new edit", mHistory.canRedo());

        mHistory.record("C", LLSD(0.0), LLSD(1.0), 3.f);
        ensure("redo tail is gone", !mHistory.canRedo());
        ensure_equals("and the stack was truncated", mHistory.depth(), (size_t)2);
    }

    // Redo walks forward again and hands back the destination values.
    template<> template<>
    void history_object::test<8>()
    {
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.undo();

        const auto* t = mHistory.redo();
        ensure("redo returned the step", t != nullptr);
        ensure_equals("with the destination", t->front().mAfter.asReal(), 1.0);
        ensure("nothing further to redo", !mHistory.canRedo());
        ensure("and it is undoable again", mHistory.canUndo());
    }

    // Applying an undo writes settings, which is what feeds this class. If that
    // fed straight back in it would either loop or corrupt the step being
    // undone, so a write immediately after an undo must start a new step rather
    // than coalesce into the old one.
    template<> template<>
    void history_object::test<9>()
    {
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.undo();
        mHistory.record("A", LLSD(1.0), LLSD(2.0), 1.01f);

        ensure_equals("the undone step was replaced, not extended", mHistory.depth(), (size_t)1);
        const auto* t = mHistory.undo();
        ensure("got the new step", t != nullptr);
        ensure_equals("carrying the new values", t->front().mBefore.asReal(), 1.0);
    }

    // A long session must not grow without bound, and it is the oldest history
    // that stops being interesting.
    template<> template<>
    void history_object::test<10>()
    {
        for (size_t i = 0; i < ALGradeHistory::MAX_DEPTH + 20; ++i)
        {
            mHistory.record("A", LLSD((F64)i), LLSD((F64)(i + 1)), (F32)i * 10.f);
        }
        ensure_equals("capped", mHistory.depth(), ALGradeHistory::MAX_DEPTH);

        // The newest is still the newest after the drop.
        const auto* t = mHistory.undo();
        ensure("got the newest", t != nullptr);
        ensure_equals("which is the last one recorded",
                      t->front().mAfter.asReal(), (F64)(ALGradeHistory::MAX_DEPTH + 20));
    }

    // An empty group leaves nothing behind -- a Reset All on a section that was
    // already at defaults should not put a do-nothing step on the stack.
    template<> template<>
    void history_object::test<11>()
    {
        mHistory.beginGroup();
        mHistory.endGroup();
        ensure("no step", !mHistory.canUndo());
        ensure_equals("nothing recorded", mHistory.depth(), (size_t)0);
    }

    // Nested groups are still one step: applying a Look may reset sections on
    // the way through, and the user did one thing.
    template<> template<>
    void history_object::test<12>()
    {
        mHistory.beginGroup();
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.beginGroup();
        mHistory.record("B", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.endGroup();
        mHistory.record("C", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.endGroup();

        ensure_equals("one step", mHistory.depth(), (size_t)1);
        const auto* t = mHistory.undo();
        ensure("got it", t != nullptr);
        ensure_equals("covering all three", t->size(), (size_t)3);
    }

    // Undoing past the beginning, or redoing past the end, answers null rather
    // than misbehaving -- the key handler calls these without checking first.
    template<> template<>
    void history_object::test<13>()
    {
        ensure("undo on empty is null", mHistory.undo() == nullptr);
        ensure("redo on empty is null", mHistory.redo() == nullptr);

        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        ensure("undo works once", mHistory.undo() != nullptr);
        ensure("and then stops", mHistory.undo() == nullptr);
        ensure("redo works once", mHistory.redo() != nullptr);
        ensure("and then stops", mHistory.redo() == nullptr);

        mHistory.clear();
        ensure("clear empties it", !mHistory.canUndo() && !mHistory.canRedo());
        ensure_equals("really empty", mHistory.depth(), (size_t)0);
    }

    // The cap holds for grouped steps too. Groups cannot evict as they push --
    // that would shift the index the group is accumulating into -- so the
    // eviction happens when the group closes, and a session of nothing but
    // Look applies and section resets must stay bounded like any other.
    template<> template<>
    void history_object::test<14>()
    {
        for (size_t i = 0; i < ALGradeHistory::MAX_DEPTH + 20; ++i)
        {
            mHistory.beginGroup();
            mHistory.record("A", LLSD((F64)i), LLSD((F64)(i + 1)), 1.f);
            mHistory.record("B", LLSD((F64)i), LLSD((F64)(i + 1)), 1.f);
            mHistory.endGroup();
        }
        ensure_equals("capped", mHistory.depth(), ALGradeHistory::MAX_DEPTH);
        ensure_equals("everything on the stack is applied", mHistory.cursor(), mHistory.depth());

        // The newest is still the newest after the drop, and still whole.
        const auto* t = mHistory.undo();
        ensure("got the newest", t != nullptr);
        ensure_equals("still covering both controls", t->size(), (size_t)2);
        ensure_equals("which is the last group recorded",
                      t->front().mAfter.asReal(), (F64)(ALGradeHistory::MAX_DEPTH + 20));

        // And the walk back stops exactly at the cap, with no phantom steps
        // left over from the evictions.
        size_t undone = 1;
        while (mHistory.undo() != nullptr)
        {
            ++undone;
        }
        ensure_equals("the whole stack is walkable", undone, ALGradeHistory::MAX_DEPTH);
    }
}
