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
        UNSCOPED_INFO("1 prefersPG");
        CHECK(aa.prefersPG());
        UNSCOPED_INFO("1 prefersMature");
        CHECK_FALSE(aa.prefersMature());
        UNSCOPED_INFO("1 prefersAdult");
        CHECK_FALSE(aa.prefersAdult());
#endif // HACKED_GODLIKE_VIEWER

        cgr.setU32("PreferredMaturity", SIM_ACCESS_MATURE);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("2 prefersPG");
        CHECK_FALSE(aa.prefersPG());
        UNSCOPED_INFO("2 prefersMature");
        CHECK(aa.prefersMature());
        UNSCOPED_INFO("2 prefersAdult");
        CHECK_FALSE(aa.prefersAdult());
#endif // HACKED_GODLIKE_VIEWER

        cgr.setU32("PreferredMaturity", SIM_ACCESS_ADULT);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("3 prefersPG");
        CHECK_FALSE(aa.prefersPG());
        UNSCOPED_INFO("3 prefersMature");
        CHECK(aa.prefersMature());
        UNSCOPED_INFO("3 prefersAdult");
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
        UNSCOPED_INFO("1 isTeen");
        CHECK(aa.isTeen());
        UNSCOPED_INFO("1 isMature");
        CHECK_FALSE(aa.isMature());
        UNSCOPED_INFO("1 isAdult");
        CHECK_FALSE(aa.isAdult());
#endif // HACKED_GODLIKE_VIEWER

        // check the conversion routine
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 conversion");
        CHECK(SIM_ACCESS_PG == aa.convertTextToMaturity('P'));
        UNSCOPED_INFO("2 conversion");
        CHECK(SIM_ACCESS_MATURE == aa.convertTextToMaturity('M'));
        UNSCOPED_INFO("3 conversion");
        CHECK(SIM_ACCESS_ADULT == aa.convertTextToMaturity('A'));
        UNSCOPED_INFO("4 conversion");
        CHECK(SIM_ACCESS_MIN == aa.convertTextToMaturity('Q'));
#endif // HACKED_GODLIKE_VIEWER

        // now try the other method of setting it - PG
        aa.setMaturity('P');
        UNSCOPED_INFO("2 isTeen");
        CHECK(aa.isTeen());
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("2 isMature");
        CHECK_FALSE(aa.isMature());
        UNSCOPED_INFO("2 isAdult");
        CHECK_FALSE(aa.isAdult());
#endif // HACKED_GODLIKE_VIEWER

        // Mature
        aa.setMaturity('M');
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("3 isTeen");
        CHECK_FALSE(aa.isTeen());
        UNSCOPED_INFO("3 isMature");
        CHECK(aa.isMature());
        UNSCOPED_INFO("3 isAdult");
        CHECK_FALSE(aa.isAdult());
#endif // HACKED_GODLIKE_VIEWER

        // Adult
        aa.setMaturity('A');
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("4 isTeen");
        CHECK_FALSE(aa.isTeen());
        UNSCOPED_INFO("4 isMature");
        CHECK(aa.isMature());
        UNSCOPED_INFO("4 isAdult");
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
        UNSCOPED_INFO("admin true");
        CHECK(aa.getAdminOverride());
        UNSCOPED_INFO("overrides 1");
        CHECK(aa.isGodlike());
#endif // HACKED_GODLIKE_VIEWER
        aa.setGodLevel(GOD_FULL);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("overrides 2");
        CHECK(aa.isGodlike());
#endif // HACKED_GODLIKE_VIEWER
        aa.setAdminOverride(false);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("admin false");
        CHECK_FALSE(aa.getAdminOverride());
        UNSCOPED_INFO("overrides 3");
        CHECK(aa.isGodlike());
#endif // HACKED_GODLIKE_VIEWER
    }

    TEST_CASE("LLAgentAccess: access needs both rating and preference unless god or admin", "[LLAgentAccess]")
    {
        LLControlGroup cgr("llagentaccess");
        cgr.declareU32("PreferredMaturity", SIM_ACCESS_PG, "declared_for_test", LLControlVariable::PERSIST_NO);
        LLAgentAccess aa(cgr);

#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 pg to start");
        CHECK(aa.wantsPGOnly());
        UNSCOPED_INFO("2 pg to start");
        CHECK_FALSE(aa.canAccessMature());
        UNSCOPED_INFO("3 pg to start");
        CHECK_FALSE(aa.canAccessAdult());
#endif // HACKED_GODLIKE_VIEWER

        aa.setGodLevel(GOD_FULL);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 full god");
        CHECK_FALSE(aa.wantsPGOnly());
        UNSCOPED_INFO("2 full god");
        CHECK(aa.canAccessMature());
        UNSCOPED_INFO("3 full god");
        CHECK(aa.canAccessAdult());
#endif // HACKED_GODLIKE_VIEWER

        aa.setGodLevel(GOD_NOT);
        aa.setAdminOverride(true);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 admin mode");
        CHECK_FALSE(aa.wantsPGOnly());
        UNSCOPED_INFO("2 admin mode");
        CHECK(aa.canAccessMature());
        UNSCOPED_INFO("3 admin mode");
        CHECK(aa.canAccessAdult());
#endif // HACKED_GODLIKE_VIEWER

        aa.setAdminOverride(false);
        aa.setMaturity('M');
        // preferred is still pg by default
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 mature pref pg");
        CHECK(aa.wantsPGOnly());
        UNSCOPED_INFO("2 mature pref pg");
        CHECK_FALSE(aa.canAccessMature());
        UNSCOPED_INFO("3 mature pref pg");
        CHECK_FALSE(aa.canAccessAdult());
#endif // HACKED_GODLIKE_VIEWER

        cgr.setU32("PreferredMaturity", SIM_ACCESS_MATURE);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 mature");
        CHECK_FALSE(aa.wantsPGOnly());
        UNSCOPED_INFO("2 mature");
        CHECK(aa.canAccessMature());
        UNSCOPED_INFO("3 mature");
        CHECK_FALSE(aa.canAccessAdult());
#endif // HACKED_GODLIKE_VIEWER

        cgr.setU32("PreferredMaturity", SIM_ACCESS_PG);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 mature pref pg");
        CHECK(aa.wantsPGOnly());
        UNSCOPED_INFO("2 mature pref pg");
        CHECK_FALSE(aa.canAccessMature());
        UNSCOPED_INFO("3 mature pref pg");
        CHECK_FALSE(aa.canAccessAdult());
#endif // HACKED_GODLIKE_VIEWER

        aa.setMaturity('A');
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 adult pref pg");
        CHECK(aa.wantsPGOnly());
        UNSCOPED_INFO("2 adult pref pg");
        CHECK_FALSE(aa.canAccessMature());
        UNSCOPED_INFO("3 adult pref pg");
        CHECK_FALSE(aa.canAccessAdult());
#endif // HACKED_GODLIKE_VIEWER

        cgr.setU32("PreferredMaturity", SIM_ACCESS_ADULT);
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 adult");
        CHECK_FALSE(aa.wantsPGOnly());
        UNSCOPED_INFO("2 adult");
        CHECK(aa.canAccessMature());
        UNSCOPED_INFO("3 adult");
        CHECK(aa.canAccessAdult());
#endif // HACKED_GODLIKE_VIEWER

        // make sure that even if pref is high, if access is low we block access
        // this shouldn't occur in real life but we want to be safe
        cgr.setU32("PreferredMaturity", SIM_ACCESS_ADULT);
        aa.setMaturity('P');
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 pref adult, actual pg");
        CHECK(aa.wantsPGOnly());
        UNSCOPED_INFO("2 pref adult, actual pg");
        CHECK_FALSE(aa.canAccessMature());
        UNSCOPED_INFO("3 pref adult, actual pg");
        CHECK_FALSE(aa.canAccessAdult());
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
        UNSCOPED_INFO("1 preferred maturity pegged to M when maturity is M");
        CHECK(cgr.getU32("PreferredMaturity") == SIM_ACCESS_MATURE);
#endif // HACKED_GODLIKE_VIEWER

        aa.setMaturity('P');
#ifndef HACKED_GODLIKE_VIEWER
        UNSCOPED_INFO("1 preferred maturity pegged to P when maturity is P");
        CHECK(cgr.getU32("PreferredMaturity") == SIM_ACCESS_PG);
#endif // HACKED_GODLIKE_VIEWER
    }
}


