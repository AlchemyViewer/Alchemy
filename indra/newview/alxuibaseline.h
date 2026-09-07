/**
 * @file alxuibaseline.h
 * @brief Builds every shipped XUI file and counts what goes wrong.
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

namespace ALXUIBaseline
{
    // Builds every file under skins/default/xui/en through the factory with a
    // diagnostics sink attached, counts overlaps and truncated labels in what
    // was built, and tallies every element tag in the files against the
    // widget registries. The summary goes to the log; the summary and the
    // detail go to xui_baseline.txt in the log directory.
    void run();
}
