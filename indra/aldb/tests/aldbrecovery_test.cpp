/**
 * @file aldbrecovery_test.cpp
 * @brief Damaged files: moved aside with their WAL and salvaged, rebuilt, or
 *        left alone and the session run in memory.
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
#include "aldbrecovery.h"

#include "fsyspath.h"
#include "llfile.h"

#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <iterator>

using namespace std::chrono_literals;

namespace
{
    // 'ALTR'
    constexpr U32 TEST_APPLICATION_ID = 0x414C5452;
    constexpr int ROWS                = 200;

    ALDbOptions testOptions(ALDbProfile profile = ALDbProfile::Durable)
    {
        ALDbOptions options;
        options.profile       = profile;
        options.applicationId = TEST_APPLICATION_ID;
        options.migrations    = { { 1, [](ALDbConnection& connection) {
                                    connection.exec("CREATE TABLE kv(k TEXT PRIMARY KEY, v);"
                                                    "CREATE INDEX kv_v ON kv(v);");
                                } } };
        options.retryFirst    = 5ms;
        options.retryLongest  = 40ms;
        return options;
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

    bool exists(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::exists(fsyspath(path), ec);
    }

    std::string contents(const std::string& path)
    {
        llifstream in(fsyspath(path), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // A file of ROWS rows, closed and its WAL emptied into it.
    void makeFile(const std::string& path)
    {
        ALDatabase db(path, testOptions());
        REQUIRE(db.open().usable());
        for (int i = 0; i < ROWS; ++i)
        {
            REQUIRE(db.post([i](ALDbConnection& connection) {
                connection.prepare("INSERT INTO kv(k, v) VALUES (?1, ?2)")
                    .bind(1, std::string_view("key" + std::to_string(i)))
                    .bind(2, S64(i))
                    .run();
            }));
        }
        REQUIRE(db.flush());
        REQUIRE(db.close());
    }

    // Overwrites the index's root page with junk: the table's rows are
    // whole, and the file fails its check.
    void damageIndex(const std::string& path)
    {
        S64 page      = 0;
        S64 page_size = 0;
        {
            auto file = raw(path);
            page      = file->queryInt("SELECT rootpage FROM sqlite_schema WHERE name = 'kv_v'").value_or(0);
            page_size = file->queryInt("PRAGMA page_size").value_or(0);
        }
        REQUIRE(page > 0);
        REQUIRE(page_size > 0);
        // Opened for reading too, so that it is written in place, not emptied.
        llofstream file(fsyspath(path), std::ios::in | std::ios::out | std::ios::binary);
        REQUIRE(file);
        file.seekp(std::streamoff((page - 1) * page_size));
        const std::string junk(size_t(page_size), '\xa5');
        file.write(junk.data(), std::streamsize(junk.size()));
        REQUIRE(file);
    }

    std::vector<std::string> quarantined(const ll_test::ALDbTempDir& dir, const std::string& stem)
    {
        std::vector<std::string> found;
        std::error_code          ec;
        for (const auto& entry : std::filesystem::directory_iterator(fsyspath(dir.path()), ec))
        {
            const std::string name = fsyspath(entry.path().filename()).string();
            if (name.rfind(stem + ".corrupt-", 0) == 0)
            {
                found.push_back(name);
            }
        }
        return found;
    }

    TEST_CASE("aldbrecovery: a damaged file is moved aside and what it held salvaged into a new one",
              "[aldbrecovery]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("damaged.db");
        makeFile(path);
        damageIndex(path);

        ALDbOptions options    = testOptions();
        options.checkIntegrity = true;
        ALDatabase            db(path, options);
        const ALDbOpenResult& result = db.open();
        INFO(result.message);
        REQUIRE(result.outcome == ALDbOpenResult::Outcome::Recovered);
        CHECK(exists(result.quarantinedTo));
        CHECK(quarantined(dir, "damaged.db").size() == 1);
        // The salvage keeps the header's version: no migration ran again.
        CHECK(result.versionBefore == 1);
        CHECK_FALSE(result.migrated);

        S64 rows = 0;
        S64 sum  = 0;
        REQUIRE(db.postAndWait([&](ALDbConnection& connection) {
            rows = connection.queryInt("SELECT count(*) FROM kv").value_or(-1);
            sum  = connection.queryInt("SELECT sum(v) FROM kv INDEXED BY kv_v WHERE v >= 0").value_or(-1);
        }));
        CHECK(rows == ROWS);
        CHECK(sum == S64(ROWS) * (ROWS - 1) / 2);
        CHECK(db.close());

        auto file = raw(path);
        CHECK(file->queryText("PRAGMA integrity_check") == "ok");
        CHECK(file->queryInt("PRAGMA application_id") == S64(TEST_APPLICATION_ID));
    }

    TEST_CASE("aldbrecovery: a file that is not a database is moved aside and begun again", "[aldbrecovery]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("garbage.db");
        {
            llofstream out(fsyspath(path), std::ios::binary);
            const std::string garbage(8192, 'x');
            out.write(garbage.data(), std::streamsize(garbage.size()));
        }

        ALDatabase            db(path, testOptions());
        const ALDbOpenResult& result = db.open();
        INFO(result.message);
        REQUIRE(result.outcome == ALDbOpenResult::Outcome::Recovered);
        CHECK(contents(result.quarantinedTo) == std::string(8192, 'x'));
        CHECK(result.versionAfter == 1);
        CHECK(db.postAndWait([](ALDbConnection& connection) { connection.exec("INSERT INTO kv VALUES ('a', 1)"); }));
        CHECK(db.close());
        CHECK(raw(path)->queryInt("SELECT count(*) FROM kv") == 1);
    }

    TEST_CASE("aldbrecovery: a damaged Regenerable file is deleted and made again", "[aldbrecovery]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("cache.db");
        makeFile(path);
        damageIndex(path);

        ALDbOptions options    = testOptions(ALDbProfile::Regenerable);
        options.checkIntegrity = true;
        ALDatabase            db(path, options);
        const ALDbOpenResult& result = db.open();
        INFO(result.message);
        REQUIRE(result.outcome == ALDbOpenResult::Outcome::Rebuilt);
        CHECK(result.quarantinedTo.empty());
        CHECK(quarantined(dir, "cache.db").empty());
        S64 rows = -1;
        REQUIRE(db.postAndWait([&](ALDbConnection& c) { rows = c.queryInt("SELECT count(*) FROM kv").value_or(-1); }));
        CHECK(rows == 0);
        CHECK(db.close());
    }

    TEST_CASE("aldbrecovery: a damaged file this process may not move is left alone and the session runs in memory",
              "[aldbrecovery]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("notmine.db");
        makeFile(path);
        damageIndex(path);
        const std::string before = contents(path);

        ALDbOptions options    = testOptions();
        options.checkIntegrity = true;
        options.mayQuarantine  = false;
        ALDatabase            db(path, options);
        const ALDbOpenResult& result = db.open();
        CHECK(result.outcome == ALDbOpenResult::Outcome::Transient);
        CHECK(db.postAndWait([](ALDbConnection& connection) { connection.exec("INSERT INTO kv VALUES ('a', 1)"); }));
        CHECK(db.close());

        CHECK(contents(path) == before);
        CHECK(quarantined(dir, "notmine.db").empty());
    }

#if LL_WINDOWS
    TEST_CASE("aldbrecovery: a damaged file another connection holds open is left alone and the session runs in memory",
              "[aldbrecovery]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("held.db");
        makeFile(path);
        damageIndex(path);

        // Open, and read, so that the handle is held: Windows renames no file
        // another process -- or connection -- has open.
        auto holder = raw(path);
        REQUIRE(holder->queryInt("SELECT count(*) FROM sqlite_schema").value_or(0) > 0);

        ALDbOptions options    = testOptions();
        options.checkIntegrity = true;
        ALDatabase            db(path, options);
        const ALDbOpenResult& result = db.open();
        INFO(result.message);
        CHECK(result.outcome == ALDbOpenResult::Outcome::Transient);
        CHECK(db.close());
        holder.reset();

        CHECK(exists(path));
        CHECK(quarantined(dir, "held.db").empty());
    }
#endif

    TEST_CASE("aldbrecovery: a quarantined file takes its WAL with it and the salvage reads it", "[aldbrecovery]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("withwal.db");
        {
            // Rows left in the WAL alone: no checkpoint as the connection goes.
            auto file = raw(path);
            REQUIRE(file->queryText("PRAGMA journal_mode=WAL") == "wal");
            REQUIRE(file->exec("CREATE TABLE kv(k TEXT PRIMARY KEY, v)") == SQLITE_OK);
            int unused = 0;
            sqlite3_db_config(file->handle(), SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, 1, &unused);
            REQUIRE(file->exec("PRAGMA wal_autocheckpoint=0") == SQLITE_OK);
            REQUIRE(file->exec("INSERT INTO kv VALUES ('only in the wal', 1)") == SQLITE_OK);
        }
        REQUIRE(exists(path + "-wal"));

        std::string                      error;
        const std::optional<std::string> moved = ALDbRecovery::quarantine(path, &error);
        INFO(error);
        REQUIRE(moved);
        CHECK_FALSE(exists(path));
        CHECK_FALSE(exists(path + "-wal"));
        CHECK(exists(*moved));
        CHECK(exists(*moved + "-wal"));

        const std::string salvaged = dir.file("salvaged.db");
        REQUIRE(ALDbRecovery::salvage(*moved, salvaged, &error));
        CHECK(raw(salvaged)->queryInt("SELECT count(*) FROM kv WHERE k = 'only in the wal'") == 1);
    }

    TEST_CASE("aldbrecovery: damage found after an open that did not check is said once", "[aldbrecovery]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("latent.db");
        makeFile(path);
        damageIndex(path);

        std::atomic<int> reported{ 0 };
        std::atomic<int> code{ 0 };
        ALDbOptions      options = testOptions();
        options.onFailure        = [&](const ALDbFailure& failure) {
            ++reported;
            code = failure.code;
        };
        ALDatabase            db(path, options);
        const ALDbOpenResult& result = db.open();
        REQUIRE(result.outcome == ALDbOpenResult::Outcome::Opened);

        // Writing through the index reads its damaged root.
        for (int i = 0; i < 3; ++i)
        {
            CHECK_FALSE(db.postAndWait([i](ALDbConnection& connection) {
                connection.prepare("INSERT INTO kv(k, v) VALUES (?1, ?2)")
                    .bind(1, std::string_view("new" + std::to_string(i)))
                    .bind(2, S64(1000 + i))
                    .run();
            }));
        }
        CHECK(reported == 1);
        CHECK(ALDb::isCorrupt(code));
        CHECK(db.close());
    }

    TEST_CASE("aldbrecovery: a check that finds the file busy waits rather than taking it for damage",
              "[aldbrecovery]")
    {
        ll_test::ALDbTempDir dir;
        ll_test::ALDbFaults  faults;
        const std::string    path = dir.file("busy.db");
        // A rollback journal, as on a network filesystem: each read takes its
        // own lock, where a WAL reader takes one and keeps it.
        ll_test::ALDbFaultVfs::withoutSharedMemory(true);
        ALDbOptions options = testOptions();
        options.vfs         = ll_test::ALDbFaultVfs::name();
        {
            ALDatabase db(path, options);
            REQUIRE(db.open().journalMode == "delete");
            for (int i = 0; i < ROWS; ++i)
            {
                REQUIRE(db.post([i](ALDbConnection& connection) {
                    connection.prepare("INSERT INTO kv(k, v) VALUES (?1, ?2)")
                        .bind(1, std::string_view("key" + std::to_string(i)))
                        .bind(2, S64(i))
                        .run();
                }));
            }
            REQUIRE(db.close());
        }

        // The header's read takes the first lock; the check's next ones -- its
        // schema as it is prepared, its pages as it runs -- find the file
        // busy, as when another viewer is writing it.
        ll_test::ALDbFaultVfs::busySharedLocks(2, 3);
        options.checkIntegrity = true;
        ALDatabase            db(path, options);
        const ALDbOpenResult& result = db.open();
        INFO(result.message);
        CHECK(result.outcome == ALDbOpenResult::Outcome::Opened);
        CHECK(result.quarantinedTo.empty());
        CHECK(quarantined(dir, "busy.db").empty());

        ll_test::ALDbFaultVfs::busySharedLocks(0, 0);
        S64 rows = 0;
        CHECK(db.postAndWait([&rows](ALDbConnection& connection) {
            rows = connection.queryInt("SELECT count(*) FROM kv").value_or(-1);
        }));
        CHECK(rows == ROWS);
        CHECK(db.close());
    }
}
