/**
 * @file llstreamtools_tut.cpp
 * @author Adroit
 * @date February 2007
 * @brief llstreamtools test cases.
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
#include "altestmatchers.h"

#include "linden_common.h"
#include "llstreamtools.h"


namespace
{
    //test cases for skip_whitespace()
    TEST_CASE("streamtools: skip_whitespace skips spaces and tabs but not line ends", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;

        is.str(str = "");
        UNSCOPED_INFO("skip_whitespace: empty string");
        CHECK_FALSE(skip_whitespace(is));

        is.clear();
        is.str(str = " SecondLife is a 3D World");
        skip_whitespace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World";
        UNSCOPED_INFO("skip_whitespace: space");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "\t          \tSecondLife is a 3D World");
        skip_whitespace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World";
        UNSCOPED_INFO("skip_whitespace: space and tabs");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "\t          \tSecondLife is a 3D World       ");
        skip_whitespace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World       ";
        UNSCOPED_INFO("skip_whitespace: space at end");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "\t \r\nSecondLife is a 3D World");
        skip_whitespace(is);
        is.get(arr, 255, '\0');
        expected_result = "\r\nSecondLife is a 3D World";
        UNSCOPED_INFO("skip_whitespace: space at end");
        CHECK(arr == expected_result);
    }

    //testcases for skip_emptyspaces()
    TEST_CASE("streamtools: skip_emptyspace skips whitespace and line ends", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "  \tSecondLife is a 3D World.\n");
        skip_emptyspace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World.\n";
        UNSCOPED_INFO("skip_emptyspace: space and tabs");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "  \t\r\n    \r    SecondLife is a 3D World.\n");
        skip_emptyspace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World.\n";
        UNSCOPED_INFO("skip_emptyspace: space, tabs, carriage return, newline");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "");
        ret = skip_emptyspace(is);
        is.get(arr, 255, '\0');
        UNSCOPED_INFO("skip_emptyspace: empty string");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "  \r\n  \t ");
        ret = skip_emptyspace(is);
        is.get(arr, 255, '\0');
        UNSCOPED_INFO("skip_emptyspace: space newline empty");
        CHECK_FALSE(ret);
    }

    //testcases for skip_comments_and_emptyspace()
    TEST_CASE("streamtools: skip_comments_and_emptyspace skips comment lines and empty space", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "  \t\r\n    \r    SecondLife is a 3D World.\n");
        skip_comments_and_emptyspace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World.\n";
        UNSCOPED_INFO("skip_comments_and_emptyspace: space, tabs, carriage return, newline");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "#    \r\n    SecondLife is a 3D World.");
        skip_comments_and_emptyspace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World.";
        UNSCOPED_INFO("skip_comments_and_emptyspace: skip comment - 1");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "#    \r\n  #  SecondLife is a 3D World. ##");
        UNSCOPED_INFO("should not be good()");
        CHECK_FALSE(skip_comments_and_emptyspace(is));
        UNSCOPED_INFO("should be at eof()");
        CHECK(is.eof());
        // don't get(): given bad state, we can't rely on results

        is.clear();
        is.str(str = " \r\n  SecondLife is a 3D World. ##");
        skip_comments_and_emptyspace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World. ##";
        UNSCOPED_INFO("skip_comments_and_emptyspace: skip comment - 3");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "");
        ret = skip_comments_and_emptyspace(is);
        UNSCOPED_INFO("skip_comments_and_emptyspace: empty string");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "  \r\n  \t # SecondLife is a 3D World");
        ret = skip_comments_and_emptyspace(is);
        UNSCOPED_INFO("skip_comments_and_emptyspace: space newline comment empty");
        CHECK_FALSE(ret);
    }

    //testcases for skip_line()
    TEST_CASE("streamtools: skip_line skips past the next newline", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "SecondLife is a 3D World.\n\n It provides an opportunity to the site \nuser to perform real life activities in virtual world.");
        skip_line(is);
        is.get(arr, 255, '\0');
        expected_result = "\n It provides an opportunity to the site \nuser to perform real life activities in virtual world.";
        UNSCOPED_INFO("skip_line: 1 newline");
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        skip_line(is);
        is.get(arr, 255, '\0');
        expected_result = " It provides an opportunity to the site \nuser to perform real life activities in virtual world.";
        UNSCOPED_INFO("skip_line: 2 newline");
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        skip_line(is);
        is.get(arr, 255, '\0');
        expected_result = "user to perform real life activities in virtual world.";
        UNSCOPED_INFO("skip_line: 3 newline");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "");
        ret = skip_line(is);
        UNSCOPED_INFO("skip_line: empty string");
        CHECK_FALSE(ret);
    }


    // testcases for skip_to_next_word()
    TEST_CASE("streamtools: skip_to_next_word skips the word and the delimiters after it", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "SecondLife is a 3D_World.\n\n It-provides an opportunity to the site \nuser to perform real life activities in virtual world.");
        skip_to_next_word(is); // get past SecondLife
        is.get(arr, 255, '\0');
        expected_result = "is a 3D_World.\n\n It-provides an opportunity to the site \nuser to perform real life activities in virtual world.";
        UNSCOPED_INFO("skip_to_next_word: 1");
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        skip_to_next_word(is); // get past is
        skip_to_next_word(is); // get past a
        skip_to_next_word(is); // get past 3D_World.\n\n
        is.get(arr, 255, '\0');
        expected_result = "It-provides an opportunity to the site \nuser to perform real life activities in virtual world.";
        UNSCOPED_INFO("skip_to_next_word: get past .\n\n 2");
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        skip_to_next_word(is); // get past It-
        expected_result = "provides an opportunity to the site \nuser to perform real life activities in virtual world.";
        is.get(arr, 255, '\0');
        UNSCOPED_INFO("skip_to_next_word: get past -");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "");
        ret = skip_to_next_word(is);
        UNSCOPED_INFO("skip_line: empty string");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "                   \r\n\r\n");
        ret = skip_to_next_word(is);
        UNSCOPED_INFO("skip_line: space new lines");
        CHECK_FALSE(ret);
    }


    //testcases for skip_to_end_of_next_keyword()
    TEST_CASE("streamtools: skip_to_end_of_next_keyword finds a keyword that starts a line", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "FIRSTKEY followed by second delimiter\nSECONDKEY\t SecondValue followed by third delimiter   \nSECONDKEY\nFOURTHKEY FOURTHVALUEis a 3DWorld.");
        ret = skip_to_end_of_next_keyword("FIRSTKEY", is);
        is.get(arr, 255, '\0');
        expected_result = " followed by second delimiter\nSECONDKEY\t SecondValue followed by third delimiter   \nSECONDKEY\nFOURTHKEY FOURTHVALUEis a 3DWorld.";
        UNSCOPED_INFO("skip_to_end_of_next_keyword: 1");
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        ret = skip_to_end_of_next_keyword("SECONDKEY", is);
        is.get(arr, 255, '\0');
        expected_result = "\t SecondValue followed by third delimiter   \nSECONDKEY\nFOURTHKEY FOURTHVALUEis a 3DWorld.";
        UNSCOPED_INFO("skip_to_end_of_next_keyword: 2");
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        ret = skip_to_end_of_next_keyword("SECONDKEY", is);
        is.get(arr, 255, '\0');
        expected_result = "\nFOURTHKEY FOURTHVALUEis a 3DWorld.";
        UNSCOPED_INFO("skip_to_end_of_next_keyword: 3");
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        ret = skip_to_end_of_next_keyword("FOURTHKEY", is);
        is.get(arr, 255, '\0');
        expected_result = " FOURTHVALUEis a 3DWorld.";
        UNSCOPED_INFO("skip_to_end_of_next_keyword: 4");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "{should be skipped as newline/space/tab does not follow but this one should be picked\n { Does it?\n");
        ret = skip_to_end_of_next_keyword("{", is);
        is.get(arr, 255, '\0');
        expected_result = " Does it?\n";
        UNSCOPED_INFO("skip_to_end_of_next_keyword: multiple delim matches on same line");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "Delim { could not be found at start");
        ret = skip_to_end_of_next_keyword("{", is);
        UNSCOPED_INFO("skip_to_end_of_next_keyword: delim should not be present");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "Empty Delim");
        ret = skip_to_end_of_next_keyword("", is);
        UNSCOPED_INFO("skip_to_end_of_next_keyword: empty delim should not be valid");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "");
        ret = skip_to_end_of_next_keyword("}", is);
        UNSCOPED_INFO("skip_to_end_of_next_keyword: empty string");
        CHECK_FALSE(ret);
    }

    //testcases for get_word(std::string& output_string, std::istream& input_stream)
    TEST_CASE("streamtools: get_word reads words split by whitespace", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "  First Second \t \r  \n Third  Fourth-ShouldThisBePartOfFourth  Fifth\n");
        actual_result = "";
        ret = get_word(actual_result, is);
        expected_result = "First";
        UNSCOPED_INFO("get_word: 1");
        CHECK(actual_result == expected_result);

        actual_result = "";
        ret = get_word(actual_result, is);
        expected_result = "Second";
        UNSCOPED_INFO("get_word: 2");
        CHECK(actual_result == expected_result);

        actual_result = "";
        ret = get_word(actual_result, is);
        expected_result = "Third";
        UNSCOPED_INFO("get_word: 3");
        CHECK(actual_result == expected_result);

        // the current implementation of get_word seems inconsistent with
        // skip_to_next_word. skip_to_next_word treats any character other
        // than alhpa-numeric and '_' as a delimter, while get_word()
        // treats only isspace() (i.e. space,  form-feed('\f'),  newline  ('\n'),
        // carriage  return ('\r'), horizontal tab ('\t'), and vertical tab ('\v')
        // as delimiters
        actual_result = "";
        ret = get_word(actual_result, is); // will copy Fourth-ShouldThisBePartOfFourth
        expected_result = "Fourth-ShouldThisBePartOfFourth"; // as per current impl.
        UNSCOPED_INFO("get_word: 4");
        CHECK(actual_result == expected_result);

        actual_result = "";
        ret = get_word(actual_result, is); // will copy Fifth
        expected_result = "Fifth"; // as per current impl.
        UNSCOPED_INFO("get_word: 5");
        CHECK(actual_result == expected_result);

        is.clear();
        is.str(str = "  \t \r  \n ");
        actual_result = "";
        ret = get_word(actual_result, is);
        UNSCOPED_INFO("get_word: empty all spaces, newline tabs");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "");
        actual_result = "";
        ret = get_word(actual_result, is);
        UNSCOPED_INFO("get_word: empty string");
        CHECK_FALSE(ret);
    }

    // testcase for get_word and skip_to_next_word compatibility
    TEST_CASE("streamtools: get_word and skip_to_next_word reach the same fifth word", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;

        is.clear();
        is.str(str = "  First Second \t \r  \n Third  Fourth-ShouldThisBePartOfFourth  Fifth\n");
        actual_result = "";
        get_word(actual_result, is); // First
        actual_result = "";
        get_word(actual_result, is); // Second
        actual_result = "";
        get_word(actual_result, is); // Third

        // the current implementation of get_word seems inconsistent with
        // skip_to_next_word. skip_to_next_word treats any character other
        // than alhpa-numeric and '_' as a delimter, while get_word()
        // treats only isspace() (i.e. space,  form-feed('\f'),  newline  ('\n'),
        // carriage  return ('\r'), horizontal tab ('\t'), and vertical tab ('\v')
        // as delimiters
        actual_result = "";
        get_word(actual_result, is); // will copy Fourth-ShouldThisBePartOfFourth

        actual_result = "";
        get_word(actual_result, is); // will copy Fifth

        is.clear();
        is.str(str = "  First Second \t \r  \n Third  Fourth_ShouldThisBePartOfFourth Fifth\n");
        skip_to_next_word(is);  // should now point to First
        skip_to_next_word(is);  // should now point to Second
        skip_to_next_word(is);  // should now point to Third
        skip_to_next_word(is);  // should now point to Fourth
        skip_to_next_word(is);  // should now point to ShouldThisBePartOfFourth
        expected_result = "";
        // will copy ShouldThisBePartOfFourth, the fifth word,
        // while using get_word above five times result in getting "Fifth"
        get_word(expected_result, is);
        UNSCOPED_INFO("get_word: skip_to_next_word compatibility");
        CHECK(actual_result == expected_result);
    }

    //testcases for get_word(std::string& output_string, std::istream& input_stream, int n)
    TEST_CASE("streamtools: get_word with a limit stops after n characters", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "  First Second \t \r  \n Third  Fourth-ShouldThisBePartOfFourth  Fifth\n");
        actual_result = "";
        ret = get_word(actual_result, is, 255);
        expected_result = "First";
        UNSCOPED_INFO("get_word: 1");
        CHECK(actual_result == expected_result);

        actual_result = "";
        ret = get_word(actual_result, is, 4);
        expected_result = "Seco"; // should be cut short
        UNSCOPED_INFO("get_word: 2");
        CHECK(actual_result == expected_result);

        actual_result = "";
        ret = get_word(actual_result, is, 255);
        expected_result = "nd"; // get remainder of Second
        UNSCOPED_INFO("get_word: 3");
        CHECK(actual_result == expected_result);

        actual_result = "";
        ret = get_word(actual_result, is, 0); // 0 size string
        expected_result = ""; // get remainder of Second
        UNSCOPED_INFO("get_word: 0 sized output");
        CHECK(actual_result == expected_result);

        actual_result = "";
        ret = get_word(actual_result, is, 255);
        expected_result = "Third";
        UNSCOPED_INFO("get_word: 4");
        CHECK(actual_result == expected_result);

        is.clear();
        is.str(str = "  \t \r  \n ");
        actual_result = "";
        ret = get_word(actual_result, is, 255);
        UNSCOPED_INFO("get_word: empty all spaces, newline tabs");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "");
        actual_result = "";
        ret = get_word(actual_result, is, 255);
        UNSCOPED_INFO("get_word: empty string");
        CHECK_FALSE(ret);
    }

    //test cases for get_line(std::string& output_string, std::istream& input_stream)
    TEST_CASE("streamtools: get_line reads up to and including each newline", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;

        is.clear();
        is.str(str = "First Second \t \r\n Third  Fourth-ShouldThisBePartOfFourth  IsThisFifth\n");
        actual_result = "";
        get_line(actual_result, is);
        expected_result = "First Second \t \r\n";
        UNSCOPED_INFO("get_line: 1");
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is);
        expected_result = " Third  Fourth-ShouldThisBePartOfFourth  IsThisFifth\n";
        UNSCOPED_INFO("get_line: 2");
        CHECK(actual_result == expected_result);

        is.clear();
        is.str(str = "\nFirst Line.\n\nSecond Line.\n");
        actual_result = "";
        get_line(actual_result, is);
        expected_result = "\n";
        UNSCOPED_INFO("get_line: First char as newline");
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is);
        expected_result = "First Line.\n";
        UNSCOPED_INFO("get_line: 3");
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is);
        expected_result = "\n";
        UNSCOPED_INFO("get_line: 4");
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is);
        expected_result = "Second Line.\n";
        UNSCOPED_INFO("get_line: 5");
        CHECK(actual_result == expected_result);
    }

    //test cases for get_line(std::string& output_string, std::istream& input_stream)
    TEST_CASE("streamtools: get_line reads a line with no newline and returns good()", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "One Line only with no newline");
        actual_result = "";
        ret = get_line(actual_result, is);
        expected_result = "One Line only with no newline";
        UNSCOPED_INFO("get_line: No newline");
        CHECK(actual_result == expected_result);
        UNSCOPED_INFO("return value is good state of stream");
        CHECK(ret == is.good());
    }

    //test cases for get_line(std::string& output_string, std::istream& input_stream)
    TEST_CASE("streamtools: get_line keeps a lone carriage return", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;

        // need to be check if this test case is wrong or the implementation is wrong.
        is.clear();
        is.str(str = "Should not skip lone \r.\r\n");
        actual_result = "";
        get_line(actual_result, is);
        expected_result = "Should not skip lone \r.\r\n";
        UNSCOPED_INFO("get_line: carriage return skipped even though not followed by newline");
        CHECK(actual_result == expected_result);
    }

    //test cases for get_line(std::string& output_string, std::istream& input_stream)
    TEST_CASE("streamtools: get_line reads a line that is just a newline", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;

        is.clear();
        is.str(str = "\n");
        actual_result = "";
        get_line(actual_result, is);
        expected_result = "\n";
        UNSCOPED_INFO("get_line: Just newline");
        CHECK(actual_result == expected_result);
    }


    //testcases for get_line(std::string& output_string, std::istream& input_stream, int n)
    TEST_CASE("streamtools: get_line with a limit cuts the line and adds a newline", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;

        is.clear();
        is.str(str = "First Line.\nSecond Line.\n");
        actual_result = "";
        get_line(actual_result, is, 255);
        expected_result = "First Line.\n";
        UNSCOPED_INFO("get_line: Basic Operation");
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is, sizeof("Second")-1);
        expected_result = "Second\n";
        UNSCOPED_INFO("get_line: Insufficient length 1");
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is, 255);
        expected_result = " Line.\n";
        UNSCOPED_INFO("get_line: Remainder after earlier insufficient length");
        CHECK(actual_result == expected_result);

        is.clear();
        is.str(str = "One Line only with no newline with limited length");
        actual_result = "";
        get_line(actual_result, is, sizeof("One Line only with no newline with limited length")-1);
        expected_result = "One Line only with no newline with limited length\n";
        UNSCOPED_INFO("get_line: No newline with limited length");
        CHECK(actual_result == expected_result);

        is.clear();
        is.str(str = "One Line only with no newline");
        actual_result = "";
        get_line(actual_result, is, 255);
        expected_result = "One Line only with no newline";
        UNSCOPED_INFO("get_line: No newline");
        CHECK(actual_result == expected_result);
    }

    //testcases for get_line(std::string& output_string, std::istream& input_stream, int n)
    TEST_CASE("streamtools: get_line with a limit reads a line with no newline and returns good()", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "One Line only with no newline");
        actual_result = "";
        ret = get_line(actual_result, is, 255);
        expected_result = "One Line only with no newline";
        UNSCOPED_INFO("get_line: No newline");
        CHECK(actual_result == expected_result);
        UNSCOPED_INFO("return value is good state of stream");
        CHECK(ret == is.good());
    }

    //testcases for remove_last_char()
    TEST_CASE("streamtools: remove_last_char removes only a matching last character", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        bool ret;

        str = "SecondLife is a 3D World";

        ret = remove_last_char('d',str);
        expected_result = "SecondLife is a 3D Worl";
        UNSCOPED_INFO("remove_last_char: should remove last char");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D World";
        ret = remove_last_char('W',str);
        expected_result = "SecondLife is a 3D World";
        UNSCOPED_INFO("remove_last_char: should not remove as it is not last char");
        CHECK(str == expected_result);
        UNSCOPED_INFO("remove_last_char: should return false");
        CHECK_FALSE(ret);

        str = "SecondLife is a 3D World\n";
        ret = remove_last_char('\n',str);
        expected_result = "SecondLife is a 3D World";
        UNSCOPED_INFO("remove_last_char: should remove last newline");
        CHECK(str == expected_result);
        UNSCOPED_INFO("remove_last_char: should remove newline and return true");
        CHECK(ret);
    }


    //testcases for unescaped_string()
    TEST_CASE("streamtools: unescape_string unescapes newlines and backslashes only", "[streamtools]")
    {
        std::string str;
        std::string expected_result;

        str = "SecondLife is a 3D world \\n";
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \n";
        UNSCOPED_INFO("unescape_string: newline");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \\\\t \\n";
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \\t \n";
        UNSCOPED_INFO("unescape_string: backslash and newline");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \\ ";
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \\ ";
        UNSCOPED_INFO("unescape_string: insufficient to unescape");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \\n \\n \\n \\\\\\n";
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \n \n \n \\\n";
        UNSCOPED_INFO("unescape_string: multipel newline and backslash");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \\t";
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \\t";
        UNSCOPED_INFO("unescape_string: leaves tab as is");
        CHECK(str == expected_result);

        str = "\\n";
        unescape_string(str);
        expected_result = "\n";
        UNSCOPED_INFO("unescape_string: only a newline");
        CHECK(str == expected_result);
    }

    //testcases for escape_string()
    TEST_CASE("streamtools: escape_string escapes newlines and backslashes and round-trips", "[streamtools]")
    {
        std::string str;
        std::string expected_result;

        str = "SecondLife is a 3D world \n";
        escape_string(str);
        expected_result = "SecondLife is a 3D world \\n";
        UNSCOPED_INFO("escape_string: newline");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \\t \n";
        escape_string(str);
        expected_result = "SecondLife is a 3D world \\\\t \\n";
        UNSCOPED_INFO("escape_string: backslash and newline");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \n \n \n \\\n";
        escape_string(str);
        expected_result = "SecondLife is a 3D world \\n \\n \\n \\\\\\n";
        UNSCOPED_INFO("escape_string: multipel newline and backslash");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \t";
        escape_string(str);
        expected_result = "SecondLife is a 3D world \t";
        UNSCOPED_INFO("unescape_string: leaves tab as is");
        CHECK(str == expected_result);

        str = "\n";
        escape_string(str);
        expected_result = "\\n";
        UNSCOPED_INFO("unescape_string: only a newline");
        CHECK(str == expected_result);

        // serialization/deserialization escape->unescape
        str = "SecondLife is a 3D world \n \n \n \\\n";
        escape_string(str);
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \n \n \n \\\n";
        UNSCOPED_INFO("escape_string: should preserve with escape/unescape");
        CHECK(str == expected_result);

        // serialization/deserialization  unescape->escape
        str = "SecondLife is a 3D world \\n \\n \\n \\\\";
        unescape_string(str);
        escape_string(str);
        expected_result = "SecondLife is a 3D world \\n \\n \\n \\\\";
        UNSCOPED_INFO("escape_string: should preserve with unescape/escape");
        CHECK(str == expected_result);
    }

    // testcases for replace_newlines_with_whitespace()
    TEST_CASE("streamtools: replace_newlines_with_whitespace replaces only newlines", "[streamtools]")
    {
        std::string str;
        std::string expected_result;

        str = "SecondLife is a 3D \n\nworld\n";
        replace_newlines_with_whitespace(str);
        expected_result = "SecondLife is a 3D   world ";
        UNSCOPED_INFO("replace_newlines_with_whitespace: replace all newline");
        CHECK(str == expected_result);

        str = "\nSecondLife is a 3D world\n";
        replace_newlines_with_whitespace(str);
        expected_result = " SecondLife is a 3D world ";
        UNSCOPED_INFO("replace_newlines_with_whitespace: begin and newline");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world\r\t";
        replace_newlines_with_whitespace(str);
        expected_result = "SecondLife is a 3D world\r\t";
        UNSCOPED_INFO("replace_newlines_with_whitespace: should only replace newline");
        CHECK(str == expected_result);

        str = "";
        replace_newlines_with_whitespace(str);
        expected_result = "";
        UNSCOPED_INFO("replace_newlines_with_whitespace: empty string");
        CHECK(str == expected_result);
    }

    //testcases for remove_double_quotes()
    TEST_CASE("streamtools: remove_double_quotes removes paired and lone double quotes", "[streamtools]")
    {
        std::string str;
        std::string expected_result;

        str = "SecondLife is a \"\"3D world";
        remove_double_quotes(str);
        expected_result = "SecondLife is a 3D world";
        UNSCOPED_INFO("remove_double_quotes: replace empty doube quotes");
        CHECK(str == expected_result);

        str = "SecondLife is a \"3D world";
        remove_double_quotes(str);
        expected_result = "SecondLife is a 3D world";
        UNSCOPED_INFO("remove_double_quotes: keep as is it matching quote not found");
        CHECK(str == expected_result);
    }

    // testcases for get_brace_count()
    TEST_CASE("streamtools: get_brace_count has no checks yet", "[streamtools]")
    {
    }

    //testcases for get_keyword_and_value()
    TEST_CASE("streamtools: get_keyword_and_value splits off the first word as the keyword", "[streamtools]")
    {
        std::string s = "SecondLife is a 3D World";
        std::string keyword;
        std::string value;
        get_keyword_and_value(keyword, value, s);
        {
            INFO("get_keyword_and_value: Unable to get Keyword and Value");
            CHECK(keyword == "SecondLife");
            CHECK(value == "is a 3D World");
        }

        s = "SecondLife";
        get_keyword_and_value(keyword, value, s);
        {
            INFO("get_keyword_and_value: value should be empty");
            CHECK(keyword == "SecondLife");
            CHECK(value == "");
        }

        s = "SecondLife \t  is cool!     \n";
        get_keyword_and_value(keyword, value, s);
        {
            INFO("get_keyword_and_value: remove space before value but not after");
            CHECK(keyword == "SecondLife");
            CHECK(value == "is cool!     ");
        }
    }

    //testcases for get_keyword_and_value()
    TEST_CASE("streamtools: get_keyword_and_value gives no value after a keyword and newline", "[streamtools]")
    {
        std::string s;
        std::string keyword = "SOME PRIOR KEYWORD";
        std::string value = "SOME PRIOR VALUE";

        s = "SecondLife\n";
        get_keyword_and_value(keyword, value, s);
        {
            INFO("get_keyword_and_value: terminated with newline. value should be empty");
            CHECK(keyword == "SecondLife");
            CHECK(value == "");
        }
    }

    //testcases for get_keyword_and_value()
    TEST_CASE("streamtools: get_keyword_and_value of an empty string clears both", "[streamtools]")
    {
        std::string s;
        std::string keyword = "SOME PRIOR KEYWORD";
        std::string value = "SOME PRIOR VALUE";

        s = "";
        get_keyword_and_value(keyword, value, s);
        {
            INFO("get_keyword_and_value: empty string. keyword value should empty");
            CHECK(keyword == "");
            CHECK(value == "");
        }
    }

    //testcase for fullread()
    TEST_CASE("streamtools: fullread reads across newlines up to the requested count", "[streamtools]")
    {
        std::string str = "First Line.\nSecond Line\n";
        std::istringstream is(str);
        char buf[255] = {0};

        fullread(is, buf, 255);
        UNSCOPED_INFO("fullread: read with newlines");
        REQUIRE_THAT(ll_test::Memory((void*) buf, static_cast<U32>(str.size())-1), ll_test::MemoryMatches((void*) str.c_str(), static_cast<U32>(str.size())-1));

        is.clear();
        is.str(str = "First Line.\nSecond Line\n");
        memset(buf, 0, 255);

        char expected_string[] = "First Line.\nSecond";
        int len = sizeof(expected_string)-1;
        fullread(is, buf, len);
        UNSCOPED_INFO("fullread: read with newlines");
        REQUIRE_THAT(ll_test::Memory((void*) buf, len), ll_test::MemoryMatches((void*) &expected_string, len));
    }


//   testcases for operator >>

    TEST_CASE("streamtools: operator>> consumes a matching C string and fails on a mismatch", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string toCheck = "SecondLife" ;
        std::string expected_result;
        std::istringstream stream("SecondLife is a 3D World");
        stream >> toCheck.c_str();
        stream.get(arr, 255, '\0');
        expected_result = " is a 3D World";
        UNSCOPED_INFO("istream << operator");
        CHECK(arr == expected_result);

        stream.clear();
        stream.str(str = "SecondLife is a 3D World");
        toCheck = "is";
        stream >> toCheck.c_str();
        UNSCOPED_INFO("istream << operator should have failed");
        CHECK_FALSE(stream.good());
    }
}
