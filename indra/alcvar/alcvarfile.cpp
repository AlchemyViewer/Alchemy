/**
 * @file alcvarfile.cpp
 * @brief One settings file, settings.db.
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

#include "alcvarfile.h"

#include <sqlite3.h>

#include <memory>

namespace
{
    // Version 1: the meta, settings, history and prior tables.
    void makeTables(ALDbConnection& connection)
    {
        connection.exec(R"sql(
            CREATE TABLE meta(
                key   TEXT PRIMARY KEY,
                value ANY
            ) STRICT, WITHOUT ROWID;

            CREATE TABLE settings(
                name      TEXT PRIMARY KEY,
                type      INTEGER NOT NULL,
                unit      TEXT,
                decl      INTEGER NOT NULL,
                value     ANY,
                tombstone INTEGER NOT NULL DEFAULT 0,
                modified  INTEGER NOT NULL
            ) STRICT, WITHOUT ROWID;

            CREATE TABLE history(
                at      INTEGER NOT NULL,
                session INTEGER NOT NULL,
                name    TEXT NOT NULL,
                kind    INTEGER NOT NULL DEFAULT 0,
                old     ANY,
                new     ANY
            ) STRICT;

            CREATE TABLE prior(
                name     TEXT NOT NULL,
                decl     INTEGER NOT NULL,
                type     INTEGER NOT NULL,
                unit     TEXT,
                value    ANY,
                modified INTEGER NOT NULL,
                PRIMARY KEY(name, decl)
            ) STRICT, WITHOUT ROWID;
        )sql");
    }

    ALDbOptions databaseOptions(const ALCVarFileOptions& options)
    {
        ALDbOptions db;
        db.profile        = ALDbProfile::Durable;
        db.applicationId  = ALCVarFile::APPLICATION_ID;
        db.migrations     = { { 1, makeTables } };
        db.checkIntegrity = options.checkIntegrity;
        db.mayQuarantine  = options.mayQuarantine;
        db.vfs            = options.vfs;
        db.readers        = 0;
        db.closeWait      = ALCVarFile::CLOSE_WAIT;
        db.onFailure      = options.onFailure;
        return db;
    }

    bool knownType(S32 type)
    {
        return type >= 0 && type < S32(ALCVAR_TYPE_COUNT);
    }
}

std::optional<ALCVarStored> ALCVarRow::stored() const
{
    if (!knownType(type) || tombstone)
    {
        return std::nullopt;
    }
    ALCVarStored out;
    out.value = value;
    if (ALCVarType(type) == ALCVarType::LLSD && value.type() == ALCVarType::String)
    {
        if (auto parsed = ALCVarValues::fromJson(ALCVarType::LLSD, value.get<std::string>()))
        {
            out.value = std::move(*parsed);
        }
    }
    out.decl = decl;
    if (!unit.empty())
    {
        const auto known = ALCVarUnits::fromName(unit);
        // A unit this build does not know scales into nothing.
        out.unit = known ? *known : ALCVarUnit::Count;
    }
    return out;
}

ALCVarFile::ALCVarFile(std::string path, const ALCVarFileOptions& options)
:   mDatabase(std::move(path), databaseOptions(options)),
    mOnFailure(options.onFailure)
{
}

ALCVarFile::~ALCVarFile()
{
    close();
}

const ALDbOpenResult& ALCVarFile::open()
{
    const ALDbOpenResult& result = mDatabase.open();
    // Nothing is posted to a file that may only be read, so its writer never
    // says so: said here, once.
    if (result.usable() && result.readOnly && mOnFailure)
    {
        mOnFailure({ SQLITE_READONLY, path() + " can only be read: changes are kept for this session alone" });
    }
    return result;
}

void ALCVarFile::bindValue(ALDbStatement& statement, int index, const ALCVarValue& value)
{
    if (value.empty())
    {
        statement.bindNull(index);
        return;
    }
    switch (value.type())
    {
        case ALCVarType::Bool:   statement.bind(index, value.get<bool>()); break;
        case ALCVarType::S32:    statement.bind(index, value.get<S32>()); break;
        case ALCVarType::U32:    statement.bind(index, S64(value.get<U32>())); break;
        case ALCVarType::S64:    statement.bind(index, value.get<S64>()); break;
        case ALCVarType::F32:    statement.bind(index, F64(value.get<F32>())); break;
        case ALCVarType::F64:    statement.bind(index, value.get<F64>()); break;
        case ALCVarType::String: statement.bind(index, std::string_view(value.get<std::string>())); break;
        case ALCVarType::UUID:   statement.bind(index, std::string_view(value.get<LLUUID>().asString())); break;
        default:                 statement.bind(index, std::string_view(ALCVarValues::toJson(value))); break;
    }
}

ALCVarValue ALCVarFile::columnValue(ALDbStatement& statement, int column, S32 type)
{
    switch (statement.columnType(column))
    {
        case SQLITE_NULL:
            return {};
        case SQLITE_INTEGER:
        {
            const S64 v = statement.columnInt(column);
            switch (ALCVarType(type))
            {
                case ALCVarType::Bool: return ALCVarValue(v != 0);
                case ALCVarType::S32:
                    if (v >= INT32_MIN && v <= INT32_MAX)
                    {
                        return ALCVarValue(S32(v));
                    }
                    break;
                case ALCVarType::U32:
                    if (v >= 0 && v <= S64(UINT32_MAX))
                    {
                        return ALCVarValue(U32(v));
                    }
                    break;
                default: break;
            }
            // Wider than the type it says, or another type's: as a number,
            // for the conversion to make of it what it can.
            return ALCVarValue(v);
        }
        case SQLITE_FLOAT:
        {
            const F64 v = statement.columnReal(column);
            return ALCVarType(type) == ALCVarType::F32 ? ALCVarValue(F32(v)) : ALCVarValue(v);
        }
        default:
        {
            const std::string text(statement.columnText(column));
            if (knownType(type) && ALCVarType(type) != ALCVarType::String)
            {
                if (ALCVarType(type) == ALCVarType::UUID)
                {
                    LLUUID id;
                    if (LLUUID::validate(text) && id.set(text, false))
                    {
                        return ALCVarValue(id);
                    }
                }
                else if (ALCVarType(type) != ALCVarType::LLSD &&
                         ALCVarTypes::storageOf(ALCVarType(type)) != ALCVarStorage::Word &&
                         ALCVarTypes::storageOf(ALCVarType(type)) != ALCVarStorage::Byte)
                {
                    if (auto value = ALCVarValues::fromJson(ALCVarType(type), text))
                    {
                        return std::move(*value);
                    }
                }
            }
            return ALCVarValue(text);
        }
    }
}

ALCVarValue ALCVarFile::persisted(const ALCVarValue& value)
{
    if (!value.empty() && value.type() == ALCVarType::LLSD)
    {
        return ALCVarValue(ALCVarValues::toJson(value));
    }
    return value;
}

std::optional<std::vector<ALCVarRow>> ALCVarFile::readRows()
{
    auto rows = std::make_shared<std::vector<ALCVarRow>>();
    const bool read = mDatabase.postAndWait([rows](ALDbConnection& connection) {
        rows->clear();
        ALDbStatement read =
            connection.prepare("SELECT name, type, unit, decl, value, tombstone, modified FROM settings");
        while (read.step())
        {
            ALCVarRow row;
            row.name      = std::string(read.columnText(0));
            row.type      = S32(read.columnInt(1));
            row.unit      = read.columnNull(2) ? std::string() : std::string(read.columnText(2));
            row.decl      = U64(read.columnInt(3));
            row.value     = columnValue(read, 4, row.type);
            row.tombstone = read.columnInt(5) != 0;
            row.modified  = read.columnInt(6);
            rows->push_back(std::move(row));
        }
    });
    if (!read)
    {
        return std::nullopt;
    }
    return std::move(*rows);
}

std::optional<boost::unordered_flat_map<std::string, std::string>> ALCVarFile::readMeta()
{
    auto meta = std::make_shared<boost::unordered_flat_map<std::string, std::string>>();
    const bool read = mDatabase.postAndWait([meta](ALDbConnection& connection) {
        meta->clear();
        ALDbStatement read = connection.prepare("SELECT key, value FROM meta");
        while (read.step())
        {
            meta->insert_or_assign(std::string(read.columnText(0)),
                                   read.columnNull(1) ? std::string() : std::string(read.columnText(1)));
        }
    });
    if (!read)
    {
        return std::nullopt;
    }
    return std::move(*meta);
}

namespace
{
    // One transaction of rows, prior rows, history and meta. Keyed by name,
    // so that a batch tried again whole writes the same. Each statement is
    // found in the connection's cache once, not a row: every column is
    // bound again for each row, so its reset keeps nothing stale.
    void put(ALDbConnection& connection, const std::vector<ALCVarRow>& rows, const std::vector<ALCVarRow>& prior,
             const std::vector<ALCVarHistory>& history, const std::vector<std::pair<std::string, std::string>>& meta,
             bool written)
    {
        ALDbStatement upsert = connection.prepare(
            "INSERT INTO settings(name, type, unit, decl, value, tombstone, modified) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) "
            "ON CONFLICT(name) DO UPDATE SET type = excluded.type, unit = excluded.unit, decl = excluded.decl, "
            "value = excluded.value, tombstone = excluded.tombstone, modified = excluded.modified");
        std::optional<ALDbStatement> drop;
        if (written)
        {
            drop.emplace(connection.prepare("DELETE FROM prior WHERE name = ?1"));
        }
        for (const ALCVarRow& row : rows)
        {
            upsert.bind(1, std::string_view(row.name)).bind(2, row.type);
            if (row.unit.empty())
            {
                upsert.bindNull(3);
            }
            else
            {
                upsert.bind(3, std::string_view(row.unit));
            }
            upsert.bind(4, S64(row.decl));
            ALCVarFile::bindValue(upsert, 5, row.tombstone ? ALCVarValue() : row.value);
            upsert.bind(6, row.tombstone).bind(7, row.modified).run();
            upsert.reset();
            if (drop)
            {
                drop->bind(1, std::string_view(row.name)).run();
                drop->reset();
            }
        }
        ALDbStatement keep = connection.prepare(
            "INSERT OR REPLACE INTO prior(name, decl, type, unit, value, modified) VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
        for (const ALCVarRow& row : prior)
        {
            keep.bind(1, std::string_view(row.name)).bind(2, S64(row.decl)).bind(3, row.type);
            if (row.unit.empty())
            {
                keep.bindNull(4);
            }
            else
            {
                keep.bind(4, std::string_view(row.unit));
            }
            ALCVarFile::bindValue(keep, 5, row.value);
            keep.bind(6, row.modified).run();
            keep.reset();
        }
        ALDbStatement append =
            connection.prepare("INSERT INTO history(at, session, name, kind, old, new) VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
        for (const ALCVarHistory& entry : history)
        {
            append.bind(1, entry.at).bind(2, entry.session).bind(3, std::string_view(entry.name)).bind(4, entry.kind);
            ALCVarFile::bindValue(append, 5, entry.before ? *entry.before : ALCVarValue());
            ALCVarFile::bindValue(append, 6, entry.after ? *entry.after : ALCVarValue());
            append.run();
            append.reset();
        }
        if (!history.empty())
        {
            ALDbStatement trim = connection.prepare(
                "DELETE FROM history WHERE rowid <= (SELECT max(rowid) FROM history) - ?1");
            trim.bind(1, ALCVarFile::HISTORY_ROWS).run();
        }
        ALDbStatement set = connection.prepare("INSERT INTO meta(key, value) VALUES (?1, ?2) "
                                               "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
        for (const auto& [key, value] : meta)
        {
            set.bind(1, std::string_view(key)).bind(2, std::string_view(value)).run();
            set.reset();
        }
    }
}

bool ALCVarFile::write(std::vector<ALCVarRow> rows, std::vector<ALCVarHistory> history,
                       std::vector<std::pair<std::string, std::string>> meta, ALDatabase::Completion completed)
{
    ALDatabase::Job job = [rows = std::move(rows), history = std::move(history),
                           meta = std::move(meta)](ALDbConnection& connection) {
        put(connection, rows, {}, history, meta, true);
    };
    return completed ? mDatabase.post(std::move(job), std::move(completed)) : mDatabase.post(std::move(job));
}

bool ALCVarFile::bringForward(std::vector<ALCVarRow> rows, std::vector<ALCVarRow> prior,
                              std::vector<ALCVarHistory> history, std::vector<std::pair<std::string, std::string>> meta)
{
    return mDatabase.post([rows = std::move(rows), prior = std::move(prior), history = std::move(history),
                           meta = std::move(meta)](ALDbConnection& connection) {
        put(connection, rows, prior, history, meta, false);
    });
}

std::optional<std::vector<ALCVarRow>> ALCVarFile::readPrior()
{
    auto rows = std::make_shared<std::vector<ALCVarRow>>();
    const bool read = mDatabase.postAndWait([rows](ALDbConnection& connection) {
        rows->clear();
        ALDbStatement read = connection.prepare("SELECT name, decl, type, unit, value, modified FROM prior");
        while (read.step())
        {
            ALCVarRow row;
            row.name     = std::string(read.columnText(0));
            row.decl     = U64(read.columnInt(1));
            row.type     = S32(read.columnInt(2));
            row.unit     = read.columnNull(3) ? std::string() : std::string(read.columnText(3));
            row.value    = columnValue(read, 4, row.type);
            row.modified = read.columnInt(5);
            rows->push_back(std::move(row));
        }
    });
    if (!read)
    {
        return std::nullopt;
    }
    return std::move(*rows);
}

bool ALCVarFile::flush()
{
    return mDatabase.flush();
}

LLCoros::Future<bool> ALCVarFile::flushed()
{
    // The promise goes with the job: the future may be let go of, and the
    // writer may tell it after this file is gone.
    auto                  done   = std::make_shared<LLCoros::Promise<bool>>();
    LLCoros::Future<bool> result = done->get_future();
    if (!mDatabase.post([](ALDbConnection&) {}, [done](bool committed) { done->set_value(committed); }))
    {
        done->set_value(false);
    }
    return result;
}

bool ALCVarFile::close()
{
    return mDatabase.close();
}
