/**
 * @file albulkrename.cpp
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

#include "llviewerprecompiledheaders.h"

#include "albulkrename.h"

#include <ctime>

std::string ALBulkRename::apply(const std::string& name, int index, int total, std::time_t when) const
{
    if (mode == Mode::Format)
    {
        return applyFormat(name, index, total, when);
    }

    if (text.empty())
    {
        return name;
    }

    if (mode == Mode::Add)
    {
        return position == Position::Before ? text + name : name + text;
    }

    if (mode == Mode::Replace)
    {
        return replaceAll(name, text, with);
    }

    LL_WARNS("ALBulkRename") << "Unknown mode: " << static_cast<int>(mode) << LL_ENDL;
    return name;
}

std::string ALBulkRename::applyFormat(const std::string& name, int index, int total, std::time_t when) const
{
    // Format: discard the original name; place date or counter before or after text.
    std::string addition;
    switch (format_kind)
    {
        case FormatKind::Count:
        {
            addition = paddedCounter(index, total);
            break;
        }
        case FormatKind::Index:
        {
            addition = std::to_string(counter_start + index);
            break;
        }
        case FormatKind::AcquiredDate:
        case FormatKind::CurrentDate:
        {
            if (format_kind == FormatKind::AcquiredDate && when == 0)
            {
                return name;
            }
            addition = formatTime(format_kind == FormatKind::AcquiredDate ? when : std::time(nullptr), date_format);
            break;
        }
        case FormatKind::Custom:
        {
            if(text.empty())
            {
                return name;
            }
            addition = replaceAll(text, "$i", std::to_string(counter_start + index));
            addition = replaceAll(addition, "$c", paddedCounter(index, total));
            addition = replaceAll(addition, "$a", formatTime(when, "%Y-%m-%d"));
            addition = replaceAll(addition, "$t", formatTime(when, "%H:%M:%S"));
            addition = formatTime(std::time(nullptr), addition);
            // $n replaced last to prevent text in previous name being treated as tokens for replacement
            return replaceAll(addition, "$n", name);
        }
        default:
            LL_WARNS("ALBulkRename") << "Unknown format kind: " << static_cast<int>(format_kind) << LL_ENDL;
            return name;
    }

    return position == Position::Before ? addition + text : text + addition;
}

std::string ALBulkRename::paddedCounter(int index, int total) const
{
    const long long value = static_cast<long long>(counter_start) + index;
    const long long count = total > 0 ? static_cast<long long>(total) : 1;
    const long long last  = static_cast<long long>(counter_start) + count - 1;
    std::string     result = std::to_string(value);
    if (value >= 0)
    {
        const long long widest = last > value ? last : value;
        const auto      width  = std::to_string(widest).size();
        if (result.size() < width)
        {
            result.insert(0, width - result.size(), '0');
        }
    }
    return result;
}

std::string ALBulkRename::formatTime(std::time_t stamp, const std::string& format)
{
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &stamp);
#else
    localtime_r(&stamp, &local);
#endif
    char buffer[64] = {};
    if (std::strftime(buffer, sizeof(buffer), format.c_str(), &local) == 0)
    {
        return std::string();
    }
    return buffer;
}

std::string ALBulkRename::replaceAll(const std::string& text, const std::string& from, const std::string& to)
{
    if (from.empty())
    {
        return text;
    }

    std::string result;
    size_t      start = 0;
    for (size_t found = text.find(from); found != std::string::npos; found = text.find(from, start))
    {
        result.append(text, start, found - start);
        result += to;
        start = found + from.size();
    }
    result.append(text, start, std::string::npos);
    return result;
}
