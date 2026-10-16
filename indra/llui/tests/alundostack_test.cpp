/**
 * @file alundostack_test.cpp
 * @brief The steps back and forward: joined runs, labels, depth
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

#include "alundostack.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace
{
    // A step here is the names a change touched.
    struct Step
    {
        std::vector<std::string> mNames;
        std::string mLabel;
    };

    struct alundostack_data
    {
        static void join(Step& last, Step&& step)
        {
            for (auto& name : step.mNames)
            {
                if (std::find(last.mNames.begin(), last.mNames.end(), name) == last.mNames.end())
                {
                    last.mNames.push_back(std::move(name));
                }
            }
        }

        // The step back taken and made the step forward, as an undo does.
        static void undoOne(ALUndoStack<Step>& stack)
        {
            std::optional<Step> taken = stack.takeUndo();
            REQUIRE(taken);
            stack.pushRedo(std::move(*taken));
        }
    };

    // A change noted is a step back; taking it makes it a step forward
    // once the caller says so; a new change forgets the steps forward.
    TEST_CASE_METHOD(alundostack_data, "alundostack: a step taken back is a step forward once pushed; a new change forgets it", "[alundostack]")
    {
        ALUndoStack<Step> stack;
        CHECK_FALSE(stack.canUndo());

        stack.note({ { "a" } }, "", 0.0, 1.0, join);
        stack.note({ { "b" } }, "", 5.0, 1.0, join);
        UNSCOPED_INFO("two steps back");
        CHECK(stack.inForce() == size_t(2));

        std::optional<Step> back = stack.takeUndo();
        {
            INFO("one taken");
            REQUIRE(back.has_value());
            CHECK(back->mNames.front() == "b");
        }
        stack.pushRedo(*back);
        CHECK(stack.canRedo());
        CHECK(stack.inForce() == size_t(1));

        stack.note({ { "c" } }, "", 10.0, 1.0, join);
        UNSCOPED_INFO("a new change forgets the way forward");
        CHECK_FALSE(stack.canRedo());
        CHECK(stack.inForce() == size_t(2));
    }

    // A run of changes to one thing within the window is one step, grown
    // to cover what each change touched; another thing, or a pause, is a
    // new step.
    TEST_CASE_METHOD(alundostack_data, "alundostack: a run of changes to one key within the window joins into one step", "[alundostack]")
    {
        ALUndoStack<Step> stack;
        stack.note({ { "slider" } }, "shape:fill", 0.0, 1.0, join);
        stack.note({ { "slider" } }, "shape:fill", 0.3, 1.0, join);
        stack.note({ { "slider", "shadow" } }, "shape:fill", 0.6, 1.0, join);
        UNSCOPED_INFO("one step for the run");
        REQUIRE(stack.inForce() == size_t(1));
        UNSCOPED_INFO("grown to cover what the run touched");
        CHECK(stack.undone().front().mNames.size() == size_t(2));

        stack.note({ { "slider" } }, "shape:fill", 2.0, 1.0, join);
        UNSCOPED_INFO("a pause starts another");
        CHECK(stack.inForce() == size_t(2));
        stack.note({ { "other" } }, "shape:radius", 2.1, 1.0, join);
        UNSCOPED_INFO("and so does another thing");
        CHECK(stack.inForce() == size_t(3));
        stack.note({ { "nothing" } }, "", 2.2, 1.0, join);
        stack.note({ { "nothing" } }, "", 2.3, 1.0, join);
        UNSCOPED_INFO("no key never joins");
        CHECK(stack.inForce() == size_t(5));
    }

    // The first thing said after a change names it; the next thing said
    // is about something else, and a frame later so is anything.
    TEST_CASE_METHOD(alundostack_data, "alundostack: the first label after a change names its step; later ones do not", "[alundostack]")
    {
        ALUndoStack<Step> stack;
        stack.label("said before anything");
        stack.note({ { "a" } }, "", 0.0, 1.0, join);
        stack.label("Set the fill");
        stack.label("Something else");
        UNSCOPED_INFO("the first word after the change names it");
        CHECK(stack.undoLabel() == std::string("Set the fill"));

        stack.note({ { "b" } }, "", 5.0, 1.0, join);
        stack.closeLabel();
        stack.label("Too late");
        UNSCOPED_INFO("a frame later names nothing");
        CHECK(stack.undoLabel() == std::string());

        undoOne(stack);
        UNSCOPED_INFO("the step forward keeps its name");
        CHECK(stack.redoLabel() == std::string());
        undoOne(stack);
        UNSCOPED_INFO("and so does the next");
        CHECK(stack.redoLabel() == std::string("Set the fill"));
    }

    // Past the depth the oldest step is forgotten.
    TEST_CASE_METHOD(alundostack_data, "alundostack: past the depth the oldest step is forgotten; clear empties both stacks", "[alundostack]")
    {
        ALUndoStack<Step> stack(3);
        for (int i = 0; i < 5; ++i)
        {
            stack.note({ { std::to_string(i) } }, "", i * 10.0, 1.0, join);
        }
        REQUIRE(stack.inForce() == size_t(3));
        UNSCOPED_INFO("the oldest gone");
        CHECK(stack.undone().front().mNames.front() == std::string("2"));
        stack.clear();
        {
            INFO("cleared");
            CHECK_FALSE(stack.canUndo());
            CHECK_FALSE(stack.canRedo());
        }
    }
    TEST_CASE_METHOD(alundostack_data, "alundostack: a change after an undo starts a new step and leaves the last one alone", "[alundostack]")
    {
        ALUndoStack<Step> stack;
        stack.note({ { "a" } }, "slider", 0.0, 1.0, join);
        stack.note({ { "b" } }, "other", 0.1, 1.0, join);
        undoOne(stack);
        stack.note({ { "c" } }, "other", 0.2, 1.0, join);
        UNSCOPED_INFO("an edit after undo starts a new step");
        REQUIRE(stack.inForce() == size_t(2));
        UNSCOPED_INFO("the earlier step is unchanged");
        CHECK(stack.undone().front().mNames.size() == size_t(1));
    }

    TEST_CASE_METHOD(alundostack_data, "alundostack: restore drops and counts what is past the depth and keeps the redo order", "[alundostack]")
    {
        ALUndoStack<Step> stack(3);
        std::vector<Step> undo{ { { "1" } }, { { "2" } }, { { "3" } }, { { "4" } }, { { "5" } } };
        std::vector<Step> redo{ { { "z" } }, { { "y" } } };
        UNSCOPED_INFO("two forgotten");
        CHECK(stack.restore(undo, redo) == size_t(2));
        REQUIRE(stack.inForce() == size_t(3));
        UNSCOPED_INFO("from the third");
        CHECK(stack.undone().front().mNames.front() == std::string("3"));
        std::optional<Step> back = stack.takeUndo();
        {
            INFO("the newest taken back first");
            REQUIRE(back);
            CHECK(back->mNames.front() == "5");
        }
        std::optional<Step> forward = stack.takeRedo();
        {
            INFO("the last of the steps forward taken first");
            REQUIRE(forward);
            CHECK(forward->mNames.front() == "y");
        }
        stack.note({ { "n" } }, "slider", 0.0, 1.0, join);
        {
            INFO("a change after is a step of its own, and forgets the steps forward");
            CHECK(stack.inForce() == 3);
            CHECK_FALSE(stack.canRedo());
        }
    }

    TEST_CASE_METHOD(alundostack_data, "alundostack: a group is one step named by its outermost label; nested ones join it", "[alundostack]")
    {
        ALUndoStack<Step> stack;
        stack.note({ { "before" } }, "slider", 0.0, 1.0, join);
        stack.beginGroup("Reset Bloom");
        CHECK(stack.inGroup());
        stack.note({ { "a" } }, "slider", 0.1, 1.0, join);
        stack.beginGroup("inner");
        stack.note({ { "b" } }, "other", 100.0, 1.0, join);
        stack.endGroup();
        UNSCOPED_INFO("still open");
        CHECK(stack.inGroup());
        stack.note({ { "c" } }, "", 500.0, 1.0, join);
        stack.endGroup();
        CHECK_FALSE(stack.inGroup());
        UNSCOPED_INFO("one step, not joined to the run before it");
        REQUIRE(stack.inForce() == size_t(2));
        UNSCOPED_INFO("all of it");
        CHECK(stack.undone().back().mNames.size() == size_t(3));
        UNSCOPED_INFO("the outermost's name");
        CHECK(stack.undoLabel() == std::string("Reset Bloom"));
        stack.label("typed");
        UNSCOPED_INFO("which a label after does not take");
        CHECK(stack.undoLabel() == std::string("Reset Bloom"));
        stack.note({ { "a" } }, "slider", 500.1, 1.0, join);
        UNSCOPED_INFO("a change after is its own step");
        CHECK(stack.inForce() == size_t(3));

        stack.beginGroup("empty");
        stack.endGroup();
        UNSCOPED_INFO("a group of nothing leaves nothing");
        CHECK(stack.inForce() == size_t(3));
        stack.beginGroup();
        stack.note({ { "d" } }, "", 0.0, 1.0, join);
        stack.endGroup();
        stack.label("named after");
        UNSCOPED_INFO("one with no name named by the first label after");
        CHECK(stack.undoLabel() == std::string("named after"));
        {
            INFO("unmatched ends ignored");
            CHECK_FALSE(stack.endGroup());
            CHECK_FALSE(stack.inGroup());
        }
        stack.beginGroup("outer");
        stack.beginGroup("inner");
        stack.note({ { "e" } }, "", 0.0, 1.0, join);
        stack.endGroup();
        stack.endGroup();
        UNSCOPED_INFO("the outermost's name though the inner opened first on a change");
        CHECK(stack.undoLabel() == std::string("outer"));
    }

    TEST_CASE_METHOD(alundostack_data, "alundostack: a run or group that undid itself is dropped and its lost redo stays lost", "[alundostack]")
    {
        ALUndoStack<Step> stack;
        const auto back_to_start = [](const Step& step) { return step.mNames.size() >= 2 && step.mNames.back() == "start"; };
        stack.note({ { "one" } }, "", 0.0, 1.0, join);
        stack.note({ { "two" } }, "", 5.0, 1.0, join);
        undoOne(stack);
        stack.note({ { "drag" } }, "slider", 10.0, 1.0, join, back_to_start);
        UNSCOPED_INFO("a drag is a step");
        CHECK(stack.inForce() == size_t(2));
        stack.note({ { "start" } }, "slider", 10.2, 1.0, join, back_to_start);
        UNSCOPED_INFO("let go where it began: gone");
        CHECK(stack.inForce() == size_t(1));
        UNSCOPED_INFO("the way forward still gone");
        CHECK_FALSE(stack.canRedo());
        stack.note({ { "drag" } }, "slider", 10.3, 1.0, join, back_to_start);
        {
            INFO("a change after it is a step of its own");
            REQUIRE(stack.inForce() == 2);
            CHECK(stack.undone().back().mNames.size() == 1);
        }

        const auto trim = [](Step& step) {
            step.mNames.erase(std::remove(step.mNames.begin(), step.mNames.end(), "undone"), step.mNames.end());
            return step.mNames.empty();
        };
        stack.beginGroup("Look");
        stack.note({ { "undone" } }, "", 20.0, 1.0, join);
        stack.note({ { "kept" } }, "", 20.0, 1.0, join);
        stack.endGroup(trim);
        {
            INFO("trimmed, kept");
            REQUIRE(stack.inForce() == 3);
            CHECK(stack.undone().back().mNames == std::vector<std::string>{ "kept" });
        }
        stack.beginGroup("Look");
        stack.note({ { "undone" } }, "", 30.0, 1.0, join);
        stack.endGroup(trim);
        UNSCOPED_INFO("nothing left: dropped");
        CHECK(stack.inForce() == size_t(3));
        UNSCOPED_INFO("and the label with it");
        CHECK(stack.undoLabel() == std::string("Look"));
    }

    TEST_CASE_METHOD(alundostack_data, "alundostack: the depth applies when a group closes; closeGroups and clear close all", "[alundostack]")
    {
        ALUndoStack<Step> stack(2);
        stack.note({ { "1" } }, "", 0.0, 1.0, join);
        stack.note({ { "2" } }, "", 5.0, 1.0, join);
        stack.beginGroup();
        {
            INFO("not while open");
            CHECK_FALSE(stack.note({ { "3" } }, "", 10.0, 1.0, join));
            CHECK(stack.inForce() == 3);
        }
        {
            INFO("when it closes");
            CHECK(stack.endGroup());
            REQUIRE(stack.inForce() == 2);
            CHECK(stack.undone().front().mNames.front() == "2");
        }
        stack.beginGroup();
        stack.note({ { "4" } }, "", 20.0, 1.0, join);
        {
            INFO("an empty group closing forgets nothing");
            CHECK_FALSE(stack.endGroup([](Step&) { return true; }));
            CHECK(stack.inForce() == 2);
        }

        stack.beginGroup();
        stack.beginGroup();
        stack.note({ { "5" } }, "", 30.0, 1.0, join);
        {
            INFO("closed at once");
            CHECK(stack.closeGroups());
            CHECK_FALSE(stack.inGroup());
            CHECK(stack.inForce() == 2);
        }
        UNSCOPED_INFO("none open: nothing");
        CHECK_FALSE(stack.closeGroups());
        stack.beginGroup();
        stack.note({ { "6" } }, "", 40.0, 1.0, join);
        undoOne(stack);
        stack.note({ { "7" } }, "", 40.0, 1.0, join);
        REQUIRE(stack.canUndo());
        UNSCOPED_INFO("taken back while open: the next change a step of its own");
        CHECK(stack.undone().back().mNames == std::vector<std::string>{ "7" });
        stack.clear();
        UNSCOPED_INFO("cleared, closed");
        CHECK_FALSE(stack.inGroup());
        stack.note({ { "8" } }, "", 50.0, 1.0, join);
        stack.note({ { "9" } }, "", 60.0, 1.0, join);
        UNSCOPED_INFO("and its own steps again");
        CHECK(stack.inForce() == size_t(2));
    }

    TEST_CASE_METHOD(alundostack_data, "alundostack: breakRun inside a group splits it into steps each named by the group", "[alundostack]")
    {
        ALUndoStack<Step> stack(1);
        stack.note({ { "0" } }, "", 0.0, 1.0, join);
        stack.beginGroup("Insert");
        stack.note({ { "a" } }, "", 1.0, 1.0, join);
        stack.note({ { "b" } }, "", 1.0, 1.0, join);
        stack.breakRun();
        stack.note({ { "c" } }, "", 1.0, 1.0, join);
        stack.note({ { "d" } }, "", 1.0, 1.0, join);
        UNSCOPED_INFO("still open");
        CHECK(stack.inGroup());
        UNSCOPED_INFO("nothing forgotten while it is");
        REQUIRE(stack.inForce() == size_t(3));
        {
            INFO("each whole");
            CHECK(stack.undone()[1].mNames == std::vector<std::string>{ "a", "b" });
            CHECK(stack.undone()[2].mNames == std::vector<std::string>{ "c", "d" });
        }
        {
            INFO("each named by the group");
            CHECK(stack.undone()[1].mLabel == "Insert");
            CHECK(stack.undone()[2].mLabel == "Insert");
        }
        UNSCOPED_INFO("as many forgotten as it closes as are over");
        CHECK(stack.endGroup() == size_t(2));
        {
            INFO("the newest kept");
            REQUIRE(stack.inForce() == 1);
            CHECK(stack.undone()[0].mNames == std::vector<std::string>{ "c", "d" });
        }
    }

    TEST_CASE_METHOD(alundostack_data, "alundostack: forgetOverBudget keeps the newest; takeForward and putForward round-trip", "[alundostack]")
    {
        ALUndoStack<Step> stack(std::numeric_limits<size_t>::max());
        const auto weight = [](const Step& step) { return step.mNames.front().size(); };
        stack.note({ { "aaaa" } }, "", 0.0, 1.0, join);
        stack.note({ { "bbb" } }, "", 5.0, 1.0, join);
        stack.note({ { "cc" } }, "", 10.0, 1.0, join);
        UNSCOPED_INFO("within the budget: none");
        CHECK(stack.forgetOverBudget(9, weight) == size_t(0));
        UNSCOPED_INFO("past it: the oldest until within");
        CHECK(stack.forgetOverBudget(5, weight) == size_t(1));
        {
            INFO("the newer kept");
            REQUIRE(stack.inForce() == 2);
            CHECK(stack.undone().front().mNames.front() == "bbb");
        }
        UNSCOPED_INFO("the newest kept whatever it weighs");
        CHECK(stack.forgetOverBudget(0, weight) == size_t(1));
        {
            INFO("alone");
            REQUIRE(stack.inForce() == 1);
            CHECK(stack.undone().front().mNames.front() == "cc");
        }

        stack.note({ { "d" } }, "", 20.0, 1.0, join);
        undoOne(stack);
        stack.note({ { "e" } }, "", 30.0, 1.0, join);
        undoOne(stack);
        undoOne(stack);
        std::vector<Step> forward = stack.takeForward();
        {
            INFO("taken out");
            CHECK(forward.size() == 2);
            CHECK_FALSE(stack.canRedo());
        }
        stack.putForward(std::move(forward));
        std::optional<Step> next = stack.takeRedo();
        {
            INFO("put back in their order");
            REQUIRE(next);
            CHECK(next->mNames.front() == "cc");
        }

        // What they weigh together as a caller keeps it, moved down by
        // what each forgotten weighed.
        ALUndoStack<Step> kept(std::numeric_limits<size_t>::max());
        kept.note({ { "aaaa" } }, "", 0.0, 1.0, join);
        kept.note({ { "bbb" } }, "", 5.0, 1.0, join);
        kept.note({ { "cc" } }, "", 10.0, 1.0, join);
        size_t held = 9;
        UNSCOPED_INFO("from what the caller keeps: the oldest until within");
        CHECK(kept.forgetOverBudget(5, weight, held) == size_t(1));
        UNSCOPED_INFO("and that less what went");
        CHECK(held == size_t(5));
        {
            INFO("the newer kept");
            REQUIRE(kept.inForce() == 2);
            CHECK(kept.undone().front().mNames.front() == "bbb");
        }
    }
}
