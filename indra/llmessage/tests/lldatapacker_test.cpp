/**
 * @file lldatapacker_tut.cpp
 * @author Adroit
 * @date 2007-02
 * @brief LLDataPacker test cases.
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

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include "altestmatchers.h"

#include "lldatapacker.h"
#include "v4color.h"
#include "v4coloru.h"
#include "v2math.h"
#include "v3math.h"
#include "v4math.h"
#include "llsdserialize.h"
#include "../test/namedtempfile.h"


namespace
{
    //*********LLDataPackerBinaryBuffer
    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer packFixed round-trips at three widths", "[datapacker_test]")
    {
        U8 packbuf[128];
        F32 f_val1 = 44.44f, f_unpkval1;
        F32 f_val2 = 12344.443232f, f_unpkval2;
        F32 f_val3 = 44.4456789f, f_unpkval3;
        LLDataPackerBinaryBuffer lldp(packbuf,128);
        lldp.packFixed( f_val1, "linden_lab", false, 8, 8);
        lldp.packFixed( f_val2, "linden_lab", false, 14, 16);
        lldp.packFixed( f_val3, "linden_lab", false, 8, 23);

        LLDataPackerBinaryBuffer lldp1(packbuf, lldp.getCurrentSize());
        lldp1.unpackFixed(f_unpkval1, "linden_lab", false, 8, 8);
        lldp1.unpackFixed(f_unpkval2, "linden_lab", false, 14, 16);
        lldp1.unpackFixed(f_unpkval3, "linden_lab", false, 8, 23);
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packFixed 8 failed");
        CHECK_THAT(f_val1, ll_test::WithinBits(f_unpkval1, 8));
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packFixed 16 failed");
        CHECK_THAT(f_val2, ll_test::WithinBits(f_unpkval2, 16));
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packFixed 23 failed");
        CHECK_THAT(f_val3, ll_test::WithinBits(f_unpkval3, 31));
    }

    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer round-trips every packed type", "[datapacker_test]")
    {
        U8 packbuf[1024];

        char str[] = "SecondLife is virtual World\0";
        char strBinary[] = "SecondLife is virtual World";
        char strBinaryFixed[] = "Fixed Data";
        S32 sizeBinaryFixed = sizeof(strBinaryFixed);
        U8 valU8 = 'C';
        U16 valU16 = 0xFFFF;
        U32 valU32 = 0xFFFFFFFF;
        S32 valS32 = -94967295;
        F32 valF32 = 4354355.44f ;
        LLColor4 llcol4(3.3f, 0, 4.4f, 5.5f);
        LLColor4U llcol4u(3, 128, 24, 33);
        LLVector2 llvec2(333.33f, 444.44f);
        LLVector3 llvec3(333.33f, 444.44f, 555.55f);
        LLVector4 llvec4(333.33f, 444.44f, 555.55f, 666.66f);
        LLUUID uuid;

        std::string unpkstr;
        char unpkstrBinary[256];
        char unpkstrBinaryFixed[256];
        S32 unpksizeBinary;
        U8 unpkvalU8;
        U16 unpkvalU16;
        U32 unpkvalU32;
        S32 unpkvalS32;
        F32 unpkvalF32;
        LLColor4 unpkllcol4;
        LLColor4U unpkllcol4u;
        LLVector2 unpkllvec2;
        LLVector3 unpkllvec3;
        LLVector4 unpkllvec4;
        LLUUID unpkuuid;

        LLDataPackerBinaryBuffer lldp(packbuf,1024);
        lldp.packString(str , "linden_lab_str");
        lldp.packBinaryData((U8*)strBinary, sizeof(strBinary), "linden_lab_bd");
        lldp.packBinaryDataFixed((U8*)strBinaryFixed, sizeBinaryFixed, "linden_lab_bdf");
        lldp.packU8(valU8,"linden_lab_u8");
        lldp.packU16(valU16,"linden_lab_u16");
        lldp.packU32(valU32, "linden_lab_u32");
        lldp.packS32(valS32, "linden_lab_s32");
        lldp.packF32(valF32, "linden_lab_f32");
        lldp.packColor4(llcol4, "linden_lab_col4");
        lldp.packColor4U(llcol4u, "linden_lab_col4u");
        lldp.packVector2(llvec2, "linden_lab_vec2");
        lldp.packVector3(llvec3, "linden_lab_vec3");
        lldp.packVector4(llvec4, "linden_lab_vec4");
        uuid.generate();
        lldp.packUUID(uuid, "linden_lab_uuid");

        S32 cur_size = lldp.getCurrentSize();

        LLDataPackerBinaryBuffer lldp1(packbuf, cur_size);
        lldp1.unpackString(unpkstr , "linden_lab_str");
        lldp1.unpackBinaryData((U8*)unpkstrBinary, 256, unpksizeBinary, "linden_lab_bd");
        lldp1.unpackBinaryDataFixed((U8*)unpkstrBinaryFixed, sizeBinaryFixed, "linden_lab_bdf");
        lldp1.unpackU8(unpkvalU8,"linden_lab_u8");
        lldp1.unpackU16(unpkvalU16,"linden_lab_u16");
        lldp1.unpackU32(unpkvalU32, "linden_lab_u32");
        lldp1.unpackS32(unpkvalS32, "linden_lab_s32");
        lldp1.unpackF32(unpkvalF32, "linden_lab_f32");
        lldp1.unpackColor4(unpkllcol4, "linden_lab_col4");
        lldp1.unpackColor4U(unpkllcol4u, "linden_lab_col4u");
        lldp1.unpackVector2(unpkllvec2, "linden_lab_vec2");
        lldp1.unpackVector3(unpkllvec3, "linden_lab_vec3");
        lldp1.unpackVector4(unpkllvec4, "linden_lab_vec4");
        lldp1.unpackUUID(unpkuuid, "linden_lab_uuid");

        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packString failed");
        CHECK(std::string_view(str) == std::string_view(unpkstr.c_str()));
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packBinaryData failed");
        CHECK(std::string_view(strBinary) == std::string_view(unpkstrBinary));
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packBinaryDataFixed failed");
        CHECK(std::string_view(strBinaryFixed) == std::string_view(unpkstrBinaryFixed));
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packU8 failed");
        CHECK(valU8 == unpkvalU8);
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packU16 failed");
        CHECK(valU16 == unpkvalU16);
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packU32 failed");
        CHECK(valU32 == unpkvalU32);
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packS32 failed");
        CHECK(valS32 == unpkvalS32);
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packF32 failed");
        CHECK(is_approx_equal(valF32, unpkvalF32));
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packColor4 failed");
        CHECK(llcol4 == unpkllcol4);
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packColor4U failed");
        CHECK(llcol4u == unpkllcol4u);
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packVector2 failed");
        CHECK(llvec2 == unpkllvec2);
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packVector3 failed");
        CHECK(llvec3 == unpkllvec3);
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packVector4 failed");
        CHECK(llvec4 == unpkllvec4);
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::packUUID failed");
        CHECK(uuid == unpkuuid);
    }

    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer sizes after packString and reset", "[datapacker_test]")
    {
        U8 packbuf[128];
        char str[] = "SecondLife is virtual World";
        S32 strSize = sizeof(str); // include '\0'
        LLDataPackerBinaryBuffer lldp(packbuf, 128);
        lldp.packString(str , "linden_lab");

        UNSCOPED_INFO("LLDataPackerBinaryBuffer: current size is wrong");
        CHECK(strSize == lldp.getCurrentSize());
        UNSCOPED_INFO("LLDataPackerBinaryBuffer: buffer size is wrong");
        CHECK(lldp.getBufferSize() == 128);

        lldp.reset();
        UNSCOPED_INFO("LLDataPackerBinaryBuffer::reset failed");
        CHECK(lldp.getCurrentSize() == 0);
    }

    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer freeBuffer leaves a zero buffer size", "[datapacker_test]")
    {
        U8* packbuf = new U8[128];
        char str[] = "SecondLife is virtual World";
        LLDataPackerBinaryBuffer lldp(packbuf, 128);
        lldp.packString(str , "linden_lab");
        lldp.freeBuffer();
        UNSCOPED_INFO("LLDataPackerBinaryBuffer.freeBuffer failed");
        CHECK(lldp.getBufferSize() == 0);

    }

    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer assignBuffer takes the new size and rewinds", "[datapacker_test]")
    {
        U8* buf = new U8[]("SecondLife is virtual World");
        S32 size = sizeof(buf);
        LLDataPackerBinaryBuffer lldp(buf, size);
        U8* new_buf = new U8[]("Its Amazing");
        size = sizeof(new_buf)-1;
        lldp.assignBuffer(new_buf, size);
        {
            INFO("LLDataPackerBinaryBuffer::assignBuffer failed");
            CHECK(lldp.getBufferSize() == size);
            CHECK(lldp.getCurrentSize() == 0);
        }
    }

    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer operator= copies the packed buffer", "[datapacker_test]")
    {
        U8 packbuf[128];
        char str[] = "SecondLife is virtual World";
        LLDataPackerBinaryBuffer lldp(packbuf, 128);
        lldp.packString(str , "linden_lab");
        U8 new_buffer[128];
        std::string unpkbuf;
        LLDataPackerBinaryBuffer lldp1(new_buffer,128);
        lldp1 = lldp;
        lldp1.unpackString(unpkbuf, "linden_lab");
        UNSCOPED_INFO("1. LLDataPackerBinaryBuffer::operator= failed");
        CHECK(lldp1.getBufferSize() == lldp.getBufferSize());
        UNSCOPED_INFO("2.LLDataPackerBinaryBuffer::operator= failed");
        CHECK(str == unpkbuf);
    }

    //*********LLDataPackerAsciiBuffer

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer packFixed round-trips", "[datapacker_test]")
    {
        char packbuf[128];
        F32 f_val = 44.44f, f_unpkval;
        LLDataPackerAsciiBuffer lldp(packbuf,128);
        lldp.packFixed( f_val, "linden_lab", false, 8, 8);

        LLDataPackerAsciiBuffer lldp1(packbuf, lldp.getCurrentSize());
        lldp1.unpackFixed(f_unpkval, "linden_lab", false, 8, 8);
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packFixed failed");
        CHECK_THAT(f_val, ll_test::WithinBits(f_unpkval, 8));
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer round-trips every packed type", "[datapacker_test]")
    {
        char packbuf[1024];

        char str[] = "SecondLife is virtual World\0";
        char strBinary[] = "SecondLife is virtual World";
        char strBinaryFixed[] = "Fixed Data";
        S32 sizeBinaryFixed = sizeof(strBinaryFixed);
        U8 valU8 = 'C';
        U16 valU16 = 0xFFFF;
        U32 valU32 = 0xFFFFFFFF;
        S32 valS32 = -94967295;
        F32 valF32 = 4354355.44f ;
        LLColor4 llcol4(3.3f, 0, 4.4f, 5.5f);
        LLColor4U llcol4u(3, 128, 24, 33);
        LLVector2 llvec2(333.33f, 444.44f);
        LLVector3 llvec3(333.33f, 444.44f, 555.55f);
        LLVector4 llvec4(4354355.44f, 444.44f, 555.55f, 666.66f);
        LLUUID uuid;

        std::string unpkstr;
        char unpkstrBinary[256];
        char unpkstrBinaryFixed[256];
        S32 unpksizeBinary;
        U8 unpkvalU8;
        U16 unpkvalU16;
        U32 unpkvalU32;
        S32 unpkvalS32;
        F32 unpkvalF32;
        LLColor4 unpkllcol4;
        LLColor4U unpkllcol4u;
        LLVector2 unpkllvec2;
        LLVector3 unpkllvec3;
        LLVector4 unpkllvec4;
        LLUUID unpkuuid;

        LLDataPackerAsciiBuffer lldp(packbuf,1024);
        lldp.packString(str , "linden_lab_str");
        lldp.packBinaryData((U8*)strBinary, sizeof(strBinary), "linden_lab_bd");
        lldp.packBinaryDataFixed((U8*)strBinaryFixed, sizeBinaryFixed, "linden_lab_bdf");
        lldp.packU8(valU8,"linden_lab_u8");
        lldp.packU16(valU16,"linden_lab_u16");
        lldp.packU32(valU32, "linden_lab_u32");
        lldp.packS32(valS32, "linden_lab_s32");
        lldp.packF32(valF32, "linden_lab_f32");
        lldp.packColor4(llcol4, "linden_lab_col4");
        lldp.packColor4U(llcol4u, "linden_lab_col4u");
        lldp.packVector2(llvec2, "linden_lab_vec2");
        lldp.packVector3(llvec3, "linden_lab_vec3");
        lldp.packVector4(llvec4, "linden_lab_vec4");
        uuid.generate();
        lldp.packUUID(uuid, "linden_lab_uuid");

        S32 cur_size = lldp.getCurrentSize();

        LLDataPackerAsciiBuffer lldp1(packbuf, cur_size);
        lldp1.unpackString(unpkstr , "linden_lab_str");
        lldp1.unpackBinaryData((U8*)unpkstrBinary, 256, unpksizeBinary, "linden_lab_bd");
        lldp1.unpackBinaryDataFixed((U8*)unpkstrBinaryFixed, sizeBinaryFixed, "linden_lab_bdf");
        lldp1.unpackU8(unpkvalU8,"linden_lab_u8");
        lldp1.unpackU16(unpkvalU16,"linden_lab_u16");
        lldp1.unpackU32(unpkvalU32, "linden_lab_u32");
        lldp1.unpackS32(unpkvalS32, "linden_lab_s32");
        lldp1.unpackF32(unpkvalF32, "linden_lab_f32");
        lldp1.unpackColor4(unpkllcol4, "linden_lab_col4");
        lldp1.unpackColor4U(unpkllcol4u, "linden_lab_col4u");
        lldp1.unpackVector2(unpkllvec2, "linden_lab_vec2");
        lldp1.unpackVector3(unpkllvec3, "linden_lab_vec3");
        lldp1.unpackVector4(unpkllvec4, "linden_lab_vec4");
        lldp1.unpackUUID(unpkuuid, "linden_lab_uuid");

        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packString failed");
        CHECK(std::string_view(str) == std::string_view(unpkstr.c_str()));
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packBinaryData failed");
        CHECK(std::string_view(strBinary) == std::string_view(unpkstrBinary));
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packBinaryDataFixed failed");
        CHECK(std::string_view(strBinaryFixed) == std::string_view(unpkstrBinaryFixed));
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packU8 failed");
        CHECK(valU8 == unpkvalU8);
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packU16 failed");
        CHECK(valU16 == unpkvalU16);
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packU32 failed");
        CHECK(valU32 == unpkvalU32);
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packS32 failed");
        CHECK(valS32 == unpkvalS32);
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packF32 failed");
        CHECK(is_approx_equal(valF32, unpkvalF32));
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packColor4 failed");
        CHECK(llcol4 == unpkllcol4);
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packColor4U failed");
        CHECK(llcol4u == unpkllcol4u);
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packVector2 failed");
        CHECK(llvec2 == unpkllvec2);
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packVector3 failed");
        CHECK(llvec3 == unpkllvec3);
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packVector4 failed");
        CHECK(llvec4 == unpkllvec4);
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::packUUID failed");
        CHECK(uuid == unpkuuid);
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer freeBuffer leaves a zero buffer size", "[datapacker_test]")
    {
        char* packbuf = new char[128];
        char str[] = "SecondLife is virtual World";
        LLDataPackerAsciiBuffer lldp(packbuf, 128);
        lldp.packString(str , "linden_lab");
        lldp.freeBuffer();
        UNSCOPED_INFO("LLDataPackerAsciiBuffer::freeBuffer failed");
        CHECK(lldp.getBufferSize() == 0);
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer assignBuffer takes the new size and rewinds", "[datapacker_test]")
    {
        char buf[] = "SecondLife is virtual World";
        S32 size = sizeof(buf);
        LLDataPackerAsciiBuffer lldp(buf, size);
        char new_buf[] = "Its Amazing";
        size = sizeof(new_buf);
        lldp.assignBuffer(new_buf, size);
        {
            INFO("LLDataPackerAsciiBuffer::assignBuffer failed");
            CHECK(lldp.getBufferSize() == size);
            CHECK(lldp.getCurrentSize() == 1);
        }
    }

    //*********LLDataPackerAsciiFile

    TEST_CASE("datapacker_test: LLDataPackerAsciiFile packFixed round-trips through a file", "[datapacker_test]")
    {
        F32 f_val = 44.44f, f_unpkval;

        // A file of the test's own, where no other run writes, removed as
        // the test ends.
        NamedTempFile file("datapacker_test", "", ".txt");
        LLFILE* fp = LLFile::fopen(file.getPath().string(), LLFILE_MODE("w+"));
        if(!fp)
        {
            LL_ERRS() << "File couldnt be open" << LL_ENDL;
            return;
        }

        LLDataPackerAsciiFile lldp(fp,2);
        lldp.packFixed( f_val, "linden_lab", false, 8, 8);

        fflush(fp);
        fseek(fp,0,SEEK_SET);
        LLDataPackerAsciiFile lldp1(fp,2);

        lldp1.unpackFixed(f_unpkval, "linden_lab", false, 8, 8);
        fclose(fp);

        UNSCOPED_INFO("LLDataPackerAsciiFile::packFixed failed");
        CHECK_THAT(f_val, ll_test::WithinBits(f_unpkval, 8));
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiFile round-trips every packed type through a file", "[datapacker_test]")
    {
        char str[] = "SecondLife is virtual World\0";
        char strBinary[] = "SecondLife is virtual World";
        char strBinaryFixed[] = "Fixed Data";
        S32 sizeBinaryFixed = sizeof(strBinaryFixed);
        U8 valU8 = 'C';
        U16 valU16 = 0xFFFF;
        U32 valU32 = 0xFFFFFFFF;
        S32 valS32 = -94967295;
        F32 valF32 = 4354355.44f ;
        LLColor4 llcol4(3.3f, 0, 4.4f, 5.5f);
        LLColor4U llcol4u(3, 128, 24, 33);
        LLVector2 llvec2(333.33f, 444.44f);
        LLVector3 llvec3(333.33f, 444.44f, 555.55f);
        LLVector4 llvec4(333.33f, 444.44f, 555.55f, 666.66f);
        LLUUID uuid;

        std::string unpkstr;
        char unpkstrBinary[256];
        char unpkstrBinaryFixed[256];
        S32 unpksizeBinary;
        U8 unpkvalU8;
        U16 unpkvalU16;
        U32 unpkvalU32;
        S32 unpkvalS32;
        F32 unpkvalF32;
        LLColor4 unpkllcol4;
        LLColor4U unpkllcol4u;
        LLVector2 unpkllvec2;
        LLVector3 unpkllvec3;
        LLVector4 unpkllvec4;
        LLUUID unpkuuid;

        // A file of the test's own, where no other run writes, removed as
        // the test ends.
        NamedTempFile file("datapacker_test", "", ".txt");
        LLFILE* fp = LLFile::fopen(file.getPath().string(), LLFILE_MODE("w+"));
        if(!fp)
        {
            LL_ERRS() << "File couldnt be open" << LL_ENDL;
            return;
        }

        LLDataPackerAsciiFile lldp(fp,2);

        lldp.packString(str , "linden_lab_str");
        lldp.packBinaryData((U8*)strBinary, sizeof(strBinary), "linden_lab_bd");
        lldp.packBinaryDataFixed((U8*)strBinaryFixed, sizeBinaryFixed, "linden_lab_bdf");
        lldp.packU8(valU8,"linden_lab_u8");
        lldp.packU16(valU16,"linden_lab_u16");
        lldp.packU32(valU32, "linden_lab_u32");
        lldp.packS32(valS32, "linden_lab_s32");
        lldp.packF32(valF32, "linden_lab_f32");
        lldp.packColor4(llcol4, "linden_lab_col4");
        lldp.packColor4U(llcol4u, "linden_lab_col4u");
        lldp.packVector2(llvec2, "linden_lab_vec2");
        lldp.packVector3(llvec3, "linden_lab_vec3");
        lldp.packVector4(llvec4, "linden_lab_vec4");
        uuid.generate();
        lldp.packUUID(uuid, "linden_lab_uuid");

        fflush(fp);
        fseek(fp,0,SEEK_SET);
        LLDataPackerAsciiFile lldp1(fp,2);

        lldp1.unpackString(unpkstr , "linden_lab_str");
        lldp1.unpackBinaryData((U8*)unpkstrBinary, 256, unpksizeBinary, "linden_lab_bd");
        lldp1.unpackBinaryDataFixed((U8*)unpkstrBinaryFixed, sizeBinaryFixed, "linden_lab_bdf");
        lldp1.unpackU8(unpkvalU8,"linden_lab_u8");
        lldp1.unpackU16(unpkvalU16,"linden_lab_u16");
        lldp1.unpackU32(unpkvalU32, "linden_lab_u32");
        lldp1.unpackS32(unpkvalS32, "linden_lab_s32");
        lldp1.unpackF32(unpkvalF32, "linden_lab_f32");
        lldp1.unpackColor4(unpkllcol4, "linden_lab_col4");
        lldp1.unpackColor4U(unpkllcol4u, "linden_lab_col4u");
        lldp1.unpackVector2(unpkllvec2, "linden_lab_vec2");
        lldp1.unpackVector3(unpkllvec3, "linden_lab_vec3");
        lldp1.unpackVector4(unpkllvec4, "linden_lab_vec4");
        lldp1.unpackUUID(unpkuuid, "linden_lab_uuid");

        fclose(fp);

        UNSCOPED_INFO("LLDataPackerAsciiFile::packString failed");
        CHECK(std::string_view(str) == std::string_view(unpkstr.c_str()));
        UNSCOPED_INFO("LLDataPackerAsciiFile::packBinaryData failed");
        CHECK(std::string_view(strBinary) == std::string_view(unpkstrBinary));
        UNSCOPED_INFO("LLDataPackerAsciiFile::packBinaryDataFixed failed");
        CHECK(std::string_view(strBinaryFixed) == std::string_view(unpkstrBinaryFixed));
        UNSCOPED_INFO("LLDataPackerAsciiFile::packU8 failed");
        CHECK(valU8 == unpkvalU8);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packU16 failed");
        CHECK(valU16 == unpkvalU16);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packU32 failed");
        CHECK(valU32 == unpkvalU32);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packS32 failed");
        CHECK(valS32 == unpkvalS32);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packF32 failed");
        CHECK(is_approx_equal(valF32, unpkvalF32));
        UNSCOPED_INFO("LLDataPackerAsciiFile::packColor4 failed");
        CHECK(llcol4 == unpkllcol4);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packColor4U failed");
        CHECK(llcol4u == unpkllcol4u);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packVector2 failed");
        CHECK(llvec2 == unpkllvec2);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packVector3 failed");
        CHECK(llvec3 == unpkllvec3);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packVector4 failed");
        CHECK(llvec4 == unpkllvec4);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packUUID failed");
        CHECK(uuid == unpkuuid);
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiFile packFixed round-trips through string streams", "[datapacker_test]")
    {
        F32 f_val = 44.44f, f_unpkval;

        std::ostringstream ostr;
        LLDataPackerAsciiFile lldp(ostr,2);
        lldp.packFixed( f_val, "linden_lab", false, 8, 8);

        std::istringstream istr(ostr.str());
        LLDataPackerAsciiFile lldp1(istr,2);

        lldp1.unpackFixed(f_unpkval, "linden_lab", false, 8, 8);

        UNSCOPED_INFO("LLDataPackerAsciiFile::packFixed (iostring) failed");
        CHECK_THAT(f_unpkval, ll_test::WithinBits(f_val, 8));
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiFile round-trips every type through string streams", "[datapacker_test]")
    {
        const char str[] = "SecondLife is virtual World";
        const char strBinary[] = "SecondLife is virtual World";
        const char strBinaryFixed[] = "Fixed Data";
        S32 sizeBinaryFixed = sizeof(strBinaryFixed);
        U8 valU8 = 'C';
        U16 valU16 = 0xFFFF;
        U32 valU32 = 0xFFFFFFFF;
        S32 valS32 = -94967295;
        F32 valF32 = 4354355.44f ;
        LLColor4 llcol4(3.3f, 0, 4.4f, 5.5f);
        LLColor4U llcol4u(3, 128, 24, 33);
        LLVector2 llvec2(3333333.33f, 444.333344f);
        LLVector3 llvec3(3323233.33f, 444.4324f, 555.553232f);
        LLVector4 llvec4(333.33233f, 444.4323234f, 55323225.55f, 6323236.66f);
        LLUUID uuid;

        std::string unpkstr;
        char unpkstrBinary[256];
        char unpkstrBinaryFixed[256];
        S32 unpksizeBinary;
        U8 unpkvalU8;
        U16 unpkvalU16;
        U32 unpkvalU32;
        S32 unpkvalS32;
        F32 unpkvalF32;
        LLColor4 unpkllcol4;
        LLColor4U unpkllcol4u;
        LLVector2 unpkllvec2;
        LLVector3 unpkllvec3;
        LLVector4 unpkllvec4;
        LLUUID unpkuuid;

        std::ostringstream ostr;
        LLDataPackerAsciiFile lldp(ostr,2);

        lldp.packString(str , "linden_lab_str");
        lldp.packBinaryData((U8*)strBinary, sizeof(strBinary), "linden_lab_bd");
        lldp.packBinaryDataFixed((U8*)strBinaryFixed, sizeBinaryFixed, "linden_lab_bdf");
        lldp.packU8(valU8,"linden_lab_u8");
        lldp.packU16(valU16,"linden_lab_u16");
        lldp.packU32(valU32, "linden_lab_u32");
        lldp.packS32(valS32, "linden_lab_s32");
        lldp.packF32(valF32, "linden_lab_f32");
        lldp.packColor4(llcol4, "linden_lab_col4");
        lldp.packColor4U(llcol4u, "linden_lab_col4u");
        lldp.packVector2(llvec2, "linden_lab_vec2");
        lldp.packVector3(llvec3, "linden_lab_vec3");
        lldp.packVector4(llvec4, "linden_lab_vec4");
        uuid.generate();
        lldp.packUUID(uuid, "linden_lab_uuid");

        std::istringstream istr(ostr.str());
        LLDataPackerAsciiFile lldp1(istr,2);

        lldp1.unpackString(unpkstr , "linden_lab_str");
        lldp1.unpackBinaryData((U8*)unpkstrBinary, 256, unpksizeBinary, "linden_lab_bd");
        lldp1.unpackBinaryDataFixed((U8*)unpkstrBinaryFixed, sizeBinaryFixed, "linden_lab_bdf");
        lldp1.unpackU8(unpkvalU8,"linden_lab_u8");
        lldp1.unpackU16(unpkvalU16,"linden_lab_u16");
        lldp1.unpackU32(unpkvalU32, "linden_lab_u32");
        lldp1.unpackS32(unpkvalS32, "linden_lab_s32");
        lldp1.unpackF32(unpkvalF32, "linden_lab_f32");
        lldp1.unpackColor4(unpkllcol4, "linden_lab_col4");
        lldp1.unpackColor4U(unpkllcol4u, "linden_lab_col4u");
        lldp1.unpackVector2(unpkllvec2, "linden_lab_vec2");
        lldp1.unpackVector3(unpkllvec3, "linden_lab_vec3");
        lldp1.unpackVector4(unpkllvec4, "linden_lab_vec4");
        lldp1.unpackUUID(unpkuuid, "linden_lab_uuid");

        UNSCOPED_INFO("LLDataPackerAsciiFile::packString (iostring) failed");
        CHECK(std::string_view(str) == std::string_view(unpkstr.c_str()));
        UNSCOPED_INFO("LLDataPackerAsciiFile::packBinaryData (iostring) failed");
        CHECK(std::string_view(strBinary) == std::string_view(unpkstrBinary));
        UNSCOPED_INFO("LLDataPackerAsciiFile::packBinaryDataFixed (iostring) failed");
        CHECK(std::string_view(strBinaryFixed) == std::string_view(unpkstrBinaryFixed));
        UNSCOPED_INFO("LLDataPackerAsciiFile::packU8 (iostring) failed");
        CHECK(valU8 == unpkvalU8);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packU16 (iostring) failed");
        CHECK(valU16 == unpkvalU16);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packU32 (iostring) failed");
        CHECK(valU32 == unpkvalU32);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packS32 (iostring) failed");
        CHECK(valS32 == unpkvalS32);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packF32 (iostring) failed");
        CHECK(is_approx_equal(valF32, unpkvalF32));
        UNSCOPED_INFO("LLDataPackerAsciiFile::packColor4 (iostring) failed");
        CHECK(llcol4 == unpkllcol4);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packColor4U (iostring) failed");
        CHECK(llcol4u == unpkllcol4u);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packVector2 (iostring) failed");
        CHECK(llvec2 == unpkllvec2);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packVector3 (iostring) failed");
        CHECK(llvec3 == unpkllvec3);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packVector4 (iostring) failed");
        CHECK(llvec4 == unpkllvec4);
        UNSCOPED_INFO("LLDataPackerAsciiFile::packUUID (iostring) failed");
        CHECK(uuid == unpkuuid);
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer refuses every read past the end", "[datapacker_test]")
    {
        // An ascii buffer that has run out says so. Every unpack in that class
        // comes through one place, and each one moves the cursor on by what it
        // read, so a caller that keeps asking past the end used to keep being
        // told yes while the cursor walked off the buffer.
        char buffer[128];
        LLDataPackerAsciiBuffer writer(buffer, sizeof(buffer));
        UNSCOPED_INFO("packS32");
        CHECK(writer.packS32(42, "first"));
        UNSCOPED_INFO("packString");
        CHECK(writer.packString("second", "second"));

        const S32 written = writer.getCurrentSize();

        LLDataPackerAsciiBuffer reader(buffer, written);
        S32 first = 0;
        std::string second;
        UNSCOPED_INFO("the first field reads back");
        CHECK(reader.unpackS32(first, "first"));
        UNSCOPED_INFO("and is what was written");
        CHECK(first == 42);
        UNSCOPED_INFO("the second field reads back");
        REQUIRE(reader.unpackString(second, "second"));
        UNSCOPED_INFO("and is what was written");
        CHECK(second == std::string("second"));

        S32 past_the_end = 7;
        UNSCOPED_INFO("a read past the end is refused");
        CHECK_FALSE(reader.unpackS32(past_the_end, "third"));
        UNSCOPED_INFO("and leaves what it was handed alone");
        CHECK(past_the_end == 7);

        std::string also_past = "untouched";
        UNSCOPED_INFO("and stays refused");
        CHECK_FALSE(reader.unpackString(also_past, "fourth"));
        UNSCOPED_INFO("however many times it is asked");
        CHECK(also_past == std::string("untouched"));
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer refuses the first read of an empty buffer", "[datapacker_test]")
    {
        // An empty buffer is refused from the first read rather than from the
        // second.
        char buffer[4] = { 0, 0, 0, 0 };
        LLDataPackerAsciiBuffer reader(buffer, 0);

        S32 value = 11;
        UNSCOPED_INFO("nothing to read");
        CHECK_FALSE(reader.unpackS32(value, "anything"));
        UNSCOPED_INFO("and nothing was written");
        CHECK(value == 11);
    }
}
