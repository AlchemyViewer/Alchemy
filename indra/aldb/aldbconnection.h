/**
 * @file aldbconnection.h
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

#pragma once

#include "stdtypes.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;
class LLUUID;
class ALDbConnection;

// A statement of a connection's cache, borrowed: reset and its bindings
// cleared when it goes, so the next borrower of the same SQL starts clean.
// Binds and columns are numbered as SQLite numbers them: binds from 1,
// columns from 0. A failure is recorded on the connection (firstError), so
// a run of calls needs no check after each.
class ALDbStatement
{
public:
    ALDbStatement() = default;
    ~ALDbStatement();
    ALDbStatement(ALDbStatement&& other) noexcept;
    ALDbStatement& operator=(ALDbStatement&& other) noexcept;
    ALDbStatement(const ALDbStatement&)            = delete;
    ALDbStatement& operator=(const ALDbStatement&) = delete;

    // Whether the SQL prepared. A statement that did not does nothing.
    bool valid() const { return mStatement != nullptr; }

    ALDbStatement& bind(int index, S32 value);
    ALDbStatement& bind(int index, U32 value);
    ALDbStatement& bind(int index, S64 value);
    ALDbStatement& bind(int index, F64 value);
    ALDbStatement& bind(int index, bool value) { return bind(index, S32(value ? 1 : 0)); }
    // Copied: the view need not outlive the call.
    ALDbStatement& bind(int index, std::string_view text);
    ALDbStatement& bind(int index, const char* text) { return bind(index, std::string_view(text)); }
    ALDbStatement& bind(int index, std::span<const std::byte> blob);
    // Sixteen bytes, as a BLOB.
    ALDbStatement& bind(int index, const LLUUID& id);
    ALDbStatement& bindNull(int index);

    // Steps once. True with a row to read; false when done or failed.
    bool step();
    // Steps to the end, for a statement that returns no rows. The result
    // code: SQLITE_DONE's success as SQLITE_OK.
    int run();
    // Back to before its first step, its bindings kept.
    void reset();

    int columnCount() const;
    // SQLITE_INTEGER, SQLITE_FLOAT, SQLITE_TEXT, SQLITE_BLOB or SQLITE_NULL.
    int columnType(int column) const;
    bool columnNull(int column) const;
    S64 columnInt(int column) const;
    F64 columnReal(int column) const;
    // Valid until the next step, reset or the statement's end.
    std::string_view columnText(int column) const;
    std::span<const std::byte> columnBlob(int column) const;
    // A null UUID unless the column holds sixteen bytes.
    LLUUID columnUUID(int column) const;

private:
    friend class ALDbConnection;
    ALDbStatement(ALDbConnection* connection, sqlite3_stmt* statement, bool owned);
    void release();
    ALDbStatement& noteBind(int rc, int index);

    ALDbConnection* mConnection = nullptr;
    sqlite3_stmt*   mStatement  = nullptr;
    // Prepared for this borrower alone, because the cached one was already
    // borrowed: finalized, not returned, when it goes.
    bool            mOwned      = false;
};

// One SQLite connection, used by one thread at a time: the writer's, a
// reader's, or a file of someone else's opened to be read once. It keeps the
// statements it has prepared, by their SQL, for as long as it lives.
//
// It remembers the first failure since it was last cleared, with SQLite's
// message for it: a job the writer runs makes its calls one after another
// and the writer looks once, at the end, at whether one failed.
class ALDbConnection
{
public:
    enum class Access
    {
        // Created if absent.
        ReadWrite,
        // Never written; may be shared with another process's writer.
        ReadOnly,
        // A file this install did not write: read-only, defensive, with no
        // triggers, views or memory mapping, held to tight limits, and
        // checked whole before anything else reads it.
        Foreign
    };

    // Opens the file at a UTF-8 path. On failure returns null and says why;
    // `rc` takes SQLite's result code.
    static std::unique_ptr<ALDbConnection> open(const std::string& path, Access access, std::string_view vfs,
                                                int* rc, std::string* error);
    // A file of someone else's, for reading: Access::Foreign, then
    // `PRAGMA integrity_check`, refused (null) unless it says "ok".
    static std::unique_ptr<ALDbConnection> openForeign(const std::string& path, std::string* error);

    ~ALDbConnection();
    ALDbConnection(const ALDbConnection&)            = delete;
    ALDbConnection& operator=(const ALDbConnection&) = delete;

    sqlite3*           handle() const { return mDb; }
    const std::string& path() const { return mPath; }
    Access             access() const { return mAccess; }

    // Runs SQL that returns nothing, one statement or several. The result
    // code; a failure is recorded. A single statement is kept with those
    // prepare() keeps, so that one run for every transaction or job (BEGIN,
    // COMMIT, a savepoint) is prepared once; text of several runs as a
    // script and is not kept.
    int exec(std::string_view sql);
    // The cached statement for this SQL, prepared the first time.
    ALDbStatement prepare(std::string_view sql);
    // The first column of the first row, or nothing when there is no row
    // or the query failed.
    std::optional<S64>         queryInt(std::string_view sql);
    std::optional<std::string> queryText(std::string_view sql);

    S64 lastInsertRowId() const;
    S32 changes() const;

    // The first failure recorded since clearFirstError, and SQLite's message
    // for it: SQLITE_OK while there is none. SQLITE_DONE and SQLITE_ROW are
    // never failures.
    int                firstError() const { return mFirstError; }
    const std::string& firstErrorMessage() const { return mFirstErrorMessage; }
    void               clearFirstError();
    // Records a failure of the caller's own, as one of SQLite's would be.
    void fail(int rc, std::string_view what);
    // Records `rc` if it is a failure, and returns it.
    int note(int rc, std::string_view what);

    // Statements prepared and kept, for the tests.
    size_t cachedStatements() const { return mStatements.size(); }

private:
    friend class ALDbStatement;
    ALDbConnection(sqlite3* db, std::string path, Access access);
    // A cached statement lent out, or one of its own for this borrower when
    // it is lent out already.
    ALDbStatement borrow(sqlite3_stmt* statement, std::string_view sql);
    void giveBack(sqlite3_stmt* statement, bool owned);

    struct StringHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view text) const noexcept;
    };

    sqlite3*    mDb = nullptr;
    std::string mPath;
    Access      mAccess;
    boost::unordered_flat_map<std::string, sqlite3_stmt*, StringHash, std::equal_to<>> mStatements;
    // The cached statements borrowed now: a second borrower of one of them
    // gets a statement of its own instead, so a query run inside the loop
    // over another's rows does not reset it.
    boost::unordered_flat_set<sqlite3_stmt*> mBorrowed;
    int         mFirstError = 0;
    std::string mFirstErrorMessage;
};

// A savepoint: released when it goes unless rolled back first. Nested ones
// nest. A savepoint that could not be begun does nothing.
class ALDbSavepoint
{
public:
    explicit ALDbSavepoint(ALDbConnection& connection, std::string_view name = "aldb");
    ~ALDbSavepoint();
    ALDbSavepoint(const ALDbSavepoint&)            = delete;
    ALDbSavepoint& operator=(const ALDbSavepoint&) = delete;

    bool begun() const { return mBegun; }
    // Undoes everything since it began, and ends it.
    void rollback();
    // Keeps everything since it began, and ends it. The result code.
    int release();

private:
    ALDbConnection& mConnection;
    std::string     mName;
    bool            mBegun = false;
};

// SQLite's own result codes, sorted by what a writer does about them.
namespace ALDb
{
    // A failure that may pass: busy or locked by another connection, the
    // disk full, an I/O error. The work is kept and tried again.
    bool isTransient(int rc);
    // The file is not a database, or is damaged.
    bool isCorrupt(int rc);
    // SQLite's name for a result code, extended codes included.
    std::string_view errorName(int rc);
}
