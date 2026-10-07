/**
 * @file almasterindex_test.cpp
 * @brief One account's links of scripts to the files on disk that master them, kept in a file.
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

#include "../masters/almasterindex.h"

#include "aldiskincludes.h"
#include "alfilewrite.h"
#include "aluploadheader.h"
#include "fsyspath.h"
#include "llfile.h"
#include "llsdserialize.h"

#include "../test/lltut.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace tut
{
    struct almasterindex_data
    {
        // A callable waiting for the test's own time to come.
        struct Waiting
        {
            F64                   at = 0.0;
            std::function<void()> callable;
        };

        std::string                    folder;
        std::string                    file;
        // The test's time, in seconds, and what waits on it.
        F64                            time = 1000.0;
        std::vector<Waiting>           waiting;
        std::unique_ptr<ALMasterIndex> index;
        // How often whoever listens was told.
        S32                            told = 0;
        boost::signals2::scoped_connection listening;

        almasterindex_data()
        {
            folder = fsyspath(std::filesystem::temp_directory_path() / fsyspath("almasterindex_" + LLUUID::generateNewID().asString())).string();
            std::error_code ec;
            std::filesystem::create_directories(fsyspath(folder), ec);
            file = in("script_masters.llsd");
        }
        ~almasterindex_data()
        {
            listening.disconnect();
            index.reset();
            std::error_code ec;
            std::filesystem::remove_all(fsyspath(folder), ec);
        }

        std::string in(const std::string& name) const { return fsyspath(fsyspath(folder) / fsyspath(name)).string(); }

        // The index's clock: the test's time, and what is to be run when.
        ALMasterIndex::Clock clock()
        {
            ALMasterIndex::Clock out;
            out.after = [this](std::function<void()> callable, F32 seconds) { waiting.push_back({ time + seconds, std::move(callable) }); };
            out.now   = [this]() { return time; };
            return out;
        }

        // The index of a file, made over the test's clock, its changes told.
        ALMasterIndex& open(const std::string& path)
        {
            listening.disconnect();
            index.reset();
            index     = std::make_unique<ALMasterIndex>(path, clock());
            listening = index->onChanged([this]() { ++told; });
            return *index;
        }
        ALMasterIndex& open() { return open(file); }

        // The time moved on, what falls due meanwhile run as it falls due,
        // the soonest first.
        void pass(F64 seconds)
        {
            const F64 until = time + seconds;
            for (;;)
            {
                const auto next = std::min_element(waiting.begin(), waiting.end(),
                                                   [](const Waiting& a, const Waiting& b) { return a.at < b.at; });
                if (next == waiting.end() || next->at > until)
                {
                    break;
                }
                time                           = std::max(time, next->at);
                const std::function<void()> run = std::move(next->callable);
                waiting.erase(next);
                run();
            }
            time = until;
        }

        static LLUUID key(int n)
        {
            char text[37];
            snprintf(text, sizeof(text), "%08x-0000-4000-8000-000000000000", n);
            return LLUUID(text);
        }

        static ALMasterLink link(int object, int item, const std::string& master)
        {
            ALMasterLink out;
            out.object = object ? almasterindex_data::key(object) : LLUUID::null;
            out.item   = almasterindex_data::key(item);
            out.master = master;
            out.target = "mono";
            return out;
        }

        // The file as it is on disk, or nothing; and the links it holds.
        std::string text() const
        {
            std::string out;
            ALFileRead::whole(file, out, ALDiskIncludes::MAX_BYTES);
            return out;
        }
        ALMasterLinks onDisk() const { return onDisk(file); }
        static ALMasterLinks onDisk(const std::string& path)
        {
            std::string body;
            if (!ALFileRead::whole(path, body, ALDiskIncludes::MAX_BYTES))
            {
                return ALMasterLinks();
            }
            LLSD               llsd;
            std::istringstream stream(body);
            LLSDSerialize::fromXML(llsd, stream);
            return ALMasterLinks::fromLLSD(llsd);
        }
        static void put(const std::string& path, const std::string& body)
        {
            llofstream out(fsyspath(path), std::ios::binary | std::ios::trunc);
            out << body;
        }
    };

    typedef test_group<almasterindex_data> almasterindex_group;
    typedef almasterindex_group::object    almasterindex_object;
    almasterindex_group                    almasterindex_instance("almasterindex");

    template<> template<>
    void almasterindex_object::test<1>()
    {
        set_test_name("links go through the file and come back as they went");
        ALMasterIndex& made = open();
        ensure("none at first", made.empty());
        ALMasterLink door = almasterindex_data::link(1, 2, "/s/door.lsl");
        door.uses         = { "disk:/s/util.lsl" };
        door.hash         = "abc";
        door.stamp        = 1234;
        door.base         = almasterindex_data::key(9);
        door.itemName     = "Door";
        ALMasterLink card = almasterindex_data::link(0, 3, "/s/card.txt");
        card.notecard     = true;
        made.link({ door, card });
        made.flush();
        ensure("written", !made.dirty());

        ALMasterIndex& back = open();
        ensure_equals("both back", back.size(), size_t(2));
        const std::optional<ALMasterLink> read = back.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        ensure("the door's", read.has_value());
        ensure_equals("its master", read->master, std::string("/s/door.lsl"));
        ensure_equals("its hash", read->hash, std::string("abc"));
        ensure_equals("its stamp", read->stamp, S64(1234));
        ensure_equals("its base", read->base, almasterindex_data::key(9));
        ensure_equals("its uses", read->uses.size(), size_t(1));
        ensure_equals("its name", read->itemName, std::string("Door"));
        ensure("the card's, a notecard's", back.linkOf(LLUUID::null, almasterindex_data::key(3))->notecard);
        ensure_equals("found by its master", back.mastering("/s/door.lsl").size(), size_t(1));
        ensure_equals("and by what it read", back.affectedBy("disk:/s/util.lsl").size(), size_t(1));
        ensure_equals("in its object", back.linksIn(almasterindex_data::key(1)).size(), size_t(1));
        ensure_equals("in the inventory", back.linksIn(LLUUID::null).size(), size_t(1));
        ensure("read, nothing changed", !back.dirty());
    }

    template<> template<>
    void almasterindex_object::test<2>()
    {
        set_test_name("a file that will not read is no links, and is not written over until a link changes");
        const std::string garbage = "this is not <llsd> at all";
        almasterindex_data::put(file, garbage);
        {
            ALMasterIndex& looked = open();
            ensure("no links", looked.empty());
            ensure("nothing to write", !looked.dirty());
            pass(10.0);
            index.reset();
        }
        ensure_equals("left as it was", text(), garbage);

        ALMasterIndex& changed = open();
        changed.link(almasterindex_data::link(1, 2, "/s/door.lsl"));
        index.reset();
        ensure_equals("a link written over it", onDisk().size(), size_t(1));
    }

    template<> template<>
    void almasterindex_object::test<3>()
    {
        set_test_name("links orphaned long ago are let go of as the file is read, and that is written");
        ALMasterLinks links;
        ALMasterLink  old     = almasterindex_data::link(1, 1, "/s/old.lsl");
        old.state             = ALMasterLink::State::Orphaned;
        old.orphanedSince     = LLDate(LLDate::now().secondsSinceEpoch() - 100.0 * 86400.0);
        ALMasterLink recent   = almasterindex_data::link(1, 2, "/s/recent.lsl");
        recent.state          = ALMasterLink::State::Orphaned;
        recent.orphanedSince  = LLDate(LLDate::now().secondsSinceEpoch() - 10.0 * 86400.0);
        links.put(old);
        links.put(recent);
        links.put(almasterindex_data::link(1, 3, "/s/live.lsl"));
        std::ostringstream out;
        LLSDSerialize::toPrettyXML(links.toLLSD(), out);
        almasterindex_data::put(file, out.str());

        ALMasterIndex& read = open();
        ensure_equals("the old one gone", read.size(), size_t(2));
        ensure("not the recent one", read.linkOf(almasterindex_data::key(1), almasterindex_data::key(2)).has_value());
        ensure("to be written", read.dirty());
        ensure_equals("nobody told: nobody listened yet", told, 0);
        index.reset();
        ensure_equals("written without it", onDisk().size(), size_t(2));
    }

    template<> template<>
    void almasterindex_object::test<4>()
    {
        set_test_name("links made together are told once, and one alone once");
        ALMasterIndex& made = open();
        made.link({ almasterindex_data::link(1, 1, "/s/a.lsl"), almasterindex_data::link(1, 2, "/s/b.lsl"), almasterindex_data::link(2, 3, "/s/c.lsl") });
        ensure_equals("three", made.size(), size_t(3));
        ensure_equals("told once", told, 1);
        made.link(almasterindex_data::link(2, 4, "/s/d.lsl"));
        ensure_equals("and once more", told, 2);
        made.link(std::vector<ALMasterLink>());
        ensure_equals("none, not told", told, 2);
        made.link(almasterindex_data::link(1, 1, "/s/a2.lsl"));
        ensure_equals("put over, not beside", made.size(), size_t(4));
        ensure_equals("the new master", made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1))->master, std::string("/s/a2.lsl"));
    }

    template<> template<>
    void almasterindex_object::test<5>()
    {
        set_test_name("a link let go of, and links marked pending, those linked only");
        ALMasterIndex& made = open();
        made.link({ almasterindex_data::link(1, 1, "/s/a.lsl"), almasterindex_data::link(1, 2, "/s/b.lsl") });
        told = 0;
        ensure("let go of", made.unlink(almasterindex_data::key(1), almasterindex_data::key(1)));
        ensure_equals("told", told, 1);
        ensure("gone", !made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1)));
        ensure("one not linked is not", !made.unlink(almasterindex_data::key(1), almasterindex_data::key(1)));
        ensure_equals("and not told", told, 1);

        const size_t marked = made.markPending({ { almasterindex_data::key(1), almasterindex_data::key(1) }, { almasterindex_data::key(1), almasterindex_data::key(2) } });
        ensure_equals("the one linked", marked, size_t(1));
        ensure("pending", made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2))->state == ALMasterLink::State::Pending);
        ensure_equals("told", told, 2);
        ensure_equals("none linked, none marked", made.markPending({ { almasterindex_data::key(5), almasterindex_data::key(5) } }), size_t(0));
        ensure_equals("and not told", told, 2);
        index.reset();
        const ALMasterLinks disk = onDisk();
        ensure_equals("one on disk", disk.size(), size_t(1));
        ensure("pending there", disk.of(almasterindex_data::key(1), almasterindex_data::key(2))->state == ALMasterLink::State::Pending);
    }

    template<> template<>
    void almasterindex_object::test<6>()
    {
        set_test_name("a save heard from elsewhere marks the link differing, unless it is what the master last sent");
        ALMasterIndex&    made = open();
        const std::string ours = "default { state_entry() {} }";
        ALMasterLink      door = almasterindex_data::link(1, 2, "/s/door.lsl");
        door.base              = almasterindex_data::key(100);
        door.hash              = ALUploadHeader::hashOfPlain("mono", ours);
        ALMasterLink card      = almasterindex_data::link(1, 3, "/s/card.txt");
        card.notecard          = true;
        card.base              = almasterindex_data::key(200);
        card.hash              = ALUploadHeader::hashOfPlain("notecard", "hello");
        made.link({ door, card });
        made.flush();
        told = 0;

        ensure("what it is of already: nothing", made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(2), almasterindex_data::key(100), ours) == ALMasterIndex::Heard::Nothing);
        ensure("no asset: nothing", made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(2), LLUUID::null, "x") == ALMasterIndex::Heard::Nothing);
        ensure("not linked: nothing", made.heardSaved(almasterindex_data::key(7), almasterindex_data::key(7), almasterindex_data::key(101), ours) == ALMasterIndex::Heard::Nothing);
        ensure("nothing changed", !made.dirty());

        // A recompile: the same text, a new asset.
        ensure("the same", made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(2), almasterindex_data::key(101), ours) == ALMasterIndex::Heard::Same);
        const std::optional<ALMasterLink> same = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        ensure_equals("the base moved", same->base, almasterindex_data::key(101));
        ensure("still active", same->state == ALMasterLink::State::Active);
        ensure_equals("nobody told", told, 0);
        ensure("to be written", made.dirty());
        ensure("a notecard's by its text", made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(3), almasterindex_data::key(201), "hello") == ALMasterIndex::Heard::Same);

        ensure("something else", made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(2), almasterindex_data::key(102), "default {}") == ALMasterIndex::Heard::Differing);
        const std::optional<ALMasterLink> other = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        ensure("differing", other->state == ALMasterLink::State::Differing);
        ensure_equals("its base as it was", other->base, almasterindex_data::key(101));
        ensure_equals("told", told, 1);

        // Nothing sent through it yet: no hash to be the same as.
        made.link(almasterindex_data::link(2, 4, "/s/new.lsl"));
        ensure("never sent: differing", made.heardSaved(almasterindex_data::key(2), almasterindex_data::key(4), almasterindex_data::key(300), ours) == ALMasterIndex::Heard::Differing);
    }

    template<> template<>
    void almasterindex_object::test<7>()
    {
        set_test_name("a send's link is put back where the item is linked still, and moves to a new item it was saved as");
        ALMasterIndex& made = open();
        made.link(almasterindex_data::link(1, 2, "/s/door.lsl"));
        told = 0;
        ALMasterLink sent = *made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        sent.hash         = "h1";
        sent.base         = almasterindex_data::key(50);
        ensure("put back", made.finished(almasterindex_data::key(1), almasterindex_data::key(2), sent));
        ensure_equals("with what the send left", made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2))->hash, std::string("h1"));
        ensure_equals("told", told, 1);

        // Saved as another item: the link is that one's.
        ALMasterLink rekeyed = sent;
        rekeyed.item         = almasterindex_data::key(3);
        rekeyed.hash         = "h2";
        ensure("put", made.finished(almasterindex_data::key(1), almasterindex_data::key(2), rekeyed));
        ensure("not the old item's", !made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2)));
        ensure_equals("the new one's", made.linkOf(almasterindex_data::key(1), almasterindex_data::key(3))->hash, std::string("h2"));
        ensure_equals("one link", made.size(), size_t(1));
        ensure_equals("found by its master", made.mastering("/s/door.lsl").size(), size_t(1));

        // Let go of while it was on its way: stays let go of.
        made.unlink(almasterindex_data::key(1), almasterindex_data::key(3));
        ensure("not put", !made.finished(almasterindex_data::key(1), almasterindex_data::key(3), rekeyed));
        ensure("none", made.empty());
    }

    template<> template<>
    void almasterindex_object::test<8>()
    {
        set_test_name("what a probe found is kept only by a link that knows nothing still, of the same master, and told a moment later");
        ALMasterIndex& made = open();
        made.link({ almasterindex_data::link(1, 1, "/s/a.lsl"), almasterindex_data::link(1, 2, "/s/b.lsl"), almasterindex_data::link(1, 3, "/s/c.lsl") });
        ensure("a new link knows nothing", ALMasterIndex::knowsNothing(*made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1))));
        told = 0;

        ensure("kept", made.adopted(almasterindex_data::key(1), almasterindex_data::key(1), "/s/a.lsl", { "disk:/s/util.lsl" }, false, "ha", 77));
        const std::optional<ALMasterLink> a = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1));
        ensure_equals("what it read", a->uses.size(), size_t(1));
        ensure_equals("what went up, as the world holds it", a->hash, std::string("ha"));
        ensure_equals("the stamp with it", a->stamp, S64(77));
        ensure("found by what it read", !made.affectedBy("disk:/s/util.lsl").empty());
        // The world holding something else: what it read, and no hash.
        ensure("kept too", made.adopted(almasterindex_data::key(1), almasterindex_data::key(2), "/s/b.lsl", {}, true, std::string(), 0));
        ensure("missed", made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2))->missed);
        ensure("no hash", made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2))->hash.empty());
        ensure_equals("not told yet", told, 0);
        pass(ALMasterIndex::TELL_SOON + 0.01);
        ensure_equals("told once for the run", told, 1);

        ensure("knowing something now: not again", !made.adopted(almasterindex_data::key(1), almasterindex_data::key(1), "/s/a.lsl", {}, false, "other", 1));
        ensure_equals("as it was", made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1))->hash, std::string("ha"));
        made.link(almasterindex_data::link(1, 3, "/s/elsewhere.lsl"));
        ensure("linked to another master: not", !made.adopted(almasterindex_data::key(1), almasterindex_data::key(3), "/s/c.lsl", { "disk:/s/x.lsl" }, false, "hc", 1));
        ensure("nothing learned: nothing kept", !made.adopted(almasterindex_data::key(1), almasterindex_data::key(3), "/s/elsewhere.lsl", {}, false, std::string(), 0));
        ensure("not linked: not", !made.adopted(almasterindex_data::key(9), almasterindex_data::key(9), "/s/a.lsl", { "disk:/s/x.lsl" }, false, "h", 1));
        ensure("still knowing nothing", ALMasterIndex::knowsNothing(*made.linkOf(almasterindex_data::key(1), almasterindex_data::key(3))));
        const S32 before = told;
        pass(1.0);
        ensure_equals("nothing more told", told, before);
    }

    template<> template<>
    void almasterindex_object::test<9>()
    {
        set_test_name("a send's bookkeeping is written once nothing has changed for a second, and no later than five after the first");
        ALMasterIndex& made = open();
        made.link(almasterindex_data::link(1, 2, "/s/door.lsl"));
        made.flush();
        ALMasterLink sent = *made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));

        sent.hash = "h1";
        made.finished(almasterindex_data::key(1), almasterindex_data::key(2), sent);
        ensure("waiting", made.dirty());
        pass(0.5);
        sent.hash = "h2";
        made.finished(almasterindex_data::key(1), almasterindex_data::key(2), sent);
        pass(0.6);
        ensure("a second after the first, not after the last: waiting still", made.dirty());
        made.awaitWrites();
        ensure("the file as it was", onDisk().of(almasterindex_data::key(1), almasterindex_data::key(2))->hash.empty());
        pass(0.5);
        ensure("a second after the last: handed over", !made.dirty());
        made.awaitWrites();
        ensure_equals("written, the newest", onDisk().of(almasterindex_data::key(1), almasterindex_data::key(2))->hash, std::string("h2"));

        // A change every 0.8 s: written five seconds after the first all the
        // same.
        for (int i = 0; i < 6; ++i)
        {
            sent.hash = "run" + std::to_string(i);
            made.finished(almasterindex_data::key(1), almasterindex_data::key(2), sent);
            pass(0.8);
        }
        ensure("4.8 s on: waiting", made.dirty());
        pass(0.3);
        ensure("5.1 s on: handed over", !made.dirty());
        made.awaitWrites();
        ensure_equals("written", onDisk().of(almasterindex_data::key(1), almasterindex_data::key(2))->hash, std::string("run5"));
    }

    template<> template<>
    void almasterindex_object::test<10>()
    {
        set_test_name("whatever waits is written as the index goes, to its own file, as for another account");
        const std::string other = in("other_account.llsd");
        ALMasterIndex&    first = open();
        first.link(almasterindex_data::link(1, 2, "/s/door.lsl"));
        ALMasterLink sent = *first.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        sent.hash         = "last";
        first.finished(almasterindex_data::key(1), almasterindex_data::key(2), sent);
        ensure("waiting", first.dirty());

        // Another account's: this one's let go of, and written as it goes,
        // with no time passed.
        ALMasterIndex& second = open(other);
        ensure("the other account's has none", second.empty());
        ensure_equals("the first's written to its own file", almasterindex_data::onDisk(file).of(almasterindex_data::key(1), almasterindex_data::key(2))->hash,
                      std::string("last"));
        second.link(almasterindex_data::link(5, 6, "/s/lamp.lsl"));
        index.reset();
        ensure_equals("the second's to its own", almasterindex_data::onDisk(other).size(), size_t(1));
        ensure("and not to the first's", !almasterindex_data::onDisk(file).of(almasterindex_data::key(5), almasterindex_data::key(6)));
        // What waits for a time gone by finds nobody.
        pass(10.0);
    }
}
