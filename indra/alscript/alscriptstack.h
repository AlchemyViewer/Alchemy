/**
 * @file alscriptstack.h
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

#pragma once

#include <cstddef>
#include <functional>

// The engines recurse on how a script nests -- Luau's parser to a thousand
// levels, Tailslide's walks over its tree, the preprocessor's expansion --
// and the threads they run on are a pool's, whose stack is whatever the
// platform gives a thread: half a megabyte on macOS, which a script nested
// deeply enough to be legal, or written to be hostile, runs out of. So the
// work is run on a stack of its own, as deep as asked, on the thread that
// asks, and anything it throws is thrown again once it is back.
constexpr std::size_t AL_SCRIPT_STACK_BYTES = 16u * 1024u * 1024u;

void alScriptOnLargeStack(const std::function<void()>& work, std::size_t bytes = AL_SCRIPT_STACK_BYTES);
