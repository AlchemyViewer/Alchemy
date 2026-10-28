/**
 * @file aldatabase.h
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

#pragma once

#include "aldbconnection.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ALDbWriter;
struct ALDbReaderPool;

// How a file is kept, by what losing its last writes would cost.
enum class ALDbProfile
{
    // The user's own choices: every commit synced to the disk, an fsync a
    // commit on the writer thread. Damage is salvaged.
    Durable,
    // Survives the application crashing; a power loss may drop the last
    // commits.
    Journal,
    // Rebuilt from elsewhere when lost: not synced, mapped into memory, and
    // deleted and made again, not salvaged, when damaged.
    Regenerable
};

// One step of a file's tables: run, inside the open's transaction, on a
// file whose user_version is below `version`, which the file then has.
struct ALDbMigration
{
    S32                                  version = 0;
    std::function<void(ALDbConnection&)> apply;
};

// A failure the writer could not get past by itself: the disk full, the file
// locked by another process for longer than a retry or two. Said once, on
// the writer thread, until the writer next succeeds; damage found while
// writing, once for as long as the file is open.
struct ALDbFailure
{
    int         code = 0;
    std::string message;
};

struct ALDbOptions
{
    ALDbProfile profile = ALDbProfile::Durable;
    // The four bytes of a file's header saying what kind of file it is
    // (PRAGMA application_id), as a FourCC: 'ALST'. A file that carries
    // another is refused, not written. 0 neither sets nor checks one.
    U32 applicationId = 0;
    // In ascending order of version.
    std::vector<ALDbMigration> migrations;
    // The last run of this process ended without closing the file: check it
    // before trusting it.
    bool checkIntegrity = false;
    // This process may move a damaged file aside. Only one process should:
    // the one that holds the application's instance lock.
    bool mayQuarantine = true;
    // A VFS of SQLite's registry to open the file through, for the tests.
    std::string vfs;
    std::chrono::milliseconds busyTimeout{ 2000 };
    // The writer's wait before trying a failed batch again, doubling up to
    // the longest.
    std::chrono::milliseconds retryFirst{ 100 };
    std::chrono::milliseconds retryLongest{ 5000 };
    // How long close() lets the writer finish what was posted.
    std::chrono::milliseconds closeWait{ 2000 };
    // The copies taken before migrating that are kept: <file>.v<N>.bak.
    U32 keepMigrationBackups = 2;
    // Reading connections kept open at most.
    U32 readers = 4;
    // On the writer thread.
    std::function<void(const ALDbFailure&)> onFailure;
};

struct ALDbOpenResult
{
    enum class Outcome
    {
        // An existing file, migrated or not.
        Opened,
        // A new file, its tables made.
        Created,
        // A damaged file, moved aside and what could be read of it written
        // into a new one.
        Recovered,
        // A damaged file of the Regenerable profile, deleted and made again.
        Rebuilt,
        // The file could not be opened or moved aside: this session runs on
        // a database in memory, and nothing it writes is kept.
        Transient,
        // The file is another kind of file (its application_id): untouched.
        Refused,
        // A migration failed: the file is as it was, and not open.
        Failed
    };

    Outcome     outcome       = Outcome::Failed;
    S32         versionBefore = 0;
    S32         versionAfter  = 0;
    bool        migrated      = false;
    // The file could be opened only for reading -- marked read-only, owned
    // by another user, on read-only media -- so its reads run and every
    // write is refused, said once through onFailure.
    bool        readOnly      = false;
    // "wal", or "delete" where WAL cannot run (a network filesystem).
    std::string journalMode;
    // Where a damaged file was moved.
    std::string quarantinedTo;
    std::string message;

    bool usable() const { return outcome != Outcome::Refused && outcome != Outcome::Failed; }
};

struct ALDbStats
{
    U64 commits = 0;
    U64 committedItems = 0;
    U64 droppedItems = 0;
    U64 retries = 0;
    U64 queued = 0;
};

// A reading connection of the pool's, this thread's until the lease goes.
class ALDbReadLease
{
public:
    ALDbReadLease() = default;
    ~ALDbReadLease();
    ALDbReadLease(ALDbReadLease&& other) noexcept;
    ALDbReadLease& operator=(ALDbReadLease&& other) noexcept;
    ALDbReadLease(const ALDbReadLease&)            = delete;
    ALDbReadLease& operator=(const ALDbReadLease&) = delete;

    explicit operator bool() const { return mConnection != nullptr; }
    ALDbConnection* operator->() const { return mConnection.get(); }
    ALDbConnection& operator*() const { return *mConnection; }

private:
    friend class ALDatabase;
    ALDbReadLease(std::shared_ptr<ALDbReaderPool> pool, std::unique_ptr<ALDbConnection> connection);
    void giveBack();

    std::shared_ptr<ALDbReaderPool> mPool;
    std::unique_ptr<ALDbConnection> mConnection;
};

// One database file. open() starts its writer thread, which opens the file,
// checks it after a crash, moves it aside and salvages it if it is damaged,
// and migrates it; then every write goes to that thread as a job, and the
// jobs queued while it worked on the last are written together in one
// transaction, each in a savepoint of its own: a job that fails is undone
// alone. A job may be run twice -- a batch that could not commit is tried
// again whole -- so each is written to be safe to repeat: an upsert keyed by
// what it writes, never by a row id held in memory.
//
// Opened, posted to and closed from one thread; reads and backups from any.
class ALDatabase
{
public:
    using Job = std::function<void(ALDbConnection&)>;
    // Told whether a posted job committed: on the writer thread, and maybe
    // after close() returned false, from a writer left to finish -- so it
    // holds what it needs rather than pointing into its owner.
    using Completion = std::function<void(bool committed)>;

    ALDatabase(std::string path, ALDbOptions options);
    ~ALDatabase();
    ALDatabase(const ALDatabase&)            = delete;
    ALDatabase& operator=(const ALDatabase&) = delete;

    // Opens the file, on the writer thread, and waits for it. Once.
    const ALDbOpenResult& open();
    const ALDbOpenResult& openResult() const { return mOpenResult; }

    bool isOpen() const;
    // Running on a database in memory (ALDbOpenResult::Outcome::Transient).
    bool transient() const;
    const std::string& path() const { return mPath; }

    // A job for the writer's next transaction. False, and the job not run,
    // when the database is not open or is closing.
    bool post(Job job);
    // The same, and `completed` told whether it committed.
    bool post(Job job, Completion completed);
    // The same, and waits for it: true when it committed, false when it
    // failed and was undone, or could not be run.
    bool postAndWait(Job job);
    // Waits until everything posted before it has been written or dropped,
    // for `closeWait` at most: false if it was not, and the writer goes on
    // trying -- a disk that hangs, a file another process holds locked.
    bool flush();

    // A reading connection, made the first time a thread asks for one more
    // than the pool holds: empty when the database is not open.
    ALDbReadLease read();

    // A whole, self-contained copy of the database at `destination`, written
    // by VACUUM INTO a temporary file beside it, made to need no WAL,
    // checked, and renamed into place: there is a whole copy there or none.
    bool backupTo(const std::string& destination, std::string* error = nullptr);

    // Stops taking jobs, gives the writer `closeWait` to write what was
    // posted, checkpoints and closes the file. A writer still busy after
    // that -- a disk that hangs -- is left to finish on its own, and close
    // returns false; WAL leaves the file whole either way.
    bool close();

    ALDbStats stats() const;

private:
    std::string                     mPath;
    ALDbOptions                     mOptions;
    ALDbOpenResult                  mOpenResult;
    std::shared_ptr<ALDbWriter>     mWriter;
    std::shared_ptr<ALDbReaderPool> mReaders;
    bool                            mOpened = false;
    bool                            mClosed = false;
};
