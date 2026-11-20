/**
 * @file almasterdeletes_test.cpp
 * @brief Tests of ALMasterDeletes, the prims asked deleted from here.
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

#include "linden_common.h"

#include "../masters/almasterdeletes.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    LLUUID fresh()
    {
        LLUUID id;
        id.generate();
        return id;
    }

    // Tapple's (QK3): a prim deleted from here takes its links with it, one
    // gone any other way keeps them.
    TEST_CASE("ALMasterDeletes: a kill is a delete only where one was asked, lately", "[ALMasterDeletes]")
    {
        ALMasterDeletes deletes;
        const LLUUID    root = fresh(), child = fresh(), other = fresh();
        deletes.asked({ root, child, LLUUID::null }, 100.0);
        UNSCOPED_INFO("a prim asked deleted, killed: deleted");
        CHECK(deletes.killed(root, 101.0));
        UNSCOPED_INFO("once: a kill of it again is not");
        CHECK_FALSE(deletes.killed(root, 102.0));
        UNSCOPED_INFO("one never asked, gone out of sight: not");
        CHECK_FALSE(deletes.killed(other, 102.0));
        UNSCOPED_INFO("the null id is never kept");
        CHECK_FALSE(deletes.killed(LLUUID::null, 102.0));
        UNSCOPED_INFO("the child, within the wait: deleted");
        CHECK(deletes.killed(child, 100.0 + ALMasterDeletes::WAIT));
        CHECK(deletes.empty());
    }

    TEST_CASE("ALMasterDeletes: a delete the region kept is forgotten after the wait", "[ALMasterDeletes]")
    {
        ALMasterDeletes deletes;
        const LLUUID    kept = fresh(), later = fresh();
        deletes.asked({ kept }, 0.0);
        UNSCOPED_INFO("killed long after: gone some other way");
        CHECK_FALSE(deletes.killed(kept, ALMasterDeletes::WAIT + 1.0));
        deletes.asked({ kept }, 0.0);
        UNSCOPED_INFO("forgotten as another delete is asked");
        deletes.asked({ later }, ALMasterDeletes::WAIT + 1.0);
        CHECK_FALSE(deletes.killed(kept, ALMasterDeletes::WAIT + 1.0));
        UNSCOPED_INFO("asked again, held from then");
        deletes.asked({ later }, 2 * ALMasterDeletes::WAIT);
        CHECK(deletes.killed(later, 2.5 * ALMasterDeletes::WAIT));
    }
}
