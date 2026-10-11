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

#include <tut/tut.hpp>
#include "lltut.h"
#include "linden_common.h"
#include "llfile.h"
#include "lluuid.h"

namespace tut
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
    typedef test_group<llfile_test> llfile_test_t;
    typedef llfile_test_t::object   llfile_test_object_t;
    tut::llfile_test_t              tut_llfile_test("llfile_test");

    template<> template<>
    void llfile_test_object_t::test<1>()
    {
        // Test creating directories and files and deleting them and checking if the
        // relevant status functions work as expected
        ensure("LLFile::tmpdir() empty", !tempdir.empty());
        ensure("LLFile::tmpdir() doesn't exist", LLFile::exists(tempdir));
        ensure("LLFile::tmpdir() is not a directory", LLFile::isdir(tempdir));
        ensure("LLFile::tmpdir() should not be a file", !LLFile::isfile(tempdir));

        // Make sure there is nothing left from a previous test run
        clear_entire_dir(testdir);
        ensure("llfile_test should not exist anymore", !LLFile::exists(testdir));

        int rc = LLFile::mkdir(testdir);
        ensure("LLFile::mkdir() failed", rc == 0);
        ensure("llfile_test should be a directory", LLFile::isdir(testdir));
        rc = LLFile::mkdir(testdir);
        ensure("LLFile::mkdir() should not fail when the directory already exists", rc == 0);

        std::filesystem::path testfile1 = testdir;
        testfile1.append("llfile_test.dat");
        ensure("llfile_test1.dat should not yet exist", !LLFile::exists(testfile1));

        const char* testdata = "testdata";
        S64 bytes = LLFile::write(testfile1, testdata, 0, sizeof(testdata));
        ensure("LLFile::write() did not write correctly", bytes == sizeof(testdata));

        rc = LLFile::remove(testfile1);
        ensure("LLFile::remove() for file test_file.dat", rc == 0);
        ensure("llfile_test.dat should not exist anymore", !LLFile::exists(testfile1));
        ensure("llfile_test.dat should not be a file", !LLFile::isfile(testfile1));
        ensure("llfile_test.dat should not be a directory", !LLFile::isdir(testfile1));
        ensure("llfile_test.dat should not be a symlink", !LLFile::islink(testfile1));

        rc = LLFile::remove(testdir);
        ensure("LLFile::remove() for directory llfile_test failed", rc == 0);
        ensure("llfile_test should not exist anymore", !LLFile::exists(testdir));
    }

    template<> template<>
    void llfile_test_object_t::test<2>()
    {
        // High level static file IO functions to read and write data files
        LLFile::mkdir(testdir);
        ensure("llfile_test should exist", LLFile::isdir(testdir));

        std::filesystem::path testfile1 = testdir;
        testfile1.append("llfile_test.dat");

        std::string testdata1("testdata");
        std::string testdata2("datateststuff");
        std::time_t current = time(nullptr);
        S64 bytes = LLFile::write(testfile1, testdata1.c_str(), 0, testdata1.length());
        ensure("LLFile::write() did not write correctly", bytes == testdata1.length());
        ensure("llfile_test.dat should exist", LLFile::exists(testfile1));
        ensure("llfile_test.dat should be a file", LLFile::isfile(testfile1));
        ensure("llfile_test.dat should not be a directory", !LLFile::isdir(testfile1));

        bytes = LLFile::size(testfile1);
        ensure("LLFile::size() did not return the correct size", bytes == testdata1.length());

        std::string data = LLFile::getContents(testfile1);
        ensure("LLFile::getContents() did not return the correct size data", data.length() == testdata1.length());
        ensure_memory_matches("LLFile::getContents() did not read correct data", testdata1.c_str(), (U32)testdata1.length(), data.c_str(), (U32)data.length());

        char buffer[1024];
        bytes = LLFile::read(testfile1, buffer, 0, testdata1.length());
        ensure("LLFile:read() did not return the correct size", bytes == testdata1.length());
        ensure_memory_matches("LLFile::read() did not read correct data", testdata1.c_str(), (U32)bytes, buffer, (U32)bytes);

        // What if we try to read more data than there is in the file?
        bytes = LLFile::read(testfile1, buffer, 0, bytes + 10);
        ensure("LLFile:read() did not correctly stop on eof", bytes == testdata1.length());
        ensure_memory_matches("LLFile::read() did not read correct data", testdata1.c_str(), (U32)bytes, buffer, (U32)bytes);

        // Let's append more data
        bytes = LLFile::write(testfile1, testdata2.c_str(), -1, testdata2.length());
        ensure("LLFile::write() did not write correctly", bytes == testdata2.length());

        bytes = LLFile::size(testfile1);
        ensure("LLFile::size() did not return the correct size", bytes == testdata1.length() + testdata2.length());
        bytes = LLFile::read(testfile1, buffer, 0, bytes);
        ensure("LLFile:read() did not read correct number of bytes", bytes == testdata1.length() + testdata2.length());
        ensure_memory_matches("LLFile:read() did not read correct testdata1", testdata1.c_str(), (U32)testdata1.length(), buffer, (U32)testdata1.length());
        ensure_memory_matches("LLFile:read() did not read correct testdata2", testdata2.c_str(), (U32)testdata2.length(), buffer + testdata1.length(), (U32)testdata2.length());
    }

    template<> template<>
    void llfile_test_object_t::test<3>()
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
        ensure("LLFile constructor did not open correctly", (bool)fileout);
        ensure("error_code from LLFile constructor should not indicate an error", !ec);
        if (fileout)
        {
            S64 length = fileout.size(ec);
            ensure("freshly created file should be empty", length == 0);
            ensure("error_code from LLFile::size() should not indicate an error", !ec);
            S64 bytes = fileout.write(data, sizeof(data), ec);
            ensure("LLFile::write() did not write correctly", bytes == sizeof(data));
            ensure("error_code from LLFile::write() should not indicate an error", !ec);
            bytes = fileout.write(data, sizeof(data), ec);
            ensure("LLFile::write() did not write correctly", bytes == sizeof(data));
            ensure("error_code from LLFile::write() should not indicate an error", !ec);
            bytes = fileout.size(ec);
            ensure("LLFile::size() returned wrong size", bytes == 2 * sizeof(data));
            ensure("error_code from LLFile::size() should not indicate an error", !ec);
            fileout.close();
        }

        LLFile filein(testfile, LLFile::in, ec);
        ensure("LLFile constructor did not open correctly", (bool)filein);
        ensure("error_code from LLFile constructor should not indicate an error", !ec);
        if (filein)
        {
            S64 length = filein.size(ec);
            ensure("LLFile::size() returned wrong size", length == 2 * sizeof(data));
            ensure("error_code from LLFile::size() should not indicate an error", !ec);
            char* buffer = (char*)malloc(length);
            S64 bytes  = filein.read(buffer, length, ec);
            ensure("LLFile::read() did not read correctly", bytes == length);
            ensure("error_code from LLFile::read() should not indicate an error", !ec);
            ensure_memory_matches("LLFile:read() did not read correct data1", data, (U32)sizeof(data), buffer, (U32)sizeof(data));
            ensure_memory_matches("LLFile:read() did not read correct data2", data, (U32)sizeof(data), buffer + sizeof(data), (U32)sizeof(data));
            S64 offset = filein.tell(ec);
            ensure("LLFile::tell() returned a bad offset", offset == length);
            ensure("error_code from LLFile::read() should not indicate an error", !ec);
            offset = sizeof(data) / 2;
            int rc = filein.seek(offset, ec);
            ensure("LLFile::seek() indicated an error", rc == 0);
            ensure("error_code from LLFile::seek() should not indicate an error", !ec);
            bytes = filein.read(buffer, 2 * sizeof(data), ec);
            ensure("LLFile::read() did not read correctly", bytes == sizeof(data) + offset);
            ensure("error_code from LLFile::read() should not indicate an error", !ec);
            ensure_memory_matches("LLFile:read() did not read correct data3", (char*)data + offset, (U32)offset, buffer, (U32)offset);
            ensure_memory_matches("LLFile:read() did not read correct data4", (char*)data, (U32)sizeof(data), buffer + offset, (U32)sizeof(data));
            filein.close();

            free(buffer);
        }
    }

    template<> template<>
    void llfile_test_object_t::test<4>()
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
        ensure("LLFile constructor should not have opened the already existing file", !file);
        ensure("error_code from LLFile constructor should indicate an error", (bool)ec);

        LLFile::remove(testfile);
        file = LLFile(testfile, LLFile::out | LLFile::app | LLFile::trunc, ec);
        ensure("LLFile constructor should not have opened the file with conflicting flags", !file);
        ensure("error_code from LLFile constructor should indicate an error", (bool)ec);

        file = LLFile(testfile, LLFile::out | LLFile::app | LLFile::noreplace, ec);
        ensure("LLFile constructor should not have opened the file with conflicting flags", !file);
        ensure("error_code from LLFile constructor should indicate an error", (bool)ec);

        testfile = testdir;
        testfile.append("llfile_test");
        testfile.append("llfile_test.bin");

        file = LLFile(testfile, LLFile::in, ec);
        ensure("LLFile constructor should not have been able to open the file in the non-existing directory", !file);
        ensure("error_code from LLFile constructor should indicate an error", (bool)ec);
    }

    template<> template<>
    void llfile_test_object_t::test<5>()
    {
        // Test file and directory operations with Unicode paths and filenames
        std::filesystem::path testdir_unicode = get_testdir_unicode(tempdir, unique);

        // Unicode filename: "ファイル_テスト.bin" (means "file_test.bin" in Japanese)
        std::string unicode_filename = "\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB_\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88.bin";
        std::filesystem::path testfile_unicode = testdir_unicode;
        testfile_unicode.append(unicode_filename);

        // Clean up any previous test artifacts
        clear_entire_dir(testdir_unicode);
        ensure("Unicode test directory should not exist", !LLFile::exists(testdir_unicode));

        // Create the Unicode directory
        int rc = LLFile::mkdir(testdir_unicode);
        ensure("LLFile::mkdir() failed for Unicode directory", rc == 0);
        ensure("Unicode test directory should exist", LLFile::isdir(testdir_unicode));


        ensure("Unicode test file should not exist", !LLFile::exists(testfile_unicode));

        // Write to the Unicode file
        const char* testdata = "unicode_testdata";
        S64 bytes = LLFile::write(testfile_unicode, testdata, 0, strlen(testdata));
        ensure("Unicode test file should exist", LLFile::exists(testfile_unicode));
        ensure("Unicode test file should be a file", LLFile::isfile(testfile_unicode));
        ensure("LLFile::write() did not write correctly to Unicode file", bytes == (S64)strlen(testdata));

        // Read back the data
        char buffer[64] = {};
        bytes = LLFile::read(testfile_unicode, buffer, 0, sizeof(buffer));
        ensure("LLFile::read() did not read correctly from Unicode file", bytes == (S64)strlen(testdata));
        ensure_memory_matches("LLFile::read() did not read correct Unicode data", testdata, (U32)bytes, buffer, (U32)bytes);

        // Remove the file and directory
        rc = LLFile::remove(testfile_unicode);
        ensure("LLFile::remove() failed for Unicode file", rc == 0);
        ensure("Unicode test file should not exist after removal", !LLFile::exists(testfile_unicode));

        rc = LLFile::remove(testdir_unicode);
        ensure("LLFile::remove() failed for Unicode directory", rc == 0);
        ensure("Unicode test directory should not exist after removal", !LLFile::exists(testdir_unicode));
    }

    template<> template<>
    void llfile_test_object_t::test<6>()
    {
        // Ordinary file names pass isSafeFileName()
        ensure("xml", LLFile::isSafeFileName("lsl_keywords.xml"));
        ensure("two extensions", LLFile::isSafeFileName("secondlife.d.luau"));
        ensure("no extension", LLFile::isSafeFileName("keywords"));
        ensure("inner dots", LLFile::isSafeFileName("a..b"));
        ensure("inner space", LLFile::isSafeFileName("my file.txt"));
        ensure("hidden", LLFile::isSafeFileName(".luaurc"));
        ensure("leading dots", LLFile::isSafeFileName("..name"));
        ensure("unicode", LLFile::isSafeFileName("\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB.bin"));
    }

    template<> template<>
    void llfile_test_object_t::test<7>()
    {
        // Nothing, the directory itself or its parent, and names Windows would
        // shorten when creating them
        ensure("empty", !LLFile::isSafeFileName(""));
        ensure("dot", !LLFile::isSafeFileName("."));
        ensure("dot dot", !LLFile::isSafeFileName(".."));
        ensure("three dots", !LLFile::isSafeFileName("..."));
        ensure("trailing dot", !LLFile::isSafeFileName("name."));
        ensure("trailing space", !LLFile::isSafeFileName("name.xml "));
    }

    template<> template<>
    void llfile_test_object_t::test<8>()
    {
        // Separators of either platform, at either end or inside
        ensure("parent posix", !LLFile::isSafeFileName("../../evil.xml"));
        ensure("parent windows", !LLFile::isSafeFileName("..\\..\\evil.xml"));
        ensure("absolute posix", !LLFile::isSafeFileName("/etc/evil"));
        ensure("absolute windows", !LLFile::isSafeFileName("\\Windows\\evil.dll"));
        ensure("unc", !LLFile::isSafeFileName("\\\\host\\share\\evil"));
        ensure("subdirectory posix", !LLFile::isSafeFileName("sub/evil.xml"));
        ensure("subdirectory windows", !LLFile::isSafeFileName("sub\\evil.xml"));
        ensure("trailing separator", !LLFile::isSafeFileName("evil/"));
    }

    template<> template<>
    void llfile_test_object_t::test<9>()
    {
        // Drive and stream colons, the rest of what Windows refuses in a name,
        // and control characters, a NUL above all: a C string would cut a
        // harmless prefix off a harmful name there
        ensure("drive absolute", !LLFile::isSafeFileName("C:\\evil.dll"));
        ensure("drive relative", !LLFile::isSafeFileName("C:evil.dll"));
        ensure("stream", !LLFile::isSafeFileName("name.xml:evil"));
        ensure("star", !LLFile::isSafeFileName("a*b"));
        ensure("question", !LLFile::isSafeFileName("a?b"));
        ensure("quote", !LLFile::isSafeFileName("a\"b"));
        ensure("less", !LLFile::isSafeFileName("a<b"));
        ensure("greater", !LLFile::isSafeFileName("a>b"));
        ensure("pipe", !LLFile::isSafeFileName("a|b"));
        ensure("tab", !LLFile::isSafeFileName("a\tb"));
        ensure("newline", !LLFile::isSafeFileName("a\nb"));
        ensure("unit separator", !LLFile::isSafeFileName("a\x1F" "b"));
        ensure("only nul", !LLFile::isSafeFileName(std::string(1, '\0')));
        ensure("inner nul", !LLFile::isSafeFileName(std::string("name.xml\0evil", 13)));
        ensure("trailing nul", !LLFile::isSafeFileName(std::string("name.xml\0", 9)));
    }

    template<> template<>
    void llfile_test_object_t::test<10>()
    {
        // The length limit is inclusive
        ensure("at limit", LLFile::isSafeFileName(std::string(255, 'a')));
        ensure("over limit", !LLFile::isSafeFileName(std::string(256, 'a')));
    }

    template<> template<>
    void llfile_test_object_t::test<11>()
    {
        // Windows device names, in any case, with or without an extension
        ensure("con", !LLFile::isSafeFileName("CON"));
        ensure("prn", !LLFile::isSafeFileName("PRN"));
        ensure("aux", !LLFile::isSafeFileName("AUX"));
        ensure("nul", !LLFile::isSafeFileName("NUL"));
        ensure("conin", !LLFile::isSafeFileName("CONIN$"));
        ensure("conout", !LLFile::isSafeFileName("CONOUT$"));
        ensure("com0", !LLFile::isSafeFileName("COM0"));
        ensure("com9", !LLFile::isSafeFileName("COM9"));
        ensure("lpt0", !LLFile::isSafeFileName("LPT0"));
        ensure("lpt9", !LLFile::isSafeFileName("LPT9"));
        ensure("com superscript one", !LLFile::isSafeFileName("COM\xC2\xB9"));
        ensure("lpt superscript three", !LLFile::isSafeFileName("LPT\xC2\xB3"));

        ensure("lower case", !LLFile::isSafeFileName("con"));
        ensure("mixed case", !LLFile::isSafeFileName("cOm1"));
        ensure("lower case conout", !LLFile::isSafeFileName("conout$"));

        ensure("extension", !LLFile::isSafeFileName("NUL.xml"));
        ensure("two extensions", !LLFile::isSafeFileName("nul.tar.gz"));
        ensure("superscript with extension", !LLFile::isSafeFileName("com\xC2\xB2.txt"));
        ensure("spaces before extension", !LLFile::isSafeFileName("CON  .xml"));
    }

    template<> template<>
    void llfile_test_object_t::test<12>()
    {
        // Names that only look like devices are files
        ensure("longer word", LLFile::isSafeFileName("CONSOLE.xml"));
        ensure("two digits", LLFile::isSafeFileName("COM10"));
        ensure("no digit", LLFile::isSafeFileName("LPT"));
        ensure("device prefix", LLFile::isSafeFileName("nul_keywords.xml"));
        ensure("device suffix", LLFile::isSafeFileName("icon.xml"));
        ensure("device extension", LLFile::isSafeFileName("keywords.con"));
        ensure("leading space", LLFile::isSafeFileName(" CON"));
        ensure("only spaces before extension", LLFile::isSafeFileName(" .xml"));
        ensure("hidden device", LLFile::isSafeFileName(".con"));
    }
} // namespace tut
