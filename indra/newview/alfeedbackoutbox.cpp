/**
 * @file alfeedbackoutbox.cpp
 * @brief Feedback reports kept on disk until the server has answered them
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

#include "alfeedbackoutbox.h"

#include "fsyspath.h"
#include "lldir.h"
#include "llfile.h"
#include "llcoros.h"
#include "llstring.h"
#include "lluuid.h"

#include <algorithm>
#include <filesystem>

namespace
{
    constexpr std::string_view BODY = ".envelope";
    constexpr std::string_view RECORD = ".json";
    constexpr std::string_view CLAIMED = ".sending";
    constexpr std::string_view PARTIAL = ".tmp";

    // One process's threads and coroutines take turns; other processes are
    // kept apart by the renames. A coroutine's mutex: the sends that use it
    // run in coroutines, and the bodies are kept from a worker thread.
    LLCoros::Mutex& outbox_mutex()
    {
        static LLCoros::Mutex mutex;
        return mutex;
    }

    // When a file was last written, in seconds since the epoch; 0 when it is
    // not there.
    S64 written_at(const std::string& path)
    {
        llstat status;
        return LLFile::stat(path, &status) == 0 ? static_cast<S64>(status.st_mtime) : 0;
    }

    // A file a run left behind this long ago, and not since touched.
    bool abandoned(const std::string& path, S64 now)
    {
        const S64 written = written_at(path);
        return written != 0 && now - written > ALFeedbackOutbox::ORPHAN_SECONDS;
    }

    bool oldest_first(const ALFeedback::Queued& lhs, const ALFeedback::Queued& rhs)
    {
        return lhs.created < rhs.created;
    }
}

ALFeedbackOutbox::ALFeedbackOutbox(std::string dir) : mDir(std::move(dir))
{
}

// static
bool ALFeedbackOutbox::replaceFile(const std::string& path, std::string_view data)
{
    const std::string partial = path + "." + LLUUID::generateNewID().asString() + std::string(PARTIAL);
    std::error_code ec;
    {
        LLFile file(partial, LLFile::out | LLFile::trunc | LLFile::binary, ec);
        if (ec || file.write(data.data(), static_cast<S64>(data.size()), ec) != static_cast<S64>(data.size()) || ec)
        {
            file.close();
            LLFile::remove(partial, ENOENT);
            return false;
        }
    }
    if (LLFile::rename(partial, path) != 0)
    {
        LLFile::remove(partial, ENOENT);
        return false;
    }
    return true;
}

std::string ALFeedbackOutbox::path(const std::string& event_id, std::string_view extension) const
{
    return gDirUtilp->add(mDir, event_id + std::string(extension));
}

std::optional<ALFeedback::Queued> ALFeedbackOutbox::readRecord(const std::string& event_id) const
{
    std::optional<ALFeedback::Queued> record = ALFeedback::queuedFromJson(LLFile::getContents(path(event_id, RECORD)));
    // A record names its own file; one that does not is not this report's.
    if (record && record->eventId != event_id)
    {
        return std::nullopt;
    }
    return record;
}

bool ALFeedbackOutbox::keep(const ALFeedback::Queued& report, std::string_view body, S64 now,
                            std::vector<ALFeedback::Queued>& given_up)
{
    if (!ALFeedback::validEventId(report.eventId))
    {
        return false;
    }
    LLCoros::LockType lock(outbox_mutex());
    LLFile::mkdir(mDir);

    std::vector<ALFeedback::Queued> records = scan(now, given_up);
    size_t others = std::count_if(records.begin(), records.end(),
                                  [&](const ALFeedback::Queued& record) { return record.eventId != report.eventId; });
    for (const ALFeedback::Queued& record : records)
    {
        if (others < ALFeedback::QUEUE_MAX_ENTRIES)
        {
            break;
        }
        if (record.eventId != report.eventId)
        {
            LL_WARNS("Feedback") << "Too many reports kept; giving up " << record.eventId << LL_ENDL;
            forgetLocked(record.eventId);
            given_up.push_back(record);
            --others;
        }
    }

    // The record first: a body without one would be nobody's, while a record
    // without its body yet is a report still being kept.
    const bool had_record = readRecord(report.eventId).has_value();
    if (!had_record)
    {
        ALFeedback::Queued record = report;
        record.created = now;
        record.attempts = 0;
        record.nextAt = now + ALFeedback::QUEUE_RETRY_SECONDS;
        record.notBefore = 0;
        if (!replaceFile(path(report.eventId, RECORD), ALFeedback::queuedToJson(record)))
        {
            return false;
        }
    }
    if (!replaceFile(path(report.eventId, BODY), body))
    {
        if (!had_record)
        {
            LLFile::remove(path(report.eventId, RECORD), ENOENT);
        }
        return false;
    }
    return true;
}

std::vector<ALFeedback::Queued> ALFeedbackOutbox::records(S64 now, std::vector<ALFeedback::Queued>& given_up)
{
    LLCoros::LockType lock(outbox_mutex());
    return scan(now, given_up);
}

std::vector<ALFeedback::Queued> ALFeedbackOutbox::scan(S64 now, std::vector<ALFeedback::Queued>& given_up)
{
    std::vector<ALFeedback::Queued> records;
    if (!LLFile::isdir(mDir))
    {
        return records;
    }

    std::vector<std::string> record_ids;
    std::vector<std::string> stray;
    for (const std::string& name : gDirUtilp->getFilesInDir(mDir))
    {
        std::string_view extension;
        for (std::string_view known : { BODY, RECORD, CLAIMED })
        {
            if (LLStringUtil::endsWith(name, std::string(known)))
            {
                extension = known;
            }
        }
        const std::string event_id = name.substr(0, name.size() - extension.size());
        if (extension == RECORD && ALFeedback::validEventId(event_id))
        {
            record_ids.push_back(event_id);
        }
        else if (extension.empty() || !ALFeedback::validEventId(event_id) || !LLFile::isfile(path(event_id, RECORD)))
        {
            // Half-written, or what a run left of a report it was forgetting.
            stray.push_back(name);
        }
    }

    for (const std::string& name : stray)
    {
        const std::string file = gDirUtilp->add(mDir, name);
        if (abandoned(file, now))
        {
            LLFile::remove(file, ENOENT);
        }
    }

    for (const std::string& event_id : record_ids)
    {
        const std::string record_file = path(event_id, RECORD);
        const std::optional<ALFeedback::Queued> record = readRecord(event_id);
        const std::string claimed_file = path(event_id, CLAIMED);
        const bool has_body = LLFile::isfile(path(event_id, BODY));
        const bool has_claim = LLFile::isfile(claimed_file);
        if (!record || (!has_body && !has_claim))
        {
            // Not readable, or its body never came: given time, a run still
            // keeping it would have finished.
            if (abandoned(record_file, now))
            {
                forgetLocked(event_id);
            }
            continue;
        }
        // A claim is touched when it is made: an old one is a run that
        // ended mid-send, and the report is free again.
        if (has_claim && !has_body)
        {
            const S64 claimed_at = written_at(claimed_file);
            if (claimed_at != 0 && now - claimed_at > CLAIM_STALE_SECONDS)
            {
                LLFile::rename(claimed_file, path(event_id, BODY), ENOENT);
            }
        }
        if (ALFeedback::expired(*record, now))
        {
            LL_WARNS("Feedback") << "Giving up feedback " << event_id << " after " << record->attempts << " tries"
                                 << LL_ENDL;
            forgetLocked(event_id);
            given_up.push_back(*record);
            continue;
        }
        records.push_back(*record);
    }
    std::sort(records.begin(), records.end(), oldest_first);
    return records;
}

bool ALFeedbackOutbox::has(const std::string& event_id) const
{
    if (!ALFeedback::validEventId(event_id))
    {
        return false;
    }
    LLCoros::LockType lock(outbox_mutex());
    return LLFile::isfile(path(event_id, RECORD))
           && (LLFile::isfile(path(event_id, BODY)) || LLFile::isfile(path(event_id, CLAIMED)));
}

bool ALFeedbackOutbox::claim(const std::string& event_id)
{
    if (!ALFeedback::validEventId(event_id))
    {
        return false;
    }
    LLCoros::LockType lock(outbox_mutex());
    const std::string claimed_file = path(event_id, CLAIMED);
    // A run that loses the race finds nothing to rename.
    if (LLFile::rename(path(event_id, BODY), claimed_file, ENOENT) != 0)
    {
        return false;
    }
    // A rename keeps the body's time; the claim's age is from now.
    std::error_code ec;
    std::filesystem::last_write_time(fsyspath(claimed_file), std::filesystem::file_time_type::clock::now(), ec);
    return true;
}

std::string ALFeedbackOutbox::claimedBody(const std::string& event_id) const
{
    if (!ALFeedback::validEventId(event_id))
    {
        return std::string();
    }
    return LLFile::getContents(path(event_id, CLAIMED));
}

void ALFeedbackOutbox::release(const std::string& event_id)
{
    if (!ALFeedback::validEventId(event_id))
    {
        return;
    }
    LLCoros::LockType lock(outbox_mutex());
    LLFile::rename(path(event_id, CLAIMED), path(event_id, BODY), ENOENT);
}

ALFeedbackOutbox::Settled ALFeedbackOutbox::settle(const std::string& event_id, bool claimed,
                                                   ALFeedback::Outcome outcome, F32 retry_after, S64 now)
{
    Settled settled;
    if (!ALFeedback::validEventId(event_id))
    {
        return settled;
    }
    LLCoros::LockType lock(outbox_mutex());
    if (!ALFeedback::retryable(outcome))
    {
        forgetLocked(event_id);
        return settled;
    }

    if (claimed)
    {
        LLFile::rename(path(event_id, CLAIMED), path(event_id, BODY), ENOENT);
    }
    // Forgotten while it went: the user discarded it, or another run gave it
    // up or sent it.
    if (!LLFile::isfile(path(event_id, BODY)) && !LLFile::isfile(path(event_id, CLAIMED)))
    {
        return settled;
    }

    const std::optional<ALFeedback::Queued> existing = readRecord(event_id);
    ALFeedback::Queued record = existing.value_or(ALFeedback::Queued{ event_id, now, 0, now });
    ++record.attempts;
    record.nextAt = ALFeedback::nextAttempt(now, record.attempts, retry_after);
    const S64 wait = ALFeedback::serverWait(retry_after);
    record.notBefore = wait > 0 ? now + wait : 0;
    if (ALFeedback::expired(record, now))
    {
        LL_WARNS("Feedback") << "Giving up feedback " << event_id << " after " << record.attempts << " tries"
                             << LL_ENDL;
        forgetLocked(event_id);
        settled.givenUp = record;
        return settled;
    }
    // A record not written leaves the one there was, which still counts.
    settled.kept = replaceFile(path(event_id, RECORD), ALFeedback::queuedToJson(record)) || existing.has_value();
    if (!settled.kept)
    {
        forgetLocked(event_id);
    }
    return settled;
}

void ALFeedbackOutbox::forget(const std::string& event_id)
{
    if (!ALFeedback::validEventId(event_id))
    {
        return;
    }
    LLCoros::LockType lock(outbox_mutex());
    forgetLocked(event_id);
}

void ALFeedbackOutbox::forgetLocked(const std::string& event_id)
{
    // The record last: until it goes, what is left is still known as this
    // report's.
    for (std::string_view extension : { BODY, CLAIMED, RECORD })
    {
        LLFile::remove(path(event_id, extension), ENOENT);
    }
}

void ALFeedbackOutbox::forgetAll()
{
    LLCoros::LockType lock(outbox_mutex());
    if (!LLFile::isdir(mDir))
    {
        return;
    }
    for (const std::string& name : gDirUtilp->getFilesInDir(mDir))
    {
        LLFile::remove(gDirUtilp->add(mDir, name), ENOENT);
    }
}
