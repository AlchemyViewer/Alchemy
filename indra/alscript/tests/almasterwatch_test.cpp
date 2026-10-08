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

#include "../test/lltut.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tut
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

    typedef test_group<almasterwatch_data> almasterwatch_group;
    typedef almasterwatch_group::object    almasterwatch_object;
    almasterwatch_group                    almasterwatch_instance("almasterwatch");

    template<> template<>
    void almasterwatch_object::test<1>()
    {
        set_test_name("a master saved outside is released once the burst has been quiet a second");
        const std::string door = in("door.lsl");
        put(door, "default {}\n", 100);
        ALMasterWatch& watched = make();
        watched.watch({ almasterwatch_data::master(door) });
        ensure("watched at once, its look answered here", watched.watching(door));
        looked();
        pass(5.0);
        ensure("what was there already is no change", released.empty());

        put(door, "default { state_entry() {} }\n", 200);
        watched.lookNow();
        ensure("moving: not heard yet", waiting.empty());
        watched.lookNow();
        pass(0.9);
        ensure("not before the quiet", released.empty());
        pass(0.2);
        ensure_equals("released once", released.size(), size_t(1));
        ensure_equals("the master", almasterwatch_data::joined(released[0].masters), std::string("door.lsl"));
        ensure("no include", released[0].includes.empty());
        looked();
        pass(5.0);
        ensure_equals("and not again", released.size(), size_t(1));
    }

    template<> template<>
    void almasterwatch_object::test<2>()
    {
        set_test_name("a checkout of many files is released once, all together, after the last of them");
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
        ensure("the burst goes on: nothing yet", released.empty());
        pass(0.2);
        ensure_equals("released once", released.size(), size_t(1));
        ensure_equals("all ten together", released[0].masters.size(), size_t(10));
    }

    template<> template<>
    void almasterwatch_object::test<3>()
    {
        set_test_name("masters and includes are told apart as they are released");
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
        ensure_equals("released once", released.size(), size_t(1));
        ensure_equals("the master", almasterwatch_data::joined(released[0].masters), std::string("door.lsl"));
        ensure_equals("the include", almasterwatch_data::joined(released[0].includes), std::string("util.lsl"));
    }

    template<> template<>
    void almasterwatch_object::test<4>()
    {
        set_test_name("an emptied file is held a moment and a half, and one filled again meanwhile no longer than the quiet");
        const std::string door = in("door.lsl");
        put(door, "default {}\n", 100);
        ALMasterWatch& watched = make();
        watched.watch({ almasterwatch_data::master(door) });

        put(door, "", 200);
        looked();
        pass(1.1);
        ensure("quiet, but emptied: held", released.empty());
        pass(0.5);
        ensure_equals("taken as empty after the hold", released.size(), size_t(1));

        // An editor saving in two steps: emptied, then filled.
        put(door, "default { touch_start(integer n) {} }\n", 300);
        looked();
        pass(1.1);
        ensure_equals("filled again: released", released.size(), size_t(2));
        put(door, "", 400);
        looked();
        pass(0.3);
        put(door, "default { state_entry() {} }\n", 500);
        looked();
        pass(0.9);
        ensure("a second after it was filled: not yet", released.size() == 2);
        pass(0.2);
        ensure_equals("then, before the emptied hold would have ended", released.size(), size_t(3));
        ensure_equals("the file, once", released[2].masters.size(), size_t(1));
    }

    template<> template<>
    void almasterwatch_object::test<5>()
    {
        set_test_name("what the studio writes itself is no change, and takes with it a change waiting in the burst");
        const std::string door = in("door.lsl");
        put(door, "a", 100);
        ALMasterWatch& watched = make();
        watched.watch({ almasterwatch_data::master(door) });

        put(door, "studio", 200);
        watched.seen(door);
        looked();
        pass(5.0);
        ensure("the studio's own write: nothing", released.empty());

        // Saved outside, and heard; then written by the studio before the
        // burst ends, which sends it itself.
        put(door, "outside", 300);
        looked();
        put(door, "studio again", 400);
        watched.seen(door);
        pass(5.0);
        ensure("nothing waiting on it from before", released.empty());
        looked();
        pass(5.0);
        ensure("and nothing after", released.empty());
    }

    template<> template<>
    void almasterwatch_object::test<6>()
    {
        set_test_name("a file the studio writes while its first look is out is watched from that write, and a save after it is heard");
        const std::string door = in("door.lsl");
        put(door, "first", 100);
        ALMasterWatch& watched = make(/*look_at_once*/ false);
        watched.watch({ almasterwatch_data::master(door) });
        ensure("waiting on its first look", watched.waiting(door) && !watched.watching(door));
        ensure_equals("asked once", asked.size(), size_t(1));

        // The studio writes it, then somebody else saves it, all before the
        // look answers: the save is heard, the studio's write is not.
        put(door, "studio", 200);
        watched.seen(door);
        put(door, "outside", 300);
        answer(0);
        ensure("watched now", watched.watching(door));
        looked();
        pass(1.1);
        ensure_equals("the save after the studio's write heard", released.size(), size_t(1));
    }

    template<> template<>
    void almasterwatch_object::test<7>()
    {
        set_test_name("a file the studio writes while its first look is out is no change, whenever the look was made");
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
        ensure("both watched", watched.watching(door) && watched.watching(lamp));
        looked();
        pass(5.0);
        ensure("the studio's write is no change", released.empty());
    }

    template<> template<>
    void almasterwatch_object::test<8>()
    {
        set_test_name("a first look answering for a file no longer wanted makes no watch of it");
        const std::string door = in("door.lsl");
        put(door, "first", 100);
        ALMasterWatch& watched = make(/*look_at_once*/ false);
        watched.watch({ almasterwatch_data::master(door) });
        watched.watch({});
        ensure("not waiting now", !watched.waiting(door));
        answer(0);
        ensure("not watched", !watched.watching(door));
        ensure_equals("nothing at all", watched.size(), size_t(0));
        put(door, "changed", 200);
        looked();
        pass(5.0);
        ensure("nothing heard", released.empty());
    }

    template<> template<>
    void almasterwatch_object::test<9>()
    {
        set_test_name("a file let go of and wanted again waits on the newer look; one wanted still keeps the look it waits on");
        const std::string door = in("door.lsl");
        const std::string lamp = in("lamp.lsl");
        put(door, "first", 100);
        put(lamp, "first", 100);
        ALMasterWatch& watched = make(/*look_at_once*/ false);
        watched.watch({ almasterwatch_data::master(door) });
        watched.watch({});
        watched.watch({ almasterwatch_data::master(door) });
        ensure_equals("asked again", asked.size(), size_t(2));
        answer(0);
        ensure("the older look makes no watch", !watched.watching(door) && watched.waiting(door));
        answer(1);
        ensure("the newer one does", watched.watching(door));

        // Wanted still while another is added: the look it waits on answers
        // it, and only the new file is asked after.
        watched.watch({ almasterwatch_data::master(door), almasterwatch_data::master(lamp) });
        ensure("door watched already: lamp alone asked", asked.size() == 3 && asked[2].paths.size() == 1);
        const std::string other = in("other.lsl");
        watched.watch({ almasterwatch_data::master(door), almasterwatch_data::master(lamp), almasterwatch_data::master(other) });
        ensure_equals("lamp waits on its look still: other alone asked", asked[3].paths.size(), size_t(1));
        answer(3);
        ensure("other watched", watched.watching(other));
        ensure("lamp waits on its own", watched.waiting(lamp));
        answer(2);
        ensure("lamp watched", watched.watching(lamp));
    }

    template<> template<>
    void almasterwatch_object::test<10>()
    {
        set_test_name("a master is looked at twice a second, an include once, and everything every two seconds past 256 files");
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
        ensure_equals("a master", *watch->periodOf(door), ALMasterWatch::MASTER_PERIOD);
        ensure_equals("an include", *watch->periodOf(util), ALMasterWatch::INCLUDE_PERIOD);
        ensure_equals("both looked at first", looks, size_t(2));

        std::vector<ALMasterLinks::Watched> many = { almasterwatch_data::master(door), almasterwatch_data::include(util) };
        for (size_t i = 0; i < ALMasterWatch::MANY - 1; ++i)
        {
            many.push_back(almasterwatch_data::master(in("m" + std::to_string(i) + ".lsl")));
        }
        watch->watch(many);
        ensure_equals("257 watched", watch->size(), size_t(257));
        ensure_equals("past 256: a master every two seconds", *watch->periodOf(door), ALMasterWatch::MANY_PERIOD);
        ensure_equals("and an include", *watch->periodOf(util), ALMasterWatch::MANY_PERIOD);
        ensure_equals("only the new ones looked at", looks, size_t(257));

        watch->watch({ almasterwatch_data::master(door), almasterwatch_data::include(util) });
        ensure_equals("fewer again: a master's own", *watch->periodOf(door), ALMasterWatch::MASTER_PERIOD);
        ensure_equals("an include's own", *watch->periodOf(util), ALMasterWatch::INCLUDE_PERIOD);
        ensure_equals("nothing looked at again", looks, size_t(257));
        ensure("one not watched has no period", !watch->periodOf(in("m0.lsl")));
    }

    template<> template<>
    void almasterwatch_object::test<11>()
    {
        set_test_name("with no main loop to answer through, the first looks are made at once, here");
        const std::string door = in("door.lsl");
        put(door, "first", 100);
        watch = std::make_unique<ALMasterWatch>(
            [this](const std::vector<std::string>& masters, const std::vector<std::string>& includes) { released.push_back({ masters, includes }); },
            clock());
        watch->setQuiet(0.5);
        watch->watch({ almasterwatch_data::master(door) });
        ensure("watched at once", watch->watching(door));
        put(door, "second", 200);
        looked();
        pass(0.6);
        ensure_equals("released after the quiet set", released.size(), size_t(1));
    }
}
