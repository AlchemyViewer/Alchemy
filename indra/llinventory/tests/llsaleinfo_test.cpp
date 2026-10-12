/**
 * @file llsaleinfo_test.cpp
 * @author Adroit
 * @date 2007-03
 * @brief Test cases of llsaleinfo.h
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include "linden_common.h"
#include "llsaleinfo.h"

namespace
{
    TEST_CASE("llsaleinfo: each sale type sets its price and CRC32 and survives LLSD", "[llsaleinfo]")
    {
        //test case for getSaleType(), getSalePrice(), getCRC32() fn.
        //test case for setSaleType(), setSalePrice() fn.

        S32 sale_price = 10000;
        LLSaleInfo llsaleinfo(LLSaleInfo::FS_COPY, sale_price);
        const char* sale= "copy";

        LLSD llsd_obj1 = ll_create_sd_from_sale_info(llsaleinfo);
        LLSaleInfo saleinfo1 = ll_sale_info_from_sd(llsd_obj1);

        CHECK(llsaleinfo.getSaleType() == LLSaleInfo::FS_COPY);
        CHECK(llsaleinfo.isForSale());
        CHECK(sale_price == llsaleinfo.getSalePrice());
        CHECK(llsaleinfo.getCRC32() == 235833404);
        CHECK(llsaleinfo.lookup(sale) == LLSaleInfo::FS_COPY);
        CHECK(llsaleinfo.getSalePrice() == saleinfo1.getSalePrice());
        CHECK(llsaleinfo.getSaleType() == saleinfo1.getSaleType());

        llsaleinfo.setSalePrice(10000000);
        llsaleinfo.setSaleType(LLSaleInfo::FS_ORIGINAL);
        sale = "cntn";
        llsd_obj1 = ll_create_sd_from_sale_info(llsaleinfo);
        saleinfo1 = ll_sale_info_from_sd(llsd_obj1);

        CHECK(llsaleinfo.getSaleType() == LLSaleInfo::FS_ORIGINAL);
        CHECK(llsaleinfo.isForSale());
        CHECK(llsaleinfo.getSalePrice() == 10000000);
        CHECK(llsaleinfo.getCRC32() == 127911702);
        CHECK(llsaleinfo.lookup(sale) == LLSaleInfo::FS_CONTENTS);
        CHECK(llsaleinfo.getSalePrice() == saleinfo1.getSalePrice());
        CHECK(llsaleinfo.getSaleType() == saleinfo1.getSaleType());

        llsaleinfo.setSalePrice(55000550);
        llsaleinfo.setSaleType(LLSaleInfo::FS_CONTENTS);
        sale = "orig";
        llsd_obj1 = ll_create_sd_from_sale_info(llsaleinfo);
        saleinfo1 = ll_sale_info_from_sd(llsd_obj1);

        CHECK(llsaleinfo.getSaleType() == LLSaleInfo::FS_CONTENTS);
        CHECK(llsaleinfo.isForSale());
        CHECK(llsaleinfo.getSalePrice() == 55000550);
        CHECK(llsaleinfo.getCRC32() == 408735656);
        CHECK(llsaleinfo.lookup(sale) == LLSaleInfo::FS_ORIGINAL);
        CHECK(llsaleinfo.getSalePrice() == saleinfo1.getSalePrice());
        CHECK(llsaleinfo.getSaleType() == saleinfo1.getSaleType());

        llsaleinfo.setSalePrice(-6432);
        llsaleinfo.setSaleType(LLSaleInfo::FS_NOT);
        sale = "not";
        llsd_obj1 = ll_create_sd_from_sale_info(llsaleinfo);
        saleinfo1 = ll_sale_info_from_sd(llsd_obj1);

        CHECK(llsaleinfo.getSaleType() == LLSaleInfo::FS_NOT);
        CHECK_FALSE(llsaleinfo.isForSale());
        CHECK(llsaleinfo.getSalePrice() == 0);
        CHECK(llsaleinfo.getCRC32() == 0);
        CHECK(llsaleinfo.lookup(sale) == LLSaleInfo::FS_NOT);
        CHECK(llsaleinfo.getSalePrice() == saleinfo1.getSalePrice());
        CHECK(llsaleinfo.getSaleType() == saleinfo1.getSaleType());
    }

    TEST_CASE("llsaleinfo: the legacy stream export and import round-trip", "[llsaleinfo]")
    {
        S32 sale_price = 525452;
        LLSaleInfo llsaleinfo(LLSaleInfo::FS_ORIGINAL, sale_price);

        std::ostringstream ostream;
        llsaleinfo.exportLegacyStream(ostream);

        std::istringstream istream(ostream.str());
        LLSaleInfo llsaleinfo1;
        U32 perm_mask = 0;
        bool has_perm_mask = false;
        llsaleinfo1.importLegacyStream(istream, has_perm_mask, perm_mask);

        CHECK(llsaleinfo.getSalePrice() == llsaleinfo1.getSalePrice());
        CHECK(llsaleinfo.getSaleType() == llsaleinfo1.getSaleType());
    }

    TEST_CASE("llsaleinfo: asLLSD and fromLLSD round-trip", "[llsaleinfo]")
    {
        S32 sale_price = 99000;
        LLSaleInfo saleinfo(LLSaleInfo::FS_ORIGINAL, sale_price);

        LLSD sd_result = saleinfo.asLLSD();

        U32 perm_mask = 0 ;
        bool has_perm_mask = false;

        LLSaleInfo saleinfo1;
        saleinfo1.fromLLSD( sd_result, has_perm_mask, perm_mask);

        CHECK(saleinfo.getSalePrice() == saleinfo1.getSalePrice());
        CHECK(saleinfo.getSaleType() == saleinfo1.getSaleType());
    }

    //static EForSale lookup(const char* name) fn test
    TEST_CASE("llsaleinfo: lookup maps names to sale types and sale types to names", "[llsaleinfo]")
    {
        S32 sale_price = 233223;
        LLSaleInfo::EForSale ret_type = LLSaleInfo::lookup("orig");

        CHECK(ret_type == LLSaleInfo::FS_ORIGINAL);

        LLSaleInfo saleinfo(LLSaleInfo::FS_COPY, sale_price);
        const char* result = LLSaleInfo::lookup(saleinfo.getSaleType());
        REQUIRE(result != nullptr);
        CHECK(std::string_view(result) == "copy");
    }

    //void LLSaleInfo::accumulate(const LLSaleInfo& sale_info) fn test
    TEST_CASE("llsaleinfo: accumulate adds the prices", "[llsaleinfo]")
    {
        S32 sale_price = 20;
        LLSaleInfo saleinfo(LLSaleInfo::FS_COPY, sale_price);
        LLSaleInfo saleinfo1(LLSaleInfo::FS_COPY, sale_price);
        saleinfo1.accumulate(saleinfo);
        CHECK(saleinfo1.getSalePrice() == 40);

    }

    // test cases of bool operator==(const LLSaleInfo &rhs) fn
    // test case of bool operator!=(const LLSaleInfo &rhs) fn
    TEST_CASE("llsaleinfo: operator== and operator!= tell sale infos apart by price", "[llsaleinfo]")
    {
        S32 sale_price = 55000;
        LLSaleInfo saleinfo(LLSaleInfo::FS_ORIGINAL, sale_price);
        LLSaleInfo saleinfoequal(LLSaleInfo::FS_ORIGINAL, sale_price);
        LLSaleInfo saleinfonotequal(LLSaleInfo::FS_ORIGINAL, sale_price*2);

        CHECK(saleinfo == saleinfoequal);
        CHECK(saleinfo != saleinfonotequal);
    }

    // The LLSD forms of packMessage and unpackMessage. The LLMessageSystem
    // forms need a message system built from the message template, which
    // this test does not have.
    TEST_CASE("llsaleinfo: the LLSD packMessage and unpackMessage round-trip", "[llsaleinfo]")
    {
        LLSaleInfo saleinfo(LLSaleInfo::FS_CONTENTS, 4321);
        LLSD message = saleinfo.packMessage();

        LLSaleInfo saleinfo1;
        saleinfo1.unpackMessage(message);

        CHECK(saleinfo1.getSaleType() == LLSaleInfo::FS_CONTENTS);
        CHECK(saleinfo1.getSalePrice() == 4321);
    }
}
