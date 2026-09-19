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

#include "../alundostack.h"

#include "../test/lltut.h"

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

}
