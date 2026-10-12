/**
 * @file llfile_test.cpp
 * @author Frederick Martian
 * @date 2025-11
 * @brief LLFile test cases.
 *
 * $LicenseInfo:firstyear=2025&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2025, Linden Research, Inc.
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
#include "llfile.h"
#include "lluuid.h"

namespace
{
    static void clear_entire_dir(const std::filesystem::path& dir_path)
    {
        std::error_code ec;
        std::filesystem::remove_all(dir_path, ec);
    }

    static std::filesystem::path append_filename(const std::filesystem::path& dir, const std::u8string& element)
    {
        std::filesystem::path path = dir;
        return path.append(element);
    }

    // Each named for its test alone, by `unique`: two runs at once, or a
    // test that failed before it cleaned up, cannot meet another's files.
    static std::filesystem::path get_testdir(const std::filesystem::path& tempdir, const std::string& unique)
    {
        return append_filename(tempdir, std::u8string(u8"test_dir_") + std::u8string(unique.begin(), unique.end()));
    }

    static std::filesystem::path get_testdir_unicode(const std::filesystem::path& tempdir, const std::string& unique)
    {
        // Example Unicode directory name: "test_ユニコード_dir"
        return append_filename(tempdir, std::u8string(u8"test_\xE3\x83\xA6\xE3\x83\x8B\xE3\x82\xB3\xE3\x83\xBC\xE3\x83\x89_dir_") +
                                            std::u8string(unique.begin(), unique.end()));
    }

    struct llfile_test
    {
        std::string           unique = LLUUID::generateNewID().asString();
        std::filesystem::path tempdir = LLFile::tmpdir();
        std::filesystem::path testdir = get_testdir(tempdir, unique);

        // whatever a test leaves in its folders goes with it
        ~llfile_test()
        {
            clear_entire_dir(testdir);
            clear_entire_dir(get_testdir_unicode(tempdir, unique));
        }
    };

    TEST_CASE_METHOD(llfile_test, "llfile_test: exists isdir and isfile follow mkdir write and remove", "[llfile_test]")
    {
        // Test creating directories and files and deleting them and checking if the
        // relevant status functions work as expected
        UNSCOPED_INFO("LLFile::tmpdir() empty");
        REQUIRE_FALSE(tempdir.empty());
        UNSCOPED_INFO("LLFile::tmpdir() doesn't exist");
        CHECK(LLFile::exists(tempdir));
        UNSCOPED_INFO("LLFile::tmpdir() is not a directory");
        CHECK(LLFile::isdir(tempdir));
        UNSCOPED_INFO("LLFile::tmpdir() should not be a file");
        CHECK_FALSE(LLFile::isfile(tempdir));

        // Make sure there is nothing left from a previous test run
        clear_entire_dir(testdir);
        UNSCOPED_INFO("llfile_test should not exist anymore");
        CHECK_FALSE(LLFile::exists(testdir));

        int rc = LLFile::mkdir(testdir);
        UNSCOPED_INFO("LLFile::mkdir() failed");
        CHECK(rc == 0);
        UNSCOPED_INFO("llfile_test should be a directory");
        CHECK(LLFile::isdir(testdir));
        rc = LLFile::mkdir(testdir);
        UNSCOPED_INFO("LLFile::mkdir() should not fail when the directory already exists");
        CHECK(rc == 0);

        std::filesystem::path testfile1 = testdir;
        testfile1.append("llfile_test.dat");
        UNSCOPED_INFO("llfile_test1.dat should not yet exist");
        CHECK_FALSE(LLFile::exists(testfile1));

        const char* testdata = "testdata";
        S64 bytes = LLFile::write(testfile1, testdata, 0, sizeof(testdata));
        UNSCOPED_INFO("LLFile::write() did not write correctly");
        CHECK(bytes == sizeof(testdata));

        rc = LLFile::remove(testfile1);
        UNSCOPED_INFO("LLFile::remove() for file test_file.dat");
        CHECK(rc == 0);
        UNSCOPED_INFO("llfile_test.dat should not exist anymore");
        CHECK_FALSE(LLFile::exists(testfile1));
        UNSCOPED_INFO("llfile_test.dat should not be a file");
        CHECK_FALSE(LLFile::isfile(testfile1));
        UNSCOPED_INFO("llfile_test.dat should not be a directory");
        CHECK_FALSE(LLFile::isdir(testfile1));
        UNSCOPED_INFO("llfile_test.dat should not be a symlink");
        CHECK_FALSE(LLFile::islink(testfile1));

        rc = LLFile::remove(testdir);
        UNSCOPED_INFO("LLFile::remove() for directory llfile_test failed");
        CHECK(rc == 0);
        UNSCOPED_INFO("llfile_test should not exist anymore");
        CHECK_FALSE(LLFile::exists(testdir));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: the static write size getContents and read agree on a file", "[llfile_test]")
    {
        // High level static file IO functions to read and write data files
        LLFile::mkdir(testdir);
        UNSCOPED_INFO("llfile_test should exist");
        CHECK(LLFile::isdir(testdir));

        std::filesystem::path testfile1 = testdir;
        testfile1.append("llfile_test.dat");

        std::string testdata1("testdata");
        std::string testdata2("datateststuff");
        std::time_t current = time(nullptr);
        S64 bytes = LLFile::write(testfile1, testdata1.c_str(), 0, testdata1.length());
        UNSCOPED_INFO("LLFile::write() did not write correctly");
        REQUIRE(bytes == testdata1.length());
        UNSCOPED_INFO("llfile_test.dat should exist");
        CHECK(LLFile::exists(testfile1));
        UNSCOPED_INFO("llfile_test.dat should be a file");
        CHECK(LLFile::isfile(testfile1));
        UNSCOPED_INFO("llfile_test.dat should not be a directory");
        CHECK_FALSE(LLFile::isdir(testfile1));

        bytes = LLFile::size(testfile1);
        UNSCOPED_INFO("LLFile::size() did not return the correct size");
        REQUIRE(bytes == testdata1.length());

        std::string data = LLFile::getContents(testfile1);
        UNSCOPED_INFO("LLFile::getContents() did not return the correct size data");
        REQUIRE(data.length() == testdata1.length());
        UNSCOPED_INFO("LLFile::getContents() did not read correct data");
        REQUIRE_THAT(ll_test::Memory(testdata1.c_str(), (U32)testdata1.length()), ll_test::MemoryMatches(data.c_str(), (U32)data.length()));

        char buffer[1024];
        bytes = LLFile::read(testfile1, buffer, 0, testdata1.length());
        UNSCOPED_INFO("LLFile:read() did not return the correct size");
        REQUIRE(bytes == testdata1.length());
        UNSCOPED_INFO("LLFile::read() did not read correct data");
        REQUIRE_THAT(ll_test::Memory(testdata1.c_str(), (U32)bytes), ll_test::MemoryMatches(buffer, (U32)bytes));

        // What if we try to read more data than there is in the file?
        bytes = LLFile::read(testfile1, buffer, 0, bytes + 10);
        UNSCOPED_INFO("LLFile:read() did not correctly stop on eof");
        REQUIRE(bytes == testdata1.length());
        UNSCOPED_INFO("LLFile::read() did not read correct data");
        REQUIRE_THAT(ll_test::Memory(testdata1.c_str(), (U32)bytes), ll_test::MemoryMatches(buffer, (U32)bytes));

        // Let's append more data
        bytes = LLFile::write(testfile1, testdata2.c_str(), -1, testdata2.length());
        UNSCOPED_INFO("LLFile::write() did not write correctly");
        REQUIRE(bytes == testdata2.length());

        bytes = LLFile::size(testfile1);
        UNSCOPED_INFO("LLFile::size() did not return the correct size");
        REQUIRE(bytes == testdata1.length() + testdata2.length());
        bytes = LLFile::read(testfile1, buffer, 0, bytes);
        UNSCOPED_INFO("LLFile:read() did not read correct number of bytes");
        REQUIRE(bytes == testdata1.length() + testdata2.length());
        UNSCOPED_INFO("LLFile:read() did not read correct testdata1");
        REQUIRE_THAT(ll_test::Memory(testdata1.c_str(), (U32)testdata1.length()), ll_test::MemoryMatches(buffer, (U32)testdata1.length()));
        UNSCOPED_INFO("LLFile:read() did not read correct testdata2");
        REQUIRE_THAT(ll_test::Memory(testdata2.c_str(), (U32)testdata2.length()), ll_test::MemoryMatches(buffer + testdata1.length(), (U32)testdata2.length()));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: an LLFile writes reads seeks and tells", "[llfile_test]")
    {
        const size_t numints = 1024;

        // Testing the LLFile class implementation
        LLFile::mkdir(testdir);
        std::filesystem::path testfile = testdir;
        testfile.append("llfile_test.bin");

        int data[numints];
        for (int &t : data)
        {
            t = rand();
        }

        std::error_code ec;
        LLFile fileout(testfile, LLFile::out, ec);
        UNSCOPED_INFO("LLFile constructor did not open correctly");
        REQUIRE((bool)fileout);
        UNSCOPED_INFO("error_code from LLFile constructor should not indicate an error");
        REQUIRE_FALSE(ec);
        if (fileout)
        {
            S64 length = fileout.size(ec);
            UNSCOPED_INFO("freshly created file should be empty");
            CHECK(length == 0);
            UNSCOPED_INFO("error_code from LLFile::size() should not indicate an error");
            REQUIRE_FALSE(ec);
            S64 bytes = fileout.write(data, sizeof(data), ec);
            UNSCOPED_INFO("LLFile::write() did not write correctly");
            CHECK(bytes == sizeof(data));
            UNSCOPED_INFO("error_code from LLFile::write() should not indicate an error");
            REQUIRE_FALSE(ec);
            bytes = fileout.write(data, sizeof(data), ec);
            UNSCOPED_INFO("LLFile::write() did not write correctly");
            CHECK(bytes == sizeof(data));
            UNSCOPED_INFO("error_code from LLFile::write() should not indicate an error");
            REQUIRE_FALSE(ec);
            bytes = fileout.size(ec);
            UNSCOPED_INFO("LLFile::size() returned wrong size");
            CHECK(bytes == 2 * sizeof(data));
            UNSCOPED_INFO("error_code from LLFile::size() should not indicate an error");
            REQUIRE_FALSE(ec);
            fileout.close();
        }

        LLFile filein(testfile, LLFile::in, ec);
        UNSCOPED_INFO("LLFile constructor did not open correctly");
        REQUIRE((bool)filein);
        UNSCOPED_INFO("error_code from LLFile constructor should not indicate an error");
        REQUIRE_FALSE(ec);
        if (filein)
        {
            S64 length = filein.size(ec);
            UNSCOPED_INFO("LLFile::size() returned wrong size");
            CHECK(length == 2 * sizeof(data));
            UNSCOPED_INFO("error_code from LLFile::size() should not indicate an error");
            REQUIRE_FALSE(ec);
            char* buffer = (char*)malloc(length);
            S64 bytes  = filein.read(buffer, length, ec);
            UNSCOPED_INFO("LLFile::read() did not read correctly");
            CHECK(bytes == length);
            UNSCOPED_INFO("error_code from LLFile::read() should not indicate an error");
            REQUIRE_FALSE(ec);
            UNSCOPED_INFO("LLFile:read() did not read correct data1");
            REQUIRE_THAT(ll_test::Memory(data, (U32)sizeof(data)), ll_test::MemoryMatches(buffer, (U32)sizeof(data)));
            UNSCOPED_INFO("LLFile:read() did not read correct data2");
            REQUIRE_THAT(ll_test::Memory(data, (U32)sizeof(data)), ll_test::MemoryMatches(buffer + sizeof(data), (U32)sizeof(data)));
            S64 offset = filein.tell(ec);
            UNSCOPED_INFO("LLFile::tell() returned a bad offset");
            CHECK(offset == length);
            UNSCOPED_INFO("error_code from LLFile::read() should not indicate an error");
            REQUIRE_FALSE(ec);
            offset = sizeof(data) / 2;
            int rc = filein.seek(offset, ec);
            UNSCOPED_INFO("LLFile::seek() indicated an error");
            CHECK(rc == 0);
            UNSCOPED_INFO("error_code from LLFile::seek() should not indicate an error");
            REQUIRE_FALSE(ec);
            bytes = filein.read(buffer, 2 * sizeof(data), ec);
            UNSCOPED_INFO("LLFile::read() did not read correctly");
            CHECK(bytes == sizeof(data) + offset);
            UNSCOPED_INFO("error_code from LLFile::read() should not indicate an error");
            REQUIRE_FALSE(ec);
            UNSCOPED_INFO("LLFile:read() did not read correct data3");
            REQUIRE_THAT(ll_test::Memory((char*)data + offset, (U32)offset), ll_test::MemoryMatches(buffer, (U32)offset));
            UNSCOPED_INFO("LLFile:read() did not read correct data4");
            REQUIRE_THAT(ll_test::Memory((char*)data, (U32)sizeof(data)), ll_test::MemoryMatches(buffer + offset, (U32)sizeof(data)));
            filein.close();

            free(buffer);
        }
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: LLFile refuses conflicting flags and missing paths", "[llfile_test]")
    {
        // Testing the LLFile class implementation with wrong paths and parameters
        std::filesystem::path testfile = testdir;
        testfile.append("llfile_test.bin");

        // a file already there, for noreplace to refuse
        LLFile::mkdir(testdir);
        const char* testdata = "testdata";
        LLFile::write(testfile, testdata, 0, strlen(testdata));

        std::error_code ec;
        LLFile file(testfile, LLFile::out | LLFile::noreplace, ec);
        UNSCOPED_INFO("LLFile constructor should not have opened the already existing file");
        CHECK(!file);
        UNSCOPED_INFO("error_code from LLFile constructor should indicate an error");
        REQUIRE((bool)ec);

        LLFile::remove(testfile);
        file = LLFile(testfile, LLFile::out | LLFile::app | LLFile::trunc, ec);
        UNSCOPED_INFO("LLFile constructor should not have opened the file with conflicting flags");
        CHECK(!file);
        UNSCOPED_INFO("error_code from LLFile constructor should indicate an error");
        REQUIRE((bool)ec);

        file = LLFile(testfile, LLFile::out | LLFile::app | LLFile::noreplace, ec);
        UNSCOPED_INFO("LLFile constructor should not have opened the file with conflicting flags");
        CHECK(!file);
        UNSCOPED_INFO("error_code from LLFile constructor should indicate an error");
        REQUIRE((bool)ec);

        testfile = testdir;
        testfile.append("llfile_test");
        testfile.append("llfile_test.bin");

        file = LLFile(testfile, LLFile::in, ec);
        UNSCOPED_INFO("LLFile constructor should not have been able to open the file in the non-existing directory");
        CHECK(!file);
        UNSCOPED_INFO("error_code from LLFile constructor should indicate an error");
        REQUIRE((bool)ec);
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: mkdir write read and remove work with Unicode names", "[llfile_test]")
    {
        // Test file and directory operations with Unicode paths and filenames
        std::filesystem::path testdir_unicode = get_testdir_unicode(tempdir, unique);

        // Unicode filename: "ファイル_テスト.bin" (means "file_test.bin" in Japanese)
        std::string unicode_filename = "\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB_\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88.bin";
        std::filesystem::path testfile_unicode = testdir_unicode;
        testfile_unicode.append(unicode_filename);

        // Clean up any previous test artifacts
        clear_entire_dir(testdir_unicode);
        UNSCOPED_INFO("Unicode test directory should not exist");
        CHECK_FALSE(LLFile::exists(testdir_unicode));

        // Create the Unicode directory
        int rc = LLFile::mkdir(testdir_unicode);
        UNSCOPED_INFO("LLFile::mkdir() failed for Unicode directory");
        CHECK(rc == 0);
        UNSCOPED_INFO("Unicode test directory should exist");
        CHECK(LLFile::isdir(testdir_unicode));


        UNSCOPED_INFO("Unicode test file should not exist");
        CHECK_FALSE(LLFile::exists(testfile_unicode));

        // Write to the Unicode file
        const char* testdata = "unicode_testdata";
        S64 bytes = LLFile::write(testfile_unicode, testdata, 0, strlen(testdata));
        UNSCOPED_INFO("Unicode test file should exist");
        CHECK(LLFile::exists(testfile_unicode));
        UNSCOPED_INFO("Unicode test file should be a file");
        CHECK(LLFile::isfile(testfile_unicode));
        UNSCOPED_INFO("LLFile::write() did not write correctly to Unicode file");
        CHECK(bytes == (S64)strlen(testdata));

        // Read back the data
        char buffer[64] = {};
        bytes = LLFile::read(testfile_unicode, buffer, 0, sizeof(buffer));
        UNSCOPED_INFO("LLFile::read() did not read correctly from Unicode file");
        CHECK(bytes == (S64)strlen(testdata));
        UNSCOPED_INFO("LLFile::read() did not read correct Unicode data");
        REQUIRE_THAT(ll_test::Memory(testdata, (U32)bytes), ll_test::MemoryMatches(buffer, (U32)bytes));

        // Remove the file and directory
        rc = LLFile::remove(testfile_unicode);
        UNSCOPED_INFO("LLFile::remove() failed for Unicode file");
        CHECK(rc == 0);
        UNSCOPED_INFO("Unicode test file should not exist after removal");
        CHECK_FALSE(LLFile::exists(testfile_unicode));

        rc = LLFile::remove(testdir_unicode);
        UNSCOPED_INFO("LLFile::remove() failed for Unicode directory");
        CHECK(rc == 0);
        UNSCOPED_INFO("Unicode test directory should not exist after removal");
        CHECK_FALSE(LLFile::exists(testdir_unicode));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName accepts ordinary file names", "[llfile_test]")
    {
        // Ordinary file names pass isSafeFileName()
        UNSCOPED_INFO("xml");
        CHECK(LLFile::isSafeFileName("lsl_keywords.xml"));
        UNSCOPED_INFO("two extensions");
        CHECK(LLFile::isSafeFileName("secondlife.d.luau"));
        UNSCOPED_INFO("no extension");
        CHECK(LLFile::isSafeFileName("keywords"));
        UNSCOPED_INFO("inner dots");
        CHECK(LLFile::isSafeFileName("a..b"));
        UNSCOPED_INFO("inner space");
        CHECK(LLFile::isSafeFileName("my file.txt"));
        UNSCOPED_INFO("hidden");
        CHECK(LLFile::isSafeFileName(".luaurc"));
        UNSCOPED_INFO("leading dots");
        CHECK(LLFile::isSafeFileName("..name"));
        UNSCOPED_INFO("unicode");
        CHECK(LLFile::isSafeFileName("\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB.bin"));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName refuses empty and dot names and ones Windows trims", "[llfile_test]")
    {
        // Nothing, the directory itself or its parent, and names Windows would
        // shorten when creating them
        UNSCOPED_INFO("empty");
        CHECK_FALSE(LLFile::isSafeFileName(""));
        UNSCOPED_INFO("dot");
        CHECK_FALSE(LLFile::isSafeFileName("."));
        UNSCOPED_INFO("dot dot");
        CHECK_FALSE(LLFile::isSafeFileName(".."));
        UNSCOPED_INFO("three dots");
        CHECK_FALSE(LLFile::isSafeFileName("..."));
        UNSCOPED_INFO("trailing dot");
        CHECK_FALSE(LLFile::isSafeFileName("name."));
        UNSCOPED_INFO("trailing space");
        CHECK_FALSE(LLFile::isSafeFileName("name.xml "));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName refuses path separators of either platform", "[llfile_test]")
    {
        // Separators of either platform, at either end or inside
        UNSCOPED_INFO("parent posix");
        CHECK_FALSE(LLFile::isSafeFileName("../../evil.xml"));
        UNSCOPED_INFO("parent windows");
        CHECK_FALSE(LLFile::isSafeFileName("..\\..\\evil.xml"));
        UNSCOPED_INFO("absolute posix");
        CHECK_FALSE(LLFile::isSafeFileName("/etc/evil"));
        UNSCOPED_INFO("absolute windows");
        CHECK_FALSE(LLFile::isSafeFileName("\\Windows\\evil.dll"));
        UNSCOPED_INFO("unc");
        CHECK_FALSE(LLFile::isSafeFileName("\\\\host\\share\\evil"));
        UNSCOPED_INFO("subdirectory posix");
        CHECK_FALSE(LLFile::isSafeFileName("sub/evil.xml"));
        UNSCOPED_INFO("subdirectory windows");
        CHECK_FALSE(LLFile::isSafeFileName("sub\\evil.xml"));
        UNSCOPED_INFO("trailing separator");
        CHECK_FALSE(LLFile::isSafeFileName("evil/"));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName refuses colons reserved characters and controls", "[llfile_test]")
    {
        // Drive and stream colons, the rest of what Windows refuses in a name,
        // and control characters, a NUL above all: a C string would cut a
        // harmless prefix off a harmful name there
        UNSCOPED_INFO("drive absolute");
        CHECK_FALSE(LLFile::isSafeFileName("C:\\evil.dll"));
        UNSCOPED_INFO("drive relative");
        CHECK_FALSE(LLFile::isSafeFileName("C:evil.dll"));
        UNSCOPED_INFO("stream");
        CHECK_FALSE(LLFile::isSafeFileName("name.xml:evil"));
        UNSCOPED_INFO("star");
        CHECK_FALSE(LLFile::isSafeFileName("a*b"));
        UNSCOPED_INFO("question");
        CHECK_FALSE(LLFile::isSafeFileName("a?b"));
        UNSCOPED_INFO("quote");
        CHECK_FALSE(LLFile::isSafeFileName("a\"b"));
        UNSCOPED_INFO("less");
        CHECK_FALSE(LLFile::isSafeFileName("a<b"));
        UNSCOPED_INFO("greater");
        CHECK_FALSE(LLFile::isSafeFileName("a>b"));
        UNSCOPED_INFO("pipe");
        CHECK_FALSE(LLFile::isSafeFileName("a|b"));
        UNSCOPED_INFO("tab");
        CHECK_FALSE(LLFile::isSafeFileName("a\tb"));
        UNSCOPED_INFO("newline");
        CHECK_FALSE(LLFile::isSafeFileName("a\nb"));
        UNSCOPED_INFO("unit separator");
        CHECK_FALSE(LLFile::isSafeFileName("a\x1F" "b"));
        UNSCOPED_INFO("only nul");
        CHECK_FALSE(LLFile::isSafeFileName(std::string(1, '\0')));
        UNSCOPED_INFO("inner nul");
        CHECK_FALSE(LLFile::isSafeFileName(std::string("name.xml\0evil", 13)));
        UNSCOPED_INFO("trailing nul");
        CHECK_FALSE(LLFile::isSafeFileName(std::string("name.xml\0", 9)));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName allows up to 255 bytes", "[llfile_test]")
    {
        // The length limit is inclusive
        UNSCOPED_INFO("at limit");
        CHECK(LLFile::isSafeFileName(std::string(255, 'a')));
        UNSCOPED_INFO("over limit");
        CHECK_FALSE(LLFile::isSafeFileName(std::string(256, 'a')));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName refuses Windows device names", "[llfile_test]")
    {
        // Windows device names, in any case, with or without an extension
        UNSCOPED_INFO("con");
        CHECK_FALSE(LLFile::isSafeFileName("CON"));
        UNSCOPED_INFO("prn");
        CHECK_FALSE(LLFile::isSafeFileName("PRN"));
        UNSCOPED_INFO("aux");
        CHECK_FALSE(LLFile::isSafeFileName("AUX"));
        UNSCOPED_INFO("nul");
        CHECK_FALSE(LLFile::isSafeFileName("NUL"));
        UNSCOPED_INFO("conin");
        CHECK_FALSE(LLFile::isSafeFileName("CONIN$"));
        UNSCOPED_INFO("conout");
        CHECK_FALSE(LLFile::isSafeFileName("CONOUT$"));
        UNSCOPED_INFO("com0");
        CHECK_FALSE(LLFile::isSafeFileName("COM0"));
        UNSCOPED_INFO("com9");
        CHECK_FALSE(LLFile::isSafeFileName("COM9"));
        UNSCOPED_INFO("lpt0");
        CHECK_FALSE(LLFile::isSafeFileName("LPT0"));
        UNSCOPED_INFO("lpt9");
        CHECK_FALSE(LLFile::isSafeFileName("LPT9"));
        UNSCOPED_INFO("com superscript one");
        CHECK_FALSE(LLFile::isSafeFileName("COM\xC2\xB9"));
        UNSCOPED_INFO("lpt superscript three");
        CHECK_FALSE(LLFile::isSafeFileName("LPT\xC2\xB3"));

        UNSCOPED_INFO("lower case");
        CHECK_FALSE(LLFile::isSafeFileName("con"));
        UNSCOPED_INFO("mixed case");
        CHECK_FALSE(LLFile::isSafeFileName("cOm1"));
        UNSCOPED_INFO("lower case conout");
        CHECK_FALSE(LLFile::isSafeFileName("conout$"));

        UNSCOPED_INFO("extension");
        CHECK_FALSE(LLFile::isSafeFileName("NUL.xml"));
        UNSCOPED_INFO("two extensions");
        CHECK_FALSE(LLFile::isSafeFileName("nul.tar.gz"));
        UNSCOPED_INFO("superscript with extension");
        CHECK_FALSE(LLFile::isSafeFileName("com\xC2\xB2.txt"));
        UNSCOPED_INFO("spaces before extension");
        CHECK_FALSE(LLFile::isSafeFileName("CON  .xml"));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName accepts names that only look like devices", "[llfile_test]")
    {
        // Names that only look like devices are files
        UNSCOPED_INFO("longer word");
        CHECK(LLFile::isSafeFileName("CONSOLE.xml"));
        UNSCOPED_INFO("two digits");
        CHECK(LLFile::isSafeFileName("COM10"));
        UNSCOPED_INFO("no digit");
        CHECK(LLFile::isSafeFileName("LPT"));
        UNSCOPED_INFO("device prefix");
        CHECK(LLFile::isSafeFileName("nul_keywords.xml"));
        UNSCOPED_INFO("device suffix");
        CHECK(LLFile::isSafeFileName("icon.xml"));
        UNSCOPED_INFO("device extension");
        CHECK(LLFile::isSafeFileName("keywords.con"));
        UNSCOPED_INFO("leading space");
        CHECK(LLFile::isSafeFileName(" CON"));
        UNSCOPED_INFO("only spaces before extension");
        CHECK(LLFile::isSafeFileName(" .xml"));
        UNSCOPED_INFO("hidden device");
        CHECK(LLFile::isSafeFileName(".con"));
    }
} // anonymous namespace
