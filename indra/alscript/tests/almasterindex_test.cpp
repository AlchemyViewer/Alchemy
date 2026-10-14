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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
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

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: links go through the file and come back as they went", "[almasterindex]")
    {
        ALMasterIndex& made = open();
        REQUIRE(made.empty());
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
        UNSCOPED_INFO("written");
        CHECK_FALSE(made.dirty());

        ALMasterIndex& back = open();
        REQUIRE(back.size() == size_t(2));
        const std::optional<ALMasterLink> read = back.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(read.has_value());
        CHECK(read->master == std::string("/s/door.lsl"));
        CHECK(read->hash == std::string("abc"));
        CHECK(read->stamp == S64(1234));
        CHECK(read->base == almasterindex_data::key(9));
        REQUIRE(read->uses.size() == size_t(1));
        CHECK(read->itemName == std::string("Door"));
        const std::optional<ALMasterLink> note = back.linkOf(LLUUID::null, almasterindex_data::key(3));
        REQUIRE(note.has_value());
        CHECK(note->notecard);
        REQUIRE(back.mastering("/s/door.lsl").size() == size_t(1));
        REQUIRE(back.affectedBy("disk:/s/util.lsl").size() == size_t(1));
        REQUIRE(back.linksIn(almasterindex_data::key(1)).size() == size_t(1));
        UNSCOPED_INFO("in the inventory");
        REQUIRE(back.linksIn(LLUUID::null).size() == size_t(1));
        UNSCOPED_INFO("read, nothing changed");
        CHECK_FALSE(back.dirty());
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: an unreadable file holds no links and is kept until a link changes", "[almasterindex]")
    {
        const std::string garbage = "this is not <llsd> at all";
        almasterindex_data::put(file, garbage);
        {
            ALMasterIndex& looked = open();
            REQUIRE(looked.empty());
            UNSCOPED_INFO("nothing to write");
            CHECK_FALSE(looked.dirty());
            pass(10.0);
            index.reset();
        }
        UNSCOPED_INFO("left as it was");
        CHECK(text() == garbage);

        ALMasterIndex& changed = open();
        changed.link(almasterindex_data::link(1, 2, "/s/door.lsl"));
        index.reset();
        UNSCOPED_INFO("a link written over it");
        REQUIRE(onDisk().size() == size_t(1));
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: links orphaned long ago are let go of on reading and that is written", "[almasterindex]")
    {
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
        UNSCOPED_INFO("the old one gone");
        REQUIRE(read.size() == size_t(2));
        UNSCOPED_INFO("not the recent one");
        REQUIRE(read.linkOf(almasterindex_data::key(1), almasterindex_data::key(2)).has_value());
        UNSCOPED_INFO("to be written");
        CHECK(read.dirty());
        UNSCOPED_INFO("nobody told: nobody listened yet");
        CHECK(told == 0);
        index.reset();
        UNSCOPED_INFO("written without it");
        REQUIRE(onDisk().size() == size_t(2));
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: links made together are told once and one alone once", "[almasterindex]")
    {
        ALMasterIndex& made = open();
        made.link({ almasterindex_data::link(1, 1, "/s/a.lsl"), almasterindex_data::link(1, 2, "/s/b.lsl"), almasterindex_data::link(2, 3, "/s/c.lsl") });
        REQUIRE(made.size() == size_t(3));
        CHECK(told == 1);
        made.link(almasterindex_data::link(2, 4, "/s/d.lsl"));
        CHECK(told == 2);
        made.link(std::vector<ALMasterLink>());
        UNSCOPED_INFO("none, not told");
        CHECK(told == 2);
        made.link(almasterindex_data::link(1, 1, "/s/a2.lsl"));
        UNSCOPED_INFO("put over, not beside");
        REQUIRE(made.size() == size_t(4));
        const std::optional<ALMasterLink> over = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1));
        REQUIRE(over.has_value());
        CHECK(over->master == std::string("/s/a2.lsl"));
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: a link let go of and links marked pending only where linked", "[almasterindex]")
    {
        ALMasterIndex& made = open();
        made.link({ almasterindex_data::link(1, 1, "/s/a.lsl"), almasterindex_data::link(1, 2, "/s/b.lsl") });
        told = 0;
        CHECK(made.unlink(almasterindex_data::key(1), almasterindex_data::key(1)));
        CHECK(told == 1);
        REQUIRE_FALSE(made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1)));
        UNSCOPED_INFO("one not linked is not");
        CHECK_FALSE(made.unlink(almasterindex_data::key(1), almasterindex_data::key(1)));
        UNSCOPED_INFO("and not told");
        CHECK(told == 1);

        const size_t marked = made.markPending({ { almasterindex_data::key(1), almasterindex_data::key(1) }, { almasterindex_data::key(1), almasterindex_data::key(2) } });
        UNSCOPED_INFO("the one linked");
        CHECK(marked == size_t(1));
        const std::optional<ALMasterLink> pending = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(pending.has_value());
        CHECK(pending->state == ALMasterLink::State::Pending);
        CHECK(told == 2);
        UNSCOPED_INFO("none linked, none marked");
        CHECK(made.markPending({ { almasterindex_data::key(5), almasterindex_data::key(5) } }) == size_t(0));
        UNSCOPED_INFO("and not told");
        CHECK(told == 2);
        index.reset();
        const ALMasterLinks disk = onDisk();
        REQUIRE(disk.size() == size_t(1));
        const ALMasterLink* const written = disk.of(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(written);
        CHECK(written->state == ALMasterLink::State::Pending);
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: a save heard elsewhere marks the link differing unless the master sent it", "[almasterindex]")
    {
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

        UNSCOPED_INFO("what it is of already: nothing");
        CHECK(made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(2), almasterindex_data::key(100), ours) == ALMasterIndex::Heard::Nothing);
        UNSCOPED_INFO("no asset: nothing");
        CHECK(made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(2), LLUUID::null, "x") == ALMasterIndex::Heard::Nothing);
        UNSCOPED_INFO("not linked: nothing");
        CHECK(made.heardSaved(almasterindex_data::key(7), almasterindex_data::key(7), almasterindex_data::key(101), ours) == ALMasterIndex::Heard::Nothing);
        UNSCOPED_INFO("nothing changed");
        CHECK_FALSE(made.dirty());

        // A recompile: the same text, a new asset.
        CHECK(made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(2), almasterindex_data::key(101), ours) == ALMasterIndex::Heard::Same);
        const std::optional<ALMasterLink> same = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(same.has_value());
        CHECK(same->base == almasterindex_data::key(101));
        CHECK(same->state == ALMasterLink::State::Active);
        CHECK(told == 0);
        UNSCOPED_INFO("to be written");
        CHECK(made.dirty());
        UNSCOPED_INFO("a notecard's by its text");
        CHECK(made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(3), almasterindex_data::key(201), "hello") == ALMasterIndex::Heard::Same);

        CHECK(made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(2), almasterindex_data::key(102), "default {}") == ALMasterIndex::Heard::Differing);
        const std::optional<ALMasterLink> other = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(other.has_value());
        CHECK(other->state == ALMasterLink::State::Differing);
        UNSCOPED_INFO("its base as it was");
        CHECK(other->base == almasterindex_data::key(101));
        CHECK(told == 1);

        // Nothing sent through it yet: no hash to be the same as.
        made.link(almasterindex_data::link(2, 4, "/s/new.lsl"));
        UNSCOPED_INFO("never sent: differing");
        CHECK(made.heardSaved(almasterindex_data::key(2), almasterindex_data::key(4), almasterindex_data::key(300), ours) == ALMasterIndex::Heard::Differing);
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: a send's link is put back if still linked and moves to the item it saved as", "[almasterindex]")
    {
        ALMasterIndex& made = open();
        made.link(almasterindex_data::link(1, 2, "/s/door.lsl"));
        told = 0;
        const std::optional<ALMasterLink> linked = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(linked.has_value());
        ALMasterLink sent = *linked;
        sent.hash         = "h1";
        sent.base         = almasterindex_data::key(50);
        CHECK(made.finished(almasterindex_data::key(1), almasterindex_data::key(2), sent));
        const std::optional<ALMasterLink> left = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(left.has_value());
        UNSCOPED_INFO("with what the send left");
        CHECK(left->hash == std::string("h1"));
        CHECK(told == 1);

        // Saved as another item: the link is that one's.
        ALMasterLink rekeyed = sent;
        rekeyed.item         = almasterindex_data::key(3);
        rekeyed.hash         = "h2";
        CHECK(made.finished(almasterindex_data::key(1), almasterindex_data::key(2), rekeyed));
        UNSCOPED_INFO("not the old item's");
        REQUIRE_FALSE(made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2)));
        const std::optional<ALMasterLink> moved = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(3));
        REQUIRE(moved.has_value());
        CHECK(moved->hash == std::string("h2"));
        REQUIRE(made.size() == size_t(1));
        REQUIRE(made.mastering("/s/door.lsl").size() == size_t(1));

        // Let go of while it was on its way: stays let go of.
        made.unlink(almasterindex_data::key(1), almasterindex_data::key(3));
        CHECK_FALSE(made.finished(almasterindex_data::key(1), almasterindex_data::key(3), rekeyed));
        REQUIRE(made.empty());
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: a linked item renamed is said by its new name and that is written", "[almasterindex]")
    {
        ALMasterIndex& made = open();
        made.link(almasterindex_data::link(1, 2, "/s/door.lsl"));
        const std::optional<ALMasterLink> linked = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(linked.has_value());
        const std::string was = linked->itemName;
        told                  = 0;
        UNSCOPED_INFO("the name it has: nothing");
        CHECK_FALSE(made.renamed(almasterindex_data::key(1), almasterindex_data::key(2), was));
        CHECK(told == 0);
        UNSCOPED_INFO("Tapple's: renamed, taken");
        CHECK(made.renamed(almasterindex_data::key(1), almasterindex_data::key(2), "front door"));
        const std::optional<ALMasterLink> now = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(now.has_value());
        {
            INFO("the new name, the rest as it was");
            CHECK(now->itemName == std::string("front door"));
            CHECK(now->master == linked->master);
            CHECK(told == 1);
        }
        UNSCOPED_INFO("an item not linked: nothing");
        CHECK_FALSE(made.renamed(almasterindex_data::key(1), almasterindex_data::key(9), "other"));
        UNSCOPED_INFO("no name: nothing");
        CHECK_FALSE(made.renamed(almasterindex_data::key(1), almasterindex_data::key(2), ""));
        made.flush();
        ALMasterIndex&                    again = open();
        const std::optional<ALMasterLink> read  = again.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(read.has_value());
        UNSCOPED_INFO("written");
        CHECK(read->itemName == std::string("front door"));
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: a target and an experience picked for the next send are kept and written", "[almasterindex]")
    {
        ALMasterIndex& made = open();
        made.link(almasterindex_data::link(1, 2, "/s/door.lsl"));
        told                    = 0;
        const LLUUID experience = almasterindex_data::key(70);
        UNSCOPED_INFO("nothing given: nothing");
        CHECK_FALSE(made.chosen(almasterindex_data::key(1), almasterindex_data::key(2), std::nullopt, std::nullopt));
        UNSCOPED_INFO("a target");
        CHECK(made.chosen(almasterindex_data::key(1), almasterindex_data::key(2), std::string("lsl2"), std::nullopt));
        UNSCOPED_INFO("an experience");
        CHECK(made.chosen(almasterindex_data::key(1), almasterindex_data::key(2), std::nullopt, std::optional<LLUUID>(experience)));
        UNSCOPED_INFO("the same again: nothing");
        CHECK_FALSE(made.chosen(almasterindex_data::key(1), almasterindex_data::key(2), std::string("lsl2"), std::optional<LLUUID>(experience)));
        CHECK(told == 2);
        UNSCOPED_INFO("an item not linked: nothing");
        CHECK_FALSE(made.chosen(almasterindex_data::key(1), almasterindex_data::key(9), std::string("mono"), std::nullopt));
        UNSCOPED_INFO("written at once");
        CHECK_FALSE(made.dirty());

        ALMasterIndex&                    again = open();
        const std::optional<ALMasterLink> read  = again.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(read.has_value());
        {
            INFO("read back as picked");
            CHECK(read->target == std::string("lsl2"));
            REQUIRE(read->experience.has_value());
            CHECK(*read->experience == experience);
        }
        // None picked: null, which is not the same as none asked.
        CHECK(again.chosen(almasterindex_data::key(1), almasterindex_data::key(2), std::nullopt, std::optional<LLUUID>(LLUUID::null)));
        ALMasterIndex&                    third = open();
        const std::optional<ALMasterLink> none  = third.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(none.has_value());
        {
            INFO("none picked, read back as none, not as unasked");
            REQUIRE(none->experience.has_value());
            CHECK(none->experience->isNull());
        }
        third.link(almasterindex_data::link(1, 3, "/s/lamp.lsl"));
        const std::optional<ALMasterLink> fresh = third.linkOf(almasterindex_data::key(1), almasterindex_data::key(3));
        REQUIRE(fresh.has_value());
        UNSCOPED_INFO("a link never asked: none asked");
        CHECK_FALSE(fresh->experience.has_value());
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: what a probe found is kept only by a link of its master knowing nothing yet", "[almasterindex]")
    {
        ALMasterIndex& made = open();
        made.link({ almasterindex_data::link(1, 1, "/s/a.lsl"), almasterindex_data::link(1, 2, "/s/b.lsl"), almasterindex_data::link(1, 3, "/s/c.lsl") });
        const std::optional<ALMasterLink> fresh = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1));
        REQUIRE(fresh.has_value());
        UNSCOPED_INFO("a new link knows nothing");
        CHECK(ALMasterIndex::knowsNothing(*fresh));
        told = 0;

        CHECK(made.adopted(almasterindex_data::key(1), almasterindex_data::key(1), "/s/a.lsl", { "disk:/s/util.lsl" }, false, "ha", 77));
        const std::optional<ALMasterLink> a = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1));
        REQUIRE(a.has_value());
        REQUIRE(a->uses.size() == size_t(1));
        UNSCOPED_INFO("what went up, as the world holds it");
        CHECK(a->hash == std::string("ha"));
        CHECK(a->stamp == S64(77));
        REQUIRE_FALSE(made.affectedBy("disk:/s/util.lsl").empty());
        // The world holding something else: what it read, and no hash.
        CHECK(made.adopted(almasterindex_data::key(1), almasterindex_data::key(2), "/s/b.lsl", {}, true, std::string(), 0));
        const std::optional<ALMasterLink> b = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(b.has_value());
        CHECK(b->missed);
        REQUIRE(b->hash.empty());
        UNSCOPED_INFO("not told yet");
        CHECK(told == 0);
        pass(ALMasterIndex::TELL_SOON + 0.01);
        UNSCOPED_INFO("told once for the run");
        CHECK(told == 1);

        UNSCOPED_INFO("knowing something now: not again");
        CHECK_FALSE(made.adopted(almasterindex_data::key(1), almasterindex_data::key(1), "/s/a.lsl", {}, false, "other", 1));
        const std::optional<ALMasterLink> kept = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1));
        REQUIRE(kept.has_value());
        UNSCOPED_INFO("as it was");
        CHECK(kept->hash == std::string("ha"));
        made.link(almasterindex_data::link(1, 3, "/s/elsewhere.lsl"));
        UNSCOPED_INFO("linked to another master: not");
        CHECK_FALSE(made.adopted(almasterindex_data::key(1), almasterindex_data::key(3), "/s/c.lsl", { "disk:/s/x.lsl" }, false, "hc", 1));
        UNSCOPED_INFO("nothing learned: nothing kept");
        CHECK_FALSE(made.adopted(almasterindex_data::key(1), almasterindex_data::key(3), "/s/elsewhere.lsl", {}, false, std::string(), 0));
        UNSCOPED_INFO("not linked: not");
        CHECK_FALSE(made.adopted(almasterindex_data::key(9), almasterindex_data::key(9), "/s/a.lsl", { "disk:/s/x.lsl" }, false, "h", 1));
        const std::optional<ALMasterLink> elsewhere = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(3));
        REQUIRE(elsewhere.has_value());
        UNSCOPED_INFO("still knowing nothing");
        CHECK(ALMasterIndex::knowsNothing(*elsewhere));
        const S32 before = told;
        pass(1.0);
        UNSCOPED_INFO("nothing more told");
        CHECK(told == before);
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: a send's bookkeeping is written after a quiet second or five after the first", "[almasterindex]")
    {
        ALMasterIndex& made = open();
        made.link(almasterindex_data::link(1, 2, "/s/door.lsl"));
        made.flush();
        const std::optional<ALMasterLink> linked = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(linked.has_value());
        ALMasterLink sent = *linked;

        sent.hash = "h1";
        made.finished(almasterindex_data::key(1), almasterindex_data::key(2), sent);
        UNSCOPED_INFO("waiting");
        CHECK(made.dirty());
        pass(0.5);
        sent.hash = "h2";
        made.finished(almasterindex_data::key(1), almasterindex_data::key(2), sent);
        pass(0.6);
        UNSCOPED_INFO("a second after the first, not after the last: waiting still");
        CHECK(made.dirty());
        made.awaitWrites();
        const ALMasterLinks       waited = onDisk();
        const ALMasterLink* const as_was = waited.of(almasterindex_data::key(1), almasterindex_data::key(2));
        {
            INFO("the file as it was");
            REQUIRE(as_was);
            REQUIRE(as_was->hash.empty());
        }
        pass(0.5);
        UNSCOPED_INFO("a second after the last: handed over");
        CHECK_FALSE(made.dirty());
        made.awaitWrites();
        const ALMasterLinks       handed = onDisk();
        const ALMasterLink* const newest = handed.of(almasterindex_data::key(1), almasterindex_data::key(2));
        {
            INFO("written, the newest");
            REQUIRE(newest);
            CHECK(newest->hash == std::string("h2"));
        }

        // A change every 0.8 s: written five seconds after the first all the
        // same.
        for (int i = 0; i < 6; ++i)
        {
            sent.hash = "run" + std::to_string(i);
            made.finished(almasterindex_data::key(1), almasterindex_data::key(2), sent);
            pass(0.8);
        }
        UNSCOPED_INFO("4.8 s on: waiting");
        CHECK(made.dirty());
        pass(0.3);
        UNSCOPED_INFO("5.1 s on: handed over");
        CHECK_FALSE(made.dirty());
        made.awaitWrites();
        const ALMasterLinks       ran  = onDisk();
        const ALMasterLink* const last = ran.of(almasterindex_data::key(1), almasterindex_data::key(2));
        {
            INFO("written");
            REQUIRE(last);
            CHECK(last->hash == std::string("run5"));
        }
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: whatever waits is written to its own file as the index goes", "[almasterindex]")
    {
        const std::string other = in("other_account.llsd");
        ALMasterIndex&    first = open();
        first.link(almasterindex_data::link(1, 2, "/s/door.lsl"));
        const std::optional<ALMasterLink> linked = first.linkOf(almasterindex_data::key(1), almasterindex_data::key(2));
        REQUIRE(linked.has_value());
        ALMasterLink sent = *linked;
        sent.hash         = "last";
        first.finished(almasterindex_data::key(1), almasterindex_data::key(2), sent);
        UNSCOPED_INFO("waiting");
        CHECK(first.dirty());

        // Another account's: this one's let go of, and written as it goes,
        // with no time passed.
        ALMasterIndex& second = open(other);
        UNSCOPED_INFO("the other account's has none");
        REQUIRE(second.empty());
        const ALMasterLinks       firsts = almasterindex_data::onDisk(file);
        const ALMasterLink* const went   = firsts.of(almasterindex_data::key(1), almasterindex_data::key(2));
        {
            INFO("the first's written to its own file");
            REQUIRE(went);
            CHECK(went->hash == std::string("last"));
        }
        second.link(almasterindex_data::link(5, 6, "/s/lamp.lsl"));
        index.reset();
        UNSCOPED_INFO("the second's to its own");
        REQUIRE(almasterindex_data::onDisk(other).size() == size_t(1));
        UNSCOPED_INFO("and not to the first's");
        REQUIRE_FALSE(almasterindex_data::onDisk(file).of(almasterindex_data::key(5), almasterindex_data::key(6)));
        // What waits for a time gone by finds nobody.
        pass(10.0);
    }

    TEST_CASE_METHOD(almasterindex_data, "almasterindex: what a person did is written at once and what a send learned later", "[almasterindex]")
    {
        ALMasterIndex& made = open();
        // Each read as a crash would leave the file: what was handed over
        // written, nothing more, and no time passed.
        const auto on_disk = [this, &made](int item) {
            made.awaitWrites();
            const ALMasterLinks       disk = almasterindex_data::onDisk(file);
            const ALMasterLink* const one  = disk.of(almasterindex_data::key(1), almasterindex_data::key(item));
            return one ? std::optional<ALMasterLink>(*one) : std::nullopt;
        };

        made.link({ almasterindex_data::link(1, 1, "/s/a.lsl"), almasterindex_data::link(1, 2, "/s/b.lsl") });
        UNSCOPED_INFO("a link: handed over at once");
        CHECK_FALSE(made.dirty());
        REQUIRE(on_disk(1).has_value());

        // A send's bookkeeping waits, and a crash now loses it.
        const std::optional<ALMasterLink> linked = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1));
        REQUIRE(linked.has_value());
        ALMasterLink sent = *linked;
        sent.hash         = "h1";
        sent.base         = almasterindex_data::key(10);
        sent.uses         = { "disk:/s/util.lsl" };
        made.finished(almasterindex_data::key(1), almasterindex_data::key(1), sent);
        UNSCOPED_INFO("a send's bookkeeping waits");
        CHECK(made.dirty());
        const std::optional<ALMasterLink> waits = on_disk(1);
        REQUIRE(waits.has_value());
        UNSCOPED_INFO("not on disk yet");
        REQUIRE(waits->hash.empty());

        // Something a person did takes it with it.
        made.markPending({ { almasterindex_data::key(1), almasterindex_data::key(2) } });
        UNSCOPED_INFO("pending: at once");
        CHECK_FALSE(made.dirty());
        const std::optional<ALMasterLink> marked = on_disk(2);
        REQUIRE(marked.has_value());
        CHECK(marked->state == ALMasterLink::State::Pending);
        const std::optional<ALMasterLink> with = on_disk(1);
        REQUIRE(with.has_value());
        UNSCOPED_INFO("and what waited with it");
        CHECK(with->hash == std::string("h1"));

        // A send that changes how a link stands: at once.
        const std::optional<ALMasterLink> again = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(1));
        REQUIRE(again.has_value());
        sent       = *again;
        sent.state = ALMasterLink::State::Suspended;
        made.finished(almasterindex_data::key(1), almasterindex_data::key(1), sent);
        UNSCOPED_INFO("suspended: at once");
        CHECK_FALSE(made.dirty());
        const std::optional<ALMasterLink> suspended = on_disk(1);
        REQUIRE(suspended.has_value());
        CHECK(suspended->state == ALMasterLink::State::Suspended);
        sent.state = ALMasterLink::State::Active;
        made.finished(almasterindex_data::key(1), almasterindex_data::key(1), sent);
        {
            INFO("active again: at once");
            CHECK_FALSE(made.dirty());
            const std::optional<ALMasterLink> active = on_disk(1);
            REQUIRE(active.has_value());
            CHECK(active->state == ALMasterLink::State::Active);
        }

        // A send saved as another item: at once, or the link is lost with it.
        ALMasterLink moved = sent;
        moved.item         = almasterindex_data::key(3);
        made.finished(almasterindex_data::key(1), almasterindex_data::key(1), moved);
        UNSCOPED_INFO("moved: at once");
        CHECK_FALSE(made.dirty());
        {
            INFO("under the new item on disk");
            REQUIRE(on_disk(3).has_value());
            REQUIRE_FALSE(on_disk(1).has_value());
        }

        // A save heard from elsewhere: differing at once, the same later.
        const std::optional<ALMasterLink> under = made.linkOf(almasterindex_data::key(1), almasterindex_data::key(3));
        REQUIRE(under.has_value());
        sent      = *under;
        sent.hash = ALUploadHeader::hashOfPlain("mono", "default {}");
        made.finished(almasterindex_data::key(1), almasterindex_data::key(3), sent);
        made.flush();
        made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(3), almasterindex_data::key(11), "default {}");
        UNSCOPED_INFO("the same: later");
        CHECK(made.dirty());
        made.heardSaved(almasterindex_data::key(1), almasterindex_data::key(3), almasterindex_data::key(12), "default { touch_start(integer n) {} }");
        {
            INFO("differing: at once");
            CHECK_FALSE(made.dirty());
            const std::optional<ALMasterLink> differing = on_disk(3);
            REQUIRE(differing.has_value());
            CHECK(differing->state == ALMasterLink::State::Differing);
        }

        // A probe's answer, and a link let go of.
        made.link(almasterindex_data::link(1, 4, "/s/d.lsl"));
        made.adopted(almasterindex_data::key(1), almasterindex_data::key(4), "/s/d.lsl", { "disk:/s/util.lsl" }, false, std::string(), 0);
        UNSCOPED_INFO("a probe's answer: later");
        CHECK(made.dirty());
        const std::optional<ALMasterLink> probed = on_disk(4);
        REQUIRE(probed.has_value());
        UNSCOPED_INFO("not on disk yet");
        REQUIRE(probed->uses.empty());
        made.unlink(almasterindex_data::key(1), almasterindex_data::key(2));
        {
            INFO("let go of: at once");
            CHECK_FALSE(made.dirty());
            REQUIRE_FALSE(on_disk(2).has_value());
        }
        const std::optional<ALMasterLink> answered = on_disk(4);
        REQUIRE(answered.has_value());
        UNSCOPED_INFO("the probe's answer with it");
        REQUIRE(answered->uses.size() == size_t(1));
        // Nothing left for the time to write.
        pass(10.0);
        UNSCOPED_INFO("nothing waiting");
        CHECK_FALSE(made.dirty());
    }
}
