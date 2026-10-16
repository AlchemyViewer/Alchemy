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
#include <memory>
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
        UNSCOPED_INFO("8 fraction bits");
        CHECK_THAT(f_val1, ll_test::WithinBits(f_unpkval1, 8));
        UNSCOPED_INFO("16 fraction bits");
        CHECK_THAT(f_val2, ll_test::WithinBits(f_unpkval2, 16));
        UNSCOPED_INFO("23 fraction bits");
        CHECK_THAT(f_val3, ll_test::WithinBits(f_unpkval3, 23));
    }

    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer round-trips every packed type", "[datapacker_test]")
    {
        U8 packbuf[1024];

        char str[] = "SecondLife is virtual World\0";
        char strBinary[] = "SecondLife is virtual World";
        char strBinaryFixed[] = "Fixed Data";
        S32 sizeBinary = sizeof(strBinary);
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
        // Zeroed, so a short or failed unpack reads as a short string
        // rather than as whatever the stack held.
        char unpkstrBinary[256] = {};
        char unpkstrBinaryFixed[256] = {};
        S32 unpksizeBinary = 0;
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
        lldp.packBinaryData((U8*)strBinary, sizeBinary, "linden_lab_bd");
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

        CHECK(std::string_view(str) == std::string_view(unpkstr.c_str()));
        CHECK(unpksizeBinary == sizeBinary);
        CHECK(std::string_view(strBinary) == std::string_view(unpkstrBinary));
        CHECK(std::string_view(strBinaryFixed) == std::string_view(unpkstrBinaryFixed));
        CHECK(valU8 == unpkvalU8);
        CHECK(valU16 == unpkvalU16);
        CHECK(valU32 == unpkvalU32);
        CHECK(valS32 == unpkvalS32);
        CHECK(is_approx_equal(valF32, unpkvalF32));
        CHECK(llcol4 == unpkllcol4);
        CHECK(llcol4u == unpkllcol4u);
        CHECK(llvec2 == unpkllvec2);
        CHECK(llvec3 == unpkllvec3);
        CHECK(llvec4 == unpkllvec4);
        CHECK(uuid == unpkuuid);
    }

    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer sizes after packString and reset", "[datapacker_test]")
    {
        U8 packbuf[128];
        char str[] = "SecondLife is virtual World";
        S32 strSize = sizeof(str); // include '\0'
        LLDataPackerBinaryBuffer lldp(packbuf, 128);
        lldp.packString(str , "linden_lab");

        CHECK(strSize == lldp.getCurrentSize());
        CHECK(lldp.getBufferSize() == 128);

        lldp.reset();
        CHECK(lldp.getCurrentSize() == 0);
    }

    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer freeBuffer leaves a zero buffer size", "[datapacker_test]")
    {
        // freeBuffer deletes it, so it comes from new[] and is the packer's.
        U8* packbuf = new U8[128];
        char str[] = "SecondLife is virtual World";
        LLDataPackerBinaryBuffer lldp(packbuf, 128);
        lldp.packString(str , "linden_lab");
        lldp.freeBuffer();
        CHECK(lldp.getBufferSize() == 0);

    }

    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer assignBuffer takes the new size and rewinds", "[datapacker_test]")
    {
        const char first[] = "SecondLife is virtual World";
        const S32 first_size = sizeof(first);
        const S32 second_size = sizeof("Its Amazing");

        // assignBuffer deletes the buffer it replaces, so the first comes
        // from new[] and is the packer's. Nothing deletes the second.
        LLDataPackerBinaryBuffer lldp(new U8[first_size], first_size);
        lldp.packString(first, "linden_lab");
        // Moved on, so that the rewind below has something to undo.
        CHECK(lldp.getCurrentSize() == first_size);

        std::unique_ptr<U8[]> new_buf = std::make_unique<U8[]>(second_size);
        lldp.assignBuffer(new_buf.get(), second_size);
        CHECK(lldp.getBufferSize() == second_size);
        CHECK(lldp.getCurrentSize() == 0);
    }

    TEST_CASE("datapacker_test: LLDataPackerBinaryBuffer operator= copies the packed buffer", "[datapacker_test]")
    {
        U8 packbuf[128];
        char str[] = "SecondLife is virtual World";
        LLDataPackerBinaryBuffer lldp(packbuf, 128);
        lldp.packString(str , "linden_lab");
        // Zeroed, so that a copy that did not happen reads as an empty
        // string rather than as whatever the stack held.
        U8 new_buffer[128] = {};
        std::string unpkbuf;
        LLDataPackerBinaryBuffer lldp1(new_buffer,128);
        lldp1 = lldp;
        lldp1.unpackString(unpkbuf, "linden_lab");
        CHECK(lldp1.getBufferSize() == lldp.getBufferSize());
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
        CHECK_THAT(f_val, ll_test::WithinBits(f_unpkval, 8));
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer round-trips every packed type", "[datapacker_test]")
    {
        char packbuf[1024];

        char str[] = "SecondLife is virtual World\0";
        char strBinary[] = "SecondLife is virtual World";
        char strBinaryFixed[] = "Fixed Data";
        S32 sizeBinary = sizeof(strBinary);
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
        // Zeroed, so a short or failed unpack reads as a short string
        // rather than as whatever the stack held.
        char unpkstrBinary[256] = {};
        char unpkstrBinaryFixed[256] = {};
        S32 unpksizeBinary = 0;
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
        lldp.packBinaryData((U8*)strBinary, sizeBinary, "linden_lab_bd");
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

        CHECK(std::string_view(str) == std::string_view(unpkstr.c_str()));
        CHECK(unpksizeBinary == sizeBinary);
        CHECK(std::string_view(strBinary) == std::string_view(unpkstrBinary));
        CHECK(std::string_view(strBinaryFixed) == std::string_view(unpkstrBinaryFixed));
        CHECK(valU8 == unpkvalU8);
        CHECK(valU16 == unpkvalU16);
        CHECK(valU32 == unpkvalU32);
        CHECK(valS32 == unpkvalS32);
        CHECK(is_approx_equal(valF32, unpkvalF32));
        CHECK(llcol4 == unpkllcol4);
        CHECK(llcol4u == unpkllcol4u);
        CHECK(llvec2 == unpkllvec2);
        CHECK(llvec3 == unpkllvec3);
        CHECK(llvec4 == unpkllvec4);
        CHECK(uuid == unpkuuid);
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer freeBuffer leaves a zero buffer size", "[datapacker_test]")
    {
        // freeBuffer deletes it, so it comes from new[] and is the packer's.
        char* packbuf = new char[128];
        char str[] = "SecondLife is virtual World";
        LLDataPackerAsciiBuffer lldp(packbuf, 128);
        lldp.packString(str , "linden_lab");
        lldp.freeBuffer();
        CHECK(lldp.getBufferSize() == 0);
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer assignBuffer takes the new size and rewinds", "[datapacker_test]")
    {
        char buf[] = "SecondLife is virtual World";
        S32 size = sizeof(buf);
        LLDataPackerAsciiBuffer lldp(buf, size);
        lldp.packS32(42, "linden_lab");
        // Moved on, so that the rewind below has something to undo: "42\n",
        // and the terminator an ascii buffer counts in its size.
        CHECK(lldp.getCurrentSize() == 4);

        char new_buf[] = "Its Amazing";
        size = sizeof(new_buf);
        lldp.assignBuffer(new_buf, size);
        CHECK(lldp.getBufferSize() == size);
        // Nothing but the terminator.
        CHECK(lldp.getCurrentSize() == 1);
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

        CHECK_THAT(f_val, ll_test::WithinBits(f_unpkval, 8));
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiFile round-trips every packed type through a file", "[datapacker_test]")
    {
        char str[] = "SecondLife is virtual World\0";
        char strBinary[] = "SecondLife is virtual World";
        char strBinaryFixed[] = "Fixed Data";
        S32 sizeBinary = sizeof(strBinary);
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
        // Zeroed, so a short or failed unpack reads as a short string
        // rather than as whatever the stack held.
        char unpkstrBinary[256] = {};
        char unpkstrBinaryFixed[256] = {};
        S32 unpksizeBinary = 0;
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
        lldp.packBinaryData((U8*)strBinary, sizeBinary, "linden_lab_bd");
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

        CHECK(std::string_view(str) == std::string_view(unpkstr.c_str()));
        CHECK(unpksizeBinary == sizeBinary);
        CHECK(std::string_view(strBinary) == std::string_view(unpkstrBinary));
        CHECK(std::string_view(strBinaryFixed) == std::string_view(unpkstrBinaryFixed));
        CHECK(valU8 == unpkvalU8);
        CHECK(valU16 == unpkvalU16);
        CHECK(valU32 == unpkvalU32);
        CHECK(valS32 == unpkvalS32);
        CHECK(is_approx_equal(valF32, unpkvalF32));
        CHECK(llcol4 == unpkllcol4);
        CHECK(llcol4u == unpkllcol4u);
        CHECK(llvec2 == unpkllvec2);
        CHECK(llvec3 == unpkllvec3);
        CHECK(llvec4 == unpkllvec4);
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

        CHECK_THAT(f_unpkval, ll_test::WithinBits(f_val, 8));
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiFile round-trips every type through string streams", "[datapacker_test]")
    {
        const char str[] = "SecondLife is virtual World";
        const char strBinary[] = "SecondLife is virtual World";
        const char strBinaryFixed[] = "Fixed Data";
        S32 sizeBinary = sizeof(strBinary);
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
        // Zeroed, so a short or failed unpack reads as a short string
        // rather than as whatever the stack held.
        char unpkstrBinary[256] = {};
        char unpkstrBinaryFixed[256] = {};
        S32 unpksizeBinary = 0;
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
        lldp.packBinaryData((U8*)strBinary, sizeBinary, "linden_lab_bd");
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

        CHECK(std::string_view(str) == std::string_view(unpkstr.c_str()));
        CHECK(unpksizeBinary == sizeBinary);
        CHECK(std::string_view(strBinary) == std::string_view(unpkstrBinary));
        CHECK(std::string_view(strBinaryFixed) == std::string_view(unpkstrBinaryFixed));
        CHECK(valU8 == unpkvalU8);
        CHECK(valU16 == unpkvalU16);
        CHECK(valU32 == unpkvalU32);
        CHECK(valS32 == unpkvalS32);
        CHECK(is_approx_equal(valF32, unpkvalF32));
        CHECK(llcol4 == unpkllcol4);
        CHECK(llcol4u == unpkllcol4u);
        CHECK(llvec2 == unpkllvec2);
        CHECK(llvec3 == unpkllvec3);
        CHECK(llvec4 == unpkllvec4);
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
        CHECK(writer.packS32(42, "first"));
        CHECK(writer.packString("second", "second"));

        const S32 written = writer.getCurrentSize();

        LLDataPackerAsciiBuffer reader(buffer, written);
        S32 first = 0;
        std::string second;
        CHECK(reader.unpackS32(first, "first"));
        CHECK(first == 42);
        REQUIRE(reader.unpackString(second, "second"));
        CHECK(second == std::string("second"));

        S32 past_the_end = 7;
        UNSCOPED_INFO("a read past the end is refused");
        CHECK_FALSE(reader.unpackS32(past_the_end, "third"));
        UNSCOPED_INFO("and leaves its argument alone");
        CHECK(past_the_end == 7);

        std::string also_past = "untouched";
        UNSCOPED_INFO("a second read past the end is refused too");
        CHECK_FALSE(reader.unpackString(also_past, "fourth"));
        UNSCOPED_INFO("and leaves its argument alone too");
        CHECK(also_past == std::string("untouched"));
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer refuses the first read of an empty buffer", "[datapacker_test]")
    {
        // An empty buffer is refused from the first read rather than from the
        // second.
        char buffer[4] = { 0, 0, 0, 0 };
        LLDataPackerAsciiBuffer reader(buffer, 0);

        S32 value = 11;
        CHECK_FALSE(reader.unpackS32(value, "anything"));
        UNSCOPED_INFO("the refused read leaves its argument alone");
        CHECK(value == 11);
    }

    // A line of binary data that claims more bytes than it holds gives what
    // it holds and fails, rather than reading on past the end of the line.
    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer unpackBinaryData reads no further than its line", "[datapacker_test]")
    {
        char text[] = "0000000100 01 02 \n";
        LLDataPackerAsciiBuffer reader(text, sizeof(text));
        U8 value[128] = {};
        S32 size = -1;
        CHECK_FALSE(reader.unpackBinaryData(value, sizeof(value), size, "data"));
        CHECK(size == 2);
        CHECK(value[0] == 1);
        CHECK(value[1] == 2);
        CHECK(value[2] == 0);
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer unpackBinaryDataFixed reads no further than its line", "[datapacker_test]")
    {
        char text[] = "01 02 \n";
        LLDataPackerAsciiBuffer reader(text, sizeof(text));
        U8 value[4] = {};
        CHECK_FALSE(reader.unpackBinaryDataFixed(value, sizeof(value), "data"));
        CHECK(value[0] == 1);
        CHECK(value[1] == 2);
        CHECK(value[2] == 0);
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiBuffer unpackBinaryData refuses a line with no size", "[datapacker_test]")
    {
        char text[] = "lots 01 02 \n";
        LLDataPackerAsciiBuffer reader(text, sizeof(text));
        U8 value[4] = {};
        S32 size = -1;
        CHECK_FALSE(reader.unpackBinaryData(value, sizeof(value), size, "data"));
        CHECK(size == 0);
    }

    TEST_CASE("datapacker_test: LLDataPackerAsciiFile unpackBinaryData and unpackBinaryDataFixed read no further than their lines", "[datapacker_test]")
    {
        std::istringstream istr("data\t0000000100 01 02 \nfixed\t01 02 \n");
        LLDataPackerAsciiFile reader(istr, 2);
        U8 value[128] = {};
        S32 size = -1;
        CHECK_FALSE(reader.unpackBinaryData(value, sizeof(value), size, "data"));
        CHECK(size == 2);
        CHECK(value[2] == 0);

        U8 fixed[4] = {};
        CHECK_FALSE(reader.unpackBinaryDataFixed(fixed, sizeof(fixed), "fixed"));
        CHECK(fixed[1] == 2);
        CHECK(fixed[2] == 0);
    }
}
