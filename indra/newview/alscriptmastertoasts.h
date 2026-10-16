/**
 * @file alscriptmastertoasts.h
 * @brief What came of sends from files on disk, said with no studio window open.
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

#include "alscriptdiskmasters.h"

#include <memory>
#include <vector>

// What came of the sends from files on disk while no Script Studio window
// is in sight to say it in Output: the failures and the conflicts only -- not
// sent, not compiled, changed in-world, the file or the item gone, out of
// reach -- since a send that went as asked is nothing to interrupt anybody
// for. They come in bursts, a checkout's or an include's, so they are
// gathered for a moment and said in one toast, the first few in full and
// the rest counted. Output has them when a window is looked at again:
// a hidden one heard them, and the one opened next where there was none
// (ALScriptDiskMasters::takeUnheard).
class ALScriptMasterToasts
{
public:
    typedef ALScriptDiskMasters::Outcome Outcome;

    // Whether an outcome is one to interrupt the scripter for.
    static bool worthSaying(const Outcome& outcome);
    // One heard with nobody to tell: said a moment from now, with whatever
    // else is heard by then.
    void heard(const Outcome& outcome);

private:
    void say();
    static std::string wordsFor(const Outcome& outcome);

    std::vector<Outcome>  mGathered;
    // Held while this is, for the timer to know it still is.
    std::shared_ptr<bool> mAlive = std::make_shared<bool>(true);
};
