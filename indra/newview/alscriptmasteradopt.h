/**
 * @file alscriptmasteradopt.h
 * @brief Links just made, probed for what a send through them would know.
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

#include "almasterlinks.h"

#include <deque>
#include <memory>
#include <vector>

// A link just made that knows nothing yet of what a send through it would
// know -- one the studio's Link to File made from a tab, which takes the
// file as it stands -- adopted: probed once, its master read and expanded
// as a send would expand it and the world's text read beside it
// (ALScriptMasterUpload::probe), four at a time. What the expansion read
// is what an include's save sends the script again for, and an include it
// missed may be any file saved; and where the world holds what the file
// makes already, that is what went up last, as a send would have left it,
// so that the Explorer's mark says the script holds its file, and a send
// changing nothing is skipped. Nothing is said: it is what the link would
// have known had it been made by a send.
class ALScriptMasterAdopt
{
public:
    // Whether a link knows nothing of what a send through it would know:
    // nothing went up through it, and nothing of its file's expansion is
    // known.
    static bool knowsNothing(const ALMasterLink& link);

    // Links just made that know nothing, probed after those waiting
    // already; what each probe finds goes to ALScriptDiskMasters::adopted.
    void adopt(const std::vector<ALMasterLink>& made);

private:
    void probeMore();

    std::deque<ALMasterLink> mToProbe;
    size_t                   mProbing     = 0;
    bool                     mInProbeMore = false;
    // Held while this is, for a probe to know it still is.
    std::shared_ptr<bool>    mAlive = std::make_shared<bool>(true);
};
