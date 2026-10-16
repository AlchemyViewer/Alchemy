/**
 * @file lldir_test.cpp
 * @date 2008-05
 * @brief LLDir test cases.
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
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

#include <filesystem>
#include <set>
#include "llstring.h"
#include "tests/StringVec.h"
#include "../lldir.h"
#include "../lldiriterator.h"

#include <catch2/catch_test_macros.hpp>
#include "../test/namedtempfile.h"
#include "stringize.h"

#if LL_LINUX
#include "../lldir_linux.h"
#include <cstdlib>
#include <fstream>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace
{
// For some tests, use a dummy LLDir that uses memory data instead of touching
// the filesystem
struct LLDir_Dummy: public LLDir
{
    /*----------------------------- LLDir API ------------------------------*/
    LLDir_Dummy()
    {
        // Initialize important LLDir data members based on the filesystem
        // data below.
        mDirDelimiter = "/";
        mExecutableDir = "install";
        mExecutableFilename = "test";
        mExecutablePathAndName = add(mExecutableDir, mExecutableFilename);
        mWorkingDir = mExecutableDir;
        mAppRODataDir = "install";
        mSkinBaseDir = add(mAppRODataDir, "skins");
        mOSUserDir = "user";
        mOSUserAppDir = mOSUserDir;
        mLindenUserDir = "";

        // Make the dummy filesystem look more or less like what we expect in
        // the real one.
        static const char* preload[] =
        {
            // We group these fixture-data pathnames by basename, rather than
            // sorting by full path as you might expect, because the outcome
            // of each test strongly depends on which skins/languages provide
            // a given basename.
            "install/skins/default/colors.xml",
            "install/skins/steam/colors.xml",
            "user/skins/default/colors.xml",
            "user/skins/steam/colors.xml",

            "install/skins/default/xui/en/strings.xml",
            "install/skins/default/xui/fr/strings.xml",
            "install/skins/steam/xui/en/strings.xml",
            "install/skins/steam/xui/fr/strings.xml",
            "user/skins/default/xui/en/strings.xml",
            "user/skins/default/xui/fr/strings.xml",
            "user/skins/steam/xui/en/strings.xml",
            "user/skins/steam/xui/fr/strings.xml",

            "install/skins/default/xui/en/floater.xml",
            "install/skins/default/xui/fr/floater.xml",
            "user/skins/default/xui/fr/floater.xml",

            "install/skins/default/xui/en/newfile.xml",
            "install/skins/default/xui/fr/newfile.xml",
            "user/skins/default/xui/en/newfile.xml",

            "install/skins/default/html/en-us/welcome.html",
            "install/skins/default/html/fr/welcome.html",

            "install/skins/default/textures/only_default.jpeg",
            "install/skins/steam/textures/only_steam.jpeg",
            "user/skins/default/textures/only_user_default.jpeg",
            "user/skins/steam/textures/only_user_steam.jpeg",

            "install/skins/default/future/somefile.txt"
        };
        for (const char* path : preload)
        {
            buildFilesystem(path);
        }
    }

    virtual ~LLDir_Dummy() {}

    virtual void initAppDirs(const std::string& app_name, const std::string& app_read_only_data_dir)
    {
        // Implement this when we write a test that needs it
    }

    virtual std::string getCurPath()
    {
        // Implement this when we write a test that needs it
        return "";
    }

    virtual U32 countFilesInDir(const std::string& dirname, const std::string& mask)
    {
        // Implement this when we write a test that needs it
        return 0;
    }

    virtual bool fileExists(const std::string& pathname) const
    {
        // Record fileExists() calls so we can check whether caching is
        // working right. Certain LLDir calls should be able to make decisions
        // without calling fileExists() again, having already checked existence.
        mChecked.insert(pathname);
        // For our simple flat set of strings, see whether the identical
        // pathname exists in our set.
        return (mFilesystem.find(pathname) != mFilesystem.end());
    }


    virtual std::string getLLPluginFilename(std::string base_name)
    {
        // Implement this when we write a test that needs it
        return "";
    }

    /*----------------------------- Dummy data -----------------------------*/
    void clearFilesystem() { mFilesystem.clear(); }
    void buildFilesystem(const std::string& path)
    {
        // Split the pathname on slashes, ignoring leading, trailing, doubles
        StringVec components;
        LLStringUtil::getTokens(path, components, "/");
        // Ensure we have an entry representing every level of this path
        std::string partial;
        for (std::string component : components)
        {
            append(partial, component);
            mFilesystem.insert(partial);
        }
    }

    void clear_checked() { mChecked.clear(); }
    void require_checked(const std::string& pathname) const
    {
        INFO(pathname << " was not checked but should have been");
        REQUIRE(mChecked.find(pathname) != mChecked.end());
    }
    void require_not_checked(const std::string& pathname) const
    {
        INFO(pathname << " was checked but should not have been");
        REQUIRE(mChecked.find(pathname) == mChecked.end());
    }

    std::set<std::string> mFilesystem;
    mutable std::set<std::string> mChecked;
};

// A dummy whose profile moved out of one legacy root into separate dirs.
struct LLDir_Relocated: public LLDir_Dummy
{
    LLDir_Relocated()
    {
        mLegacyUserAppDir = "old";
        mUserSettingsDir = "cfg";
        mLogsDir = "state";
        mDefaultCacheDir = "cachedir";
    }
};
}

#if LL_LINUX
namespace
{
    namespace fs = std::filesystem;

    // A scratch home with every XDG variable pointed into it, so nothing a
    // test does reaches the real profile. Puts the variables back after.
    struct ScratchHome
    {
        ScratchHome()
        {
            std::string pattern = (fs::temp_directory_path() / "lldir_test_XXXXXX").string();
            home = ::mkdtemp(pattern.data());
            for (const char* var : VARS)
            {
                const char* value = ::getenv(var);
                saved.emplace_back(value ? std::optional<std::string>(value) : std::nullopt);
                ::unsetenv(var);
            }
        }

        ~ScratchHome()
        {
            for (size_t i = 0; i < std::size(VARS); ++i)
            {
                if (saved[i])
                {
                    ::setenv(VARS[i], saved[i]->c_str(), 1);
                }
                else
                {
                    ::unsetenv(VARS[i]);
                }
            }
            std::error_code ec;
            fs::remove_all(home, ec);
        }

        std::string path(const std::string& rel) const { return home + "/" + rel; }

        void write(const std::string& rel, const std::string& text = "x") const
        {
            fs::create_directories(fs::path(path(rel)).parent_path());
            std::ofstream(path(rel)) << text;
        }

        bool exists(const std::string& rel) const
        {
            std::error_code ec;
            return fs::exists(fs::symlink_status(path(rel), ec));
        }

        // a profile as older viewers left it
        void writeLegacyProfile() const
        {
            write(".app/user_settings/settings.xml", "settings");
            write(".app/logs/Alchemy.log", "log");
            write(".app/cache/texturecache/t", "texture");
            write(".app/bob_resident/settings_per_account.xml", "account");
            write(".app/skins/default/colors.xml", "skin");
        }

        bool legacyProfileIntact() const
        {
            return exists(".app/user_settings/settings.xml") && exists(".app/logs/Alchemy.log") &&
                   exists(".app/cache/texturecache/t") && exists(".app/bob_resident/settings_per_account.xml") &&
                   exists(".app/skins/default/colors.xml");
        }

        static constexpr const char* VARS[] = { "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME" };
        std::string home;
        std::vector<std::optional<std::string>> saved;
    };
}
#endif // LL_LINUX

namespace
{
    TEST_CASE("LLDir: getDirDelimiter is not empty", "[LLDir]")
    {
        CHECK_FALSE(gDirUtilp->getDirDelimiter().empty());
    }

    TEST_CASE("LLDir: getBaseFileName with and without stripping the extension", "[LLDir]")
    {
        std::string delim = gDirUtilp->getDirDelimiter();
        std::string rawFile = "foo";
        std::string rawFileExt = "foo.bAr";
        std::string rawFileNullExt = "foo.";
        std::string rawExt = ".bAr";
        std::string rawDot = ".";
        std::string pathNoExt = "aa" + delim + "bb" + delim + "cc" + delim + "dd" + delim + "ee";
        std::string pathExt = pathNoExt + ".eXt";
        std::string dottedPathNoExt = "aa" + delim + "bb" + delim + "cc.dd" + delim + "ee";
        std::string dottedPathExt = dottedPathNoExt + ".eXt";

        // foo[.bAr]
        CHECK(gDirUtilp->getBaseFileName(rawFile, false) == "foo");
        CHECK(gDirUtilp->getBaseFileName(rawFile, true) == "foo");
        CHECK(gDirUtilp->getBaseFileName(rawFileExt, false) == "foo.bAr");
        CHECK(gDirUtilp->getBaseFileName(rawFileExt, true) == "foo");

        // foo.
        CHECK(gDirUtilp->getBaseFileName(rawFileNullExt, false) == "foo.");
        CHECK(gDirUtilp->getBaseFileName(rawFileNullExt, true) == "foo");

        // .bAr
        // interesting case - with no basename, this IS the basename, not the extension.
        CHECK(gDirUtilp->getBaseFileName(rawExt, false) == ".bAr");
        CHECK(gDirUtilp->getBaseFileName(rawExt, true) == ".bAr");

        // .
        CHECK(gDirUtilp->getBaseFileName(rawDot, false) == ".");
        CHECK(gDirUtilp->getBaseFileName(rawDot, true) == ".");

        // aa/bb/cc/dd/ee[.eXt]
        CHECK(gDirUtilp->getBaseFileName(pathNoExt, false) == "ee");
        CHECK(gDirUtilp->getBaseFileName(pathNoExt, true) == "ee");
        CHECK(gDirUtilp->getBaseFileName(pathExt, false) == "ee.eXt");
        CHECK(gDirUtilp->getBaseFileName(pathExt, true) == "ee");

        // aa/bb/cc.dd/ee[.eXt]
        CHECK(gDirUtilp->getBaseFileName(dottedPathNoExt, false) == "ee");
        CHECK(gDirUtilp->getBaseFileName(dottedPathNoExt, true) == "ee");
        CHECK(gDirUtilp->getBaseFileName(dottedPathExt, false) == "ee.eXt");
        CHECK(gDirUtilp->getBaseFileName(dottedPathExt, true) == "ee");
    }

    TEST_CASE("LLDir: getDirName drops the last path component", "[LLDir]")
    {
        std::string delim = gDirUtilp->getDirDelimiter();
        std::string rawFile = "foo";
        std::string rawFileExt = "foo.bAr";
        std::string pathNoExt = "aa" + delim + "bb" + delim + "cc" + delim + "dd" + delim + "ee";
        std::string pathExt = pathNoExt + ".eXt";
        std::string dottedPathNoExt = "aa" + delim + "bb" + delim + "cc.dd" + delim + "ee";
        std::string dottedPathExt = dottedPathNoExt + ".eXt";

        // foo[.bAr]
        CHECK(gDirUtilp->getDirName(rawFile) == "");
        CHECK(gDirUtilp->getDirName(rawFileExt) == "");

        // aa/bb/cc/dd/ee[.eXt]
        CHECK(gDirUtilp->getDirName(pathNoExt) == "aa" + delim + "bb" + delim + "cc" + delim + "dd");
        CHECK(gDirUtilp->getDirName(pathExt) == "aa" + delim + "bb" + delim + "cc" + delim + "dd");

        // aa/bb/cc.dd/ee[.eXt]
        CHECK(gDirUtilp->getDirName(dottedPathNoExt) == "aa" + delim + "bb" + delim + "cc.dd");
        CHECK(gDirUtilp->getDirName(dottedPathExt) == "aa" + delim + "bb" + delim + "cc.dd");
    }

    TEST_CASE("LLDir: getExtension is the lower-cased extension of the basename", "[LLDir]")
    {
        std::string delim = gDirUtilp->getDirDelimiter();
        std::string rawFile = "foo";
        std::string rawFileExt = "foo.bAr";
        std::string rawFileNullExt = "foo.";
        std::string rawExt = ".bAr";
        std::string rawDot = ".";
        std::string pathNoExt = "aa" + delim + "bb" + delim + "cc" + delim + "dd" + delim + "ee";
        std::string pathExt = pathNoExt + ".eXt";
        std::string dottedPathNoExt = "aa" + delim + "bb" + delim + "cc.dd" + delim + "ee";
        std::string dottedPathExt = dottedPathNoExt + ".eXt";

        // foo[.bAr]
        CHECK(gDirUtilp->getExtension(rawFile) == "");
        CHECK(gDirUtilp->getExtension(rawFileExt) == "bar");

        // foo.
        CHECK(gDirUtilp->getExtension(rawFileNullExt) == "");

        // .bAr
        // interesting case - with no basename, this IS the basename, not the extension.
        CHECK(gDirUtilp->getExtension(rawExt) == "");

        // .
        CHECK(gDirUtilp->getExtension(rawDot) == "");

        // aa/bb/cc/dd/ee[.eXt]
        CHECK(gDirUtilp->getExtension(pathNoExt) == "");
        CHECK(gDirUtilp->getExtension(pathExt) == "ext");

        // aa/bb/cc.dd/ee[.eXt]
        CHECK(gDirUtilp->getExtension(dottedPathNoExt) == "");
        CHECK(gDirUtilp->getExtension(dottedPathExt) == "ext");
    }

   std::string makeTestFile( const std::string& dir, const std::string& file )
   {
      std::string path = dir + file;
      LLFILE* handle = LLFile::fopen(path, LLFILE_MODE("w"));
      CAPTURE(path);
      REQUIRE(handle != NULL);
      // Harbison & Steele, 4th ed., p. 366: "If an error occurs, fputs
      // returns EOF; otherwise, it returns some other, nonnegative value."
      REQUIRE(fputs("test file", handle) != EOF);
      fclose(handle);
      return path;
   }

   std::string makeTestDir(  )
   {
      auto p = NamedTempFile::temp_path();
      std::filesystem::create_directories(p.native());

      std::string ret = p.string();

      // There's an implicit assumtion all over this code that the returned path ends with "/" (or "\")

      if(ret.size() >= 1 && ret[ ret.size()-1 ] != std::filesystem::path::preferred_separator )
          ret += std::filesystem::path::preferred_separator;

      return ret;
   }

   static const char* DirScanFilename[5] = { "file1.abc", "file2.abc", "file1.xyz", "file2.xyz", "file1.mno" };

   void require_scan_returns(const std::string& directory, const std::string& pattern, bool correctResult[5])
   {
      CAPTURE(directory, pattern);

      // Scan directory for pattern and check that exactly the files correctResult marks are found
      std::string scanResult;
      int   found = 0;
      bool  filesFound[5] = { false, false, false, false, false };
      //std::cerr << "searching '"+directory+"' for '"+pattern+"'\n";

      LLDirIterator iter(directory, pattern);
      while ( found <= 5 && iter.next(scanResult) )
      {
         found++;
         //std::cerr << "  found '"+scanResult+"'\n";
         int check;
         for (check=0; check < 5 && ! ( scanResult == DirScanFilename[check] ); check++)
         {
         }
         // check is now either 5 (not found) or the index of the matching name
         if (check < 5)
         {
            UNSCOPED_INFO("found file '" << DirScanFilename[check] << "' twice");
            REQUIRE_FALSE(filesFound[check]);
            filesFound[check] = true;
         }
         else // check is 5 - should not happen
         {
            FAIL("found unknown file '" << scanResult << "'");
         }
      }
      for (int i=0; i<5; i++)
      {
         CAPTURE(DirScanFilename[i]);
         if (correctResult[i])
         {
            REQUIRE(filesFound[i]);
         }
         else
         {
            REQUIRE_FALSE(filesFound[i]);
         }
      }
   }

   TEST_CASE("LLDir: LLDirIterator::next matches glob patterns", "[LLDir]")
   {
      std::string delim = gDirUtilp->getDirDelimiter();
      std::string dirTemp = LLFile::tmpdir();

      // Create the same 5 file names in each of two directories

      std::string dir1 = makeTestDir();
      std::string dir2 = makeTestDir();

      std::string dir1files[5];
      std::string dir2files[5];
      for (int i=0; i<5; i++)
      {
         dir1files[i] = makeTestFile(dir1, DirScanFilename[i]);
         dir2files[i] = makeTestFile(dir2, DirScanFilename[i]);
      }

      // Scan dir1 and see if each of the 5 files is found exactly once
      bool expected1[5] = { true, true, true, true, true };
      require_scan_returns(dir1, "*", expected1);

      // Scan dir1 and see if only the 2 *.xyz files are found
      bool  expected2[5] = { false, false, true, true, false };
      require_scan_returns(dir1, "*.xyz", expected2);

      // Scan dir2 and see if only the 1 *.mno file is found
      bool  expected3[5] = { false, false, false, false, true };
      require_scan_returns(dir2, "*.mno", expected3);

      // Scan dir1 and see if any *.foo files are found
      bool  expected4[5] = { false, false, false, false, false };
      require_scan_returns(dir1, "*.foo", expected4);

      // Scan dir1 and see if any file1.* files are found
      bool  expected5[5] = { true, false, true, false, true };
      require_scan_returns(dir1, "file1.*", expected5);

      // Scan dir1 and see if any file?.abc files are found
      bool  expected6[5] = { true, true, false, false, false };
      require_scan_returns(dir1, "file?.abc", expected6);

      // Scan dir2 and see if any file?.x?z files are found
      bool  expected7[5] = { false, false, true, true, false };
      require_scan_returns(dir2, "file?.x?z", expected7);

      // Scan dir2 and see if any file?.??c or *.??c files are found
      bool  expected8[5] = { true, true, false, false, false };
      require_scan_returns(dir2, "file?.??c", expected8);
      require_scan_returns(dir2, "*.??c", expected8);

      // Scan dir1 and see if any *.?n? files are found
      bool  expected9[5] = { false, false, false, false, true };
      require_scan_returns(dir1, "*.?n?", expected9);

      // Scan dir1 and see if any *.???? files are found
      bool  expected10[5] = { false, false, false, false, false };
      require_scan_returns(dir1, "*.????", expected10);

      // Scan dir1 and see if any ?????.* files are found
      bool  expected11[5] = { true, true, true, true, true };
      require_scan_returns(dir1, "?????.*", expected11);

      // Scan dir1 and see if any ??l??.xyz files are found
      bool  expected12[5] = { false, false, true, true, false };
      require_scan_returns(dir1, "??l??.xyz", expected12);

      bool expected13[5] = { true, false, true, false, false };
      require_scan_returns(dir1, "file1.{abc,xyz}", expected13);

      bool expected14[5] = { true, true, false, false, false };
      require_scan_returns(dir1, "file[0-9].abc", expected14);

      bool expected15[5] = { true, true, false, false, false };
      require_scan_returns(dir1, "file[!a-z].abc", expected15);

      // clean up all test files and directories
      for (int i=0; i<5; i++)
      {
         LLFile::remove(dir1files[i]);
         LLFile::remove(dir2files[i]);
      }
      LLFile::remove(dir1);
      LLFile::remove(dir2);
   }

    TEST_CASE("LLDir: findSkinnedFilenames searches skins and languages and caches probes", "[LLDir]")
    {
        LLDir_Dummy lldir;
        /*------------------------ "default", "en" -------------------------*/
        // Setting "default" means we shouldn't consider any "*/skins/steam"
        // directories; setting "en" means we shouldn't consider any "xui/fr"
        // directories.
        lldir.setSkinFolder("default", "en");
        CHECK(lldir.getSkinFolder() == "default");
        CHECK(lldir.getLanguage() == "en");

        // top-level directory of a skin isn't localized
        CHECK(lldir.findSkinnedFilenames(LLDir::SKINBASE, "colors.xml", LLDir::ALL_SKINS) ==
                      StringVec{ "install/skins/default/colors.xml", "user/skins/default/colors.xml" });
        // We should not have needed to check for skins/default/en. We should
        // just "know" that SKINBASE is not localized.
        lldir.require_not_checked("install/skins/default/en");

        CHECK(lldir.findSkinnedFilenames(LLDir::TEXTURES, "only_default.jpeg") ==
                      StringVec{ "install/skins/default/textures/only_default.jpeg" });
        // Nor should we have needed to check skins/default/textures/en
        // because textures is known not to be localized.
        lldir.require_not_checked("install/skins/default/textures/en");

        StringVec expected(StringVec{ "install/skins/default/xui/en/strings.xml", "user/skins/default/xui/en/strings.xml" });
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml", LLDir::ALL_SKINS) ==
                      expected);
        // The first time, we had to probe to find out whether xui was localized.
        lldir.require_checked("install/skins/default/xui/en");
        lldir.clear_checked();
        // Now make the same call again -- should return same result --
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml", LLDir::ALL_SKINS) ==
                      expected);
        // but this time it should remember that xui is localized.
        lldir.require_not_checked("install/skins/default/xui/en");

        // localized subdir with "en-us" instead of "en"
        CHECK(lldir.findSkinnedFilenames("html", "welcome.html") == StringVec{ "install/skins/default/html/en-us/welcome.html" });
        lldir.require_checked("install/skins/default/html/en");
        lldir.require_checked("install/skins/default/html/en-us");
        lldir.clear_checked();
        CHECK(lldir.findSkinnedFilenames("html", "welcome.html") == StringVec{ "install/skins/default/html/en-us/welcome.html" });
        lldir.require_not_checked("install/skins/default/html/en");
        lldir.require_not_checked("install/skins/default/html/en-us");

        CHECK(lldir.findSkinnedFilenames("future", "somefile.txt") == StringVec{ "install/skins/default/future/somefile.txt" });
        // Test probing for an unrecognized unlocalized future subdir.
        lldir.require_checked("install/skins/default/future/en");
        lldir.clear_checked();
        CHECK(lldir.findSkinnedFilenames("future", "somefile.txt") == StringVec{ "install/skins/default/future/somefile.txt" });
        // Second time it should remember that future is unlocalized.
        lldir.require_not_checked("install/skins/default/future/en");

        // When language is set to "en", requesting an html file pulls up the
        // "en-us" version -- not because it magically matches those strings,
        // but because there's no "en" localization and it falls back on the
        // default "en-us"! Note that it would probably still be better to
        // make the default localization be "en" and allow "en-gb" (or
        // whatever) localizations, which would work much more the way you'd
        // expect.
        CHECK(lldir.findSkinnedFilenames("html", "welcome.html") == StringVec{ "install/skins/default/html/en-us/welcome.html" });

        /*------------------------ "default", "fr" -------------------------*/
        // We start being able to distinguish localized subdirs from
        // unlocalized when we ask for a non-English language.
        lldir.setSkinFolder("default", "fr");
        CHECK(lldir.getLanguage() == "fr");

        // pass merge=true to request this filename in all relevant skins
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml", LLDir::ALL_SKINS) ==
                      StringVec{ "install/skins/default/xui/en/strings.xml", "install/skins/default/xui/fr/strings.xml",
                                 "user/skins/default/xui/en/strings.xml", "user/skins/default/xui/fr/strings.xml" });

        // pass (or default) merge=false to request only most specific skin
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml") ==
                      StringVec{ "user/skins/default/xui/en/strings.xml", "user/skins/default/xui/fr/strings.xml" });

        // Our dummy floater.xml has a user localization (for "fr") but no
        // English override. This is a case in which CURRENT_SKIN nonetheless
        // returns paths from two different skins.
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "floater.xml") ==
                      StringVec{ "install/skins/default/xui/en/floater.xml", "user/skins/default/xui/fr/floater.xml" });

        // Our dummy newfile.xml has an English override but no user
        // localization. This is another case in which CURRENT_SKIN
        // nonetheless returns paths from two different skins.
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "newfile.xml") ==
                      StringVec{ "user/skins/default/xui/en/newfile.xml", "install/skins/default/xui/fr/newfile.xml" });

        CHECK(lldir.findSkinnedFilenames("html", "welcome.html") ==
                      StringVec{ "install/skins/default/html/en-us/welcome.html", "install/skins/default/html/fr/welcome.html" });

        /*------------------------ "default", "zh" -------------------------*/
        lldir.setSkinFolder("default", "zh");
        // Because strings.xml has only a "fr" override but no "zh" override
        // in any skin, the most localized version we can find is "en".
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml") == StringVec{ "user/skins/default/xui/en/strings.xml" });

        /*------------------------- "steam", "en" --------------------------*/
        lldir.setSkinFolder("steam", "en");

        CHECK(lldir.findSkinnedFilenames(LLDir::SKINBASE, "colors.xml", LLDir::ALL_SKINS) ==
                      StringVec{ "install/skins/default/colors.xml", "install/skins/steam/colors.xml", "user/skins/default/colors.xml",
                                 "user/skins/steam/colors.xml" });

        CHECK(lldir.findSkinnedFilenames(LLDir::TEXTURES, "only_default.jpeg") ==
                      StringVec{ "install/skins/default/textures/only_default.jpeg" });

        CHECK(lldir.findSkinnedFilenames(LLDir::TEXTURES, "only_steam.jpeg") ==
                      StringVec{ "install/skins/steam/textures/only_steam.jpeg" });

        CHECK(lldir.findSkinnedFilenames(LLDir::TEXTURES, "only_user_default.jpeg") ==
                      StringVec{ "user/skins/default/textures/only_user_default.jpeg" });

        CHECK(lldir.findSkinnedFilenames(LLDir::TEXTURES, "only_user_steam.jpeg") ==
                      StringVec{ "user/skins/steam/textures/only_user_steam.jpeg" });

        // CURRENT_SKIN
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml") == StringVec{ "user/skins/steam/xui/en/strings.xml" });

        // pass constraint=ALL_SKINS to request this filename in all relevant skins
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml", LLDir::ALL_SKINS) ==
                      StringVec{ "install/skins/default/xui/en/strings.xml", "install/skins/steam/xui/en/strings.xml",
                                 "user/skins/default/xui/en/strings.xml", "user/skins/steam/xui/en/strings.xml" });

        /*------------------------- "steam", "fr" --------------------------*/
        lldir.setSkinFolder("steam", "fr");

        // pass CURRENT_SKIN to request only the most specialized files
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml") ==
                      StringVec{ "user/skins/steam/xui/en/strings.xml", "user/skins/steam/xui/fr/strings.xml" });

        // pass ALL_SKINS to request this filename in all relevant skins
        CHECK(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml", LLDir::ALL_SKINS) ==
                      StringVec{ "install/skins/default/xui/en/strings.xml", "install/skins/default/xui/fr/strings.xml",
                                 "install/skins/steam/xui/en/strings.xml", "install/skins/steam/xui/fr/strings.xml",
                                 "user/skins/default/xui/en/strings.xml", "user/skins/default/xui/fr/strings.xml",
                                 "user/skins/steam/xui/en/strings.xml", "user/skins/steam/xui/fr/strings.xml" });
    }

    TEST_CASE("LLDir: add joins with a single slash and skips an empty part", "[LLDir]")
    {
        LLDir_Dummy lldir;
        CHECK(lldir.add("", "") == "");
        CHECK(lldir.add("", "b") == "b");
        CHECK(lldir.add("a", "") == "a");
        CHECK(lldir.add("a", "b") == "a/b");
        CHECK(lldir.add("a", "/b") == "a/b");
        CHECK(lldir.add("a/", "b") == "a/b");
        CHECK(lldir.add("a/", "/b") == "a/b");
    }

    TEST_CASE("LLDir: relocateLegacyPath maps a moved legacy profile into its new dirs", "[LLDir]")
    {
        LLDir_Relocated lldir;
        CHECK(lldir.relocateLegacyPath("old") == "user");
        CHECK(lldir.relocateLegacyPath("old/") == "user");
        CHECK(lldir.relocateLegacyPath("old/bob_resident") == "user/bob_resident");
        CHECK(lldir.relocateLegacyPath("old/user_settings/settings.xml") == "cfg/settings.xml");
        CHECK(lldir.relocateLegacyPath("old/logs") == "state");
        CHECK(lldir.relocateLegacyPath("old/cache/texturecache") == "cachedir/texturecache");
        UNSCOPED_INFO("a sibling sharing the root's prefix");
        CHECK(lldir.relocateLegacyPath("oldish/x") == "oldish/x");
        CHECK(lldir.relocateLegacyPath("chat/logs") == "chat/logs");

        lldir.buildFilesystem("old/bob_resident");
        UNSCOPED_INFO("a path still in the legacy root stays");
        CHECK(lldir.relocateLegacyPath("old/bob_resident") == "old/bob_resident");

        LLDir_Dummy unmoved;
        UNSCOPED_INFO("no legacy root");
        CHECK(unmoved.relocateLegacyPath("old/bob_resident") == "old/bob_resident");
    }

#if LL_LINUX
    TEST_CASE("LLDir: LLDirXDG::layout uses only absolute XDG variables and legacyLayout the old dirs", "[LLDir]")
    {
        ScratchHome scratch;
        LLDirXDG::Layout dirs = LLDirXDG::layout("/h", "app");
        CHECK(dirs.data == "/h/.local/share/app");
        CHECK(dirs.config == "/h/.config/app");
        CHECK(dirs.cache == "/h/.cache/app");
        CHECK(dirs.state == "/h/.local/state/app");

        ::setenv("XDG_DATA_HOME", "/d", 1);
        ::setenv("XDG_CONFIG_HOME", "relative/c", 1);
        ::setenv("XDG_CACHE_HOME", "", 1);
        ::setenv("XDG_STATE_HOME", "/s", 1);
        dirs = LLDirXDG::layout("/h", "app");
        CHECK(dirs.data == "/d/app");
        UNSCOPED_INFO("a relative XDG_CONFIG_HOME is ignored");
        CHECK(dirs.config == "/h/.config/app");
        UNSCOPED_INFO("an empty XDG_CACHE_HOME is ignored");
        CHECK(dirs.cache == "/h/.cache/app");
        CHECK(dirs.state == "/s/app");

        dirs = LLDirXDG::legacyLayout("/h/.app");
        CHECK(dirs.data == "/h/.app");
        CHECK(dirs.config == "/h/.app/user_settings");
        CHECK(dirs.cache == "/h/.app/cache");
        CHECK(dirs.state == "/h/.app/logs");
    }

    TEST_CASE("LLDir: LLDirXDG::migrate moves a legacy profile into the XDG dirs", "[LLDir]")
    {
        ScratchHome scratch;
        std::vector<std::string> notes;
        const LLDirXDG::Layout dirs = LLDirXDG::layout(scratch.home, "app");

        CHECK(LLDirXDG::migrate(scratch.path(".app"), dirs, notes) == LLDirXDG::Migration::NONE);
        CHECK_FALSE(scratch.exists(".local"));

        scratch.writeLegacyProfile();
        CHECK(LLDirXDG::migrate(scratch.path(".app"), dirs, notes) == LLDirXDG::Migration::MOVED);
        CHECK_FALSE(scratch.exists(".app"));
        CHECK(scratch.exists(".config/app/settings.xml"));
        CHECK(scratch.exists(".local/state/app/Alchemy.log"));
        CHECK(scratch.exists(".cache/app/texturecache/t"));
        CHECK(scratch.exists(".local/share/app/bob_resident/settings_per_account.xml"));
        CHECK(scratch.exists(".local/share/app/skins/default/colors.xml"));
        CHECK_FALSE(scratch.exists(".local/share/app/user_settings"));
        CHECK_FALSE(scratch.exists(".local/share/app/logs"));
        CHECK_FALSE(scratch.exists(".local/share/app/cache"));
        UNSCOPED_INFO("the base dirs it makes are private");
        CHECK((fs::status(scratch.path(".local/state")).permissions() & fs::perms::all) == fs::perms::owner_all);

        // an older viewer run afterwards makes a new legacy root; it stays put
        scratch.write(".app/user_settings/settings.xml", "new");
        UNSCOPED_INFO("a legacy root made after the move is left");
        CHECK(LLDirXDG::migrate(scratch.path(".app"), dirs, notes) == LLDirXDG::Migration::NONE);
        CHECK(scratch.exists(".app/user_settings/settings.xml"));
    }

    TEST_CASE("LLDir: LLDirXDG::migrate leaves a profile it cannot move whole", "[LLDir]")
    {
        std::vector<std::string> notes;
        {
            ScratchHome scratch;
            const LLDirXDG::Layout dirs = LLDirXDG::layout(scratch.home, "app");
            scratch.writeLegacyProfile();
            scratch.write(".config/app/other.xml");
            UNSCOPED_INFO("a config dir in the way");
            CHECK(LLDirXDG::migrate(scratch.path(".app"), dirs, notes) == LLDirXDG::Migration::FAILED);
            UNSCOPED_INFO("the profile is put back");
            CHECK(scratch.legacyProfileIntact());
            CHECK_FALSE(scratch.exists(".local/share/app"));
            CHECK_FALSE(scratch.exists(".local/state/app"));
            CHECK(scratch.exists(".config/app/other.xml"));
        }
        {
            ScratchHome scratch;
            const LLDirXDG::Layout dirs = LLDirXDG::layout(scratch.home, "app");
            scratch.writeLegacyProfile();
            scratch.write(".app/logs/Alchemy.exec_marker");
            const int fd = ::open(scratch.path(".app/logs/Alchemy.exec_marker").c_str(), O_RDWR);
            UNSCOPED_INFO("the running viewer's marker is locked");
            REQUIRE((fd != -1 && ::flock(fd, LOCK_EX | LOCK_NB) == 0));
            CHECK(LLDirXDG::migrate(scratch.path(".app"), dirs, notes) == LLDirXDG::Migration::FAILED);
            CHECK(scratch.legacyProfileIntact());
            ::close(fd);
            UNSCOPED_INFO("moved once the viewer exits");
            CHECK(LLDirXDG::migrate(scratch.path(".app"), dirs, notes) == LLDirXDG::Migration::MOVED);
        }
        {
            ScratchHome scratch;
            const LLDirXDG::Layout dirs = LLDirXDG::layout(scratch.home, "app");
            scratch.writeLegacyProfile();
            fs::rename(scratch.path(".app"), scratch.path("elsewhere"));
            fs::create_directory_symlink(scratch.path("elsewhere"), scratch.path(".app"));
            UNSCOPED_INFO("a symlinked profile");
            CHECK(LLDirXDG::migrate(scratch.path(".app"), dirs, notes) == LLDirXDG::Migration::FAILED);
            CHECK(scratch.legacyProfileIntact());
            CHECK(fs::is_symlink(scratch.path(".app")));
        }
    }

    TEST_CASE("LLDir: LLDirXDG::adoptCache renames an old cache only where none is in the way", "[LLDir]")
    {
        ScratchHome scratch;
        std::vector<std::string> notes;
        const std::string old_cache = scratch.path(".cache/App");
        const std::string cache = scratch.path(".cache/app");

        LLDirXDG::adoptCache(old_cache, cache, notes);
        CHECK_FALSE(scratch.exists(".cache"));

        scratch.write(".cache/App/texturecache/t");
        LLDirXDG::adoptCache(old_cache, cache, notes);
        CHECK(scratch.exists(".cache/app/texturecache/t"));
        CHECK_FALSE(scratch.exists(".cache/App"));

        scratch.write(".cache/App/texturecache/stale");
        LLDirXDG::adoptCache(old_cache, cache, notes);
        CHECK(scratch.exists(".cache/app/texturecache/t"));
        CHECK(scratch.exists(".cache/App/texturecache/stale"));
        CHECK_FALSE(scratch.exists(".cache/app/texturecache/stale"));
    }
#endif // LL_LINUX
}
