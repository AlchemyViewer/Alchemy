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

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace
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
        // through, as the viewer's is. Made when a test first pumps, and
        // gone with its fixture: one left alive would stand in for the
        // viewer's main loop for every test after it in the process. The
        // watched files go first, and with the last of them the watcher's
        // worker is joined, so nothing posts here once it is gone.
        std::unique_ptr<LL::WorkQueue> main_loop;

        // The timers ticked and the main loop run, as the viewer's frames
        // do, until `until` holds or a few seconds go by.
        bool pumped(const std::function<bool()>& until)
        {
            if (!main_loop)
            {
                main_loop = std::make_unique<LL::WorkQueue>("mainloop", 1024);
            }
            LLTimer timer;
            while (timer.getElapsedTimeF32() < 5.f)
            {
                LLEventTimer::updateClass();
                main_loop->runPending();
                if (until())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return false;
        }
    };

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: a write is heard once when it holds still; what was there is not", "[alwatchedfile]")
    {
        put("print(1)\n", 100);
        ALWatchedFile watched(file, hear());
        UNSCOPED_INFO("nothing yet");
        CHECK_FALSE(looked(watched));
        REQUIRE(heard.empty());

        put("print(2)\n", 200);
        UNSCOPED_INFO("the first look finds it moving");
        CHECK_FALSE(watched.check());
        UNSCOPED_INFO("the next finds it still");
        CHECK(watched.check());
        UNSCOPED_INFO("told once");
        REQUIRE(heard.size() == size_t(1));
        CHECK(heard[0] == file);
        CHECK(held[0] == std::string("print(2)\n"));
        UNSCOPED_INFO("and not again");
        CHECK_FALSE(looked(watched));
    }

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: two writes in one second are two changes of either size", "[alwatchedfile]")
    {
        put("a = 1\n", 100);
        ALWatchedFile watched(file, hear());
        put("a = 2\n", 300);
        CHECK(looked(watched));
        // The same size, the same second: the time alone tells them apart.
        put("a = 3\n", 500);
        CHECK(looked(watched));
        // The same time, a size of its own.
        put("a = 40\n", 500);
        CHECK(looked(watched));
        REQUIRE(heard.size() == size_t(3));
        CHECK(held[2] == std::string("a = 40\n"));
    }

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: the owner's own write is not heard but the next one from outside is", "[alwatchedfile]")
    {
        put("one\n", 100);
        ALWatchedFile watched(file, hear());
        put("two\n", 200);
        UNSCOPED_INFO("from outside");
        CHECK(looked(watched));

        // The owner's own, in the same second: a flag waiting on the next
        // change would never be taken by a write a time by the second
        // missed, and would take the editor's save after it.
        put("three\n", 300);
        watched.seen();
        UNSCOPED_INFO("the owner's own is no change");
        CHECK_FALSE(looked(watched));
        put("four\n", 400);
        UNSCOPED_INFO("the editor's save after it is heard");
        CHECK(looked(watched));
        REQUIRE(heard.size() == size_t(2));
        CHECK(held[1] == std::string("four\n"));
    }

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: a file caught half written is not read until it holds still", "[alwatchedfile]")
    {
        put("local x = 1\n", 100);
        ALWatchedFile watched(file, hear());
        // An editor saving in two steps: emptied, then filled.
        put("", 200);
        UNSCOPED_INFO("emptied: not still yet");
        CHECK_FALSE(watched.check());
        put("local x = 2\n", 300);
        UNSCOPED_INFO("filled: moved again");
        CHECK_FALSE(watched.check());
        CHECK(watched.check());
        REQUIRE(heard.size() == size_t(1));
        CHECK(held[0] == std::string("local x = 2\n"));
    }

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: a file gone or come back or put back older is each a change", "[alwatchedfile]")
    {
        put("kept\n", 500);
        ALWatchedFile watched(file, hear());
        std::error_code ec;
        fs::remove(fsyspath(file), ec);
        UNSCOPED_INFO("gone");
        CHECK(looked(watched));
        put("back\n", 600);
        UNSCOPED_INFO("back");
        CHECK(looked(watched));
        // Older than it was: a backup put back, which a later time only
        // would not see.
        fs::last_write_time(fsyspath(file), second, ec);
        UNSCOPED_INFO("older");
        CHECK(looked(watched));
        REQUIRE(heard.size() == size_t(3));
    }

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: a file that is not there yet is heard when it comes", "[alwatchedfile]")
    {
        ALWatchedFile watched(file, hear());
        UNSCOPED_INFO("nothing there, nothing heard");
        CHECK_FALSE(looked(watched));
        put("made\n", 100);
        CHECK(looked(watched));
        CHECK(held.at(0) == std::string("made\n"));
    }

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: the owner may let go of the watch as it is told", "[alwatchedfile]")
    {
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
        UNSCOPED_INFO("moving");
        REQUIRE_FALSE(watch.check());
        UNSCOPED_INFO("heard, and let go");
        CHECK(watch.check());
        REQUIRE_FALSE(watched);
        UNSCOPED_INFO("told of the file all the same");
        CHECK(told == file);
    }

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: a polled file is looked at off thread and told on the main loop", "[alwatchedfile]")
    {
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
        REQUIRE(pumped([&]() { return !heard.empty(); }));
        CHECK(held.at(0) == std::string("print(2)\n"));
        UNSCOPED_INFO("told on the main loop's thread");
        CHECK(told_on == here);
        // Still: not told again, however many looks go by.
        LLTimer timer;
        pumped([&]() { return timer.getElapsedTimeF32() > 0.5f; });
        UNSCOPED_INFO("told once");
        REQUIRE(heard.size() == size_t(1));
    }

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: a watch let go of is not told; the last may be let go of as it is told", "[alwatchedfile]")
    {
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
        REQUIRE(pumped([&]() { return !heard.empty(); }));
        REQUIRE_FALSE(kept);
        LLTimer timer;
        pumped([&]() { return timer.getElapsedTimeF32() > 0.4f; });
        UNSCOPED_INFO("the one kept, only");
        REQUIRE(heard.size() == size_t(1));
        CHECK(heard[0] == file);
    }

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: ALFileStamp holds a file's size and time and nothing for a folder", "[alwatchedfile]")
    {
        put("12345", 250);
        const ALFileStamp stamp = ALFileStamp::of(file);
        CHECK(stamp.exists);
        REQUIRE(stamp.size == std::uintmax_t(5));
        put("12345", 251);
        UNSCOPED_INFO("a millisecond later is another time");
        CHECK_FALSE(ALFileStamp::of(file) == stamp);
        UNSCOPED_INFO("a folder is no file");
        CHECK_FALSE(ALFileStamp::of(folder).exists);
        CHECK_FALSE(ALFileStamp::of(file + ".gone").exists);
    }

    TEST_CASE_METHOD(alwatchedfile_data, "alwatchedfile: a watch made from an earlier stamp starts from what it found", "[alwatchedfile]")
    {
        put("print(1)\n", 100);
        const ALFileStamp before = ALFileStamp::of(file);
        // Written after the look and before the watch is made: heard, since
        // the watch is from the look, not from the file as it is now.
        put("print(2)\n", 200);
        ALWatchedFile watched(file, before, hear());
        UNSCOPED_INFO("as the look found it");
        CHECK(watched.stamp() == before);
        UNSCOPED_INFO("what came after it heard");
        CHECK(looked(watched));
        REQUIRE(heard.size() == size_t(1));
        CHECK(held.at(0) == std::string("print(2)\n"));

        // From a look that found the file as it is: nothing to hear.
        ALWatchedFile current(file, ALFileStamp::of(file), hear());
        CHECK_FALSE(looked(current));

        // From a look that found nothing there -- the stamp of no file,
        // which a look of its own here would not have found: the file there
        // now is heard, which shows the watch took the look it was given.
        ALWatchedFile none(file, ALFileStamp(), hear());
        UNSCOPED_INFO("nothing there at the look");
        CHECK_FALSE(none.there());
        UNSCOPED_INFO("the file there now heard");
        CHECK(looked(none));
        REQUIRE(heard.size() == size_t(2));

        // And seen() takes the file as it is, whatever look it began from.
        put("print(3)\n", 300);
        ALWatchedFile owned(file, before, hear());
        owned.seen();
        UNSCOPED_INFO("the owner's own is no change");
        CHECK_FALSE(looked(owned));
        REQUIRE(heard.size() == size_t(2));
    }
}
