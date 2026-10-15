/**
 * @file almasterwatch_test.cpp
 * @brief The files on disk whose saves send linked scripts, watched.
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

#include "../masters/almasterwatch.h"

#include "fsyspath.h"
#include "llfile.h"
#include "lluuid.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct almasterwatch_data
    {
        // A callable waiting for the test's own time to come.
        struct Waiting
        {
            F64                   at = 0.0;
            std::function<void()> callable;
        };
        // A first look the watch asked for, not yet answered.
        struct Asked
        {
            std::vector<std::string>         paths;
            ALMasterWatch::stamped_t         found;
            bool                             answered = false;
        };
        // What the watch released, in order.
        struct Released
        {
            std::vector<std::string> masters;
            std::vector<std::string> includes;
        };

        std::string                    folder;
        // A second within which each test's file times fall, well in the
        // past, so that each write is a time of its own.
        fs::file_time_type             second;
        F64                            time = 1000.0;
        std::vector<Waiting>           waiting;
        std::vector<Asked>             asked;
        std::vector<Released>          released;
        std::unique_ptr<ALMasterWatch> watch;

        almasterwatch_data()
        {
            folder = fsyspath(fs::temp_directory_path() / fsyspath("almasterwatch_" + LLUUID::generateNewID().asString())).string();
            std::error_code ec;
            fs::create_directories(fsyspath(folder), ec);
            second = std::chrono::floor<std::chrono::seconds>(fs::file_time_type::clock::now()) - std::chrono::hours(1);
        }
        ~almasterwatch_data()
        {
            watch.reset();
            std::error_code ec;
            fs::remove_all(fsyspath(folder), ec);
        }

        std::string in(const std::string& name) const { return fsyspath(fsyspath(folder) / fsyspath(name)).string(); }

        // Written, and dated `ms` milliseconds into the test's second.
        void put(const std::string& path, const std::string& text, int ms)
        {
            {
                llofstream out(fsyspath(path), std::ios::binary | std::ios::trunc);
                out << text;
            }
            std::error_code ec;
            fs::last_write_time(fsyspath(path), second + std::chrono::milliseconds(ms), ec);
        }

        ALMasterClock clock()
        {
            ALMasterClock out;
            out.after = [this](std::function<void()> callable, F32 seconds) { waiting.push_back({ time + seconds, std::move(callable) }); };
            out.now   = [this]() { return time; };
            return out;
        }

        // The watch, its first looks made at once here, or kept for the test
        // to answer when it chooses.
        ALMasterWatch& make(bool look_at_once = true)
        {
            watch.reset();
            ALMasterWatch::look_t look;
            if (look_at_once)
            {
                look = [](std::vector<std::string> paths, ALMasterWatch::stamped_t found) {
                    std::vector<ALFileStamp> stamps;
                    for (const std::string& path : paths)
                    {
                        stamps.push_back(ALFileStamp::of(path));
                    }
                    found(std::move(stamps));
                    return true;
                };
            }
            else
            {
                look = [this](std::vector<std::string> paths, ALMasterWatch::stamped_t found) {
                    asked.push_back({ std::move(paths), std::move(found) });
                    return true;
                };
            }
            watch = std::make_unique<ALMasterWatch>(
                [this](const std::vector<std::string>& masters, const std::vector<std::string>& includes) { released.push_back({ masters, includes }); },
                clock(), std::move(look));
            watch->setQuiet(1.0);
            return *watch;
        }

        // A first look asked for, made now and answered; or answered with
        // what a look made earlier found.
        void answer(size_t which)
        {
            std::vector<ALFileStamp> stamps;
            for (const std::string& path : asked.at(which).paths)
            {
                stamps.push_back(ALFileStamp::of(path));
            }
            answer(which, std::move(stamps));
        }
        void answer(size_t which, std::vector<ALFileStamp> stamps)
        {
            Asked& one   = asked.at(which);
            one.answered = true;
            one.found(std::move(stamps));
        }

        // Two looks, as the watcher makes them: a change is heard once the
        // file has held still from one look to the next.
        void looked()
        {
            watch->lookNow();
            watch->lookNow();
        }

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
                time                            = std::max(time, next->at);
                const std::function<void()> run = std::move(next->callable);
                waiting.erase(next);
                run();
            }
            time = until;
        }

        static ALMasterLinks::Watched master(const std::string& path) { return { path, true }; }
        static ALMasterLinks::Watched include(const std::string& path) { return { path, false }; }

        static std::string joined(const std::vector<std::string>& list)
        {
            std::vector<std::string> sorted = list;
            std::sort(sorted.begin(), sorted.end());
            std::string out;
            for (const std::string& one : sorted)
            {
                out += (out.empty() ? "" : " ") + fsyspath(one).filename().string();
            }
            return out;
        }
    };

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: a master saved outside is released once the burst has been quiet a second", "[almasterwatch]")
    {
        const std::string door = in("door.lsl");
        put(door, "default {}\n", 100);
        ALMasterWatch& watched = make();
        watched.watch({ almasterwatch_data::master(door) });
        UNSCOPED_INFO("watched at once, its look answered here");
        CHECK(watched.watching(door));
        looked();
        pass(5.0);
        UNSCOPED_INFO("what was there already is no change");
        REQUIRE(released.empty());

        put(door, "default { state_entry() {} }\n", 200);
        watched.lookNow();
        UNSCOPED_INFO("moving: not heard yet");
        REQUIRE(waiting.empty());
        watched.lookNow();
        pass(0.9);
        UNSCOPED_INFO("not before the quiet");
        REQUIRE(released.empty());
        pass(0.2);
        REQUIRE(released.size() == size_t(1));
        CHECK(almasterwatch_data::joined(released[0].masters) == std::string("door.lsl"));
        REQUIRE(released[0].includes.empty());
        looked();
        pass(5.0);
        UNSCOPED_INFO("and not again");
        REQUIRE(released.size() == size_t(1));
    }

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: a checkout of many files is released once and together after the last", "[almasterwatch]")
    {
        std::vector<ALMasterLinks::Watched> files;
        for (int i = 0; i < 10; ++i)
        {
            const std::string path = in("s" + std::to_string(i) + ".lsl");
            put(path, "x", 100);
            files.push_back(almasterwatch_data::master(path));
        }
        ALMasterWatch& watched = make();
        watched.watch(files);
        for (int i = 0; i < 5; ++i)
        {
            put(files[i].path, "changed", 200 + i);
        }
        looked();
        pass(0.5);
        for (int i = 5; i < 10; ++i)
        {
            put(files[i].path, "changed", 300 + i);
        }
        looked();
        pass(0.9);
        UNSCOPED_INFO("the burst goes on: nothing yet");
        REQUIRE(released.empty());
        pass(0.2);
        REQUIRE(released.size() == size_t(1));
        REQUIRE(released[0].masters.size() == size_t(10));
    }

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: masters and includes are told apart as they are released", "[almasterwatch]")
    {
        const std::string door = in("door.lsl");
        const std::string util = in("util.lsl");
        put(door, "a", 100);
        put(util, "b", 100);
        ALMasterWatch& watched = make();
        watched.watch({ almasterwatch_data::master(door), almasterwatch_data::include(util) });
        put(door, "a2", 200);
        put(util, "b2", 200);
        looked();
        pass(1.1);
        REQUIRE(released.size() == size_t(1));
        CHECK(almasterwatch_data::joined(released[0].masters) == std::string("door.lsl"));
        CHECK(almasterwatch_data::joined(released[0].includes) == std::string("util.lsl"));
    }

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: an emptied file is held longer than the quiet unless filled again meanwhile", "[almasterwatch]")
    {
        const std::string door = in("door.lsl");
        put(door, "default {}\n", 100);
        ALMasterWatch& watched = make();
        watched.watch({ almasterwatch_data::master(door) });

        put(door, "", 200);
        looked();
        pass(1.1);
        UNSCOPED_INFO("quiet, but emptied: held");
        REQUIRE(released.empty());
        pass(0.5);
        UNSCOPED_INFO("taken as empty after the hold");
        REQUIRE(released.size() == size_t(1));

        // An editor saving in two steps: emptied, then filled.
        put(door, "default { touch_start(integer n) {} }\n", 300);
        looked();
        pass(1.1);
        UNSCOPED_INFO("filled again: released");
        REQUIRE(released.size() == size_t(2));
        put(door, "", 400);
        looked();
        pass(0.3);
        put(door, "default { state_entry() {} }\n", 500);
        looked();
        pass(0.9);
        UNSCOPED_INFO("a second after it was filled: not yet");
        REQUIRE(released.size() == 2);
        pass(0.2);
        UNSCOPED_INFO("then, before the emptied hold would have ended");
        REQUIRE(released.size() == size_t(3));
        REQUIRE(released[2].masters.size() == size_t(1));
    }

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: what the studio writes is no change and takes a waiting change with it", "[almasterwatch]")
    {
        const std::string door = in("door.lsl");
        put(door, "a", 100);
        ALMasterWatch& watched = make();
        watched.watch({ almasterwatch_data::master(door) });

        put(door, "studio", 200);
        watched.seen(door);
        looked();
        pass(5.0);
        UNSCOPED_INFO("the studio's own write: nothing");
        REQUIRE(released.empty());

        // Saved outside, and heard; then written by the studio before the
        // burst ends, which sends it itself.
        put(door, "outside", 300);
        looked();
        put(door, "studio again", 400);
        watched.seen(door);
        pass(5.0);
        UNSCOPED_INFO("nothing waiting on it from before");
        REQUIRE(released.empty());
        looked();
        pass(5.0);
        UNSCOPED_INFO("and nothing after");
        REQUIRE(released.empty());
    }

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: a save after a studio write during the first look is heard", "[almasterwatch]")
    {
        const std::string door = in("door.lsl");
        put(door, "first", 100);
        ALMasterWatch& watched = make(/*look_at_once*/ false);
        watched.watch({ almasterwatch_data::master(door) });
        {
            INFO("waiting on its first look");
            CHECK(watched.waiting(door));
            CHECK_FALSE(watched.watching(door));
        }
        REQUIRE(asked.size() == size_t(1));

        // The studio writes it, then somebody else saves it, all before the
        // look answers: the save is heard, the studio's write is not.
        put(door, "studio", 200);
        watched.seen(door);
        put(door, "outside", 300);
        answer(0);
        UNSCOPED_INFO("watched now");
        CHECK(watched.watching(door));
        looked();
        pass(1.1);
        UNSCOPED_INFO("the save after the studio's write heard");
        REQUIRE(released.size() == size_t(1));
    }

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: a studio write during the first look is no change however old the look", "[almasterwatch]")
    {
        const std::string door = in("door.lsl");
        const std::string lamp = in("lamp.lsl");
        put(door, "first", 100);
        put(lamp, "first", 100);
        ALMasterWatch& watched = make(/*look_at_once*/ false);
        watched.watch({ almasterwatch_data::master(door), almasterwatch_data::master(lamp) });
        // The look made before the studio's write, and answered after it.
        const std::vector<ALFileStamp> before = { ALFileStamp::of(door), ALFileStamp::of(lamp) };
        put(door, "studio", 200);
        watched.seen(door);
        answer(0, before);
        CHECK(watched.watching(door));
        CHECK(watched.watching(lamp));
        looked();
        pass(5.0);
        UNSCOPED_INFO("the studio's write is no change");
        REQUIRE(released.empty());
    }

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: a first look answering for a file no longer wanted makes no watch of it", "[almasterwatch]")
    {
        const std::string door = in("door.lsl");
        put(door, "first", 100);
        ALMasterWatch& watched = make(/*look_at_once*/ false);
        watched.watch({ almasterwatch_data::master(door) });
        watched.watch({});
        UNSCOPED_INFO("not waiting now");
        CHECK_FALSE(watched.waiting(door));
        answer(0);
        CHECK_FALSE(watched.watching(door));
        REQUIRE(watched.size() == size_t(0));
        put(door, "changed", 200);
        looked();
        pass(5.0);
        REQUIRE(released.empty());
    }

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: a file wanted again waits on the newer look and one wanted still on its own", "[almasterwatch]")
    {
        const std::string door = in("door.lsl");
        const std::string lamp = in("lamp.lsl");
        put(door, "first", 100);
        put(lamp, "first", 100);
        ALMasterWatch& watched = make(/*look_at_once*/ false);
        watched.watch({ almasterwatch_data::master(door) });
        watched.watch({});
        watched.watch({ almasterwatch_data::master(door) });
        UNSCOPED_INFO("asked again");
        REQUIRE(asked.size() == size_t(2));
        answer(0);
        {
            INFO("the older look makes no watch");
            CHECK_FALSE(watched.watching(door));
            CHECK(watched.waiting(door));
        }
        answer(1);
        UNSCOPED_INFO("the newer one does");
        CHECK(watched.watching(door));

        // Wanted still while another is added: the look it waits on answers
        // it, and only the new file is asked after.
        watched.watch({ almasterwatch_data::master(door), almasterwatch_data::master(lamp) });
        {
            INFO("door watched already: lamp alone asked");
            REQUIRE(asked.size() == 3);
            REQUIRE(asked[2].paths.size() == 1);
        }
        const std::string other = in("other.lsl");
        watched.watch({ almasterwatch_data::master(door), almasterwatch_data::master(lamp), almasterwatch_data::master(other) });
        {
            INFO("lamp waits on its look still: other alone asked");
            REQUIRE(asked.size() == size_t(4));
            REQUIRE(asked[3].paths.size() == size_t(1));
        }
        answer(3);
        CHECK(watched.watching(other));
        UNSCOPED_INFO("lamp waits on its own");
        CHECK(watched.waiting(lamp));
        answer(2);
        CHECK(watched.watching(lamp));
    }

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: masters are looked at twice a second and includes once; past 256 every 2 s", "[almasterwatch]")
    {
        const std::string door = in("door.lsl");
        const std::string util = in("util.lsl");
        size_t            looks = 0;
        watch.reset();
        watch = std::make_unique<ALMasterWatch>([](const std::vector<std::string>&, const std::vector<std::string>&) {}, clock(),
                                                [&looks](std::vector<std::string> paths, ALMasterWatch::stamped_t found) {
                                                    looks += paths.size();
                                                    found(std::vector<ALFileStamp>(paths.size()));
                                                    return true;
                                                });
        watch->watch({ almasterwatch_data::master(door), almasterwatch_data::include(util) });
        std::optional<F32> door_period = watch->periodOf(door);
        std::optional<F32> util_period = watch->periodOf(util);
        REQUIRE(door_period.has_value());
        REQUIRE(util_period.has_value());
        CHECK(*door_period == ALMasterWatch::MASTER_PERIOD);
        CHECK(*util_period == ALMasterWatch::INCLUDE_PERIOD);
        UNSCOPED_INFO("both looked at first");
        CHECK(looks == size_t(2));

        std::vector<ALMasterLinks::Watched> many = { almasterwatch_data::master(door), almasterwatch_data::include(util) };
        for (size_t i = 0; i < ALMasterWatch::MANY - 1; ++i)
        {
            many.push_back(almasterwatch_data::master(in("m" + std::to_string(i) + ".lsl")));
        }
        watch->watch(many);
        REQUIRE(watch->size() == size_t(257));
        door_period = watch->periodOf(door);
        util_period = watch->periodOf(util);
        REQUIRE(door_period.has_value());
        REQUIRE(util_period.has_value());
        UNSCOPED_INFO("past 256: a master every two seconds");
        CHECK(*door_period == ALMasterWatch::MANY_PERIOD);
        CHECK(*util_period == ALMasterWatch::MANY_PERIOD);
        UNSCOPED_INFO("only the new ones looked at");
        CHECK(looks == size_t(257));

        watch->watch({ almasterwatch_data::master(door), almasterwatch_data::include(util) });
        door_period = watch->periodOf(door);
        util_period = watch->periodOf(util);
        REQUIRE(door_period.has_value());
        REQUIRE(util_period.has_value());
        UNSCOPED_INFO("fewer again: a master's own");
        CHECK(*door_period == ALMasterWatch::MASTER_PERIOD);
        CHECK(*util_period == ALMasterWatch::INCLUDE_PERIOD);
        UNSCOPED_INFO("nothing looked at again");
        CHECK(looks == size_t(257));
        UNSCOPED_INFO("one not watched has no period");
        REQUIRE_FALSE(watch->periodOf(in("m0.lsl")));
    }

    TEST_CASE_METHOD(almasterwatch_data, "almasterwatch: with no main loop to answer through the first looks are made at once", "[almasterwatch]")
    {
        const std::string door = in("door.lsl");
        put(door, "first", 100);
        watch = std::make_unique<ALMasterWatch>(
            [this](const std::vector<std::string>& masters, const std::vector<std::string>& includes) { released.push_back({ masters, includes }); },
            clock());
        watch->setQuiet(0.5);
        watch->watch({ almasterwatch_data::master(door) });
        UNSCOPED_INFO("watched at once");
        CHECK(watch->watching(door));
        put(door, "second", 200);
        looked();
        pass(0.6);
        UNSCOPED_INFO("released after the quiet set");
        REQUIRE(released.size() == size_t(1));
    }
}
