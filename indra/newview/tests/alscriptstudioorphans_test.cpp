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

#include "../test/lltut.h"

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

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

    typedef test_group<alscriptstudioorphans_data> alscriptstudioorphans_group;
    typedef alscriptstudioorphans_group::object    alscriptstudioorphans_object;
    alscriptstudioorphans_group                    alscriptstudioorphans_instance("alscriptstudioorphans");

    template<> template<>
    void alscriptstudioorphans_object::test<1>()
    {
        set_test_name("what a tab has become, as what is in reach says: each kind, and a locked or unloaded text kept so while there is an item");
        make();
        Doc&  o     = tab("object");
        Reach reach = inReach();
        ensure("in reach: nothing", Orphans::seen(o, reach) == Orphan::None);
        reach.objectThere = false;
        ensure("its object out of sight", Orphans::seen(o, reach) == Orphan::Away);
        reach.objectThere = true;
        reach.heldByPrim  = false;
        ensure("its prim holds it no more", Orphans::seen(o, reach) == Orphan::Removed);
        reach.heldByPrim.reset();
        o.orphan->kind = Orphan::Removed;
        ensure("while the prim is asked again, gone stays gone", Orphans::seen(o, reach) == Orphan::Removed);
        o.orphan->kind = Orphan::None;
        ensure("and nothing stays nothing", Orphans::seen(o, reach) == Orphan::None);
        reach.heldByPrim = true;
        o.orphan->kind   = Orphan::Locked;
        ensure("a locked text stays locked while its item is there", Orphans::seen(o, reach) == Orphan::Locked);
        reach.heldByPrim = false;
        ensure("but not once it is gone", Orphans::seen(o, reach) == Orphan::Removed);
        reach.offline = true;
        ensure("offline", Orphans::seen(o, reach) == Orphan::Offline);

        Doc&  i    = tab("inventory", true);
        Reach item = inReach();
        item.trashed = true;
        ensure("in the Trash", Orphans::seen(i, item) == Orphan::Trashed);
        i.orphan->kind = Orphan::Unloaded;
        ensure("an unloaded text stays so in the Trash", Orphans::seen(i, item) == Orphan::Unloaded);
        item.itemThere = false;
        ensure("its item gone", Orphans::seen(i, item) == Orphan::Removed);

        Doc& f = tab("file");
        f.ref  = ALScriptRef();
        f.file = "/somewhere/f.lsl";
        Reach file;
        ensure("its file gone", Orphans::seen(f, file) == Orphan::FileGone);
        file.fileThere = true;
        ensure("its file there", Orphans::seen(f, file) == Orphan::None);
        f.loaded = false;
        ensure("not loaded yet: nothing", Orphans::seen(f, Reach()) == Orphan::None);
    }

    template<> template<>
    void alscriptstudioorphans_object::test<2>()
    {
        set_test_name("what a failed load makes a tab: locked, unloaded, or gone -- from the inventory, its object, or out of sight");
        make();
        Doc& o = tab("object");
        Doc& i = tab("inventory", true);
        ensure("not permitted", Orphans::failedAs(o, Failure::NotPermitted, true) == Orphan::Locked);
        ensure("unreadable", Orphans::failedAs(o, Failure::Unreadable, true) == Orphan::Unloaded);
        ensure("a fetch that failed", Orphans::failedAs(o, Failure::Fetch, true) == Orphan::Unloaded);
        ensure("gone from its object", Orphans::failedAs(o, Failure::Missing, true) == Orphan::Removed);
        ensure("its object out of sight", Orphans::failedAs(o, Failure::Missing, false) == Orphan::Away);
        ensure("gone from the inventory", Orphans::failedAs(i, Failure::Missing, false) == Orphan::Removed);
    }

    template<> template<>
    void alscriptstudioorphans_object::test<3>()
    {
        set_test_name("the check: out of sight a moment is not gone; lost with changes, kept and said; back, said; the notice and toolbar told");
        Orphans& unit = make();
        Doc&     a    = tab("a");
        Doc&     b    = tab("b");
        studio.reaches["a"].objectThere = false;
        const F64 due = unit.check();
        ensure("each tab's place taken", studio.placed == Names{ "a", "b" });
        ensure("out of sight a moment: not yet away", a.orphan->kind == Orphan::None && a.orphan->awaySince > 0.0);
        ensure("and to be looked at again once the moment is past", due > a.orphan->awaySince && due <= a.orphan->awaySince + 3.0);
        a.orphan->awaySince -= 5.0;
        a.orphan->noticeDismissed = true;
        unit.check();
        ensure("gone a while: away", a.orphan->kind == Orphan::Away && !a.orphan->noticeDismissed && studio.toolbars == 1);
        b.editor->setCaret(b.editor->document().end());
        b.editor->insertText(" typed");
        studio.reaches["b"].heldByPrim = false;
        unit.check();
        ensure("gone with changes: kept", b.orphan->kind == Orphan::Removed && recovery->keptFor(b.recoveryKey) == 1);
        ensure("and said, with what can be done", services().reports.back().text == said("OrphanRemovedKept", "[NAME]", "b") &&
                                                       services().reports.back().actions == Names{ "copy", "export" });
        studio.reaches["a"].objectThere = true;
        unit.check();
        ensure("back: said", a.orphan->kind == Orphan::None && services().reports.back().text == said("OrphanBack", "[NAME]", "a") &&
                                 a.orphan->awaySince == 0.0);
        const size_t said = services().reports.size();
        unit.check();
        ensure("nothing changed: nothing said", services().reports.size() == said && studio.toolbars == 3);
        Doc& c                  = tab("c");
        c.orphan->kind          = Orphan::Away;
        c.orphan->detached      = true;
        c.orphan->reattachTries = ALRecoveryRetry::TRIES;
        unit.check();
        ensure("a detached one back: nothing said, its load will", c.orphan->kind == Orphan::None && services().reports.size() == said);
    }

    template<> template<>
    void alscriptstudioorphans_object::test<4>()
    {
        set_test_name("a detached tab loaded under what it holds once its item is in reach, while its tries last and the next is due");
        Orphans& unit = make();
        Doc&     a    = tab("a");
        a.orphan->detached = true;
        // A kept text, unsaved over nothing loaded.
        a.editor->setText("kept");
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" text");
        a.orphan->reattachTries = ALRecoveryRetry::TRIES;
        unit.check();
        ensure("tried out: waits for a person", studio.loads.empty());
        a.orphan->reattachTries = 1;
        a.orphan->nextReattach  = LLTimer::getTotalSeconds() + 60.0;
        const F64 next = unit.check();
        ensure("to be looked at again when its try is due", next == a.orphan->nextReattach);
        ensure("not due yet", studio.loads.empty());
        a.orphan->nextReattach = 0.0;
        unit.check();
        ensure("loaded", studio.loads.size() == 1 && studio.loads[0] == a.ref);
        ensure("what it holds kept first, and carried over",
               recovery->keptFor(a.recoveryKey) == 1 && a.carriedText && *a.carriedText == "kept text");
        ensure("not loaded, and not to be typed in, until it is", !a.loaded && a.editor->isReadOnly() && !a.orphan->detached);
        Doc& u = tab("u");
        u.orphan->detached = true;
        u.orphan->kind     = Orphan::Unloaded;
        u.loadFailure     = Failure::Unreadable;
        studio.reaches["u"].heldByPrim = true;
        unit.check();
        ensure("an unreadable one is not tried on its own", studio.loads.size() == 1);
        u.loadFailure = Failure::Fetch;
        unit.check();
        ensure("a fetch that failed is", studio.loads.size() == 2);
    }

    template<> template<>
    void alscriptstudioorphans_object::test<5>()
    {
        set_test_name("what the notice says: a kept text first, stale where saved since; each kind's words and buttons; nothing once hidden");
        make();
        const al_studio_test::FakeServices& words = services();
        ensure("no tab: nothing", Orphans::noticeFor(nullptr, words).text.empty());
        Doc& a = tab("a");
        ensure("nothing to say", Orphans::noticeFor(&a, words).text.empty());
        ALRecoveryEntry kept;
        kept.baseAsset = LLUUID::generateNewID();
        a.recoverable  = kept;
        a.orphan->kind  = Orphan::Away;
        ALScriptNoticeBar::Notice notice = Orphans::noticeFor(&a, words);
        const std::string when = kept.whenSaid();
        ensure("a kept text first", notice.text == said("NoticeRecoverable", "[WHEN]", when) && notice.buttons[0].first == "compare_kept" &&
                                        notice.buttons[1].first == "restore" &&
                                        notice.buttons[2] == std::make_pair(std::string("discard_left"), std::string("NoticeDiscard")));
        a.assetId = LLUUID::generateNewID();
        ensure("stale where saved since", Orphans::noticeFor(&a, words).text == said("NoticeRecoverableStale", "[WHEN]", when));
        a.recoverable.reset();
        notice = Orphans::noticeFor(&a, words);
        ensure("away", notice.text == said("NoticeAway") && notice.buttons[0].first == "copy" && notice.buttons[1].first == "export");
        a.orphan->kind = Orphan::Offline;
        notice         = Orphans::noticeFor(&a, words);
        ensure("offline: a copy on disk alone", notice.buttons[0].first == "export" && notice.buttons[1].first.empty());
        a.orphan->kind = Orphan::Unloaded;
        a.loadError    = "no such asset";
        notice         = Orphans::noticeFor(&a, words);
        ensure("unloaded: why, and to try again",
               notice.text.find("no such asset") != std::string::npos && notice.buttons[0].first == "retry_load");
        a.orphan->kind = Orphan::Trashed;
        notice         = Orphans::noticeFor(&a, words);
        ensure("in the Trash: said, nothing to do", notice.text == said("NoticeTrashed") && notice.buttons[0].first.empty());
        a.orphan->kind = Orphan::FileGone;
        a.file         = "/somewhere/a.lsl";
        notice         = Orphans::noticeFor(&a, words);
        ensure("a file gone: saved again", notice.text.find("/somewhere/a.lsl") != std::string::npos && notice.buttons[0].first == "save");
        Doc& i = tab("i", true);
        i.orphan->kind = Orphan::Removed;
        ensure("gone from the inventory, said so", Orphans::noticeFor(&i, words).text == said("NoticeRemovedInventory"));
        i.orphan->noticeDismissed = true;
        ensure("hidden: nothing", Orphans::noticeFor(&i, words).text.empty());
    }

    template<> template<>
    void alscriptstudioorphans_object::test<6>()
    {
        set_test_name("the notice's actions for the tab in front: hide, restore, discard, try again, a copy, a file, a save");
        Orphans& unit = make();
        Doc&     a    = tab("a");
        services().front = 0;
        a.recoverable    = recovery->leftFor(a, "restored text");
        unit.noticeAction("restore");
        ensure("restored, the source in front", studio.did == Names{ "source a", "carried a: restored text" } && !a.recoverable &&
                                                     services().reports.back().text == said("RecoveryRestored", "[NAME]", "a"));
        a.recoverable = ALRecoveryEntry();
        unit.noticeAction("discard_left");
        ensure("discarded", studio.did.back() == "discard" && !a.recoverable);
        a.recoverable = ALRecoveryEntry();
        unit.noticeAction("close");
        ensure("hidden, the kept text let go of here", a.orphan->noticeDismissed && !a.recoverable);
        unit.noticeAction("copy");
        unit.noticeAction("export");
        unit.noticeAction("save");
        ensure("a copy, a file, a save",
               studio.did == Names{ "source a", "carried a: restored text", "source a", "discard", "copy a", "save a" } &&
                   files->picked == Names{ "a" });
        a.orphan->reattachTries = 3;
        unit.noticeAction("retry_load");
        ensure("not detached: nothing to try", studio.loads.empty());
        a.orphan->detached = true;
        unit.noticeAction("retry_load");
        ensure("tried again, from the first try", studio.loads.size() == 1 && a.orphan->reattachTries == 0);
    }

    template<> template<>
    void alscriptstudioorphans_object::test<7>()
    {
        set_test_name("the bar: shown with its words and buttons, as wide as their labels; hidden with nothing to say; its buttons the window's");
        Orphans&           unit = make();
        ALScriptNoticeBar* bar  = window.find<ALScriptNoticeBar>("notice");
        ensure("built from the skin", bar != nullptr);
        studio.bar = bar;
        Doc& a     = tab("a");
        services().front = 0;
        a.orphan->kind    = Orphan::Away;
        unit.refreshNotice();
        LLView* holder = bar->getParent();
        ensure("shown", holder->getVisible());
        ensure_equals("its words", bar->getChild<LLTextBox>("notice_text")->getText(), said("NoticeAway"));
        LLButton* first  = bar->getChild<LLButton>("notice_first");
        LLButton* second = bar->getChild<LLButton>("notice_second");
        ensure("its buttons, labelled", first->getVisible() && second->getVisible() && first->getLabelSelected() == said("NoticeCopy") &&
                                            first->getToolTip() == said("NoticeCopyTip"));
        ensure("the first left of the second", first->getRect().mRight < second->getRect().mLeft);
        first->onCommit();
        second->onCommit();
        bar->getChild<LLButton>("notice_close")->onCommit();
        ensure("the window told", window.pane().pressed == Names{ "copy", "export", "close" });
        a.orphan->kind = Orphan::Offline;
        unit.refreshNotice();
        ensure("one button", first->getVisible() && !second->getVisible() && bar->action(0) == "export");
        a.orphan->kind = Orphan::None;
        unit.refreshNotice();
        ensure("hidden", !holder->getVisible());
    }

    template<> template<>
    void alscriptstudioorphans_object::test<8>()
    {
        set_test_name("a compiled half the source could not have made: said after a kept text and a tab gone, kept as source or taken as it");
        Orphans& unit = make();
        const al_studio_test::FakeServices& words = services();
        Doc& a            = tab("a");
        services().front  = 0;
        a.compiledDiffers = std::string("default { touch_start(integer n) { llDie(); } }");
        ALScriptNoticeBar::Notice notice = Orphans::noticeFor(&a, words);
        ensure("said, with its ways", notice.text == said("NoticeCompiledDiffers") && notice.buttons[0].first == "compare_compiled" &&
                                          notice.buttons[1].first == "keep_source" && notice.buttons[2].first == "take_compiled");
        a.uploaded.text = std::make_shared<const std::string>("default { }");
        unit.noticeAction("compare_compiled");
        ensure_equals("compared: as saved, beside what the source makes", studio.did.back(),
                      "compare a: default { touch_start(integer n) { llDie(); } } | default { } (" + said("CompareCompiled") + " | " + said("CompareMade") + ")");
        ensure("the notice stays, to act on after looking", a.compiledDiffers.has_value());
        studio.did.clear();
        a.orphan->kind = Orphan::Away;
        ensure("a tab gone first", Orphans::noticeFor(&a, words).text == said("NoticeAway"));
        a.orphan->kind = Orphan::None;
        unit.noticeAction("take_compiled");
        ensure("the source back in front, then taken as the source",
               studio.did == Names{ "source a", "carried a: default { touch_start(integer n) { llDie(); } }" } && !a.compiledDiffers);
        ensure("and said", services().reports.back().text == said("CompiledTaken", "[NAME]", "a"));
        a.compiledDiffers = std::string("x");
        unit.noticeAction("keep_source");
        ensure("kept: let go of, the next save replacing it", !a.compiledDiffers && studio.did.back() == "source a");
        ensure("nothing more to say", Orphans::noticeFor(&a, words).text.empty());
    }

    template<> template<>
    void alscriptstudioorphans_object::test<9>()
    {
        set_test_name("a kept text compared with what the tab holds now, the notice staying; restoring puts the source back in front");
        Orphans& unit    = make();
        Doc&     a       = tab("a");
        services().front = 0;
        // As a tab is offered it: the entry's listing, not yet its text.
        const ALRecoveryEntry kept = recovery->leftFor(a, "default { state_entry() { } }");
        a.recoverable              = kept;
        ensure("offered as listed", !kept.whole);
        unit.noticeAction("compare_kept");
        ensure_equals("now beside kept", studio.did.back(),
                      "compare a: default {} | default { state_entry() { } } (" + said("CompareNow") + " | " + said("CompareKept", "[WHEN]", kept.whenSaid()) + ")");
        ensure("still offered", a.recoverable.has_value());
        unit.noticeAction("restore");
        ensure("the source in front first",
               studio.did[studio.did.size() - 2] == "source a" && studio.did.back() == "carried a: default { state_entry() { } }");
    }

    template<> template<>
    void alscriptstudioorphans_object::test<10>()
    {
        set_test_name("what the last word offered: said after what the tab is, in the notice's words, a conflict's only while there is another text; "
                      "taken up as Output would, or hidden");
        Orphans&                            unit  = make();
        const al_studio_test::FakeServices& words = services();
        Doc&                                a     = tab("a");
        services().front                          = 0;
        a.offer                                   = Doc::Offer{ "Saving a failed.", { "retry", "copy", "export" } };
        ALScriptNoticeBar::Notice notice          = Orphans::noticeFor(&a, words);
        ensure("said with its buttons", notice.text == "Saving a failed." &&
                                            notice.buttons[0] == std::make_pair(std::string("retry"), std::string("NoticeRetrySave")) &&
                                            notice.buttons[1].first == "copy" && notice.buttons[2].first == "export");
        a.orphan->kind = Orphan::Away;
        ensure("what the tab is first", Orphans::noticeFor(&a, words).text == said("NoticeAway"));
        a.orphan->noticeDismissed = true;
        ensure("then the offer, once that is hidden", Orphans::noticeFor(&a, words).text == "Saving a failed.");
        a.orphan->kind            = Orphan::None;
        a.orphan->noticeDismissed = false;
        unit.noticeAction("retry");
        ensure("taken up, and let go of", studio.did.back() == "offer a: retry" && !a.offer);
        a.offer = Doc::Offer{ "Saved elsewhere.", { "take_saved", "keep_saved", "merge_saved", "compare_saved" } };
        ensure("a conflict with nothing to take: not said", Orphans::noticeFor(&a, words).text.empty());
        a.savedThere = std::string("theirs");
        notice       = Orphans::noticeFor(&a, words);
        ensure("with it: said, all four", notice.text == "Saved elsewhere." && notice.buttons[0].second == "NoticeTakeSaved" &&
                                              notice.buttons[2].second == "NoticeMerge" && notice.buttons[3].second == "NoticeCompare");
        unit.noticeAction("close");
        ensure("hidden: let go of, and nothing else", !a.offer && !a.orphan->noticeDismissed && studio.did.back() == "offer a: retry");
        a.offer           = Doc::Offer{ "Held.", { "save_anyway" } };
        a.compiledDiffers = std::string("x");
        ensure("said before a compiled half", Orphans::noticeFor(&a, words).text == "Held.");
        ensure("a save's, answered by a save; a conflict's not",
               a.offer->bySave() && !Doc::Offer{ "x", { "take_external", "keep_here" } }.bySave());
        a.orphan->kind = Orphan::Removed;
        a.offer        = Doc::Offer{ "Kept.", { "copy", "export" } };
        unit.noticeAction("close");
        ensure("what a tab gone says of itself, hidden with it", a.orphan->noticeDismissed && !a.offer);
        a.orphan->noticeDismissed = false;
        a.offer                   = Doc::Offer{ "Saving a failed.", { "retry", "copy", "export" } };
        unit.noticeAction("close");
        ensure("more than that: said next", a.offer && Orphans::noticeFor(&a, words).text == "Saving a failed.");
    }

    template<> template<>
    void alscriptstudioorphans_object::test<11>()
    {
        set_test_name("no string of the skin's begins with a quotation mark, which the skin reads as quoting it: vim's quotes are put in by the code");
        make();
        ensure_equals("a name in quotes", said("RenameBadName", "[NAME]", "9lives"), std::string("Not a valid name: \"9lives\"."));
        LLStringUtil::format_map_t written;
        written["[QUOTED]"] = "\"a.lsl\"";
        written["[LINES]"]  = "4";
        written["[BYTES]"]  = "20";
        ensure_equals("vim's written", services().words("VimWritten", written), std::string("\"a.lsl\" 4L, 20B written"));
        const std::string vimrc = services().words("VimrcNewFile");
        ensure("the vimrc's words whole", vimrc.rfind("Script Studio's vimrc.", 0) == 0 && std::count(vimrc.begin(), vimrc.end(), '\n') == 3);
    }

    template<> template<>
    void alscriptstudioorphans_object::test<12>()
    {
        set_test_name("a tab's standing as the window and a save change it: the notice back each time, a kept text detached where it is an item's, "
                      "a failed load tried later and further apart");
        make();
        Doc& a                    = tab("a");
        a.orphan->noticeDismissed = true;
        Orphans::become(a, Doc::Orphan::Removed);
        ensure("removed, and said", a.orphan->kind == Doc::Orphan::Removed && !a.orphan->noticeDismissed && !a.orphan->detached);
        a.orphan->noticeDismissed = true;
        Orphans::showNotice(a);
        ensure("the notice back", !a.orphan->noticeDismissed);
        Orphans::detach(a, Doc::Orphan::Unloaded);
        ensure("an item's kept text detached", a.orphan->detached && a.orphan->kind == Doc::Orphan::Unloaded);
        Doc& f = tab("f");
        f.file = "/nowhere/f.lsl";
        Orphans::detach(f, Doc::Orphan::FileGone);
        ensure("a file's not: nothing is loaded under it", !f.orphan->detached && f.orphan->kind == Doc::Orphan::FileGone);

        const F64 now = LLTimer::getTotalSeconds();
        Orphans::loadFailed(a);
        const F64 first = a.orphan->nextReattach - now;
        Orphans::loadFailed(a);
        const F64 second = a.orphan->nextReattach - now;
        ensure("tried later, further apart each time", a.orphan->reattachTries == 2 && first > 0.0 && second > first);
        Orphans::loadWentThrough(a);
        ensure("from the first try once one goes through", a.orphan->reattachTries == 0);
    }

    template<> template<>
    void alscriptstudioorphans_object::test<13>()
    {
        set_test_name("a save of the history offered back: restored as carried text, the comparison ended, said; or let go of with the notice");
        Orphans& unit = make();
        Doc&     a    = tab("a");
        ALSavedText shown;
        shown.text     = "default { }";
        shown.when     = LLDate(1.8e9);
        a.historyShown = shown;
        unit.noticeAction("restore_saved");
        ensure("the source back in front, then taken", studio.did == Names{ "source a", "carried a: default { }" } && !a.historyShown);
        LLStringUtil::format_map_t args;
        args["[NAME]"] = "a";
        args["[WHEN]"] = ALRecoveryEntry::sayWhen(shown.when);
        ensure_equals("and said", services().reports.back().text, services().words("HistoryRestored", args));

        studio.did.clear();
        a.historyShown = shown;
        a.modifiable   = false;
        unit.noticeAction("restore_saved");
        ensure("not into a tab that may not be changed", studio.did.empty() && a.historyShown);
        unit.noticeAction("close");
        ensure("let go of with the notice", !a.historyShown);
    }
}
