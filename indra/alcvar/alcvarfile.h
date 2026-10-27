/**
 * @file alcvarfile.h
 * @brief One settings file, settings.db: its tables, and its rows read and
 *        written on its writer thread.
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

#include "alcvarconvert.h"

#include "aldatabase.h"

#include "llcoros.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// A row of the settings table: a value with the type and unit it was written
// in and the fingerprint of the step of its setting's history it was
// written at, or a tombstone saying the user's value was removed.
struct ALCVarRow
{
    std::string name;
    // The type's number as written; one this build does not know leaves the
    // row unknown and untouched.
    S32         type = 0;
    // The unit's name as written, "" for none.
    std::string unit;
    U64         decl = 0;
    ALCVarValue value;
    bool        tombstone = false;
    // Unix milliseconds.
    S64         modified = 0;

    // The row as the one conversion takes it, on the main thread: an LLSD
    // value is read as its text and parsed here. Null where its type or unit
    // is one this build does not know.
    std::optional<ALCVarStored> stored() const;
};

// A history row: one a setting a flush, its oldest old value and newest new
// one, or nothing of either for a sensitive setting.
struct ALCVarHistory
{
    // A change, or a row brought forward at open.
    enum Kind : S32
    {
        CHANGED  = 0,
        MIGRATED = 1,
    };

    S64                        at      = 0;
    S64                        session = 0;
    std::string                name;
    std::optional<ALCVarValue> before;
    std::optional<ALCVarValue> after;
    S32                        kind = CHANGED;
};

// The settings file's options as its owner opens it.
struct ALCVarFileOptions
{
    // The last run ended without closing the file: check it first.
    bool        checkIntegrity = false;
    // This process holds the viewer's instance lock and may move a damaged
    // file aside.
    bool        mayQuarantine = true;
    // A VFS of SQLite's registry, for the tests.
    std::string vfs;
    // An LLControlGroup settings file to import, if this file has imported
    // none before (ALCVarXmlImport).
    std::string importXml;
    // Told of a failure the file could not get past -- the disk full, the
    // file locked by another process, damage found while writing, a file
    // that may only be read -- for the viewer to say so: on the file's
    // writer thread, or as it opens on the thread opening it. What could not
    // be written is kept in memory and tried again with the next flush.
    std::function<void(const ALDbFailure&)> onFailure;
};

// settings.db: the user's values of one scope, global or one account's, in
// a Durable file of aldb's. Its rows are read once, at open, and written
// in batches as the store flushes; rows this build knows nothing of are
// kept and never touched, for the viewer that wrote them.
class ALCVarFile
{
public:
    // 'ALST', what kind of file this is.
    static constexpr U32 APPLICATION_ID = 0x414C5354;
    // The history kept, most recent last.
    static constexpr S64 HISTORY_ROWS   = 10000;
    // How long a flush or a close waits for the writer: a disk that hangs
    // holds the main thread no longer.
    static constexpr std::chrono::milliseconds CLOSE_WAIT{ 2000 };

    ALCVarFile(std::string path, const ALCVarFileOptions& options);
    ~ALCVarFile();
    ALCVarFile(const ALCVarFile&)            = delete;
    ALCVarFile& operator=(const ALCVarFile&) = delete;

    // Opens it on its writer thread and waits.
    const ALDbOpenResult& open();
    bool                  usable() const { return mDatabase.openResult().usable() && mDatabase.isOpen(); }
    // Open only for reading: nothing will be written to it.
    bool                  readOnly() const { return mDatabase.openResult().readOnly; }
    const std::string&    path() const { return mDatabase.path(); }
    ALDatabase&           database() { return mDatabase; }

    // Every row, read on the writer, waited for: null when the read failed,
    // since rows read up to damage are not the file's.
    std::optional<std::vector<ALCVarRow>> readRows();
    // The meta table, read the same way.
    std::optional<boost::unordered_flat_map<std::string, std::string>> readMeta();
    // The rows migrates replaced, as they were: for a viewer that does not
    // know the step a row was brought to.
    std::optional<std::vector<ALCVarRow>> readPrior();

    // Posts one transaction to the writer: the rows upserted by name, the
    // history appended and trimmed, the meta values set; `completed`, if
    // given, told on the writer thread whether it committed. False when the
    // file is not open.
    bool write(std::vector<ALCVarRow> rows, std::vector<ALCVarHistory> history,
               std::vector<std::pair<std::string, std::string>> meta, ALDatabase::Completion completed = nullptr);
    // The open's transaction: rows brought forward, the history of each, and
    // the rows a migrate replaced kept as they were, by name and step. A
    // write() of a setting drops its prior rows: the value they held is no
    // longer the user's latest.
    bool bringForward(std::vector<ALCVarRow> rows, std::vector<ALCVarRow> prior, std::vector<ALCVarHistory> history,
                      std::vector<std::pair<std::string, std::string>> meta);
    // Writes rows imported from `source` unless the file records an import
    // already: the check and the rows in one transaction, so two viewers
    // starting at once import once, the import recorded as pending at
    // `stamp` until a quality level settles it. Waits; true when this call
    // imported.
    bool importRows(std::vector<ALCVarRow> rows, const std::string& source, S64 stamp);
    // Waits for everything posted before it, for CLOSE_WAIT at most.
    bool flush();
    // Ready once everything posted before it has been written or dropped,
    // or at once, false, when the file is not open: for a caller that waits
    // on more than one file at a time.
    LLCoros::Future<bool> flushed();
    bool close();

    // A value as a row's value column holds it, and back: numbers as
    // themselves, a UUID as its text, vectors, lists and LLSD as JSON. An
    // LLSD value is read back as its text, for ALCVarRow::stored() to parse
    // on the main thread.
    static void        bindValue(ALDbStatement& statement, int index, const ALCVarValue& value);
    static ALCVarValue columnValue(ALDbStatement& statement, int column, S32 type);
    // A value as it goes to the file, made on the main thread: an LLSD
    // value as its JSON text, since LLSD's reference counts are not atomic
    // and the flusher's and the writer's threads take it from there.
    static ALCVarValue persisted(const ALCVarValue& value);

private:
    ALDatabase                              mDatabase;
    std::function<void(const ALDbFailure&)> mOnFailure;
};
