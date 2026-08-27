/**
 * @file llui_libtest.cpp
 * @brief Integration test for the LLUI library
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
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

#include "llui_libtest.h"

// project includes
#include "alparamfingerprint.h"
#include "llwidgetreg.h"

// linden library includes
#include "llcontrol.h"      // LLControlGroup
#include "lldir.h"
#include "lldiriterator.h"
#include "llerrorcontrol.h"
#include "llfloater.h"
#include "llfontfreetype.h"
#include "llfontgl.h"
#include "lltexture.h"
#include "lltransutil.h"
#include "llui.h"
#include "lluictrlfactory.h"

#include <iostream>

// *TODO: switch to using TUT
// *TODO: teach Parabuild about this program, run automatically after full builds

// I believe these must be globals, not stack variables.  JC
LLControlGroup gSavedSettings("Global");    // saved at end of session
LLControlGroup gSavedPerAccountSettings("PerAccount"); // saved at end of session
LLControlGroup gWarningSettings("Warnings"); // persists ignored dialogs/warnings

// [RLVa:KB] - Checked: 2010-11-12 (RLVa-1.2.2a) | Added: RLVa-1.2.2a
#include "llavatarname.h"

// Stub for rlvGetAnonym
const std::string& rlvGetAnonym(const LLAvatarName& avName)
{
    static std::string strAnonym = "A resident";
    return strAnonym;
}
// [/RLVa:KB]

// LLUIImage's constructor caches getWidth()/getHeight(), and those dereference
// the texture. Virtual dispatch is not yet active during that base-class
// construction, so a subclass override cannot stand in for it -- the old
// "NULL ImageGL, don't deref!" arrangement now crashes. Supply a real texture.
class TestTexture : public LLTexture
{
public:
    S32 getWidth(S32 discard_level = -1) const override { return 16; }
    S32 getHeight(S32 discard_level = -1) const override { return 16; }
};

// We can't create LLImageGL objects because we have no window or rendering
// context.  Provide enough of an LLUIImage to test the LLUI library without
// an underlying image.
class TestUIImage : public LLUIImage
{
public:
    TestUIImage()
    :   LLUIImage( std::string(), new TestTexture() )
    { }

    /*virtual*/ S32 getWidth() const
    {
        return 16;
    }

    /*virtual*/ S32 getHeight() const
    {
        return 16;
    }
};


// We need to supply dummy images
class TestImageProvider : public LLImageProviderInterface
{
public:
    LLPointer<LLUIImage> getUIImage(std::string_view name, S32 priority) override
    {
        return makeImage();
    }

    LLPointer<LLUIImage> getUIImageByID(const LLUUID& id, S32 priority) override
    {
        return makeImage();
    }

    void cleanUp() override
    {
    }

    LLPointer<LLUIImage> makeImage()
    {
        LLPointer<LLUIImage> image = new TestUIImage();
        mImageList.push_back(image);
        return image;
    }

public:
    // Unclear if we need this, hold on to one copy of each image we make
    std::vector<LLPointer<LLUIImage> > mImageList;
};
TestImageProvider gTestImageProvider;

void init_llui()
{
    // Font lookup needs directory support
    const char* newview_path = LLUI_LIBTEST_APP_RO_DIR;
    gDirUtilp->initAppDirs("SecondLife", newview_path);
    gDirUtilp->setSkinFolder("default", "en");

    // colors are no longer stored in a LLControlGroup file
    LLUIColorTable::instance().loadFromSettings();

    std::string config_filename = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "settings.xml");
    // This is the default settings file, so load it as defaults the way
    // LLAppViewer does. Loading it as user settings marks every control
    // PERSIST_ALWAYS, and the first one declared without a comment is fatal.
    gSavedSettings.loadFromFile(config_filename, true);

    // See LLAppViewer::init()
    LLUI::settings_map_t settings;
    settings["config"] = &gSavedSettings;
    settings["ignores"] = &gWarningSettings;
    settings["floater"] = &gSavedSettings;
    settings["account"] = &gSavedPerAccountSettings;

    // Don't use real images as we don't have a GL context. LLUI's constructor
    // stands up LLRender2D with the same provider, and there is no audio here.
    LLUI::createInstance(settings, &gTestImageProvider, nullptr, nullptr);

    // Each widget's own static registrar already runs, so registering here as
    // well trips the duplicate-key check. This is only needed where the
    // linker has dropped those translation units.
    const bool register_widgets = false;
    LLWidgetReg::initClass( register_widgets );

    // Otherwise we get translation warnings when setting up floaters
    // (tooltips for buttons)
    std::set<std::string> default_args;
    LLTransUtil::parseStrings("strings.xml", default_args);
    LLTransUtil::parseLanguageStrings("language_settings.xml");
    LLFontManager::initClass();

    // Creating widgets apparently requires fonts to be initialized,
    // otherwise it crashes.
    LLFontGL::initClass(96.f, 1.f, 1.f,
                        gDirUtilp->getAppRODataDir(),
                        LLSD(),     // no font overrides in libtest
                        false );    // don't create gl textures

    LLFloaterView::Params fvparams;
    fvparams.name("Floater View");
    fvparams.rect( LLRect(0,480,640,0) );
    fvparams.mouse_opaque(false);
    fvparams.follows.flags(FOLLOWS_ALL);
    fvparams.tab_stop(false);
    gFloaterView = LLUICtrlFactory::create<LLFloaterView> (fvparams);
}

int main(int argc, char** argv)
{
    // Must init LLError for llerrs to actually cause errors.
    LLError::initForApplication(".", ".");
    // Loading settings logs a line per unrecognized variable, which buries
    // anything this program actually prints.
    LLError::setDefaultLevel(LLError::LEVEL_WARN);

    init_llui();

    // All of these are off by default so the usual run stays silent.
    //
    // "--params" fingerprints every widget's parameter block. Reworking
    // LLInitParam must leave this byte-identical, so it is the gate.
    //
    // "--census" reports what the parameter tables cost, "--sizes" what one
    // instance of each block costs, and "--bench" what building one costs.
    // All three are *expected* to move -- shrinking them is the point --
    // which is why none of them shares a stream with the gate above.
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if (arg == "--params")
        {
            ALParamFingerprint::collect(std::cout);
        }
        else if (arg == "--sizes")
        {
            std::cout << ALParamFingerprint::sizes();
        }
        else if (arg == "--census")
        {
            std::cout << ALParamFingerprint::census();
        }
        else if (arg == "--bench")
        {
            std::cout << ALParamFingerprint::bench();
        }
    }

    return 0;
}
