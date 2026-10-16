/**
 * @file llagentaccess_test.cpp
 * @brief LLAgentAccess tests
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>

#include "../llagentaccess.h"

#include "llcontrol.h"
#include "indra_constants.h"

#include <iostream>

namespace
{
    TEST_CASE("LLAgentAccess: the prefers queries follow the PreferredMaturity setting", "[LLAgentAccess]")
    {
        LLControlGroup cgr("llagentaccess");
        cgr.declareU32("PreferredMaturity", SIM_ACCESS_PG, "declared_for_test", LLControlVariable::PERSIST_NO);
        LLAgentAccess aa(cgr);

        cgr.setU32("PreferredMaturity", SIM_ACCESS_PG);
#ifndef HACKED_GODLIKE_VIEWER
        CHECK(aa.prefersPG());
        CHECK_FALSE(aa.prefersMature());
        CHECK_FALSE(aa.prefersAdult());
#endif // HACKED_GODLIKE_VIEWER

        cgr.setU32("PreferredMaturity", SIM_ACCESS_MATURE);
#ifndef HACKED_GODLIKE_VIEWER
        CHECK_FALSE(aa.prefersPG());
        CHECK(aa.prefersMature());
        CHECK_FALSE(aa.prefersAdult());
#endif // HACKED_GODLIKE_VIEWER

        cgr.setU32("PreferredMaturity", SIM_ACCESS_ADULT);
#ifndef HACKED_GODLIKE_VIEWER
        CHECK_FALSE(aa.prefersPG());
        CHECK(aa.prefersMature());
        CHECK(aa.prefersAdult());
#endif // HACKED_GODLIKE_VIEWER
    }

    TEST_CASE("LLAgentAccess: maturity starts PG and setMaturity takes the P M and A letters", "[LLAgentAccess]")
    {
        LLControlGroup cgr("llagentaccess");
        cgr.declareU32("PreferredMaturity", SIM_ACCESS_PG, "declared_for_test", LLControlVariable::PERSIST_NO);
        LLAgentAccess aa(cgr);

        // make sure default is PG
#ifndef HACKED_GODLIKE_VIEWER
        CHECK(aa.isTeen());
        CHECK_FALSE(aa.isMature());
        CHECK_FALSE(aa.isAdult());
#endif // HACKED_GODLIKE_VIEWER

        // check the conversion routine
#ifndef HACKED_GODLIKE_VIEWER
        CHECK(SIM_ACCESS_PG == aa.convertTextToMaturity('P'));
        CHECK(SIM_ACCESS_MATURE == aa.convertTextToMaturity('M'));
        CHECK(SIM_ACCESS_ADULT == aa.convertTextToMaturity('A'));
        CHECK(SIM_ACCESS_MIN == aa.convertTextToMaturity('Q'));
#endif // HACKED_GODLIKE_VIEWER

        // now try the other method of setting it - PG
        aa.setMaturity('P');
        CHECK(aa.isTeen());
#ifndef HACKED_GODLIKE_VIEWER
        CHECK_FALSE(aa.isMature());
        CHECK_FALSE(aa.isAdult());
#endif // HACKED_GODLIKE_VIEWER

        // Mature
        aa.setMaturity('M');
#ifndef HACKED_GODLIKE_VIEWER
        CHECK_FALSE(aa.isTeen());
        CHECK(aa.isMature());
        CHECK_FALSE(aa.isAdult());
#endif // HACKED_GODLIKE_VIEWER

        // Adult
        aa.setMaturity('A');
#ifndef HACKED_GODLIKE_VIEWER
        CHECK_FALSE(aa.isTeen());
        CHECK(aa.isMature());
        CHECK(aa.isAdult());
#endif // HACKED_GODLIKE_VIEWER

    }

    TEST_CASE("LLAgentAccess: isGodlike follows the god level and the admin override", "[LLAgentAccess]")
    {
        LLControlGroup cgr("llagentaccess");
        cgr.declareU32("PreferredMaturity", SIM_ACCESS_PG, "declared_for_test", LLControlVariable::PERSIST_NO);
        LLAgentAccess aa(cgr);

#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("starts normal");
        CHECK_FALSE(aa.isGodlike());
#endif // HACKED_GODLIKE_VIEWER
        aa.setGodLevel(GOD_NOT);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("stays normal");
        CHECK_FALSE(aa.isGodlike());
#endif // HACKED_GODLIKE_VIEWER
        aa.setGodLevel(GOD_FULL);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("sets full");
        CHECK(aa.isGodlike());
#endif // HACKED_GODLIKE_VIEWER
        aa.setGodLevel(GOD_NOT);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("resets normal");
        CHECK_FALSE(aa.isGodlike());
#endif // HACKED_GODLIKE_VIEWER
        aa.setAdminOverride(true);
#ifndef HACKED_GODLIKE_VIEWER
        CHECK(aa.getAdminOverride());
        UNSCOPED_INFO("the admin override alone");
        CHECK(aa.isGodlike());
#endif // HACKED_GODLIKE_VIEWER
        aa.setGodLevel(GOD_FULL);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("the admin override and full god level");
        CHECK(aa.isGodlike());
#endif // HACKED_GODLIKE_VIEWER
        aa.setAdminOverride(false);
#ifndef HACKED_GODLIKE_VIEWER
        CHECK_FALSE(aa.getAdminOverride());
        UNSCOPED_INFO("full god level alone");
        CHECK(aa.isGodlike());
#endif // HACKED_GODLIKE_VIEWER
    }

    TEST_CASE("LLAgentAccess: access needs both rating and preference unless god or admin", "[LLAgentAccess]")
    {
        LLControlGroup cgr("llagentaccess");
        cgr.declareU32("PreferredMaturity", SIM_ACCESS_PG, "declared_for_test", LLControlVariable::PERSIST_NO);
        LLAgentAccess aa(cgr);

#ifndef HACKED_GODLIKE_VIEWER
        {
            INFO("pg to start");
            CHECK(aa.wantsPGOnly());
            CHECK_FALSE(aa.canAccessMature());
            CHECK_FALSE(aa.canAccessAdult());
        }
#endif // HACKED_GODLIKE_VIEWER

        aa.setGodLevel(GOD_FULL);
#ifndef HACKED_GODLIKE_VIEWER
        {
            INFO("full god");
            CHECK_FALSE(aa.wantsPGOnly());
            CHECK(aa.canAccessMature());
            CHECK(aa.canAccessAdult());
        }
#endif // HACKED_GODLIKE_VIEWER

        aa.setGodLevel(GOD_NOT);
        aa.setAdminOverride(true);
#ifndef HACKED_GODLIKE_VIEWER
        {
            INFO("admin mode");
            CHECK_FALSE(aa.wantsPGOnly());
            CHECK(aa.canAccessMature());
            CHECK(aa.canAccessAdult());
        }
#endif // HACKED_GODLIKE_VIEWER

        aa.setAdminOverride(false);
        aa.setMaturity('M');
        // preferred is still pg by default
#ifndef HACKED_GODLIKE_VIEWER
        {
            INFO("mature pref pg");
            CHECK(aa.wantsPGOnly());
            CHECK_FALSE(aa.canAccessMature());
            CHECK_FALSE(aa.canAccessAdult());
        }
#endif // HACKED_GODLIKE_VIEWER

        cgr.setU32("PreferredMaturity", SIM_ACCESS_MATURE);
#ifndef HACKED_GODLIKE_VIEWER
        {
            INFO("mature");
            CHECK_FALSE(aa.wantsPGOnly());
            CHECK(aa.canAccessMature());
            CHECK_FALSE(aa.canAccessAdult());
        }
#endif // HACKED_GODLIKE_VIEWER

        cgr.setU32("PreferredMaturity", SIM_ACCESS_PG);
#ifndef HACKED_GODLIKE_VIEWER
        {
            INFO("mature pref pg");
            CHECK(aa.wantsPGOnly());
            CHECK_FALSE(aa.canAccessMature());
            CHECK_FALSE(aa.canAccessAdult());
        }
#endif // HACKED_GODLIKE_VIEWER

        aa.setMaturity('A');
#ifndef HACKED_GODLIKE_VIEWER
        {
            INFO("adult pref pg");
            CHECK(aa.wantsPGOnly());
            CHECK_FALSE(aa.canAccessMature());
            CHECK_FALSE(aa.canAccessAdult());
        }
#endif // HACKED_GODLIKE_VIEWER

        cgr.setU32("PreferredMaturity", SIM_ACCESS_ADULT);
#ifndef HACKED_GODLIKE_VIEWER
        {
            INFO("adult");
            CHECK_FALSE(aa.wantsPGOnly());
            CHECK(aa.canAccessMature());
            CHECK(aa.canAccessAdult());
        }
#endif // HACKED_GODLIKE_VIEWER

        // make sure that even if pref is high, if access is low we block access
        // this shouldn't occur in real life but we want to be safe. The
        // preference goes after the rating, which setMaturity would pull it
        // down to.
        aa.setMaturity('P');
        cgr.setU32("PreferredMaturity", SIM_ACCESS_ADULT);
#ifndef HACKED_GODLIKE_VIEWER
        {
            INFO("pref adult, actual pg");
            CHECK(aa.prefersAdult());
            CHECK(aa.wantsPGOnly());
            CHECK_FALSE(aa.canAccessMature());
            CHECK_FALSE(aa.canAccessAdult());
        }
#endif // HACKED_GODLIKE_VIEWER

    }

    TEST_CASE("LLAgentAccess: setMaturity lowers a higher preferred maturity to match", "[LLAgentAccess]")
    {
        LLControlGroup cgr("llagentaccess");
        cgr.declareU32("PreferredMaturity", SIM_ACCESS_PG, "declared_for_test", LLControlVariable::PERSIST_NO);
        LLAgentAccess aa(cgr);

        cgr.setU32("PreferredMaturity", SIM_ACCESS_ADULT);
        aa.setMaturity('M');
#ifndef HACKED_GODLIKE_VIEWER
        CHECK(cgr.getU32("PreferredMaturity") == SIM_ACCESS_MATURE);
#endif // HACKED_GODLIKE_VIEWER

        aa.setMaturity('P');
#ifndef HACKED_GODLIKE_VIEWER
        CHECK(cgr.getU32("PreferredMaturity") == SIM_ACCESS_PG);
#endif // HACKED_GODLIKE_VIEWER
    }
}


