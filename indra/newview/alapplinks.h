/**
 * @file alapplinks.h
 * @brief What the viewer's app link handlers read from a link's parts
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

#ifndef AL_ALAPPLINKS_H
#define AL_ALAPPLINKS_H

#include "llsd.h"

#include <list>
#include <string>

// What the handlers of secondlife:///app links read from a link, apart from
// what they do with it. A handler is given the link's path after the
// command as the link writes it, each part escaped, and its query's keys
// and values unescaped.
class ALAppLinks
{
public:
    // The floater an openfloater link names, unescaped, as the handler opens
    // it.
    static std::string floaterName(const LLSD& params);

    // Whether the floater an openfloater link would open is one of
    // `floater_names`.
    static bool floaterNameIn(const LLSD& params, const std::list<std::string>& floater_names);
};

#endif // AL_ALAPPLINKS_H
