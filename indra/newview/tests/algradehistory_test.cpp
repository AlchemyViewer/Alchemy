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

#include <catch2/catch_test_macros.hpp>

#include "../algradehistory.h"

#include "llformat.h"

namespace
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

    // The basic contract: one change, one step, and undo hands back the value
    // it started from.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: one change is one step whose undo hands back where it started", "[ALGradeHistory]")
    {
        CHECK_FALSE(mHistory.canUndo());
        CHECK_FALSE(mHistory.canRedo());

        mHistory.record("RenderColorGradeSaturation", LLSD(1.0), LLSD(1.5), 1.f);
        CHECK(mHistory.canUndo());
        CHECK(mHistory.depth() == (size_t)1);

        const auto* t = mHistory.undo();
        REQUIRE(t != nullptr);
        REQUIRE(t->size() == (size_t)1);
        CHECK(t->front().mName == std::string("RenderColorGradeSaturation"));
        CHECK(t->front().mBefore.asReal() == 1.0);
        CHECK_FALSE(mHistory.canUndo());
        CHECK(mHistory.canRedo());
    }

    // The one everybody notices. A wheel drag emits a commit per mouse-move;
    // without coalescing one drag is a hundred undo steps and Ctrl+Z looks
    // broken. The whole drag must collapse to a single step -- and that step
    // must start from where the drag started, not from its penultimate value.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: a whole drag coalesces into one step from where it began", "[ALGradeHistory]")
    {
        drag("RenderColorGradeLift", 100, 0.01f);

        CHECK(mHistory.depth() == (size_t)1);

        const auto* t = mHistory.undo();
        REQUIRE(t != nullptr);
        REQUIRE(t->size() == (size_t)1);
        UNSCOPED_INFO("from where the drag began");
        CHECK(t->front().mBefore.asReal() == 0.0);
        UNSCOPED_INFO("to where it ended");
        CHECK(t->front().mAfter.asReal() == 100.0);
    }

    // Pausing ends the gesture: two deliberate edits are two steps, however
    // much the same control they touch.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: a pause ends the gesture so two edits of one control are two steps", "[ALGradeHistory]")
    {
        mHistory.record("RenderColorGradeGain", LLSD(1.0), LLSD(1.1), 1.f);
        mHistory.record("RenderColorGradeGain", LLSD(1.1), LLSD(1.2),
                        1.f + ALGradeHistory::COALESCE_SECONDS * 2.f);
        CHECK(mHistory.depth() == (size_t)2);
    }

    // A different control is always a different action, however fast the user
    // moved between them -- otherwise nudging saturation would swallow the
    // contrast change before it.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: a different control is a different step however fast it follows", "[ALGradeHistory]")
    {
        mHistory.record("RenderColorGradeContrast", LLSD(1.0), LLSD(1.2), 1.f);
        mHistory.record("RenderColorGradeSaturation", LLSD(1.0), LLSD(1.2), 1.01f);
        CHECK(mHistory.depth() == (size_t)2);
    }

    // A section's Reset All writes many controls; that is one thing the user
    // did, so it must undo in one go.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: a group's writes to many controls undo as one step", "[ALGradeHistory]")
    {
        mHistory.beginGroup();
        mHistory.record("A", LLSD(1.0), LLSD(0.0), 1.f);
        mHistory.record("B", LLSD(2.0), LLSD(0.0), 1.f);
        mHistory.record("C", LLSD(3.0), LLSD(0.0), 1.f);
        mHistory.endGroup();

        CHECK(mHistory.depth() == (size_t)1);
        const auto* t = mHistory.undo();
        REQUIRE(t != nullptr);
        REQUIRE(t->size() == (size_t)3);
    }

    // Within a group a control may be written more than once. The group should
    // still describe one before and one after for it, or undo would restore an
    // intermediate value.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: a control written twice in a group keeps one before and one after", "[ALGradeHistory]")
    {
        mHistory.beginGroup();
        mHistory.record("A", LLSD(1.0), LLSD(2.0), 1.f);
        mHistory.record("A", LLSD(2.0), LLSD(3.0), 1.f);
        mHistory.endGroup();

        const auto* t = mHistory.undo();
        REQUIRE(t != nullptr);
        REQUIRE(t->size() == (size_t)1);
        CHECK(t->front().mBefore.asReal() == 1.0);
        CHECK(t->front().mAfter.asReal() == 3.0);
    }

    // An edit made after undoing discards the redo tail: that future is no
    // longer reachable.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: an edit after an undo discards the redo tail", "[ALGradeHistory]")
    {
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.record("B", LLSD(0.0), LLSD(1.0), 2.f);
        mHistory.undo();
        CHECK(mHistory.canRedo());

        mHistory.record("C", LLSD(0.0), LLSD(1.0), 3.f);
        CHECK_FALSE(mHistory.canRedo());
        UNSCOPED_INFO("and the stack was truncated");
        CHECK(mHistory.depth() == (size_t)2);
    }

    // Redo walks forward again and hands back the destination values.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: redo walks forward and hands back the destination values", "[ALGradeHistory]")
    {
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.undo();

        const auto* t = mHistory.redo();
        REQUIRE(t != nullptr);
        REQUIRE(t->size() == (size_t)1);
        CHECK(t->front().mAfter.asReal() == 1.0);
        CHECK_FALSE(mHistory.canRedo());
        CHECK(mHistory.canUndo());
    }

    // Applying an undo writes settings, which is what feeds this class. If that
    // fed straight back in it would either loop or corrupt the step being
    // undone, so a write immediately after an undo must start a new step rather
    // than coalesce into the old one.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: a write right after an undo starts a new step", "[ALGradeHistory]")
    {
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.undo();
        mHistory.record("A", LLSD(1.0), LLSD(2.0), 1.01f);

        UNSCOPED_INFO("the undone step was replaced, not extended");
        CHECK(mHistory.depth() == (size_t)1);
        const auto* t = mHistory.undo();
        REQUIRE(t != nullptr);
        REQUIRE(t->size() == (size_t)1);
        UNSCOPED_INFO("carrying the new values");
        CHECK(t->front().mBefore.asReal() == 1.0);
    }

    // A long session must not grow without bound, and it is the oldest history
    // that stops being interesting.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: the stack is capped at MAX_DEPTH by dropping the oldest", "[ALGradeHistory]")
    {
        for (size_t i = 0; i < ALGradeHistory::MAX_DEPTH + 20; ++i)
        {
            mHistory.record("A", LLSD((F64)i), LLSD((F64)(i + 1)), (F32)i * 10.f);
        }
        CHECK(mHistory.depth() == ALGradeHistory::MAX_DEPTH);

        // The newest is still the newest after the drop.
        const auto* t = mHistory.undo();
        REQUIRE(t != nullptr);
        REQUIRE(t->size() == (size_t)1);
        UNSCOPED_INFO("which is the last one recorded");
        CHECK(t->front().mAfter.asReal() == (F64)(ALGradeHistory::MAX_DEPTH + 20));
    }

    // An empty group leaves nothing behind -- a Reset All on a section that was
    // already at defaults should not put a do-nothing step on the stack.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: an empty group leaves no step", "[ALGradeHistory]")
    {
        mHistory.beginGroup();
        mHistory.endGroup();
        CHECK_FALSE(mHistory.canUndo());
        CHECK(mHistory.depth() == (size_t)0);
    }

    // Nested groups are still one step: applying a Look may reset sections on
    // the way through, and the user did one thing.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: nested groups are still one step", "[ALGradeHistory]")
    {
        mHistory.beginGroup();
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.beginGroup();
        mHistory.record("B", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.endGroup();
        mHistory.record("C", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.endGroup();

        CHECK(mHistory.depth() == (size_t)1);
        const auto* t = mHistory.undo();
        REQUIRE(t != nullptr);
        REQUIRE(t->size() == (size_t)3);
    }

    // Undoing past the beginning, or redoing past the end, answers null rather
    // than misbehaving -- the key handler calls these without checking first.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: undo and redo past either end answer null", "[ALGradeHistory]")
    {
        REQUIRE(mHistory.undo() == nullptr);
        REQUIRE(mHistory.redo() == nullptr);

        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        UNSCOPED_INFO("undo works once");
        REQUIRE(mHistory.undo() != nullptr);
        UNSCOPED_INFO("and then stops");
        REQUIRE(mHistory.undo() == nullptr);
        UNSCOPED_INFO("redo works once");
        REQUIRE(mHistory.redo() != nullptr);
        UNSCOPED_INFO("and then stops");
        REQUIRE(mHistory.redo() == nullptr);

        mHistory.clear();
        {
            INFO("clear empties it");
            CHECK_FALSE(mHistory.canUndo());
            CHECK_FALSE(mHistory.canRedo());
        }
        CHECK(mHistory.depth() == (size_t)0);
    }

    // The cap holds for grouped steps too. Groups cannot evict as they push --
    // that would shift the index the group is accumulating into -- so the
    // eviction happens when the group closes, and a session of nothing but
    // Look applies and section resets must stay bounded like any other.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: the cap holds for grouped steps too", "[ALGradeHistory]")
    {
        for (size_t i = 0; i < ALGradeHistory::MAX_DEPTH + 20; ++i)
        {
            mHistory.beginGroup();
            mHistory.record("A", LLSD((F64)i), LLSD((F64)(i + 1)), 1.f);
            mHistory.record("B", LLSD((F64)i), LLSD((F64)(i + 1)), 1.f);
            mHistory.endGroup();
        }
        CHECK(mHistory.depth() == ALGradeHistory::MAX_DEPTH);
        UNSCOPED_INFO("everything on the stack is applied");
        CHECK(mHistory.cursor() == mHistory.depth());

        // The newest is still the newest after the drop, and still whole.
        const auto* t = mHistory.undo();
        REQUIRE(t != nullptr);
        REQUIRE(t->size() == (size_t)2);
        UNSCOPED_INFO("which is the last group recorded");
        CHECK(t->front().mAfter.asReal() == (F64)(ALGradeHistory::MAX_DEPTH + 20));

        // And the walk back stops exactly at the cap, with no phantom steps
        // left over from the evictions.
        size_t undone = 1;
        while (mHistory.undo() != nullptr)
        {
            ++undone;
        }
        UNSCOPED_INFO("the whole stack is walkable");
        CHECK(undone == ALGradeHistory::MAX_DEPTH);
    }

    // A group names the step it makes, for a history that is shown; a step
    // made any other way has no name, and the caller names it from its
    // changes.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: a group names its step and a lone write's step has no name", "[ALGradeHistory]")
    {
        mHistory.beginGroup("Reset Bloom");
        mHistory.record("A", LLSD(1.0), LLSD(0.0), 1.f);
        mHistory.record("B", LLSD(2.0), LLSD(0.0), 1.f);
        mHistory.endGroup();
        mHistory.record("C", LLSD(0.0), LLSD(1.0), 5.f);

        REQUIRE(mHistory.depth() == (size_t)2);
        UNSCOPED_INFO("the group's step carries its name");
        CHECK(mHistory.labelOf(0) == std::string("Reset Bloom"));
        UNSCOPED_INFO("a lone write's step has none");
        CHECK(mHistory.labelOf(1).empty());

        // And a group that follows an unnamed one is not named after it.
        mHistory.beginGroup();
        mHistory.record("D", LLSD(0.0), LLSD(1.0), 9.f);
        mHistory.endGroup();
        REQUIRE(mHistory.depth() == (size_t)3);
        UNSCOPED_INFO("an unnamed group stays unnamed");
        CHECK(mHistory.labelOf(2).empty());
    }

    // Groups nest, and the one the user started is the one the step is named
    // for: applying a Look that resets a section on the way is "Look: X".
    TEST_CASE_METHOD(history_data, "ALGradeHistory: nested groups name their step for the outermost", "[ALGradeHistory]")
    {
        mHistory.beginGroup("Look: Soft Film");
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.beginGroup("Reset Basic");
        mHistory.record("B", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.endGroup();
        mHistory.endGroup();

        REQUIRE(mHistory.depth() == (size_t)1);
        CHECK(mHistory.labelOf(0) == std::string("Look: Soft Film"));
    }

    // A name belongs to its step through everything that moves steps: the
    // oldest dropping off the front at the cap, and the redo tail being cut.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: a name stays with its step through eviction and a cut redo tail", "[ALGradeHistory]")
    {
        const size_t made = ALGradeHistory::MAX_DEPTH + 5;
        for (size_t i = 0; i < made; ++i)
        {
            mHistory.beginGroup(llformat("step %d", (S32)i));
            mHistory.record("A", LLSD((F64)i), LLSD((F64)(i + 1)), 1.f);
            mHistory.endGroup();
        }
        REQUIRE(mHistory.depth() == ALGradeHistory::MAX_DEPTH);
        UNSCOPED_INFO("the oldest kept is the sixth made");
        CHECK(mHistory.labelOf(0) == std::string("step 5"));
        UNSCOPED_INFO("the newest is the last made");
        CHECK(mHistory.labelOf(mHistory.depth() - 1) ==
                      llformat("step %d", (S32)(made - 1)));

        mHistory.clear();
        for (const char* name : { "one", "two", "three" })
        {
            mHistory.beginGroup(name);
            mHistory.record(name, LLSD(0.0), LLSD(1.0), 1.f);
            mHistory.endGroup();
        }
        mHistory.undo();
        mHistory.undo();
        mHistory.beginGroup("four");
        mHistory.record("four", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.endGroup();
        UNSCOPED_INFO("the undone two were cut");
        REQUIRE(mHistory.depth() == (size_t)2);
        UNSCOPED_INFO("the first kept its name");
        CHECK(mHistory.labelOf(0) == std::string("one"));
        UNSCOPED_INFO("and the new one has its own");
        CHECK(mHistory.labelOf(1) == std::string("four"));
    }

    // A control a group moved and then put back is not part of what the
    // group did, and a group made of nothing else is not a step at all.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: a control a group moved and put back is not part of its step", "[ALGradeHistory]")
    {
        mHistory.beginGroup("There and back");
        mHistory.record("A", LLSD(1.0), LLSD(2.0), 1.f);
        mHistory.record("A", LLSD(2.0), LLSD(1.0), 1.f);
        mHistory.endGroup();
        CHECK_FALSE(mHistory.canUndo());
        CHECK(mHistory.depth() == (size_t)0);

        mHistory.beginGroup();
        mHistory.record("A", LLSD(1.0), LLSD(2.0), 1.f);
        mHistory.record("B", LLSD(0.0), LLSD(5.0), 1.f);
        mHistory.record("A", LLSD(2.0), LLSD(1.0), 1.f);
        mHistory.endGroup();
        CHECK(mHistory.depth() == (size_t)1);
        const auto* t = mHistory.undo();
        REQUIRE(t != nullptr);
        UNSCOPED_INFO("holding only what changed");
        REQUIRE(t->size() == (size_t)1);
        CHECK(t->front().mName == std::string("B"));
    }

    // A drag that comes back to where it started did nothing, so it leaves no
    // step -- and if it carries on, it is a new step from that same value.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: a drag back to where it started leaves no step", "[ALGradeHistory]")
    {
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.record("A", LLSD(1.0), LLSD(0.0), 1.1f);
        CHECK(mHistory.depth() == (size_t)0);
        CHECK_FALSE(mHistory.canUndo());

        mHistory.record("A", LLSD(0.0), LLSD(2.0), 1.2f);
        UNSCOPED_INFO("carrying on is a step");
        CHECK(mHistory.depth() == (size_t)1);
        const auto* t = mHistory.undo();
        REQUIRE(t != nullptr);
        REQUIRE(t->size() == (size_t)1);
        UNSCOPED_INFO("from the value the drag began at");
        CHECK(t->front().mBefore.asReal() == 0.0);
        CHECK(t->front().mAfter.asReal() == 2.0);

        // A drag that returns to its start does not take an earlier step with
        // it: only its own is dropped.
        mHistory.clear();
        mHistory.record("B", LLSD(0.0), LLSD(3.0), 1.f);
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 5.f);
        mHistory.record("A", LLSD(1.0), LLSD(0.0), 5.1f);
        UNSCOPED_INFO("the earlier step stays");
        REQUIRE(mHistory.depth() == (size_t)1);
        REQUIRE(mHistory.at(0).size() == (size_t)1);
        CHECK(mHistory.at(0).front().mName == std::string("B"));
    }

    // Writing the value already there is not a change, so it must not cost the
    // user their redo tail.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: writing the value already there keeps the redo tail", "[ALGradeHistory]")
    {
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.record("B", LLSD(0.0), LLSD(1.0), 2.f);
        mHistory.undo();
        CHECK(mHistory.canRedo());

        mHistory.record("C", LLSD(5.0), LLSD(5.0), 3.f);
        CHECK(mHistory.canRedo());
        UNSCOPED_INFO("and nothing added");
        CHECK(mHistory.depth() == (size_t)2);
    }

    // The revision moves with the stack and the cursor and at no other time,
    // which is what lets a view of the history ask one number whether it is
    // out of date.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: the revision moves with the stack and the cursor and at no other time", "[ALGradeHistory]")
    {
        const U32 start = mHistory.revision();
        {
            INFO("reading an empty stack moves nothing");
            CHECK_FALSE(mHistory.canUndo());
            CHECK(mHistory.revision() == start);
        }

        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        const U32 recorded = mHistory.revision();
        UNSCOPED_INFO("a write moves it");
        CHECK(recorded != start);

        REQUIRE(mHistory.depth() == (size_t)1);
        mHistory.cursor();
        mHistory.at(0);
        mHistory.labelOf(0);
        UNSCOPED_INFO("reads do not");
        CHECK(mHistory.revision() == recorded);

        mHistory.record("A", LLSD(1.0), LLSD(1.0), 1.1f);
        UNSCOPED_INFO("nor does a write of the value already there");
        CHECK(mHistory.revision() == recorded);

        mHistory.undo();
        const U32 undone = mHistory.revision();
        UNSCOPED_INFO("an undo moves it");
        CHECK(undone != recorded);
        mHistory.redo();
        UNSCOPED_INFO("a redo moves it");
        CHECK(mHistory.revision() != undone);

        const U32 before_clear = mHistory.revision();
        mHistory.clear();
        UNSCOPED_INFO("a clear moves it");
        CHECK(mHistory.revision() != before_clear);
    }

    // What the stack shows is what undo will do.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: what the stack shows is what undo will do", "[ALGradeHistory]")
    {
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        mHistory.record("B", LLSD(2.0), LLSD(3.0), 5.f);

        REQUIRE(mHistory.depth() == (size_t)2);
        const ALGradeHistory::Transaction shown = mHistory.at(1);
        REQUIRE(shown.size() == (size_t)1);
        const auto* undone = mHistory.undo();
        REQUIRE(undone != nullptr);
        REQUIRE(undone->size() == shown.size());
        CHECK(undone->front().mName == shown.front().mName);
        CHECK(undone->front().mBefore.asReal() == shown.front().mBefore.asReal());
        CHECK(undone->front().mAfter.asReal() == shown.front().mAfter.asReal());
        UNSCOPED_INFO("and the cursor says it is no longer in force");
        CHECK(mHistory.cursor() == (size_t)1);
    }

    // The stack shown oldest first through its steps forward too, however
    // many; and a group closing is a change to what it shows, since it
    // trims its step, where one closing inside it is not.
    TEST_CASE_METHOD(history_data, "ALGradeHistory: the stack lists redo steps and only the outermost close is a change", "[ALGradeHistory]")
    {
        for (const char* name : { "one", "two", "three" })
        {
            mHistory.beginGroup(name);
            mHistory.record(name, LLSD(0.0), LLSD(1.0), 1.f);
            mHistory.endGroup();
        }
        mHistory.undo();
        mHistory.undo();
        REQUIRE(mHistory.depth() == (size_t)3);
        UNSCOPED_INFO("in force");
        CHECK(mHistory.labelOf(0) == std::string("one"));
        UNSCOPED_INFO("the next redo");
        CHECK(mHistory.labelOf(1) == std::string("two"));
        UNSCOPED_INFO("the last redo");
        CHECK(mHistory.labelOf(2) == std::string("three"));
        REQUIRE(mHistory.at(2).size() == (size_t)1);
        UNSCOPED_INFO("a redo step's changes");
        CHECK(mHistory.at(2).front().mName == std::string("three"));

        mHistory.clear();
        mHistory.beginGroup("outer");
        mHistory.beginGroup("inner");
        mHistory.record("A", LLSD(0.0), LLSD(1.0), 1.f);
        const U32 recorded = mHistory.revision();
        mHistory.endGroup();
        UNSCOPED_INFO("an inner group closing is not a change");
        CHECK(mHistory.revision() == recorded);
        mHistory.endGroup();
        UNSCOPED_INFO("the outermost closing is");
        CHECK(mHistory.revision() != recorded);
        const U32 closed = mHistory.revision();
        mHistory.endGroup();
        UNSCOPED_INFO("nor is an end with no group open");
        CHECK(mHistory.revision() == closed);
    }
}
