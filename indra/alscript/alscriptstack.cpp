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

void alScriptOnLargeStack(const std::function<void()>& work, std::size_t bytes)
{
    namespace ctx = boost::context;
    // Guarded at its end, so that running off it is a fault where it
    // happens rather than a write over whatever lies beyond.
    std::exception_ptr failed;
    ctx::fiber         deep(std::allocator_arg, ctx::protected_fixedsize_stack(bytes),
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
    std::move(deep).resume();
    if (failed)
    {
        std::rethrow_exception(failed);
    }
}
