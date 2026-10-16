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
        REQUIRE_FALSE(tempdir.empty());
        CHECK(LLFile::exists(tempdir));
        CHECK(LLFile::isdir(tempdir));
        CHECK_FALSE(LLFile::isfile(tempdir));

        // Make sure there is nothing left from a previous test run
        clear_entire_dir(testdir);
        CHECK_FALSE(LLFile::exists(testdir));

        int rc = LLFile::mkdir(testdir);
        UNSCOPED_INFO("mkdir");
        CHECK(rc == 0);
        CHECK(LLFile::isdir(testdir));
        rc = LLFile::mkdir(testdir);
        UNSCOPED_INFO("mkdir of a directory that exists");
        CHECK(rc == 0);

        std::filesystem::path testfile1 = testdir;
        testfile1.append("llfile_test.dat");
        CHECK_FALSE(LLFile::exists(testfile1));

        const char* testdata = "testdata";
        S64 bytes = LLFile::write(testfile1, testdata, 0, strlen(testdata));
        CHECK(bytes == (S64)strlen(testdata));
        CHECK(LLFile::isfile(testfile1));

        rc = LLFile::remove(testfile1);
        UNSCOPED_INFO("remove of the file");
        CHECK(rc == 0);
        CHECK_FALSE(LLFile::exists(testfile1));
        CHECK_FALSE(LLFile::isfile(testfile1));
        CHECK_FALSE(LLFile::isdir(testfile1));
        CHECK_FALSE(LLFile::islink(testfile1));

        rc = LLFile::remove(testdir);
        UNSCOPED_INFO("remove of the directory");
        CHECK(rc == 0);
        CHECK_FALSE(LLFile::exists(testdir));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: the static write size getContents and read agree on a file", "[llfile_test]")
    {
        // High level static file IO functions to read and write data files
        LLFile::mkdir(testdir);
        CHECK(LLFile::isdir(testdir));

        std::filesystem::path testfile1 = testdir;
        testfile1.append("llfile_test.dat");

        std::string testdata1("testdata");
        std::string testdata2("datateststuff");
        S64 bytes = LLFile::write(testfile1, testdata1.c_str(), 0, testdata1.length());
        UNSCOPED_INFO("write");
        CHECK(bytes == testdata1.length());
        CHECK(LLFile::exists(testfile1));
        CHECK(LLFile::isfile(testfile1));
        CHECK_FALSE(LLFile::isdir(testfile1));

        bytes = LLFile::size(testfile1);
        UNSCOPED_INFO("size");
        CHECK(bytes == testdata1.length());

        std::string data = LLFile::getContents(testfile1);
        CHECK(data.length() == testdata1.length());
        CHECK_THAT(ll_test::Memory(testdata1.c_str(), (U32)testdata1.length()), ll_test::MemoryMatches(data.c_str(), (U32)data.length()));

        char buffer[1024];
        bytes = LLFile::read(testfile1, buffer, 0, testdata1.length());
        UNSCOPED_INFO("read");
        REQUIRE(bytes == testdata1.length());
        CHECK_THAT(ll_test::Memory(testdata1.c_str(), (U32)bytes), ll_test::MemoryMatches(buffer, (U32)bytes));

        // What if we try to read more data than there is in the file?
        bytes = LLFile::read(testfile1, buffer, 0, bytes + 10);
        UNSCOPED_INFO("read past the end of the file");
        REQUIRE(bytes == testdata1.length());
        CHECK_THAT(ll_test::Memory(testdata1.c_str(), (U32)bytes), ll_test::MemoryMatches(buffer, (U32)bytes));

        // Let's append more data
        bytes = LLFile::write(testfile1, testdata2.c_str(), -1, testdata2.length());
        UNSCOPED_INFO("append");
        CHECK(bytes == testdata2.length());

        bytes = LLFile::size(testfile1);
        UNSCOPED_INFO("size after the append");
        REQUIRE(bytes == testdata1.length() + testdata2.length());
        bytes = LLFile::read(testfile1, buffer, 0, bytes);
        UNSCOPED_INFO("read after the append");
        CHECK(bytes == testdata1.length() + testdata2.length());
        CHECK_THAT(ll_test::Memory(testdata1.c_str(), (U32)testdata1.length()), ll_test::MemoryMatches(buffer, (U32)testdata1.length()));
        CHECK_THAT(ll_test::Memory(testdata2.c_str(), (U32)testdata2.length()), ll_test::MemoryMatches(buffer + testdata1.length(), (U32)testdata2.length()));
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
        REQUIRE((bool)fileout);
        UNSCOPED_INFO("constructor");
        CHECK_FALSE(ec);
        if (fileout)
        {
            S64 length = fileout.size(ec);
            CHECK(length == 0);
            UNSCOPED_INFO("size");
            CHECK_FALSE(ec);
            S64 bytes = fileout.write(data, sizeof(data), ec);
            UNSCOPED_INFO("write");
            CHECK(bytes == sizeof(data));
            UNSCOPED_INFO("write");
            CHECK_FALSE(ec);
            bytes = fileout.write(data, sizeof(data), ec);
            UNSCOPED_INFO("second write");
            CHECK(bytes == sizeof(data));
            UNSCOPED_INFO("second write");
            CHECK_FALSE(ec);
            bytes = fileout.size(ec);
            UNSCOPED_INFO("size after the writes");
            CHECK(bytes == 2 * sizeof(data));
            UNSCOPED_INFO("size after the writes");
            CHECK_FALSE(ec);
            fileout.close();
        }

        LLFile filein(testfile, LLFile::in, ec);
        REQUIRE((bool)filein);
        UNSCOPED_INFO("constructor");
        CHECK_FALSE(ec);
        if (filein)
        {
            S64 length = filein.size(ec);
            REQUIRE(length == 2 * sizeof(data));
            UNSCOPED_INFO("size");
            CHECK_FALSE(ec);
            char* buffer = (char*)malloc(length);
            S64 bytes  = filein.read(buffer, length, ec);
            UNSCOPED_INFO("read");
            CHECK(bytes == length);
            UNSCOPED_INFO("read");
            CHECK_FALSE(ec);
            CHECK_THAT(ll_test::Memory(data, (U32)sizeof(data)), ll_test::MemoryMatches(buffer, (U32)sizeof(data)));
            CHECK_THAT(ll_test::Memory(data, (U32)sizeof(data)), ll_test::MemoryMatches(buffer + sizeof(data), (U32)sizeof(data)));
            S64 offset = filein.tell(ec);
            CHECK(offset == length);
            UNSCOPED_INFO("tell");
            CHECK_FALSE(ec);
            offset = sizeof(data) / 2;
            int rc = filein.seek(offset, ec);
            CHECK(rc == 0);
            UNSCOPED_INFO("seek");
            CHECK_FALSE(ec);
            bytes = filein.read(buffer, 2 * sizeof(data), ec);
            UNSCOPED_INFO("read from the middle");
            CHECK(bytes == sizeof(data) + offset);
            UNSCOPED_INFO("read from the middle");
            CHECK_FALSE(ec);
            CHECK_THAT(ll_test::Memory((char*)data + offset, (U32)offset), ll_test::MemoryMatches(buffer, (U32)offset));
            CHECK_THAT(ll_test::Memory((char*)data, (U32)sizeof(data)), ll_test::MemoryMatches(buffer + offset, (U32)sizeof(data)));
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
        UNSCOPED_INFO("noreplace on a file that exists");
        CHECK_FALSE(file);
        CHECK((bool)ec);

        LLFile::remove(testfile);
        file = LLFile(testfile, LLFile::out | LLFile::app | LLFile::trunc, ec);
        UNSCOPED_INFO("app with trunc");
        CHECK_FALSE(file);
        CHECK((bool)ec);

        file = LLFile(testfile, LLFile::out | LLFile::app | LLFile::noreplace, ec);
        UNSCOPED_INFO("app with noreplace");
        CHECK_FALSE(file);
        CHECK((bool)ec);

        testfile = testdir;
        testfile.append("llfile_test");
        testfile.append("llfile_test.bin");

        file = LLFile(testfile, LLFile::in, ec);
        UNSCOPED_INFO("a directory that does not exist");
        CHECK_FALSE(file);
        CHECK((bool)ec);
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
        CHECK_FALSE(LLFile::exists(testdir_unicode));

        // Create the Unicode directory
        int rc = LLFile::mkdir(testdir_unicode);
        UNSCOPED_INFO("mkdir");
        CHECK(rc == 0);
        CHECK(LLFile::isdir(testdir_unicode));


        CHECK_FALSE(LLFile::exists(testfile_unicode));

        // Write to the Unicode file
        const char* testdata = "unicode_testdata";
        S64 bytes = LLFile::write(testfile_unicode, testdata, 0, strlen(testdata));
        CHECK(LLFile::exists(testfile_unicode));
        CHECK(LLFile::isfile(testfile_unicode));
        UNSCOPED_INFO("write");
        CHECK(bytes == (S64)strlen(testdata));

        // Read back the data
        char buffer[64] = {};
        bytes = LLFile::read(testfile_unicode, buffer, 0, sizeof(buffer));
        UNSCOPED_INFO("read");
        REQUIRE(bytes == (S64)strlen(testdata));
        CHECK_THAT(ll_test::Memory(testdata, (U32)bytes), ll_test::MemoryMatches(buffer, (U32)bytes));

        // Remove the file and directory
        rc = LLFile::remove(testfile_unicode);
        UNSCOPED_INFO("remove of the file");
        CHECK(rc == 0);
        CHECK_FALSE(LLFile::exists(testfile_unicode));

        rc = LLFile::remove(testdir_unicode);
        UNSCOPED_INFO("remove of the directory");
        CHECK(rc == 0);
        CHECK_FALSE(LLFile::exists(testdir_unicode));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName accepts ordinary file names", "[llfile_test]")
    {
        // Ordinary file names pass isSafeFileName()
        CHECK(LLFile::isSafeFileName("lsl_keywords.xml"));
        CHECK(LLFile::isSafeFileName("secondlife.d.luau"));
        CHECK(LLFile::isSafeFileName("keywords"));
        CHECK(LLFile::isSafeFileName("a..b"));
        CHECK(LLFile::isSafeFileName("my file.txt"));
        UNSCOPED_INFO("hidden");
        CHECK(LLFile::isSafeFileName(".luaurc"));
        CHECK(LLFile::isSafeFileName("..name"));
        UNSCOPED_INFO("unicode");
        CHECK(LLFile::isSafeFileName("\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB.bin"));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName refuses empty and dot names and ones Windows trims", "[llfile_test]")
    {
        // Nothing, the directory itself or its parent, and names Windows would
        // shorten when creating them
        CHECK_FALSE(LLFile::isSafeFileName(""));
        CHECK_FALSE(LLFile::isSafeFileName("."));
        CHECK_FALSE(LLFile::isSafeFileName(".."));
        CHECK_FALSE(LLFile::isSafeFileName("..."));
        CHECK_FALSE(LLFile::isSafeFileName("name."));
        UNSCOPED_INFO("trailing space");
        CHECK_FALSE(LLFile::isSafeFileName("name.xml "));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName refuses path separators of either platform", "[llfile_test]")
    {
        // Separators of either platform, at either end or inside
        CHECK_FALSE(LLFile::isSafeFileName("../../evil.xml"));
        CHECK_FALSE(LLFile::isSafeFileName("..\\..\\evil.xml"));
        CHECK_FALSE(LLFile::isSafeFileName("/etc/evil"));
        CHECK_FALSE(LLFile::isSafeFileName("\\Windows\\evil.dll"));
        UNSCOPED_INFO("unc");
        CHECK_FALSE(LLFile::isSafeFileName("\\\\host\\share\\evil"));
        CHECK_FALSE(LLFile::isSafeFileName("sub/evil.xml"));
        CHECK_FALSE(LLFile::isSafeFileName("sub\\evil.xml"));
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
        CHECK_FALSE(LLFile::isSafeFileName("a*b"));
        CHECK_FALSE(LLFile::isSafeFileName("a?b"));
        CHECK_FALSE(LLFile::isSafeFileName("a\"b"));
        CHECK_FALSE(LLFile::isSafeFileName("a<b"));
        CHECK_FALSE(LLFile::isSafeFileName("a>b"));
        CHECK_FALSE(LLFile::isSafeFileName("a|b"));
        CHECK_FALSE(LLFile::isSafeFileName("a\tb"));
        CHECK_FALSE(LLFile::isSafeFileName("a\nb"));
        UNSCOPED_INFO("unit separator");
        CHECK_FALSE(LLFile::isSafeFileName("a\x1F" "b"));
        CHECK_FALSE(LLFile::isSafeFileName(std::string(1, '\0')));
        CHECK_FALSE(LLFile::isSafeFileName(std::string("name.xml\0evil", 13)));
        CHECK_FALSE(LLFile::isSafeFileName(std::string("name.xml\0", 9)));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName allows up to 255 bytes", "[llfile_test]")
    {
        // The length limit is inclusive
        CHECK(LLFile::isSafeFileName(std::string(255, 'a')));
        CHECK_FALSE(LLFile::isSafeFileName(std::string(256, 'a')));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName refuses Windows device names", "[llfile_test]")
    {
        // Windows device names, in any case, with or without an extension
        CHECK_FALSE(LLFile::isSafeFileName("CON"));
        CHECK_FALSE(LLFile::isSafeFileName("PRN"));
        CHECK_FALSE(LLFile::isSafeFileName("AUX"));
        CHECK_FALSE(LLFile::isSafeFileName("NUL"));
        CHECK_FALSE(LLFile::isSafeFileName("CONIN$"));
        CHECK_FALSE(LLFile::isSafeFileName("CONOUT$"));
        CHECK_FALSE(LLFile::isSafeFileName("COM0"));
        CHECK_FALSE(LLFile::isSafeFileName("COM9"));
        CHECK_FALSE(LLFile::isSafeFileName("LPT0"));
        CHECK_FALSE(LLFile::isSafeFileName("LPT9"));
        UNSCOPED_INFO("com superscript one");
        CHECK_FALSE(LLFile::isSafeFileName("COM\xC2\xB9"));
        UNSCOPED_INFO("lpt superscript three");
        CHECK_FALSE(LLFile::isSafeFileName("LPT\xC2\xB3"));

        CHECK_FALSE(LLFile::isSafeFileName("con"));
        CHECK_FALSE(LLFile::isSafeFileName("cOm1"));
        CHECK_FALSE(LLFile::isSafeFileName("conout$"));

        CHECK_FALSE(LLFile::isSafeFileName("NUL.xml"));
        CHECK_FALSE(LLFile::isSafeFileName("nul.tar.gz"));
        UNSCOPED_INFO("superscript with extension");
        CHECK_FALSE(LLFile::isSafeFileName("com\xC2\xB2.txt"));
        UNSCOPED_INFO("spaces before extension");
        CHECK_FALSE(LLFile::isSafeFileName("CON  .xml"));
    }

    TEST_CASE_METHOD(llfile_test, "llfile_test: isSafeFileName accepts names that only look like devices", "[llfile_test]")
    {
        // Names that only look like devices are files
        CHECK(LLFile::isSafeFileName("CONSOLE.xml"));
        CHECK(LLFile::isSafeFileName("COM10"));
        CHECK(LLFile::isSafeFileName("LPT"));
        CHECK(LLFile::isSafeFileName("nul_keywords.xml"));
        CHECK(LLFile::isSafeFileName("icon.xml"));
        CHECK(LLFile::isSafeFileName("keywords.con"));
        UNSCOPED_INFO("leading space");
        CHECK(LLFile::isSafeFileName(" CON"));
        UNSCOPED_INFO("only spaces before extension");
        CHECK(LLFile::isSafeFileName(" .xml"));
        CHECK(LLFile::isSafeFileName(".con"));
    }
} // anonymous namespace
