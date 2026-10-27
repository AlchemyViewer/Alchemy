/**
 * @file alcvarfile_test.cpp
 * @brief Settings kept in settings.db: rows and history written as the store
 *        flushes, tombstones, renames, scope moves, conversions at open,
 *        rows left alone, the account swap, and a process killed.
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

#include "alcvartestschema.h"

#include "aldbfaultvfs.h"
#include "altesthelper.h"
#include "fsyspath.h"
#include "llcoros.h"
#include "lleventcoro.h"
#include "llevents.h"
#include "llprocess.h"
#include "llsd.h"
#include "llstring.h"
#include "lltimer.h"
#include "threadpool.h"

#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <optional>

using namespace std::chrono_literals;

namespace
{
    namespace render  = cvartest::render;
    namespace shadows = cvartest::render::shadows;
    namespace ui      = cvartest::ui;
    namespace chat    = cvartest::chat;

    const char* const KILLED_PATH   = "AL_ALCVAR_TEST_KILLED_PATH";
    const char* const KILLED_MARKER = "AL_ALCVAR_TEST_KILLED_MARKER";

    std::unique_ptr<ALDbConnection> raw(const std::string& path)
    {
        int         rc = SQLITE_OK;
        std::string error;
        auto        connection = ALDbConnection::open(path, ALDbConnection::Access::ReadWrite, {}, &rc, &error);
        INFO(error);
        REQUIRE(connection);
        return connection;
    }

    // A row of a settings file as SQL sees it.
    struct RawRow
    {
        S64         type = -1;
        std::string unit;
        S64         decl = 0;
        std::string value;
        S64         tombstone = -1;
        S64         modified  = 0;
    };

    std::optional<RawRow> rowOf(const std::string& path, std::string_view name)
    {
        auto          file = raw(path);
        ALDbStatement read = file->prepare(
            "SELECT type, coalesce(unit, ''), decl, coalesce(cast(value AS TEXT), 'NULL'), tombstone, modified "
            "FROM settings WHERE name = ?1");
        read.bind(1, name);
        if (!read.step())
        {
            return std::nullopt;
        }
        return RawRow{ read.columnInt(0), std::string(read.columnText(1)), read.columnInt(2),
                       std::string(read.columnText(3)), read.columnInt(4), read.columnInt(5) };
    }

    // Writes a row as a viewer other than this one might have, in a file
    // with the settings tables: at the step `decl` names, 0 for none.
    void putRow(const std::string& path, std::string_view name, S64 type, std::string_view unit, U64 decl,
                std::string_view valueSql, S64 modified, bool tombstone = false)
    {
        auto file = raw(path);
        file->prepare("INSERT OR REPLACE INTO settings(name, type, unit, decl, value, tombstone, modified) "
                      "VALUES (?1, ?2, ?3, ?4, " +
                      std::string(valueSql) + ", ?5, ?6)")
            .bind(1, name)
            .bind(2, type)
            .bind(3, unit)
            .bind(4, S64(decl))
            .bind(5, tombstone)
            .bind(6, modified)
            .run();
        REQUIRE(file->firstError() == SQLITE_OK);
    }

    // Keeps a row as a migrate's rewrite keeps it, for a viewer of its step.
    void putPrior(const std::string& path, std::string_view name, U64 decl, S64 type, std::string_view valueSql, S64 modified)
    {
        auto file = raw(path);
        file->prepare("INSERT INTO prior(name, decl, type, unit, value, modified) VALUES (?1, ?2, ?3, NULL, " +
                      std::string(valueSql) + ", ?4)")
            .bind(1, name)
            .bind(2, S64(decl))
            .bind(3, type)
            .bind(4, modified)
            .run();
        REQUIRE(file->firstError() == SQLITE_OK);
    }

    // A settings file with its tables and nothing in them.
    std::string emptyFile(const ll_test::ALDbTempDir& dir, const std::string& name)
    {
        const std::string path = dir.file(name);
        {
            ALCVarFile file(path, {});
            REQUIRE(file.open().usable());
            file.close();
        }
        return path;
    }

    S64 count(const std::string& path, const std::string& sql)
    {
        return raw(path)->queryInt(sql).value_or(-1);
    }

    // The store as the next run of the viewer makes it.
    void restart()
    {
        ALCVars::deleteSingleton();
        ALCVars::resetSlots(cvartest::schema());
        ALCVars::instance().registerSchema(cvartest::schema());
    }

    // A store whose files are in a directory of the case's own, which
    // outlives it: the files are closed before the directory goes.
    struct FileStore
    {
        ll_test::ALDbTempDir dir;
        TestStore            test;

        ALCVars& store() { return test.store(); }
    };

    // An environment variable set for a while, and put back as it was.
    class ScopedEnvironment
    {
    public:
        ScopedEnvironment(const char* name, const std::string& value)
        :   mName(name),
            mBefore(LLStringUtil::getoptenv(name))
        {
            set(value.c_str());
        }
        ~ScopedEnvironment() { set(mBefore ? mBefore->c_str() : nullptr); }

    private:
        void set(const char* value)
        {
#if LL_WINDOWS
            _putenv_s(mName.c_str(), value ? value : "");
#else
            if (value)
            {
                setenv(mName.c_str(), value, 1);
            }
            else
            {
                unsetenv(mName.c_str());
            }
#endif
        }
        std::string                mName;
        std::optional<std::string> mBefore;
    };

    // Takes a file's write permission away, and gives it back as it goes.
    class ReadOnlyFile
    {
    public:
        explicit ReadOnlyFile(const std::string& path)
        :   mPath(path)
        {
            namespace fs = std::filesystem;
            fs::permissions(fsyspath(mPath), fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                            fs::perm_options::remove);
        }

        ~ReadOnlyFile()
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::permissions(fsyspath(mPath), fs::perms::owner_write, fs::perm_options::add, ec);
        }

        ReadOnlyFile(const ReadOnlyFile&)            = delete;
        ReadOnlyFile& operator=(const ReadOnlyFile&) = delete;

    private:
        std::string mPath;
    };

    // A file's bytes.
    std::string contents(const std::string& path)
    {
        std::ifstream in(std::filesystem::path(fsyspath(path)), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // A file of another kind, which a settings file's open refuses.
    std::string otherKindOfFile(const ll_test::ALDbTempDir& dir, const std::string& name)
    {
        const std::string path = dir.file(name);
        REQUIRE(raw(path)->exec("PRAGMA application_id = 1234; CREATE TABLE other(x)") == SQLITE_OK);
        return path;
    }

    // Waits for the files opened through the fault VFS to close, and for
    // the pools of writers let go to end with them.
    void waitForHeldFiles(size_t pools)
    {
        CHECK(ll_test::waitUntil(30s, [] { return ll_test::ALDbFaultVfs::openFiles() == 0; }));
        CHECK(ll_test::waitUntil(30s, [pools] { return LL::ThreadPoolBase::instanceCount() <= pools; }));
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a value is written as its type unit and step",
                     "[alcvarfile]")
    {
        const std::string    path = dir.file("settings.db");
        ALCVars&             cvars = store();
        REQUIRE(cvars.openGlobal(path));
        CHECK(render::draw_distance.set(256.f));
        CHECK(shadows::tint.set(LLColor4(0.5f, 0.25f, 0.f, 1.f)));
        CHECK(ui::sounds::click.set(LLUUID("b4ba225c-373f-446d-9f7e-6cb7b5cf9b3d")));
        cvars.flushNow();

        const auto distance = rowOf(path, "render.draw_distance");
        REQUIRE(distance);
        CHECK(distance->type == S64(ALCVarType::F32));
        CHECK(distance->unit == "metres");
        CHECK(U64(distance->decl) == render::draw_distance.desc().decl());
        CHECK(distance->value == "256.0");
        CHECK(distance->tombstone == 0);
        CHECK(distance->modified > 0);
        // Readable with sqlite3 when support asks.
        CHECK(*ALCVarValues::fromJson(ALCVarType::Color4, rowOf(path, "render.shadows.tint")->value) ==
              ALCVarValue(LLColor4(0.5f, 0.25f, 0.f, 1.f)));
        CHECK(rowOf(path, "ui.sounds.click")->value == "b4ba225c-373f-446d-9f7e-6cb7b5cf9b3d");
        CHECK(count(path, "PRAGMA application_id") == S64(ALCVarFile::APPLICATION_ID));

        restart();
        REQUIRE(ALCVars::instance().openGlobal(path));
        CHECK(render::draw_distance() == 256.f);
        CHECK(shadows::tint() == LLColor4(0.5f, 0.25f, 0.f, 1.f));
        CHECK(ui::sounds::click() == LLUUID("b4ba225c-373f-446d-9f7e-6cb7b5cf9b3d"));
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a slider dragged through a hundred values writes one row once",
                     "[alcvarfile]")
    {
        const std::string    path  = dir.file("settings.db");
        ALCVars&             cvars = store();
        REQUIRE(cvars.openGlobal(path));
        for (int i = 0; i < 100; ++i)
        {
            CHECK(shadows::resolution_scale.set(0.25f + 0.03f * F32(i)));
        }
        cvars.flushNow();
        CHECK(count(path, "SELECT count(*) FROM settings") == 1);
        CHECK(count(path, "SELECT count(*) FROM history") == 1);
        CHECK(count(path, "SELECT count(*) FROM history WHERE old IS NULL AND name = 'render.shadows.resolution_scale'") ==
              1);
        CHECK(count(path, "SELECT count(*) FROM history WHERE session = " + std::to_string(cvars.session())) == 1);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: changes are written after the quiet time on their own",
                     "[alcvarfile][slow]")
    {
        const std::string    path  = dir.file("settings.db");
        ALCVars&             cvars = store();
        cvars.setFlushDelays(20ms, 200ms);
        REQUIRE(cvars.openGlobal(path));
        CHECK(render::draw_distance.set(300.f));
        CHECK(ll_test::waitUntil(5s, [&] { return count(path, "SELECT count(*) FROM settings") == 1; }));
        // A batch is one transaction, written once it closes.
        {
            ALCVars::Batch batch;
            for (int i = 0; i < 10; ++i)
            {
                CHECK(render::quality_level.set(i % 7));
                ms_sleep(5);
            }
            CHECK(shadows::detail.set(1));
            ms_sleep(60);
            CHECK(count(path, "SELECT count(*) FROM settings") == 1);
        }
        CHECK(ll_test::waitUntil(5s, [&] { return count(path, "SELECT count(*) FROM settings") == 3; }));
    }

    TEST_CASE("alcvarfile: the flusher's thread goes when the store closes", "[alcvarfile]")
    {
        const size_t pools  = LL::ThreadPoolBase::instanceCount();
        const size_t queues = LL::WorkQueueBase::instanceCount();
        {
            FileStore files;
            REQUIRE(files.store().openGlobal(files.dir.file("settings.db")));
            CHECK(LL::ThreadPoolBase::instanceCount() > pools);
            CHECK(render::draw_distance.set(300.f));
            files.store().close();
            CHECK(LL::ThreadPoolBase::instanceCount() == pools);
            CHECK(LL::WorkQueueBase::instanceCount() == queues);
            // A store opened again starts a flusher of its own.
            REQUIRE(files.store().openGlobal(files.dir.file("settings.db")));
            CHECK(render::draw_distance() == 300.f);
        }
        CHECK(LL::ThreadPoolBase::instanceCount() == pools);
        CHECK(LL::WorkQueueBase::instanceCount() == queues);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a flush the writer drops is written with the next", "[alcvarfile]")
    {
        const std::string path  = dir.file("settings.db");
        ALCVars&          cvars = store();
        REQUIRE(cvars.openGlobal(path));
        cvars.flushNow();
        // A rule of the file's that refuses one name, as damage would refuse
        // a page: the flush that carries it fails whole, for good.
        REQUIRE(raw(path)->exec("CREATE TRIGGER refuse BEFORE INSERT ON settings "
                                "WHEN NEW.name = 'render.draw_distance' BEGIN SELECT RAISE(ABORT, 'refused'); END") ==
                SQLITE_OK);
        CHECK(render::draw_distance.set(300.f));
        CHECK(shadows::detail.set(1));
        cvars.flushNow();
        CHECK_FALSE(rowOf(path, "render.draw_distance"));
        CHECK_FALSE(rowOf(path, "render.shadows.detail"));

        REQUIRE(raw(path)->exec("DROP TRIGGER refuse") == SQLITE_OK);
        CHECK(shadows::detail.set(0));
        CHECK(ui::scale.set(1.5f));
        cvars.flushNow();
        const auto distance = rowOf(path, "render.draw_distance");
        REQUIRE(distance);
        CHECK(distance->value == "300.0");
        CHECK(rowOf(path, "render.shadows.detail")->value == "0");
        CHECK(rowOf(path, "ui.scale")->value == "1.5");
        // From the value before the dropped flush, once.
        CHECK(count(path, "SELECT count(*) FROM history WHERE name = 'render.shadows.detail' AND old IS NULL AND new = 0") ==
              1);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a failure the writer cannot get past is told", "[alcvarfile]")
    {
        ll_test::ALDbFaults faults;
        const std::string   path  = dir.file("settings.db");
        ALCVars&            cvars = store();
        std::atomic<int>    told{ 0 };
        ALCVarFileOptions   options;
        options.vfs       = ll_test::ALDbFaultVfs::name();
        options.onFailure = [&told](const ALDbFailure& failure) {
            if ((failure.code & 0xff) == SQLITE_FULL)
            {
                ++told;
            }
        };
        cvars.setFlushDelays(1ms, 5ms);
        REQUIRE(cvars.openGlobal(path, options));
        cvars.flushNow();
        ll_test::ALDbFaultVfs::failWrites(SQLITE_FULL);
        CHECK(render::draw_distance.set(300.f));
        CHECK(ll_test::waitUntil(10s, [&told] { return told.load() > 0; }));
        // The disk has room again: written, nothing lost.
        ll_test::ALDbFaultVfs::failWrites(SQLITE_OK);
        CHECK(ll_test::waitUntil(10s, [&path] {
            const auto row = rowOf(path, "render.draw_distance");
            return row && row->value == "300.0";
        }));
        cvars.close();
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a file that may only be read is read and nothing is written to it",
                     "[alcvarfile]")
    {
        const std::string path = emptyFile(dir, "settings.db");
        putRow(path, "render.draw_distance", S64(ALCVarType::F32), "metres", render::draw_distance.desc().decl(), "300.0",
               1000);
        const std::string before = contents(path);
        {
            ReadOnlyFile      read_only(path);
            std::atomic<int>  told{ 0 };
            ALCVarFileOptions options;
            options.onFailure = [&told](const ALDbFailure& failure) {
                if ((failure.code & 0xff) == SQLITE_READONLY)
                {
                    ++told;
                }
            };
            ALCVars& cvars = store();
            REQUIRE(cvars.openGlobal(path, options));
            CHECK(render::draw_distance() == 300.f);
            CHECK(told.load() == 1);
            // Kept for the session.
            CHECK(render::draw_distance.set(400.f));
            CHECK(shadows::detail.set(1));
            cvars.flushNow();
            CHECK(render::draw_distance() == 400.f);
            cvars.close();
            CHECK(told.load() == 1);
        }
        CHECK(contents(path) == before);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a file usable only in memory is not kept", "[alcvarfile]")
    {
        // Not a database, and this process may not move it aside.
        const std::string path = dir.file("settings.db");
        std::ofstream(std::filesystem::path(fsyspath(path)), std::ios::binary) << std::string(8192, 'x');
        ALCVarFileOptions options;
        options.mayQuarantine = false;
        CHECK_FALSE(store().openGlobal(path, options));
        CHECK(render::draw_distance.set(300.f));
        store().flushNow();
        CHECK(render::draw_distance() == 300.f);
        store().close();
        CHECK(contents(path) == std::string(8192, 'x'));
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a file read only partway is not taken or written over", "[alcvarfile]")
    {
        const std::string path = emptyFile(dir, "settings.db");
        {
            auto file = raw(path);
            REQUIRE(file->exec("BEGIN") == SQLITE_OK);
            const std::string filler(200, 'f');
            for (int i = 0; i < 2000; ++i)
            {
                file->prepare("INSERT INTO settings(name, type, unit, decl, value, tombstone, modified) "
                              "VALUES (?1, 6, NULL, 0, ?2, 0, 1000)")
                    .bind(1, std::string_view(fmt::format("a.filler.{:04}", i)))
                    .bind(2, std::string_view(filler))
                    .run();
            }
            REQUIRE(file->exec("COMMIT") == SQLITE_OK);
        }
        // Past the filler, in key order.
        putRow(path, "render.draw_distance", S64(ALCVarType::F32), "metres", render::draw_distance.desc().decl(), "300.0",
               1000);
        const S64 page  = count(path, "PRAGMA page_size");
        const S64 pages = count(path, "PRAGMA page_count");
        REQUIRE(pages > 40);
        {
            // A page in the middle of the table damaged, as a disk damages one.
            std::fstream file(std::filesystem::path(fsyspath(path)), std::ios::in | std::ios::out | std::ios::binary);
            file.seekp(std::streamoff((pages / 2) * page));
            const std::string junk(size_t(page), char(0xff));
            file.write(junk.data(), std::streamsize(junk.size()));
        }
        const std::string before = contents(path);

        ALCVars& cvars = store();
        CHECK_FALSE(cvars.openGlobal(path));
        CHECK(render::draw_distance() == 128.f);
        CHECK(render::draw_distance.set(500.f));
        cvars.flushNow();
        cvars.close();
        CHECK(contents(path) == before);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: an account's changes are written with no global file", "[alcvarfile]")
    {
        const std::string global  = otherKindOfFile(dir, "settings.db");
        const std::string account = emptyFile(dir, "account.db");
        ALCVars&          cvars   = store();
        cvars.setFlushDelays(1ms, 5ms);
        CHECK_FALSE(cvars.openGlobal(global));
        REQUIRE(cvars.openAccount(account));
        cvars.confirmAccount();
        CHECK(chat::font_size.set(2));
        // Written by the flusher, with nothing asking.
        CHECK(ll_test::waitUntil(10s, [&account] {
            const auto row = rowOf(account, "chat.font_size");
            return row && row->value == "2";
        }));
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: flushNow and closeAccount wait no longer than a file's close",
                     "[alcvarfile][slow]")
    {
        const size_t        pools = LL::ThreadPoolBase::instanceCount();
        ll_test::ALDbFaults faults;
        ALCVarFileOptions   held;
        held.vfs       = ll_test::ALDbFaultVfs::name();
        ALCVars& cvars = store();
        REQUIRE(cvars.openGlobal(dir.file("settings.db"), held));
        REQUIRE(cvars.openAccount(dir.file("account.db"), held));
        cvars.confirmAccount();
        cvars.flushNow();

        // A disk that hangs, under both files.
        ll_test::ALDbFaultVfs::holdWrites(true);
        CHECK(render::draw_distance.set(300.f));
        CHECK(chat::font_size.set(2));
        const F64 wait = F64(ALCVarFile::CLOSE_WAIT.count()) / 1000.0;
        LLTimer   timer;
        cvars.flushNow();
        CHECK(timer.getElapsedTimeF64() < wait * 1.5);
        timer.reset();
        cvars.closeAccount();
        CHECK(timer.getElapsedTimeF64() < wait * 1.5);

        ll_test::ALDbFaultVfs::holdWrites(false);
        cvars.close();
        waitForHeldFiles(pools);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: an account closed while a coroutine's flushNow waits is let go after it",
                     "[alcvarfile][slow]")
    {
        const size_t        pools = LL::ThreadPoolBase::instanceCount();
        ll_test::ALDbFaults faults;
        ALCVarFileOptions   held;
        held.vfs                  = ll_test::ALDbFaultVfs::name();
        const std::string global  = dir.file("settings.db");
        const std::string account = dir.file("account.db");
        ALCVars&          cvars   = store();
        REQUIRE(cvars.openGlobal(global, held));
        REQUIRE(cvars.openAccount(account));
        cvars.confirmAccount();
        cvars.flushNow();

        // The coroutine waits on the global file, held, while this one
        // closes the account.
        ll_test::ALDbFaultVfs::holdWrites(true);
        CHECK(render::draw_distance.set(300.f));
        CHECK(chat::font_size.set(2));
        bool flushed = false;
        LLCoros::instance().launch("alcvarfile_test flushNow", [&cvars, &flushed] {
            cvars.flushNow();
            flushed = true;
        });
        llcoro::suspend();
        CHECK_FALSE(flushed);
        cvars.closeAccount();
        ll_test::ALDbFaultVfs::holdWrites(false);
        CHECK(ll_test::waitUntil(10s, [&flushed] {
            llcoro::suspend();
            return flushed;
        }));
        CHECK(rowOf(account, "chat.font_size")->value == "2");
        cvars.close();
        CHECK(rowOf(global, "render.draw_distance")->value == "300.0");
        waitForHeldFiles(pools);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: an llsd setting's value reaches the writer as its text",
                     "[alcvarfile][slow]")
    {
        const size_t        pools = LL::ThreadPoolBase::instanceCount();
        ll_test::ALDbFaults faults;
        ALCVarFileOptions   held;
        held.vfs                = ll_test::ALDbFaultVfs::name();
        const std::string path  = dir.file("settings.db");
        ALCVars&          cvars = store();
        REQUIRE(cvars.openGlobal(path, held));
        cvars.flushNow();

        // The writer stuck on the disk with both flushes: what it holds of
        // the map is all that is left of it.
        ll_test::ALDbFaultVfs::holdWrites(true);
        const U32 before = llsd::outstandingCount();
        {
            LLSD layout     = LLSD::emptyMap();
            layout["pane"]  = 3;
            layout["split"] = LLSD::emptyArray();
            layout["split"].append(0.5);
            layout["split"].append(0.5);
            CHECK(ui::layout.set(layout));
        }
        cvars.flushNow();
        ui::layout.reset();
        cvars.flushNow();
        if (llsd::COUNTS_IMPLS)
        {
            CHECK(llsd::outstandingCount() == before);
        }

        ll_test::ALDbFaultVfs::holdWrites(false);
        cvars.flushNow();
        CHECK(rowOf(path, "ui.layout")->tombstone == 1);
        CHECK(count(path, "SELECT count(*) FROM history WHERE name = 'ui.layout' AND old IS NULL AND "
                          "new LIKE '%\"pane\":3%'") == 1);
        cvars.close();
        waitForHeldFiles(pools);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: an account that fails to open closes the one open before it",
                     "[alcvarfile]")
    {
        const std::string first   = emptyFile(dir, "first.db");
        const std::string refused = otherKindOfFile(dir, "refused.db");
        putRow(first, "chat.font_size", S64(ALCVarType::S32), "", chat::font_size.desc().decl(), "3", 1000);
        ALCVars& cvars = store();
        REQUIRE(cvars.openAccount(first));
        cvars.confirmAccount();
        CHECK(chat::font_size() == 3);

        // The relog skipped closeAccount; the next account's file is refused.
        CHECK_FALSE(cvars.openAccount(refused));
        CHECK_FALSE(cvars.accountOpen());
        CHECK(chat::font_size() == 1);
        CHECK(chat::font_size.set(2));
        cvars.confirmAccount();
        cvars.flushNow();
        cvars.close();
        CHECK(rowOf(first, "chat.font_size")->value == "3");
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a reset is a tombstone and holds and runtime values write nothing",
                     "[alcvarfile]")
    {
        const std::string    path  = dir.file("settings.db");
        ALCVars&             cvars = store();
        REQUIRE(cvars.openGlobal(path));
        CHECK(render::draw_distance.set(300.f));
        cvars.flushNow();
        render::draw_distance.reset();
        cvars.flushNow();
        const auto row = rowOf(path, "render.draw_distance");
        REQUIRE(row);
        CHECK(row->tombstone == 1);
        CHECK(row->value == "NULL");

        // The autotuner's holds, a runtime setting, the Session layer.
        const S64 history = count(path, "SELECT count(*) FROM history");
        cvars.hold("autotune", render::draw_distance.index(), ALCVarValue(64.f));
        cvars.hold("autotune", shadows::max_maps.index(), ALCVarValue(U32(2)));
        CHECK(render::frame_count.set(S64(12345)));
        CHECK(cvars.setSession(shadows::detail.index(), ALCVarValue(S32(0))));
        cvars.flushNow();
        cvars.releaseAll("autotune");
        cvars.flushNow();
        CHECK(count(path, "SELECT count(*) FROM settings") == 1);
        CHECK(count(path, "SELECT count(*) FROM history") == history);

        restart();
        REQUIRE(ALCVars::instance().openGlobal(path));
        CHECK(render::draw_distance() == 128.f);
        CHECK(render::frame_count() == 0);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a sensitive setting's history says only that it changed", "[alcvarfile]")
    {
        const std::string    path  = dir.file("settings.db");
        ALCVars&             cvars = store();
        REQUIRE(cvars.openGlobal(path));
        CHECK(cvartest::net::translate::api_key.set("secret"));
        cvars.flushNow();
        CHECK(count(path, "SELECT count(*) FROM history WHERE name = 'net.translate.api_key' AND old IS NULL AND new IS NULL") ==
              1);
        CHECK(rowOf(path, "net.translate.api_key")->value == "secret");
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: an old name's row is taken under the new and kept for older viewers",
                     "[alcvarfile]")
    {
        const std::string    path = emptyFile(dir, "settings.db");
        putRow(path, "RenderFarClip", S64(ALCVarType::F32), "", 0, "300.0", 1000);
        putRow(path, "RenderDisableVintageMode", S64(ALCVarType::Bool), "", 0, "1", 1000);
        putRow(path, "UISndClick", S64(ALCVarType::String), "", 0, "'b4ba225c-373f-446d-9f7e-6cb7b5cf9b3d'", 1000);

        ALCVars& cvars = store();
        REQUIRE(cvars.openGlobal(path));
        CHECK(render::draw_distance() == 300.f);
        CHECK_FALSE(render::vintage::enabled());
        CHECK(ui::sounds::click() == LLUUID("b4ba225c-373f-446d-9f7e-6cb7b5cf9b3d"));
        cvars.flushNow();
        const auto renamed = rowOf(path, "render.draw_distance");
        REQUIRE(renamed);
        CHECK(renamed->value == "300.0");
        CHECK(renamed->modified == 1000);
        // Untouched, for an older viewer.
        CHECK(rowOf(path, "RenderFarClip")->value == "300.0");
        CHECK(rowOf(path, "RenderFarClip")->type == S64(ALCVarType::F32));

        // Reset: the old row does not bring the value back.
        render::draw_distance.reset();
        cvars.close();
        restart();
        REQUIRE(ALCVars::instance().openGlobal(path));
        CHECK(render::draw_distance() == 128.f);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: an old row an older viewer changed later wins over the new name's",
                     "[alcvarfile]")
    {
        const std::string    path = emptyFile(dir, "settings.db");
        putRow(path, "render.draw_distance", S64(ALCVarType::F32), "metres", render::draw_distance.desc().decl(), "200.0",
               1000);
        putRow(path, "RenderFarClip", S64(ALCVarType::F32), "", 0, "400.0", 2000);
        REQUIRE(store().openGlobal(path));
        CHECK(render::draw_distance() == 400.f);

        // ...and not one it changed earlier.
        store().close();
        putRow(path, "RenderFarClip", S64(ALCVarType::F32), "", 0, "500.0", 1500);
        restart();
        REQUIRE(ALCVars::instance().openGlobal(path));
        CHECK(render::draw_distance() == 400.f);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a row of an earlier step is brought forward at open and written back",
                     "[alcvarfile]")
    {
        const std::string path = emptyFile(dir, "settings.db");
        // Seconds in a float then; a percentage then; a choice since renamed.
        const ALCVarDesc& delay = ui::toast::delay.desc();
        const ALCVarDesc& wind  = cvartest::world::wind_scale.desc();
        putRow(path, "ui.toast.delay", S64(ALCVarType::F32), "seconds", delay.steps[0].decl, "1.5", 1000);
        putRow(path, "world.wind_scale", S64(ALCVarType::F32), "", wind.steps[0].decl, "150.0", 1000);
        putRow(path, "ui.toast.quality", S64(ALCVarType::String), "", ui::toast::quality.desc().decl(), "'best'", 1000);
        REQUIRE(store().openGlobal(path));
        CHECK(ui::toast::delay() == 1500);
        CHECK(cvartest::world::wind_scale() == 1.5f);
        CHECK(ui::toast::quality.view() == "high");
        store().flushNow();
        const auto row = rowOf(path, "ui.toast.delay");
        REQUIRE(row);
        CHECK(row->type == S64(ALCVarType::S32));
        CHECK(row->unit == "milliseconds");
        CHECK(row->value == "1500");
        CHECK(U64(row->decl) == delay.decl());
        CHECK(row->modified == 1000);
        CHECK(rowOf(path, "world.wind_scale")->value == "1.5");
        CHECK(U64(rowOf(path, "world.wind_scale")->decl) == wind.decl());
        // At its step already, and fitted only as it is read.
        CHECK(rowOf(path, "ui.toast.quality")->value == "best");

        // Brought forward once: the next open takes each row as it is now.
        store().close();
        restart();
        REQUIRE(ALCVars::instance().openGlobal(path));
        CHECK(ui::toast::delay() == 1500);
        CHECK(cvartest::world::wind_scale() == 1.5f);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a row of a step this build does not know is read and left as it was written",
                     "[alcvarfile]")
    {
        // A newer viewer's, or a build of another branch's: by its own type
        // and unit, through no migrate.
        const std::string path = emptyFile(dir, "settings.db");
        putRow(path, "world.wind_scale", S64(ALCVarType::F32), "", 0x777, "1.5", 1000);
        putRow(path, "ui.toast.delay", S64(ALCVarType::F32), "seconds", 0x778, "2.5", 1000);
        REQUIRE(store().openGlobal(path));
        CHECK(cvartest::world::wind_scale() == 1.5f);
        CHECK(ui::toast::delay() == 2500);
        store().close();
        const auto wind = rowOf(path, "world.wind_scale");
        REQUIRE(wind);
        CHECK(wind->decl == 0x777);
        CHECK(wind->value == "1.5");
        const auto delay = rowOf(path, "ui.toast.delay");
        REQUIRE(delay);
        CHECK(delay->decl == 0x778);
        CHECK(delay->type == S64(ALCVarType::F32));
        CHECK(delay->unit == "seconds");
        CHECK(delay->value == "2.5");
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a row of no step is brought forward from its setting's first and written back",
                     "[alcvarfile]")
    {
        // As the XML import keeps a name the schema did not declare then --
        // the file's type, no unit, no step -- and a later build declares.
        const std::string path  = emptyFile(dir, "settings.db");
        const ALCVarDesc& wind  = cvartest::world::wind_scale.desc();
        const ALCVarDesc& delay = ui::toast::delay.desc();
        putRow(path, "world.wind_scale", S64(ALCVarType::F32), "", 0, "150.0", 1000);
        putRow(path, "ui.toast.delay", S64(ALCVarType::F32), "", 0, "1.5", 1000);
        REQUIRE(store().openGlobal(path));
        CHECK(cvartest::world::wind_scale() == 1.5f);
        CHECK(ui::toast::delay() == 1500);
        store().flushNow();
        const auto windRow = rowOf(path, "world.wind_scale");
        REQUIRE(windRow);
        CHECK(windRow->value == "1.5");
        CHECK(U64(windRow->decl) == wind.decl());
        CHECK(windRow->modified == 1000);
        const auto delayRow = rowOf(path, "ui.toast.delay");
        REQUIRE(delayRow);
        CHECK(delayRow->type == S64(ALCVarType::S32));
        CHECK(delayRow->unit == "milliseconds");
        CHECK(delayRow->value == "1500");
        CHECK(U64(delayRow->decl) == delay.decl());

        // Once: the next open takes each as it is now.
        store().close();
        restart();
        REQUIRE(ALCVars::instance().openGlobal(path));
        CHECK(cvartest::world::wind_scale() == 1.5f);
        CHECK(ui::toast::delay() == 1500);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a row is brought forward once and written at its setting's last step",
                     "[alcvarfile]")
    {
        const std::string path   = emptyFile(dir, "settings.db");
        const ALCVarDesc& detail = cvartest::world::detail_level.desc();
        const ALCVarDesc& wind   = cvartest::world::wind_scale.desc();
        // Never through world.detail_level's migrate: levels before they
        // doubled.
        putRow(path, "world.detail_level", S64(ALCVarType::S32), "", detail.steps[0].decl, "3", 1000);
        // Through world.wind_scale's already.
        putRow(path, "world.wind_scale", S64(ALCVarType::F32), "", wind.decl(), "1.5", 1000);
        REQUIRE(store().openGlobal(path));
        CHECK(cvartest::world::detail_level() == 6);
        CHECK(cvartest::world::wind_scale() == 1.5f);
        store().flushNow();
        CHECK(rowOf(path, "world.detail_level")->value == "6");
        CHECK(U64(rowOf(path, "world.detail_level")->decl) == detail.decl());

        store().close();
        restart();
        REQUIRE(ALCVars::instance().openGlobal(path));
        CHECK(cvartest::world::detail_level() == 6);
        CHECK(cvartest::world::wind_scale.set(2.f));
        CHECK(render::draw_distance.set(300.f));
        ALCVars::instance().flushNow();
        CHECK(U64(rowOf(path, "world.wind_scale")->decl) == wind.decl());
        CHECK(U64(rowOf(path, "render.draw_distance")->decl) == render::draw_distance.desc().decl());
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a current row fitted as it is read is not written back", "[alcvarfile]")
    {
        // A newer viewer's wider range, and a choice it added: neither is a
        // step, so its rows look current here.
        const std::string path = emptyFile(dir, "settings.db");
        putRow(path, "render.draw_distance", S64(ALCVarType::F32), "metres", render::draw_distance.desc().decl(), "5000.0", 1000);
        putRow(path, "ui.toast.quality", S64(ALCVarType::String), "", ui::toast::quality.desc().decl(), "'ultra'", 1000);
        REQUIRE(store().openGlobal(path));
        CHECK(render::draw_distance() == 1024.f);
        CHECK(ui::toast::quality.view() == "high");
        store().flushNow();
        CHECK(rowOf(path, "render.draw_distance")->value == "5000.0");
        CHECK(rowOf(path, "ui.toast.quality")->value == "ultra");
        CHECK(count(path, "SELECT count(*) FROM history") == 0);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a rewrite goes into history and a migrate's keeps the row it replaced",
                     "[alcvarfile]")
    {
        const std::string path  = emptyFile(dir, "settings.db");
        const ALCVarDesc& wind  = cvartest::world::wind_scale.desc();
        const ALCVarDesc& delay = ui::toast::delay.desc();
        putRow(path, "world.wind_scale", S64(ALCVarType::F32), "", wind.steps[0].decl, "150.0", 1000);
        putRow(path, "ui.toast.delay", S64(ALCVarType::F32), "seconds", delay.steps[0].decl, "1.5", 1000);
        REQUIRE(store().openGlobal(path));
        store().flushNow();
        CHECK(count(path, "SELECT count(*) FROM history WHERE kind = 1") == 2);
        CHECK(count(path, "SELECT count(*) FROM history WHERE kind = 1 AND name = 'world.wind_scale' AND old = 150.0 AND "
                          "new = 1.5") == 1);
        // Through a migrate: the row as it was, for a viewer of its step.
        CHECK(count(path, "SELECT count(*) FROM prior WHERE name = 'world.wind_scale' AND decl = " +
                              std::to_string(S64(wind.steps[0].decl)) + " AND type = " + std::to_string(S64(ALCVarType::F32)) +
                              " AND value = 150.0 AND modified = 1000") == 1);
        // Through the conversions alone: nothing kept.
        CHECK(count(path, "SELECT count(*) FROM prior WHERE name = 'ui.toast.delay'") == 0);

        // Written again: the value the prior row held is not the latest.
        CHECK(cvartest::world::wind_scale.set(2.f));
        store().flushNow();
        CHECK(count(path, "SELECT count(*) FROM prior") == 0);
        CHECK(count(path, "SELECT count(*) FROM history WHERE kind = 0 AND name = 'world.wind_scale'") == 1);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a row of a step this build does not know is read from the prior row it knows",
                     "[alcvarfile]")
    {
        // A newer viewer took world.wind_scale through a migrate of a step
        // this build has never seen, keeping the row from before it.
        const std::string path = emptyFile(dir, "settings.db");
        const ALCVarDesc& wind = cvartest::world::wind_scale.desc();
        putRow(path, "world.wind_scale", S64(ALCVarType::F32), "", 0x777, "9.0", 3000);
        putPrior(path, "world.wind_scale", wind.steps[0].decl, S64(ALCVarType::F32), "150.0", 1000);
        putPrior(path, "world.wind_scale", 0x778, S64(ALCVarType::F32), "7.0", 2000);
        REQUIRE(store().openGlobal(path));
        CHECK(cvartest::world::wind_scale() == 1.5f);
        store().close();
        const auto row = rowOf(path, "world.wind_scale");
        REQUIRE(row);
        CHECK(row->decl == 0x777);
        CHECK(row->value == "9.0");
        CHECK(count(path, "SELECT count(*) FROM prior") == 2);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a hook that throws leaves its row and the open goes on", "[alcvarfile]")
    {
        const std::string path = emptyFile(dir, "settings.db");
        const ALCVarDesc& fog  = cvartest::world::fog_density.desc();
        putRow(path, "world.fog_density", S64(ALCVarType::F32), "", fog.steps[0].decl, "0.25", 1000);
        putRow(path, "render.draw_distance", S64(ALCVarType::F32), "metres", render::draw_distance.desc().decl(), "300.0", 1000);
        store().registerMigration("fog_from_old", [](const ALCVarDesc&, const ALCVarStored*, const ALCVarStoredLookup&)
                                                      -> std::optional<ALCVarValue> { throw std::runtime_error("bad row"); });
        REQUIRE(store().openGlobal(path));
        CHECK(cvartest::world::fog_density() == 0.5f);
        CHECK(render::draw_distance() == 300.f);
        store().close();
        const auto row = rowOf(path, "world.fog_density");
        REQUIRE(row);
        CHECK(U64(row->decl) == fog.steps[0].decl);
        CHECK(row->value == "0.25");
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: two settings each take their value from one source", "[alcvarfile]")
    {
        const std::string path = emptyFile(dir, "settings.db");
        putRow(path, "world.sun_angles", S64(ALCVarType::Vec2), "", 0, "'[90.0,30.0]'", 1000);
        store().registerMigration("azimuth_from_angles", [](const ALCVarDesc&, const ALCVarStored* own, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>(ALCVarValue(own->value.get<LLVector2>().mV[0]));
        });
        store().registerMigration("elevation_from_angles", [](const ALCVarDesc&, const ALCVarStored* own, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>(ALCVarValue(own->value.get<LLVector2>().mV[1]));
        });
        REQUIRE(store().openGlobal(path));
        CHECK(cvartest::world::sun_azimuth() == 90.f);
        CHECK(cvartest::world::sun_elevation() == 30.f);
        store().flushNow();
        CHECK(U64(rowOf(path, "world.sun_azimuth")->decl) == cvartest::world::sun_azimuth.desc().decl());
        CHECK(rowOf(path, "world.sun_elevation")->value == "30.0");
        CHECK(rowOf(path, "world.sun_angles")->value == "[90.0,30.0]");
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: an old name's row of a later step of its chain is migrated once",
                     "[alcvarfile]")
    {
        // ui.toast.duration's seconds doubled at its step 2; renamed since.
        const std::string path     = emptyFile(dir, "settings.db");
        const ALCVarDesc& lifetime = ui::toast::lifetime.desc();
        putRow(path, "ui.toast.duration", S64(ALCVarType::F32), "seconds", lifetime.steps[1].decl, "3.0", 1000);
        REQUIRE(store().openGlobal(path));
        CHECK(ui::toast::lifetime() == 3.f);
        store().flushNow();
        CHECK(rowOf(path, "ui.toast.lifetime")->value == "3.0");
        CHECK(U64(rowOf(path, "ui.toast.lifetime")->decl) == lifetime.decl());
        CHECK(rowOf(path, "ui.toast.duration")->value == "3.0");
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a name declared again with reuse never takes the old rows", "[alcvarfile]")
    {
        const std::string path  = emptyFile(dir, "settings.db");
        const ALCVarDesc& style = ui::toast::style.desc();
        REQUIRE(style.superseded.size() == 1);
        putRow(path, "ui.toast.style", S64(ALCVarType::S32), "", style.superseded[0], "2", 1000);
        REQUIRE(store().openGlobal(path));
        CHECK(ui::toast::style.view() == "plain");
        store().close();
        const auto row = rowOf(path, "ui.toast.style");
        REQUIRE(row);
        CHECK(row->type == S64(ALCVarType::S32));
        CHECK(row->value == "2");
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: what a migration hook is given stays valid through its later lookups",
                     "[alcvarfile]")
    {
        const std::string path = emptyFile(dir, "settings.db");
        putRow(path, "world.sky_tint_red", S64(ALCVarType::F32), "", 0, "0.25", 1000);
        putRow(path, "world.sky_tint_blue", S64(ALCVarType::F32), "", 0, "0.75", 1000);
        for (int i = 0; i < 64; ++i)
        {
            putRow(path, fmt::format("SkyTintOld{}", i), S64(ALCVarType::F32), "", 0, "0.5", 1000);
        }
        bool stable = false;
        store().registerMigration(
            "sky_tint_from_two",
            [&stable](const ALCVarDesc&, const ALCVarStored*, const ALCVarStoredLookup& lookup)
                -> std::optional<ALCVarValue> {
                const ALCVarStored* red = lookup("world.sky_tint_red");
                // Names made for the call, many enough to grow what the
                // lookup keeps.
                const ALCVarStored* old = lookup(fmt::format("SkyTintOld{}", 0));
                for (int i = 1; i < 64; ++i)
                {
                    lookup(fmt::format("SkyTintOld{}", i));
                }
                const ALCVarStored* blue = lookup("world.sky_tint_blue");
                stable = red && old && blue && lookup("world.sky_tint_red") == red && lookup("SkyTintOld0") == old;
                if (!stable)
                {
                    return std::nullopt;
                }
                return ALCVarValue(LLColor3(red->value.get<F32>(), old->value.get<F32>(), blue->value.get<F32>()));
            });
        REQUIRE(store().openGlobal(path));
        CHECK(stable);
        CHECK(cvartest::world::sky_tint() == LLColor3(0.25f, 0.5f, 0.75f));
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: rows this build does not know or cannot convert are left as they were",
                     "[alcvarfile]")
    {
        const std::string    path = emptyFile(dir, "settings.db");
        putRow(path, "render.from_the_future", S64(ALCVarType::F32), "", 99, "1.0", 1000);
        putRow(path, "render.shadows.detail", 77, "", 99, "'a type from the future'", 1000);
        putRow(path, "render.shadows.max_maps", S64(ALCVarType::Vec3), "", 0, "'[1,2,3]'", 1000);
        putRow(path, "render.shadows.resolution_scale", S64(ALCVarType::F32), "lightyears", 0, "0.5", 1000);
        const std::string before = count(path, "SELECT count(*) FROM settings") == 4 ? "ok" : "bad";
        REQUIRE(before == "ok");

        REQUIRE(store().openGlobal(path));
        CHECK(shadows::detail() == 2);
        CHECK(shadows::max_maps() == 4u);
        CHECK(shadows::resolution_scale() == 1.f);
        CHECK(shadows::soft.set(false));
        store().close();

        CHECK(rowOf(path, "render.from_the_future")->decl == 99);
        CHECK(rowOf(path, "render.shadows.detail")->type == 77);
        CHECK(rowOf(path, "render.shadows.max_maps")->value == "[1,2,3]");
        CHECK(rowOf(path, "render.shadows.resolution_scale")->unit == "lightyears");
        CHECK(count(path, "SELECT count(*) FROM settings") == 5);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a setting moved into accounts is taken by each once", "[alcvarfile]")
    {
        const std::string    global = emptyFile(dir, "settings.db");
        const std::string    first  = emptyFile(dir, "first.db");
        const std::string    second = emptyFile(dir, "second.db");
        // chat.font_size is an account's now; the global file has it.
        putRow(global, "chat.font_size", S64(ALCVarType::S32), "", chat::font_size.desc().decl(), "3", 1000);

        ALCVars& cvars = store();
        REQUIRE(cvars.openGlobal(global));
        CHECK(chat::font_size() == 1);
        REQUIRE(cvars.openAccount(first));
        cvars.confirmAccount();
        CHECK(chat::font_size() == 3);
        chat::font_size.reset();
        CHECK(chat::font_size() == 1);
        cvars.closeAccount();
        CHECK(raw(first)->queryText("SELECT value FROM meta WHERE key = 'moved:chat.font_size'") == "1");

        // Not again after the reset.
        REQUIRE(cvars.openAccount(first));
        CHECK(chat::font_size() == 1);
        cvars.closeAccount();
        // Another account takes it too.
        REQUIRE(cvars.openAccount(second));
        CHECK(chat::font_size() == 3);
        cvars.closeAccount();
        // The global row stays, for accounts not yet seen.
        CHECK(rowOf(global, "chat.font_size"));
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a setting moved out of accounts takes the first account's value",
                     "[alcvarfile]")
    {
        const std::string    global = emptyFile(dir, "settings.db");
        const std::string    first  = emptyFile(dir, "first.db");
        const std::string    second = emptyFile(dir, "second.db");
        const U64 decl = render::draw_distance.desc().decl();
        putRow(first, "render.draw_distance", S64(ALCVarType::F32), "metres", decl, "222.0", 1000);
        putRow(second, "render.draw_distance", S64(ALCVarType::F32), "metres", decl, "333.0", 1000);

        ALCVars& cvars = store();
        REQUIRE(cvars.openGlobal(global));
        REQUIRE(cvars.openAccount(first));
        CHECK(render::draw_distance() == 222.f);
        cvars.closeAccount();
        REQUIRE(cvars.openAccount(second));
        CHECK(render::draw_distance() == 222.f);
        cvars.closeAccount();
        cvars.flushNow();
        CHECK(rowOf(global, "render.draw_distance")->value == "222.0");
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: writes before login land in the account that logs in", "[alcvarfile]")
    {
        const std::string    first  = emptyFile(dir, "first.db");
        const std::string    failed = emptyFile(dir, "failed.db");
        putRow(first, "chat.show_timestamps", S64(ALCVarType::Bool), "", chat::show_timestamps.desc().decl(), "0", 1000);
        ALCVars& cvars = store();

        // At the login screen: kept in memory.
        CHECK(chat::font_size.set(2));
        CHECK(chat::font_size() == 2);
        // A login that fails drops them, and the file never sees them.
        REQUIRE(cvars.openAccount(failed));
        CHECK(chat::font_size() == 2);
        cvars.closeAccount();
        CHECK(chat::font_size() == 1);
        CHECK_FALSE(rowOf(failed, "chat.font_size"));

        CHECK(chat::font_size.set(3));
        REQUIRE(cvars.openAccount(first));
        CHECK(chat::font_size() == 3);
        CHECK_FALSE(chat::show_timestamps());
        cvars.confirmAccount();
        cvars.flushNow();
        CHECK(rowOf(first, "chat.font_size")->value == "3");
        CHECK(chat::font_size.set(0));
        cvars.closeAccount();
        CHECK(rowOf(first, "chat.font_size")->value == "0");
        CHECK(chat::font_size() == 1);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: an account's override of a global setting", "[alcvarfile]")
    {
        const std::string    global  = dir.file("settings.db");
        const std::string    account = dir.file("account.db");
        ALCVars&             cvars   = store();
        REQUIRE(cvars.openGlobal(global));
        REQUIRE(cvars.openAccount(account));
        cvars.confirmAccount();
        const U32 scale = ui::scale.index();

        // set() writes the global value while the account has none.
        CHECK(ui::scale.set(1.25f));
        CHECK(cvars.layerOf(scale) == ALCVarLayer::Global);
        // "This account only".
        cvars.moveToAccount(scale);
        CHECK(cvars.layerOf(scale) == ALCVarLayer::Account);
        CHECK(ui::scale.set(2.f));
        CHECK(*cvars.layerValue(scale, ALCVarLayer::Global) == ALCVarValue(1.25f));
        CHECK(*cvars.layerValue(scale, ALCVarLayer::Account) == ALCVarValue(2.f));
        CHECK(ui::scale.isUserSet());
        // reset() drops the account's first.
        ui::scale.reset();
        CHECK(ui::scale() == 1.25f);
        CHECK(ui::scale.isUserSet());
        // "All accounts".
        cvars.moveToAccount(scale);
        CHECK(ui::scale.set(1.75f));
        cvars.moveToGlobal(scale);
        CHECK(ui::scale() == 1.25f);
        cvars.flushNow();
        CHECK(rowOf(global, "ui.scale")->value == "1.25");
        CHECK(rowOf(account, "ui.scale")->tombstone == 1);

        // Logged out, every account sees the global value.
        CHECK(ui::scale.set(1.5f));
        cvars.moveToAccount(scale);
        CHECK(ui::scale.set(2.25f));
        cvars.closeAccount();
        CHECK(ui::scale() == 1.5f);
        REQUIRE(cvars.openAccount(account));
        CHECK(ui::scale() == 2.25f);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: an override whose writes are denied stays where it is",
                     "[alcvarfile]")
    {
        ALCVars& cvars = store();
        REQUIRE(cvars.openGlobal(dir.file("settings.db")));
        REQUIRE(cvars.openAccount(dir.file("account.db")));
        cvars.confirmAccount();
        const U32 scale = ui::scale.index();
        CHECK(ui::scale.set(1.25f));

        cvars.denyWrite("rlva", ALCVarTarget::setting(scale));
        cvars.moveToAccount(scale);
        CHECK(cvars.layerOf(scale) == ALCVarLayer::Global);
        cvars.allowWrite("rlva", ALCVarTarget::setting(scale));
        cvars.moveToAccount(scale);
        CHECK(ui::scale.set(2.f));
        cvars.denyWrite("rlva", ALCVarTarget::setting(scale));
        cvars.moveToGlobal(scale);
        CHECK(cvars.layerOf(scale) == ALCVarLayer::Account);
        CHECK(ui::scale() == 2.f);
    }

    TEST_CASE_METHOD(FileStore, "alcvarfile: a reader never sees a default between two accounts",
                     "[alcvarfile][slow]")
    {
        const std::string    first  = emptyFile(dir, "first.db");
        const std::string    second = emptyFile(dir, "second.db");
        putRow(first, "chat.font_size", S64(ALCVarType::S32), "", chat::font_size.desc().decl(), "3", 1000);
        putRow(first, "chat.show_timestamps", S64(ALCVarType::Bool), "", chat::show_timestamps.desc().decl(), "0", 1000);
        putRow(second, "chat.font_size", S64(ALCVarType::S32), "", chat::font_size.desc().decl(), "2", 1000);

        ALCVars& cvars = store();
        REQUIRE(cvars.openAccount(first));
        std::atomic<bool> done{ false };
        std::atomic<bool> between{ false };
        std::atomic<U64>  reads{ 0 };
        auto              reader = std::async(std::launch::async, [&] {
            while (!done.load(std::memory_order_acquire))
            {
                // One account or the other, whole: (3, false) or (2, true).
                const chat::Values values = chat::read();
                const bool         firsts = values.font_size == 3 && !values.show_timestamps;
                const bool         seconds = values.font_size == 2 && values.show_timestamps;
                if (!firsts && !seconds)
                {
                    between = true;
                }
                const S32 size = chat::font_size();
                if (size != 3 && size != 2)
                {
                    between = true;
                }
                ++reads;
            }
        });
        for (int i = 0; i < 40; ++i)
        {
            REQUIRE(cvars.openAccount(i % 2 ? first : second));
        }
        done = true;
        reader.get();
        cvars.closeAccount();
        CHECK(reads.load() > 0);
        CHECK_FALSE(between.load());
    }

    // The process the next case kills: writes, waits past the longest
    // flush delay, writes again, says so, and waits to be killed.
    AL_TEST_HELPER(alcvar_killed_writer)
    {
        const std::string path   = LLStringUtil::getenv(KILLED_PATH);
        const std::string marker = LLStringUtil::getenv(KILLED_MARKER);
        if (path.empty() || marker.empty())
        {
            return 2;
        }
        ALCVars& cvars = ALCVars::instance();
        cvars.registerSchema(cvartest::schema());
        if (!cvars.openGlobal(path))
        {
            return 3;
        }
        render::draw_distance.set(300.f);
        shadows::detail.set(1);
        ms_sleep(U32((ALCVars::LONGEST + 500ms).count()));
        render::draw_distance.set(500.f);
        const std::filesystem::path written = fsyspath(marker);
        std::ofstream(written) << "written";
        ms_sleep(60 * 1000);
        return 4;
    }

    TEST_CASE("alcvarfile: a killed process keeps everything older than the flush delay", "[alcvarfile][slow]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path   = dir.file("settings.db");
        const std::string    marker = dir.file("written");

        LLProcessPtr child;
        {
            ScopedEnvironment pathVar(KILLED_PATH, path);
            ScopedEnvironment markerVar(KILLED_MARKER, marker);
            LLProcess::Params params;
            params.executable = ll_test::currentExecutable();
            params.args.add(ll_test::Helper::argument("alcvar_killed_writer"));
            params.desc = "alcvar killed writer";
            child       = LLProcess::create(params);
        }
        REQUIRE(child);
        REQUIRE(ll_test::waitUntil(30s, [&] {
            LLEventPumps::instance().obtain("mainloop").post(LLSD());
            std::error_code ec;
            return std::filesystem::exists(fsyspath(marker), ec);
        }));
        // Killed as Task Manager kills: nothing runs after.
        child->kill("the test");
        REQUIRE(ll_test::waitUntil(30s, [&] {
            LLEventPumps::instance().obtain("mainloop").post(LLSD());
            return !child->isRunning();
        }));

        ALCVars::instance().registerSchema(cvartest::schema());
        const bool opened = ALCVars::instance().openGlobal(path);
        CHECK(opened);
        CHECK(render::draw_distance() == 300.f);
        CHECK(shadows::detail() == 1);
        ALCVars::deleteSingleton();
        ALCVars::resetSlots(cvartest::schema());
    }
}
