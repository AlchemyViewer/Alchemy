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
#include "alscriptstudio_fixture.h"
#include "fsyspath.h"

#include "../test/lltut.h"

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
            // The tab's own text on the right, as the studio puts it.
            compare(doc, theirs, doc.editor->wholeText(), their_title, own_title);
        }
        void pick(std::vector<ALQuickOpen::Candidate> given, const std::string&, const std::string& title,
                  std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)>) override
        {
            candidates = std::move(given);
            picked     = title;
            choose     = std::move(chosen);
        }
        void refreshNotice() override { ++notices; }

        std::vector<ALQuickOpen::Candidate>      candidates;
        std::string                              picked;
        std::function<void(const std::string&)> choose;
        Names                                    did;
        S32                                      notices = 0;
    };
}

namespace tut
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
        void saved(const Doc& doc, const std::string& text, F64 ago, const LLUUID& asset = LLUUID::generateNewID())
        {
            ALSavedText one;
            one.key   = ALScriptStudioHistory::keyOf(doc);
            one.text  = text;
            one.when  = LLDate(LLDate::now().secondsSinceEpoch() - ago);
            one.asset = asset;
            ensure("kept", history->keep(one));
        }
    };

    typedef test_group<alscriptstudiohistory_data> alscriptstudiohistory_group;
    typedef alscriptstudiohistory_group::object    alscriptstudiohistory_object;
    tut::alscriptstudiohistory_group               alscriptstudiohistory_test("alscriptstudiohistory");

    template<> template<>
    void alscriptstudiohistory_object::test<1>()
    {
        set_test_name("an item's saves are kept by the item, in its object or not; a file's are not kept here");
        Doc doc;
        doc.ref = ALScriptRef(LLUUID::null, LLUUID::generateNewID());
        ensure_equals("an inventory item's", ALScriptStudioHistory::keyOf(doc), ALRecoveryStore::keyOf(LLUUID::null, doc.ref.item, std::string()));
        doc.ref = ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID());
        ensure_equals("an object's", ALScriptStudioHistory::keyOf(doc), ALRecoveryStore::keyOf(doc.ref.object, doc.ref.item, std::string()));
        doc.file = "/somewhere/door.lsl";
        ensure("none for a file", ALScriptStudioHistory::keyOf(doc).empty());
    }

    template<> template<>
    void alscriptstudiohistory_object::test<2>()
    {
        set_test_name("a tab's saves listed newest first: when, how long, against the one before, and which is saved now; none said so");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "now");
        unit.show(doc);
        ensure("nothing to list", studio.candidates.empty() && !services().statuses.empty());
        LLStringUtil::format_map_t args;
        args["[NAME]"] = "door";
        ensure_equals("said so", services().statuses.back(), said("HistoryNone", args));

        const LLUUID current = LLUUID::generateNewID();
        saved(doc, "12345", 300.0);
        saved(doc, "1234567", 200.0);
        saved(doc, "123", 100.0, current);
        doc.assetId = current;
        unit.show(doc);
        ensure_equals("three", studio.candidates.size(), 3U);
        ensure_equals("titled", studio.picked, said("HistoryTitle", args));
        const std::vector<ALSavedText> listed = history->list(ALScriptStudioHistory::keyOf(doc));
        ensure_equals("labelled by when", studio.candidates[0].label, ALRecoveryEntry::sayWhen(listed[0].when));
        const std::string& newest = studio.candidates[0].detail;
        ensure("its length" + newest, newest.find(services().counted("HistoryBytes", 3)) != std::string::npos);
        ensure("shorter than the one before" + newest, newest.find(services().counted("HistoryShorter", 4)) != std::string::npos);
        ensure("saved now" + newest, newest.find(said("HistoryCurrent")) != std::string::npos);
        const std::string& middle = studio.candidates[1].detail;
        ensure("longer" + middle, middle.find(services().counted("HistoryLonger", 2)) != std::string::npos);
        ensure("not saved now" + middle, middle.find(said("HistoryCurrent")) == std::string::npos);
        ensure("the first kept says nothing of one before", studio.candidates[2].detail.find(said("HistorySameLength")) == std::string::npos &&
                                                               studio.candidates[2].detail.find("longer") == std::string::npos);
    }

    template<> template<>
    void alscriptstudiohistory_object::test<3>()
    {
        set_test_name("a save chosen is compared with the tab now, and offered back; before the tab has loaded, once it has");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "the text now");
        saved(doc, "the text then", 100.0);
        unit.show(doc);
        studio.choose(studio.candidates[0].value);
        ensure_equals("compared", studio.did.size(), 1U);
        LLStringUtil::format_map_t when;
        when["[WHEN]"] = studio.candidates[0].label;
        when["[NAME]"] = "door";
        ensure_equals("then beside now", studio.did[0],
                      "compare door: the text then | the text now (" + said("HistorySavedAt", when) + " | " + said("CompareNow") + ")");
        ensure("held to offer back", doc.historyShown && doc.historyShown->text == "the text then");
        ensure("the notice said again", studio.notices == 1);

        Doc& loading = tab("lamp", "", false);
        saved(loading, "a lamp's text", 50.0);
        unit.show(loading);
        studio.choose(studio.candidates[0].value);
        ensure_equals("not compared yet", studio.did.size(), 1U);
        ensure("once it has loaded", loading.pendingCompare && loading.pendingCompare->text == "a lamp's text");
    }

    template<> template<>
    void alscriptstudiohistory_object::test<4>()
    {
        set_test_name("the notice offers a save back where the tab may be changed; restored as carried text, the comparison ended");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "now");
        ALSavedText shown;
        shown.text       = "then";
        shown.when       = LLDate(1.8e9);
        doc.historyShown = shown;
        LLStringUtil::format_map_t args;
        args["[WHEN]"]                         = ALRecoveryEntry::sayWhen(shown.when);
        const ALScriptNoticeBar::Notice notice = ALScriptStudioOrphans::noticeFor(&doc, services());
        ensure_equals("said", notice.text, said("NoticeHistory", args));
        ensure_equals("offered back", notice.buttons[0].first, std::string("restore_saved"));
        doc.modifiable = false;
        ensure("not where it may not be changed", ALScriptStudioOrphans::noticeFor(&doc, services()).buttons[0].first.empty());
    }

    template<> template<>
    void alscriptstudiohistory_object::test<5>()
    {
        set_test_name("a list of saves in a window's own words: each asked by name, with a count where it has one, and joined as it joins");
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
        ensure_equals("each", listed.size(), 3U);
        ensure_equals("the newest", listed[0].detail, std::string("HistoryBytes(10) + HistorySameLength + HistoryCurrent"));
        ensure_equals("shorter than the first", listed[1].detail, std::string("HistoryBytes(10) + HistoryShorter(2)"));
        ensure_equals("the first says only its length", listed[2].detail, std::string("HistoryBytes(12)"));
        ensure_equals("by when", listed[2].label, ALRecoveryEntry::sayWhen(saves[2].when));
        ensure_equals("valued by place", listed[1].value, std::string("1"));
    }
}
