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
#include "v3math.h"

#include <list>
#include <optional>
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

    // What an agent or object link's block or unblock asks for: the link's
    // second part is the verb and its third the object's name, which both
    // verbs' links escape.
    struct ObjectMute
    {
        bool        mBlock = false;
        std::string mName;
    };

    // Nothing for another verb, or for a block or unblock with no name.
    static std::optional<ObjectMute> objectMute(const LLSD& params);

    // Where in its region a teleport link goes: its first part is the region
    // and the three after it x, y and z. A coordinate the link leaves out is
    // the region's centre's, as the teleport event's are, and a link with
    // more than three goes to the centre.
    static LLVector3 teleportPosition(const LLSD& tokens);

    // A flag in a link's query, which the query holds as text: "false" and
    // "0" and their kin read as false, "true" and "1" and theirs as true,
    // and anything else as LLSD reads a string, true unless it is empty, so
    // that a flag written from a boolean, "true" or empty, reads as that
    // boolean. A key with no value reads as true.
    static bool queryFlag(const LLSD& value);
};

#endif // AL_ALAPPLINKS_H
