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

#include "alscriptstudio_fixture.h"

#include "fsyspath.h"
#include "lltimer.h"

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>

namespace
{
    typedef ALScriptStudioRecovery::Entry Entry;
    typedef ALScriptStudioDoc             Doc;

    // The window, faked: a record of what recovery asked of it.
    struct FakeRecoveryWindow : public ALScriptStudioRecovery::Window
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
        Doc::Orphan failedAs(const Doc& doc, ALScriptWorkspace::Loaded::Failure failure) const override
        {
            return failure == ALScriptWorkspace::Loaded::Failure::NotPermitted ? Doc::Orphan::Locked : Doc::Orphan::Unloaded;
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
        void tabsChanged() override { ++tabs; }
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

namespace tut
{
    struct alscriptstudiorecovery_data
    {
        al_studio_test::StudioWindowOf<FakeRecoveryWindow> window;
        std::unique_ptr<ALScriptStudioRecovery>             recovery;
        std::string                                         folder;
        std::unique_ptr<ALScriptRecoveryStore>              store;

        ~alscriptstudiorecovery_data()
        {
            ALScriptStudioRecovery::useStore(nullptr);
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            recovery          = std::make_unique<ALScriptStudioRecovery>(services(), studio());
            studio().recovery = recovery.get();
            folder = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alscriptstudiorecovery_" + LLUUID::generateNewID().asString())).string();
            std::filesystem::create_directories(fsyspath(folder));
            store = std::make_unique<ALScriptRecoveryStore>(folder, "this-session");
            ALScriptStudioRecovery::useStore(store.get());
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
            doc.recoveryKey = ALScriptRecoveryStore::keyOf(ref.object, ref.item, std::string());
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
        Entry leftFor(const Doc& doc, const std::string& text, const std::string& session = "old-session")
        {
            ALScriptRecoveryStore other(folder, session);
            Entry                 entry = ALScriptStudioRecovery::entryOf(doc);
            entry.text                  = text;
            entry.history               = LLSD();
            entry.historyWritten.clear();
            other.write(entry);
            // What another session left, as this one's store finds it.
            const std::optional<Entry> left = store->leftFor(entry.key);
            ensure("left by the other session", left.has_value());
            return *left;
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
    typedef test_group<alscriptstudiorecovery_data> alscriptstudiorecovery_group;
    typedef alscriptstudiorecovery_group::object    alscriptstudiorecovery_object;
    tut::alscriptstudiorecovery_group               alscriptstudiorecovery_instance("alscriptstudiorecovery");

    template<> template<>
    void alscriptstudiorecovery_object::test<1>()
    {
        set_test_name("an entry is the tab as it stands, and put back over a tab of its script holding nothing of its own, brings its text, its history and its caret");
        ALScriptStudioRecovery& r = make();
        Doc&                    a = tab("a", "default {}\n");
        type(a, "// one");
        type(a, " two");
        const Entry entry = ALScriptStudioRecovery::entryOf(a);
        ensure("the text", entry.text == "default {}\n// one two" && entry.key == a.recoveryKey);
        ensure("where it came from", entry.object == a.ref.object && entry.item == a.ref.item && entry.name == "a" && !entry.notecard);
        ensure("the caret", entry.caretLine == 1 && entry.caretColumn == 10);
        ensure("and the steps, as the journal writes them", entry.historyOf().isMap() && !entry.historyWritten.empty());

        Doc& b = tab("b", "default {}\n", a.ref);
        ensure("put back", r.restoreHistory(b, entry));
        ensure("its text", b.editor->text() == entry.text && b.editor->isDirty());
        ensure("its caret", b.editor->caret().line == 1 && b.editor->caret().column == 10);
        b.editor->undo();
        ensure("and the steps that led to it, to take back", b.editor->text() != entry.text && b.editor->text().starts_with("default {}\n"));
        ensure("the tabs told", studio().tabs == 1);

        type(b, "x");
        ensure("not over a tab holding something of its own", !r.restoreHistory(b, entry));
        Doc&  c     = tab("c", "something else\n", a.ref);
        Entry wrong = entry;
        wrong.text  = "not where the steps lead\n";
        ensure("nor where the history is not of its text", !r.restoreHistory(c, wrong) && c.editor->text() == "something else\n");
    }

    template<> template<>
    void alscriptstudiorecovery_object::test<2>()
    {
        set_test_name("kept while unsaved and forgotten once clean; what a tab took up is its own to keep from then on");
        ALScriptStudioRecovery& r   = make();
        Doc&                    doc = tab("a", "x\n");
        ensure("clean: nothing kept", r.keep(doc) && keptFor(doc.recoveryKey) == 0);
        type(doc, "y");
        ensure("kept", r.keep(doc) && keptFor(doc.recoveryKey) == 1);
        doc.editor->resetDirty();
        ensure("clean again: forgotten", r.keep(doc) && keptFor(doc.recoveryKey) == 0);

        doc.recovering = leftFor(doc, "an old session's");
        ensure("the old session's there", keptFor(doc.recoveryKey, "old-session") == 1);
        type(doc, "z");
        r.keep(doc);
        ensure("taken up, it is this session's alone", keptFor(doc.recoveryKey, "old-session") == 0 && keptFor(doc.recoveryKey) == 1 && !doc.recovering);

        Doc& loading   = tab("b", "q\n");
        type(loading, "w");
        loading.loaded = false;
        ensure("nothing kept of a tab still loading", r.keep(loading) && keptFor(loading.recoveryKey) == 0);
    }

    template<> template<>
    void alscriptstudiorecovery_object::test<3>()
    {
        set_test_name("a write that fails is said once, not at every pause in typing, whether now or on the store's thread; one that works again may say it again");
        ALScriptStudioRecovery& r       = make();
        Doc&                    doc     = tab("a", "x\n");
        const std::string       blocked = folder + "/blocked";
        {
            llofstream file(blocked);
            file << "a file where a folder would be";
        }
        ALScriptRecoveryStore nowhere(blocked, "this-session");
        ALScriptStudioRecovery::useStore(&nowhere);
        type(doc, "y");
        ensure("not kept", !r.keep(doc));
        ensure("said", services().reports.size() == 1 && services().reports[0].failure && services().reports[0].doc == "a" &&
                           services().reports[0].text.find("a") != std::string::npos);
        type(doc, "z");
        ensure("not said again", !r.keep(doc) && services().reports.size() == 1);
        ALScriptStudioRecovery::useStore(store.get());
        ensure("kept, once it can be", r.keep(doc) && !doc.recoveryFailed);

        ALScriptStudioRecovery::useStore(&nowhere);
        type(doc, "w");
        r.keepSoon(doc);
        nowhere.flush();
        r.keepSoon(doc);
        ensure("from the store's thread, said once", services().reports.size() == 2 && doc.recoveryFailed);
        nowhere.flush();
        r.keepSoon(doc);
        nowhere.flush();
        r.keepSoon(doc);
        ensure("and not again", services().reports.size() == 2);
        ALScriptStudioRecovery::useStore(store.get());
    }

    template<> template<>
    void alscriptstudiorecovery_object::test<4>()
    {
        set_test_name("a kept text taken up waits for its tab to load, then goes in as one step to undo; over a tab nothing can be saved from, the tab holds it on its own");
        ALScriptStudioRecovery& r   = make();
        Doc&                    doc = tab("a", "old\n");
        const Entry             kept = leftFor(doc, "kept\n");
        doc.loaded                   = false;
        r.takeUp(doc, kept);
        ensure("waiting", doc.carriedText == std::optional<std::string>("kept\n") && doc.recovering && doc.editor->text() == "old\n");

        doc.loaded      = true;
        doc.carriedText.reset();
        doc.recovering.reset();
        r.takeUp(doc, kept);
        ensure("in", doc.editor->text() == "kept\n" && doc.editor->isDirty() && !doc.carriedText);
        ensure("kept by this session, the old entry let go of", keptFor(doc.recoveryKey) == 1 && keptFor(doc.recoveryKey, "old-session") == 0);
        ensure("the notice told", studio().notices == 1);
        doc.editor->undo();
        ensure("one step to undo", doc.editor->text() == "old\n");

        Doc& locked      = tab("b", "locked\n");
        locked.modifiable = false;
        r.takeUp(locked, leftFor(locked, "kept too\n"));
        ensure("held on its own", studio().became.size() == 1 && studio().became[0] == std::make_pair(std::string("b"), Doc::Orphan::Locked));
        ensure("and said, with what can be done with it", services().reports.size() == 1 && services().reports[0].actions.size() == 2);
    }

    template<> template<>
    void alscriptstudiorecovery_object::test<5>()
    {
        set_test_name("recovered where it belongs: another window that has it; its file, or a tab of its own where the file is gone; its script where it can be had, or a tab of its own");
        ALScriptStudioRecovery& r = make();
        Entry                   entry;
        entry.name   = "held";
        entry.object = LLUUID::generateNewID();
        entry.item   = LLUUID::generateNewID();
        entry.text   = "kept\n";

        studio().elsewhere = true;
        r.recover(entry);
        ensure("in the window that has it", studio().tookElsewhere.size() == 1 && studio().orphans.empty());
        studio().elsewhere = false;

        Entry file = entry;
        file.object.setNull();
        file.item.setNull();
        file.file = folder + "/gone.lsl";
        file.name = "gone";
        r.recover(file);
        ensure("a file gone, a tab of its own", studio().orphans.size() == 1 && studio().orphans[0].second == Doc::Orphan::FileGone);
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
        ensure("a file there, opened and taken up", studio().activated.size() == 1 && services().findDoc("disk:" + file.file)->carriedText);

        studio().inHand = false;
        r.recover(entry);
        ensure("an object out of sight, a tab of its own", studio().orphans.size() == 2 && studio().orphans[1].second == Doc::Orphan::Away);
        Entry inventory = entry;
        inventory.object.setNull();
        r.recover(inventory);
        ensure("an item gone from the inventory", studio().orphans.size() == 3 && studio().orphans[2].second == Doc::Orphan::Removed);

        studio().inHand          = true;
        services().whenOpened    = [this](const ALScriptRef& ref, const std::string&) {
            Doc& doc   = tab("opened", "server\n", ref);
            doc.loaded = false;
        };
        r.recover(entry);
        ensure("its script opened, and the text to go in once loaded",
               services().opened.size() == 1 && services().findDoc(ALScriptRef(entry.object, entry.item))->carriedText == std::optional<std::string>("kept\n"));
    }

    template<> template<>
    void alscriptstudiorecovery_object::test<6>()
    {
        set_test_name("Recover Unsaved Changes offers what other sessions left and what was thrown away, not this session's own; picked, taken up; Shift-Return throws one away, and one thrown away already goes for good");
        ALScriptStudioRecovery& r = make();
        r.show();
        ensure("nothing to offer, said", services().statuses.size() == 1 && studio().candidates.empty());

        Doc& mine = tab("mine", "a\n");
        type(mine, "b");
        r.keep(mine);
        Doc&        theirs = tab("theirs", "c\n");
        const Entry left   = leftFor(theirs, "left\n");
        Doc&        gone   = tab("gone", "d\n");
        type(gone, "e");
        ensure("set aside", r.setAside(gone));
        r.show();
        ensure("theirs and what was set aside", studio().candidates.size() == 2);
        std::string theirs_value, gone_value;
        for (const ALQuickOpen::Candidate& one : studio().candidates)
        {
            (one.label == "theirs" ? theirs_value : gone_value) = one.value;
        }
        ensure("by name", !theirs_value.empty() && !gone_value.empty());

        studio().dropped(gone_value);
        bool still = false;
        for (const Entry& entry : store->list())
        {
            still |= entry.name == "gone";
        }
        ensure("thrown away already: gone for good", !still);

        studio().dropped(theirs_value);
        bool discarded = false;
        for (const Entry& entry : store->list())
        {
            discarded |= entry.name == "theirs" && entry.state == Entry::State::Discarded;
        }
        ensure("thrown away", discarded);

        theirs.carriedText.reset();
        theirs.loaded = false;
        r.show();
        studio().chosen(studio().candidates.front().value);
        ensure("picked, taken up", theirs.carriedText.has_value());
    }

    template<> template<>
    void alscriptstudiorecovery_object::test<7>()
    {
        set_test_name("typing is written a moment after it began, whatever is typed meanwhile; a tab gone clean is forgotten at once");
        ALScriptStudioRecovery& r   = make();
        Doc&                    doc = tab("a", "x\n");
        type(doc, "y");
        r.schedule(doc);
        const F64 due = doc.recoveryDue;
        ensure("due in a moment", due > LLTimer::getTotalSeconds());
        type(doc, "z");
        r.schedule(doc);
        ensure("not put off by more typing", doc.recoveryDue == due);
        r.pump();
        ensure("not yet", keptFor(doc.recoveryKey) == 0);
        doc.recoveryDue = LLTimer::getTotalSeconds() - 1.0;
        r.pump();
        store->flush();
        ensure("written", keptFor(doc.recoveryKey) == 1 && doc.recoveryDue == 0.0);
        doc.editor->resetDirty();
        r.schedule(doc);
        ensure("clean: forgotten at once", keptFor(doc.recoveryKey) == 0);
    }

    template<> template<>
    void alscriptstudiorecovery_object::test<8>()
    {
        set_test_name("a tab whose window went with it unsaved is offered back as its script opens again this session, and not while another tab holds it");
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
        ensure("not offered while another tab holds it", !moving.recoverable && keptFor(was.recoveryKey) == 1);

        Doc& again = tab("c", "x\n", ref);
        r.offerFor(again, false);
        ensure("offered back", again.recoverable && again.recoverable->text == "x\ntyped");
        ensure("set aside, out of the way", keptFor(again.recoveryKey) == 0);
        // Loaded clean, as a tab opened again is: what it keeps of its own
        // forgets nothing of that.
        r.keep(again);
        ensure("still there to take", std::filesystem::exists(fsyspath(again.recoverable->path)));
        const Entry offered = *again.recoverable;
        r.takeUp(again, offered);
        ensure_equals("taken up", again.editor->text(), std::string("x\ntyped"));
        ensure("its own from here, the set-aside one let go of", keptFor(again.recoveryKey) == 1 && !std::filesystem::exists(fsyspath(offered.path)));
    }
}
