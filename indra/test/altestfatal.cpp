/**
 * @file altestfatal.cpp
 * @brief What an LL_ERRS does in a Catch2 test.
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

#include "altestfatal.h"
#include "altestname.h"

#include "llerrorcontrol.h"

#include <cstdlib>
#include <exception>
#include <iostream>

namespace ll_test
{
    FatalError::FatalError(const std::string& message)
        : std::runtime_error("LL_ERRS: " + message)
    {
    }

    namespace
    {
        void throwFatalError(const std::string& message)
        {
            throw FatalError(message);
        }
    }

    void installFatalFunction()
    {
        LLError::setFatalFunction(throwFatalError);
    }

    void installTerminateHandler()
    {
        std::set_terminate(
            []
            {
                std::cerr << "std::terminate in test case \"" << currentTestName() << "\"";
                if (std::exception_ptr in_flight = std::current_exception())
                {
                    try
                    {
                        std::rethrow_exception(in_flight);
                    }
                    catch (const std::exception& e)
                    {
                        std::cerr << ": " << e.what();
                    }
                    catch (...)
                    {
                        std::cerr << ": a non-standard exception";
                    }
                }
                std::cerr << std::endl;
#if LL_WINDOWS
                // No abort dialog for CTest to wait out.
                _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
                std::abort();
            });
    }
}
