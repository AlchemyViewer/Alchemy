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

#include "../test/lltut.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace tut
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
    };

    typedef test_group<alundostack_data> alundostack_group;
    typedef alundostack_group::object alundostack_object;
    alundostack_group alundostack_instance("alundostack");

    // A change noted is a step back; taking it makes it a step forward
    // once the caller says so; a new change forgets the steps forward.
    template<> template<>
    void alundostack_object::test<1>()
    {
        ALUndoStack<Step> stack;
        ensure("nothing to undo yet", !stack.canUndo());

        stack.note({ { "a" } }, "", 0.0, 1.0, join);
        stack.note({ { "b" } }, "", 5.0, 1.0, join);
        ensure_equals("two steps back", stack.inForce(), size_t(2));

        std::optional<Step> back = stack.takeUndo();
        ensure("one taken", back.has_value() && back->mNames.front() == "b");
        stack.pushRedo(*back);
        ensure("and it is the step forward", stack.canRedo());
        ensure_equals("one in force", stack.inForce(), size_t(1));

        stack.note({ { "c" } }, "", 10.0, 1.0, join);
        ensure("a new change forgets the way forward", !stack.canRedo());
        ensure_equals("two in force", stack.inForce(), size_t(2));
    }

    // A run of changes to one thing within the window is one step, grown
    // to cover what each change touched; another thing, or a pause, is a
    // new step.
    template<> template<>
    void alundostack_object::test<2>()
    {
        ALUndoStack<Step> stack;
        stack.note({ { "slider" } }, "shape:fill", 0.0, 1.0, join);
        stack.note({ { "slider" } }, "shape:fill", 0.3, 1.0, join);
        stack.note({ { "slider", "shadow" } }, "shape:fill", 0.6, 1.0, join);
        ensure_equals("one step for the run", stack.inForce(), size_t(1));
        ensure_equals("grown to cover what the run touched", stack.undone().front().mNames.size(), size_t(2));

        stack.note({ { "slider" } }, "shape:fill", 2.0, 1.0, join);
        ensure_equals("a pause starts another", stack.inForce(), size_t(2));
        stack.note({ { "other" } }, "shape:radius", 2.1, 1.0, join);
        ensure_equals("and so does another thing", stack.inForce(), size_t(3));
        stack.note({ { "nothing" } }, "", 2.2, 1.0, join);
        stack.note({ { "nothing" } }, "", 2.3, 1.0, join);
        ensure_equals("no key never joins", stack.inForce(), size_t(5));
    }

    // The first thing said after a change names it; the next thing said
    // is about something else, and a frame later so is anything.
    template<> template<>
    void alundostack_object::test<3>()
    {
        ALUndoStack<Step> stack;
        stack.label("said before anything");
        stack.note({ { "a" } }, "", 0.0, 1.0, join);
        stack.label("Set the fill");
        stack.label("Something else");
        ensure_equals("the first word after the change names it", stack.undoLabel(), std::string("Set the fill"));

        stack.note({ { "b" } }, "", 5.0, 1.0, join);
        stack.closeLabel();
        stack.label("Too late");
        ensure_equals("a frame later names nothing", stack.undoLabel(), std::string());

        std::optional<Step> back = stack.takeUndo();
        stack.pushRedo(*back);
        ensure_equals("the step forward keeps its name", stack.redoLabel(), std::string());
        back = stack.takeUndo();
        stack.pushRedo(*back);
        ensure_equals("and so does the next", stack.redoLabel(), std::string("Set the fill"));
    }

    // Past the depth the oldest step is forgotten.
    template<> template<>
    void alundostack_object::test<4>()
    {
        ALUndoStack<Step> stack(3);
        for (int i = 0; i < 5; ++i)
        {
            stack.note({ { std::to_string(i) } }, "", i * 10.0, 1.0, join);
        }
        ensure_equals("three kept", stack.inForce(), size_t(3));
        ensure_equals("the oldest gone", stack.undone().front().mNames.front(), std::string("2"));
        stack.clear();
        ensure("cleared", !stack.canUndo() && !stack.canRedo());
    }
    template<> template<>
    void alundostack_object::test<5>()
    {
        ALUndoStack<Step> stack;
        stack.note({ { "a" } }, "slider", 0.0, 1.0, join);
        stack.note({ { "b" } }, "other", 0.1, 1.0, join);
        auto step = stack.takeUndo();
        stack.pushRedo(*step);
        stack.note({ { "c" } }, "other", 0.2, 1.0, join);
        ensure_equals("an edit after undo starts a new step", stack.inForce(), size_t(2));
        ensure_equals("the earlier step is unchanged", stack.undone().front().mNames.size(), size_t(1));
    }

    template<> template<>
    void alundostack_object::test<6>()
    {
        set_test_name("both stacks put back whole: the oldest steps back past the depth forgotten and counted, the last step forward the next taken");
        ALUndoStack<Step> stack(3);
        std::vector<Step> undo{ { { "1" } }, { { "2" } }, { { "3" } }, { { "4" } }, { { "5" } } };
        std::vector<Step> redo{ { { "z" } }, { { "y" } } };
        ensure_equals("two forgotten", stack.restore(undo, redo), size_t(2));
        ensure_equals("three kept", stack.inForce(), size_t(3));
        ensure_equals("from the third", stack.undone().front().mNames.front(), std::string("3"));
        std::optional<Step> back = stack.takeUndo();
        ensure("the newest taken back first", back && back->mNames.front() == "5");
        std::optional<Step> forward = stack.takeRedo();
        ensure("the last of the steps forward taken first", forward && forward->mNames.front() == "y");
        stack.note({ { "n" } }, "slider", 0.0, 1.0, join);
        ensure("a change after is a step of its own, and forgets the steps forward", stack.inForce() == 3 && !stack.canRedo());
    }

    template<> template<>
    void alundostack_object::test<7>()
    {
        set_test_name("a group: everything noted in it one step whatever its keys, nested counting as the outermost, named by the outermost's label");
        ALUndoStack<Step> stack;
        stack.note({ { "before" } }, "slider", 0.0, 1.0, join);
        stack.beginGroup("Reset Bloom");
        ensure("open", stack.inGroup());
        stack.note({ { "a" } }, "slider", 0.1, 1.0, join);
        stack.beginGroup("inner");
        stack.note({ { "b" } }, "other", 100.0, 1.0, join);
        stack.endGroup();
        ensure("still open", stack.inGroup());
        stack.note({ { "c" } }, "", 500.0, 1.0, join);
        stack.endGroup();
        ensure("closed", !stack.inGroup());
        ensure_equals("one step, not joined to the run before it", stack.inForce(), size_t(2));
        ensure_equals("all of it", stack.undone().back().mNames.size(), size_t(3));
        ensure_equals("the outermost's name", stack.undoLabel(), std::string("Reset Bloom"));
        stack.label("typed");
        ensure_equals("which a label after does not take", stack.undoLabel(), std::string("Reset Bloom"));
        stack.note({ { "a" } }, "slider", 500.1, 1.0, join);
        ensure_equals("a change after is its own step", stack.inForce(), size_t(3));

        stack.beginGroup("empty");
        stack.endGroup();
        ensure_equals("a group of nothing leaves nothing", stack.inForce(), size_t(3));
        stack.beginGroup();
        stack.note({ { "d" } }, "", 0.0, 1.0, join);
        stack.endGroup();
        stack.label("named after");
        ensure_equals("one with no name named by the first label after", stack.undoLabel(), std::string("named after"));
        ensure("unmatched ends ignored", !stack.endGroup() && !stack.inGroup());
        stack.beginGroup("outer");
        stack.beginGroup("inner");
        stack.note({ { "e" } }, "", 0.0, 1.0, join);
        stack.endGroup();
        stack.endGroup();
        ensure_equals("the outermost's name though the inner opened first on a change", stack.undoLabel(), std::string("outer"));
    }

    template<> template<>
    void alundostack_object::test<8>()
    {
        set_test_name("a run back where it began, or a group that undid itself, dropped; the steps forward it threw away stay thrown");
        ALUndoStack<Step> stack;
        const auto back_to_start = [](const Step& step) { return step.mNames.size() >= 2 && step.mNames.back() == "start"; };
        stack.note({ { "one" } }, "", 0.0, 1.0, join);
        stack.note({ { "two" } }, "", 5.0, 1.0, join);
        stack.pushRedo(*stack.takeUndo());
        stack.note({ { "drag" } }, "slider", 10.0, 1.0, join, back_to_start);
        ensure_equals("a drag is a step", stack.inForce(), size_t(2));
        stack.note({ { "start" } }, "slider", 10.2, 1.0, join, back_to_start);
        ensure_equals("let go where it began: gone", stack.inForce(), size_t(1));
        ensure("the way forward still gone", !stack.canRedo());
        stack.note({ { "drag" } }, "slider", 10.3, 1.0, join, back_to_start);
        ensure("a change after it is a step of its own", stack.inForce() == 2 && stack.undone().back().mNames.size() == 1);

        const auto trim = [](Step& step) {
            step.mNames.erase(std::remove(step.mNames.begin(), step.mNames.end(), "undone"), step.mNames.end());
            return step.mNames.empty();
        };
        stack.beginGroup("Look");
        stack.note({ { "undone" } }, "", 20.0, 1.0, join);
        stack.note({ { "kept" } }, "", 20.0, 1.0, join);
        stack.endGroup(trim);
        ensure("trimmed, kept", stack.inForce() == 3 && stack.undone().back().mNames == std::vector<std::string>{ "kept" });
        stack.beginGroup("Look");
        stack.note({ { "undone" } }, "", 30.0, 1.0, join);
        stack.endGroup(trim);
        ensure_equals("nothing left: dropped", stack.inForce(), size_t(3));
        ensure_equals("and the label with it", stack.undoLabel(), std::string("Look"));
    }

    template<> template<>
    void alundostack_object::test<9>()
    {
        set_test_name("the oldest forgotten when a group closes, not while it is open; groups closed at once, or with everything cleared");
        ALUndoStack<Step> stack(2);
        stack.note({ { "1" } }, "", 0.0, 1.0, join);
        stack.note({ { "2" } }, "", 5.0, 1.0, join);
        stack.beginGroup();
        ensure("not while open", !stack.note({ { "3" } }, "", 10.0, 1.0, join) && stack.inForce() == 3);
        ensure("when it closes", stack.endGroup() && stack.inForce() == 2 && stack.undone().front().mNames.front() == "2");
        stack.beginGroup();
        stack.note({ { "4" } }, "", 20.0, 1.0, join);
        ensure("an empty group closing forgets nothing", !stack.endGroup([](Step&) { return true; }) && stack.inForce() == 2);

        stack.beginGroup();
        stack.beginGroup();
        stack.note({ { "5" } }, "", 30.0, 1.0, join);
        ensure("closed at once", stack.closeGroups() && !stack.inGroup() && stack.inForce() == 2);
        ensure("none open: nothing", !stack.closeGroups());
        stack.beginGroup();
        stack.note({ { "6" } }, "", 40.0, 1.0, join);
        stack.pushRedo(*stack.takeUndo());
        stack.note({ { "7" } }, "", 40.0, 1.0, join);
        ensure("taken back while open: the next change a step of its own", stack.undone().back().mNames == std::vector<std::string>{ "7" });
        stack.clear();
        ensure("cleared, closed", !stack.inGroup());
        stack.note({ { "8" } }, "", 50.0, 1.0, join);
        stack.note({ { "9" } }, "", 60.0, 1.0, join);
        ensure_equals("and its own steps again", stack.inForce(), size_t(2));
    }

    template<> template<>
    void alundostack_object::test<10>()
    {
        set_test_name("a run broken while a group is open: the group goes on in a step of its own, both named by it, the oldest forgotten for each");
        ALUndoStack<Step> stack(1);
        stack.note({ { "0" } }, "", 0.0, 1.0, join);
        stack.beginGroup("Insert");
        stack.note({ { "a" } }, "", 1.0, 1.0, join);
        stack.note({ { "b" } }, "", 1.0, 1.0, join);
        stack.breakRun();
        stack.note({ { "c" } }, "", 1.0, 1.0, join);
        stack.note({ { "d" } }, "", 1.0, 1.0, join);
        ensure("still open", stack.inGroup());
        ensure_equals("nothing forgotten while it is", stack.inForce(), size_t(3));
        ensure("each whole", stack.undone()[1].mNames == std::vector<std::string>{ "a", "b" } &&
                                 stack.undone()[2].mNames == std::vector<std::string>{ "c", "d" });
        ensure("each named by the group", stack.undone()[1].mLabel == "Insert" && stack.undone()[2].mLabel == "Insert");
        ensure_equals("as many forgotten as it closes as are over", stack.endGroup(), size_t(2));
        ensure("the newest kept", stack.inForce() == 1 && stack.undone()[0].mNames == std::vector<std::string>{ "c", "d" });
    }

    template<> template<>
    void alundostack_object::test<11>()
    {
        set_test_name("a stack capped by what its steps weigh, the newest always kept; the steps forward taken out whole and put back");
        ALUndoStack<Step> stack(std::numeric_limits<size_t>::max());
        const auto weight = [](const Step& step) { return step.mNames.front().size(); };
        stack.note({ { "aaaa" } }, "", 0.0, 1.0, join);
        stack.note({ { "bbb" } }, "", 5.0, 1.0, join);
        stack.note({ { "cc" } }, "", 10.0, 1.0, join);
        ensure_equals("within the budget: none", stack.forgetOverBudget(9, weight), size_t(0));
        ensure_equals("past it: the oldest until within", stack.forgetOverBudget(5, weight), size_t(1));
        ensure("the newer kept", stack.inForce() == 2 && stack.undone().front().mNames.front() == "bbb");
        ensure_equals("the newest kept whatever it weighs", stack.forgetOverBudget(0, weight), size_t(1));
        ensure("alone", stack.inForce() == 1 && stack.undone().front().mNames.front() == "cc");

        stack.note({ { "d" } }, "", 20.0, 1.0, join);
        stack.pushRedo(*stack.takeUndo());
        stack.note({ { "e" } }, "", 30.0, 1.0, join);
        stack.pushRedo(*stack.takeUndo());
        stack.pushRedo(*stack.takeUndo());
        std::vector<Step> forward = stack.takeForward();
        ensure("taken out", forward.size() == 2 && !stack.canRedo());
        stack.putForward(std::move(forward));
        std::optional<Step> next = stack.takeRedo();
        ensure("put back in their order", next && next->mNames.front() == "cc");

        // What they weigh together as a caller keeps it, moved down by
        // what each forgotten weighed.
        ALUndoStack<Step> kept(std::numeric_limits<size_t>::max());
        kept.note({ { "aaaa" } }, "", 0.0, 1.0, join);
        kept.note({ { "bbb" } }, "", 5.0, 1.0, join);
        kept.note({ { "cc" } }, "", 10.0, 1.0, join);
        size_t held = 9;
        ensure_equals("from what the caller keeps: the oldest until within", kept.forgetOverBudget(5, weight, held), size_t(1));
        ensure_equals("and that less what went", held, size_t(5));
        ensure("the newer kept", kept.inForce() == 2 && kept.undone().front().mNames.front() == "bbb");
    }
}
