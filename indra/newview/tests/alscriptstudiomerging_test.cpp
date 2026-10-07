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

#include "../test/lltut.h"

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

namespace tut
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

    typedef test_group<alscriptstudiomerging_data> alscriptstudiomerging_group;
    typedef alscriptstudiomerging_group::object    alscriptstudiomerging_object;
    tut::alscriptstudiomerging_group               alscriptstudiomerging_test("alscriptstudiomerging");

    template<> template<>
    void alscriptstudiomerging_object::test<1>()
    {
        set_test_name("saved elsewhere, merged: what only theirs changed put in as one step to undo, the rest a merge to settle; take and keep done with");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc       = tab("door");
        doc.savedThere = theirs;
        ensure("it can be merged", ALScriptStudioMerging::canMerge(doc));
        unit->mergeSaved(doc);
        ensure_equals("theirs's line put in, ours kept", doc.editor->wholeText(), lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" } }));
        ensure("compared under the saved elsewhere's title", studio.titles.size() == 1 && studio.titles[0] == said("CompareSavedThere"));
        ensure("as a merge", doc.compareView && doc.compareView->merging());
        ensure_equals("one conflict", doc.compareView->conflictCount(), 1);
        ensure("take and keep done with", !doc.savedThere && studio.notices == 1);
        ensure_equals("said", services().reports.back().text, services().counted("MergeConflicts", 1, named(doc)));

        // Settled from the comparison's bar: an edit of the tab.
        doc.compareView->right()->setFocus(true);
        doc.compareView->right()->goTo(ALTextPos(6, 0));
        ALViewType::as<ALFlatButton>(doc.compareView->bar()->getChild<LLView>("keep_both"))->press();
        ensure_equals("mine then theirs", doc.editor->wholeText(),
                      lines(9, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" }, { 7, "theirs 6" }, { 8, "line 7" } }));
        ensure_equals("none left", doc.compareView->conflictCount(), 0);

        // Each a step to undo: the settling, then the merge.
        doc.editor->undo();
        doc.editor->undo();
        ensure_equals("back to ours", doc.editor->wholeText(), ours);
    }

    template<> template<>
    void alscriptstudiomerging_object::test<2>()
    {
        set_test_name("nothing both changed: merged, and said so");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc       = tab("door");
        doc.savedThere = lines(8, { { 0, "theirs 0" } });
        unit->mergeSaved(doc);
        ensure_equals("merged", doc.editor->wholeText(), lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" } }));
        ensure("no conflicts", doc.compareView->merging() && doc.compareView->conflictCount() == 0);
        ensure_equals("said", services().reports.back().text, said("MergeClean", named(doc)));
    }

    template<> template<>
    void alscriptstudiomerging_object::test<3>()
    {
        set_test_name("what both were no longer known, or the tab not to be changed: compared as they are, take and keep still offered, and said why");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc       = tab("door");
        doc.savedThere = theirs;
        doc.editor->undoJournal().markNeverSaved();
        unit->mergeSaved(doc);
        ensure_equals("nothing put in", doc.editor->wholeText(), ours);
        ensure("compared, not merged", doc.compareView && !doc.compareView->merging() && doc.compareView->changeCount() > 0);
        ensure("take and keep still offered", doc.savedThere && studio.notices == 0);
        ensure_equals("said", services().statuses.back(), said("MergeNoBase", named(doc)));

        Doc& locked       = tab("lamp");
        locked.modifiable = false;
        locked.savedThere = theirs;
        ensure("cannot be merged", !ALScriptStudioMerging::canMerge(locked));
        unit->mergeSaved(locked);
        ensure("compared as they are", locked.editor->wholeText() == ours && locked.compareView && !locked.compareView->merging());
        ensure_equals("said", services().statuses.back(), said("MergeReadOnly", named(locked)));
    }

    template<> template<>
    void alscriptstudiomerging_object::test<4>()
    {
        set_test_name("the world's item moved on: asked for, merged once loaded, and the tab made from its asset; nothing once the window has gone");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc&         doc   = tab("door");
        const LLUUID was   = LLUUID::generateNewID();
        const LLUUID moved = LLUUID::generateNewID();
        doc.assetId        = was;
        unit->mergeWorld(doc);
        ensure_equals("asked", studio.worldAsked, std::string("door"));
        ensure_equals("nothing yet", doc.editor->wholeText(), ours);
        studio.world(doc, theirs, moved);
        ensure_equals("merged", doc.editor->wholeText(), lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" } }));
        ensure("under the world's title, as a merge", studio.titles.back() == said("CompareWorld") && doc.compareView->conflictCount() == 1);
        ensure("made from what the world holds", doc.assetId == moved);

        Doc& lamp   = tab("lamp");
        lamp.assetId = was;
        unit->mergeWorld(lamp);
        unit.reset();
        studio.world(lamp, theirs, moved);
        ensure("nothing", lamp.editor->wholeText() == ours && lamp.assetId == was);
    }

    template<> template<>
    void alscriptstudiomerging_object::test<5>()
    {
        set_test_name("a merge undone before it is saved: what was saved elsewhere back, said, and offered again; a step forward leaves it so");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc       = tab("door");
        doc.savedThere = theirs;
        unit->mergeSaved(doc);
        ensure("set aside", !doc.savedThere && doc.merged);
        ensure("the conflicts said with the comparison to show", services().reports.back().actions == std::vector<std::string>{ "show_compare" });
        // Typed in, or any change not a step back: nothing.
        unit->textChanged(doc);
        ensure("still merged", !doc.savedThere && doc.merged);

        const S32 notices = studio.notices;
        doc.editor->undo();
        ensure_equals("back to ours", doc.editor->wholeText(), ours);
        unit->textChanged(doc);
        ensure("what was saved elsewhere back", doc.savedThere && *doc.savedThere == theirs && !doc.merged);
        ensure_equals("said", services().reports.back().text, said("MergeTakenBack", named(doc)));
        ensure("take, keep, merge and compare offered again",
               services().reports.back().actions == std::vector<std::string>{ "take_saved", "keep_saved", "merge_saved", "compare_saved" });
        ensure("the notice said again", studio.notices > notices);

        // Stepped forward: the merge is back in the text, the other version
        // still stands until it is merged again.
        doc.editor->redo();
        unit->textChanged(doc);
        ensure("left so", doc.savedThere && *doc.savedThere == theirs && !doc.merged);
        unit->mergeSaved(doc);
        ensure("merged again: set aside again", !doc.savedThere && doc.merged);
    }

    template<> template<>
    void alscriptstudiomerging_object::test<6>()
    {
        set_test_name("the world's item merged, then undone: the tab made from the asset it was again, so a save stops for the world");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc&         doc   = tab("door");
        const LLUUID was   = LLUUID::generateNewID();
        const LLUUID moved = LLUUID::generateNewID();
        doc.assetId        = was;
        unit->mergeWorld(doc);
        studio.world(doc, theirs, moved);
        ensure("made from what the world holds", doc.assetId == moved && doc.merged);
        doc.editor->undo();
        unit->textChanged(doc);
        ensure("made from what it was again", doc.assetId == was && !doc.merged);
        ensure("reload, merge and compare offered again",
               services().reports.back().actions == std::vector<std::string>{ "reload_world", "merge_world", "compare_world" });
    }

    template<> template<>
    void alscriptstudiomerging_object::test<7>()
    {
        set_test_name("a conflict settled from the keyboard where the caret is in one; the notice says conflicts are left until none are");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc       = tab("door");
        doc.savedThere = theirs;
        unit->mergeSaved(doc);
        // As the window keeps what was said, and shows the comparison.
        doc.offer = Doc::Offer{ services().reports.back().text, services().reports.back().actions };
        ensure("not while the comparison is not in front", !ALScriptStudioMerging::canSettle(doc));
        doc.view = Doc::View::Compare;
        ALScriptNoticeBar::Notice notice = ALScriptStudioOrphans::noticeFor(&doc, services());
        ensure("the notice offers the comparison", notice.buttons[0].first == "show_compare" && notice.buttons[1].first.empty());

        doc.compareView->right()->setFocus(true);
        doc.compareView->right()->goTo(ALTextPos(2, 0));
        ensure("not where the caret is in no conflict", !ALScriptStudioMerging::canSettle(doc));
        doc.compareView->right()->goTo(ALTextPos(6, 0));
        ensure("where it is in one", ALScriptStudioMerging::canSettle(doc));
        ensure("settled with theirs", ALScriptStudioMerging::settle(doc, ALTextMerge::Take::Theirs));
        ensure_equals("theirs in the tab", doc.editor->wholeText(), lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "theirs 6" } }));
        ensure_equals("none left", doc.compareView->conflictCount(), 0);
        ensure("nothing left to settle", !ALScriptStudioMerging::canSettle(doc));
        notice = ALScriptStudioOrphans::noticeFor(&doc, services());
        ensure("nor to say", notice.buttons[0].first.empty());
    }

    template<> template<>
    void alscriptstudiomerging_object::test<8>()
    {
        set_test_name("a merge begun as the tab's comparison tells lines: blanks let go of there, theirs's change among the lines they re-indented put in, and no conflict said where it finds none");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("theirs's line 3 put in, the tab's own kept", doc.editor->wholeText(),
                      lines(8, { { 3, "  three" }, { 5, "mine 5" }, { 7, "    line 7" } }));
        ensure("compared with the tab as merged", doc.compareView->rightText() == doc.editor->wholeText());
        ensure("a merge with no conflict", doc.compareView->merging() && doc.compareView->conflictCount() == 0);
        ensure_equals("said", services().reports.back().text, said("MergeClean", named(doc)));
    }
}
