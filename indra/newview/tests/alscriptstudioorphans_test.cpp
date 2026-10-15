/**
 * @file alscriptstudioorphans_test.cpp
 * @brief Script Studio's orphans: what a tab has become, a failed load, the check, reattaching, and the notice with its bar.
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

#include "../alscriptstudioorphans.h"

#include "alrecoverystore.h"
#include "alscriptstudio_fixture.h"
#include "llbutton.h"
#include "lltextbox.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    typedef ALScriptStudioDoc                 Doc;
    typedef Doc::Orphan                       Orphan;
    typedef ALScriptStudioOrphans             Orphans;
    typedef Orphans::Reach                    Reach;
    typedef ALScriptLoaded::Failure Failure;
    typedef std::vector<std::string>          Names;

    // The notice bar's window, faked: its buttons pressed, by action.
    struct FakeNoticeWindow : public ALScriptNoticeBar::Window
    {
        void  noticeAction(const std::string& action) override { pressed.push_back(action); }
        Names pressed;
    };

    // The orphans' window, faked: what is in reach as a test says, and a
    // record of what was asked.
    struct FakeOrphansWindow : public Orphans::Window, public al_studio_test::QuietTabs, public al_studio_test::QuietSaves
    {
        // A tab, as each role this fakes names it.
        typedef ALScriptStudioDoc Doc;

        Reach reach(const Doc& doc) override { return reaches.count(doc.id) ? reaches[doc.id] : Reach(); }
        void  refreshPlace(Doc& doc) override { placed.push_back(doc.id); }
        void  loadScript(const ALScriptRef& ref) override { loads.push_back(ref); }
        ALScriptNoticeBar* noticeBar() override { return bar; }
        void               refreshToolbar() override { ++toolbars; }
        void               saveCopyToInventory(Doc& doc) override { did.push_back("copy " + doc.id); }
        void               saveAsked(Doc& doc) override { did.push_back("save " + doc.id); }
        void               takeOffer(Doc& doc, const std::string& action) override { did.push_back("offer " + doc.id + ": " + action); }
        void               discardRecovery(const ALRecoveryEntry&) override { did.push_back("discard"); }
        void               takeCarriedText(Doc& doc) override
        {
            did.push_back("carried " + doc.id + ": " + doc.carriedText.value_or(std::string()));
            doc.carriedText.reset();
        }
        void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                     const std::string& right_title) override
        {
            did.push_back("compare " + doc.id + ": " + left + " | " + right + " (" + left_title + " | " + right_title + ")");
        }
        void endCompare(Doc& doc) override { did.push_back("source " + doc.id); }

        std::map<std::string, Reach> reaches;
        Names                        placed, did;
        std::vector<ALScriptRef>     loads;
        ALScriptNoticeBar*           bar      = nullptr;
        S32                          toolbars = 0;
    };

    Reach inReach()
    {
        Reach reach;
        reach.itemThere   = true;
        reach.objectThere = true;
        reach.fileThere   = true;
        return reach;
    }
}

namespace
{
    struct alscriptstudioorphans_data
    {
        al_studio_test::StudioWindowOf<FakeNoticeWindow> window;
        FakeOrphansWindow                                studio;
        std::unique_ptr<al_studio_test::StudioRecovery>  recovery;
        std::unique_ptr<al_studio_test::StudioFiles>     files;
        std::unique_ptr<Orphans>                         unit;

        al_studio_test::FakeServices& services() { return window.services(); }
        // The window's words, as the skin has them.
        std::string said(const std::string& name, const std::string& blank = std::string(), const std::string& word = std::string())
        {
            LLStringUtil::format_map_t args;
            if (!blank.empty())
            {
                args[blank] = word;
            }
            return services().words(name, args);
        }
        Orphans&                      make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            recovery = std::make_unique<al_studio_test::StudioRecovery>(services(), &studio);
            files    = std::make_unique<al_studio_test::StudioFiles>(services());
            unit     = std::make_unique<Orphans>(services(), studio, studio, recovery->unit, files->unit, studio);
            return *unit;
        }
        // A tab of a script in an object, or in the inventory, loaded and
        // changeable.
        Doc& tab(const std::string& id, bool inventory = false)
        {
            Doc& doc = services().addDoc(id, ALScriptRef(inventory ? LLUUID::null : LLUUID::generateNewID(), LLUUID::generateNewID()), id);
            doc.recoveryKey = ALRecoveryStore::keyOf(doc.ref.object, doc.ref.item, std::string());
            doc.name       = id;
            doc.loaded     = true;
            doc.modifiable = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name     = "editor_" + id;
            p.rect     = LLRect(0, 200, 400, 0);
            p.syntax   = "lsl";
            doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(doc.editor);
            doc.editor->setText("default {}");
            doc.editor->resetDirty();
            studio.reaches[id] = inReach();
            return doc;
        }
    };

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: a tab's orphan kind follows what is in reach", "[alscriptstudioorphans]")
    {
        make();
        Doc&  o     = tab("object");
        Reach reach = inReach();
        UNSCOPED_INFO("in reach: nothing");
        CHECK(Orphans::seen(o, reach) == Orphan::None);
        reach.objectThere = false;
        UNSCOPED_INFO("its object out of sight");
        CHECK(Orphans::seen(o, reach) == Orphan::Away);
        reach.objectThere = true;
        reach.heldByPrim  = false;
        UNSCOPED_INFO("its prim holds it no more");
        CHECK(Orphans::seen(o, reach) == Orphan::Removed);
        reach.heldByPrim.reset();
        o.orphan->kind = Orphan::Removed;
        UNSCOPED_INFO("while the prim is asked again, gone stays gone");
        CHECK(Orphans::seen(o, reach) == Orphan::Removed);
        o.orphan->kind = Orphan::None;
        UNSCOPED_INFO("and nothing stays nothing");
        CHECK(Orphans::seen(o, reach) == Orphan::None);
        reach.heldByPrim = true;
        o.orphan->kind   = Orphan::Locked;
        UNSCOPED_INFO("a locked text stays locked while its item is there");
        CHECK(Orphans::seen(o, reach) == Orphan::Locked);
        reach.heldByPrim = false;
        UNSCOPED_INFO("but not once it is gone");
        CHECK(Orphans::seen(o, reach) == Orphan::Removed);
        reach.offline = true;
        CHECK(Orphans::seen(o, reach) == Orphan::Offline);

        Doc&  i    = tab("inventory", true);
        Reach item = inReach();
        item.trashed = true;
        CHECK(Orphans::seen(i, item) == Orphan::Trashed);
        i.orphan->kind = Orphan::Unloaded;
        UNSCOPED_INFO("an unloaded text stays so in the Trash");
        CHECK(Orphans::seen(i, item) == Orphan::Unloaded);
        item.itemThere = false;
        UNSCOPED_INFO("its item gone");
        CHECK(Orphans::seen(i, item) == Orphan::Removed);

        Doc& f = tab("file");
        f.ref  = ALScriptRef();
        f.file = "/somewhere/f.lsl";
        Reach file;
        CHECK(Orphans::seen(f, file) == Orphan::FileGone);
        file.fileThere = true;
        UNSCOPED_INFO("its file there");
        CHECK(Orphans::seen(f, file) == Orphan::None);
        f.loaded = false;
        UNSCOPED_INFO("not loaded yet: nothing");
        CHECK(Orphans::seen(f, Reach()) == Orphan::None);
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: a failed load makes a tab locked or unloaded or gone or away", "[alscriptstudioorphans]")
    {
        make();
        Doc& o = tab("object");
        Doc& i = tab("inventory", true);
        CHECK(Orphans::failedAs(o, Failure::NotPermitted, true) == Orphan::Locked);
        CHECK(Orphans::failedAs(o, Failure::Unreadable, true) == Orphan::Unloaded);
        CHECK(Orphans::failedAs(o, Failure::Fetch, true) == Orphan::Unloaded);
        UNSCOPED_INFO("gone from its object");
        CHECK(Orphans::failedAs(o, Failure::Missing, true) == Orphan::Removed);
        UNSCOPED_INFO("its object out of sight");
        CHECK(Orphans::failedAs(o, Failure::Missing, false) == Orphan::Away);
        UNSCOPED_INFO("gone from the inventory");
        CHECK(Orphans::failedAs(i, Failure::Missing, false) == Orphan::Removed);
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: the check lets a tab be away a moment and says what was lost or back", "[alscriptstudioorphans]")
    {
        Orphans& unit = make();
        Doc&     a    = tab("a");
        Doc&     b    = tab("b");
        studio.reaches["a"].objectThere = false;
        const F64 due = unit.check();
        UNSCOPED_INFO("each tab's place taken");
        CHECK(studio.placed == Names{ "a", "b" });
        {
            INFO("out of sight a moment: not yet away");
            CHECK(a.orphan->kind == Orphan::None);
            CHECK(a.orphan->awaySince > 0.0);
        }
        {
            INFO("and to be looked at again once the moment is past");
            CHECK(due > a.orphan->awaySince);
            CHECK(due <= a.orphan->awaySince + 3.0);
        }
        a.orphan->awaySince -= 5.0;
        a.orphan->noticeDismissed = true;
        unit.check();
        {
            INFO("gone a while: away");
            CHECK(a.orphan->kind == Orphan::Away);
            CHECK_FALSE(a.orphan->noticeDismissed);
            CHECK(studio.toolbars == 1);
        }
        b.editor->setCaret(b.editor->document().end());
        b.editor->insertText(" typed");
        studio.reaches["b"].heldByPrim = false;
        unit.check();
        {
            INFO("gone with changes: kept");
            CHECK(b.orphan->kind == Orphan::Removed);
            CHECK(recovery->keptFor(b.recoveryKey) == 1);
        }
        {
            INFO("and said, with what can be done");
            REQUIRE_FALSE(services().reports.empty());
            CHECK(services().reports.back().text == said("OrphanRemovedKept", "[NAME]", "b"));
            CHECK(services().reports.back().actions == Names{ "copy", "export" });
        }
        studio.reaches["a"].objectThere = true;
        unit.check();
        {
            INFO("back: said");
            CHECK(a.orphan->kind == Orphan::None);
            CHECK(services().reports.back().text == said("OrphanBack", "[NAME]", "a"));
            CHECK(a.orphan->awaySince == 0.0);
        }
        const size_t said = services().reports.size();
        unit.check();
        {
            INFO("nothing changed: nothing said");
            CHECK(services().reports.size() == said);
            CHECK(studio.toolbars == 3);
        }
        Doc& c                  = tab("c");
        c.orphan->kind          = Orphan::Away;
        c.orphan->detached      = true;
        c.orphan->reattachTries = ALRecoveryRetry::TRIES;
        unit.check();
        {
            INFO("a detached one back: nothing said, its load will");
            CHECK(c.orphan->kind == Orphan::None);
            CHECK(services().reports.size() == said);
        }
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: a detached tab reloads under its kept text while tries last and are due", "[alscriptstudioorphans]")
    {
        Orphans& unit = make();
        Doc&     a    = tab("a");
        a.orphan->detached = true;
        // A kept text, unsaved over nothing loaded.
        a.editor->setText("kept");
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" text");
        a.orphan->reattachTries = ALRecoveryRetry::TRIES;
        unit.check();
        UNSCOPED_INFO("tried out: waits for a person");
        CHECK(studio.loads.empty());
        a.orphan->reattachTries = 1;
        a.orphan->nextReattach  = LLTimer::getTotalSeconds() + 60.0;
        const F64 next = unit.check();
        UNSCOPED_INFO("to be looked at again when its try is due");
        CHECK(next == a.orphan->nextReattach);
        UNSCOPED_INFO("not due yet");
        CHECK(studio.loads.empty());
        a.orphan->nextReattach = 0.0;
        unit.check();
        REQUIRE(studio.loads.size() == 1);
        CHECK(studio.loads[0] == a.ref);
        {
            INFO("what it holds kept first, and carried over");
            CHECK(recovery->keptFor(a.recoveryKey) == 1);
            REQUIRE(a.carriedText);
            CHECK(*a.carriedText == "kept text");
        }
        {
            INFO("not loaded, and not to be typed in, until it is");
            CHECK_FALSE(a.loaded);
            CHECK(a.editor->isReadOnly());
            CHECK_FALSE(a.orphan->detached);
        }
        Doc& u = tab("u");
        u.orphan->detached = true;
        u.orphan->kind     = Orphan::Unloaded;
        u.loadFailure     = Failure::Unreadable;
        studio.reaches["u"].heldByPrim = true;
        unit.check();
        UNSCOPED_INFO("an unreadable one is not tried on its own");
        CHECK(studio.loads.size() == 1);
        u.loadFailure = Failure::Fetch;
        unit.check();
        UNSCOPED_INFO("a fetch that failed is");
        CHECK(studio.loads.size() == 2);
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: the notice says a kept text first then each kind's words and buttons", "[alscriptstudioorphans]")
    {
        make();
        const al_studio_test::FakeServices& words = services();
        UNSCOPED_INFO("no tab: nothing");
        CHECK(Orphans::noticeFor(nullptr, words).text.empty());
        Doc& a = tab("a");
        CHECK(Orphans::noticeFor(&a, words).text.empty());
        ALRecoveryEntry kept;
        kept.baseAsset = LLUUID::generateNewID();
        a.recoverable  = kept;
        a.orphan->kind  = Orphan::Away;
        ALScriptNoticeBar::Notice notice = Orphans::noticeFor(&a, words);
        const std::string when = kept.whenSaid();
        {
            INFO("a kept text first");
            CHECK(notice.text == said("NoticeRecoverable", "[WHEN]", when));
            CHECK(notice.buttons[0].first == "compare_kept");
            CHECK(notice.buttons[1].first == "restore");
            CHECK(notice.buttons[2] == std::make_pair(std::string("discard_left"), std::string("NoticeDiscard")));
        }
        a.assetId = LLUUID::generateNewID();
        UNSCOPED_INFO("stale where saved since");
        CHECK(Orphans::noticeFor(&a, words).text == said("NoticeRecoverableStale", "[WHEN]", when));
        a.recoverable.reset();
        notice = Orphans::noticeFor(&a, words);
        CHECK(notice.text == said("NoticeAway"));
        CHECK(notice.buttons[0].first == "copy");
        CHECK(notice.buttons[1].first == "export");
        a.orphan->kind = Orphan::Offline;
        notice         = Orphans::noticeFor(&a, words);
        {
            INFO("offline: a copy on disk alone");
            CHECK(notice.buttons[0].first == "export");
            CHECK(notice.buttons[1].first.empty());
        }
        a.orphan->kind = Orphan::Unloaded;
        a.loadError    = "no such asset";
        notice         = Orphans::noticeFor(&a, words);
        {
            INFO("unloaded: why, and to try again");
            CHECK(notice.text.find("no such asset") != std::string::npos);
            CHECK(notice.buttons[0].first == "retry_load");
        }
        a.orphan->kind = Orphan::Trashed;
        notice         = Orphans::noticeFor(&a, words);
        {
            INFO("in the Trash: said, nothing to do");
            CHECK(notice.text == said("NoticeTrashed"));
            CHECK(notice.buttons[0].first.empty());
        }
        a.orphan->kind = Orphan::FileGone;
        a.file         = "/somewhere/a.lsl";
        notice         = Orphans::noticeFor(&a, words);
        {
            INFO("a file gone: saved again");
            CHECK(notice.text.find("/somewhere/a.lsl") != std::string::npos);
            CHECK(notice.buttons[0].first == "save");
        }
        Doc& i = tab("i", true);
        i.orphan->kind = Orphan::Removed;
        UNSCOPED_INFO("gone from the inventory, said so");
        CHECK(Orphans::noticeFor(&i, words).text == said("NoticeRemovedInventory"));
        i.orphan->noticeDismissed = true;
        UNSCOPED_INFO("hidden: nothing");
        CHECK(Orphans::noticeFor(&i, words).text.empty());
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: the notice's actions act on the tab in front", "[alscriptstudioorphans]")
    {
        Orphans& unit = make();
        Doc&     a    = tab("a");
        services().front = 0;
        a.recoverable    = recovery->leftFor(a, "restored text");
        unit.noticeAction("restore");
        {
            INFO("restored, the source in front");
            CHECK(studio.did == Names{ "source a", "carried a: restored text" });
            CHECK_FALSE(a.recoverable);
            REQUIRE_FALSE(services().reports.empty());
            CHECK(services().reports.back().text == said("RecoveryRestored", "[NAME]", "a"));
        }
        a.recoverable = ALRecoveryEntry();
        unit.noticeAction("discard_left");
        REQUIRE_FALSE(studio.did.empty());
        CHECK(studio.did.back() == "discard");
        CHECK_FALSE(a.recoverable);
        a.recoverable = ALRecoveryEntry();
        unit.noticeAction("close");
        {
            INFO("hidden, the kept text let go of here");
            CHECK(a.orphan->noticeDismissed);
            CHECK_FALSE(a.recoverable);
        }
        unit.noticeAction("copy");
        unit.noticeAction("export");
        unit.noticeAction("save");
        CHECK(studio.did == Names{ "source a", "carried a: restored text", "source a", "discard", "copy a", "save a" });
        CHECK(files->picked == Names{ "a" });
        a.orphan->reattachTries = 3;
        unit.noticeAction("retry_load");
        UNSCOPED_INFO("not detached: nothing to try");
        CHECK(studio.loads.empty());
        a.orphan->detached = true;
        unit.noticeAction("retry_load");
        {
            INFO("tried again, from the first try");
            CHECK(studio.loads.size() == 1);
            CHECK(a.orphan->reattachTries == 0);
        }
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: the bar shows words and buttons and hides with nothing to say", "[alscriptstudioorphans]")
    {
        Orphans&           unit = make();
        ALScriptNoticeBar* bar  = window.find<ALScriptNoticeBar>("notice");
        UNSCOPED_INFO("built from the skin");
        REQUIRE(bar != nullptr);
        studio.bar = bar;
        Doc& a     = tab("a");
        services().front = 0;
        a.orphan->kind    = Orphan::Away;
        unit.refreshNotice();
        LLView* holder = bar->getParent();
        REQUIRE(holder != nullptr);
        CHECK(holder->getVisible());
        CHECK(bar->getChild<LLTextBox>("notice_text")->getText() == said("NoticeAway"));
        LLButton* first  = bar->getChild<LLButton>("notice_first");
        LLButton* second = bar->getChild<LLButton>("notice_second");
        {
            INFO("its buttons, labelled");
            CHECK(first->getVisible());
            CHECK(second->getVisible());
            CHECK(first->getLabelSelected() == said("NoticeCopy"));
            CHECK(first->getToolTip() == said("NoticeCopyTip"));
        }
        UNSCOPED_INFO("the first left of the second");
        CHECK(first->getRect().mRight < second->getRect().mLeft);
        first->onCommit();
        second->onCommit();
        bar->getChild<LLButton>("notice_close")->onCommit();
        UNSCOPED_INFO("the window told");
        CHECK(window.pane().pressed == Names{ "copy", "export", "close" });
        a.orphan->kind = Orphan::Offline;
        unit.refreshNotice();
        {
            INFO("one button");
            CHECK(first->getVisible());
            CHECK_FALSE(second->getVisible());
            CHECK(bar->action(0) == "export");
        }
        a.orphan->kind = Orphan::None;
        unit.refreshNotice();
        CHECK_FALSE(holder->getVisible());
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: a compiled half the source could not make is compared or kept or taken", "[alscriptstudioorphans]")
    {
        Orphans& unit = make();
        const al_studio_test::FakeServices& words = services();
        Doc& a            = tab("a");
        services().front  = 0;
        a.compiledDiffers = std::string("default { touch_start(integer n) { llDie(); } }");
        ALScriptNoticeBar::Notice notice = Orphans::noticeFor(&a, words);
        {
            INFO("said, with its ways");
            CHECK(notice.text == said("NoticeCompiledDiffers"));
            CHECK(notice.buttons[0].first == "compare_compiled");
            CHECK(notice.buttons[1].first == "keep_source");
            CHECK(notice.buttons[2].first == "take_compiled");
        }
        a.uploaded.text = std::make_shared<const std::string>("default { }");
        unit.noticeAction("compare_compiled");
        REQUIRE_FALSE(studio.did.empty());
        UNSCOPED_INFO("compared: as saved, beside what the source makes");
        CHECK(studio.did.back() ==
                      "compare a: default { touch_start(integer n) { llDie(); } } | default { } (" + said("CompareCompiled") + " | " + said("CompareMade") + ")");
        UNSCOPED_INFO("the notice stays, to act on after looking");
        CHECK(a.compiledDiffers.has_value());
        studio.did.clear();
        a.orphan->kind = Orphan::Away;
        UNSCOPED_INFO("a tab gone first");
        CHECK(Orphans::noticeFor(&a, words).text == said("NoticeAway"));
        a.orphan->kind = Orphan::None;
        unit.noticeAction("take_compiled");
        {
            INFO("the source back in front, then taken as the source");
            CHECK(studio.did == Names{ "source a", "carried a: default { touch_start(integer n) { llDie(); } }" });
            CHECK_FALSE(a.compiledDiffers);
        }
        REQUIRE_FALSE(services().reports.empty());
        UNSCOPED_INFO("and said");
        CHECK(services().reports.back().text == said("CompiledTaken", "[NAME]", "a"));
        a.compiledDiffers = std::string("x");
        unit.noticeAction("keep_source");
        {
            INFO("kept: let go of, the next save replacing it");
            CHECK_FALSE(a.compiledDiffers);
            REQUIRE_FALSE(studio.did.empty());
            CHECK(studio.did.back() == "source a");
        }
        UNSCOPED_INFO("nothing more to say");
        CHECK(Orphans::noticeFor(&a, words).text.empty());

        // Tapple's: its X lets go of it as Keep Source does.
        a.compiledDiffers = std::string("y");
        unit.noticeAction("close");
        {
            INFO("closed: let go of, not said again");
            CHECK_FALSE(a.compiledDiffers);
            CHECK(Orphans::noticeFor(&a, words).text.empty());
        }
        // Closed while something else is said, it is said next.
        a.compiledDiffers         = std::string("z");
        a.orphan->kind            = Orphan::Away;
        a.orphan->noticeDismissed = false;
        REQUIRE(Orphans::noticeFor(&a, words).text == said("NoticeAway"));
        unit.noticeAction("close");
        {
            INFO("another notice closed: the compiled half's said next");
            CHECK(a.compiledDiffers.has_value());
            CHECK(Orphans::noticeFor(&a, words).text == said("NoticeCompiledDiffers"));
        }
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: a kept text compares with the tab and restores with the source in front", "[alscriptstudioorphans]")
    {
        Orphans& unit    = make();
        Doc&     a       = tab("a");
        services().front = 0;
        // As a tab is offered it: the entry's listing, not yet its text.
        const ALRecoveryEntry kept = recovery->leftFor(a, "default { state_entry() { } }");
        a.recoverable              = kept;
        UNSCOPED_INFO("offered as listed");
        CHECK_FALSE(kept.whole);
        unit.noticeAction("compare_kept");
        REQUIRE_FALSE(studio.did.empty());
        UNSCOPED_INFO("now beside kept");
        CHECK(studio.did.back() ==
                      "compare a: default {} | default { state_entry() { } } (" + said("CompareNow") + " | " + said("CompareKept", "[WHEN]", kept.whenSaid()) + ")");
        UNSCOPED_INFO("still offered");
        CHECK(a.recoverable.has_value());
        unit.noticeAction("restore");
        {
            INFO("the source in front first");
            REQUIRE(studio.did.size() >= 2);
            CHECK(studio.did[studio.did.size() - 2] == "source a");
            CHECK(studio.did.back() == "carried a: default { state_entry() { } }");
        }
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: the last word's offer shows after the tab's state and is taken or hidden", "[alscriptstudioorphans]")
    {
        Orphans&                            unit  = make();
        const al_studio_test::FakeServices& words = services();
        Doc&                                a     = tab("a");
        services().front                          = 0;
        a.offer                                   = Doc::Offer{ "Saving a failed.", { "retry", "copy", "export" } };
        ALScriptNoticeBar::Notice notice          = Orphans::noticeFor(&a, words);
        {
            INFO("said with its buttons");
            CHECK(notice.text == "Saving a failed.");
            CHECK(notice.buttons[0] == std::make_pair(std::string("retry"), std::string("NoticeRetrySave")));
            CHECK(notice.buttons[1].first == "copy");
            CHECK(notice.buttons[2].first == "export");
        }
        a.orphan->kind = Orphan::Away;
        UNSCOPED_INFO("what the tab is first");
        CHECK(Orphans::noticeFor(&a, words).text == said("NoticeAway"));
        a.orphan->noticeDismissed = true;
        UNSCOPED_INFO("then the offer, once that is hidden");
        CHECK(Orphans::noticeFor(&a, words).text == "Saving a failed.");
        a.orphan->kind            = Orphan::None;
        a.orphan->noticeDismissed = false;
        unit.noticeAction("retry");
        {
            INFO("taken up, and let go of");
            REQUIRE_FALSE(studio.did.empty());
            CHECK(studio.did.back() == "offer a: retry");
            CHECK_FALSE(a.offer);
        }
        a.offer = Doc::Offer{ "Saved elsewhere.", { "take_saved", "keep_saved", "merge_saved", "compare_saved" } };
        UNSCOPED_INFO("a conflict with nothing to take: not said");
        CHECK(Orphans::noticeFor(&a, words).text.empty());
        a.savedThere = std::string("theirs");
        notice       = Orphans::noticeFor(&a, words);
        {
            INFO("with it: said, all four");
            CHECK(notice.text == "Saved elsewhere.");
            CHECK(notice.buttons[0].second == "NoticeTakeSaved");
            CHECK(notice.buttons[2].second == "NoticeMerge");
            CHECK(notice.buttons[3].second == "NoticeCompare");
        }
        unit.noticeAction("close");
        {
            INFO("hidden: let go of, and nothing else");
            CHECK_FALSE(a.offer);
            CHECK_FALSE(a.orphan->noticeDismissed);
            CHECK(studio.did.back() == "offer a: retry");
        }
        a.offer           = Doc::Offer{ "Held.", { "save_anyway" } };
        a.compiledDiffers = std::string("x");
        UNSCOPED_INFO("said before a compiled half");
        CHECK(Orphans::noticeFor(&a, words).text == "Held.");
        {
            INFO("a save's, answered by a save; a conflict's not");
            CHECK(a.offer->bySave());
            CHECK_FALSE(Doc::Offer{ "x", { "take_external", "keep_here" } }.bySave());
        }
        a.orphan->kind = Orphan::Removed;
        a.offer        = Doc::Offer{ "Kept.", { "copy", "export" } };
        unit.noticeAction("close");
        {
            INFO("what a tab gone says of itself, hidden with it");
            CHECK(a.orphan->noticeDismissed);
            CHECK_FALSE(a.offer);
        }
        a.orphan->noticeDismissed = false;
        a.offer                   = Doc::Offer{ "Saving a failed.", { "retry", "copy", "export" } };
        unit.noticeAction("close");
        {
            INFO("more than that: said next");
            CHECK(a.offer);
            CHECK(Orphans::noticeFor(&a, words).text == "Saving a failed.");
        }
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: no skin string begins with a quotation mark so code adds vim's quotes", "[alscriptstudioorphans]")
    {
        make();
        UNSCOPED_INFO("a name in quotes");
        CHECK(said("RenameBadName", "[NAME]", "9lives") == std::string("Not a valid name: \"9lives\"."));
        LLStringUtil::format_map_t written;
        written["[QUOTED]"] = "\"a.lsl\"";
        written["[LINES]"]  = "4";
        written["[BYTES]"]  = "20";
        UNSCOPED_INFO("vim's written");
        CHECK(services().words("VimWritten", written) == std::string("\"a.lsl\" 4L, 20B written"));
        const std::string vimrc = services().words("VimrcNewFile");
        {
            INFO("the vimrc's words whole");
            CHECK(vimrc.rfind("Script Studio's vimrc.", 0) == 0);
            CHECK(std::count(vimrc.begin(), vimrc.end(), '\n') == 3);
        }
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: a tab's standing changes bring the notice back and space out retries", "[alscriptstudioorphans]")
    {
        make();
        Doc& a                    = tab("a");
        a.orphan->noticeDismissed = true;
        Orphans::become(a, Doc::Orphan::Removed);
        {
            INFO("removed, and said");
            CHECK(a.orphan->kind == Doc::Orphan::Removed);
            CHECK_FALSE(a.orphan->noticeDismissed);
            CHECK_FALSE(a.orphan->detached);
        }
        a.orphan->noticeDismissed = true;
        Orphans::showNotice(a);
        UNSCOPED_INFO("the notice back");
        CHECK_FALSE(a.orphan->noticeDismissed);
        Orphans::detach(a, Doc::Orphan::Unloaded);
        {
            INFO("an item's kept text detached");
            CHECK(a.orphan->detached);
            CHECK(a.orphan->kind == Doc::Orphan::Unloaded);
        }
        Doc& f = tab("f");
        f.file = "/nowhere/f.lsl";
        Orphans::detach(f, Doc::Orphan::FileGone);
        {
            INFO("a file's not: nothing is loaded under it");
            CHECK_FALSE(f.orphan->detached);
            CHECK(f.orphan->kind == Doc::Orphan::FileGone);
        }

        const F64 now = LLTimer::getTotalSeconds();
        Orphans::loadFailed(a);
        const F64 first = a.orphan->nextReattach - now;
        Orphans::loadFailed(a);
        const F64 second = a.orphan->nextReattach - now;
        {
            INFO("tried later, further apart each time");
            CHECK(a.orphan->reattachTries == 2);
            CHECK(first > 0.0);
            CHECK(second > first);
        }
        Orphans::loadWentThrough(a);
        UNSCOPED_INFO("from the first try once one goes through");
        CHECK(a.orphan->reattachTries == 0);
    }

    TEST_CASE_METHOD(alscriptstudioorphans_data, "alscriptstudioorphans: a history save offered back is restored as carried text or let go of", "[alscriptstudioorphans]")
    {
        Orphans& unit = make();
        Doc&     a    = tab("a");
        ALSavedText shown;
        shown.text     = "default { }";
        shown.when     = LLDate(1.8e9);
        a.historyShown = shown;
        unit.noticeAction("restore_saved");
        {
            INFO("the source back in front, then taken");
            CHECK(studio.did == Names{ "source a", "carried a: default { }" });
            CHECK_FALSE(a.historyShown);
        }
        LLStringUtil::format_map_t args;
        args["[NAME]"] = "a";
        args["[WHEN]"] = ALRecoveryEntry::sayWhen(shown.when);
        REQUIRE_FALSE(services().reports.empty());
        UNSCOPED_INFO("and said");
        CHECK(services().reports.back().text == services().words("HistoryRestored", args));

        studio.did.clear();
        a.historyShown = shown;
        a.modifiable   = false;
        unit.noticeAction("restore_saved");
        {
            INFO("not into a tab that may not be changed");
            CHECK(studio.did.empty());
            CHECK(a.historyShown);
        }
        unit.noticeAction("close");
        UNSCOPED_INFO("let go of with the notice");
        CHECK_FALSE(a.historyShown);
    }
}
