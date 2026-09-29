/**
 * @file altextgotoline.cpp
 * @brief Go to Line over a text: a line, or a line and a column, as it is typed
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

#include "altextgotoline.h"

#include <cctype>
#include <cstdlib>
#include <string_view>

void ALTextGoToLine::placeTyped(const std::string& text, S32& line, S32& column)
{
    line = column = 0;
    std::string_view rest(text);
    while (!rest.empty() && rest.front() == ' ')
    {
        rest.remove_prefix(1);
    }
    while (!rest.empty() && rest.front() == ':')
    {
        rest.remove_prefix(1);
    }
    size_t digits = 0;
    while (digits < rest.size() && isdigit(static_cast<unsigned char>(rest[digits])))
    {
        ++digits;
    }
    if (digits == 0)
    {
        return;
    }
    line = static_cast<S32>(std::strtol(std::string(rest.substr(0, digits)).c_str(), nullptr, 10));
    rest.remove_prefix(digits);
    if (rest.empty() || (rest.front() != ':' && rest.front() != ','))
    {
        return;
    }
    rest.remove_prefix(1);
    while (!rest.empty() && rest.front() == ' ')
    {
        rest.remove_prefix(1);
    }
    digits = 0;
    while (digits < rest.size() && isdigit(static_cast<unsigned char>(rest[digits])))
    {
        ++digits;
    }
    if (digits > 0)
    {
        column = static_cast<S32>(std::strtol(std::string(rest.substr(0, digits)).c_str(), nullptr, 10));
    }
}
