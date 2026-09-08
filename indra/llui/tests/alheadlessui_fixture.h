/**
 * @file alheadlessui_fixture.h
 * @brief A UI for tests to build widgets in, with no window behind it
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#ifndef AL_ALHEADLESSUI_FIXTURE_H
#define AL_ALHEADLESSUI_FIXTURE_H

#include "linden_common.h"

#include "../llaccordionctrl.h"
#include "../lllayoutstack.h"
#include "../llui.h"

#include "llcontrol.h"
#include "lldir.h"
#include "llfontfreetype.h"
#include "llfloater.h"
#include "llfontgl.h"

#include <cstdio>
#include <vector>

#ifndef LLUI_TEST_APP_DIR
#  define LLUI_TEST_APP_DIR ""
#endif

namespace ll_test
{
    // The directory object the UI resolves app_settings and skins through,
    // pointed at the source tree. The platform LLDir's initAppDirs creates
    // directories under the user's profile; this one creates nothing.
    class UIDir : public LLDir
    {
    public:
        explicit UIDir(const std::string& app_dir)
        {
            mAppName = "llui_test";
            mAppRODataDir = app_dir;
            mSkinBaseDir = add(mAppRODataDir, "skins");
            setSkinFolder("default", "en");
        }

        void initAppDirs(const std::string&, const std::string&) override {}
        std::string getCurPath() override { return mWorkingDir; }
        std::string getLLPluginFilename(std::string) override { return std::string(); }
    };

    // What a widget above LLView needs before it can be constructed: settings
    // groups, where a key nobody declared warns and reads as its default; a
    // 2D renderer with no image provider, so every image is null; and the
    // font registry over the source tree's fonts, with no GL textures.
    //
    // One per process. LLFontGL's per-face getters cache their answer in a
    // static, and the LLUI singleton owns spell-check and 2D state that is
    // not built to be torn down and stood up again.
    class HeadlessUI
    {
    public:
        static HeadlessUI& get()
        {
            static HeadlessUI ui;
            return ui;
        }

        // False when the source tree is not where the build said it was. A
        // test that needs the UI skips rather than fails on that.
        bool ok() const { return mOk; }

        HeadlessUI(const HeadlessUI&) = delete;
        HeadlessUI& operator=(const HeadlessUI&) = delete;

    private:
        HeadlessUI()
        :   mDir(LLUI_TEST_APP_DIR),
            mConfig("config")
        {
            const std::string fonts_xml = mDir.add(std::string(LLUI_TEST_APP_DIR), "skins/default/xui/en/fonts.xml");
            if (std::string(LLUI_TEST_APP_DIR).empty() || !fileExists(fonts_xml))
            {
                return;
            }

            gDirUtilp = &mDir;

            mConfig.declareS32("UIResizeBarHeight", 6, "Read by LLLayoutStack::Params");
            mConfig.declareS32("UIScrollbarSize", 15, "Read by scrolling widgets' params");

            LLUI::settings_map_t settings;
            settings["config"] = &mConfig;
            settings["floater"] = &mConfig;
            settings["account"] = &mConfig;
            settings["ignores"] = &mConfig;
            LLUI::createInstance(settings, nullptr, nullptr, nullptr);

            LLFontManager::initClass();
            LLFontGL::initClass(96.f, 1.f, 1.f, LLUI_TEST_APP_DIR, fonts_xml, LLSD(),
                                /*create_gl_textures=*/false);

            // A floater reaches for the floater view while it is being built
            // -- to fit itself on a screen this has none of -- and reads its
            // rect for the snap. One here, the size of a plausible window,
            // is what lets a test build a floater at all.
            LLFloaterView::Params fvp;
            fvp.name = "test_floater_view";
            fvp.rect = LLRect(0, 1080, 1920, 0);
            fvp.mouse_opaque = false;
            fvp.follows.flags = FOLLOWS_ALL;
            gFloaterView = LLUICtrlFactory::create<LLFloaterView>(fvp);

            mOk = true;
        }

        static bool fileExists(const std::string& path)
        {
            if (FILE* f = std::fopen(path.c_str(), "rb"))
            {
                std::fclose(f);
                return true;
            }
            return false;
        }

        UIDir mDir;
        LLControlGroup mConfig;
        bool mOk { false };
    };

    // What a frame does to a layout, run until it stops changing.
    //
    // A layout stack lays its panels out on the frame AFTER it is told to,
    // and laying one out tells the stacks around it -- so one pass measures
    // what the file said, and a second measures something halfway between
    // that and the answer. The viewer converges because it draws sixty
    // frames a second and nobody watches the first three; a test has to say
    // so out loud, and has to know whether it got there.
    //
    // Returns false if it did not settle, which is itself worth failing on:
    // a layout that never stops moving is one the viewer redraws for ever.
    inline bool settle(LLView* root, S32 passes = 16)
    {
        const auto snapshot = [](LLView* view, std::vector<LLRect>& out, auto&& self) -> void
        {
            out.push_back(view->getRect());
            for (LLView* child : *view->getChildList())
            {
                self(child, out, self);
            }
        };

        std::vector<LLRect> before;
        std::vector<LLRect> after;
        snapshot(root, before, snapshot);
        for (S32 pass = 0; pass < passes; ++pass)
        {
            LLAccordionCtrl::updateClass();
            LLLayoutStack::updateClass();
            after.clear();
            snapshot(root, after, snapshot);
            if (after == before)
            {
                return true;
            }
            before.swap(after);
        }
        return false;
    }
}

#endif // AL_ALHEADLESSUI_FIXTURE_H
