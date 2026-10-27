/**
 * @file aldbconnection.cpp
 * @brief One SQLite connection, its prepared statements, and the savepoints and
 *        transactions work runs inside.
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

#include "lluuid.h"

#include <sqlite3.h>

#include <cstring>

namespace
{
    // What a file of someone else's may ask of SQLite: generous for any
    // honest backup, small next to what a hostile one could otherwise make
    // the reader allocate or recurse through.
    void limitForeign(sqlite3* db)
    {
        sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 32 * 1024 * 1024);
        sqlite3_limit(db, SQLITE_LIMIT_SQL_LENGTH, 1024 * 1024);
        sqlite3_limit(db, SQLITE_LIMIT_COLUMN, 256);
        sqlite3_limit(db, SQLITE_LIMIT_EXPR_DEPTH, 100);
        sqlite3_limit(db, SQLITE_LIMIT_COMPOUND_SELECT, 50);
        sqlite3_limit(db, SQLITE_LIMIT_FUNCTION_ARG, 32);
        sqlite3_limit(db, SQLITE_LIMIT_ATTACHED, 0);
        sqlite3_limit(db, SQLITE_LIMIT_LIKE_PATTERN_LENGTH, 1000);
        sqlite3_limit(db, SQLITE_LIMIT_VARIABLE_NUMBER, 1000);
        sqlite3_limit(db, SQLITE_LIMIT_TRIGGER_DEPTH, 1);
    }

    bool isFailure(int rc)
    {
        return rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE;
    }

    // Whether what follows a prepared statement holds no other.
    bool nothingAfter(std::string_view rest)
    {
        return rest.find_first_not_of(" \t\r\n\f\v;") == std::string_view::npos;
    }
}

//
// ALDbStatement
//

ALDbStatement::ALDbStatement(ALDbConnection* connection, sqlite3_stmt* statement, bool owned)
:   mConnection(connection),
    mStatement(statement),
    mOwned(owned)
{
}

ALDbStatement::~ALDbStatement()
{
    release();
}

ALDbStatement::ALDbStatement(ALDbStatement&& other) noexcept
:   mConnection(other.mConnection),
    mStatement(other.mStatement),
    mOwned(other.mOwned)
{
    other.mConnection = nullptr;
    other.mStatement  = nullptr;
    other.mOwned      = false;
}

ALDbStatement& ALDbStatement::operator=(ALDbStatement&& other) noexcept
{
    if (this != &other)
    {
        release();
        mConnection       = other.mConnection;
        mStatement        = other.mStatement;
        mOwned            = other.mOwned;
        other.mConnection = nullptr;
        other.mStatement  = nullptr;
        other.mOwned      = false;
    }
    return *this;
}

void ALDbStatement::release()
{
    if (mStatement && mConnection)
    {
        mConnection->giveBack(mStatement, mOwned);
    }
    mConnection = nullptr;
    mStatement  = nullptr;
    mOwned      = false;
}

ALDbStatement& ALDbStatement::noteBind(int rc, int index)
{
    if (rc != SQLITE_OK && mConnection)
    {
        mConnection->note(rc, "bind " + std::to_string(index));
    }
    return *this;
}

ALDbStatement& ALDbStatement::bind(int index, S32 value)
{
    return mStatement ? noteBind(sqlite3_bind_int(mStatement, index, value), index) : *this;
}

ALDbStatement& ALDbStatement::bind(int index, U32 value)
{
    return mStatement ? noteBind(sqlite3_bind_int64(mStatement, index, sqlite3_int64(value)), index) : *this;
}

ALDbStatement& ALDbStatement::bind(int index, S64 value)
{
    return mStatement ? noteBind(sqlite3_bind_int64(mStatement, index, value), index) : *this;
}

ALDbStatement& ALDbStatement::bind(int index, F64 value)
{
    return mStatement ? noteBind(sqlite3_bind_double(mStatement, index, value), index) : *this;
}

ALDbStatement& ALDbStatement::bind(int index, std::string_view text)
{
    if (!mStatement)
    {
        return *this;
    }
    return noteBind(sqlite3_bind_text64(mStatement, index, text.data(), text.size(), SQLITE_TRANSIENT, SQLITE_UTF8),
                    index);
}

ALDbStatement& ALDbStatement::bind(int index, std::span<const std::byte> blob)
{
    if (!mStatement)
    {
        return *this;
    }
    // A zero-length blob is still a blob, not a NULL: SQLite needs a
    // non-null pointer to tell them apart.
    static const std::byte empty{};
    const void* data = blob.empty() ? &empty : blob.data();
    return noteBind(sqlite3_bind_blob64(mStatement, index, data, blob.size(), SQLITE_TRANSIENT), index);
}

ALDbStatement& ALDbStatement::bind(int index, const LLUUID& id)
{
    return bind(index, std::span<const std::byte>(reinterpret_cast<const std::byte*>(id.mData), UUID_BYTES));
}

ALDbStatement& ALDbStatement::bindNull(int index)
{
    return mStatement ? noteBind(sqlite3_bind_null(mStatement, index), index) : *this;
}

bool ALDbStatement::step()
{
    if (!mStatement)
    {
        return false;
    }
    const int rc = sqlite3_step(mStatement);
    if (rc == SQLITE_ROW)
    {
        return true;
    }
    if (rc != SQLITE_DONE && mConnection)
    {
        mConnection->note(rc, sqlite3_sql(mStatement));
    }
    return false;
}

int ALDbStatement::run()
{
    if (!mStatement)
    {
        return SQLITE_MISUSE;
    }
    int rc = SQLITE_ROW;
    while (rc == SQLITE_ROW)
    {
        rc = sqlite3_step(mStatement);
    }
    if (rc == SQLITE_DONE)
    {
        return SQLITE_OK;
    }
    if (mConnection)
    {
        mConnection->note(rc, sqlite3_sql(mStatement));
    }
    return rc;
}

void ALDbStatement::reset()
{
    if (mStatement)
    {
        sqlite3_reset(mStatement);
    }
}

int ALDbStatement::columnCount() const
{
    return mStatement ? sqlite3_column_count(mStatement) : 0;
}

int ALDbStatement::columnType(int column) const
{
    return mStatement ? sqlite3_column_type(mStatement, column) : SQLITE_NULL;
}

bool ALDbStatement::columnNull(int column) const
{
    return columnType(column) == SQLITE_NULL;
}

S64 ALDbStatement::columnInt(int column) const
{
    return mStatement ? sqlite3_column_int64(mStatement, column) : 0;
}

F64 ALDbStatement::columnReal(int column) const
{
    return mStatement ? sqlite3_column_double(mStatement, column) : 0.0;
}

std::string_view ALDbStatement::columnText(int column) const
{
    if (!mStatement)
    {
        return {};
    }
    const unsigned char* text = sqlite3_column_text(mStatement, column);
    const int            size = sqlite3_column_bytes(mStatement, column);
    return text ? std::string_view(reinterpret_cast<const char*>(text), size_t(size)) : std::string_view();
}

std::span<const std::byte> ALDbStatement::columnBlob(int column) const
{
    if (!mStatement)
    {
        return {};
    }
    const void* data = sqlite3_column_blob(mStatement, column);
    const int   size = sqlite3_column_bytes(mStatement, column);
    return data ? std::span<const std::byte>(static_cast<const std::byte*>(data), size_t(size))
                : std::span<const std::byte>();
}

LLUUID ALDbStatement::columnUUID(int column) const
{
    LLUUID                           id;
    const std::span<const std::byte> blob = columnBlob(column);
    if (blob.size() == UUID_BYTES)
    {
        std::memcpy(id.mData, blob.data(), UUID_BYTES);
    }
    return id;
}

//
// ALDbConnection
//

size_t ALDbConnection::StringHash::operator()(std::string_view text) const noexcept
{
    return std::hash<std::string_view>{}(text);
}

ALDbConnection::ALDbConnection(sqlite3* db, std::string path, Access access)
:   mDb(db),
    mPath(std::move(path)),
    mAccess(access)
{
}

ALDbConnection::~ALDbConnection()
{
    // Every borrower is gone by now: a statement does not outlive the
    // connection it was borrowed from.
    llassert(mBorrowed.empty());
    for (auto& [sql, statement] : mStatements)
    {
        sqlite3_finalize(statement);
    }
    mStatements.clear();
    // v2: a statement something leaked keeps the connection a zombie rather
    // than leaving it open and unusable.
    sqlite3_close_v2(mDb);
}

std::unique_ptr<ALDbConnection> ALDbConnection::open(const std::string& path, Access access, std::string_view vfs,
                                                     int* rc_out, std::string* error)
{
    int flags = SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_EXRESCODE;
    flags |= access == Access::ReadWrite ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) : SQLITE_OPEN_READONLY;
    // A database in memory is named as a URI (the memdb VFS's shared form);
    // a file of someone else's never is, so its name cannot ask for a VFS or
    // a mode of its own.
    if (access != Access::Foreign && path.starts_with("file:"))
    {
        flags |= SQLITE_OPEN_URI;
    }

    const std::string vfs_name(vfs);
    sqlite3*          db = nullptr;
    const int         rc = sqlite3_open_v2(path.c_str(), &db, flags, vfs_name.empty() ? nullptr : vfs_name.c_str());
    if (rc_out)
    {
        *rc_out = rc;
    }
    if (rc != SQLITE_OK)
    {
        if (error)
        {
            *error = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
        }
        sqlite3_close_v2(db);
        return nullptr;
    }
    sqlite3_extended_result_codes(db, 1);

    if (access == Access::Foreign)
    {
        // A file is code as much as data: its schema can hold triggers and
        // views that run as it is read, and functions with side effects in
        // either. None of them runs here.
        sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
        sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
        sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_TRIGGER, 0, nullptr);
        sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_VIEW, 0, nullptr);
        limitForeign(db);
    }

    std::unique_ptr<ALDbConnection> connection(new ALDbConnection(db, path, access));
    if (access == Access::Foreign)
    {
        connection->exec("PRAGMA mmap_size=0");
        connection->exec("PRAGMA query_only=1");
    }
    return connection;
}

std::unique_ptr<ALDbConnection> ALDbConnection::openForeign(const std::string& path, std::string* error)
{
    int                             rc = SQLITE_OK;
    std::unique_ptr<ALDbConnection> connection = open(path, Access::Foreign, {}, &rc, error);
    if (!connection)
    {
        return nullptr;
    }
    // Every page read and checked before any row of it is trusted. A file
    // that is not a database fails here too, at its first read.
    const std::optional<std::string> check = connection->queryText("PRAGMA integrity_check");
    if (!check || *check != "ok")
    {
        if (error)
        {
            *error = check ? *check : connection->firstErrorMessage();
        }
        return nullptr;
    }
    connection->clearFirstError();
    return connection;
}

int ALDbConnection::note(int rc, std::string_view what)
{
    if (isFailure(rc) && mFirstError == SQLITE_OK)
    {
        mFirstError        = rc;
        mFirstErrorMessage = std::string(what);
        if (mDb)
        {
            mFirstErrorMessage += ": ";
            mFirstErrorMessage += sqlite3_errmsg(mDb);
        }
    }
    return rc;
}

void ALDbConnection::fail(int rc, std::string_view what)
{
    if (mFirstError == SQLITE_OK)
    {
        mFirstError        = isFailure(rc) ? rc : SQLITE_ERROR;
        mFirstErrorMessage = std::string(what);
    }
}

void ALDbConnection::clearFirstError()
{
    mFirstError = SQLITE_OK;
    mFirstErrorMessage.clear();
}

int ALDbConnection::exec(std::string_view sql)
{
    auto found = mStatements.find(sql);
    if (found == mStatements.end())
    {
        sqlite3_stmt* statement = nullptr;
        const char*   tail      = nullptr;
        const char*   end       = sql.data() + sql.size();
        if (sqlite3_prepare_v3(mDb, sql.data(), int(sql.size()), SQLITE_PREPARE_PERSISTENT, &statement, &tail) !=
                SQLITE_OK ||
            !statement || !nothingAfter(std::string_view(tail, size_t(end - tail))))
        {
            // Several statements, none, or one that will not prepare: run as
            // a script, which says why it failed.
            sqlite3_finalize(statement);
            const std::string text(sql);
            char*             message = nullptr;
            const int         rc      = sqlite3_exec(mDb, text.c_str(), nullptr, nullptr, &message);
            if (rc != SQLITE_OK && mFirstError == SQLITE_OK)
            {
                mFirstError        = rc;
                mFirstErrorMessage = text + ": " + (message ? message : sqlite3_errstr(rc));
            }
            sqlite3_free(message);
            return rc;
        }
        found = mStatements.emplace(std::string(sql), statement).first;
    }
    return borrow(found->second, sql).run();
}

ALDbStatement ALDbConnection::prepare(std::string_view sql)
{
    if (auto found = mStatements.find(sql); found != mStatements.end())
    {
        return borrow(found->second, sql);
    }

    sqlite3_stmt* statement = nullptr;
    if (note(sqlite3_prepare_v3(mDb, sql.data(), int(sql.size()), SQLITE_PREPARE_PERSISTENT, &statement, nullptr),
             sql) != SQLITE_OK ||
        !statement)
    {
        // Text with no statement in it prepares to null.
        sqlite3_finalize(statement);
        return {};
    }
    mStatements.emplace(std::string(sql), statement);
    return borrow(statement, sql);
}

ALDbStatement ALDbConnection::borrow(sqlite3_stmt* statement, std::string_view sql)
{
    if (mBorrowed.insert(statement).second)
    {
        return ALDbStatement(this, statement, false);
    }
    // Borrowed already: one of its own, for this borrower alone.
    sqlite3_stmt* own = nullptr;
    if (note(sqlite3_prepare_v3(mDb, sql.data(), int(sql.size()), 0, &own, nullptr), sql) != SQLITE_OK)
    {
        return {};
    }
    return ALDbStatement(this, own, true);
}

void ALDbConnection::giveBack(sqlite3_stmt* statement, bool owned)
{
    if (owned)
    {
        sqlite3_finalize(statement);
        return;
    }
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    mBorrowed.erase(statement);
}

std::optional<S64> ALDbConnection::queryInt(std::string_view sql)
{
    ALDbStatement statement = prepare(sql);
    if (!statement.step())
    {
        return std::nullopt;
    }
    return statement.columnInt(0);
}

std::optional<std::string> ALDbConnection::queryText(std::string_view sql)
{
    ALDbStatement statement = prepare(sql);
    if (!statement.step())
    {
        return std::nullopt;
    }
    return std::string(statement.columnText(0));
}

S64 ALDbConnection::lastInsertRowId() const
{
    return sqlite3_last_insert_rowid(mDb);
}

S32 ALDbConnection::changes() const
{
    return sqlite3_changes(mDb);
}

//
// ALDbSavepoint
//

ALDbSavepoint::ALDbSavepoint(ALDbConnection& connection, std::string_view name)
:   mConnection(connection),
    mName(name)
{
    mBegun = mConnection.exec("SAVEPOINT " + mName) == SQLITE_OK;
}

ALDbSavepoint::~ALDbSavepoint()
{
    if (mBegun)
    {
        release();
    }
}

void ALDbSavepoint::rollback()
{
    if (!mBegun)
    {
        return;
    }
    // Run whatever failed before: these two must not be the ones the
    // connection remembers.
    const std::string rollback = "ROLLBACK TO " + mName;
    const std::string release  = "RELEASE " + mName;
    sqlite3_exec(mConnection.handle(), rollback.c_str(), nullptr, nullptr, nullptr);
    sqlite3_exec(mConnection.handle(), release.c_str(), nullptr, nullptr, nullptr);
    mBegun = false;
}

int ALDbSavepoint::release()
{
    if (!mBegun)
    {
        return SQLITE_MISUSE;
    }
    mBegun = false;
    return mConnection.exec("RELEASE " + mName);
}

//
// Result codes
//

bool ALDb::isTransient(int rc)
{
    switch (rc & 0xff)
    {
        case SQLITE_BUSY:
        case SQLITE_LOCKED:
        case SQLITE_FULL:
        case SQLITE_IOERR:
        case SQLITE_CANTOPEN:
        case SQLITE_NOMEM:
        case SQLITE_PROTOCOL:
            return true;
        default:
            return false;
    }
}

bool ALDb::isCorrupt(int rc)
{
    const int primary = rc & 0xff;
    return primary == SQLITE_CORRUPT || primary == SQLITE_NOTADB;
}

std::string_view ALDb::errorName(int rc)
{
    switch (rc & 0xff)
    {
        case SQLITE_OK: return "SQLITE_OK";
        case SQLITE_ERROR: return "SQLITE_ERROR";
        case SQLITE_INTERNAL: return "SQLITE_INTERNAL";
        case SQLITE_PERM: return "SQLITE_PERM";
        case SQLITE_ABORT: return "SQLITE_ABORT";
        case SQLITE_BUSY: return "SQLITE_BUSY";
        case SQLITE_LOCKED: return "SQLITE_LOCKED";
        case SQLITE_NOMEM: return "SQLITE_NOMEM";
        case SQLITE_READONLY: return "SQLITE_READONLY";
        case SQLITE_INTERRUPT: return "SQLITE_INTERRUPT";
        case SQLITE_IOERR: return "SQLITE_IOERR";
        case SQLITE_CORRUPT: return "SQLITE_CORRUPT";
        case SQLITE_NOTFOUND: return "SQLITE_NOTFOUND";
        case SQLITE_FULL: return "SQLITE_FULL";
        case SQLITE_CANTOPEN: return "SQLITE_CANTOPEN";
        case SQLITE_PROTOCOL: return "SQLITE_PROTOCOL";
        case SQLITE_SCHEMA: return "SQLITE_SCHEMA";
        case SQLITE_TOOBIG: return "SQLITE_TOOBIG";
        case SQLITE_CONSTRAINT: return "SQLITE_CONSTRAINT";
        case SQLITE_MISMATCH: return "SQLITE_MISMATCH";
        case SQLITE_MISUSE: return "SQLITE_MISUSE";
        case SQLITE_AUTH: return "SQLITE_AUTH";
        case SQLITE_RANGE: return "SQLITE_RANGE";
        case SQLITE_NOTADB: return "SQLITE_NOTADB";
        default: return "SQLITE_?";
    }
}
