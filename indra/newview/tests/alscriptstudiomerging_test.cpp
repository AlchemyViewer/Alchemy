/**
 * @file alscriptstudiomerging_test.cpp
 * @brief Tests for ALScriptStudioMerging: a save that came up against another, merged and settled.
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

#include "../alscriptstudiomerging.h"
#include "../alscriptstudioorphans.h"

#include "aldiffbar.h"
#include "aldiffview.h"
#include "alflatbutton.h"
#include "alscriptstudio_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace
{
    typedef ALScriptStudioDoc Doc;

    // The merges' window, faked: a comparison of the view's own made in
    // the tab's place, as the studio makes one -- its right the tab's text,
    // a change taken back an edit of the tab where it may be changed --
    // and the world's text answered when the test says.
    struct FakeMergingWindow : public ALScriptStudioMerging::Window
    {
        explicit FakeMergingWindow(LLView* host) : host(host) {}

        void compareWithTab(Doc& doc, const std::string& theirs, const std::string& their_title, const std::string&,
                            const ALTextDiff::ranges_t&) override
        {
            titles.push_back(their_title);
            if (!doc.compareView)
            {
                ALDiffView::Params p(LLUICtrlFactory::getDefaultParams<ALDiffView>());
                p.name          = "compare_" + doc.id;
                p.rect          = LLRect(0, 300, 600, 0);
                doc.compareView = LLUICtrlFactory::create<ALDiffView>(p);
                host->addChild(doc.compareView);
            }
            doc.compareView->setTexts(theirs, doc.editor->wholeText());
            doc.compareView->setOnTakeBack(nullptr);
            if (doc.modifiable)
            {
                ALCodeEditor* editor = doc.editor;
                doc.compareView->setOnTakeBack([editor](const ALTextRange& range, const std::string& text) { return editor->replaceAll({ { range, text } }); });
            }
        }
        void loadWorld(Doc& doc, std::function<void(Doc&, const std::string&, const LLUUID&)> loaded) override
        {
            worldAsked = doc.id;
            world      = std::move(loaded);
        }
        void refreshNotice() override { ++notices; }

        LLView*                                                          host = nullptr;
        std::vector<std::string>                                         titles;
        std::string                                                      worldAsked;
        std::function<void(Doc&, const std::string&, const LLUUID&)> world;
        S32                                                              notices = 0;
    };
}

namespace
{
    struct alscriptstudiomerging_data
    {
        al_studio_test::StudioWindow           window;
        FakeMergingWindow                      studio{ window.floater };
        std::unique_ptr<ALScriptStudioMerging> unit = std::make_unique<ALScriptStudioMerging>(window.services(), studio);

        al_studio_test::FakeServices& services() { return window.services(); }
        std::string                   said(const std::string& name, const LLStringUtil::format_map_t& args = {})
        {
            return services().words(name, args);
        }

        // The lines "line 0" on, so many, with some of them said otherwise.
        static std::string lines(S32 count, std::initializer_list<std::pair<S32, const char*>> changed = {})
        {
            std::string text;
            for (S32 n = 0; n < count; ++n)
            {
                std::string line = "line " + std::to_string(n);
                for (const auto& [at, line_said] : changed)
                {
                    if (at == n)
                    {
                        line = line_said;
                    }
                }
                text += (n ? "\n" : "") + line;
            }
            return text;
        }
        // What both were, what was saved elsewhere since -- lines 0 and 6
        // changed -- and what the tab has -- lines 2 and 6.
        const std::string base   = lines(8);
        const std::string theirs = lines(8, { { 0, "theirs 0" }, { 6, "theirs 6" } });
        const std::string ours   = lines(8, { { 2, "mine 2" }, { 6, "mine 6" } });

        // A tab of a script in an object, changeable, saved as the base and
        // changed since to ours.
        Doc& tab(const std::string& id)
        {
            Doc& doc       = services().addDoc(id, ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), id);
            doc.loaded     = true;
            doc.modifiable = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name     = "editor_" + id;
            p.rect     = LLRect(0, 200, 400, 0);
            p.syntax   = "lsl";
            doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(doc.editor);
            doc.editor->setText(base);
            doc.editor->resetDirty();
            doc.editor->selectAll();
            doc.editor->insertText(ours);
            return doc;
        }
        LLStringUtil::format_map_t named(const Doc& doc) { return { { "[NAME]", doc.name } }; }
    };

    TEST_CASE_METHOD(alscriptstudiomerging_data, "alscriptstudiomerging: mergeSaved puts in theirs as one undo step and leaves conflicts to settle", "[alscriptstudiomerging]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc       = tab("door");
        doc.savedThere = theirs;
        CHECK(ALScriptStudioMerging::canMerge(doc));
        unit->mergeSaved(doc);
        UNSCOPED_INFO("theirs's line put in, ours kept");
        CHECK(doc.editor->wholeText() == lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" } }));
        {
            INFO("compared under the saved elsewhere's title");
            REQUIRE(studio.titles.size() == 1);
            CHECK(studio.titles[0] == said("CompareSavedThere"));
        }
        REQUIRE(doc.compareView);
        CHECK(doc.compareView->merging());
        CHECK(doc.compareView->conflictCount() == 1);
        {
            INFO("take and keep done with");
            REQUIRE_FALSE(doc.savedThere);
            CHECK(studio.notices == 1);
        }
        REQUIRE_FALSE(services().reports.empty());
        CHECK(services().reports.back().text == services().counted("MergeConflicts", 1, named(doc)));

        // Settled from the comparison's bar: an edit of the tab.
        doc.compareView->right()->setFocus(true);
        doc.compareView->right()->goTo(ALTextPos(6, 0));
        ALFlatButton* keep_both = ALViewType::as<ALFlatButton>(doc.compareView->bar()->findChild<LLView>("keep_both"));
        REQUIRE(keep_both);
        keep_both->press();
        UNSCOPED_INFO("mine then theirs");
        CHECK(doc.editor->wholeText() ==
                      lines(9, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" }, { 7, "theirs 6" }, { 8, "line 7" } }));
        CHECK(doc.compareView->conflictCount() == 0);

        // Each a step to undo: the settling, then the merge.
        doc.editor->undo();
        doc.editor->undo();
        CHECK(doc.editor->wholeText() == ours);
    }

    TEST_CASE_METHOD(alscriptstudiomerging_data, "alscriptstudiomerging: with nothing both changed a merge is clean and said so", "[alscriptstudiomerging]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc       = tab("door");
        doc.savedThere = lines(8, { { 0, "theirs 0" } });
        unit->mergeSaved(doc);
        CHECK(doc.editor->wholeText() == lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" } }));
        REQUIRE(doc.compareView);
        REQUIRE(doc.compareView->merging());
        CHECK(doc.compareView->conflictCount() == 0);
        REQUIRE_FALSE(services().reports.empty());
        CHECK(services().reports.back().text == said("MergeClean", named(doc)));
    }

    TEST_CASE_METHOD(alscriptstudiomerging_data, "alscriptstudiomerging: with no base or a read-only tab the texts are compared rather than merged", "[alscriptstudiomerging]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc       = tab("door");
        doc.savedThere = theirs;
        doc.editor->undoJournal().markNeverSaved();
        unit->mergeSaved(doc);
        UNSCOPED_INFO("nothing put in");
        CHECK(doc.editor->wholeText() == ours);
        {
            INFO("compared, not merged");
            REQUIRE(doc.compareView);
            REQUIRE_FALSE(doc.compareView->merging());
            CHECK(doc.compareView->changeCount() > 0);
        }
        {
            INFO("take and keep still offered");
            REQUIRE(doc.savedThere);
            CHECK(studio.notices == 0);
        }
        REQUIRE_FALSE(services().statuses.empty());
        CHECK(services().statuses.back() == said("MergeNoBase", named(doc)));

        Doc& locked       = tab("lamp");
        locked.modifiable = false;
        locked.savedThere = theirs;
        CHECK_FALSE(ALScriptStudioMerging::canMerge(locked));
        unit->mergeSaved(locked);
        {
            INFO("compared as they are");
            REQUIRE(locked.editor->wholeText() == ours);
            REQUIRE(locked.compareView);
            CHECK_FALSE(locked.compareView->merging());
        }
        CHECK(services().statuses.back() == said("MergeReadOnly", named(locked)));
    }

    TEST_CASE_METHOD(alscriptstudiomerging_data, "alscriptstudiomerging: mergeWorld merges the world's text once loaded unless the unit has gone", "[alscriptstudiomerging]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc&         doc   = tab("door");
        const LLUUID was   = LLUUID::generateNewID();
        const LLUUID moved = LLUUID::generateNewID();
        doc.assetId        = was;
        unit->mergeWorld(doc);
        CHECK(studio.worldAsked == std::string("door"));
        UNSCOPED_INFO("nothing yet");
        CHECK(doc.editor->wholeText() == ours);
        studio.world(doc, theirs, moved);
        CHECK(doc.editor->wholeText() == lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" } }));
        {
            INFO("under the world's title, as a merge");
            REQUIRE(studio.titles.size() == 1);
            CHECK(studio.titles.back() == said("CompareWorld"));
            REQUIRE(doc.compareView);
            CHECK(doc.compareView->conflictCount() == 1);
        }
        UNSCOPED_INFO("made from what the world holds");
        CHECK(doc.assetId == moved);

        Doc& lamp   = tab("lamp");
        lamp.assetId = was;
        unit->mergeWorld(lamp);
        unit.reset();
        studio.world(lamp, theirs, moved);
        {
            INFO("nothing once the merges are gone");
            CHECK(lamp.editor->wholeText() == ours);
            CHECK(lamp.assetId == was);
        }
    }

    TEST_CASE_METHOD(alscriptstudiomerging_data, "alscriptstudiomerging: a merge undone before saving brings back the other version and its offer", "[alscriptstudiomerging]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc       = tab("door");
        doc.savedThere = theirs;
        unit->mergeSaved(doc);
        {
            INFO("set aside");
            REQUIRE_FALSE(doc.savedThere);
            REQUIRE(doc.merged);
        }
        REQUIRE_FALSE(services().reports.empty());
        UNSCOPED_INFO("the conflicts said with the comparison to show");
        CHECK(services().reports.back().actions == std::vector<std::string>{ "show_compare" });
        // Typed in, or any change not a step back: nothing.
        unit->textChanged(doc);
        {
            INFO("still merged");
            REQUIRE_FALSE(doc.savedThere);
            REQUIRE(doc.merged);
        }

        const S32 notices = studio.notices;
        doc.editor->undo();
        CHECK(doc.editor->wholeText() == ours);
        unit->textChanged(doc);
        {
            INFO("what was saved elsewhere back");
            REQUIRE(doc.savedThere);
            CHECK(*doc.savedThere == theirs);
            REQUIRE_FALSE(doc.merged);
        }
        CHECK(services().reports.back().text == said("MergeTakenBack", named(doc)));
        UNSCOPED_INFO("take, keep, merge and compare offered again");
        CHECK(services().reports.back().actions == std::vector<std::string>{ "take_saved", "keep_saved", "merge_saved", "compare_saved" });
        UNSCOPED_INFO("the notice said again");
        CHECK(studio.notices > notices);

        // Stepped forward: the merge is back in the text, the other version
        // still stands until it is merged again.
        doc.editor->redo();
        unit->textChanged(doc);
        {
            INFO("left so");
            REQUIRE(doc.savedThere);
            CHECK(*doc.savedThere == theirs);
            REQUIRE_FALSE(doc.merged);
        }
        unit->mergeSaved(doc);
        {
            INFO("merged again: set aside again");
            REQUIRE_FALSE(doc.savedThere);
            REQUIRE(doc.merged);
        }
    }

    TEST_CASE_METHOD(alscriptstudiomerging_data, "alscriptstudiomerging: a world merge undone puts back the old asset so a save stops for the world", "[alscriptstudiomerging]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc&         doc   = tab("door");
        const LLUUID was   = LLUUID::generateNewID();
        const LLUUID moved = LLUUID::generateNewID();
        doc.assetId        = was;
        unit->mergeWorld(doc);
        studio.world(doc, theirs, moved);
        {
            INFO("made from what the world holds");
            CHECK(doc.assetId == moved);
            REQUIRE(doc.merged);
        }
        doc.editor->undo();
        unit->textChanged(doc);
        {
            INFO("made from what it was again");
            CHECK(doc.assetId == was);
            REQUIRE_FALSE(doc.merged);
        }
        REQUIRE_FALSE(services().reports.empty());
        UNSCOPED_INFO("reload, merge and compare offered again");
        CHECK(services().reports.back().actions == std::vector<std::string>{ "reload_world", "merge_world", "compare_world" });
    }

    TEST_CASE_METHOD(alscriptstudiomerging_data, "alscriptstudiomerging: settle takes the conflict at the caret and the notice goes with the last", "[alscriptstudiomerging]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc       = tab("door");
        doc.savedThere = theirs;
        unit->mergeSaved(doc);
        // As the window keeps what was said, and shows the comparison.
        REQUIRE_FALSE(services().reports.empty());
        doc.offer = Doc::Offer{ services().reports.back().text, services().reports.back().actions };
        UNSCOPED_INFO("not while the comparison is not in front");
        CHECK_FALSE(ALScriptStudioMerging::canSettle(doc));
        doc.view = Doc::View::Compare;
        ALScriptNoticeBar::Notice notice = ALScriptStudioOrphans::noticeFor(&doc, services());
        {
            INFO("the notice offers the comparison");
            CHECK(notice.buttons[0].first == "show_compare");
            REQUIRE(notice.buttons[1].first.empty());
        }

        REQUIRE(doc.compareView);
        doc.compareView->right()->setFocus(true);
        doc.compareView->right()->goTo(ALTextPos(2, 0));
        UNSCOPED_INFO("not where the caret is in no conflict");
        CHECK_FALSE(ALScriptStudioMerging::canSettle(doc));
        doc.compareView->right()->goTo(ALTextPos(6, 0));
        UNSCOPED_INFO("where it is in one");
        CHECK(ALScriptStudioMerging::canSettle(doc));
        CHECK(ALScriptStudioMerging::settle(doc, ALTextMerge::Take::Theirs));
        UNSCOPED_INFO("theirs in the tab");
        CHECK(doc.editor->wholeText() == lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "theirs 6" } }));
        CHECK(doc.compareView->conflictCount() == 0);
        CHECK_FALSE(ALScriptStudioMerging::canSettle(doc));
        notice = ALScriptStudioOrphans::noticeFor(&doc, services());
        UNSCOPED_INFO("nor to say");
        REQUIRE(notice.buttons[0].first.empty());
    }

    TEST_CASE_METHOD(alscriptstudiomerging_data, "alscriptstudiomerging: a merge follows the tab's comparison in letting blank changes go", "[alscriptstudiomerging]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // The tab compared before, letting blanks go; then changed line 5
        // and re-indented line 7, while what was saved elsewhere re-indented
        // lines 2 to 5 and changed line 3.
        Doc& doc = tab("door");
        studio.compareWithTab(doc, theirs, "earlier", std::string(), {});
        doc.compareView->setIgnore("whitespace", true);
        doc.editor->selectAll();
        doc.editor->insertText(lines(8, { { 5, "mine 5" }, { 7, "    line 7" } }));
        doc.savedThere = lines(8, { { 2, "  line 2" }, { 3, "  three" }, { 4, "  line 4" }, { 5, "  line 5" } });
        unit->mergeSaved(doc);
        UNSCOPED_INFO("theirs's line 3 put in, the tab's own kept");
        CHECK(doc.editor->wholeText() ==
                      lines(8, { { 3, "  three" }, { 5, "mine 5" }, { 7, "    line 7" } }));
        UNSCOPED_INFO("compared with the tab as merged");
        CHECK(doc.compareView->rightText() == doc.editor->wholeText());
        REQUIRE(doc.compareView->merging());
        CHECK(doc.compareView->conflictCount() == 0);
        REQUIRE_FALSE(services().reports.empty());
        CHECK(services().reports.back().text == said("MergeClean", named(doc)));
    }
}
