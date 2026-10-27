/**
 * @file aldbconnection_test.cpp
 * @brief ALDbConnection, ALDbStatement and ALDbSavepoint: statements cached
 *        and borrowed, every bind and column, the first failure, savepoints, and
 *        a file of someone else's read safely.
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

#include "aldbconnection.h"
#include "aldbfaultvfs.h"

#include "llfile.h"
#include "lluuid.h"

#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

namespace
{
    std::unique_ptr<ALDbConnection> openFile(const std::string& path)
    {
        int         rc = SQLITE_OK;
        std::string error;
        auto        connection = ALDbConnection::open(path, ALDbConnection::Access::ReadWrite, {}, &rc, &error);
        INFO(error);
        REQUIRE(connection);
        return connection;
    }

    TEST_CASE("aldbconnection: a statement is cached by its SQL and a second borrower gets one of its own",
              "[aldbconnection]")
    {
        ll_test::ALDbTempDir dir;
        auto                 db = openFile(dir.file("cache.db"));
        REQUIRE(db->exec("CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT)") == SQLITE_OK);
        const size_t before = db->cachedStatements();
        for (int i = 0; i < 3; ++i)
        {
            db->prepare("INSERT INTO t(v) VALUES (?1)").bind(1, std::string_view("row")).run();
        }
        CHECK(db->cachedStatements() == before + 1);

        // A query run inside the loop over another's rows, of the same SQL:
        // the outer one must keep its place.
        ALDbStatement outer = db->prepare("SELECT id FROM t ORDER BY id");
        int           rows  = 0;
        while (outer.step())
        {
            ALDbStatement inner = db->prepare("SELECT id FROM t ORDER BY id");
            REQUIRE(inner.step());
            CHECK(inner.columnInt(0) == 1);
            ++rows;
        }
        CHECK(rows == 3);
        CHECK(db->cachedStatements() == before + 2);
        CHECK(db->firstError() == SQLITE_OK);
    }

    TEST_CASE("aldbconnection: every bind comes back as the column it was written as", "[aldbconnection]")
    {
        ll_test::ALDbTempDir dir;
        auto                 db = openFile(dir.file("types.db"));
        REQUIRE(db->exec("CREATE TABLE t(i, u, l, r, s, b, e, id, n)") == SQLITE_OK);

        const std::array<std::byte, 3> blob = { std::byte{ 0 }, std::byte{ 0xff }, std::byte{ 7 } };
        LLUUID                         id;
        id.generate();
        db->prepare("INSERT INTO t VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)")
            .bind(1, S32(-7))
            .bind(2, U32(0xffffffffu))
            .bind(3, S64(1) << 40)
            .bind(4, F64(0.25))
            .bind(5, std::string_view("text with\0a nul", 15))
            .bind(6, std::span<const std::byte>(blob))
            .bind(7, std::span<const std::byte>())
            .bind(8, id)
            .bindNull(9)
            .run();
        REQUIRE(db->firstError() == SQLITE_OK);

        ALDbStatement row = db->prepare("SELECT i, u, l, r, s, b, e, id, n FROM t");
        REQUIRE(row.step());
        CHECK(row.columnInt(0) == -7);
        CHECK(row.columnInt(1) == S64(0xffffffffu));
        CHECK(row.columnInt(2) == (S64(1) << 40));
        CHECK(row.columnReal(3) == 0.25);
        CHECK(row.columnText(4) == std::string_view("text with\0a nul", 15));
        const std::span<const std::byte> read = row.columnBlob(5);
        CHECK(std::vector<std::byte>(read.begin(), read.end()) == std::vector<std::byte>(blob.begin(), blob.end()));
        // An empty blob is a blob, not a NULL.
        CHECK(row.columnType(6) == SQLITE_BLOB);
        CHECK(row.columnBlob(6).empty());
        CHECK(row.columnUUID(7) == id);
        CHECK(row.columnNull(8));
        CHECK(row.columnUUID(8).isNull());
    }

    TEST_CASE("aldbconnection: the first failure is remembered until cleared", "[aldbconnection]")
    {
        ll_test::ALDbTempDir dir;
        auto                 db = openFile(dir.file("errors.db"));
        REQUIRE(db->exec("CREATE TABLE t(id INTEGER PRIMARY KEY)") == SQLITE_OK);
        db->prepare("INSERT INTO t VALUES (1)").run();
        CHECK(db->firstError() == SQLITE_OK);

        db->prepare("INSERT INTO t VALUES (1)").run();
        CHECK((db->firstError() & 0xff) == SQLITE_CONSTRAINT);
        const std::string first = db->firstErrorMessage();
        CHECK_FALSE(first.empty());

        // A second failure does not replace the first.
        db->exec("SELECT * FROM no_such_table");
        CHECK((db->firstError() & 0xff) == SQLITE_CONSTRAINT);
        CHECK(db->firstErrorMessage() == first);

        db->clearFirstError();
        CHECK(db->firstError() == SQLITE_OK);
        CHECK(db->firstErrorMessage().empty());

        // Text with no statement in it is nothing, not a failure.
        CHECK_FALSE(db->prepare("  ").valid());
        CHECK(db->firstError() == SQLITE_OK);
    }

    TEST_CASE("aldbconnection: savepoints nest and one rolled back undoes only itself", "[aldbconnection]")
    {
        ll_test::ALDbTempDir dir;
        auto                 db = openFile(dir.file("savepoints.db"));
        REQUIRE(db->exec("CREATE TABLE t(v)") == SQLITE_OK);
        {
            ALDbSavepoint outer(*db, "outer");
            REQUIRE(outer.begun());
            db->exec("INSERT INTO t VALUES (1)");
            {
                ALDbSavepoint inner(*db, "inner");
                db->exec("INSERT INTO t VALUES (2)");
                inner.rollback();
            }
            {
                ALDbSavepoint kept(*db, "kept");
                db->exec("INSERT INTO t VALUES (3)");
            }
        }
        CHECK(db->queryInt("SELECT group_concat(v) = '1,3' FROM t") == 1);
        CHECK(db->firstError() == SQLITE_OK);
    }

    TEST_CASE("aldbconnection: a single statement exec runs is prepared once and a script is not kept",
              "[aldbconnection]")
    {
        ll_test::ALDbTempDir dir;
        auto                 db = openFile(dir.file("exec.db"));
        REQUIRE(db->exec("CREATE TABLE t(v UNIQUE)") == SQLITE_OK);

        // What a writer runs for every transaction and job, one statement
        // each: prepared the first time and kept.
        const size_t before = db->cachedStatements();
        for (int i = 0; i < 100; ++i)
        {
            REQUIRE(db->exec("BEGIN IMMEDIATE") == SQLITE_OK);
            {
                ALDbSavepoint job(*db, "job");
                REQUIRE(job.begun());
                db->prepare("INSERT INTO t VALUES (?1)").bind(1, S32(i)).run();
            }
            REQUIRE(db->exec("COMMIT") == SQLITE_OK);
        }
        // BEGIN IMMEDIATE, SAVEPOINT job, RELEASE job, COMMIT and the insert.
        CHECK(db->cachedStatements() == before + 5);
        CHECK(db->queryInt("SELECT count(*) FROM t") == 100);

        // Text of several statements runs whole, and is not kept.
        const size_t kept = db->cachedStatements();
        CHECK(db->exec("CREATE TABLE a(v); INSERT INTO a VALUES (1); INSERT INTO a VALUES (2);") == SQLITE_OK);
        CHECK(db->cachedStatements() == kept);
        CHECK(db->queryInt("SELECT count(*) FROM a") == 2);

        // A kept statement that fails is recorded with its SQL, and runs
        // again from the start the next time.
        CHECK(db->exec("INSERT INTO t VALUES (0)") != SQLITE_OK);
        CHECK((db->firstError() & 0xff) == SQLITE_CONSTRAINT);
        CHECK(db->firstErrorMessage().find("INSERT INTO t VALUES (0)") != std::string::npos);
        db->clearFirstError();
        REQUIRE(db->exec("DELETE FROM t WHERE v = 0") == SQLITE_OK);
        CHECK(db->exec("INSERT INTO t VALUES (0)") == SQLITE_OK);
        CHECK(db->firstError() == SQLITE_OK);
    }

    TEST_CASE("aldbconnection: a file of someone else's is checked whole and runs no trigger or view",
              "[aldbconnection]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("foreign.db");
        {
            auto db = openFile(path);
            REQUIRE(db->exec("CREATE TABLE t(v);"
                             "CREATE TABLE log(v);"
                             "CREATE TRIGGER t_log AFTER INSERT ON t BEGIN INSERT INTO log VALUES (new.v); END;"
                             "CREATE VIEW t_view AS SELECT v FROM t;"
                             "INSERT INTO t VALUES (1);") == SQLITE_OK);
        }

        std::string error;
        auto        foreign = ALDbConnection::openForeign(path, &error);
        INFO(error);
        REQUIRE(foreign);
        CHECK(foreign->queryInt("SELECT count(*) FROM t") == 1);
        // Views are refused, and nothing can be written to fire a trigger.
        CHECK_FALSE(foreign->queryInt("SELECT count(*) FROM t_view").has_value());
        foreign->clearFirstError();
        CHECK(foreign->exec("INSERT INTO t VALUES (2)") != SQLITE_OK);
        // Nothing written here could fire a trigger anyway, so each defence
        // is read back as well.
        const auto setting = [&](int option) {
            int value = -1;
            sqlite3_db_config(foreign->handle(), option, -1, &value);
            return value;
        };
        CHECK(setting(SQLITE_DBCONFIG_DEFENSIVE) == 1);
        CHECK(setting(SQLITE_DBCONFIG_TRUSTED_SCHEMA) == 0);
        CHECK(setting(SQLITE_DBCONFIG_ENABLE_TRIGGER) == 0);
        CHECK(setting(SQLITE_DBCONFIG_ENABLE_VIEW) == 0);
        foreign.reset();

        auto db = openFile(path);
        CHECK(db->queryInt("SELECT count(*) FROM log") == 1);
    }

    TEST_CASE("aldbconnection: a file of someone else's that is not a database is refused", "[aldbconnection]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("garbage.db");
        {
            llofstream        out(path, std::ios::binary);
            const std::string garbage(8192, 'x');
            out.write(garbage.data(), std::streamsize(garbage.size()));
            REQUIRE(out);
        }
        std::string error;
        CHECK_FALSE(ALDbConnection::openForeign(path, &error));
        CHECK_FALSE(error.empty());
    }

    TEST_CASE("aldbconnection: a file of someone else's cannot hand the reader a value past its limits",
              "[aldbconnection]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("oversized.db");
        {
            auto db = openFile(path);
            REQUIRE(db->exec("CREATE TABLE t(v)") == SQLITE_OK);
            // One byte past the 32 MB a foreign file may hold in a value.
            db->prepare("INSERT INTO t VALUES (zeroblob(?1))").bind(1, S64(32 * 1024 * 1024 + 1)).run();
            REQUIRE(db->firstError() == SQLITE_OK);
        }
        std::string error;
        auto        foreign = ALDbConnection::openForeign(path, &error);
        INFO(error);
        REQUIRE(foreign);
        ALDbStatement read = foreign->prepare("SELECT v FROM t");
        CHECK_FALSE(read.step());
        CHECK((foreign->firstError() & 0xff) == SQLITE_TOOBIG);
    }

    TEST_CASE("aldbconnection: result codes sort into those that may pass and damage", "[aldbconnection]")
    {
        CHECK(ALDb::isTransient(SQLITE_BUSY));
        CHECK(ALDb::isTransient(SQLITE_BUSY_SNAPSHOT));
        CHECK(ALDb::isTransient(SQLITE_FULL));
        CHECK(ALDb::isTransient(SQLITE_IOERR_WRITE));
        CHECK_FALSE(ALDb::isTransient(SQLITE_CONSTRAINT_PRIMARYKEY));
        CHECK_FALSE(ALDb::isTransient(SQLITE_CORRUPT));
        CHECK(ALDb::isCorrupt(SQLITE_CORRUPT_INDEX));
        CHECK(ALDb::isCorrupt(SQLITE_NOTADB));
        CHECK_FALSE(ALDb::isCorrupt(SQLITE_IOERR));
        CHECK(ALDb::errorName(SQLITE_IOERR_FSYNC) == "SQLITE_IOERR");
    }
}
