/**
 * @file alxuidiagnostics.h
 * @brief Collects what the XUI parser and the widget factory would otherwise only log.
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

#include <string>
#include <string_view>
#include <vector>

// While one of these is alive, the XUI parser and the widget factory report
// to it instead of the log, and the parser also reports the failures it
// keeps quiet about when nothing is listening: an attribute of a nested
// element that no parameter answers to, and a dotted element whose leading
// token is not the element it sits in. One at a time: a build is not
// re-entrant, and neither is this.
class ALXUIDiagnostics
{
public:
    enum class Kind : U8
    {
        ParseError,         // the parser's own error path
        ParseWarning,       // the parser's own warning path
        UnknownAttribute,   // an attribute no parameter answered to
        MisScopedElement,   // a dotted element whose head is not its parent
        InvalidChild,       // a registered widget the parent's registry refuses
        CreateFailed,       // a child the factory could not build at all
    };

    struct Entry
    {
        Kind        kind;
        S32         depth;      // 0 is the widget's own attributes and children
        S32         line;       // -1 when the node carries none
        std::string file;
        std::string path;       // the parser's name stack, dot separated; the element name for factory entries
        std::string message;
    };

    ALXUIDiagnostics();
    ~ALXUIDiagnostics();

    ALXUIDiagnostics(const ALXUIDiagnostics&) = delete;
    ALXUIDiagnostics& operator=(const ALXUIDiagnostics&) = delete;

    static ALXUIDiagnostics* active() { return sActive; }

    void report(Kind kind, S32 depth, std::string_view file, S32 line, std::string_view path, std::string_view message);

    const std::vector<Entry>& entries() const { return mEntries; }
    size_t count(Kind kind) const;
    void clear() { mEntries.clear(); }

    static const char* kindName(Kind kind);

private:
    static ALXUIDiagnostics* sActive;
    std::vector<Entry>       mEntries;
};
