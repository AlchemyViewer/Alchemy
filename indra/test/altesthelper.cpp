/**
 * @file altesthelper.cpp
 * @brief A process a test launches from its own binary, run by name and
 *        without the binary's test cases.
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

#include "altesthelper.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>

namespace ll_test
{
    namespace
    {
        constexpr char PREFIX[] = "--al-helper=";

        // Made on first use, so a helper registered from any static
        // initializer finds it ready.
        std::map<std::string, Helper::Main>& helpers()
        {
            static std::map<std::string, Helper::Main> sHelpers;
            return sHelpers;
        }
    }

    Helper::Helper(const char* name, Main main)
    {
        if (!helpers().emplace(name, main).second)
        {
            std::cerr << "Two test helpers are named " << name << std::endl;
            std::abort();
        }
    }

    std::string Helper::argument(const std::string& name)
    {
        return PREFIX + name;
    }

    const char* Helper::named(int argc, char* argv[])
    {
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], PREFIX, sizeof(PREFIX) - 1) == 0)
            {
                return argv[i] + sizeof(PREFIX) - 1;
            }
        }
        return nullptr;
    }

    int Helper::run(const std::string& name)
    {
        auto found = helpers().find(name);
        if (found == helpers().end())
        {
            std::cerr << "No test helper is named " << name << std::endl;
            return 2;
        }
        return found->second();
    }
}
