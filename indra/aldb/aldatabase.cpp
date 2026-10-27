/**
 * @file aldatabase.cpp
 * @brief One SQLite database file: opened, checked and migrated on its own
 *        writer thread, written in batches there, read through a pool.
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

#include "aldbrecovery.h"
#include "aldbwriter.h"
#include "fsyspath.h"
#include "llcoros.h"

#include LLCOROS_MUTEX_HEADER

#include <sqlite3.h>

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <system_error>

struct ALDbReaderPool
{
    LLCoros::Mutex                               mMutex;
    std::vector<std::unique_ptr<ALDbConnection>> mIdle;
    std::string                                  mPath;
    std::string                                  mVfs;
    U32                                          mKeep      = 4;
    int                                          mBusyMs    = 2000;
    bool                                         mOpen      = false;
};

namespace
{
    namespace fs = std::filesystem;

    std::atomic<U32> sTransientCount{ 0 };

    std::string fileName(const std::string& path)
    {
        return fsyspath(fsyspath(path).filename()).string();
    }

    bool fileHasContent(const std::string& path)
    {
        std::error_code ec;
        return fs::exists(fsyspath(path), ec) && fs::file_size(fsyspath(path), ec) > 0 && !ec;
    }

    // The pragmas a profile sets, in the order they must go: the journal
    // first, since what synchronous means depends on it. Where WAL cannot
    // run -- its shared memory does not work on a network filesystem -- the
    // file keeps a rollback journal, and says so.
    void configure(ALDbConnection& connection, ALDbProfile profile, const ALDbOptions& options, bool in_memory,
                   std::string& journal_mode)
    {
        sqlite3_busy_timeout(connection.handle(), int(options.busyTimeout.count()));

        if (in_memory)
        {
            journal_mode = connection.queryText("PRAGMA journal_mode").value_or("memory");
        }
        else
        {
            journal_mode = connection.queryText("PRAGMA journal_mode=WAL").value_or("");
            if (journal_mode != "wal")
            {
                LL_WARNS("ALDb") << connection.path() << ": WAL is not available here (" << journal_mode
                                 << "); using a rollback journal" << LL_ENDL;
                connection.clearFirstError();
                journal_mode = connection.queryText("PRAGMA journal_mode=DELETE").value_or("delete");
            }
        }

        switch (profile)
        {
            case ALDbProfile::Durable:
                connection.exec("PRAGMA synchronous=FULL");
#if LL_DARWIN
                // Without these an fsync on macOS reaches the drive's cache,
                // not the disk.
                connection.exec("PRAGMA fullfsync=ON");
                connection.exec("PRAGMA checkpoint_fullfsync=ON");
#endif
                break;
            case ALDbProfile::Journal:
                connection.exec("PRAGMA synchronous=NORMAL");
                break;
            case ALDbProfile::Regenerable:
                connection.exec("PRAGMA synchronous=OFF");
                connection.exec("PRAGMA mmap_size=268435456");
                connection.exec("PRAGMA cache_size=-65536");
                break;
        }
        connection.exec("PRAGMA foreign_keys=ON");
        connection.exec("PRAGMA temp_store=MEMORY");
        connection.exec("PRAGMA journal_size_limit=4194304");
        connection.exec("PRAGMA wal_autocheckpoint=1000");
    }

    // A whole copy of what `connection` holds at `destination`, which needs
    // no WAL beside it, checked before it takes the name.
    bool copyTo(ALDbConnection& connection, const std::string& destination, std::string* error)
    {
        const std::string temporary = destination + ".tmp";
        std::error_code   ec;
        ALDbRecovery::remove(temporary);

        connection.clearFirstError();
        ALDbStatement vacuum = connection.prepare("VACUUM INTO ?1");
        vacuum.bind(1, std::string_view(temporary));
        if (vacuum.run() != SQLITE_OK)
        {
            if (error)
            {
                *error = connection.firstErrorMessage();
            }
            connection.clearFirstError();
            ALDbRecovery::remove(temporary);
            return false;
        }
        vacuum = ALDbStatement();

        {
            // Its own journal, so that the copy is one file.
            int                             rc = SQLITE_OK;
            std::unique_ptr<ALDbConnection> copy =
                ALDbConnection::open(temporary, ALDbConnection::Access::ReadWrite, {}, &rc, error);
            if (!copy || copy->queryText("PRAGMA journal_mode=DELETE").value_or("") != "delete")
            {
                copy.reset();
                ALDbRecovery::remove(temporary);
                return false;
            }
        }
        if (!ALDbConnection::openForeign(temporary, error))
        {
            ALDbRecovery::remove(temporary);
            return false;
        }

        fs::rename(fsyspath(temporary), fsyspath(destination), ec);
        if (ec)
        {
            if (error)
            {
                *error = fmt::format("could not move the copy into place: {}", ec.message());
            }
            ALDbRecovery::remove(temporary);
            return false;
        }
        return true;
    }

    // The copies taken before migrating, oldest first: <file>.v<N>.bak.
    void pruneMigrationBackups(const std::string& path, U32 keep)
    {
        const fsyspath    file   = fsyspath(path);
        const std::string prefix = fsyspath(file.filename()).string() + ".v";
        std::vector<std::pair<S32, fs::path>> found;
        std::error_code ec;
        for (const fs::directory_entry& entry : fs::directory_iterator(file.parent_path(), ec))
        {
            const std::string name = fsyspath(entry.path().filename()).string();
            if (name.size() <= prefix.size() + 4 || name.compare(0, prefix.size(), prefix) != 0 ||
                name.compare(name.size() - 4, 4, ".bak") != 0)
            {
                continue;
            }
            S32         version = 0;
            const char* first   = name.data() + prefix.size();
            const char* last    = name.data() + name.size() - 4;
            if (std::from_chars(first, last, version).ptr == last)
            {
                found.emplace_back(version, entry.path());
            }
        }
        std::sort(found.begin(), found.end());
        while (found.size() > keep)
        {
            fs::remove(found.front().second, ec);
            found.erase(found.begin());
        }
    }

    S32 latestVersion(const std::vector<ALDbMigration>& migrations)
    {
        S32 latest = 0;
        for (const ALDbMigration& migration : migrations)
        {
            latest = std::max(latest, migration.version);
        }
        return latest;
    }
}

//
// ALDbReadLease
//

ALDbReadLease::ALDbReadLease(std::shared_ptr<ALDbReaderPool> pool, std::unique_ptr<ALDbConnection> connection)
:   mPool(std::move(pool)),
    mConnection(std::move(connection))
{
}

ALDbReadLease::ALDbReadLease(ALDbReadLease&& other) noexcept = default;

ALDbReadLease& ALDbReadLease::operator=(ALDbReadLease&& other) noexcept
{
    if (this != &other)
    {
        giveBack();
        mPool       = std::move(other.mPool);
        mConnection = std::move(other.mConnection);
    }
    return *this;
}

ALDbReadLease::~ALDbReadLease()
{
    giveBack();
}

void ALDbReadLease::giveBack()
{
    if (mPool && mConnection)
    {
        mConnection->clearFirstError();
        LLCoros::LockType lock(mPool->mMutex);
        if (mPool->mOpen && mPool->mIdle.size() < mPool->mKeep)
        {
            mPool->mIdle.push_back(std::move(mConnection));
        }
    }
    mConnection.reset();
    mPool.reset();
}

//
// ALDatabase
//

ALDatabase::ALDatabase(std::string path, ALDbOptions options)
:   mPath(std::move(path)),
    mOptions(std::move(options)),
    mReaders(std::make_shared<ALDbReaderPool>())
{
    std::sort(mOptions.migrations.begin(), mOptions.migrations.end(),
              [](const ALDbMigration& a, const ALDbMigration& b) { return a.version < b.version; });
}

ALDatabase::~ALDatabase()
{
    close();
}

bool ALDatabase::isOpen() const
{
    return mOpened && !mClosed && mOpenResult.usable();
}

bool ALDatabase::transient() const
{
    return mOpenResult.outcome == ALDbOpenResult::Outcome::Transient;
}

const ALDbOpenResult& ALDatabase::open()
{
    if (mOpened)
    {
        return mOpenResult;
    }
    mOpened = true;

    mWriter = std::make_shared<ALDbWriter>(fileName(mPath), mOptions);
    mWriter->start();

    ALDbOpenResult result;
    std::string    reader_path = mPath;
    const bool     ran = mWriter->control([this, &result, &reader_path](std::unique_ptr<ALDbConnection>& slot) {
        const ALDbOptions& options  = mOptions;
        const bool         existed  = fileHasContent(mPath);
        const S32          latest   = latestVersion(options.migrations);
        bool               damaged  = false;
        std::string        problem;

        std::unique_ptr<ALDbConnection> connection;
        int                             rc = SQLITE_OK;
        connection = ALDbConnection::open(mPath, ALDbConnection::Access::ReadWrite, options.vfs, &rc, &problem);
        if (!connection)
        {
            damaged = ALDb::isCorrupt(rc);
        }
        else
        {
            // Before the first read: a file another viewer is writing is
            // waited for, not taken for damage.
            sqlite3_busy_timeout(connection->handle(), int(options.busyTimeout.count()));
            // Reading the header is the first read: a file that is not a
            // database, or one with a bad first page, fails here.
            const std::optional<S64> application_id = connection->queryInt("PRAGMA application_id");
            if (!application_id)
            {
                damaged = ALDb::isCorrupt(connection->firstError());
                problem = connection->firstErrorMessage();
            }
            else if (*application_id != 0 && options.applicationId != 0 && U32(*application_id) != options.applicationId)
            {
                result.outcome = ALDbOpenResult::Outcome::Refused;
                result.message = fmt::format("{} is another kind of file (application_id {:#x})", mPath,
                                             U32(*application_id));
                LL_WARNS("ALDb") << result.message << LL_ENDL;
                return;
            }
            else if (existed && options.checkIntegrity)
            {
                const std::optional<std::string> check = connection->queryText("PRAGMA quick_check");
                if (check)
                {
                    damaged = *check != "ok";
                    problem = *check;
                }
                else
                {
                    // A check that could not read the file -- busy past the
                    // timeout, an I/O error -- says nothing of what is in it.
                    damaged = ALDb::isCorrupt(connection->firstError());
                    problem = connection->firstErrorMessage();
                    if (!damaged)
                    {
                        LL_WARNS("ALDb") << mPath << " could not be checked (" << problem << "); opened unchecked"
                                         << LL_ENDL;
                    }
                }
            }
        }

        if (damaged)
        {
            connection.reset();
            LL_WARNS("ALDb") << mPath << " is damaged: " << problem << LL_ENDL;
            if (!options.mayQuarantine)
            {
                result.message = "damaged, and another process may hold it: " + problem;
            }
            else if (options.profile == ALDbProfile::Regenerable)
            {
                std::string error;
                if (ALDbRecovery::remove(mPath, &error))
                {
                    result.outcome = ALDbOpenResult::Outcome::Rebuilt;
                }
                else
                {
                    result.message = error;
                }
            }
            else
            {
                std::string                      error;
                const std::optional<std::string> moved = ALDbRecovery::quarantine(mPath, &error);
                if (moved)
                {
                    result.quarantinedTo = *moved;
                    result.outcome       = ALDbOpenResult::Outcome::Recovered;
                    if (!ALDbRecovery::salvage(*moved, mPath, &error))
                    {
                        // Nothing could be read: start again, the damaged file
                        // kept where it was moved.
                        LL_WARNS("ALDb") << error << LL_ENDL;
                        ALDbRecovery::remove(mPath);
                    }
                }
                else
                {
                    result.message = error;
                }
            }

            if (result.outcome == ALDbOpenResult::Outcome::Recovered ||
                result.outcome == ALDbOpenResult::Outcome::Rebuilt)
            {
                connection =
                    ALDbConnection::open(mPath, ALDbConnection::Access::ReadWrite, options.vfs, &rc, &problem);
                if (!connection)
                {
                    result.message = problem;
                }
            }
        }

        bool in_memory = false;
        if (!connection)
        {
            // Nothing on disk to use this session: keep going in memory, and
            // say so. The name is the memdb VFS's shared form, so that the
            // pool's readers see what the writer writes.
            reader_path =
                fmt::format("file:/aldb-transient-{}-{}?vfs=memdb", sTransientCount++, fileName(mPath));
            connection = ALDbConnection::open(reader_path, ALDbConnection::Access::ReadWrite, {}, &rc, &problem);
            if (!connection)
            {
                result.outcome = ALDbOpenResult::Outcome::Failed;
                result.message = "could not open even a database in memory: " + problem;
                return;
            }
            LL_WARNS("ALDb") << mPath << " cannot be used this session (" << result.message
                             << "); running in memory" << LL_ENDL;
            result.outcome = ALDbOpenResult::Outcome::Transient;
            in_memory      = true;
        }

        connection->clearFirstError();
        configure(*connection, options.profile, options, in_memory, result.journalMode);
        connection->clearFirstError();

        // SQLite opens a file it may not write -- marked read-only, owned by
        // another user, on read-only media -- for reading alone, and says so
        // only here.
        result.readOnly = !in_memory && sqlite3_db_readonly(connection->handle(), "main") == 1;
        if (result.readOnly)
        {
            LL_WARNS("ALDb") << mPath << " can only be read: nothing will be written to it" << LL_ENDL;
        }

        // The version the file is at, and the steps that bring it to now.
        // New: in memory, or at version 0 with nothing in it -- a file made
        // now, rebuilt, or that nothing could be salvaged from.
        S32  before = S32(connection->queryInt("PRAGMA user_version").value_or(0));
        bool is_new =
            in_memory || (before == 0 && connection->queryInt("SELECT count(*) FROM sqlite_schema").value_or(0) == 0);
        result.versionBefore = before;
        result.versionAfter  = before;
        if (before > latest)
        {
            // Written by a newer build: read and written as it is, its newer
            // tables left alone, nothing run backwards.
            LL_INFOS("ALDb") << mPath << " is at version " << before << ", newer than this build's " << latest
                             << "; opened as it is" << LL_ENDL;
        }
        else if (before < latest)
        {
            if (!is_new && before > 0)
            {
                std::string error;
                const std::string backup = fmt::format("{}.v{}.bak", mPath, before);
                if (!copyTo(*connection, backup, &error))
                {
                    LL_WARNS("ALDb") << mPath << ": could not keep a copy before migrating: " << error << LL_ENDL;
                }
                pruneMigrationBackups(mPath, options.keepMigrationBackups);
            }

            connection->clearFirstError();
            connection->exec("BEGIN IMMEDIATE");
            // Read again under the lock: another viewer opening the file at
            // the same moment may have migrated it while this one waited.
            if (connection->firstError() == SQLITE_OK)
            {
                if (const std::optional<S64> locked = connection->queryInt("PRAGMA user_version");
                    locked && S32(*locked) != before)
                {
                    LL_INFOS("ALDb") << mPath << " was brought to version " << *locked
                                     << " by another process as this one opened it" << LL_ENDL;
                    before               = S32(*locked);
                    is_new               = false;
                    result.versionBefore = before;
                    result.versionAfter  = before;
                }
            }
            for (const ALDbMigration& migration : options.migrations)
            {
                if (migration.version <= before || connection->firstError() != SQLITE_OK)
                {
                    continue;
                }
                try
                {
                    migration.apply(*connection);
                }
                catch (const std::exception& e)
                {
                    connection->fail(SQLITE_ABORT, std::string("the migration threw: ") + e.what());
                }
                if (connection->firstError() == SQLITE_OK)
                {
                    connection->exec(fmt::format("PRAGMA user_version={}", migration.version));
                }
                if (connection->firstError() != SQLITE_OK)
                {
                    result.message = fmt::format("migrating to version {} failed: {}", migration.version,
                                                 connection->firstErrorMessage());
                }
            }
            if (connection->firstError() == SQLITE_OK)
            {
                connection->exec("COMMIT");
            }
            if (connection->firstError() != SQLITE_OK)
            {
                if (result.message.empty())
                {
                    result.message = connection->firstErrorMessage();
                }
                sqlite3_exec(connection->handle(), "ROLLBACK", nullptr, nullptr, nullptr);
                LL_WARNS("ALDb") << mPath << ": " << result.message << "; the file is as it was" << LL_ENDL;
                result.outcome = ALDbOpenResult::Outcome::Failed;
                return;
            }
            if (before < latest)
            {
                result.versionAfter = latest;
                result.migrated     = true;
            }
        }

        if (is_new && options.applicationId != 0)
        {
            connection->exec(fmt::format("PRAGMA application_id={}", S64(options.applicationId)));
        }
        connection->clearFirstError();

        if (result.outcome == ALDbOpenResult::Outcome::Failed)
        {
            result.outcome = is_new ? ALDbOpenResult::Outcome::Created : ALDbOpenResult::Outcome::Opened;
        }
        slot = std::move(connection);
    });
    if (!ran)
    {
        result.outcome = ALDbOpenResult::Outcome::Failed;
        result.message = "opening threw";
    }
    mOpenResult = std::move(result);

    if (mOpenResult.usable())
    {
        LLCoros::LockType lock(mReaders->mMutex);
        mReaders->mPath   = reader_path;
        mReaders->mVfs    = transient() ? std::string() : mOptions.vfs;
        mReaders->mKeep   = mOptions.readers;
        mReaders->mBusyMs = int(mOptions.busyTimeout.count());
        mReaders->mOpen   = true;
    }
    else
    {
        mWriter->close(mOptions.closeWait);
    }
    return mOpenResult;
}

bool ALDatabase::post(Job job)
{
    return isOpen() && mWriter->post(std::move(job));
}

bool ALDatabase::post(Job job, Completion completed)
{
    return isOpen() && mWriter->post(std::move(job), nullptr, std::move(completed));
}

bool ALDatabase::postAndWait(Job job)
{
    if (!isOpen())
    {
        return false;
    }
    auto                  done   = std::make_shared<LLCoros::Promise<bool>>();
    LLCoros::Future<bool> result = done->get_future();
    if (!mWriter->post(std::move(job), done))
    {
        return false;
    }
    return result.get();
}

bool ALDatabase::flush()
{
    if (!isOpen())
    {
        return false;
    }
    // Its job holds nothing of the caller's, so it can be left queued.
    auto                  done   = std::make_shared<LLCoros::Promise<bool>>();
    LLCoros::Future<bool> result = done->get_future();
    if (!mWriter->post([](ALDbConnection&) {}, done))
    {
        return false;
    }
    if (result.wait_for(mOptions.closeWait) != boost::fibers::future_status::ready)
    {
        LL_WARNS("ALDb") << mPath << ": not written within " << mOptions.closeWait.count()
                         << " ms; the writer is still trying" << LL_ENDL;
        return false;
    }
    return result.get();
}

ALDbReadLease ALDatabase::read()
{
    std::string path;
    std::string vfs;
    int         busy_ms = 0;
    {
        LLCoros::LockType lock(mReaders->mMutex);
        if (!mReaders->mOpen)
        {
            return {};
        }
        if (!mReaders->mIdle.empty())
        {
            std::unique_ptr<ALDbConnection> connection = std::move(mReaders->mIdle.back());
            mReaders->mIdle.pop_back();
            return ALDbReadLease(mReaders, std::move(connection));
        }
        path    = mReaders->mPath;
        vfs     = mReaders->mVfs;
        busy_ms = mReaders->mBusyMs;
    }

    int                             rc = SQLITE_OK;
    std::string                     error;
    std::unique_ptr<ALDbConnection> connection =
        ALDbConnection::open(path, ALDbConnection::Access::ReadOnly, vfs, &rc, &error);
    if (!connection)
    {
        LL_WARNS("ALDb") << mPath << ": could not open a reader: " << error << LL_ENDL;
        return {};
    }
    sqlite3_busy_timeout(connection->handle(), busy_ms);
    connection->exec("PRAGMA query_only=1");
    connection->clearFirstError();
    return ALDbReadLease(mReaders, std::move(connection));
}

bool ALDatabase::backupTo(const std::string& destination, std::string* error)
{
    if (!isOpen())
    {
        if (error)
        {
            *error = "the database is not open";
        }
        return false;
    }
    bool        copied = false;
    std::string why;
    // On the writer, between transactions: VACUUM INTO cannot run inside one.
    const bool ran = mWriter->control([&](std::unique_ptr<ALDbConnection>& connection) {
        copied = connection && copyTo(*connection, destination, &why);
    });
    if (error && !(ran && copied))
    {
        *error = ran ? why : "the database is closing";
    }
    return ran && copied;
}

bool ALDatabase::close()
{
    if (!mOpened || mClosed)
    {
        return true;
    }
    mClosed = true;
    {
        LLCoros::LockType lock(mReaders->mMutex);
        mReaders->mOpen = false;
        mReaders->mIdle.clear();
    }
    return mWriter->close(mOptions.closeWait);
}

ALDbStats ALDatabase::stats() const
{
    return mWriter ? mWriter->stats() : ALDbStats();
}
