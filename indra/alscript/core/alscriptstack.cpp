/**
 * @file alscriptstack.cpp
 * @brief Work over a script's text run on a stack deep enough for the script.
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

#include "alscriptstack.h"

#include <boost/context/fiber.hpp>
#include <boost/context/protected_fixedsize_stack.hpp>

#include <exception>

namespace
{
    namespace ctx = boost::context;

    // The thread's own large stack, made the first time a job wants one and
    // kept until the thread ends: a guarded stack of sixteen megabytes is
    // mapped and protected afresh otherwise, for every job. How deep this
    // thread is in jobs on it, since one begun inside another is on it
    // already and must not start again at its top.
    struct Kept
    {
        ctx::protected_fixedsize_stack maker{ AL_SCRIPT_STACK_BYTES };
        ctx::stack_context             stack;
        std::size_t                    bytes = 0;
        int                            depth = 0;

        ~Kept()
        {
            if (stack.sp)
            {
                maker.deallocate(stack);
            }
        }
    };
    thread_local Kept tKept;

    // Hands the fiber the kept stack, and takes nothing back.
    struct KeptStack
    {
        std::size_t bytes;

        ctx::stack_context allocate()
        {
            if (!tKept.stack.sp || tKept.bytes != bytes)
            {
                // Let go of before the new one is made, and forgotten
                // then: a stack that cannot be made (bad_alloc) leaves
                // none kept, not the old one's dangling pointer.
                if (tKept.stack.sp)
                {
                    tKept.maker.deallocate(tKept.stack);
                    tKept.stack = ctx::stack_context();
                    tKept.bytes = 0;
                }
                tKept.maker = ctx::protected_fixedsize_stack(bytes);
                tKept.stack = tKept.maker.allocate();
                tKept.bytes = bytes;
            }
            return tKept.stack;
        }
        void deallocate(ctx::stack_context&) noexcept {}
    };
}

void alScriptOnLargeStack(const std::function<void()>& work, std::size_t bytes)
{
    // Already on it: a job begun inside another runs where it stands.
    if (tKept.depth > 0)
    {
        work();
        return;
    }
    // Guarded at its end, so that running off it is a fault where it
    // happens rather than a write over whatever lies beyond.
    // Counted as on it only once the fiber, and so its stack, is made:
    // making the stack can fail (bad_alloc), and a count left raised then
    // would run every later job of this thread's on its native stack.
    std::exception_ptr failed;
    ctx::fiber         deep(std::allocator_arg, KeptStack{ bytes },
                            [&](ctx::fiber&& back)
                            {
                                try
                                {
                                    work();
                                }
                                catch (...)
                                {
                                    // Nothing may leave a context's own
                                    // stack but by its return.
                                    failed = std::current_exception();
                                }
                                return std::move(back);
                            });
    ++tKept.depth;
    std::move(deep).resume();
    --tKept.depth;
    if (failed)
    {
        std::rethrow_exception(failed);
    }
}
