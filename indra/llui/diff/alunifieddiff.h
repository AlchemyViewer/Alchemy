/**
 * @file alunifieddiff.h
 * @brief Two texts' differences written as a unified diff, as diff -u and git write one.
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

#ifndef AL_ALUNIFIEDDIFF_H
#define AL_ALUNIFIEDDIFF_H

#include "altextdiff.h"

#include <string>
#include <string_view>

// Two texts' differences written as diff -u and git write them: each
// under its name, then each stretch of changes with so many lines the same
// either side -- stretches that close joined -- under its lines in each,
// counted from one: the lines the same as the right has them, those taken
// out after "-", those put in after "+", and a text's last line without a
// line break said so. By lines, as told the same; a change of nothing but
// lines let go of -- blank ones, or comments where a lexer given says
// where they are -- starts no stretch of its own. A lexer given reads the
// texts here, which a comparison's own, holding its texts, should not.
// Pure.
namespace ALUnifiedDiff
{
    constexpr S32 CONTEXT = 3;

    // Nothing where the two are the same, as compared.
    std::string write(std::string_view left, std::string_view right, const std::string& left_name, const std::string& right_name,
                      const ALTextDiff::Options& options = ALTextDiff::Options(), S32 context = CONTEXT);
}

#endif // AL_ALUNIFIEDDIFF_H
