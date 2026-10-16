/**
 * @file alrandomreseed.h
 * @brief llrand's generator seeded from entropy again, for a test that
 *        seeded it
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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

#ifndef AL_ALRANDOMRESEED_H
#define AL_ALRANDOMRESEED_H

#include "linden_common.h"

#include "llrand.h"

#include <random>

namespace ll_test
{
    // A test that seeds the thread's generator, so that what it draws is the
    // same on every run, seeds it from entropy again as it ends, as a thread's
    // first draw does. Left seeded, everything drawn after it on the thread
    // -- by later tests, and by what they call, such as LLUUID's clock
    // sequence -- would be one fixed sequence that hangs on which tests ran
    // before. llrand cannot hand back the state it had, so this is the
    // nearest to putting it back.
    inline void reseedRandomFromEntropy()
    {
        std::random_device entropy;
        ll_rand_seed((U64(entropy()) << 32) | entropy());
    }
}

#endif // AL_ALRANDOMRESEED_H
