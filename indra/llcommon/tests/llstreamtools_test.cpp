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
    TEST_CASE("streamtools: skip_whitespace skips spaces and tabs but not line ends", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::istringstream is;

        is.str(str = "");
        UNSCOPED_INFO("empty string");
        CHECK_FALSE(skip_whitespace(is));

        is.clear();
        is.str(str = " SecondLife is a 3D World");
        skip_whitespace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World";
        UNSCOPED_INFO("a leading space");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "\t          \tSecondLife is a 3D World");
        skip_whitespace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World";
        UNSCOPED_INFO("spaces and tabs");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "\t          \tSecondLife is a 3D World       ");
        skip_whitespace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World       ";
        UNSCOPED_INFO("trailing spaces are kept");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "\t \r\nSecondLife is a 3D World");
        skip_whitespace(is);
        is.get(arr, 255, '\0');
        expected_result = "\r\nSecondLife is a 3D World";
        UNSCOPED_INFO("stops at a carriage return");
        CHECK(arr == expected_result);
    }

    TEST_CASE("streamtools: skip_emptyspace skips whitespace and line ends", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "  \tSecondLife is a 3D World.\n");
        skip_emptyspace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World.\n";
        UNSCOPED_INFO("spaces and tabs");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "  \t\r\n    \r    SecondLife is a 3D World.\n");
        skip_emptyspace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World.\n";
        UNSCOPED_INFO("spaces, tabs, carriage returns and newlines");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "");
        ret = skip_emptyspace(is);
        is.get(arr, 255, '\0');
        UNSCOPED_INFO("empty string");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "  \r\n  \t ");
        ret = skip_emptyspace(is);
        is.get(arr, 255, '\0');
        UNSCOPED_INFO("only empty space");
        CHECK_FALSE(ret);
    }

    TEST_CASE("streamtools: skip_comments_and_emptyspace skips comment lines and empty space", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "  \t\r\n    \r    SecondLife is a 3D World.\n");
        skip_comments_and_emptyspace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World.\n";
        UNSCOPED_INFO("spaces, tabs, carriage returns and newlines");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "#    \r\n    SecondLife is a 3D World.");
        skip_comments_and_emptyspace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World.";
        UNSCOPED_INFO("a comment line");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "#    \r\n  #  SecondLife is a 3D World. ##");
        CHECK_FALSE(skip_comments_and_emptyspace(is));
        CHECK(is.eof());
        // don't get(): given bad state, we can't rely on results

        is.clear();
        is.str(str = " \r\n  SecondLife is a 3D World. ##");
        skip_comments_and_emptyspace(is);
        is.get(arr, 255, '\0');
        expected_result = "SecondLife is a 3D World. ##";
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "");
        ret = skip_comments_and_emptyspace(is);
        UNSCOPED_INFO("empty string");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "  \r\n  \t # SecondLife is a 3D World");
        ret = skip_comments_and_emptyspace(is);
        UNSCOPED_INFO("only empty space and a comment");
        CHECK_FALSE(ret);
    }

    TEST_CASE("streamtools: skip_line skips past the next newline", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "SecondLife is a 3D World.\n\n It provides an opportunity to the site \nuser to perform real life activities in virtual world.");
        skip_line(is);
        is.get(arr, 255, '\0');
        expected_result = "\n It provides an opportunity to the site \nuser to perform real life activities in virtual world.";
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        skip_line(is);
        is.get(arr, 255, '\0');
        expected_result = " It provides an opportunity to the site \nuser to perform real life activities in virtual world.";
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        skip_line(is);
        is.get(arr, 255, '\0');
        expected_result = "user to perform real life activities in virtual world.";
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "");
        ret = skip_line(is);
        UNSCOPED_INFO("empty string");
        CHECK_FALSE(ret);
    }


    TEST_CASE("streamtools: skip_to_next_word skips the word and the delimiters after it", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "SecondLife is a 3D_World.\n\n It-provides an opportunity to the site \nuser to perform real life activities in virtual world.");
        skip_to_next_word(is); // get past SecondLife
        is.get(arr, 255, '\0');
        expected_result = "is a 3D_World.\n\n It-provides an opportunity to the site \nuser to perform real life activities in virtual world.";
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        skip_to_next_word(is); // get past is
        skip_to_next_word(is); // get past a
        skip_to_next_word(is); // get past 3D_World.\n\n
        is.get(arr, 255, '\0');
        expected_result = "It-provides an opportunity to the site \nuser to perform real life activities in virtual world.";
        UNSCOPED_INFO("past a full stop and newlines");
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        skip_to_next_word(is); // get past It-
        expected_result = "provides an opportunity to the site \nuser to perform real life activities in virtual world.";
        is.get(arr, 255, '\0');
        UNSCOPED_INFO("past a hyphen");
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "");
        ret = skip_to_next_word(is);
        UNSCOPED_INFO("empty string");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "                   \r\n\r\n");
        ret = skip_to_next_word(is);
        UNSCOPED_INFO("only spaces and newlines");
        CHECK_FALSE(ret);
    }


    TEST_CASE("streamtools: skip_to_end_of_next_keyword finds a keyword that starts a line", "[streamtools]")
    {
        char arr[255];
        std::string str;
        std::string expected_result;
        std::istringstream is;
        bool ret;

        is.clear();
        is.str(str = "FIRSTKEY followed by second delimiter\nSECONDKEY\t SecondValue followed by third delimiter   \nSECONDKEY\nFOURTHKEY FOURTHVALUEis a 3DWorld.");
        ret = skip_to_end_of_next_keyword("FIRSTKEY", is);
        is.get(arr, 255, '\0');
        expected_result = " followed by second delimiter\nSECONDKEY\t SecondValue followed by third delimiter   \nSECONDKEY\nFOURTHKEY FOURTHVALUEis a 3DWorld.";
        CHECK(ret);
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        ret = skip_to_end_of_next_keyword("SECONDKEY", is);
        is.get(arr, 255, '\0');
        expected_result = "\t SecondValue followed by third delimiter   \nSECONDKEY\nFOURTHKEY FOURTHVALUEis a 3DWorld.";
        CHECK(ret);
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        ret = skip_to_end_of_next_keyword("SECONDKEY", is);
        is.get(arr, 255, '\0');
        expected_result = "\nFOURTHKEY FOURTHVALUEis a 3DWorld.";
        CHECK(ret);
        CHECK(arr == expected_result);

        is.clear();
        is.str(expected_result);
        ret = skip_to_end_of_next_keyword("FOURTHKEY", is);
        is.get(arr, 255, '\0');
        expected_result = " FOURTHVALUEis a 3DWorld.";
        CHECK(ret);
        CHECK(arr == expected_result);

        is.clear();
        is.str(str = "{should be skipped as newline/space/tab does not follow but this one should be picked\n { Does it?\n");
        ret = skip_to_end_of_next_keyword("{", is);
        is.get(arr, 255, '\0');
        expected_result = " Does it?\n";
        {
            INFO("a match not followed by whitespace is passed over");
            CHECK(ret);
            CHECK(arr == expected_result);
        }

        is.clear();
        is.str(str = "Delim { could not be found at start");
        ret = skip_to_end_of_next_keyword("{", is);
        UNSCOPED_INFO("keyword not at the start of a line");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "Empty Delim");
        ret = skip_to_end_of_next_keyword("", is);
        UNSCOPED_INFO("an empty keyword is not valid");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "");
        ret = skip_to_end_of_next_keyword("}", is);
        UNSCOPED_INFO("empty string");
        CHECK_FALSE(ret);
    }

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
        CHECK(actual_result == expected_result);
        CHECK(ret);

        actual_result = "";
        ret = get_word(actual_result, is);
        expected_result = "Second";
        CHECK(actual_result == expected_result);
        CHECK(ret);

        actual_result = "";
        ret = get_word(actual_result, is);
        expected_result = "Third";
        CHECK(actual_result == expected_result);
        CHECK(ret);

        // the current implementation of get_word seems inconsistent with
        // skip_to_next_word. skip_to_next_word treats any character other
        // than alhpa-numeric and '_' as a delimter, while get_word()
        // treats only isspace() (i.e. space,  form-feed('\f'),  newline  ('\n'),
        // carriage  return ('\r'), horizontal tab ('\t'), and vertical tab ('\v')
        // as delimiters
        actual_result = "";
        ret = get_word(actual_result, is); // will copy Fourth-ShouldThisBePartOfFourth
        expected_result = "Fourth-ShouldThisBePartOfFourth"; // as per current impl.
        CHECK(actual_result == expected_result);
        CHECK(ret);

        actual_result = "";
        ret = get_word(actual_result, is); // will copy Fifth
        expected_result = "Fifth"; // as per current impl.
        CHECK(actual_result == expected_result);
        // the newline after it is still to be read
        CHECK(ret);

        is.clear();
        is.str(str = "  \t \r  \n ");
        actual_result = "";
        ret = get_word(actual_result, is);
        UNSCOPED_INFO("only whitespace");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "");
        actual_result = "";
        ret = get_word(actual_result, is);
        UNSCOPED_INFO("empty string");
        CHECK_FALSE(ret);
    }

    TEST_CASE("streamtools: get_word splits only at whitespace where skip_to_next_word splits at anything but a letter digit or underscore", "[streamtools]")
    {
        // get_word() treats only isspace() characters (space, form feed,
        // newline, carriage return, horizontal and vertical tab) as
        // delimiters, while skip_to_next_word treats any character other
        // than a letter, a digit or '_' as one. So a hyphen splits a word for
        // skip_to_next_word but not for get_word, and an underscore splits it
        // for neither.
        std::string str;
        std::string fifth_by_get_word;
        std::string fifth_by_skipping;
        std::string word;
        std::istringstream is;

        is.clear();
        is.str(str = "  First Second \t \r  \n Third  Fourth-ShouldThisBePartOfFourth  Fifth\n");
        get_word(word, is); // First
        word = "";
        get_word(word, is); // Second
        word = "";
        get_word(word, is); // Third
        word = "";
        get_word(word, is); // Fourth-ShouldThisBePartOfFourth
        get_word(fifth_by_get_word, is);
        CHECK(fifth_by_get_word == "Fifth");

        is.clear();
        is.str(str);
        skip_to_next_word(is);  // should now point to First
        skip_to_next_word(is);  // should now point to Second
        skip_to_next_word(is);  // should now point to Third
        skip_to_next_word(is);  // should now point to Fourth
        skip_to_next_word(is);  // should now point to ShouldThisBePartOfFourth
        get_word(fifth_by_skipping, is);
        UNSCOPED_INFO("skip_to_next_word splits at the hyphen");
        CHECK(fifth_by_skipping == "ShouldThisBePartOfFourth");

        // With an underscore in place of the hyphen, skip_to_next_word's fifth
        // word is "Fifth" too.
        is.clear();
        is.str(str = "  First Second \t \r  \n Third  Fourth_ShouldThisBePartOfFourth Fifth\n");
        skip_to_next_word(is);  // should now point to First
        skip_to_next_word(is);  // should now point to Second
        skip_to_next_word(is);  // should now point to Third
        skip_to_next_word(is);  // should now point to Fourth_ShouldThisBePartOfFourth
        skip_to_next_word(is);  // should now point to Fifth
        fifth_by_skipping = "";
        get_word(fifth_by_skipping, is);
        UNSCOPED_INFO("skip_to_next_word keeps an underscore in the word");
        CHECK(fifth_by_skipping == "Fifth");
    }

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
        CHECK(actual_result == expected_result);
        CHECK(ret);

        actual_result = "";
        ret = get_word(actual_result, is, 4);
        expected_result = "Seco"; // should be cut short
        CHECK(actual_result == expected_result);
        CHECK(ret);

        actual_result = "";
        ret = get_word(actual_result, is, 255);
        expected_result = "nd"; // get remainder of Second
        CHECK(actual_result == expected_result);
        CHECK(ret);

        actual_result = "";
        ret = get_word(actual_result, is, 0); // 0 size string
        expected_result = ""; // reads nothing, though it still skips the whitespace before Third
        UNSCOPED_INFO("a limit of zero");
        CHECK(actual_result == expected_result);
        CHECK(ret);

        actual_result = "";
        ret = get_word(actual_result, is, 255);
        expected_result = "Third";
        CHECK(actual_result == expected_result);
        CHECK(ret);

        is.clear();
        is.str(str = "  \t \r  \n ");
        actual_result = "";
        ret = get_word(actual_result, is, 255);
        UNSCOPED_INFO("only whitespace");
        CHECK_FALSE(ret);

        is.clear();
        is.str(str = "");
        actual_result = "";
        ret = get_word(actual_result, is, 255);
        UNSCOPED_INFO("empty string");
        CHECK_FALSE(ret);
    }

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
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is);
        expected_result = " Third  Fourth-ShouldThisBePartOfFourth  IsThisFifth\n";
        CHECK(actual_result == expected_result);

        is.clear();
        is.str(str = "\nFirst Line.\n\nSecond Line.\n");
        actual_result = "";
        get_line(actual_result, is);
        expected_result = "\n";
        UNSCOPED_INFO("a newline first");
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is);
        expected_result = "First Line.\n";
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is);
        expected_result = "\n";
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is);
        expected_result = "Second Line.\n";
        CHECK(actual_result == expected_result);
    }

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
        CHECK(actual_result == expected_result);
        CHECK(ret == is.good());
    }

    TEST_CASE("streamtools: get_line keeps a lone carriage return", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        std::string actual_result;
        std::istringstream is;

        // A line ends only at '\n', so a '\r' on its own is part of the line.
        is.clear();
        is.str(str = "Should not skip lone \r.\r\n");
        actual_result = "";
        get_line(actual_result, is);
        expected_result = "Should not skip lone \r.\r\n";
        CHECK(actual_result == expected_result);
    }

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
        CHECK(actual_result == expected_result);
    }


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
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is, sizeof("Second")-1);
        expected_result = "Second\n";
        UNSCOPED_INFO("cut short");
        CHECK(actual_result == expected_result);

        actual_result = "";
        get_line(actual_result, is, 255);
        expected_result = " Line.\n";
        UNSCOPED_INFO("the rest after a cut");
        CHECK(actual_result == expected_result);

        is.clear();
        is.str(str = "One Line only with no newline with limited length");
        actual_result = "";
        get_line(actual_result, is, sizeof("One Line only with no newline with limited length")-1);
        expected_result = "One Line only with no newline with limited length\n";
        UNSCOPED_INFO("no newline and a limit of its length");
        CHECK(actual_result == expected_result);

        is.clear();
        is.str(str = "One Line only with no newline");
        actual_result = "";
        get_line(actual_result, is, 255);
        expected_result = "One Line only with no newline";
        UNSCOPED_INFO("no newline");
        CHECK(actual_result == expected_result);
    }

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
        CHECK(actual_result == expected_result);
        CHECK(ret == is.good());
    }

    TEST_CASE("streamtools: remove_last_char removes only a matching last character", "[streamtools]")
    {
        std::string str;
        std::string expected_result;
        bool ret;

        str = "SecondLife is a 3D World";

        ret = remove_last_char('d',str);
        expected_result = "SecondLife is a 3D Worl";
        CHECK(str == expected_result);
        CHECK(ret);

        str = "SecondLife is a 3D World";
        ret = remove_last_char('W',str);
        expected_result = "SecondLife is a 3D World";
        UNSCOPED_INFO("not the last char");
        CHECK(str == expected_result);
        CHECK_FALSE(ret);

        str = "SecondLife is a 3D World\n";
        ret = remove_last_char('\n',str);
        expected_result = "SecondLife is a 3D World";
        UNSCOPED_INFO("a trailing newline");
        CHECK(str == expected_result);
        CHECK(ret);
    }


    TEST_CASE("streamtools: unescape_string unescapes newlines and backslashes only", "[streamtools]")
    {
        std::string str;
        std::string expected_result;

        str = "SecondLife is a 3D world \\n";
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \n";
        UNSCOPED_INFO("newline");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \\\\t \\n";
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \\t \n";
        UNSCOPED_INFO("backslash and newline");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \\ ";
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \\ ";
        UNSCOPED_INFO("a lone trailing backslash");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \\n \\n \\n \\\\\\n";
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \n \n \n \\\n";
        UNSCOPED_INFO("several newlines and a backslash");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \\t";
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \\t";
        UNSCOPED_INFO("leaves a tab as is");
        CHECK(str == expected_result);

        str = "\\n";
        unescape_string(str);
        expected_result = "\n";
        UNSCOPED_INFO("only a newline");
        CHECK(str == expected_result);
    }

    TEST_CASE("streamtools: escape_string escapes newlines and backslashes and round-trips", "[streamtools]")
    {
        std::string str;
        std::string expected_result;

        str = "SecondLife is a 3D world \n";
        escape_string(str);
        expected_result = "SecondLife is a 3D world \\n";
        UNSCOPED_INFO("newline");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \\t \n";
        escape_string(str);
        expected_result = "SecondLife is a 3D world \\\\t \\n";
        UNSCOPED_INFO("backslash and newline");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \n \n \n \\\n";
        escape_string(str);
        expected_result = "SecondLife is a 3D world \\n \\n \\n \\\\\\n";
        UNSCOPED_INFO("several newlines and a backslash");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world \t";
        escape_string(str);
        expected_result = "SecondLife is a 3D world \t";
        UNSCOPED_INFO("leaves a tab as is");
        CHECK(str == expected_result);

        str = "\n";
        escape_string(str);
        expected_result = "\\n";
        UNSCOPED_INFO("only a newline");
        CHECK(str == expected_result);

        // serialization/deserialization escape->unescape
        str = "SecondLife is a 3D world \n \n \n \\\n";
        escape_string(str);
        unescape_string(str);
        expected_result = "SecondLife is a 3D world \n \n \n \\\n";
        CHECK(str == expected_result);

        // serialization/deserialization  unescape->escape
        str = "SecondLife is a 3D world \\n \\n \\n \\\\";
        unescape_string(str);
        escape_string(str);
        expected_result = "SecondLife is a 3D world \\n \\n \\n \\\\";
        CHECK(str == expected_result);
    }

    TEST_CASE("streamtools: replace_newlines_with_whitespace replaces only newlines", "[streamtools]")
    {
        std::string str;
        std::string expected_result;

        str = "SecondLife is a 3D \n\nworld\n";
        replace_newlines_with_whitespace(str);
        expected_result = "SecondLife is a 3D   world ";
        UNSCOPED_INFO("every newline");
        CHECK(str == expected_result);

        str = "\nSecondLife is a 3D world\n";
        replace_newlines_with_whitespace(str);
        expected_result = " SecondLife is a 3D world ";
        UNSCOPED_INFO("newlines at both ends");
        CHECK(str == expected_result);

        str = "SecondLife is a 3D world\r\t";
        replace_newlines_with_whitespace(str);
        expected_result = "SecondLife is a 3D world\r\t";
        UNSCOPED_INFO("carriage returns and tabs are kept");
        CHECK(str == expected_result);

        str = "";
        replace_newlines_with_whitespace(str);
        expected_result = "";
        UNSCOPED_INFO("empty string");
        CHECK(str == expected_result);
    }

    TEST_CASE("streamtools: remove_double_quotes removes paired and lone double quotes", "[streamtools]")
    {
        std::string str;
        std::string expected_result;

        str = "SecondLife is a \"\"3D world";
        remove_double_quotes(str);
        expected_result = "SecondLife is a 3D world";
        UNSCOPED_INFO("an empty pair of quotes");
        CHECK(str == expected_result);

        str = "SecondLife is a \"3D world";
        remove_double_quotes(str);
        expected_result = "SecondLife is a 3D world";
        UNSCOPED_INFO("a lone quote is removed too");
        CHECK(str == expected_result);
    }

    TEST_CASE("streamtools: get_keyword_and_value splits off the first word as the keyword", "[streamtools]")
    {
        std::string s = "SecondLife is a 3D World";
        std::string keyword;
        std::string value;
        get_keyword_and_value(keyword, value, s);
        CHECK(keyword == "SecondLife");
        CHECK(value == "is a 3D World");

        s = "SecondLife";
        get_keyword_and_value(keyword, value, s);
        CHECK(keyword == "SecondLife");
        CHECK(value == "");

        s = "SecondLife \t  is cool!     \n";
        get_keyword_and_value(keyword, value, s);
        {
            INFO("space before the value is dropped but not after");
            CHECK(keyword == "SecondLife");
            CHECK(value == "is cool!     ");
        }
    }

    TEST_CASE("streamtools: get_keyword_and_value gives no value after a keyword and newline", "[streamtools]")
    {
        std::string s;
        std::string keyword = "SOME PRIOR KEYWORD";
        std::string value = "SOME PRIOR VALUE";

        s = "SecondLife\n";
        get_keyword_and_value(keyword, value, s);
        CHECK(keyword == "SecondLife");
        CHECK(value == "");
    }

    TEST_CASE("streamtools: get_keyword_and_value of an empty string clears both", "[streamtools]")
    {
        std::string s;
        std::string keyword = "SOME PRIOR KEYWORD";
        std::string value = "SOME PRIOR VALUE";

        s = "";
        get_keyword_and_value(keyword, value, s);
        CHECK(keyword == "");
        CHECK(value == "");
    }

    TEST_CASE("streamtools: fullread reads across newlines up to the requested count", "[streamtools]")
    {
        std::string str = "First Line.\nSecond Line\n";
        std::istringstream is(str);
        char buf[255] = {0};

        CHECK(fullread(is, buf, 255) == std::streamsize(str.size()));
        CHECK_THAT(ll_test::Memory(buf, str.size()), ll_test::MemoryMatches(str.c_str(), str.size()));

        is.clear();
        is.str(str = "First Line.\nSecond Line\n");
        memset(buf, 0, 255);

        char expected_string[] = "First Line.\nSecond";
        int len = sizeof(expected_string)-1;
        CHECK(fullread(is, buf, len) == len);
        CHECK_THAT(ll_test::Memory(buf, len), ll_test::MemoryMatches(expected_string, len));
    }


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
        CHECK(arr == expected_result);

        stream.clear();
        stream.str(str = "SecondLife is a 3D World");
        toCheck = "is";
        stream >> toCheck.c_str();
        CHECK_FALSE(stream.good());
    }
}
