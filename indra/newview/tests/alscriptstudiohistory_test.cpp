/**
 * @file alscriptstudiohistory_test.cpp
 * @brief Tests for ALScriptStudioHistory: a tab's saves listed, compared and offered back.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
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

#include "linden_common.h"

#include "../alscriptstudiohistory.h"

#include "../alrecovery.h"
#include "../alscriptstudioorphans.h"
#include "aldiffbar.h"
#include "aldiffview.h"
#include "alflatbutton.h"
#include "alscriptstudio_fixture.h"
#include "fsyspath.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;

    // The history's window, faked: the list it was given, to choose from,
    // and a record of what it was asked.
    struct FakeHistoryWindow : public ALScriptStudioHistory::Window
    {
        void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                     const std::string& right_title) override
        {
            did.push_back("compare " + doc.id + ": " + left + " | " + right + " (" + left_title + " | " + right_title + ")");
        }
        void compareWithTab(Doc& doc, const std::string& theirs, const std::string& their_title, const std::string& own_title,
                            const ALTextDiff::ranges_t&) override
        {
            // The tab's own text on the right, as the studio puts it; and
            // where the test gives it somewhere, in a comparison of the
            // view's own that follows the tab.
            compare(doc, theirs, doc.editor->wholeText(), their_title, own_title);
            if (host)
            {
                if (!doc.compareView)
                {
                    ALDiffView::Params p(LLUICtrlFactory::getDefaultParams<ALDiffView>());
                    p.name          = "compare_" + doc.id;
                    p.rect          = LLRect(0, 300, 600, 0);
                    doc.compareView = LLUICtrlFactory::create<ALDiffView>(p);
                    host->addChild(doc.compareView);
                }
                doc.compareView->setTexts(theirs, doc.editor->wholeText());
                doc.compareTitles = Doc::CompareTitles{ their_title, own_title };
            }
        }
        void retitleCompare(const Doc& doc) const override { retitled.push_back(doc.compareTitles ? doc.compareTitles->theirs : std::string()); }
        void pick(std::vector<ALQuickOpen::Candidate> given, const std::string&, const std::string& title,
                  std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)>) override
        {
            candidates = std::move(given);
            picked     = title;
            choose     = std::move(chosen);
        }
        void refreshNotice() override { ++notices; }

        LLView*                                  host = nullptr;
        mutable Names                            retitled;
        std::vector<ALQuickOpen::Candidate>      candidates;
        std::string                              picked;
        std::function<void(const std::string&)> choose;
        Names                                    did;
        S32                                      notices = 0;
    };
}

namespace
{
    struct alscriptstudiohistory_data
    {
        al_studio_test::StudioWindow         window;
        FakeHistoryWindow                    studio;
        ALScriptStudioHistory                unit{ window.services(), studio };
        std::string                          folder;
        std::shared_ptr<ALSaveHistory>       history;

        alscriptstudiohistory_data()
        {
            folder  = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alscripthistory_" + LLUUID::generateNewID().asString())).string();
            history = std::make_shared<ALSaveHistory>(folder);
            ALRecovery::useHistory(history);
        }
        ~alscriptstudiohistory_data()
        {
            ALRecovery::useHistory(nullptr);
            std::error_code ec;
            std::filesystem::remove_all(fsyspath(folder), ec);
        }

        al_studio_test::FakeServices& services() { return window.services(); }
        std::string                   said(const std::string& name, const LLStringUtil::format_map_t& args = {})
        {
            return services().words(name, args);
        }

        // A tab of a script in an object, loaded and changeable, holding a
        // text; with an editor where there is UI to make one.
        Doc& tab(const std::string& id, const std::string& text, bool loaded = true)
        {
            Doc& doc      = services().addDoc(id, ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), id);
            doc.loaded     = loaded;
            doc.modifiable = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name     = "editor_" + id;
            p.rect     = LLRect(0, 200, 400, 0);
            p.syntax   = "lsl";
            doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(doc.editor);
            doc.editor->setText(text);
            return doc;
        }

        // A save of a tab's item, so long ago.
        void require_saved(const Doc& doc, const std::string& text, F64 ago, const LLUUID& asset = LLUUID::generateNewID())
        {
            ALSavedText one;
            one.key   = ALScriptStudioHistory::keyOf(doc);
            one.text  = text;
            one.when  = LLDate(LLDate::now().secondsSinceEpoch() - ago);
            one.asset = asset;
            INFO("kept in the history");
            REQUIRE(history->keep(one));
        }
    };

    TEST_CASE_METHOD(alscriptstudiohistory_data, "alscriptstudiohistory: keyOf keys saves by item and object and gives a file no key", "[alscriptstudiohistory]")
    {
        Doc doc;
        doc.ref = ALScriptRef(LLUUID::null, LLUUID::generateNewID());
        UNSCOPED_INFO("an inventory item's");
        CHECK(ALScriptStudioHistory::keyOf(doc) == ALRecoveryStore::keyOf(LLUUID::null, doc.ref.item, std::string()));
        doc.ref = ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID());
        UNSCOPED_INFO("an object's");
        CHECK(ALScriptStudioHistory::keyOf(doc) == ALRecoveryStore::keyOf(doc.ref.object, doc.ref.item, std::string()));
        doc.file = "/somewhere/door.lsl";
        UNSCOPED_INFO("none for a file");
        REQUIRE(ALScriptStudioHistory::keyOf(doc).empty());
    }

    TEST_CASE_METHOD(alscriptstudiohistory_data, "alscriptstudiohistory: a tab's saves list newest first with their sizes or say there are none", "[alscriptstudiohistory]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "now");
        unit.show(doc);
        {
            INFO("nothing to list");
            REQUIRE(studio.candidates.empty());
            REQUIRE_FALSE(services().statuses.empty());
        }
        LLStringUtil::format_map_t args;
        args["[NAME]"] = "door";
        UNSCOPED_INFO("said so");
        CHECK(services().statuses.back() == said("HistoryNone", args));

        const LLUUID current = LLUUID::generateNewID();
        require_saved(doc, "12345", 300.0);
        require_saved(doc, "1234567", 200.0);
        require_saved(doc, "123", 100.0, current);
        doc.assetId = current;
        unit.show(doc);
        REQUIRE(studio.candidates.size() == 3U);
        CHECK(studio.picked == said("HistoryTitle", args));
        const std::vector<ALSavedText> listed = history->list(ALScriptStudioHistory::keyOf(doc));
        REQUIRE(listed.size() == 3U);
        CHECK(studio.candidates[0].label == ALRecoveryEntry::sayWhen(listed[0].when));
        const std::string& newest = studio.candidates[0].detail;
        {
            CAPTURE(newest);
            REQUIRE(newest.find(services().counted("HistoryBytes", 3)) != std::string::npos);
            REQUIRE(newest.find(services().counted("HistoryShorter", 4)) != std::string::npos);
            REQUIRE(newest.find(said("HistoryCurrent")) != std::string::npos);
        }
        const std::string& middle = studio.candidates[1].detail;
        {
            CAPTURE(middle);
            REQUIRE(middle.find(services().counted("HistoryLonger", 2)) != std::string::npos);
            REQUIRE(middle.find(said("HistoryCurrent")) == std::string::npos);
        }
        {
            INFO("the first kept says nothing of one before");
            CHECK(studio.candidates[2].detail.find(said("HistorySameLength")) == std::string::npos);
            CHECK(studio.candidates[2].detail.find("longer") == std::string::npos);
            CHECK(studio.candidates[2].detail.find("shorter") == std::string::npos);
        }
    }

    TEST_CASE_METHOD(alscriptstudiohistory_data, "alscriptstudiohistory: a chosen save is compared with the tab now or once the tab has loaded", "[alscriptstudiohistory]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "the text now");
        require_saved(doc, "the text then", 100.0);
        unit.show(doc);
        REQUIRE(studio.candidates.size() == 1U);
        studio.choose(studio.candidates[0].value);
        UNSCOPED_INFO("compared");
        REQUIRE(studio.did.size() == 1U);
        LLStringUtil::format_map_t when;
        when["[WHEN]"] = studio.candidates[0].label;
        when["[NAME]"] = "door";
        UNSCOPED_INFO("then beside now");
        CHECK(studio.did[0] ==
                      "compare door: the text then | the text now (" + said("HistorySavedAt", when) + " | " + said("CompareNow") + ")");
        {
            INFO("held to offer back");
            REQUIRE(doc.historyShown);
            CHECK(doc.historyShown->text == "the text then");
        }
        UNSCOPED_INFO("the notice said again");
        CHECK(studio.notices == 1);

        Doc& loading = tab("lamp", "", false);
        require_saved(loading, "a lamp's text", 50.0);
        unit.show(loading);
        REQUIRE(studio.candidates.size() == 1U);
        studio.choose(studio.candidates[0].value);
        UNSCOPED_INFO("not compared yet");
        REQUIRE(studio.did.size() == 1U);
        {
            INFO("once it has loaded");
            REQUIRE(loading.pendingCompare);
            CHECK(loading.pendingCompare->text == "a lamp's text");
        }
    }

    TEST_CASE_METHOD(alscriptstudiohistory_data, "alscriptstudiohistory: the notice offers a save back only where the tab may be changed", "[alscriptstudiohistory]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "now");
        ALSavedText shown;
        shown.text       = "then";
        shown.when       = LLDate(1.8e9);
        doc.historyShown = shown;
        LLStringUtil::format_map_t args;
        args["[WHEN]"]                         = ALRecoveryEntry::sayWhen(shown.when);
        const ALScriptNoticeBar::Notice notice = ALScriptStudioOrphans::noticeFor(&doc, services());
        CHECK(notice.text == said("NoticeHistory", args));
        UNSCOPED_INFO("offered back");
        CHECK(notice.buttons[0].first == std::string("restore_saved"));
        doc.modifiable = false;
        UNSCOPED_INFO("not where it may not be changed");
        REQUIRE(ALScriptStudioOrphans::noticeFor(&doc, services()).buttons[0].first.empty());
    }

    TEST_CASE_METHOD(alscriptstudiohistory_data, "alscriptstudiohistory: candidatesOf describes saves with the words and joiner it is given", "[alscriptstudiohistory]")
    {
        std::vector<ALSavedText> saves(3);
        saves[0].bytes = 10;
        saves[0].asset = LLUUID::generateNewID();
        saves[0].when  = LLDate(1.8e9 + 60.0);
        saves[1].bytes = 10;
        saves[1].when  = LLDate(1.8e9 + 30.0);
        saves[2].bytes = 12;
        saves[2].when  = LLDate(1.8e9);
        const auto words = [](const char* name, std::optional<S32> count) {
            return std::string(name) + (count ? "(" + std::to_string(*count) + ")" : std::string());
        };
        const auto joined = [](const std::vector<std::string>& items) {
            std::string out;
            for (const std::string& item : items)
            {
                out += (out.empty() ? "" : " + ") + item;
            }
            return out;
        };
        const std::vector<ALQuickOpen::Candidate> listed = ALScriptStudioHistory::candidatesOf(saves, saves[0].asset, words, joined);
        REQUIRE(listed.size() == 3U);
        UNSCOPED_INFO("the newest");
        CHECK(listed[0].detail == std::string("HistoryBytes(10) + HistorySameLength + HistoryCurrent"));
        UNSCOPED_INFO("shorter than the first");
        CHECK(listed[1].detail == std::string("HistoryBytes(10) + HistoryShorter(2)"));
        UNSCOPED_INFO("the first says only its length");
        CHECK(listed[2].detail == std::string("HistoryBytes(12)"));
        CHECK(listed[2].label == ALRecoveryEntry::sayWhen(saves[2].when));
        UNSCOPED_INFO("valued by place");
        CHECK(listed[1].value == std::string("1"));
    }

    TEST_CASE_METHOD(alscriptstudiohistory_data, "alscriptstudiohistory: a compared save gets a slider that steps the left through the saves", "[alscriptstudiohistory]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        studio.host = window.floater;
        Doc& doc    = tab("door", "now\nsame");
        require_saved(doc, "first\nsame", 300.0);
        require_saved(doc, "second\nsame", 200.0);
        require_saved(doc, "third\nsame", 100.0);
        unit.show(doc);
        REQUIRE(studio.candidates.size() == 3U);
        // Listed newest first: the middle one.
        studio.choose(studio.candidates[1].value);
        REQUIRE(doc.compareView);
        ALDiffView& view = *doc.compareView;
        UNSCOPED_INFO("the slider");
        CHECK(view.bar()->getChild<LLView>("versions")->getVisible());
        UNSCOPED_INFO("at the middle of three, oldest first");
        CHECK(view.bar()->versionShown() == 1);
        CHECK(view.leftText() == std::string("second\nsame"));

        const S32 notices = studio.notices;
        ALViewType::as<ALFlatButton>(view.bar()->getChild<LLView>("newer"))->press();
        UNSCOPED_INFO("the newest on the left");
        CHECK(view.leftText() == std::string("third\nsame"));
        {
            INFO("held to offer back");
            REQUIRE(doc.historyShown);
            CHECK(doc.historyShown->text == "third\nsame");
        }
        const std::vector<ALSavedText> listed = history->list(ALScriptStudioHistory::keyOf(doc));
        REQUIRE(listed.size() == 3U);
        LLStringUtil::format_map_t     when;
        when["[NAME]"] = "door";
        when["[WHEN]"] = ALRecoveryEntry::sayWhen(listed[0].when);
        REQUIRE(doc.compareTitles);
        UNSCOPED_INFO("titled by it");
        CHECK(doc.compareTitles->theirs == said("HistorySavedAt", when));
        {
            INFO("said again");
            REQUIRE_FALSE(studio.retitled.empty());
            CHECK(studio.retitled.back() == said("HistorySavedAt", when));
            CHECK(studio.notices == notices + 1);
        }
        UNSCOPED_INFO("the right still the tab");
        CHECK(view.rightText() == std::string("now\nsame"));

        ALViewType::as<ALFlatButton>(view.bar()->getChild<LLView>("older"))->press();
        ALViewType::as<ALFlatButton>(view.bar()->getChild<LLView>("older"))->press();
        {
            INFO("the oldest");
            CHECK(view.leftText() == "first\nsame");
            CHECK(doc.historyShown->text == "first\nsame");
            CHECK(view.bar()->versionShown() == 0);
        }
    }

    TEST_CASE_METHOD(alscriptstudiohistory_data, "alscriptstudiohistory: a save chosen before load gets its slider when shown and a lone one none", "[alscriptstudiohistory]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        studio.host  = window.floater;
        Doc& loading = tab("lamp", "", false);
        require_saved(loading, "one", 200.0);
        require_saved(loading, "two", 100.0);
        unit.show(loading);
        REQUIRE(studio.candidates.size() == 2U);
        studio.choose(studio.candidates[0].value);
        {
            INFO("waiting, with something to do once shown");
            REQUIRE(loading.pendingCompare);
            REQUIRE(loading.pendingCompare->shown);
        }
        // As the window does once the tab has loaded.
        loading.loaded  = true;
        loading.editor->setText("two");
        const Doc::PendingCompare pending = *loading.pendingCompare;
        loading.pendingCompare.reset();
        studio.compareWithTab(loading, pending.text, pending.theirTitle, pending.ownTitle, pending.ranges);
        pending.shown(loading);
        {
            INFO("the slider, at the newest");
            CHECK(loading.compareView->bar()->getChild<LLView>("versions")->getVisible());
            CHECK(loading.compareView->bar()->versionShown() == 1);
        }

        Doc& alone = tab("gate", "now");
        require_saved(alone, "only", 100.0);
        unit.show(alone);
        REQUIRE(studio.candidates.size() == 1U);
        studio.choose(studio.candidates[0].value);
        {
            INFO("one save: no slider");
            REQUIRE(alone.compareView);
            CHECK_FALSE(alone.compareView->bar()->getChild<LLView>("versions")->getVisible());
        }
    }

    TEST_CASE_METHOD(alscriptstudiohistory_data, "alscriptstudiohistory: stepping to an unreadable save says so and puts the slider back", "[alscriptstudiohistory]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        studio.host = window.floater;
        Doc& doc    = tab("door", "now");
        require_saved(doc, "older", 200.0);
        require_saved(doc, "newer", 100.0);
        unit.show(doc);
        REQUIRE(studio.candidates.size() == 2U);
        studio.choose(studio.candidates[0].value);
        REQUIRE(doc.compareView);
        ALDiffView& view = *doc.compareView;
        UNSCOPED_INFO("at the newest");
        CHECK(view.bar()->versionShown() == 1);
        // The older one gone from disk since it was listed.
        const std::vector<ALSavedText> listed = history->list(ALScriptStudioHistory::keyOf(doc));
        REQUIRE(listed.size() == 2U);
        std::error_code                gone;
        std::filesystem::remove(fsyspath(listed[1].path), gone);
        ALViewType::as<ALFlatButton>(view.bar()->getChild<LLView>("older"))->press();
        LLStringUtil::format_map_t args;
        args["[NAME]"] = "door";
        args["[WHEN]"] = ALRecoveryEntry::sayWhen(listed[1].when);
        REQUIRE_FALSE(services().statuses.empty());
        CHECK(services().statuses.back() == said("HistoryUnreadable", args));
        UNSCOPED_INFO("the slider put back");
        CHECK(view.bar()->versionShown() == 1);
        {
            INFO("the left as it was");
            CHECK(view.leftText() == "newer");
            REQUIRE(doc.historyShown);
            CHECK(doc.historyShown->text == "newer");
        }
    }

    TEST_CASE_METHOD(alscriptstudiohistory_data, "alscriptstudiohistory: a save stepped to is read once and kept after its file is gone", "[alscriptstudiohistory]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        studio.host = window.floater;
        Doc& doc    = tab("door", "now");
        require_saved(doc, "first", 300.0);
        require_saved(doc, "second", 200.0);
        require_saved(doc, "third", 100.0);
        unit.show(doc);
        REQUIRE(studio.candidates.size() == 3U);
        studio.choose(studio.candidates[1].value);
        REQUIRE(doc.compareView);
        ALDiffView&  view  = *doc.compareView;
        const auto   press = [&view](const char* name) { ALViewType::as<ALFlatButton>(view.bar()->getChild<LLView>(name))->press(); };
        press("older");
        UNSCOPED_INFO("the oldest");
        CHECK(view.leftText() == std::string("first"));
        press("newer");
        const std::vector<ALSavedText> listed = history->list(ALScriptStudioHistory::keyOf(doc));
        REQUIRE(listed.size() == 3U);
        std::error_code                gone;
        std::filesystem::remove(fsyspath(listed[2].path), gone);
        press("older");
        {
            INFO("the oldest again, as it was read");
            CHECK(view.leftText() == "first");
            CHECK(view.bar()->versionShown() == 0);
            REQUIRE(doc.historyShown);
            CHECK(doc.historyShown->text == "first");
        }
    }

    TEST_CASE_METHOD(alscriptstudiohistory_data, "alscriptstudiohistory: letting go of a save as its comparison is left removes its slider", "[alscriptstudiohistory]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        studio.host = window.floater;
        Doc& doc    = tab("door", "now");
        require_saved(doc, "first", 300.0);
        require_saved(doc, "second", 200.0);
        unit.show(doc);
        REQUIRE(studio.candidates.size() == 2U);
        studio.choose(studio.candidates[0].value);
        REQUIRE(doc.compareView);
        ALDiffView& view = *doc.compareView;
        {
            INFO("the slider");
            CHECK(view.bar()->getChild<LLView>("versions")->getVisible());
            CHECK(view.bar()->versionCount() == 2);
        }
        // As leaving the comparison does: Escape, or a keystroke typed into
        // the source.
        ALScriptStudioHistory::letGo(doc);
        UNSCOPED_INFO("let go of");
        REQUIRE_FALSE(doc.historyShown);
        {
            INFO("and its slider");
            REQUIRE(view.bar()->versionCount() == 0);
            CHECK_FALSE(view.bar()->getChild<LLView>("versions")->getVisible());
        }
        ALViewType::as<ALFlatButton>(view.bar()->getChild<LLView>("older"))->press();
        UNSCOPED_INFO("nothing stepped");
        CHECK(view.leftText() == std::string("second"));
    }
}
