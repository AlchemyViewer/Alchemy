/**
 * @file allsldepth.h
 * @brief How deep an LSL script's tree may go for alscript to walk it
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

#include "stdtypes.h"

#include <string_view>

namespace Tailslide
{
    class LSLASTNode;
    class LSLScript;
    struct ScopedScriptParser;
}

// Tailslide's trees are walked by recursion wherever they are read -- its
// own passes, the lints, the outline, the weighers, the writer of SLua -- a
// level of the stack for each level of the tree, and a chain of operators,
// which its grammar builds leaning to one side, is as deep as it is long:
// the 256 KiB of source the grid takes holds a chain of 128 thousand terms,
// and the writer of SLua runs off the analysis stack's 16 MiB at two
// thousand under the sanitizers. So no tree deeper than LIMIT is walked: a
// parse that makes one is refused, with an error in the parser's log where
// it goes too deep, as Luau's parser refuses SLua nested past the same
// depth. The script still saves and compiles; Script Studio says it will
// not analyze it.
namespace ALLSLDepth
{
    constexpr S32 LIMIT = 1000;

    // The first node found deeper than `limit` levels under `root`, or null
    // where none is: found with a stack of its own, not by recursing.
    Tailslide::LSLASTNode* deeperThan(Tailslide::LSLASTNode* root, S32 limit = LIMIT);

    // Tailslide's parse of the text, in place of parseLSLBytes: null where
    // it does not parse, and where its tree goes deeper than LIMIT, which
    // is logged as an error at the place it does, and which `deep`, where
    // given, says.
    Tailslide::LSLScript* parse(Tailslide::ScopedScriptParser& parser, const char* text, int size, bool* deep = nullptr);

    // Whether a message of the parser's log is that refusal.
    bool said(std::string_view message);
}
