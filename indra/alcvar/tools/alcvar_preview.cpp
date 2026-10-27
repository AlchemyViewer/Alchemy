/**
 * @file alcvar_preview.cpp
 * @brief alcvar_preview <settings.db>: what opening a settings file would do
 *        to each of its rows, a row a line, with nothing written.
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

#include "alcvarpreview.h"

// The tests' schema, until the viewer's declares settings.
#include "cvartest/schema.h"

#include <iostream>

int main(int argc, char* argv[])
{
    std::string path;
    ALCVarLayer which = ALCVarLayer::Global;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--account")
        {
            which = ALCVarLayer::Account;
        }
        else if (arg == "--help" || arg == "-h" || arg.starts_with("--") || !path.empty())
        {
            path.clear();
            break;
        }
        else
        {
            path = arg;
        }
    }
    if (path.empty())
    {
        std::cerr << "usage: alcvar_preview [--account] <settings.db>\n"
                     "\n"
                     "Says what opening the settings file would do to each of its rows, a row a\n"
                     "line: the step it is at, what it becomes and through which steps, or what\n"
                     "is left alone and why. A copy is opened, never the file, and nothing is\n"
                     "written. --account reads it as an account's file. Migrations are this\n"
                     "build's, and one it does not register is said. doc/CVARS.md has more.\n";
        return 2;
    }

    ALCVars& store = ALCVars::instance();
    store.registerSchema(cvartest::schema());
    std::string error;
    const auto  lines = ALCVarPreview::file(store, path, which, error);
    if (!lines)
    {
        std::cerr << path << ": " << error << "\n";
        ALCVars::deleteSingleton();
        return 1;
    }
    for (const auto& line : *lines)
    {
        std::cout << line << "\n";
    }
    ALCVars::deleteSingleton();
    return 0;
}
