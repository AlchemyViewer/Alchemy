/**
 * @file alwatchedfile_test.cpp
 * @brief A file watched for whoever changes it, to the nanosecond.
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

#include "../alwatchedfile.h"

#include "../fsyspath.h"
#include "../lleventtimer.h"
#include "../llfile.h"
#include "../lltimer.h"
#include "../lluuid.h"
#include "../workqueue.h"

#include "../test/lltut.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace tut
{
    namespace fs = std::filesystem;

    struct alwatchedfile_data
    {
        std::string              folder;
        std::string              file;
        // What the owner was told, and what the file held as it was told.
        std::vector<std::string> heard;
        std::vector<std::string> held;
        // A second within which each test's times fall, well in the past.
        fs::file_time_type       second;

        alwatchedfile_data()
        {
            folder = fsyspath(fs::temp_directory_path() / fsyspath("alwatchedfile_" + LLUUID::generateNewID().asString())).string();
            std::error_code ec;
            fs::create_directories(fsyspath(folder), ec);
            file   = fsyspath(fsyspath(folder) / fsyspath("sl_script_test.luau")).string();
            second = std::chrono::floor<std::chrono::seconds>(fs::file_time_type::clock::now()) - std::chrono::hours(1);
        }
        ~alwatchedfile_data()
        {
            std::error_code ec;
            fs::remove_all(fsyspath(folder), ec);
        }

        ALWatchedFile::changed_t hear()
        {
            return [this](const std::string& path) {
                heard.push_back(path);
                held.push_back(read(path));
            };
        }

        static std::string read(const std::string& path)
        {
            llifstream        in(path, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }

        // Written, and dated `ms` milliseconds into the test's second: two
        // writes within one second, as far as a time by the second can
        // tell, which is where LLLiveFile goes wrong.
        void put(const std::string& text, int ms)
        {
            {
                llofstream out(file, std::ios::binary | std::ios::trunc);
                out << text;
            }
            std::error_code ec;
            fs::last_write_time(fsyspath(file), second + std::chrono::milliseconds(ms), ec);
        }

        // Two looks, as the timer would make them: a change is said once it
        // has held still from one look to the next.
        static bool looked(ALWatchedFile& watched)
        {
            const bool first = watched.check();
            return watched.check() || first;
        }

        // The main loop's queue, which the watcher hands its looks back
        // through, as the viewer's is.
        static LL::WorkQueue::ptr_t mainLoop()
        {
            static LL::WorkQueue queue("mainloop", 1024);
            return LL::WorkQueue::getInstance("mainloop");
        }

        // The timers ticked and the main loop run, as the viewer's frames
        // do, until `until` holds or a few seconds go by.
        static bool pumped(const std::function<bool()>& until)
        {
            const LL::WorkQueue::ptr_t main = mainLoop();
            LLTimer                    timer;
            while (timer.getElapsedTimeF32() < 5.f)
            {
                LLEventTimer::updateClass();
                main->runPending();
                if (until())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return false;
        }
    };

    typedef test_group<alwatchedfile_data> alwatchedfile_group;
    typedef alwatchedfile_group::object    alwatchedfile_object;
    alwatchedfile_group                    alwatchedfile_instance("alwatchedfile");

    template<> template<>
    void alwatchedfile_object::test<1>()
    {
        set_test_name("what is there when the watch begins is seen, and a write is heard once, after it holds still");
        put("print(1)\n", 100);
        ALWatchedFile watched(file, hear());
        ensure("nothing yet", !looked(watched));
        ensure("nobody told", heard.empty());

        put("print(2)\n", 200);
        ensure("the first look finds it moving", !watched.check());
        ensure("the next finds it still", watched.check());
        ensure_equals("told once", heard.size(), size_t(1));
        ensure_equals("of the file", heard[0], file);
        ensure_equals("which held what was written", held[0], std::string("print(2)\n"));
        ensure("and not again", !looked(watched));
    }

    template<> template<>
    void alwatchedfile_object::test<2>()
    {
        set_test_name("two writes in one second are two changes, of one size or another");
        put("a = 1\n", 100);
        ALWatchedFile watched(file, hear());
        put("a = 2\n", 300);
        ensure("the first", looked(watched));
        // The same size, the same second: the time alone tells them apart.
        put("a = 3\n", 500);
        ensure("the second", looked(watched));
        // The same time, a size of its own.
        put("a = 40\n", 500);
        ensure("the third", looked(watched));
        ensure_equals("each heard", heard.size(), size_t(3));
        ensure_equals("the last as it was written", held[2], std::string("a = 40\n"));
    }

    template<> template<>
    void alwatchedfile_object::test<3>()
    {
        set_test_name("a write of the owner's own is seen, and the next write from outside is heard all the same");
        put("one\n", 100);
        ALWatchedFile watched(file, hear());
        put("two\n", 200);
        ensure("from outside", looked(watched));

        // The owner's own, in the same second: a flag waiting on the next
        // change would never be taken by a write a time by the second
        // missed, and would take the editor's save after it.
        put("three\n", 300);
        watched.seen();
        ensure("the owner's own is no change", !looked(watched));
        put("four\n", 400);
        ensure("the editor's save after it is heard", looked(watched));
        ensure_equals("two heard", heard.size(), size_t(2));
        ensure_equals("the editor's", held[1], std::string("four\n"));
    }

    template<> template<>
    void alwatchedfile_object::test<4>()
    {
        set_test_name("a file caught half written is not read until it holds still");
        put("local x = 1\n", 100);
        ALWatchedFile watched(file, hear());
        // An editor saving in two steps: emptied, then filled.
        put("", 200);
        ensure("emptied: not still yet", !watched.check());
        put("local x = 2\n", 300);
        ensure("filled: moved again", !watched.check());
        ensure("still", watched.check());
        ensure_equals("heard once", heard.size(), size_t(1));
        ensure_equals("filled", held[0], std::string("local x = 2\n"));
    }

    template<> template<>
    void alwatchedfile_object::test<5>()
    {
        set_test_name("a file gone is a change, one come back is another, and one put back older is a third");
        put("kept\n", 500);
        ALWatchedFile watched(file, hear());
        std::error_code ec;
        fs::remove(fsyspath(file), ec);
        ensure("gone", looked(watched));
        put("back\n", 600);
        ensure("back", looked(watched));
        // Older than it was: a backup put back, which a later time only
        // would not see.
        fs::last_write_time(fsyspath(file), second, ec);
        ensure("older", looked(watched));
        ensure_equals("three heard", heard.size(), size_t(3));
    }

    template<> template<>
    void alwatchedfile_object::test<6>()
    {
        set_test_name("a file that is not there yet is heard when it comes");
        ALWatchedFile watched(file, hear());
        ensure("nothing there, nothing heard", !looked(watched));
        put("made\n", 100);
        ensure("made", looked(watched));
        ensure_equals("its text", held.at(0), std::string("made\n"));
    }

    template<> template<>
    void alwatchedfile_object::test<7>()
    {
        set_test_name("the owner may let go of the watch as it is told");
        put("before\n", 100);
        std::unique_ptr<ALWatchedFile> watched;
        std::string                    told;
        watched = std::make_unique<ALWatchedFile>(file, [&](const std::string& path) {
            // Gone as it is heard: the path it was told of is its own copy.
            watched.reset();
            told = path;
        });
        put("after\n", 200);
        ALWatchedFile& watch = *watched;
        ensure("moving", !watch.check());
        ensure("heard, and let go", watch.check());
        ensure("gone", !watched);
        ensure_equals("told of the file all the same", told, file);
    }

    template<> template<>
    void alwatchedfile_object::test<8>()
    {
        set_test_name("a polled file is looked at on the watcher's thread, and its owner told on the main loop");
        put("print(1)\n", 100);
        const std::thread::id here = std::this_thread::get_id();
        std::thread::id       told_on;
        ALWatchedFile         watched(file, [&](const std::string& path) {
            heard.push_back(path);
            held.push_back(alwatchedfile_data::read(path));
            told_on = std::this_thread::get_id();
        });
        watched.poll(0.05f);
        put("print(2)\n", 200);
        ensure("heard", pumped([&]() { return !heard.empty(); }));
        ensure_equals("what was written", held.at(0), std::string("print(2)\n"));
        ensure("told on the main loop's thread", told_on == here);
        // Still: not told again, however many looks go by.
        LLTimer timer;
        pumped([&]() { return timer.getElapsedTimeF32() > 0.5f; });
        ensure_equals("told once", heard.size(), size_t(1));
    }

    template<> template<>
    void alwatchedfile_object::test<9>()
    {
        set_test_name("a file let go of is not told, and the others are; the owner may let go of the last as it is told");
        const std::string other = fsyspath(fsyspath(folder) / fsyspath("sl_script_other.lsl")).string();
        put("one\n", 100);
        {
            llofstream out(other, std::ios::binary);
            out << "default {}\n";
        }
        std::unique_ptr<ALWatchedFile> kept;
        kept = std::make_unique<ALWatchedFile>(file, [&](const std::string& path) {
            heard.push_back(path);
            kept.reset();
        });
        std::unique_ptr<ALWatchedFile> dropped = std::make_unique<ALWatchedFile>(other, [&](const std::string& path) { heard.push_back(path); });
        kept->poll(0.05f);
        dropped->poll(0.05f);
        dropped.reset();
        put("two\n", 200);
        {
            llofstream out(other, std::ios::binary | std::ios::trunc);
            out << "default { state_entry() {} }\n";
        }
        ensure("heard", pumped([&]() { return !heard.empty(); }));
        ensure("and let go", !kept);
        LLTimer timer;
        pumped([&]() { return timer.getElapsedTimeF32() > 0.4f; });
        ensure_equals("the one kept, only", heard.size(), size_t(1));
        ensure_equals("which is the file", heard[0], file);
    }

    template<> template<>
    void alwatchedfile_object::test<10>()
    {
        set_test_name("a stamp: a file's size and time, and nothing for a folder or nothing at all");
        put("12345", 250);
        const ALFileStamp stamp = ALFileStamp::of(file);
        ensure("there", stamp.exists);
        ensure_equals("its size", stamp.size, std::uintmax_t(5));
        put("12345", 251);
        ensure("a millisecond later is another time", !(ALFileStamp::of(file) == stamp));
        ensure("a folder is no file", !ALFileStamp::of(folder).exists);
        ensure("nothing is nothing", !ALFileStamp::of(file + ".gone").exists);
    }
}
