/**
 * @file altestteardown.cpp
 * @brief The end of a test binary's process-wide fixtures, in order.
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

#include "altestteardown.h"

#include <exception>
#include <iostream>
#include <vector>

namespace ll_test
{
    namespace
    {
        // Made on first use, so registering a step from any static
        // initializer finds it ready.
        std::vector<std::function<void()>>& steps()
        {
            static std::vector<std::function<void()>> sSteps;
            return sSteps;
        }
    }

    void Teardown::add(std::function<void()> step)
    {
        steps().push_back(std::move(step));
    }

    int Teardown::runAll()
    {
        int failed = 0;
        std::vector<std::function<void()>>& pending = steps();
        while (!pending.empty())
        {
            std::function<void()> step = std::move(pending.back());
            pending.pop_back();
            try
            {
                step();
            }
            catch (const std::exception& e)
            {
                ++failed;
                std::cerr << "Teardown step threw: " << e.what() << std::endl;
            }
            catch (...)
            {
                ++failed;
                std::cerr << "Teardown step threw a non-standard exception" << std::endl;
            }
        }
        return failed;
    }
}
