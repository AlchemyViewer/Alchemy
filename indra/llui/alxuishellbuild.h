/**
 * @file alxuishellbuild.h
 * @brief Builds XUI as the shell its file describes, not the code behind it.
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

#pragma once

// While one of these is alive, a panel whose class attribute names a
// registered panel class is built as a plain panel instead. A registered
// class is viewer code with the side effects of viewer code: event pumps
// with fixed names, listeners on the agent, singletons it expects to exist
// once. A build that only wants to see what the file describes cannot
// afford any of that, and nothing in the file changes because of it.
class ALXUIShellBuild
{
public:
    ALXUIShellBuild();
    ~ALXUIShellBuild();

    ALXUIShellBuild(const ALXUIShellBuild&) = delete;
    ALXUIShellBuild& operator=(const ALXUIShellBuild&) = delete;

    static bool active() { return sDepth > 0; }

private:
    static int sDepth;
};
