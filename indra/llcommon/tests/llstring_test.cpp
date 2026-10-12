/**
 * @file llstring_test.cpp
 * @author Adroit, Steve Linden, Tofu Linden
 * @date 2006-12-24
 * @brief Test cases of llstring.cpp
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

#include "../llstring.h"
#include "../llsd.h"
#include "StringVec.h"                  // must come BEFORE lltut.h
#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace
{
    TEST_CASE("LLString: std::string constructors give the expected text and size", "[LLString]")
    {
        std::string llstr1;
        {
            INFO("Empty std::string");
            REQUIRE(llstr1.size() == 0);
            REQUIRE(llstr1.empty());
        }

        std::string llstr2("Hello");
        {
            INFO("std::string = Hello");
            CHECK(std::string_view(llstr2.c_str()) == "Hello");
            REQUIRE(llstr2.size() == 5);
            REQUIRE_FALSE(llstr2.empty());
        }

        std::string llstr3(llstr2);
        {
            INFO("std::string = std::string(std::string)");
            CHECK(std::string_view(llstr3.c_str()) == "Hello");
            REQUIRE(llstr3.size() == 5);
            REQUIRE_FALSE(llstr3.empty());
        }

        std::string str("Hello World");
        std::string llstr4(str, 6);
        {
            INFO("std::string = std::string(s, size_type pos, size_type n = npos)");
            CHECK(std::string_view(llstr4.c_str()) == "World");
            REQUIRE(llstr4.size() == 5);
            REQUIRE_FALSE(llstr4.empty());
        }

        std::string llstr5(str, str.size());
        {
            INFO("std::string = std::string(s, size_type pos, size_type n = npos)");
            REQUIRE(llstr5.size() == 0);
            REQUIRE(llstr5.empty());
        }

        std::string llstr6(5, 'A');
        {
            INFO("std::string = std::string(count, c)");
            CHECK(std::string_view(llstr6.c_str()) == "AAAAA");
            REQUIRE(llstr6.size() == 5);
            REQUIRE_FALSE(llstr6.empty());
        }

        std::string llstr7("Hello World", 5);
        {
            INFO("std::string(s, n)");
            CHECK(std::string_view(llstr7.c_str()) == "Hello");
            REQUIRE(llstr7.size() == 5);
            REQUIRE_FALSE(llstr7.empty());
        }

        std::string llstr8("Hello World", 6, 5);
        {
            INFO("std::string(s, n, count)");
            CHECK(std::string_view(llstr8.c_str()) == "World");
            REQUIRE(llstr8.size() == 5);
            REQUIRE_FALSE(llstr8.empty());
        }

        std::string llstr9("Hello World", sizeof("Hello World")-1, 5); // go past end
        {
            INFO("std::string(s, n, count) goes past end");
            REQUIRE(llstr9.size() == 0);
            REQUIRE(llstr9.empty());
        }
    }

    TEST_CASE("LLString: isValidIndex accepts indexes up to and including the length", "[LLString]")
    {
        std::string str("Len=5");
        {
            INFO("isValidIndex failed");
            CHECK(LLStringUtil::isValidIndex(str, 0));
            CHECK(LLStringUtil::isValidIndex(str, 5));
            CHECK_FALSE(LLStringUtil::isValidIndex(str, 6));
        }

        std::string str1;
        UNSCOPED_INFO("isValidIndex failed fo rempty string");
        CHECK_FALSE(LLStringUtil::isValidIndex(str1, 0));
    }

    TEST_CASE("LLString: trimHead removes leading spaces and line ends", "[LLString]")
    {
        std::string str_val("               Testing the extra whitespaces   ");
        LLStringUtil::trimHead(str_val);
        UNSCOPED_INFO("1: trimHead failed");
        CHECK(str_val == "Testing the extra whitespaces   ");

        std::string str_val1("\n\t\r\n  Testing the extra whitespaces   ");
        LLStringUtil::trimHead(str_val1);
        UNSCOPED_INFO("2: trimHead failed");
        CHECK(str_val1 == "Testing the extra whitespaces   ");
    }

    TEST_CASE("LLString: trimTail removes trailing spaces and line ends", "[LLString]")
    {
        std::string str_val("  Testing the   extra     whitespaces         ");
        LLStringUtil::trimTail(str_val);
        UNSCOPED_INFO("1: trimTail failed");
        CHECK(str_val == "  Testing the   extra     whitespaces");

        std::string str_val1("\n  Testing the extra whitespaces  \n\t\r\n   ");
        LLStringUtil::trimTail(str_val1);
        UNSCOPED_INFO("2: trimTail failed");
        CHECK(str_val1 == "\n  Testing the extra whitespaces");
    }


    TEST_CASE("LLString: trim removes whitespace at both ends only", "[LLString]")
    {
        std::string str_val("  \t \r Testing the   extra     \r\n whitespaces     \n \t    ");
        LLStringUtil::trim(str_val);
        UNSCOPED_INFO("1: trim failed");
        CHECK(str_val == "Testing the   extra     \r\n whitespaces");
    }

    TEST_CASE("LLString: truncate shortens to a length and to nothing", "[LLString]")
    {
        std::string str("Second LindenLabs");
        LLStringUtil::truncate(str, 6);
        UNSCOPED_INFO("1: truncate");
        CHECK(str == "Second");

        // further truncate more than the length
        LLStringUtil::truncate(str, 0);
        UNSCOPED_INFO("2: truncate");
        CHECK(str == "");
    }

    TEST_CASE("LLString: toUpper uppercases ASCII", "[LLString]")
    {
        std::string str_val("SecondLife Source");
        LLStringUtil::toUpper(str_val);
        UNSCOPED_INFO("toUpper failed");
        CHECK(str_val == "SECONDLIFE SOURCE");
    }

    TEST_CASE("LLString: toLower lowercases ASCII", "[LLString]")
    {
        std::string str_val("SecondLife Source");
        LLStringUtil::toLower(str_val);
        UNSCOPED_INFO("toLower failed");
        CHECK(str_val == "secondlife source");
    }

    TEST_CASE("LLString: isHead matches a prefix and never an empty string", "[LLString]")
    {
        std::string str_val("Second");
        UNSCOPED_INFO("1. isHead failed");
        CHECK(LLStringUtil::isHead(str_val, "SecondLife Source"));
        UNSCOPED_INFO("2. isHead failed");
        CHECK_FALSE(LLStringUtil::isHead(str_val, " SecondLife Source"));
        std::string str_val2("");
        UNSCOPED_INFO("3. isHead failed");
        CHECK_FALSE(LLStringUtil::isHead(str_val2, ""));
    }

    TEST_CASE("LLString: addCRLF and removeCRLF convert line ends both ways", "[LLString]")
    {
        std::string str_val("Hello.\n\n Lindenlabs. \n This is \na simple test.\n");
        std::string orig_str_val(str_val);
        LLStringUtil::addCRLF(str_val);
        UNSCOPED_INFO("addCRLF failed");
        CHECK(str_val == "Hello.\r\n\r\n Lindenlabs. \r\n This is \r\na simple test.\r\n");
        LLStringUtil::removeCRLF(str_val);
        UNSCOPED_INFO("removeCRLF failed");
        CHECK(str_val == orig_str_val);
    }

    TEST_CASE("LLString: replaceTabsWithSpaces puts n spaces for each tab", "[LLString]")
    {
        std::string str_val("Hello.\n\n\t \t Lindenlabs. \t\t");
        std::string orig_str_val(str_val);
        LLStringUtil::replaceTabsWithSpaces(str_val, 1);
        UNSCOPED_INFO("replaceTabsWithSpaces failed");
        CHECK(str_val == "Hello.\n\n    Lindenlabs.   ");
        LLStringUtil::replaceTabsWithSpaces(orig_str_val, 0);
        UNSCOPED_INFO("replaceTabsWithSpaces failed for 0");
        CHECK(orig_str_val == "Hello.\n\n  Lindenlabs. ");

        str_val = "\t\t\t\t";
        LLStringUtil::replaceTabsWithSpaces(str_val, 0);
        UNSCOPED_INFO("replaceTabsWithSpaces failed for all tabs");
        CHECK(str_val == "");
    }

    TEST_CASE("LLString: replaceNonstandardASCII replaces controls other than newline", "[LLString]")
    {
        std::string str_val("Hello.\n\n\t\t\r\nLindenlabsX.");
        LLStringUtil::replaceNonstandardASCII(str_val, 'X');
        UNSCOPED_INFO("replaceNonstandardASCII failed");
        CHECK(str_val == "Hello.\n\nXXX\nLindenlabsX.");
    }

    TEST_CASE("LLString: replaceChar replaces every instance of a character", "[LLString]")
    {
        std::string str_val("Hello.\n\t\r\nABCDEFGHIABABAB");
        LLStringUtil::replaceChar(str_val, 'A', 'X');
        UNSCOPED_INFO("1: replaceChar failed");
        CHECK(str_val == "Hello.\n\t\r\nXBCDEFGHIXBXBXB");
        std::string str_val1("Hello.\n\t\r\nABCDEFGHIABABAB");
    }

    TEST_CASE("LLString: containsNonprintable finds controls and separators but not text", "[LLString]")
    {
        std::string str_val("Hello.\n\r\t");
        UNSCOPED_INFO("containsNonprintable failed");
        CHECK(LLStringUtil::containsNonprintable(str_val));

        str_val = "ABC ";
        UNSCOPED_INFO("containsNonprintable failed");
        CHECK_FALSE(LLStringUtil::containsNonprintable(str_val));

        // Controls above the ASCII range are two bytes, and neither of them
        // looks like a control on its own. U+0085 NEXT LINE.
        str_val = "AB\xC2\x85" "C";
        UNSCOPED_INFO("C1 control found");
        CHECK(LLStringUtil::containsNonprintable(str_val));
        // U+2028 LINE SEPARATOR.
        str_val = "AB\xE2\x80\xA8" "C";
        UNSCOPED_INFO("line separator found");
        CHECK(LLStringUtil::containsNonprintable(str_val));
        // DEL, which a test for "below 0x20" never saw.
        str_val = "AB\x7F" "C";
        UNSCOPED_INFO("delete found");
        CHECK(LLStringUtil::containsNonprintable(str_val));

        // Non-ASCII text is printable, and so are the format characters that
        // scripts depend on -- ZWJ here, which Indic conjuncts need.
        str_val = "\xE6\x97\xA5" "\xC3\xA9" "ok";
        UNSCOPED_INFO("ordinary text is printable");
        CHECK_FALSE(LLStringUtil::containsNonprintable(str_val));
        str_val = "\xE0\xA4\x95\xE2\x80\x8D\xE0\xA4\xB7";
        UNSCOPED_INFO("ZWJ is printable");
        CHECK_FALSE(LLStringUtil::containsNonprintable(str_val));
    }

    TEST_CASE("LLString: stripNonprintable removes controls and separators whole", "[LLString]")
    {
        std::string str_val("Hello.\n\r\t Again!");
        LLStringUtil::stripNonprintable(str_val);
        UNSCOPED_INFO("stripNonprintable failed");
        CHECK(str_val == "Hello. Again!");

        str_val = "\r\n\t\t";
        LLStringUtil::stripNonprintable(str_val);
        UNSCOPED_INFO("stripNonprintable resulting in empty string failed");
        CHECK(str_val == "");

        str_val = "";
        LLStringUtil::stripNonprintable(str_val);
        UNSCOPED_INFO("stripNonprintable of empty string resulting in empty string failed");
        CHECK(str_val == "");

        // A control above the ASCII range goes whole, taking both its bytes.
        str_val = "AB\xC2\x85" "C";
        LLStringUtil::stripNonprintable(str_val);
        UNSCOPED_INFO("C1 control stripped whole");
        CHECK(str_val == "ABC");

        // Text either side of it is left alone, bytes and all.
        str_val = "\xE6\x97\xA5" "\xE2\x80\xA8" "\xC3\xA9";
        LLStringUtil::stripNonprintable(str_val);
        UNSCOPED_INFO("only the separator goes");
        CHECK(str_val == "\xE6\x97\xA5" "\xC3\xA9");
    }

    TEST_CASE("LLString: convertToBOOL reads the true and false spellings", "[LLString]")
    {
        bool value;
        std::string str_val("1");
        {
            INFO("convertToBOOL 1 failed");
            REQUIRE(LLStringUtil::convertToBOOL(str_val, value));
            CHECK(value);
        }
        str_val = "T";
        {
            INFO("convertToBOOL T failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK(value);
        }
        str_val = "t";
        {
            INFO("convertToBOOL t failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK(value);
        }
        str_val = "TRUE";
        {
            INFO("convertToBOOL TRUE failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK(value);
        }
        str_val = "True";
        {
            INFO("convertToBOOL True failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK(value);
        }
        str_val = "true";
        {
            INFO("convertToBOOL true failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK(value);
        }

        str_val = "0";
        {
            INFO("convertToBOOL 0 failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK_FALSE(value);
        }
        str_val = "F";
        {
            INFO("convertToBOOL F failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK_FALSE(value);
        }
        str_val = "f";
        {
            INFO("convertToBOOL f failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK_FALSE(value);
        }
        str_val = "FALSE";
        {
            INFO("convertToBOOL FASLE failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK_FALSE(value);
        }
        str_val = "False";
        {
            INFO("convertToBOOL False failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK_FALSE(value);
        }
        str_val = "false";
        {
            INFO("convertToBOOL false failed");
            CHECK(LLStringUtil::convertToBOOL(str_val, value));
            CHECK_FALSE(value);
        }

        str_val = "Tblah";
        UNSCOPED_INFO("convertToBOOL false failed");
        CHECK_FALSE(LLStringUtil::convertToBOOL(str_val, value));
    }

    TEST_CASE("LLString: convertToU8 accepts 0 to 255 only", "[LLString]")
    {
        U8 value;
        std::string str_val("255");
        {
            INFO("1: convertToU8 failed");
            REQUIRE(LLStringUtil::convertToU8(str_val, value));
            CHECK(value == 255);
        }

        str_val = "0";
        {
            INFO("2: convertToU8 failed");
            CHECK(LLStringUtil::convertToU8(str_val, value));
            CHECK(value == 0);
        }

        str_val = "-1";
        UNSCOPED_INFO("3: convertToU8 failed");
        CHECK_FALSE(LLStringUtil::convertToU8(str_val, value));

        str_val = "256"; // bigger than MAX_U8
        UNSCOPED_INFO("4: convertToU8 failed");
        CHECK_FALSE(LLStringUtil::convertToU8(str_val, value));
    }

    TEST_CASE("LLString: convertToS8 accepts -128 to 127 only", "[LLString]")
    {
        S8 value;
        std::string str_val("127");
        {
            INFO("1: convertToS8 failed");
            REQUIRE(LLStringUtil::convertToS8(str_val, value));
            CHECK(value == 127);
        }

        str_val = "0";
        {
            INFO("2: convertToS8 failed");
            CHECK(LLStringUtil::convertToS8(str_val, value));
            CHECK(value == 0);
        }

        str_val = "-128";
        {
            INFO("3: convertToS8 failed");
            CHECK(LLStringUtil::convertToS8(str_val, value));
            CHECK(value == -128);
        }

        str_val = "128"; // bigger than MAX_S8
        UNSCOPED_INFO("4: convertToS8 failed");
        CHECK_FALSE(LLStringUtil::convertToS8(str_val, value));

        str_val = "-129";
        UNSCOPED_INFO("5: convertToS8 failed");
        CHECK_FALSE(LLStringUtil::convertToS8(str_val, value));
    }

    TEST_CASE("LLString: convertToS16 accepts -32768 to 32767 only", "[LLString]")
    {
        S16 value;
        std::string str_val("32767");
        {
            INFO("1: convertToS16 failed");
            REQUIRE(LLStringUtil::convertToS16(str_val, value));
            CHECK(value == 32767);
        }

        str_val = "0";
        {
            INFO("2: convertToS16 failed");
            CHECK(LLStringUtil::convertToS16(str_val, value));
            CHECK(value == 0);
        }

        str_val = "-32768";
        {
            INFO("3: convertToS16 failed");
            CHECK(LLStringUtil::convertToS16(str_val, value));
            CHECK(value == -32768);
        }

        str_val = "32768";
        UNSCOPED_INFO("4: convertToS16 failed");
        CHECK_FALSE(LLStringUtil::convertToS16(str_val, value));

        str_val = "-32769";
        UNSCOPED_INFO("5: convertToS16 failed");
        CHECK_FALSE(LLStringUtil::convertToS16(str_val, value));
    }

    TEST_CASE("LLString: convertToU16 accepts 0 to 65535 only", "[LLString]")
    {
        U16 value;
        std::string str_val("65535"); //0xFFFF
        {
            INFO("1: convertToU16 failed");
            REQUIRE(LLStringUtil::convertToU16(str_val, value));
            CHECK(value == 65535);
        }

        str_val = "0";
        {
            INFO("2: convertToU16 failed");
            CHECK(LLStringUtil::convertToU16(str_val, value));
            CHECK(value == 0);
        }

        str_val = "-1";
        UNSCOPED_INFO("3: convertToU16 failed");
        CHECK_FALSE(LLStringUtil::convertToU16(str_val, value));

        str_val = "65536";
        UNSCOPED_INFO("4: convertToU16 failed");
        CHECK_FALSE(LLStringUtil::convertToU16(str_val, value));
    }

    TEST_CASE("LLString: convertToU32 accepts 0 to 4294967295 and a leading plus", "[LLString]")
    {
        U32 value;
        std::string str_val("4294967295"); //0xFFFFFFFF
        {
            INFO("1: convertToU32 failed");
            REQUIRE(LLStringUtil::convertToU32(str_val, value));
            CHECK(value == 4294967295UL);
        }

        str_val = "0";
        {
            INFO("2: convertToU32 failed");
            CHECK(LLStringUtil::convertToU32(str_val, value));
            CHECK(value == 0);
        }

        str_val = "4294967296";
        UNSCOPED_INFO("3: convertToU32 failed");
        CHECK_FALSE(LLStringUtil::convertToU32(str_val, value));

        // A negative is rejected rather than wrapped. convertToU8 and
        // convertToU16 above already answer false for "-1" -- they range-check
        // after parsing as signed -- so this is the unsigned form agreeing with
        // its own siblings. Stream extraction let num_get negate it instead.
        str_val = "-1";
        UNSCOPED_INFO("4: convertToU32 failed");
        CHECK_FALSE(LLStringUtil::convertToU32(str_val, value));

        // A leading '+' is still accepted, as it was through the stream.
        str_val = "+7";
        {
            INFO("5: convertToU32 failed");
            CHECK(LLStringUtil::convertToU32(str_val, value));
            CHECK(value == 7);
        }
    }

    TEST_CASE("LLString: convertToS32 reads the S32 range with a sign or spaces or trailing text", "[LLString]")
    {
        S32 value;
        std::string str_val("2147483647"); //0x7FFFFFFF
        {
            INFO("1: convertToS32 failed");
            REQUIRE(LLStringUtil::convertToS32(str_val, value));
            CHECK(value == 2147483647);
        }

        str_val = "0";
        {
            INFO("2: convertToS32 failed");
            CHECK(LLStringUtil::convertToS32(str_val, value));
            CHECK(value == 0);
        }

        // Avoid "unary minus operator applied to unsigned type" warning on VC++. JC
        S32 min_val = -2147483647 - 1;
        str_val = "-2147483648";
        {
            INFO("3: convertToS32 failed");
            CHECK(LLStringUtil::convertToS32(str_val, value));
            CHECK(value == min_val);
        }

        str_val = "2147483648";
        UNSCOPED_INFO("4: convertToS32 failed");
        CHECK_FALSE(LLStringUtil::convertToS32(str_val, value));

        str_val = "-2147483649";
        UNSCOPED_INFO("5: convertToS32 failed");
        CHECK_FALSE(LLStringUtil::convertToS32(str_val, value));

        // from_chars does not take a leading '+' on its own; the conversions
        // strip it so they keep accepting what stream extraction accepted.
        str_val = "+42";
        {
            INFO("6: convertToS32 failed");
            CHECK(LLStringUtil::convertToS32(str_val, value));
            CHECK(value == 42);
        }

        // Surrounding space is trimmed, and a parse that stops early still
        // reports what it read -- both as the stream did.
        str_val = "  13  ";
        {
            INFO("7: convertToS32 failed");
            CHECK(LLStringUtil::convertToS32(str_val, value));
            CHECK(value == 13);
        }

        str_val = "12abc";
        {
            INFO("8: convertToS32 failed");
            CHECK(LLStringUtil::convertToS32(str_val, value));
            CHECK(value == 12);
        }

        str_val = "abc";
        UNSCOPED_INFO("9: convertToS32 failed");
        CHECK_FALSE(LLStringUtil::convertToS32(str_val, value));
    }

    TEST_CASE("LLString: convertToF32 reads decimals and exponents and signs", "[LLString]")
    {
        F32 value;
        std::string str_val("2147483647"); //0x7FFFFFFF
        {
            INFO("1: convertToF32 failed");
            REQUIRE(LLStringUtil::convertToF32(str_val, value));
            CHECK(value == 2147483647.f);
        }

        str_val = "0";
        {
            INFO("2: convertToF32 failed");
            CHECK(LLStringUtil::convertToF32(str_val, value));
            CHECK(value == 0);
        }

        /* Need to find max/min F32 values
        str_val = "-2147483648";
        ensure("3: convertToF32 failed", LLStringUtil::convertToF32(str_val, value)  && value == -2147483648);

        str_val = "2147483648";
        ensure("4: convertToF32 failed", !LLStringUtil::convertToF32(str_val, value));

        str_val = "-2147483649";
        ensure("5: convertToF32 failed", !LLStringUtil::convertToF32(str_val, value));
        */

        // The reals go through fast_float now. Decimals, exponents, a sign and
        // a leading '+' all read the same as they did through the stream, and
        // the decimal point stays a point whatever the C locale is set to.
        str_val = "1.5";
        {
            INFO("6: convertToF32 failed");
            CHECK(LLStringUtil::convertToF32(str_val, value));
            CHECK(value == 1.5f);
        }

        str_val = "-2.25";
        {
            INFO("7: convertToF32 failed");
            CHECK(LLStringUtil::convertToF32(str_val, value));
            CHECK(value == -2.25f);
        }

        str_val = "+0.5";
        {
            INFO("8: convertToF32 failed");
            CHECK(LLStringUtil::convertToF32(str_val, value));
            CHECK(value == 0.5f);
        }

        str_val = "1.5e2";
        {
            INFO("9: convertToF32 failed");
            CHECK(LLStringUtil::convertToF32(str_val, value));
            CHECK(value == 150.f);
        }

        str_val = "  0.25  ";
        {
            INFO("10: convertToF32 failed");
            CHECK(LLStringUtil::convertToF32(str_val, value));
            CHECK(value == 0.25f);
        }

        str_val = "nonsense";
        UNSCOPED_INFO("11: convertToF32 failed");
        CHECK_FALSE(LLStringUtil::convertToF32(str_val, value));
    }

    TEST_CASE("LLString: convertToF64 reads a large integer and zero", "[LLString]")
    {
        F64 value;
        std::string str_val("9223372036854775807"); //0x7FFFFFFFFFFFFFFF
        {
            INFO("1: convertToF64 failed");
            REQUIRE(LLStringUtil::convertToF64(str_val, value));
            CHECK(value == 9223372036854775807.0);
        }

        str_val = "0";
        {
            INFO("2: convertToF64 failed");
            CHECK(LLStringUtil::convertToF64(str_val, value));
            CHECK(value == 0.0F);
        }

        /* Need to find max/min F64 values
        str_val = "-2147483648";
        ensure("3: convertToF32 failed", LLStringUtil::convertToF32(str_val, value)  && value == -2147483648);

        str_val = "2147483648";
        ensure("4: convertToF32 failed", !LLStringUtil::convertToF32(str_val, value));

        str_val = "-2147483649";
        ensure("5: convertToF32 failed", !LLStringUtil::convertToF32(str_val, value));
        */
    }

    TEST_CASE("LLString: compareStrings puts null after text and compares text", "[LLString]")
    {
        const char* str1 = NULL;
        const char* str2 = NULL;

        UNSCOPED_INFO("1: compareStrings failed");
        CHECK(LLStringUtil::compareStrings(str1, str2) == 0);
        str2 = "A";
        UNSCOPED_INFO("2: compareStrings failed");
        CHECK(LLStringUtil::compareStrings(str1, str2) > 0);
        UNSCOPED_INFO("3: compareStrings failed");
        CHECK(LLStringUtil::compareStrings(str2, str1) < 0);

        str1 = "A is smaller than B";
        str2 = "B is greater than A";
        UNSCOPED_INFO("4: compareStrings failed");
        CHECK(LLStringUtil::compareStrings(str1, str2) < 0);

        str2 = "A is smaller than B";
        UNSCOPED_INFO("5: compareStrings failed");
        CHECK(LLStringUtil::compareStrings(str1, str2) == 0);
    }

    TEST_CASE("LLString: compareInsensitive ignores case but not accents or sharp s", "[LLString]")
    {
        const char* str1 = NULL;
        const char* str2 = NULL;

        UNSCOPED_INFO("1: compareInsensitive failed");
        CHECK(LLStringUtil::compareInsensitive(str1, str2) == 0);
        str2 = "A";
        UNSCOPED_INFO("2: compareInsensitive failed");
        CHECK(LLStringUtil::compareInsensitive(str1, str2) > 0);
        UNSCOPED_INFO("3: compareInsensitive failed");
        CHECK(LLStringUtil::compareInsensitive(str2, str1) < 0);

        str1 = "A is equal to a";
        str2 = "a is EQUAL to A";
        UNSCOPED_INFO("4: compareInsensitive failed");
        CHECK(LLStringUtil::compareInsensitive(str1, str2) == 0);

        // Ignoring case is not the same as uppercasing both sides, which is
        // what this used to do: that turns sharp s into SS and so calls two
        // different words the same word.
        UNSCOPED_INFO("sharp s is not ss");
        CHECK(LLStringUtil::compareInsensitive(std::string("stra\xC3\x9F""e"),
                                                std::string("strasse")) != 0);
        UNSCOPED_INFO("sharp s is not SS");
        CHECK(LLStringUtil::compareInsensitive(std::string("stra\xC3\x9F""e"),
                                                std::string("STRASSE")) != 0);

        // Accents are a secondary difference and survive; case is tertiary
        // and does not.
        UNSCOPED_INFO("accents survive");
        CHECK(LLStringUtil::compareInsensitive(std::string("ab"),
                                                std::string("\xC3\xA4""b")) < 0);
        UNSCOPED_INFO("case does not");
        CHECK(LLStringUtil::compareInsensitive(std::string("\xC3\x84""B"),
                                                std::string("\xC3\xA4""b")) == 0);
    }

    TEST_CASE("LLString: compareDict and precedesDict order dictionary style", "[LLString]")
    {
        std::string lhs_str("PROgraM12files");
        std::string rhs_str("PROgram12Files");
        UNSCOPED_INFO("compareDict 1 failed");
        CHECK(LLStringUtil::compareDict(lhs_str, rhs_str) < 0);
        UNSCOPED_INFO("precedesDict 1 failed");
        CHECK(LLStringUtil::precedesDict(lhs_str, rhs_str));

        lhs_str = "PROgram12Files";
        rhs_str = "PROgram12Files";
        UNSCOPED_INFO("compareDict 2 failed");
        CHECK(LLStringUtil::compareDict(lhs_str, rhs_str) == 0);
        UNSCOPED_INFO("precedesDict 2 failed");
        CHECK_FALSE(LLStringUtil::precedesDict(lhs_str, rhs_str));

        lhs_str = "PROgram12Files";
        rhs_str = "PROgRAM12FILES";
        UNSCOPED_INFO("compareDict 3 failed");
        CHECK(LLStringUtil::compareDict(lhs_str, rhs_str) > 0);
        UNSCOPED_INFO("precedesDict 3 failed");
        CHECK_FALSE(LLStringUtil::precedesDict(lhs_str, rhs_str));

        // Uppercase sorts first, which is a tertiary-level ordering. The
        // caseless form drops that level and so sees no difference at all.
        UNSCOPED_INFO("compareDictInsensitive case");
        CHECK(LLStringUtil::compareDictInsensitive(lhs_str, rhs_str) == 0);
        UNSCOPED_INFO("compareDict case");
        CHECK(LLStringUtil::compareDict(std::string("abc"), std::string("ABC")) > 0);
        UNSCOPED_INFO("compareDict case 2");
        CHECK(LLStringUtil::compareDict(std::string("Apple"), std::string("apple")) < 0);

        // Digit runs compare as the numbers they spell.
        UNSCOPED_INFO("natural 2 before 10");
        CHECK(LLStringUtil::compareDict(std::string("item2"), std::string("item10")) < 0);
        UNSCOPED_INFO("natural 10 after 9");
        CHECK(LLStringUtil::compareDict(std::string("item10"), std::string("item9")) > 0);

        // An accented letter sorts beside the letter it decorates rather than
        // past the end of the alphabet: a < a-diaeresis < b, not merely
        // somewhere before z. Codepoint order put it after z.
        UNSCOPED_INFO("accent after plain");
        CHECK(LLStringUtil::compareDict(std::string("ab"), std::string("\xC3\xA4" "b")) < 0);
        UNSCOPED_INFO("accent before next letter");
        CHECK(LLStringUtil::compareDict(std::string("\xC3\xA4" "b"), std::string("bb")) < 0);
        UNSCOPED_INFO("accent before z");
        CHECK(LLStringUtil::compareDict(std::string("\xC3\xA4" "b"), std::string("zb")) < 0);
        // ... and case-insensitive still keeps accents apart.
        UNSCOPED_INFO("caseless keeps accents");
        CHECK(LLStringUtil::compareDictInsensitive(std::string("ab"), std::string("\xC3\xA4" "b")) < 0);
    }

    TEST_CASE("LLString: copy copies no more than the destination size", "[LLString]")
    {
        char str1[] = "First String...";
        char str2[100];

        LLStringUtil::copy(str2, str1, 100);
        UNSCOPED_INFO("LLStringUtil::copy with enough dest length failed");
        CHECK(std::string_view(str2) == std::string_view(str1));
        LLStringUtil::copy(str2, str1, sizeof("First"));
        UNSCOPED_INFO("LLStringUtil::copy with less dest length failed");
        CHECK(std::string_view(str2) == "First");
    }

    TEST_CASE("LLString: copyInto inserts at an offset and appends at the end", "[LLString]")
    {
        std::string str1 = "This is the sentence...";
        std::string str2 = "This is the ";
        std::string str3 = "first ";
        std::string str4 = "This is the first sentence...";
        std::string str5 = "This is the sentence...first ";
        std::string dest;

        dest = str1;
        LLStringUtil::copyInto(dest, str3, str2.length());
        UNSCOPED_INFO("LLStringUtil::copyInto insert failed");
        CHECK(dest == str4);

        dest = str1;
        LLStringUtil::copyInto(dest, str3, dest.length());
        UNSCOPED_INFO("LLStringUtil::copyInto append failed");
        CHECK(dest == str5);
    }

    TEST_CASE("LLString: strip_invalid_xml removes controls other than tab and line ends", "[LLString]")
    {
        std::string stripped;

        // Plain US ASCII text, including spaces and punctuation,
        // should not be altered.
        std::string simple_text = "Hello, world!";
        stripped = LLStringFn::strip_invalid_xml(simple_text);
        UNSCOPED_INFO("Simple text passed unchanged");
        CHECK(stripped == simple_text);

        // Control characters should be removed
        // except for 0x09, 0x0a, 0x0d
        std::string control_chars;
        for (char c = 0x01; c < 0x20; c++)
        {
            control_chars.push_back(c);
        }
        std::string allowed_control_chars;
        allowed_control_chars.push_back( (char)0x09 );
        allowed_control_chars.push_back( (char)0x0a );
        allowed_control_chars.push_back( (char)0x0d );

        stripped = LLStringFn::strip_invalid_xml(control_chars);
        UNSCOPED_INFO("Only tab, LF, CR control characters allowed");
        CHECK(stripped == allowed_control_chars);

        // UTF-8 should be passed intact, including high byte
        // characters.  Try Francais (with C squiggle cedilla)
        std::string french = "Fran";
        french.push_back( (char)0xC3 );
        french.push_back( (char)0xA7 );
        french += "ais";
        stripped = LLStringFn::strip_invalid_xml( french );
        UNSCOPED_INFO("UTF-8 high byte text is allowed");
        CHECK(french == stripped);
    }

    TEST_CASE("LLString: format substitutes keys and counts the substitutions", "[LLString]")
    {
        // Test LLStringUtil::format() string interpolation
        LLStringUtil::format_map_t fmt_map;
        std::string s;
        int subcount;

        fmt_map["[TRICK1]"] = "[A]";
        fmt_map["[A]"] = "a";
        fmt_map["[B]"] = "b";
        fmt_map["[AAA]"] = "aaa";
        fmt_map["[BBB]"] = "bbb";
        fmt_map["[TRICK2]"] = "[A]";
        fmt_map["[EXPLOIT]"] = "!!!!!!!!!!!![EXPLOIT]!!!!!!!!!!!!";
        fmt_map["[KEYLONGER]"] = "short";
        fmt_map["[KEYSHORTER]"] = "Am I not a long string?";
        fmt_map["?"] = "?";
        fmt_map["[DELETE]"] = "";
        fmt_map["[]"] = "[]"; // doesn't do a substitution, but shouldn't crash either

        for (LLStringUtil::format_map_t::const_iterator iter = fmt_map.begin(); iter != fmt_map.end(); ++iter)
        {
            // Test when source string is entirely one key
            std::string s1 = (std::string)iter->first;
            std::string s2 = (std::string)iter->second;
            subcount = LLStringUtil::format(s1, fmt_map);
            UNSCOPED_INFO("LLStringUtil::format: Raw interpolation result");
            REQUIRE(s1 == s2);
            if (s1 == "?" || s1 == "[]") // no interp expected
            {
                UNSCOPED_INFO("LLStringUtil::format: Raw interpolation result count");
                REQUIRE(0 == subcount);
            }
            else
            {
                UNSCOPED_INFO("LLStringUtil::format: Raw interpolation result count");
                REQUIRE(1 == subcount);
            }
        }

        for (LLStringUtil::format_map_t::const_iterator iter = fmt_map.begin(); iter != fmt_map.end(); ++iter)
        {
            // Test when source string is one key, duplicated
            std::string s1 = (std::string)iter->first;
            std::string s2 = (std::string)iter->second;
            s = s1 + s1 + s1 + s1;
            subcount = LLStringUtil::format(s, fmt_map);
            UNSCOPED_INFO("LLStringUtil::format: Rawx4 interpolation result");
            REQUIRE(s == s2 + s2 + s2 + s2);
            if (s1 == "?" || s1 == "[]") // no interp expected
            {
                UNSCOPED_INFO("LLStringUtil::format: Rawx4 interpolation result count");
                REQUIRE(0 == subcount);
            }
            else
            {
                UNSCOPED_INFO("LLStringUtil::format: Rawx4 interpolation result count");
                REQUIRE(4 == subcount);
            }
        }

        // Test when source string has no keys
        std::string srcs = "!!!!!!!!!!!!!!!!";
        s = srcs;
        subcount = LLStringUtil::format(s, fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: No key test result");
        CHECK(s == srcs);
        UNSCOPED_INFO("LLStringUtil::format: No key test result count");
        CHECK(0 == subcount);

        // Test when source string has no keys and is empty
        std::string srcs3;
        s = srcs3;
        subcount = LLStringUtil::format(s, fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: No key test3 result");
        REQUIRE(s.empty());
        UNSCOPED_INFO("LLStringUtil::format: No key test3 result count");
        CHECK(0 == subcount);

        // Test a substitution where a key is substituted with blankness
        std::string srcs2 = "[DELETE]";
        s = srcs2;
        subcount = LLStringUtil::format(s, fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: Delete key test2 result");
        REQUIRE(s.empty());
        UNSCOPED_INFO("LLStringUtil::format: Delete key test2 result count");
        CHECK(1 == subcount);

        // Test an assorted substitution
        std::string srcs4 = "[TRICK1][A][B][AAA][BBB][TRICK2][KEYLONGER][KEYSHORTER]?[DELETE]";
        s = srcs4;
        subcount = LLStringUtil::format(s, fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: Assorted Test1 result");
        CHECK(s == "[A]abaaabbb[A]shortAm I not a long string??");
        UNSCOPED_INFO("LLStringUtil::format: Assorted Test1 result count");
        CHECK(9 == subcount);

        // Test an assorted substitution
        std::string srcs5 = "[DELETE]?[KEYSHORTER][KEYLONGER][TRICK2][BBB][AAA][B][A][TRICK1]";
        s = srcs5;
        subcount = LLStringUtil::format(s, fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: Assorted Test2 result");
        CHECK(s == "?Am I not a long string?short[A]bbbaaaba[A]");
        UNSCOPED_INFO("LLStringUtil::format: Assorted Test2 result count");
        CHECK(9 == subcount);

        // Test on nested brackets
        std::string srcs6 = "[[TRICK1]][[A]][[B]][[AAA]][[BBB]][[TRICK2]][[KEYLONGER]][[KEYSHORTER]]?[[DELETE]]";
        s = srcs6;
        subcount = LLStringUtil::format(s, fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: Assorted Test2 result");
        CHECK(s == "[[A]][a][b][aaa][bbb][[A]][short][Am I not a long string?]?[]");
        UNSCOPED_INFO("LLStringUtil::format: Assorted Test2 result count");
        CHECK(9 == subcount);


        // Test an assorted substitution
        std::string srcs8 = "foo[DELETE]bar?";
        s = srcs8;
        subcount = LLStringUtil::format(s, fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: Assorted Test3 result");
        CHECK(s == "foobar?");
        UNSCOPED_INFO("LLStringUtil::format: Assorted Test3 result count");
        CHECK(1 == subcount);
    }

    TEST_CASE("LLString: format with an empty map changes nothing", "[LLString]")
    {
        // Test LLStringUtil::format() string interpolation
        LLStringUtil::format_map_t blank_fmt_map;
        std::string s;
        int subcount;

        // Test substituting out of a blank format_map
        std::string srcs6 = "12345";
        s = srcs6;
        subcount = LLStringUtil::format(s, blank_fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: Blankfmt Test1 result");
        CHECK(s == "12345");
        UNSCOPED_INFO("LLStringUtil::format: Blankfmt Test1 result count");
        CHECK(0 == subcount);

        // Test substituting a blank string out of a blank format_map
        std::string srcs7;
        s = srcs7;
        subcount = LLStringUtil::format(s, blank_fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: Blankfmt Test2 result");
        REQUIRE(s.empty());
        UNSCOPED_INFO("LLStringUtil::format: Blankfmt Test2 result count");
        CHECK(0 == subcount);
    }

    TEST_CASE("LLString: format with an empty key in the map changes nothing", "[LLString]")
    {
        // Test that incorrect LLStringUtil::format() use does not explode.
        LLStringUtil::format_map_t nasty_fmt_map;
        std::string s;
        int subcount;

        nasty_fmt_map[""] = "never used"; // see, this is nasty.

        // Test substituting out of a nasty format_map
        std::string srcs6 = "12345";
        s = srcs6;
        subcount = LLStringUtil::format(s, nasty_fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: Nastyfmt Test1 result");
        CHECK(s == "12345");
        UNSCOPED_INFO("LLStringUtil::format: Nastyfmt Test1 result count");
        CHECK(0 == subcount);

        // Test substituting a blank string out of a nasty format_map
        std::string srcs7;
        s = srcs7;
        subcount = LLStringUtil::format(s, nasty_fmt_map);
        UNSCOPED_INFO("LLStringUtil::format: Nastyfmt Test2 result");
        REQUIRE(s.empty());
        UNSCOPED_INFO("LLStringUtil::format: Nastyfmt Test2 result count");
        CHECK(0 == subcount);
    }

    TEST_CASE("LLString: startsWith finds a prefix", "[LLString]")
    {
        // Make sure startsWith works
        std::string string("anybody in there?");
        std::string substr("anybody");
        UNSCOPED_INFO("startsWith works.");
        CHECK(LLStringUtil::startsWith(string, substr));
    }

    TEST_CASE("LLString: startsWith rejects a word that is not the prefix", "[LLString]")
    {
        // Make sure startsWith correctly fails
        std::string string("anybody in there?");
        std::string substr("there");
        UNSCOPED_INFO("startsWith fails.");
        CHECK_FALSE(LLStringUtil::startsWith(string, substr));
    }

    TEST_CASE("LLString: startsWith is false where either string is empty", "[LLString]")
    {
        // startsWith fails on empty strings
        std::string value("anybody in there?");
        std::string empty;
        UNSCOPED_INFO("empty string.");
        CHECK_FALSE(LLStringUtil::startsWith(value, empty));
        UNSCOPED_INFO("empty substr.");
        CHECK_FALSE(LLStringUtil::startsWith(empty, value));
        UNSCOPED_INFO("empty everything.");
        CHECK_FALSE(LLStringUtil::startsWith(empty, empty));
    }

    TEST_CASE("LLString: endsWith finds a suffix", "[LLString]")
    {
        // Make sure endsWith works correctly
        std::string string("anybody in there?");
        std::string substr("there?");
        UNSCOPED_INFO("endsWith works.");
        CHECK(LLStringUtil::endsWith(string, substr));
    }

    TEST_CASE("LLString: endsWith rejects what is not the suffix", "[LLString]")
    {
        // Make sure endsWith correctly fails
        std::string string("anybody in there?");
        std::string substr("anybody");
        UNSCOPED_INFO("endsWith fails.");
        CHECK_FALSE(LLStringUtil::endsWith(string, substr));
        substr = "there";
        UNSCOPED_INFO("endsWith fails.");
        CHECK_FALSE(LLStringUtil::endsWith(string, substr));
        substr = "ther?";
        UNSCOPED_INFO("endsWith fails.");
        CHECK_FALSE(LLStringUtil::endsWith(string, substr));
    }

    TEST_CASE("LLString: endsWith is false where either string is empty", "[LLString]")
    {
        // endsWith fails on empty strings
        std::string value("anybody in there?");
        std::string empty;
        UNSCOPED_INFO("empty string.");
        CHECK_FALSE(LLStringUtil::endsWith(value, empty));
        UNSCOPED_INFO("empty substr.");
        CHECK_FALSE(LLStringUtil::endsWith(empty, value));
        UNSCOPED_INFO("empty everything.");
        CHECK_FALSE(LLStringUtil::endsWith(empty, empty));
    }

    TEST_CASE("LLString: getTokens splits on delimiters and drops them", "[LLString]")
    {
        UNSCOPED_INFO("empty string");
        CHECK(LLStringUtil::getTokens("", " ") == StringVec());
        UNSCOPED_INFO("only delims");
        CHECK(LLStringUtil::getTokens("   \r\n   ", " \r\n") == StringVec());
        UNSCOPED_INFO("sequence of delims");
        CHECK(LLStringUtil::getTokens(",,, one ,,,", ",") == StringVec{"one"});
        // nat considers this a dubious implementation side effect, but I'd
        // hate to change it now...
        UNSCOPED_INFO("noncontiguous tokens");
        CHECK(LLStringUtil::getTokens(", ,, , one ,,,", ",") == StringVec{ "", "", "one" });
        UNSCOPED_INFO("space-padded tokens");
        CHECK(LLStringUtil::getTokens(",    one  ,  two  ,", ",") == StringVec{"one", "two"});
        UNSCOPED_INFO("no delims");
        CHECK(LLStringUtil::getTokens("one", ",") == StringVec{ "one" });
    }

    // Shorthand for verifying that getTokens() behaves the same when you
    // don't pass a string of escape characters, when you pass an empty string
    // (different overloads), and when you pass a string of characters that
    // aren't actually present.
    static void ensure_getTokens(const std::string& desc,
                                 const std::string& string,
                                 const std::string& drop_delims,
                                 const std::string& keep_delims,
                                 const std::string& quotes,
                                 const std::vector<std::string>& expect)
    {
        UNSCOPED_INFO(desc + " - no esc");
        REQUIRE(LLStringUtil::getTokens(string, drop_delims, keep_delims, quotes) ==
                      expect);
        UNSCOPED_INFO(desc + " - empty esc");
        REQUIRE(LLStringUtil::getTokens(string, drop_delims, keep_delims, quotes, "") ==
                      expect);
        UNSCOPED_INFO(desc + " - unused esc");
        REQUIRE(LLStringUtil::getTokens(string, drop_delims, keep_delims, quotes, "!") ==
                      expect);
    }

    static void ensure_getTokens(const std::string& desc,
                                 const std::string& string,
                                 const std::string& drop_delims,
                                 const std::string& keep_delims,
                                 const std::vector<std::string>& expect)
    {
        ensure_getTokens(desc, string, drop_delims, keep_delims, "", expect);
    }

    TEST_CASE("LLString: getTokens keeps delimiters and handles quotes and escapes", "[LLString]")
    {
        // Signatures to test in this method:
        // getTokens(string, drop_delims, keep_delims [, quotes [, escapes]])
        // If you omit keep_delims, you get the older function (test above).

        // cases like the getTokens(string, delims) tests above
        ensure_getTokens("empty string", "", " ", "", StringVec());
        ensure_getTokens("only delims",
                         "   \r\n   ", " \r\n", "", StringVec());
        ensure_getTokens("sequence of delims",
                         ",,, one ,,,", ", ", "", StringVec{"one"});
        // Note contrast with the case in the previous method
        ensure_getTokens("noncontiguous tokens",
                         ", ,, , one ,,,", ", ", "", StringVec{"one"});
        ensure_getTokens("space-padded tokens",
                         ",    one  ,  two  ,", ", ", "",
                         StringVec{"one", "two"});
        ensure_getTokens("no delims", "one", ",", "", StringVec{ "one" });

        // drop_delims vs. keep_delims
        ensure_getTokens("arithmetic",
                         " ab+def  / xx*  yy ", " ", "+-*/", { "ab", "+", "def", "/", "xx", "*", "yy" });

        // quotes
        ensure_getTokens("no quotes",
                         "She said, \"Don't go.\"", " ", ",", "", { "She", "said", ",", "\"Don't", "go.\"" });
        ensure_getTokens("quotes",
                         "She said, \"Don't go.\"", " ", ",", "\"", { "She", "said", ",", "Don't go." });
        ensure_getTokens("quotes and delims",
                         "run c:/'Documents and Settings'/someone", " ", "", "'",
                         { "run", "c:/Documents and Settings/someone" });
        ensure_getTokens("unmatched quote",
                         "baby don't leave", " ", "", "'", { "baby", "don't", "leave" });
        ensure_getTokens("adjacent quoted",
                         "abc'def \"ghi'\"jkl' mno\"pqr", " ", "", "\"'", { "abcdef \"ghijkl' mnopqr" });
        ensure_getTokens("quoted empty string",
                         "--set SomeVar ''", " ", "", "'", { "--set", "SomeVar", "" });

        // escapes
        // Don't use backslash as an escape for these tests -- you'll go nuts
        // between the C++ string scanner and getTokens() escapes. Test with
        // something else!
        UNSCOPED_INFO("escaped delims");
        CHECK(LLStringUtil::getTokens("^ a - dog^-gone^ phrase", " ", "-", "", "^") ==
                      StringVec{ " a", "-", "dog-gone phrase" });
        UNSCOPED_INFO("escaped quotes");
        CHECK(LLStringUtil::getTokens("say: 'this isn^'t w^orking'.", " ", "", "'", "^") ==
                      StringVec{ "say:", "this isn't working." });
        UNSCOPED_INFO("escaped escape");
        CHECK(LLStringUtil::getTokens("want x^^2", " ", "", "", "^") == StringVec{ "want", "x^2" });
        UNSCOPED_INFO("escape at end");
        CHECK(LLStringUtil::getTokens("it's^ up there^", " ", "", "'", "^") == StringVec{ "it's up", "there^" });
    }

    TEST_CASE("LLString: xml_encode and xml_decode escape element and attribute specials", "[LLString]")
    {
        UNSCOPED_INFO("encode passthrough");
        CHECK(LLStringFn::xml_encode("plain text") == "plain text");
        UNSCOPED_INFO("encode element specials");
        CHECK(LLStringFn::xml_encode("a<b>c&d\"e'f") ==
                      "a&lt;b&gt;c&amp;d\"e'f");
        UNSCOPED_INFO("encode attribute specials");
        CHECK(LLStringFn::xml_encode("a<b>c&d\"e'f", true) ==
                      "a&lt;b&gt;c&amp;d&quot;e&apos;f");
        UNSCOPED_INFO("encode empty");
        CHECK(LLStringFn::xml_encode("") == "");

        UNSCOPED_INFO("decode element specials");
        CHECK(LLStringFn::xml_decode("a&lt;b&gt;c&amp;d&quot;e&apos;f") ==
                      "a<b>c&d&quot;e&apos;f");
        UNSCOPED_INFO("decode attribute specials");
        CHECK(LLStringFn::xml_decode("a&lt;b&gt;c&amp;d&quot;e&apos;f", true) ==
                      "a<b>c&d\"e'f");
        UNSCOPED_INFO("decode unknown entity untouched");
        CHECK(LLStringFn::xml_decode("a&foo;b&") == "a&foo;b&");

        // each literal decodes exactly once: &amp;lt; is the text "&lt;",
        // not a doubly-encoded "<"
        UNSCOPED_INFO("decode does not cascade");
        CHECK(LLStringFn::xml_decode("a&amp;lt;b") == "a&lt;b");
        UNSCOPED_INFO("round trip nested entity text");
        CHECK(LLStringFn::xml_decode(LLStringFn::xml_encode("a&lt;b")) ==
                      "a&lt;b");
    }

    TEST_CASE("LLString: format leaves unsubstitutable strings alone", "[LLString]")
    {
        LLStringUtil::format_map_t fmt_map;
        fmt_map["FOO"] = "bar";

        LLSD sd_map;
        sd_map["FOO"] = "bar";

        // Strings that reach format() without a usable [KEY] must come back
        // byte-identical, whether or not they contain brackets at all.
        const char* const untouched[] = {
            "",
            "plain label with no brackets",
            "hello [MISSING_REPLACEMENT]",
            "-=[Stylized Name]=-",
            "[unclosed",
            "]before[",
            "]",
        };

        for (const char* src : untouched)
        {
            std::string s(src);
            UNSCOPED_INFO(std::string("format_map count: ") + src);
            REQUIRE(0 == LLStringUtil::format(s, fmt_map));
            UNSCOPED_INFO(std::string("format_map result: ") + src);
            REQUIRE(s == std::string(src));

            std::string s2(src);
            UNSCOPED_INFO(std::string("LLSD count: ") + src);
            REQUIRE(0 == LLStringUtil::format(s2, sd_map));
            UNSCOPED_INFO(std::string("LLSD result: ") + src);
            REQUIRE(s2 == std::string(src));
        }

        // A real substitution still fires, and the surrounding text — including
        // brackets that are not a key — survives it.
        std::string s("no [FOO] here [MISSING] -=[Stylized Name]=-");
        UNSCOPED_INFO("mixed count");
        CHECK(1 == LLStringUtil::format(s, fmt_map));
        UNSCOPED_INFO("mixed result");
        CHECK(s ==
                      std::string("no bar here [MISSING] -=[Stylized Name]=-"));
    }

    // Capitalising a byte can only ever reach ASCII, so the narrow form works
    // in codepoints.
    TEST_CASE("LLString: capitalize works in codepoints and not bytes", "[LLString]")
    {
        std::string str_val("hello world-again_now");
        LLStringUtil::capitalize(str_val);
        UNSCOPED_INFO("ascii words");
        CHECK(str_val == "Hello World-Again_Now");

        // U+00E9 -> U+00C9, which uppercasing one byte at a time could not do.
        str_val = "\xC3\xA9" "cole";
        LLStringUtil::capitalize(str_val);
        UNSCOPED_INFO("accented first letter");
        CHECK(str_val == "\xC3\x89" "cole");

        // The word rule is the caller's and still only sees these three.
        str_val = "a b-c_d.e";
        LLStringUtil::capitalize(str_val);
        UNSCOPED_INFO("dot is not a separator");
        CHECK(str_val == "A B-C_D.e");

        str_val = "";
        LLStringUtil::capitalize(str_val);
        UNSCOPED_INFO("empty");
        CHECK(str_val == "");
    }

    // Both views replaceString takes are allowed to point into the string it is
    // replacing into, and both are read after a replace that can have moved the
    // buffer under them. Taking them by value used to make that safe without
    // saying so; a view does not, so the guard is the only thing holding it up
    // and nothing was watching the guard.
    TEST_CASE("LLString: replaceString takes views into the string it replaces in", "[LLString]")
    {
        // replacement views the subject: the classic case, and the one the
        // original guard covered.
        {
            std::string s("abcabc");
            LLStringUtil::replaceString(s, "b", std::string_view(s).substr(0, 3));
            UNSCOPED_INFO("replacement viewing the subject");
            CHECK(s == std::string("aabccaabcc"));
        }

        // target views the subject. This one is read on every turn of the loop,
        // so it dangles from the second replace onward rather than the first.
        {
            std::string s("xyxyxy");
            LLStringUtil::replaceString(s, std::string_view(s).substr(0, 2), "Q");
            UNSCOPED_INFO("target viewing the subject");
            CHECK(s == std::string("QQQ"));
        }

        // Both at once, and a replacement long enough to force the subject to
        // reallocate while the views are still live.
        {
            std::string s("aXbXc");
            LLStringUtil::replaceString(s,
                                        std::string_view(s).substr(1, 1),
                                        std::string_view(s).substr(0, 1));
            UNSCOPED_INFO("both viewing the subject");
            CHECK(s == std::string("aabac"));
        }

        // And the ordinary path still works.
        {
            std::string s("one two one");
            LLStringUtil::replaceString(s, "one", "three");
            UNSCOPED_INFO("unrelated arguments");
            CHECK(s == std::string("three two three"));
        }
        {
            std::string s("aaa");
            LLStringUtil::replaceString(s, "a", "aa");
            UNSCOPED_INFO("replacement containing the target terminates");
            CHECK(s == std::string("aaaaaa"));
        }
    }

    // copyInto has the same shape: src may view dst, the append path appends
    // from it, and the insert path assigns dst out from under it first.
    TEST_CASE("LLString: copyInto takes a source that views the destination", "[LLString]")
    {
        // Append from a view of the destination.
        {
            std::string s("abcd");
            LLStringUtil::copyInto(s, std::string_view(s).substr(0, 2), s.length());
            UNSCOPED_INFO("append from a view of the destination");
            CHECK(s == std::string("abcdab"));
        }

        // Insert from a view of the destination, at the front, so the assign
        // that shortens dst happens before src is read.
        {
            std::string s("abcd");
            LLStringUtil::copyInto(s, std::string_view(s).substr(2, 2), 0);
            UNSCOPED_INFO("insert from a view of the destination");
            CHECK(s == std::string("cdabcd"));
        }

        // A view of the whole of it, long enough that the growth reallocates.
        {
            std::string s("0123456789");
            LLStringUtil::copyInto(s, std::string_view(s), 5);
            UNSCOPED_INFO("insert the whole of the destination into itself");
            CHECK(s == std::string("01234012345678956789"));
        }

        // Offset at the end is the append fast path; offset past it is not a
        // case any caller has, so it is simply not exercised here.
        {
            std::string s("head");
            LLStringUtil::copyInto(s, std::string("-tail"), s.length());
            UNSCOPED_INFO("unrelated source appends");
            CHECK(s == std::string("head-tail"));
        }
    }

    TEST_CASE("LLString: format over two maps reads the first's value as a merge would", "[LLString]")
    {
        LLStringUtil::format_map_t first;
        first["[APP]"] = "Alchemy";
        first["[BOTH]"] = "first";
        first["[BARE]"] = "bracketed in the first";
        first["datetime"] = "0";
        LLStringUtil::format_map_t second;
        second["[NAME]"] = "Rye";
        second["[BOTH]"] = "second";
        second["BARE"] = "bare in the second";
        second["[COUNT]"] = "3.14159";

        // A bare key is found before a bracketed one whichever map has it,
        // as the merge's one lookup order had it.
        const char* const text = "[APP] for [NAME]: [BOTH], [BARE], [COUNT,number,2], [MISSING]";
        std::string       s(text);
        UNSCOPED_INFO("five put in");
        CHECK(5 == LLStringUtil::format(s, first, second));
        UNSCOPED_INFO("from either, the first first, a number formatted, what neither has left");
        CHECK(s ==
                      std::string("Alchemy for Rye: first, bare in the second, 3.14, [MISSING]"));

        // The same as the merge would give.
        LLStringUtil::format_map_t merged = first;
        merged.insert(second.begin(), second.end());
        std::string m(text);
        LLStringUtil::format(m, merged);
        UNSCOPED_INFO("as the merge");
        CHECK(s == m);

        // The maps the other way about: the second's value where both have
        // the key.
        std::string r("[BOTH]");
        UNSCOPED_INFO("one put in");
        CHECK(1 == LLStringUtil::format(r, second, first));
        UNSCOPED_INFO("the other first");
        CHECK(r == std::string("second"));

        std::string plain("nothing bracketed");
        UNSCOPED_INFO("nothing to do");
        CHECK(0 == LLStringUtil::format(plain, first, second));
        UNSCOPED_INFO("and nothing done");
        CHECK(plain == std::string("nothing bracketed"));

        // A map over an LLSD map, as a notification's substitutions under
        // the defaults: the map first, the LLSD where the map has nothing,
        // and a datetime the LLSD lacks counted from the epoch as the LLSD
        // form has always had it.
        LLSD sd;
        sd["NAME"] = "Rye";
        sd["[BOTH]"] = "the notification's";
        std::string over("[APP] for [NAME]: [BOTH]");
        UNSCOPED_INFO("three put in");
        CHECK(3 == LLStringUtil::format(over, first, sd));
        UNSCOPED_INFO("the map first, the LLSD after");
        CHECK(over == std::string("Alchemy for Rye: first"));
        LLSD not_a_map;
        std::string alone("[APP] [NAME]");
        UNSCOPED_INFO("with no LLSD map, the map alone");
        CHECK(1 == LLStringUtil::format(alone, first, not_a_map));
        UNSCOPED_INFO("what the map had");
        CHECK(alone == std::string("Alchemy [NAME]"));
    }
}
