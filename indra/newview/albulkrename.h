/**
 * @file albulkrename.h
 * @brief The naming rule of the inventory's bulk rename.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Wolfgang <wolfgang@wlf.io>
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

#include <ctime>
#include <string>

struct ALBulkRename
{
    enum class Mode
    {
        Add,
        Replace,
        Format
    };
    enum class Position
    {
        Before,
        After
    };
    enum class FormatKind
    {
        CurrentDate,
        AcquiredDate,
        Count,
        Index,
        Custom
    };

    Mode        mode          = Mode::Add;
    Position    position      = Position::After;
    FormatKind  format_kind   = FormatKind::CurrentDate;
    std::string text;         // Add text, Replace find-text, or Format name.
    std::string with;         // What replaces each occurrence of text.
    std::string date_format   = "%Y-%m-%d"; // strftime pattern for Format+CurrentDate/AcquiredDate.
    int         counter_start = 1;

    // index is the counter offset (0 for the first renamed item).
    // total is the batch size. Format+Count zero-pads to the width of the last
    // number in that batch (counter_start + total - 1); Format+Index does not pad.
    // when is the acquired date, used only by Format+AcquiredDate; 0 (unknown)
    // leaves the name unchanged. Format+CurrentDate always stamps the time now.
    std::string apply(const std::string& name, int index = 0, int total = 1, std::time_t when = 0) const;

private:
    std::string applyFormat(const std::string& name, int index, int total, std::time_t when) const;

    // counter_start + index, zero-padded to the width of the last number in the
    // batch (counter_start + total - 1) so every name has the same number of
    // digits: 1..10 with a batch of 10 is 01..10. A total of 0 or less counts as 1.
    std::string paddedCounter(int index, int total) const;

    // stamp in local time, formatted by the strftime pattern `format`. Empty if
    // the result is empty or does not fit in 63 bytes.
    static std::string formatTime(std::time_t stamp, const std::string& format);

    // Every non-overlapping occurrence of `from` in `text`, swapped for `to`.
    // A replacement that itself contains `from` is left as written. An empty
    // `from` leaves `text` unchanged.
    static std::string replaceAll(const std::string& text, const std::string& from, const std::string& to);
};
