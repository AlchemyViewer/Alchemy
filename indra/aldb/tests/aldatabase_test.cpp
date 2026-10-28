/**
 * @file aldatabase_test.cpp
 * @brief ALDatabase: opening and the profiles' pragmas, migrations and the
 *        copies kept before them, the writer's batches, failures and retries,
 *        closing, readers, backups, and two processes on one file.
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

#include "aldatabase.h"
#include "aldbfaultvfs.h"

#include "altesthelper.h"
#include "fsyspath.h"
#include "llcond.h"
#include "llevents.h"
#include "llprocess.h"
#include "llstring.h"
#include "lltimer.h"
#include "threadpool.h"

#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <filesystem>
#include <future>
#include <stdexcept>

using namespace std::chrono_literals;

namespace
{
    // 'ALTS': a kind of file of the tests' own.
    constexpr U32 TEST_APPLICATION_ID = 0x414C5453;

    const char* const SECOND_WRITER_PATH = "AL_ALDB_TEST_SECOND_WRITER_PATH";

    ALDbMigration makeTable(S32 version, const std::string& table)
    {
        return ALDbMigration{ version, [table](ALDbConnection& connection) {
                                 connection.exec("CREATE TABLE " + table + "(k TEXT PRIMARY KEY, v)");
                             } };
    }

    ALDbOptions testOptions(ALDbProfile profile = ALDbProfile::Durable)
    {
        ALDbOptions options;
        options.profile       = profile;
        options.applicationId = TEST_APPLICATION_ID;
        options.migrations    = { makeTable(1, "kv") };
        options.busyTimeout   = 20ms;
        options.retryFirst    = 5ms;
        options.retryLongest  = 40ms;
        options.closeWait     = 2000ms;
        return options;
    }

    // An upsert keyed by what it writes, safe to run twice.
    ALDatabase::Job put(std::string key, S64 value)
    {
        return [key = std::move(key), value](ALDbConnection& connection) {
            connection.prepare("INSERT INTO kv(k, v) VALUES (?1, ?2) ON CONFLICT(k) DO UPDATE SET v = excluded.v")
                .bind(1, std::string_view(key))
                .bind(2, value)
                .run();
        };
    }

    std::unique_ptr<ALDbConnection> raw(const std::string& path)
    {
        int         rc = SQLITE_OK;
        std::string error;
        auto        connection = ALDbConnection::open(path, ALDbConnection::Access::ReadWrite, {}, &rc, &error);
        INFO(error);
        REQUIRE(connection);
        return connection;
    }

    S64 rowsIn(const std::string& path, const std::string& table = "kv")
    {
        return raw(path)->queryInt("SELECT count(*) FROM " + table).value_or(-1);
    }

    bool exists(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::exists(fsyspath(path), ec);
    }

    // Holds the writer inside a job until released, so that what is posted
    // meanwhile waits for one batch.
    struct Gate
    {
        LLOneShotCond     mStarted;
        LLOneShotCond     mRelease;
        std::atomic<bool> mRan{ false };

        ALDatabase::Job job()
        {
            return [this](ALDbConnection&) {
                if (!mRan.exchange(true))
                {
                    mStarted.set_all();
                }
                mRelease.wait();
            };
        }
    };

    class ScopedEnvironment
    {
    public:
        ScopedEnvironment(const char* name, const std::string& value)
        :   mName(name)
        {
            const std::string before = LLStringUtil::getenv(name);
            mHad                     = !before.empty();
            mBefore                  = before;
            set(value.c_str());
        }
        ~ScopedEnvironment() { set(mHad ? mBefore.c_str() : nullptr); }

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

        std::string mName;
        std::string mBefore;
        bool        mHad = false;
    };

    TEST_CASE("aldatabase: a new file is made with its tables and the profile's pragmas", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::array<std::pair<ALDbProfile, S64>, 3> profiles = { { { ALDbProfile::Durable, 2 },
                                                                         { ALDbProfile::Journal, 1 },
                                                                         { ALDbProfile::Regenerable, 0 } } };
        for (const auto& [profile, synchronous] : profiles)
        {
            const std::string path = dir.file("profile" + std::to_string(int(profile)) + ".db");
            ALDatabase        db(path, testOptions(profile));
            const ALDbOpenResult& result = db.open();
            INFO(result.message);
            REQUIRE(result.outcome == ALDbOpenResult::Outcome::Created);
            CHECK(result.journalMode == "wal");
            CHECK(result.versionAfter == 1);
            CHECK(db.isOpen());
            CHECK_FALSE(db.transient());

            S64  sync = -1;
            S64  keys = -1;
            std::string mode;
            REQUIRE(db.postAndWait([&](ALDbConnection& connection) {
                sync = connection.queryInt("PRAGMA synchronous").value_or(-1);
                keys = connection.queryInt("PRAGMA foreign_keys").value_or(-1);
                mode = connection.queryText("PRAGMA journal_mode").value_or("");
            }));
            CHECK(sync == synchronous);
            CHECK(keys == 1);
            CHECK(mode == "wal");
            CHECK(db.close());
            // Closing emptied the WAL into the file.
            CHECK_FALSE(exists(path + "-wal"));

            auto file = raw(path);
            CHECK(file->queryInt("PRAGMA application_id") == S64(TEST_APPLICATION_ID));
            CHECK(file->queryInt("PRAGMA user_version") == 1);
            CHECK(file->queryInt("SELECT count(*) FROM kv") == 0);
        }
    }

    TEST_CASE("aldatabase: a file of another kind is refused and left as it was", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("other.db");
        {
            auto file = raw(path);
            REQUIRE(file->exec("PRAGMA application_id=12345; CREATE TABLE theirs(x); INSERT INTO theirs VALUES(1);") ==
                    SQLITE_OK);
        }
        ALDatabase            db(path, testOptions());
        const ALDbOpenResult& result = db.open();
        CHECK(result.outcome == ALDbOpenResult::Outcome::Refused);
        CHECK_FALSE(db.isOpen());
        CHECK_FALSE(db.post(put("a", 1)));
        db.close();

        auto file = raw(path);
        CHECK(file->queryInt("PRAGMA application_id") == 12345);
        CHECK(file->queryInt("SELECT count(*) FROM theirs") == 1);
        CHECK_FALSE(file->queryInt("SELECT count(*) FROM kv").has_value());
    }

    TEST_CASE("aldatabase: migrations run in order and one that fails leaves the file as it was", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("migrate.db");

        std::vector<S32> order;
        ALDbOptions      failing = testOptions();
        failing.migrations       = {
            { 2, [&](ALDbConnection& c) { order.push_back(2); c.exec("CREATE TABLE two(x)"); } },
            { 1, [&](ALDbConnection& c) { order.push_back(1); c.exec("CREATE TABLE kv(k TEXT PRIMARY KEY, v)"); } },
            { 3, [&](ALDbConnection& c) { order.push_back(3); c.exec("CREATE TABLE three(x"); } },
        };
        {
            ALDatabase            db(path, failing);
            const ALDbOpenResult& result = db.open();
            CHECK(result.outcome == ALDbOpenResult::Outcome::Failed);
            CHECK_FALSE(result.message.empty());
            CHECK_FALSE(db.isOpen());
        }
        CHECK(order == std::vector<S32>{ 1, 2, 3 });
        {
            auto file = raw(path);
            CHECK(file->queryInt("PRAGMA user_version") == 0);
            CHECK(file->queryInt("SELECT count(*) FROM sqlite_schema") == 0);
        }

        ALDbOptions good = testOptions();
        good.migrations  = { makeTable(1, "kv"), makeTable(2, "two") };
        {
            ALDatabase db(path, good);
            CHECK(db.open().outcome == ALDbOpenResult::Outcome::Created);
            CHECK(db.openResult().versionAfter == 2);
        }
        CHECK(raw(path)->queryInt("PRAGMA user_version") == 2);
    }

    TEST_CASE("aldatabase: a copy is kept before migrating and only the last few are kept", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("backups.db");

        ALDbOptions options = testOptions();
        options.migrations  = { makeTable(1, "kv") };
        {
            ALDatabase db(path, options);
            REQUIRE(db.open().outcome == ALDbOpenResult::Outcome::Created);
            REQUIRE(db.postAndWait(put("kept", 7)));
        }
        // A new file is not copied: there was nothing to lose.
        CHECK_FALSE(exists(path + ".v0.bak"));

        for (S32 version = 2; version <= 4; ++version)
        {
            options.migrations.push_back(makeTable(version, "t" + std::to_string(version)));
            ALDatabase            db(path, options);
            const ALDbOpenResult& result = db.open();
            INFO(result.message);
            CHECK(result.outcome == ALDbOpenResult::Outcome::Opened);
            CHECK(result.migrated);
            CHECK(result.versionBefore == version - 1);
            CHECK(result.versionAfter == version);
        }
        // Copies of versions 1, 2 and 3 were taken; the last two are kept.
        CHECK_FALSE(exists(path + ".v1.bak"));
        CHECK(exists(path + ".v2.bak"));
        CHECK(exists(path + ".v3.bak"));

        // A copy is whole: version 3, the row in it, no WAL beside it.
        CHECK_FALSE(exists(path + ".v3.bak-wal"));
        std::string error;
        auto        copy = ALDbConnection::openForeign(path + ".v3.bak", &error);
        INFO(error);
        REQUIRE(copy);
        CHECK(copy->queryInt("PRAGMA user_version") == 3);
        CHECK(copy->queryInt("SELECT v FROM kv WHERE k = 'kept'") == 7);
    }

    TEST_CASE("aldatabase: a file from a newer build opens without migrating and stays writable", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("newer.db");

        ALDbOptions newer = testOptions();
        newer.migrations  = { makeTable(1, "kv"), makeTable(2, "two"), makeTable(3, "three") };
        {
            ALDatabase db(path, newer);
            REQUIRE(db.open().usable());
        }

        ALDatabase            db(path, testOptions());
        const ALDbOpenResult& result = db.open();
        CHECK(result.outcome == ALDbOpenResult::Outcome::Opened);
        CHECK_FALSE(result.migrated);
        CHECK(result.versionBefore == 3);
        CHECK(result.versionAfter == 3);
        CHECK(db.postAndWait(put("from the older build", 1)));
        CHECK(db.close());

        auto file = raw(path);
        CHECK(file->queryInt("PRAGMA user_version") == 3);
        CHECK(file->queryInt("SELECT count(*) FROM three") == 0);
        CHECK(file->queryInt("SELECT count(*) FROM kv") == 1);
    }

    TEST_CASE("aldatabase: jobs queued together are written in one transaction", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("batch.db");
        ALDatabase           db(path, testOptions());
        REQUIRE(db.open().usable());

        Gate gate;
        REQUIRE(db.post(gate.job()));
        gate.mStarted.wait();
        const U64 commits_before = db.stats().commits;
        for (int i = 0; i < 1000; ++i)
        {
            REQUIRE(db.post(put("key" + std::to_string(i), i)));
        }
        gate.mRelease.set_all();
        REQUIRE(db.flush());

        // The gate's batch, then the thousand in one, and the flush either
        // with them or after.
        const ALDbStats stats = db.stats();
        CHECK(stats.commits - commits_before >= 2);
        CHECK(stats.commits - commits_before <= 3);
        CHECK(stats.committedItems >= 1001);
        CHECK(stats.droppedItems == 0);
        CHECK(db.close());
        CHECK(rowsIn(path) == 1000);
    }

    TEST_CASE("aldatabase: a job that fails for good is dropped alone and the rest commit", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("drop.db");
        ALDatabase           db(path, testOptions());
        REQUIRE(db.open().usable());

        Gate gate;
        REQUIRE(db.post(gate.job()));
        gate.mStarted.wait();
        REQUIRE(db.post(put("first", 1)));
        // A constraint: the same primary key inserted twice in one job.
        REQUIRE(db.post([](ALDbConnection& connection) {
            connection.exec("INSERT INTO kv(k, v) VALUES ('dup', 1)");
            connection.exec("INSERT INTO kv(k, v) VALUES ('dup', 2)");
        }));
        REQUIRE(db.post([](ALDbConnection&) { throw std::runtime_error("a job that throws"); }));
        REQUIRE(db.post(put("last", 3)));
        gate.mRelease.set_all();
        REQUIRE(db.flush());

        CHECK(db.stats().droppedItems == 2);
        CHECK_FALSE(db.postAndWait([](ALDbConnection& connection) { connection.fail(SQLITE_CONSTRAINT, "no"); }));
        CHECK(db.close());

        auto file = raw(path);
        CHECK(file->queryInt("SELECT count(*) FROM kv") == 2);
        CHECK(file->queryInt("SELECT count(*) FROM kv WHERE k = 'dup'") == 0);
    }

    TEST_CASE("aldatabase: a file another connection holds locked is tried again until the lock goes",
              "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("busy.db");
        ALDatabase           db(path, testOptions());
        REQUIRE(db.open().usable());

        auto other = raw(path);
        sqlite3_busy_timeout(other->handle(), 1000);
        REQUIRE(other->exec("BEGIN IMMEDIATE") == SQLITE_OK);

        auto              done   = std::async(std::launch::async, [&db] { return db.postAndWait(put("waited", 1)); });
        // Long enough for several of the writer's tries to find it locked.
        ms_sleep(150);
        CHECK(done.wait_for(0ms) == std::future_status::timeout);
        REQUIRE(other->exec("COMMIT") == SQLITE_OK);

        CHECK(done.get());
        CHECK(db.stats().retries > 0);
        CHECK(db.close());
        other.reset();
        CHECK(rowsIn(path) == 1);
    }

    TEST_CASE("aldatabase: a full disk keeps what was posted and says so once", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        ll_test::ALDbFaults  faults;
        const std::string    path = dir.file("full.db");

        std::atomic<int> reported{ 0 };
        std::atomic<int> code{ 0 };
        ALDbOptions      options = testOptions();
        options.vfs              = ll_test::ALDbFaultVfs::name();
        options.onFailure        = [&](const ALDbFailure& failure) {
            ++reported;
            code = failure.code;
        };
        ALDatabase db(path, options);
        REQUIRE(db.open().usable());

        ll_test::ALDbFaultVfs::failWrites(SQLITE_FULL);
        auto done = std::async(std::launch::async, [&db] { return db.postAndWait(put("survives", 42)); });
        REQUIRE(ll_test::waitUntil(5s, [&db] { return db.stats().retries >= 5; }));
        CHECK(done.wait_for(0ms) == std::future_status::timeout);
        CHECK(reported == 1);
        CHECK((code & 0xff) == SQLITE_FULL);

        ll_test::ALDbFaultVfs::failWrites(SQLITE_OK);
        CHECK(done.get());
        CHECK(reported == 1);
        CHECK(db.close());
        CHECK(rowsIn(path) == 1);
    }

    TEST_CASE("aldatabase: close writes everything posted before it", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("close.db");
        {
            ALDatabase db(path, testOptions());
            REQUIRE(db.open().usable());
            for (int i = 0; i < 100; ++i)
            {
                REQUIRE(db.post(put("key" + std::to_string(i), i)));
            }
            CHECK(db.close());
            CHECK_FALSE(db.post(put("after", 1)));
            CHECK_FALSE(db.isOpen());
        }
        CHECK(rowsIn(path) == 100);
    }

    TEST_CASE("aldatabase: close gives up on a writer stuck on a disk that hangs", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        ll_test::ALDbFaults  faults;
        const std::string    path = dir.file("hang.db");
        {
            ALDbOptions options = testOptions();
            options.vfs         = ll_test::ALDbFaultVfs::name();
            options.closeWait   = 200ms;
            ALDatabase db(path, options);
            REQUIRE(db.open().usable());

            ll_test::ALDbFaultVfs::holdWrites(true);
            REQUIRE(db.post(put("held", 1)));
            REQUIRE(ll_test::waitUntil(5s, [] { return ll_test::ALDbFaultVfs::heldWrites() > 0; }));

            LLTimer timer;
            CHECK_FALSE(db.close());
            CHECK(timer.getElapsedTimeF32() < 3.f);
        }
        // Let the writer left behind finish, and wait for it to close the file
        // and for its thread to take its pool with it.
        ll_test::ALDbFaultVfs::holdWrites(false);
        CHECK(ll_test::waitUntil(10s, [] { return ll_test::ALDbFaultVfs::openFiles() == 0; }));
        CHECK(ll_test::waitUntil(10s, [] {
            for (const std::string& name : LL::ThreadPoolBase::key_snapshot())
            {
                if (name.starts_with("ALDb:hang.db"))
                {
                    return false;
                }
            }
            return true;
        }));

        // The file is whole, whether the held write made it or not.
        std::string error;
        auto        file = ALDbConnection::openForeign(path, &error);
        INFO(error);
        CHECK(file);
    }

    TEST_CASE("aldatabase: without shared memory the file keeps a rollback journal", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        ll_test::ALDbFaults  faults;
        const std::string    path = dir.file("noshm.db");
        ll_test::ALDbFaultVfs::withoutSharedMemory(true);

        ALDbOptions options = testOptions();
        options.vfs         = ll_test::ALDbFaultVfs::name();
        ALDatabase            db(path, options);
        const ALDbOpenResult& result = db.open();
        INFO(result.message);
        REQUIRE(result.usable());
        CHECK(result.journalMode == "delete");
        CHECK(db.postAndWait(put("written", 1)));
        CHECK(db.close());
        CHECK_FALSE(exists(path + "-wal"));
        CHECK(rowsIn(path) == 1);
    }

    TEST_CASE("aldatabase: readers see what the writer committed and are kept for the next", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("readers.db");
        ALDatabase           db(path, testOptions());
        REQUIRE(db.open().usable());
        REQUIRE(db.postAndWait(put("a", 1)));

        ALDbConnection* first = nullptr;
        {
            ALDbReadLease lease = db.read();
            REQUIRE(lease);
            first = &*lease;
            CHECK(lease->queryInt("SELECT v FROM kv WHERE k = 'a'") == 1);
            CHECK(lease->exec("INSERT INTO kv(k, v) VALUES ('b', 2)") != SQLITE_OK);

            // Two at once are two connections.
            ALDbReadLease second = db.read();
            REQUIRE(second);
            CHECK(&*second != first);
        }
        ALDbReadLease again = db.read();
        REQUIRE(again);
        CHECK(again->firstError() == SQLITE_OK);

        REQUIRE(db.postAndWait(put("a", 5)));
        CHECK(again->queryInt("SELECT v FROM kv WHERE k = 'a'") == 5);
        again = ALDbReadLease();
        CHECK(db.close());
        CHECK_FALSE(db.read());
    }

    TEST_CASE("aldatabase: a backup is whole and needs nothing beside it", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path   = dir.file("live.db");
        const std::string    backup = dir.file("live.albackup");
        ALDatabase           db(path, testOptions());
        REQUIRE(db.open().usable());
        for (int i = 0; i < 50; ++i)
        {
            REQUIRE(db.post(put("key" + std::to_string(i), i)));
        }
        REQUIRE(db.flush());

        std::string error;
        CHECK(db.backupTo(backup, &error));
        INFO(error);
        CHECK_FALSE(exists(backup + ".tmp"));
        CHECK_FALSE(exists(backup + "-wal"));
        // A second backup replaces the first.
        REQUIRE(db.postAndWait(put("later", 1)));
        CHECK(db.backupTo(backup, &error));
        CHECK(db.close());

        auto copy = ALDbConnection::openForeign(backup, &error);
        INFO(error);
        REQUIRE(copy);
        CHECK(copy->queryText("PRAGMA journal_mode") == "delete");
        CHECK(copy->queryInt("SELECT count(*) FROM kv") == 51);
        CHECK(copy->queryInt("PRAGMA application_id") == S64(TEST_APPLICATION_ID));
    }

    TEST_CASE("aldatabase: a file that cannot be opened runs in memory for the session", "[aldatabase]")
    {
        ll_test::ALDbTempDir  dir;
        const std::string     path = dir.file("no/such/directory/x.db");
        ALDatabase            db(path, testOptions());
        const ALDbOpenResult& result = db.open();
        CHECK(result.outcome == ALDbOpenResult::Outcome::Transient);
        CHECK(db.transient());
        CHECK(db.isOpen());

        // It works as a file would, readers included, and keeps nothing.
        REQUIRE(db.postAndWait(put("in memory", 1)));
        ALDbReadLease lease = db.read();
        REQUIRE(lease);
        CHECK(lease->queryInt("SELECT count(*) FROM kv") == 1);
        lease = ALDbReadLease();
        CHECK(db.close());
        CHECK_FALSE(exists(path));
    }

    // The other process of the next case: writes its rows into the file the
    // environment names while the test's own process writes its own.
    AL_TEST_HELPER(aldb_second_writer)
    {
        const std::string path = LLStringUtil::getenv(SECOND_WRITER_PATH);
        if (path.empty())
        {
            return 2;
        }
        ALDatabase db(path, testOptions());
        if (!db.open().usable())
        {
            return 3;
        }
        for (int i = 0; i < 200; ++i)
        {
            db.post(put("child" + std::to_string(i), i));
            if (i % 20 == 0)
            {
                db.flush();
            }
        }
        const bool flushed = db.flush();
        return db.close() && flushed ? 0 : 4;
    }

    TEST_CASE("aldatabase: two processes write one file and both keep what they wrote", "[aldatabase][slow]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("shared.db");
        ALDatabase           db(path, testOptions());
        REQUIRE(db.open().usable());

        LLProcessPtr child;
        {
            ScopedEnvironment environment(SECOND_WRITER_PATH, path);
            LLProcess::Params params;
            params.executable = ll_test::currentExecutable();
            params.args.add(ll_test::Helper::argument("aldb_second_writer"));
            params.desc = "aldb second writer";
            child       = LLProcess::create(params);
        }
        REQUIRE(child);

        for (int i = 0; i < 200; ++i)
        {
            REQUIRE(db.post(put("parent" + std::to_string(i), i)));
            if (i % 20 == 0)
            {
                REQUIRE(db.flush());
            }
        }
        REQUIRE(db.flush());

        const bool exited = ll_test::waitUntil(60s, [&child] {
            LLEventPumps::instance().obtain("mainloop").post(LLSD());
            return !child->isRunning();
        });
        REQUIRE(exited);
        const LLProcess::Status status = child->getStatus();
        CHECK(status.mState == LLProcess::EXITED);
        CHECK(status.mData == 0);

        CHECK(db.close());
        auto file = raw(path);
        CHECK(file->queryInt("SELECT count(*) FROM kv WHERE k LIKE 'parent%'") == 200);
        CHECK(file->queryInt("SELECT count(*) FROM kv WHERE k LIKE 'child%'") == 200);
    }

    TEST_CASE("aldatabase: a job that makes SQLite undo the whole batch has the jobs before it tried again",
              "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        ll_test::ALDbFaults  faults;
        const std::string    path = dir.file("undone.db");
        // A rollback journal: a page's first change in a transaction writes
        // the journal there and then, so a job can fail on a full disk.
        ll_test::ALDbFaultVfs::withoutSharedMemory(true);
        ALDbOptions options = testOptions();
        options.vfs         = ll_test::ALDbFaultVfs::name();
        options.migrations  = { makeTable(1, "kv"), makeTable(2, "other") };
        ALDatabase db(path, options);
        REQUIRE(db.open().usable());
        REQUIRE(db.openResult().journalMode == "delete");

        Gate              gate;
        std::atomic<bool> filled{ false };
        REQUIRE(db.post(gate.job()));
        gate.mStarted.wait();
        // The first job writes, then fills the disk; the second's first
        // change to its table cannot be journalled, and a full disk in a
        // statement with no journal of its own undoes the whole transaction.
        REQUIRE(db.post([&filled](ALDbConnection& connection) {
            connection.exec("INSERT INTO kv(k, v) VALUES ('first', 1)");
            if (!filled.exchange(true))
            {
                ll_test::ALDbFaultVfs::failWrites(SQLITE_FULL);
            }
        }));
        REQUIRE(db.post([](ALDbConnection& connection) { connection.exec("INSERT INTO other(k, v) VALUES ('second', 2)"); }));
        gate.mRelease.set_all();

        REQUIRE(ll_test::waitUntil(5s, [&db] {
            const ALDbStats stats = db.stats();
            return stats.retries > 0 || stats.droppedItems > 0;
        }));
        ll_test::ALDbFaultVfs::failWrites(SQLITE_OK);
        REQUIRE(db.flush());
        CHECK(db.stats().droppedItems == 0);
        CHECK(db.close());
        CHECK(rowsIn(path, "kv") == 1);
        CHECK(rowsIn(path, "other") == 1);
    }

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

    TEST_CASE("aldatabase: a file this process may only read is read and its writes are refused and said once",
              "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("readonly.db");
        {
            ALDatabase db(path, testOptions());
            REQUIRE(db.open().usable());
            REQUIRE(db.postAndWait(put("kept", 7)));
            REQUIRE(db.close());
        }
        ReadOnlyFile read_only(path);

        std::atomic<int> reported{ 0 };
        std::atomic<int> code{ 0 };
        ALDbOptions      options = testOptions();
        options.onFailure        = [&](const ALDbFailure& failure) {
            ++reported;
            code = failure.code;
        };
        ALDatabase            db(path, options);
        const ALDbOpenResult& result = db.open();
        INFO(result.message);
        REQUIRE(result.usable());
        CHECK(result.readOnly);

        S64 value = 0;
        CHECK(db.postAndWait([&value](ALDbConnection& connection) {
            value = connection.queryInt("SELECT v FROM kv WHERE k = 'kept'").value_or(-1);
        }));
        CHECK(value == 7);

        for (int i = 0; i < 3; ++i)
        {
            CHECK_FALSE(db.postAndWait(put("refused" + std::to_string(i), i)));
        }
        CHECK(reported == 1);
        CHECK((code & 0xff) == SQLITE_READONLY);
        CHECK(db.close());
    }

    TEST_CASE("aldatabase: a file another viewer migrates while this one opens is not migrated twice",
              "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("raced.db");
        // A file with nothing in it yet, as two viewers starting at once
        // would both find.
        REQUIRE(raw(path)->exec("PRAGMA journal_mode=WAL") == SQLITE_OK);

        auto other = raw(path);
        REQUIRE(other->exec("BEGIN IMMEDIATE") == SQLITE_OK);

        ALDbOptions options = testOptions();
        options.busyTimeout = 5000ms;
        ALDatabase db(path, options);
        auto       opening = std::async(std::launch::async, [&db] { db.open(); });
        // The open reads the file's version, then waits for the lock the
        // other viewer holds while it makes the same tables.
        ms_sleep(300);
        REQUIRE(other->exec("CREATE TABLE kv(k TEXT PRIMARY KEY, v)") == SQLITE_OK);
        REQUIRE(other->exec("PRAGMA user_version=1") == SQLITE_OK);
        REQUIRE(other->exec("COMMIT") == SQLITE_OK);

        opening.get();
        const ALDbOpenResult& result = db.openResult();
        INFO(result.message);
        REQUIRE(result.usable());
        CHECK_FALSE(result.migrated);
        CHECK(db.postAndWait(put("after", 1)));
        CHECK(db.close());
        other.reset();
        CHECK(rowsIn(path) == 1);
    }

    TEST_CASE("aldatabase: flush gives up waiting on a disk that hangs", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        ll_test::ALDbFaults  faults;
        const std::string    path = dir.file("slowflush.db");
        ALDbOptions          options = testOptions();
        options.vfs                  = ll_test::ALDbFaultVfs::name();
        options.closeWait            = 300ms;
        ALDatabase db(path, options);
        REQUIRE(db.open().usable());

        ll_test::ALDbFaultVfs::holdWrites(true);
        REQUIRE(db.post(put("held", 1)));
        REQUIRE(ll_test::waitUntil(5s, [] { return ll_test::ALDbFaultVfs::heldWrites() > 0; }));

        auto flushing = std::async(std::launch::async, [&db] {
            LLTimer timer;
            const bool written = db.flush();
            return std::make_pair(written, F32(timer.getElapsedTimeF32()));
        });
        const bool returned = flushing.wait_for(3s) == std::future_status::ready;
        // Lets a flush that waits for ever finish, so the test can.
        ll_test::ALDbFaultVfs::holdWrites(false);
        REQUIRE(returned);
        const auto [written, seconds] = flushing.get();
        CHECK_FALSE(written);
        CHECK(seconds < 2.f);
        CHECK(db.close());
    }

    TEST_CASE("aldatabase: a job posted with a completion hears whether it was written", "[aldatabase]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("completion.db");
        ALDatabase           db(path, testOptions());
        REQUIRE(db.open().usable());

        std::atomic<int> written{ -1 };
        std::atomic<int> refused{ -1 };
        REQUIRE(db.post(put("one", 1), [&written](bool committed) { written = committed ? 1 : 0; }));
        REQUIRE(db.post([](ALDbConnection& connection) { connection.fail(SQLITE_CONSTRAINT, "no"); },
                        [&refused](bool committed) { refused = committed ? 1 : 0; }));
        REQUIRE(db.flush());
        CHECK(written == 1);
        CHECK(refused == 0);
        CHECK(db.close());
    }
}
