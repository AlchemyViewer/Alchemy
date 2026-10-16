/**
 * @file alfeedbackoutbox.h
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

#ifndef AL_ALFEEDBACKOUTBOX_H
#define AL_ALFEEDBACKOUTBOX_H

#include "alfeedback.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Each report as the body that was posted, beside a record of its tries,
// until the server has answered it. A body being sent is renamed, so no
// other run sends it at the same time; files are written beside their name
// and renamed over it, so a reader finds them whole. Any thread may use it,
// and two viewers may share the directory.
class ALFeedbackOutbox
{
public:
    explicit ALFeedbackOutbox(std::string dir);

    const std::string& dir() const { return mDir; }

    // Keeps a report's body. A report kept already keeps its record, its
    // tries so far; a new one is first due a quarter of an hour after now.
    // The oldest others are given up to make room, and added to given_up.
    // False when it could not be kept.
    bool keep(const ALFeedback::Queued& report, std::string_view body, S64 now,
              std::vector<ALFeedback::Queued>& given_up);

    // The kept reports, oldest first. On the way it tidies up after other
    // runs: what one left half-written, and a claim whose run ended
    // mid-send. Reports too old or tried too often are given up, and added
    // to given_up.
    std::vector<ALFeedback::Queued> records(S64 now, std::vector<ALFeedback::Queued>& given_up);
    bool has(const std::string& event_id) const;

    // Takes a kept report to send it, so no other run does. False when
    // another run has it, or it is gone.
    bool claim(const std::string& event_id);
    // A claimed report's body; empty when it cannot be read.
    std::string claimedBody(const std::string& event_id) const;
    // A claimed report goes back unanswered, for a later run.
    void release(const std::string& event_id);

    struct Settled
    {
        // Kept to go again later.
        bool kept = false;
        // Given up instead: tried too often, or kept too long.
        std::optional<ALFeedback::Queued> givenUp;
    };
    // What the server answered for a report, claimed or not: gone if it took
    // the report or never will, kept for later if it could not be reached.
    Settled settle(const std::string& event_id, bool claimed, ALFeedback::Outcome outcome, F32 retry_after, S64 now);

    void forget(const std::string& event_id);
    void forgetAll();

    // A claim this old is a run that ended mid-send.
    static constexpr S64 CLAIM_STALE_SECONDS = 60 * 60;
    // What a run left half-written this long ago is tidied away.
    static constexpr S64 ORPHAN_SECONDS = 10 * 60;

    // Writes a file whole: beside it, then renamed over it.
    static bool replaceFile(const std::string& path, std::string_view data);

private:
    std::string path(const std::string& event_id, std::string_view extension) const;
    std::optional<ALFeedback::Queued> readRecord(const std::string& event_id) const;
    std::vector<ALFeedback::Queued> scan(S64 now, std::vector<ALFeedback::Queued>& given_up);
    void forgetLocked(const std::string& event_id);

    std::string mDir;
};

#endif // AL_ALFEEDBACKOUTBOX_H
