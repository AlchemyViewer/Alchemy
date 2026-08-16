/**
 * @file lldir_test.cpp
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
#include <gtest/gtest.h>
#include "../lldir.h"
#include "../lldiriterator.h"
#include "../test/namedtempfile.h"
#include "llstring.h"
#include "tests/StringVec.h"

// For some tests, use a dummy LLDir that uses memory data instead of touching
// the filesystem
struct LLDir_Dummy : public LLDir {
    /*----------------------------- LLDir API ------------------------------*/
    LLDir_Dummy() {
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
        static const char* preload[] = {"install/skins/default/colors.xml",
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

                                        "install/skins/default/future/somefile.txt"};
        for (const char* path : preload) buildFilesystem(path);
    }

    virtual ~LLDir_Dummy() {}

    virtual void initAppDirs(const std::string& app_name, const std::string& app_read_only_data_dir) {
        // Implement this when we write a test that needs it
    }

    virtual std::string getCurPath() {
        // Implement this when we write a test that needs it
        return "";
    }

    virtual U32 countFilesInDir(const std::string& dirname, const std::string& mask) {
        // Implement this when we write a test that needs it
        return 0;
    }

    virtual bool fileExists(const std::string& pathname) const {
        // Record fileExists() calls so we can check whether caching is
        // working right. Certain LLDir calls should be able to make decisions
        // without calling fileExists() again, having already checked existence.
        mChecked.insert(pathname);
        // For our simple flat set of strings, see whether the identical
        // pathname exists in our set.
        return mFilesystem.find(pathname) != mFilesystem.end();
    }

    virtual std::string getLLPluginFilename(std::string base_name) {
        // Implement this when we write a test that needs it
        return "";
    }

    /*----------------------------- Dummy data -----------------------------*/
    void clearFilesystem() { mFilesystem.clear(); }
    void buildFilesystem(const std::string& path) {
        // Split the pathname on slashes, ignoring leading, trailing, doubles
        StringVec components;
        LLStringUtil::getTokens(path, components, "/");
        // Ensure we have an entry representing every level of this path
        std::string partial;
        for (std::string component : components) {
            append(partial, component);
            mFilesystem.insert(partial);
        }
    }

    void clear_checked() { mChecked.clear(); }
    void ensure_checked(const std::string& pathname) const {
        EXPECT_TRUE(mChecked.find(pathname) != mChecked.end()) << pathname << " was not checked but should have been";
    }
    void ensure_not_checked(const std::string& pathname) const {
        EXPECT_TRUE(mChecked.find(pathname) == mChecked.end()) << pathname << " was checked but should not have been";
    }

    std::set<std::string> mFilesystem;
    mutable std::set<std::string> mChecked;
};

TEST(LLDirIntegration, GetDirDelimiter) {
    EXPECT_TRUE(!gDirUtilp->getDirDelimiter().empty()) << "getDirDelimiter";
}

TEST(LLDirIntegration, GetBaseFileName) {
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

    EXPECT_EQ(gDirUtilp->getBaseFileName(rawFile, false), "foo") << "getBaseFileName/r-no-ext/no-strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(rawFile, true), "foo") << "getBaseFileName/r-no-ext/strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(rawFileExt, false), "foo.bAr") << "getBaseFileName/r-ext/no-strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(rawFileExt, true), "foo") << "getBaseFileName/r-ext/strip-exten";

    // foo.

    EXPECT_EQ(gDirUtilp->getBaseFileName(rawFileNullExt, false), "foo.") << "getBaseFileName/rn-no-ext/no-strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(rawFileNullExt, true), "foo") << "getBaseFileName/rn-no-ext/strip-exten";

    // .bAr
    // interesting case - with no basename, this IS the basename, not the extension.

    EXPECT_EQ(gDirUtilp->getBaseFileName(rawExt, false), ".bAr") << "getBaseFileName/e-ext/no-strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(rawExt, true), ".bAr") << "getBaseFileName/e-ext/strip-exten";

    // .

    EXPECT_EQ(gDirUtilp->getBaseFileName(rawDot, false), ".") << "getBaseFileName/d/no-strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(rawDot, true), ".") << "getBaseFileName/d/strip-exten";

    // aa/bb/cc/dd/ee[.eXt]

    EXPECT_EQ(gDirUtilp->getBaseFileName(pathNoExt, false), "ee") << "getBaseFileName/no-ext/no-strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(pathNoExt, true), "ee") << "getBaseFileName/no-ext/strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(pathExt, false), "ee.eXt") << "getBaseFileName/ext/no-strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(pathExt, true), "ee") << "getBaseFileName/ext/strip-exten";

    // aa/bb/cc.dd/ee[.eXt]

    EXPECT_EQ(gDirUtilp->getBaseFileName(dottedPathNoExt, false), "ee") << "getBaseFileName/d-no-ext/no-strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(dottedPathNoExt, true), "ee") << "getBaseFileName/d-no-ext/strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(dottedPathExt, false), "ee.eXt") << "getBaseFileName/d-ext/no-strip-exten";

    EXPECT_EQ(gDirUtilp->getBaseFileName(dottedPathExt, true), "ee") << "getBaseFileName/d-ext/strip-exten";
}

TEST(LLDirIntegration, GetDirName) {
    std::string delim = gDirUtilp->getDirDelimiter();
    std::string rawFile = "foo";
    std::string rawFileExt = "foo.bAr";
    std::string pathNoExt = "aa" + delim + "bb" + delim + "cc" + delim + "dd" + delim + "ee";
    std::string pathExt = pathNoExt + ".eXt";
    std::string dottedPathNoExt = "aa" + delim + "bb" + delim + "cc.dd" + delim + "ee";
    std::string dottedPathExt = dottedPathNoExt + ".eXt";

    // foo[.bAr]

    EXPECT_EQ(gDirUtilp->getDirName(rawFile), "") << "getDirName/r-no-ext";

    EXPECT_EQ(gDirUtilp->getDirName(rawFileExt), "") << "getDirName/r-ext";

    // aa/bb/cc/dd/ee[.eXt]

    EXPECT_EQ(gDirUtilp->getDirName(pathNoExt), "aa" + delim + "bb" + delim + "cc" + delim + "dd") << "getDirName/no-ext";

    EXPECT_EQ(gDirUtilp->getDirName(pathExt), "aa" + delim + "bb" + delim + "cc" + delim + "dd") << "getDirName/ext";

    // aa/bb/cc.dd/ee[.eXt]

    EXPECT_EQ(gDirUtilp->getDirName(dottedPathNoExt), "aa" + delim + "bb" + delim + "cc.dd") << "getDirName/d-no-ext";

    EXPECT_EQ(gDirUtilp->getDirName(dottedPathExt), "aa" + delim + "bb" + delim + "cc.dd") << "getDirName/d-ext";
}

TEST(LLDirIntegration, GetExtension) {
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

    EXPECT_EQ(gDirUtilp->getExtension(rawFile), "") << "getExtension/r-no-ext";

    EXPECT_EQ(gDirUtilp->getExtension(rawFileExt), "bar") << "getExtension/r-ext";

    // foo.

    EXPECT_EQ(gDirUtilp->getExtension(rawFileNullExt), "") << "getExtension/rn-no-ext";

    // .bAr
    // interesting case - with no basename, this IS the basename, not the extension.

    EXPECT_EQ(gDirUtilp->getExtension(rawExt), "") << "getExtension/e-ext";

    // .

    EXPECT_EQ(gDirUtilp->getExtension(rawDot), "") << "getExtension/d";

    // aa/bb/cc/dd/ee[.eXt]

    EXPECT_EQ(gDirUtilp->getExtension(pathNoExt), "") << "getExtension/no-ext";

    EXPECT_EQ(gDirUtilp->getExtension(pathExt), "ext") << "getExtension/ext";

    // aa/bb/cc.dd/ee[.eXt]

    EXPECT_EQ(gDirUtilp->getExtension(dottedPathNoExt), "") << "getExtension/d-no-ext";

    EXPECT_EQ(gDirUtilp->getExtension(dottedPathExt), "ext") << "getExtension/d-ext";
}

std::string makeTestFile(const std::string& dir, const std::string& file) {
    std::string path = dir + file;
    LLFILE* handle = LLFile::fopen(path, LLFILE_MODE("w"));
    EXPECT_TRUE(handle != NULL) << "failed to open test file '" + path + "'";
    // Harbison & Steele, 4th ed., p. 366: "If an error occurs, fputs
    // returns EOF; otherwise, it returns some other, nonnegative value."
    EXPECT_TRUE(EOF != fputs("test file", handle)) << "failed to write to test file '" + path + "'";
    fclose(handle);
    return path;
}

std::string makeTestDir() {
    auto p = NamedTempFile::temp_path();
    std::filesystem::create_directories(p.native());

    std::string ret = p.string();

    // There's an implicit assumtion all over this code that the returned path ends with "/" (or "\")

    if (ret.size() >= 1 && ret[ret.size() - 1] != std::filesystem::path::preferred_separator) ret += std::filesystem::path::preferred_separator;

    return ret;
}

static const char* DirScanFilename[5] = {"file1.abc", "file2.abc", "file1.xyz", "file2.xyz", "file1.mno"};

void scanTest(const std::string& directory, const std::string& pattern, bool correctResult[5]) {
    // Scan directory and see if any file1.* files are found
    std::string scanResult;
    int found = 0;
    bool filesFound[5] = {false, false, false, false, false};
    //std::cerr << "searching '"+directory+"' for '"+pattern+"'\n";

    LLDirIterator iter(directory, pattern);
    while (found <= 5 && iter.next(scanResult)) {
        found++;
        //std::cerr << "  found '"+scanResult+"'\n";
        int check;
        for (check = 0; check < 5 && !(scanResult == DirScanFilename[check]); check++) {}
        // check is now either 5 (not found) or the index of the matching name
        if (check < 5) {
            EXPECT_TRUE(!filesFound[check]) << "found file '" + (std::string)DirScanFilename[check] + "' twice";
            filesFound[check] = true;
        } else // check is 5 - should not happen
        {
            FAIL() << "found unknown file '" + scanResult + "'";
        }
    }
    for (int i = 0; i < 5; i++)
        if (correctResult[i])
            EXPECT_TRUE(filesFound[i]) << "scan of '" + directory + "' using '" + pattern + "' did not return '" + DirScanFilename[i] + "'";
        else EXPECT_TRUE(!filesFound[i]) << "scan of '" + directory + "' using '" + pattern + "' incorrectly returned '" + DirScanFilename[i] + "'";
}

TEST(LLDirIntegration, LLDirIteratorNext) {
    // Create the same 5 file names of the two directories

    std::string dir1 = makeTestDir();
    std::string dir2 = makeTestDir();

    std::string dir1files[5];
    std::string dir2files[5];
    for (int i = 0; i < 5; i++) {
        dir1files[i] = makeTestFile(dir1, DirScanFilename[i]);
        dir2files[i] = makeTestFile(dir2, DirScanFilename[i]);
    }

    // Scan dir1 and see if each of the 5 files is found exactly once
    bool expected1[5] = {true, true, true, true, true};
    scanTest(dir1, "*", expected1);

    // Scan dir2 and see if only the 2 *.xyz files are found
    bool expected2[5] = {false, false, true, true, false};
    scanTest(dir1, "*.xyz", expected2);

    // Scan dir2 and see if only the 1 *.mno file is found
    bool expected3[5] = {false, false, false, false, true};
    scanTest(dir2, "*.mno", expected3);

    // Scan dir1 and see if any *.foo files are found
    bool expected4[5] = {false, false, false, false, false};
    scanTest(dir1, "*.foo", expected4);

    // Scan dir1 and see if any file1.* files are found
    bool expected5[5] = {true, false, true, false, true};
    scanTest(dir1, "file1.*", expected5);

    // Scan dir1 and see if any file1.* files are found
    bool expected6[5] = {true, true, false, false, false};
    scanTest(dir1, "file?.abc", expected6);

    // Scan dir2 and see if any file?.x?z files are found
    bool expected7[5] = {false, false, true, true, false};
    scanTest(dir2, "file?.x?z", expected7);

    // Scan dir2 and see if any file?.??c files are found
    bool expected8[5] = {true, true, false, false, false};
    scanTest(dir2, "file?.??c", expected8);
    scanTest(dir2, "*.??c", expected8);

    // Scan dir1 and see if any *.?n? files are found
    bool expected9[5] = {false, false, false, false, true};
    scanTest(dir1, "*.?n?", expected9);

    // Scan dir1 and see if any *.???? files are found
    bool expected10[5] = {false, false, false, false, false};
    scanTest(dir1, "*.????", expected10);

    // Scan dir1 and see if any ?????.* files are found
    bool expected11[5] = {true, true, true, true, true};
    scanTest(dir1, "?????.*", expected11);

    // Scan dir1 and see if any ??l??.xyz files are found
    bool expected12[5] = {false, false, true, true, false};
    scanTest(dir1, "??l??.xyz", expected12);

    bool expected13[5] = {true, false, true, false, false};
    scanTest(dir1, "file1.{abc,xyz}", expected13);

    bool expected14[5] = {true, true, false, false, false};
    scanTest(dir1, "file[0-9].abc", expected14);

    bool expected15[5] = {true, true, false, false, false};
    scanTest(dir1, "file[!a-z].abc", expected15);

    // clean up all test files and directories
    for (int i = 0; i < 5; i++) {
        LLFile::remove(dir1files[i]);
        LLFile::remove(dir2files[i]);
    }
    LLFile::remove(dir1);
    LLFile::remove(dir2);
}

TEST(LLDirIntegration, FindSkinnedFilenames) {
    LLDir_Dummy lldir;
    /*------------------------ "default", "en" -------------------------*/
    // Setting "default" means we shouldn't consider any "*/skins/steam"
    // directories; setting "en" means we shouldn't consider any "xui/fr"
    // directories.
    lldir.setSkinFolder("default", "en");
    EXPECT_EQ(lldir.getSkinFolder(), "default");
    EXPECT_EQ(lldir.getLanguage(), "en");

    // top-level directory of a skin isn't localized
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::SKINBASE, "colors.xml", LLDir::ALL_SKINS),
              StringVec({"install/skins/default/colors.xml", "user/skins/default/colors.xml"}));
    // We should not have needed to check for skins/default/en. We should
    // just "know" that SKINBASE is not localized.
    lldir.ensure_not_checked("install/skins/default/en");

    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::TEXTURES, "only_default.jpeg"), StringVec({"install/skins/default/textures/only_default.jpeg"}));
    // Nor should we have needed to check skins/default/textures/en
    // because textures is known not to be localized.
    lldir.ensure_not_checked("install/skins/default/textures/en");

    StringVec expected(StringVec{"install/skins/default/xui/en/strings.xml", "user/skins/default/xui/en/strings.xml"});
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml", LLDir::ALL_SKINS), expected);
    // The first time, we had to probe to find out whether xui was localized.
    lldir.ensure_checked("install/skins/default/xui/en");
    lldir.clear_checked();
    // Now make the same call again -- should return same result --
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml", LLDir::ALL_SKINS), expected);
    // but this time it should remember that xui is localized.
    lldir.ensure_not_checked("install/skins/default/xui/en");

    // localized subdir with "en-us" instead of "en"
    EXPECT_EQ(lldir.findSkinnedFilenames("html", "welcome.html"), StringVec({"install/skins/default/html/en-us/welcome.html"}));
    lldir.ensure_checked("install/skins/default/html/en");
    lldir.ensure_checked("install/skins/default/html/en-us");
    lldir.clear_checked();
    EXPECT_EQ(lldir.findSkinnedFilenames("html", "welcome.html"), StringVec({"install/skins/default/html/en-us/welcome.html"}));
    lldir.ensure_not_checked("install/skins/default/html/en");
    lldir.ensure_not_checked("install/skins/default/html/en-us");

    EXPECT_EQ(lldir.findSkinnedFilenames("future", "somefile.txt"), StringVec({"install/skins/default/future/somefile.txt"}));
    // Test probing for an unrecognized unlocalized future subdir.
    lldir.ensure_checked("install/skins/default/future/en");
    lldir.clear_checked();
    EXPECT_EQ(lldir.findSkinnedFilenames("future", "somefile.txt"), StringVec({"install/skins/default/future/somefile.txt"}));
    // Second time it should remember that future is unlocalized.
    lldir.ensure_not_checked("install/skins/default/future/en");

    // When language is set to "en", requesting an html file pulls up the
    // "en-us" version -- not because it magically matches those strings,
    // but because there's no "en" localization and it falls back on the
    // default "en-us"! Note that it would probably still be better to
    // make the default localization be "en" and allow "en-gb" (or
    // whatever) localizations, which would work much more the way you'd
    // expect.
    EXPECT_EQ(lldir.findSkinnedFilenames("html", "welcome.html"), StringVec({"install/skins/default/html/en-us/welcome.html"}));

    /*------------------------ "default", "fr" -------------------------*/
    // We start being able to distinguish localized subdirs from
    // unlocalized when we ask for a non-English language.
    lldir.setSkinFolder("default", "fr");
    EXPECT_EQ(lldir.getLanguage(), "fr");

    // pass merge=true to request this filename in all relevant skins
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml", LLDir::ALL_SKINS),
              StringVec({"install/skins/default/xui/en/strings.xml", "install/skins/default/xui/fr/strings.xml",
                         "user/skins/default/xui/en/strings.xml", "user/skins/default/xui/fr/strings.xml"}));

    // pass (or default) merge=false to request only most specific skin
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml"),
              StringVec({"user/skins/default/xui/en/strings.xml", "user/skins/default/xui/fr/strings.xml"}));

    // Our dummy floater.xml has a user localization (for "fr") but no
    // English override. This is a case in which CURRENT_SKIN nonetheless
    // returns paths from two different skins.
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "floater.xml"),
              StringVec({"install/skins/default/xui/en/floater.xml", "user/skins/default/xui/fr/floater.xml"}));

    // Our dummy newfile.xml has an English override but no user
    // localization. This is another case in which CURRENT_SKIN
    // nonetheless returns paths from two different skins.
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "newfile.xml"),
              StringVec({"user/skins/default/xui/en/newfile.xml", "install/skins/default/xui/fr/newfile.xml"}));

    EXPECT_EQ(lldir.findSkinnedFilenames("html", "welcome.html"),
              StringVec({"install/skins/default/html/en-us/welcome.html", "install/skins/default/html/fr/welcome.html"}));

    /*------------------------ "default", "zh" -------------------------*/
    lldir.setSkinFolder("default", "zh");
    // Because strings.xml has only a "fr" override but no "zh" override
    // in any skin, the most localized version we can find is "en".
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml"), StringVec({"user/skins/default/xui/en/strings.xml"}));

    /*------------------------- "steam", "en" --------------------------*/
    lldir.setSkinFolder("steam", "en");

    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::SKINBASE, "colors.xml", LLDir::ALL_SKINS),
              StringVec({"install/skins/default/colors.xml", "install/skins/steam/colors.xml", "user/skins/default/colors.xml",
                         "user/skins/steam/colors.xml"}));

    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::TEXTURES, "only_default.jpeg"), StringVec({"install/skins/default/textures/only_default.jpeg"}));

    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::TEXTURES, "only_steam.jpeg"), StringVec({"install/skins/steam/textures/only_steam.jpeg"}));

    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::TEXTURES, "only_user_default.jpeg"),
              StringVec({"user/skins/default/textures/only_user_default.jpeg"}));

    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::TEXTURES, "only_user_steam.jpeg"), StringVec({"user/skins/steam/textures/only_user_steam.jpeg"}));

    // CURRENT_SKIN
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml"), StringVec({"user/skins/steam/xui/en/strings.xml"}));

    // pass constraint=ALL_SKINS to request this filename in all relevant skins
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml", LLDir::ALL_SKINS),
              StringVec({"install/skins/default/xui/en/strings.xml", "install/skins/steam/xui/en/strings.xml",
                         "user/skins/default/xui/en/strings.xml", "user/skins/steam/xui/en/strings.xml"}));

    /*------------------------- "steam", "fr" --------------------------*/
    lldir.setSkinFolder("steam", "fr");

    // pass CURRENT_SKIN to request only the most specialized files
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml"),
              StringVec({"user/skins/steam/xui/en/strings.xml", "user/skins/steam/xui/fr/strings.xml"}));

    // pass ALL_SKINS to request this filename in all relevant skins
    EXPECT_EQ(lldir.findSkinnedFilenames(LLDir::XUI, "strings.xml", LLDir::ALL_SKINS),
              StringVec({"install/skins/default/xui/en/strings.xml", "install/skins/default/xui/fr/strings.xml",
                         "install/skins/steam/xui/en/strings.xml", "install/skins/steam/xui/fr/strings.xml", "user/skins/default/xui/en/strings.xml",
                         "user/skins/default/xui/fr/strings.xml", "user/skins/steam/xui/en/strings.xml", "user/skins/steam/xui/fr/strings.xml"}));
}

TEST(LLDirIntegration, Add) {
    LLDir_Dummy lldir;
    EXPECT_EQ(lldir.add("", ""), "") << "both empty";
    EXPECT_EQ(lldir.add("", "b"), "b") << "path empty";
    EXPECT_EQ(lldir.add("a", ""), "a") << "name empty";
    EXPECT_EQ(lldir.add("a", "b"), "a/b") << "both simple";
    EXPECT_EQ(lldir.add("a", "/b"), "a/b") << "name leading slash";
    EXPECT_EQ(lldir.add("a/", "b"), "a/b") << "path trailing slash";
    EXPECT_EQ(lldir.add("a/", "/b"), "a/b") << "both bring slashes";
}
