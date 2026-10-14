/**
 * @file alscriptstudiorecovery_test.cpp
 * @brief Script Studio's unsaved texts kept against a crash, and offered back.
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

#include "../alscriptstudiorecovery.h"

#include "../alrecovery.h"

#include "alscriptstudio_fixture.h"

#include "fsyspath.h"
#include "lltimer.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>

namespace
{
    typedef ALScriptStudioRecovery::Entry Entry;
    typedef ALScriptStudioDoc             Doc;

    // The window, faked: a record of what recovery asked of it.
    struct FakeRecoveryWindow : public ALScriptStudioRecovery::Window, public al_studio_test::QuietTabs
    {
        bool recoverElsewhere(const Entry& entry) override
        {
            if (!elsewhere)
            {
                return false;
            }
            tookElsewhere.push_back(entry);
            return true;
        }
        Doc* openFileTab(const std::string& path, bool lua) override { return opensFile ? opensFile(path) : nullptr; }
        bool scriptInHand(const ALScriptRef& ref) const override { return inHand; }
        void activate(Doc& doc) override { activated.push_back(doc.id); }
        void openOrphan(const Entry& entry, Doc::Orphan orphan) override { orphans.emplace_back(entry.name, orphan); }
        void becomeOrphan(Doc& doc, const Entry& entry, Doc::Orphan orphan) override { became.emplace_back(doc.id, orphan); }
        Doc::Orphan failedAs(const Doc& doc, ALScriptLoaded::Failure failure) const override
        {
            return failure == ALScriptLoaded::Failure::NotPermitted ? Doc::Orphan::Locked : Doc::Orphan::Unloaded;
        }
        // As the window does: the kept history where it fits, else the text
        // as one step to undo.
        void takeCarriedText(Doc& doc) override
        {
            if (!doc.carriedText)
            {
                return;
            }
            if (!(doc.recovering && *doc.carriedText == doc.recovering->text && recovery->restoreHistory(doc, *doc.recovering)))
            {
                doc.editor->setSelection(ALTextRange(doc.editor->document().start(), doc.editor->document().end()));
                doc.editor->insertText(*doc.carriedText);
            }
            doc.carriedText.reset();
        }
        void refreshNotice() override { ++notices; }
        void fillTabs() override { ++tabs; }
        void pick(std::vector<ALQuickOpen::Candidate> offered, const std::string&, const std::string&, std::function<void(const std::string&)> choose,
                  std::function<void(const std::string&)> drop) override
        {
            candidates = std::move(offered);
            chosen     = std::move(choose);
            dropped    = std::move(drop);
        }

        ALScriptStudioRecovery*                        recovery  = nullptr;
        bool                                           elsewhere = false;
        bool                                           inHand    = true;
        std::function<Doc*(const std::string& path)>   opensFile;
        std::vector<Entry>                             tookElsewhere;
        std::vector<std::string>                       activated;
        std::vector<std::pair<std::string, Doc::Orphan>> orphans;
        std::vector<std::pair<std::string, Doc::Orphan>> became;
        S32                                            notices = 0;
        S32                                            tabs    = 0;
        std::vector<ALQuickOpen::Candidate>            candidates;
        std::function<void(const std::string&)>        chosen;
        std::function<void(const std::string&)>        dropped;
    };
}

namespace
{
    struct alscriptstudiorecovery_data
    {
        al_studio_test::StudioWindowOf<FakeRecoveryWindow> window;
        std::unique_ptr<ALScriptStudioRecovery>             recovery;
        std::string                                         folder;
        std::unique_ptr<ALRecoveryStore>              store;

        // Every store a case makes, the fixture's or its own, writes on the
        // process's writer.
        alscriptstudiorecovery_data() { newview_test::useRecoveryWriter(); }

        ~alscriptstudiorecovery_data()
        {
            ALRecovery::useStore(nullptr);
            store.reset();
            if (!folder.empty())
            {
                std::error_code ignored;
                std::filesystem::remove_all(fsyspath(folder), ignored);
            }
        }

        al_studio_test::FakeServices& services() { return window.services(); }
        FakeRecoveryWindow&           studio() { return window.pane(); }

        ALScriptStudioRecovery& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            recovery          = std::make_unique<ALScriptStudioRecovery>(services(), studio(), studio());
            studio().recovery = recovery.get();
            folder = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alscriptstudiorecovery_" + LLUUID::generateNewID().asString())).string();
            std::filesystem::create_directories(fsyspath(folder));
            store = std::make_unique<ALRecoveryStore>(folder, "this-session");
            ALRecovery::useStore(store.get());
            return *recovery;
        }

        // A script's tab, loaded and changeable, over an editor holding
        // `text` as saved.
        Doc& tab(const std::string& id, const std::string& text, ALScriptRef ref = ALScriptRef())
        {
            if (ref.isNull())
            {
                LLUUID object, item;
                object.generate();
                item.generate();
                ref = ALScriptRef(object, item);
            }
            Doc& doc        = services().addDoc(id, ref, id);
            doc.loaded      = true;
            doc.modifiable  = true;
            doc.recoveryKey = ALRecoveryStore::keyOf(ref.object, ref.item, std::string());
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name      = "editor_" + id;
            p.rect      = LLRect(0, 200, 400, 0);
            p.syntax    = "lsl";
            doc.editor  = LLUICtrlFactory::create<ALCodeEditor>(p);
            doc.editor->setFont(LLFontGL::getFontMonospace());
            window.floater->addChild(doc.editor);
            doc.editor->setText(text);
            doc.editor->resetDirty();
            return doc;
        }

        // Typed at the end of a tab's text.
        static void type(Doc& doc, const std::string& text)
        {
            doc.editor->setCaret(doc.editor->document().end());
            doc.editor->insertText(text);
        }

        // An entry another session left for a tab's script.
        Entry require_left_for(const Doc& doc, const std::string& text, const std::string& session = "old-session")
        {
            ALRecoveryStore other(folder, session);
            Entry                 entry = ALScriptStudioRecovery::entryOf(doc);
            entry.text                  = text;
            entry.history               = LLSD();
            entry.historyWritten.clear();
            other.write(entry);
            // What another session left, as this one's store finds it.
            const std::optional<Entry> left = store->leftFor(entry.key);
            UNSCOPED_INFO("left by the other session");
            REQUIRE(left.has_value());
            return *left;
        }

        // An entry by its key alone, as another session may have left it.
        static Entry entry(const std::string& key, const std::string& text)
        {
            Entry one;
            one.key  = key;
            one.name = key;
            one.text = text;
            return one;
        }

        size_t keptFor(const std::string& key, const std::string& session = "this-session") const
        {
            size_t count = 0;
            for (const Entry& entry : store->list())
            {
                count += entry.key == key && entry.session == session && entry.state != Entry::State::Discarded;
            }
            return count;
        }
    };

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: restoreHistory brings back an entry's text and steps and caret", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r = make();
        Doc&                    a = tab("a", "default {}\n");
        type(a, "// one");
        type(a, " two");
        const Entry entry = ALScriptStudioRecovery::entryOf(a);
        CHECK(entry.text == "default {}\n// one two");
        CHECK(entry.key == a.recoveryKey);
        {
            INFO("where it came from");
            CHECK(entry.object == a.ref.object);
            CHECK(entry.item == a.ref.item);
            CHECK(entry.name == "a");
            CHECK_FALSE(entry.notecard);
        }
        CHECK(entry.caretLine == 1);
        CHECK(entry.caretColumn == 10);
        {
            INFO("and the steps, as the journal writes them");
            CHECK(entry.historyOf().isMap());
            REQUIRE_FALSE(entry.historyWritten.empty());
        }

        Doc& b = tab("b", "default {}\n", a.ref);
        CHECK(r.restoreHistory(b, entry));
        CHECK(b.editor->text() == entry.text);
        CHECK(b.editor->isDirty());
        CHECK(b.editor->caret().line == 1);
        CHECK(b.editor->caret().column == 10);
        b.editor->undo();
        {
            INFO("and the steps that led to it, to take back");
            CHECK(b.editor->text() != entry.text);
            CHECK(b.editor->text().starts_with("default {}\n"));
        }
        UNSCOPED_INFO("the tabs told");
        CHECK(studio().tabs == 1);

        type(b, "x");
        UNSCOPED_INFO("not over a tab holding something of its own");
        CHECK_FALSE(r.restoreHistory(b, entry));
        Doc&  c     = tab("c", "something else\n", a.ref);
        Entry wrong = entry;
        wrong.text  = "not where the steps lead\n";
        {
            INFO("nor where the history is not of its text");
            CHECK_FALSE(r.restoreHistory(c, wrong));
            CHECK(c.editor->text() == "something else\n");
        }
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: keep holds unsaved text and forgets it once the tab is clean", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r   = make();
        Doc&                    doc = tab("a", "x\n");
        {
            INFO("clean: nothing kept");
            CHECK(r.keep(doc));
            CHECK(keptFor(doc.recoveryKey) == 0);
        }
        type(doc, "y");
        CHECK(r.keep(doc));
        CHECK(keptFor(doc.recoveryKey) == 1);
        doc.editor->resetDirty();
        {
            INFO("clean again: forgotten");
            CHECK(r.keep(doc));
            CHECK(keptFor(doc.recoveryKey) == 0);
        }

        doc.recovering = require_left_for(doc, "an old session's");
        UNSCOPED_INFO("the old session's there");
        CHECK(keptFor(doc.recoveryKey, "old-session") == 1);
        type(doc, "z");
        r.keep(doc);
        {
            INFO("taken up, it is this session's alone");
            CHECK(keptFor(doc.recoveryKey, "old-session") == 0);
            CHECK(keptFor(doc.recoveryKey) == 1);
            REQUIRE_FALSE(doc.recovering);
        }

        Doc& loading   = tab("b", "q\n");
        type(loading, "w");
        loading.loaded = false;
        {
            INFO("nothing kept of a tab still loading");
            CHECK(r.keep(loading));
            CHECK(keptFor(loading.recoveryKey) == 0);
        }
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: a failing write is reported once whether now or from the store's thread", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r       = make();
        Doc&                    doc     = tab("a", "x\n");
        const std::string       blocked = folder + "/blocked";
        {
            llofstream file(blocked);
            file << "a file where a folder would be";
        }
        ALRecoveryStore nowhere(blocked, "this-session");
        ALRecovery::useStore(&nowhere);
        type(doc, "y");
        CHECK_FALSE(r.keep(doc));
        REQUIRE(services().reports.size() == 1);
        CHECK(services().reports[0].failure);
        CHECK(services().reports[0].doc == "a");
        CHECK(services().reports[0].text.find("a") != std::string::npos);
        type(doc, "z");
        {
            INFO("not said again");
            CHECK_FALSE(r.keep(doc));
            REQUIRE(services().reports.size() == 1);
        }
        ALRecovery::useStore(store.get());
        {
            INFO("kept, once it can be");
            REQUIRE(r.keep(doc));
            CHECK_FALSE(doc.recovery->failed);
        }

        ALRecovery::useStore(&nowhere);
        type(doc, "w");
        r.keepSoon(doc);
        nowhere.flush();
        r.keepSoon(doc);
        {
            INFO("from the store's thread, said once");
            REQUIRE(services().reports.size() == 2);
            CHECK(doc.recovery->failed);
        }
        nowhere.flush();
        r.keepSoon(doc);
        nowhere.flush();
        r.keepSoon(doc);
        UNSCOPED_INFO("and not again");
        REQUIRE(services().reports.size() == 2);
        ALRecovery::useStore(store.get());
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: takeUp waits for the tab to load and goes in as one step to undo", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r   = make();
        Doc&                    doc = tab("a", "old\n");
        const Entry             kept = require_left_for(doc, "kept\n");
        doc.loaded                   = false;
        r.takeUp(doc, kept);
        {
            INFO("waiting");
            CHECK(doc.carriedText == std::optional<std::string>("kept\n"));
            REQUIRE(doc.recovering);
            CHECK(doc.editor->text() == "old\n");
        }

        doc.loaded      = true;
        doc.carriedText.reset();
        doc.recovering.reset();
        r.takeUp(doc, kept);
        CHECK(doc.editor->text() == "kept\n");
        CHECK(doc.editor->isDirty());
        REQUIRE_FALSE(doc.carriedText);
        {
            INFO("kept by this session, the old entry let go of");
            CHECK(keptFor(doc.recoveryKey) == 1);
            CHECK(keptFor(doc.recoveryKey, "old-session") == 0);
        }
        UNSCOPED_INFO("the notice told");
        CHECK(studio().notices == 1);
        doc.editor->undo();
        UNSCOPED_INFO("one step to undo");
        CHECK(doc.editor->text() == "old\n");

        Doc& locked      = tab("b", "locked\n");
        locked.modifiable = false;
        r.takeUp(locked, require_left_for(locked, "kept too\n"));
        {
            INFO("held on its own");
            REQUIRE(studio().became.size() == 1);
            CHECK(studio().became[0] == std::make_pair(std::string("b"), Doc::Orphan::Locked));
        }
        {
            INFO("and said, with what can be done with it");
            REQUIRE(services().reports.size() == 1);
            REQUIRE(services().reports[0].actions.size() == 2);
        }
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: recover takes an entry to where it belongs or to a tab of its own", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r = make();
        Entry                   entry;
        entry.name   = "held";
        entry.object = LLUUID::generateNewID();
        entry.item   = LLUUID::generateNewID();
        entry.text   = "kept\n";

        studio().elsewhere = true;
        r.recover(entry);
        {
            INFO("in the window that has it");
            REQUIRE(studio().tookElsewhere.size() == 1);
            REQUIRE(studio().orphans.empty());
        }
        studio().elsewhere = false;

        Entry file = entry;
        file.object.setNull();
        file.item.setNull();
        file.file = folder + "/gone.lsl";
        file.name = "gone";
        r.recover(file);
        {
            INFO("a file gone, a tab of its own");
            REQUIRE(studio().orphans.size() == 1);
            CHECK(studio().orphans[0].second == Doc::Orphan::FileGone);
        }
        {
            llofstream there(folder + "/here.lsl");
            there << "on disk\n";
        }
        file.file         = folder + "/here.lsl";
        studio().opensFile = [this](const std::string& path) -> Doc* {
            Doc& doc   = tab("disk:" + path, "on disk\n");
            doc.file   = path;
            doc.loaded = false;
            return &doc;
        };
        r.recover(file);
        {
            INFO("a file there, opened and taken up");
            REQUIRE(studio().activated.size() == 1);
            const Doc* on_disk = services().findDoc("disk:" + file.file);
            REQUIRE(on_disk != nullptr);
            REQUIRE(on_disk->carriedText);
        }

        studio().inHand = false;
        r.recover(entry);
        {
            INFO("an object out of sight, a tab of its own");
            REQUIRE(studio().orphans.size() == 2);
            CHECK(studio().orphans[1].second == Doc::Orphan::Away);
        }
        Entry inventory = entry;
        inventory.object.setNull();
        r.recover(inventory);
        {
            INFO("an item gone from the inventory");
            REQUIRE(studio().orphans.size() == 3);
            CHECK(studio().orphans[2].second == Doc::Orphan::Removed);
        }

        studio().inHand          = true;
        services().whenOpened    = [this](const ALScriptRef& ref, const std::string&) {
            Doc& doc   = tab("opened", "server\n", ref);
            doc.loaded = false;
        };
        r.recover(entry);
        {
            INFO("its script opened, and the text to go in once loaded");
            REQUIRE(services().opened.size() == 1);
            const Doc* reopened = services().findDoc(ALScriptRef(entry.object, entry.item));
            REQUIRE(reopened != nullptr);
            CHECK(reopened->carriedText == std::optional<std::string>("kept\n"));
        }
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: show offers other sessions' and discarded entries to pick or drop", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r = make();
        r.show();
        {
            INFO("nothing to offer, said");
            REQUIRE(services().statuses.size() == 1);
            REQUIRE(studio().candidates.empty());
        }

        Doc& mine = tab("mine", "a\n");
        type(mine, "b");
        r.keep(mine);
        Doc& theirs = tab("theirs", "c\n");
        require_left_for(theirs, "left\n");
        Doc& gone = tab("gone", "d\n");
        type(gone, "e");
        CHECK(r.setAside(gone));
        r.show();
        UNSCOPED_INFO("theirs and what was set aside");
        REQUIRE(studio().candidates.size() == 2);
        std::string theirs_value, gone_value;
        for (const ALQuickOpen::Candidate& one : studio().candidates)
        {
            (one.label == "theirs" ? theirs_value : gone_value) = one.value;
        }
        {
            INFO("by name");
            REQUIRE_FALSE(theirs_value.empty());
            REQUIRE_FALSE(gone_value.empty());
        }

        studio().dropped(gone_value);
        bool still = false;
        for (const Entry& entry : store->list())
        {
            still |= entry.name == "gone";
        }
        UNSCOPED_INFO("thrown away already: gone for good");
        CHECK_FALSE(still);

        studio().dropped(theirs_value);
        bool discarded = false;
        for (const Entry& entry : store->list())
        {
            discarded |= entry.name == "theirs" && entry.state == Entry::State::Discarded;
        }
        UNSCOPED_INFO("thrown away");
        CHECK(discarded);

        theirs.carriedText.reset();
        theirs.loaded = false;
        // The offer made afresh, not the one before still standing.
        studio().candidates.clear();
        r.show();
        UNSCOPED_INFO("the one thrown away, offered still");
        REQUIRE(studio().candidates.size() == 1);
        studio().chosen(studio().candidates.front().value);
        UNSCOPED_INFO("picked, taken up");
        REQUIRE(theirs.carriedText.has_value());
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: schedule writes soon after typing began and forgets a clean tab", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r   = make();
        Doc&                    doc = tab("a", "x\n");
        type(doc, "y");
        r.schedule(doc);
        const F64 due = doc.recovery->due;
        UNSCOPED_INFO("due in a moment");
        CHECK(due > LLTimer::getTotalSeconds());
        type(doc, "z");
        r.schedule(doc);
        UNSCOPED_INFO("not put off by more typing");
        CHECK(doc.recovery->due == due);
        r.pump();
        UNSCOPED_INFO("not yet");
        CHECK(keptFor(doc.recoveryKey) == 0);
        doc.recovery->due = LLTimer::getTotalSeconds() - 1.0;
        r.pump();
        store->flush();
        {
            INFO("written");
            REQUIRE(keptFor(doc.recoveryKey) == 1);
            CHECK(doc.recovery->due == 0.0);
        }
        doc.editor->resetDirty();
        r.schedule(doc);
        UNSCOPED_INFO("clean: forgotten at once");
        CHECK(keptFor(doc.recoveryKey) == 0);
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: a closed window's unsaved tab is offered back when its script reopens", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r = make();
        const ALScriptRef       ref(LLUUID::generateNewID(), LLUUID::generateNewID());
        Doc&                    was = tab("a", "x\n", ref);
        type(was, "typed");
        // Its window going: its tabs written as they stand, as the window
        // does as it goes.
        store->write(ALScriptStudioRecovery::entryOf(was));

        // Held by a tab on its way here from another window: that tab's.
        Doc& moving = tab("b", "x\n", ref);
        r.offerFor(moving, /*held_elsewhere*/ true);
        {
            INFO("not offered while another tab holds it");
            REQUIRE_FALSE(moving.recoverable);
            CHECK(keptFor(was.recoveryKey) == 1);
        }

        Doc& again = tab("c", "x\n", ref);
        r.offerFor(again, false);
        {
            INFO("offered back");
            REQUIRE(again.recoverable);
            CHECK(again.recoverable->text == "x\ntyped");
        }
        UNSCOPED_INFO("set aside, out of the way");
        CHECK(keptFor(again.recoveryKey) == 0);
        // Loaded clean, as a tab opened again is: what it keeps of its own
        // forgets nothing of that.
        r.keep(again);
        {
            INFO("still there to take");
            REQUIRE(again.recoverable);
            CHECK(std::filesystem::exists(fsyspath(again.recoverable->path)));
        }
        const Entry offered = *again.recoverable;
        r.takeUp(again, offered);
        UNSCOPED_INFO("taken up");
        CHECK(again.editor->text() == std::string("x\ntyped"));
        {
            INFO("its own from here, the set-aside one let go of");
            CHECK(keptFor(again.recoveryKey) == 1);
            CHECK_FALSE(std::filesystem::exists(fsyspath(offered.path)));
        }
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: keepAll writes each unsaved tab and reports each failure", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r     = make();
        Doc&                    a     = tab("a", "one\n");
        Doc&                    b     = tab("b", "two\n");
        Doc&                    clean = tab("c", "three\n");
        type(a, "x");
        type(b, "y");
        CHECK(r.keepAll({ &a, &b, &clean }, Entry::State::Kept));
        {
            INFO("both written");
            CHECK(keptFor(a.recoveryKey) == 1);
            CHECK(keptFor(b.recoveryKey) == 1);
        }
        UNSCOPED_INFO("and nothing of the clean one");
        CHECK(keptFor(clean.recoveryKey) == 0);
        const std::vector<Entry> all = store->list();
        UNSCOPED_INFO("kept for next time");
        CHECK(std::all_of(all.begin(), all.end(), [](const Entry& one) { return one.state == Entry::State::Kept; }));
        UNSCOPED_INFO("nothing said");
        REQUIRE(services().reports.empty());

        const std::string blocked = folder + "/blocked";
        {
            llofstream file(blocked);
            file << "a file where a folder would be";
        }
        ALRecoveryStore nowhere(blocked, "this-session");
        ALRecovery::useStore(&nowhere);
        type(a, "z");
        type(b, "w");
        CHECK_FALSE(r.keepAll({ &a, &b }));
        {
            INFO("each said once");
            REQUIRE(services().reports.size() == 2);
            CHECK(a.recovery->failed);
            CHECK(b.recovery->failed);
        }
        ALRecovery::useStore(store.get());
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: keep writes again only when the text or its history has moved", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r = make();
        Doc&                    a = tab("a", "one\n");
        type(a, "x");
        CHECK(r.keep(a));
        CHECK(keptFor(a.recoveryKey) == 1);
        // Taken away behind its back: not written again while nothing moved.
        for (const Entry& entry : store->list())
        {
            store->remove(entry);
        }
        {
            INFO("kept again, as nothing moved");
            CHECK(r.keep(a));
            CHECK(keptFor(a.recoveryKey) == 0);
        }
        r.keepSoon(a);
        store->flush();
        UNSCOPED_INFO("nor as typing asks");
        CHECK(keptFor(a.recoveryKey) == 0);
        a.editor->undoJournal().label("Named");
        r.keepSoon(a);
        store->flush();
        UNSCOPED_INFO("its history moved: written");
        CHECK(keptFor(a.recoveryKey) == 1);
        for (const Entry& entry : store->list())
        {
            store->remove(entry);
        }
        {
            INFO("kept for next time is more than was asked");
            CHECK(r.keep(a, Entry::State::Kept));
            CHECK(keptFor(a.recoveryKey) == 1);
        }
        for (const Entry& entry : store->list())
        {
            store->remove(entry);
        }
        type(a, "y");
        {
            INFO("typed in: written");
            CHECK(r.keep(a));
            CHECK(keptFor(a.recoveryKey) == 1);
        }
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: at login kept entries are offered only while the studio is closed", "[alscriptstudiorecovery]")
    {
        make();
        ALRecoveryStore earlier(folder, "earlier");
        Entry                 lost = entry("item:lost", "typed, then a crash");
        Entry                 kept = entry("item:kept", "kept for next time");
        kept.state                 = Entry::State::Kept;
        CHECK(earlier.write(lost));
        CHECK(earlier.write(kept));
        const ALRecovery::Offers closed = ALRecovery::offersAt(*store, false);
        {
            INFO("both offered");
            REQUIRE(closed.unsaved.size() == 1);
            REQUIRE(closed.kept.size() == 1);
            CHECK(closed.kept.front().key == "item:kept");
        }
        const ALRecovery::Offers open = ALRecovery::offersAt(*store, true);
        {
            INFO("the kept not, the studio open having opened it");
            REQUIRE(open.unsaved.size() == 1);
            REQUIRE(open.kept.empty());
        }
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: a picked target and experience are kept and picked again on takeUp", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r          = make();
        Doc&                    a          = tab("a", "one\n");
        const LLUUID            experience = LLUUID::generateNewID();
        a.language.compileTarget           = "lsl2";
        a.targetChosen                     = true;
        a.experience                       = experience;
        a.experienceChosen                 = true;
        {
            INFO("a pick alone is unsaved, and kept");
            CHECK(r.keep(a));
            CHECK(keptFor(a.recoveryKey) == 1);
        }
        type(a, "x");
        const Entry written = ALScriptStudioRecovery::entryOf(a);
        {
            INFO("the picks with the entry");
            CHECK(written.pickedTarget == std::optional<std::string>("lsl2"));
            CHECK(written.pickedExperience == std::optional<LLUUID>(experience));
        }
        CHECK(r.keep(a));
        std::optional<Entry> back = store->leftFor(a.recoveryKey);
        UNSCOPED_INFO("not another session's");
        REQUIRE_FALSE(back);
        const std::vector<Entry> listed = store->list();
        REQUIRE(listed.size() == 1);
        Entry read = listed.front();
        {
            INFO("read whole, the picks with it");
            CHECK(store->load(read));
            CHECK(read.pickedTarget == written.pickedTarget);
            CHECK(read.pickedExperience == written.pickedExperience);
        }

        Doc& loaded = tab("b", "one\n", a.ref);
        r.takeUp(loaded, read);
        {
            INFO("picked again over a tab that has loaded");
            CHECK(loaded.targetChosen);
            CHECK(loaded.language.compileTarget == "lsl2");
            CHECK(loaded.experienceChosen);
            CHECK(loaded.experience == experience);
        }
        Doc& loading   = tab("c", "one\n");
        loading.loaded = false;
        r.takeUp(loading, read);
        {
            INFO("carried to be picked as a tab loading loads");
            CHECK(loading.carriedTarget == std::optional<std::string>("lsl2"));
            CHECK(loading.carriedExperience == std::optional<LLUUID>(experience));
            CHECK_FALSE(loading.targetChosen);
        }
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: a notecard window's entry goes back to it unless it cannot take it", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r = make();
        Entry                   card;
        card.name     = "card";
        card.object   = LLUUID::generateNewID();
        card.item     = LLUUID::generateNewID();
        card.notecard = true;
        card.text     = "kept\n";
        card.key      = ALRecoveryStore::windowKeyOf(card.object, card.item);
        {
            INFO("a key of its own");
            CHECK(ALRecoveryStore::isWindowKey(card.key));
            CHECK(card.key != ALRecoveryStore::keyOf(card.object, card.item, std::string()));
            CHECK_FALSE(ALRecoveryStore::isWindowKey(ALRecoveryStore::keyOf(card.object, card.item, std::string())));
        }

        std::vector<std::string> taken;
        bool                     has = true;
        // The windows' taker is the process's, and holds this case's own:
        // let go of however this ends.
        struct TakerBack
        {
            ~TakerBack() { ALRecovery::takeWindowsTo(nullptr); }
        } taker_back;
        ALRecovery::takeWindowsTo([&taken, &has](const Entry& entry) {
            taken.push_back(entry.name);
            return has;
        });
        studio().inHand = false;
        r.recover(card);
        {
            INFO("taken up in the window");
            REQUIRE(taken.size() == 1);
            REQUIRE(studio().orphans.empty());
            REQUIRE(services().opened.empty());
        }

        // A studio tab's own entry for the same notecard is the studio's.
        Entry tab = card;
        tab.key   = ALRecoveryStore::keyOf(card.object, card.item, std::string());
        r.recover(tab);
        {
            INFO("a tab's entry is not the window's");
            REQUIRE(taken.size() == 1);
            REQUIRE(studio().orphans.size() == 1);
        }

        has = false;
        r.recover(card);
        {
            INFO("the window cannot have it: the studio keeps it");
            REQUIRE(taken.size() == 2);
            REQUIRE(studio().orphans.size() == 2);
        }
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: rekey lets go of the old key's entry and writes under the new one", "[alscriptstudiorecovery]")
    {
        ALScriptStudioRecovery& r = make();
        Doc&                    a = tab("a", "one\n");
        type(a, "x");
        CHECK(r.keep(a));
        CHECK(keptFor(a.recoveryKey) == 1);
        const std::string was = a.recoveryKey;
        ALScriptStudioRecovery::rekey(a, ALRecoveryStore::keyOf(LLUUID::null, LLUUID::null, "/somewhere/a.lsl"));
        store->flush();
        {
            INFO("the old let go of");
            CHECK(a.recoveryKey != was);
            CHECK(keptFor(was) == 0);
        }
        {
            INFO("written afresh, though nothing moved");
            CHECK(r.keep(a));
            CHECK(keptFor(a.recoveryKey) == 1);
        }
    }

    TEST_CASE_METHOD(alscriptstudiorecovery_data, "alscriptstudiorecovery: keepSaved records on the writer's thread and moves saves to a new item", "[alscriptstudiorecovery]")
    {
        const std::string folder =
            fsyspath(std::filesystem::temp_directory_path() / fsyspath("alsavehistory_" + LLUUID::generateNewID().asString())).string();
        auto history = std::make_shared<ALSaveHistory>(folder);
        // The history is the process's: the one before put back, and the
        // folder taken out of the way, however this ends.
        struct HistoryBack
        {
            std::shared_ptr<ALSaveHistory> was;
            std::string                    folder;
            ~HistoryBack()
            {
                ALRecovery::useHistory(was);
                std::error_code ec;
                std::filesystem::remove_all(fsyspath(folder), ec);
            }
        } history_back{ ALRecovery::history(), folder };
        ALRecovery::useHistory(history);
        // Everything posted before this, done: the writer is one thread.
        const auto written = []() {
            std::promise<void> done;
            std::future<void>  waited = done.get_future();
            if (ALRecoveryWriter::instance().post([&done]() { done.set_value(); }))
            {
                waited.wait();
            }
        };
        const F64 now  = LLDate::now().secondsSinceEpoch();
        auto      save = [now](const std::string& key, const std::string& text, F64 ago) {
            ALSavedText one;
            one.key  = key;
            one.text = text;
            one.when = LLDate(now - ago);
            return one;
        };
        ALRecovery::keepSaved(save("item:x", "one", 30.0));
        ALRecovery::keepSaved(save("task:p:old", "a", 20.0));
        ALRecovery::keepSaved(save("task:p:new", "b", 10.0), "task:p:old");
        written();
        REQUIRE(history->list("item:x").size() == 1U);
        UNSCOPED_INFO("the old item's moved");
        REQUIRE(history->list("task:p:old").empty());
        const std::vector<ALSavedText> moved = history->list("task:p:new");
        REQUIRE(moved.size() == 2U);
        {
            // Newest first: the new item's own save, then the old item's.
            INFO("behind the new one's");
            ALSavedText newer = moved[0];
            ALSavedText older = moved[1];
            REQUIRE(history->load(newer));
            REQUIRE(history->load(older));
            CHECK(newer.text == "b");
            CHECK(older.text == "a");
        }
        // Each item's saves are a folder of the history's.
        const auto folders = [&folder]() {
            std::error_code ec;
            return std::distance(std::filesystem::directory_iterator(fsyspath(folder), ec), std::filesystem::directory_iterator());
        };
        const auto before = folders();
        ALRecovery::keepSaved(save("", "nobody's", 5.0));
        written();
        UNSCOPED_INFO("a save of nobody's kept nowhere");
        CHECK(folders() == before);
    }
}
