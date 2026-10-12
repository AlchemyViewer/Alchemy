/**
 * @file LLSaleInfo_tut.cpp
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

        UNSCOPED_INFO("1. The getSaleType() fn failed");
        CHECK(llsaleinfo.getSaleType() == LLSaleInfo::FS_COPY);
        UNSCOPED_INFO("2. LLSaleInfo::isForSale() fn failed");
        CHECK(llsaleinfo.isForSale());
        UNSCOPED_INFO("3. The getSalePrice() fn failed");
        CHECK(sale_price == llsaleinfo.getSalePrice());
        UNSCOPED_INFO("4. The getCRC32() fn failed");
        CHECK(llsaleinfo.getCRC32() == 235833404);
        UNSCOPED_INFO("5. LLSaleInfo::lookup(const char* name) fn failed");
        CHECK(llsaleinfo.lookup(sale) == LLSaleInfo::FS_COPY);
        UNSCOPED_INFO("6. ll_create_sd_from_sale_info() fn failed");
        CHECK(llsaleinfo.getSalePrice() == saleinfo1.getSalePrice());
        UNSCOPED_INFO("7. ll_create_sd_from_sale_info() fn failed");
        CHECK(llsaleinfo.getSaleType() == saleinfo1.getSaleType());

        llsaleinfo.setSalePrice(10000000);
        llsaleinfo.setSaleType(LLSaleInfo::FS_ORIGINAL);
        sale = "cntn";
        llsd_obj1 = ll_create_sd_from_sale_info(llsaleinfo);
        saleinfo1 = ll_sale_info_from_sd(llsd_obj1);

        UNSCOPED_INFO("8. The getSaleType() and setSaleType() fn failed");
        CHECK(llsaleinfo.getSaleType() == LLSaleInfo::FS_ORIGINAL);
        UNSCOPED_INFO("9. LLSaleInfo::isForSale() fn failed");
        CHECK(llsaleinfo.isForSale());
        UNSCOPED_INFO("10. The getSalePrice() fn failed");
        CHECK(llsaleinfo.getSalePrice() == 10000000);
        UNSCOPED_INFO("11. The getCRC32() fn failed");
        CHECK(llsaleinfo.getCRC32() == 127911702);
        UNSCOPED_INFO("12. LLSaleInfo::lookup(const char* name) fn failed");
        CHECK(llsaleinfo.lookup(sale) == LLSaleInfo::FS_CONTENTS);
        UNSCOPED_INFO("13. ll_create_sd_from_sale_info() fn failed");
        CHECK(llsaleinfo.getSalePrice() == saleinfo1.getSalePrice());
        UNSCOPED_INFO("14. ll_create_sd_from_sale_info() fn failed");
        CHECK(llsaleinfo.getSaleType() == saleinfo1.getSaleType());

        llsaleinfo.setSalePrice(55000550);
        llsaleinfo.setSaleType(LLSaleInfo::FS_CONTENTS);
        sale = "orig";
        llsd_obj1 = ll_create_sd_from_sale_info(llsaleinfo);
        saleinfo1 = ll_sale_info_from_sd(llsd_obj1);

        UNSCOPED_INFO("15. The getSaleType() and setSaleType() fn failed");
        CHECK(llsaleinfo.getSaleType() == LLSaleInfo::FS_CONTENTS);
        UNSCOPED_INFO("16. LLSaleInfo::isForSale() fn failed");
        CHECK(llsaleinfo.isForSale());
        UNSCOPED_INFO("17. The getSalePrice() fn failed");
        CHECK(llsaleinfo.getSalePrice() == 55000550);
        UNSCOPED_INFO("18. The getCRC32() fn failed");
        CHECK(llsaleinfo.getCRC32() == 408735656);
        UNSCOPED_INFO("19. LLSaleInfo::lookup(const char* name) fn failed");
        CHECK(llsaleinfo.lookup(sale) == LLSaleInfo::FS_ORIGINAL);
        UNSCOPED_INFO("20. ll_create_sd_from_sale_info() fn failed");
        CHECK(llsaleinfo.getSalePrice() == saleinfo1.getSalePrice());
        UNSCOPED_INFO("21. ll_create_sd_from_sale_info() fn failed");
        CHECK(llsaleinfo.getSaleType() == saleinfo1.getSaleType());

        llsaleinfo.setSalePrice(-6432);
        llsaleinfo.setSaleType(LLSaleInfo::FS_NOT);
        sale = "not";
        llsd_obj1 = ll_create_sd_from_sale_info(llsaleinfo);
        saleinfo1 = ll_sale_info_from_sd(llsd_obj1);

        UNSCOPED_INFO("22. The getSaleType() and setSaleType() fn failed");
        CHECK(llsaleinfo.getSaleType() == LLSaleInfo::FS_NOT);
        UNSCOPED_INFO("23. LLSaleInfo::isForSale() fn failed");
        CHECK_FALSE(llsaleinfo.isForSale());
        UNSCOPED_INFO("24. The getSalePrice() fn failed");
        CHECK(llsaleinfo.getSalePrice() == 0);
        UNSCOPED_INFO("25. The getCRC32() fn failed");
        CHECK(llsaleinfo.getCRC32() == 0);
        UNSCOPED_INFO("26. LLSaleInfo::lookup(const char* name) fn failed");
        CHECK(llsaleinfo.lookup(sale) == LLSaleInfo::FS_NOT);
        UNSCOPED_INFO("27. ll_create_sd_from_sale_info() fn failed");
        CHECK(llsaleinfo.getSalePrice() == saleinfo1.getSalePrice());
        UNSCOPED_INFO("28. ll_create_sd_from_sale_info() fn failed");
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

        {
            INFO("importStream() fn failed ");
            CHECK(llsaleinfo.getSalePrice() == llsaleinfo1.getSalePrice());
            CHECK(llsaleinfo.getSaleType() == llsaleinfo1.getSaleType());
        }
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

        UNSCOPED_INFO("asLLSD and fromLLSD failed");
        CHECK(saleinfo.getSalePrice() == saleinfo1.getSalePrice());
        UNSCOPED_INFO("asLLSD and fromLLSD failed");
        CHECK(saleinfo.getSaleType() == saleinfo1.getSaleType());
    }

    //static EForSale lookup(const char* name) fn test
    TEST_CASE("llsaleinfo: lookup maps names to sale types and sale types to names", "[llsaleinfo]")
    {
        S32 sale_price = 233223;
        LLSaleInfo::EForSale ret_type = LLSaleInfo::lookup("orig");

        UNSCOPED_INFO("lookup(const char* name) fn failed");
        CHECK(ret_type == LLSaleInfo::FS_ORIGINAL);

        LLSaleInfo saleinfo(LLSaleInfo::FS_COPY, sale_price);
        const char* result = LLSaleInfo::lookup(LLSaleInfo::FS_COPY);
        UNSCOPED_INFO("char* lookup(EForSale type) fn failed");
        CHECK(std::string_view(result) == "copy");
    }

    //void LLSaleInfo::accumulate(const LLSaleInfo& sale_info) fn test
    TEST_CASE("llsaleinfo: accumulate adds the prices", "[llsaleinfo]")
    {
        S32 sale_price = 20;
        LLSaleInfo saleinfo(LLSaleInfo::FS_COPY, sale_price);
        LLSaleInfo saleinfo1(LLSaleInfo::FS_COPY, sale_price);
        saleinfo1.accumulate(saleinfo);
        UNSCOPED_INFO("LLSaleInfo::accumulate(const LLSaleInfo& sale_info) fn failed");
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

        UNSCOPED_INFO("operator == fn. failed");
        CHECK(saleinfo == saleinfoequal);
        UNSCOPED_INFO("operator != fn. failed");
        CHECK(saleinfo != saleinfonotequal);
    }

    TEST_CASE("llsaleinfo: placeholder for the untested message packing", "[llsaleinfo]")
    {

        //TBD: void LLSaleInfo::packMessage(LLMessageSystem* msg) const
        //TBD: void LLSaleInfo::unpackMessage(LLMessageSystem* msg, const char* block)
        //TBD: void LLSaleInfo::unpackMultiMessage(LLMessageSystem* msg, const char* block, S32 block_num)
    }

}
