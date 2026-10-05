/**
 * @file aldiffedit.h
 * @brief Lines of a text put in place of others, as one edit of it.
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

#ifndef AL_ALDIFFEDIT_H
#define AL_ALDIFFEDIT_H

#include "altextdocument.h"

#include <string>
#include <vector>

// Lines of a text put in place of others, as one edit of the text: what a
// comparison's change taken back and a merge's conflict settled both are.
namespace ALDiffEdit
{
    // `count` lines of a text from `first`, counted from nought -- its
    // lines as ALTextDiff::split has them -- replaced by `with`: what
    // stretch of the text to put what in, and the text as it will be. A
    // line's break goes with the lines taken out or put in; at the text's
    // end, the one before them. False where nothing changes: no lines
    // taken out and none put in.
    bool replaceLines(const std::string& text, const std::vector<std::string>& lines, S32 first, S32 count,
                      const std::vector<std::string>& with, ALTextRange& range, std::string& put, std::string& made);
}

#endif // AL_ALDIFFEDIT_H
