/**
 * @file alscriptstudioservices.h
 * @brief What every unit split out of the Script Studio's window asks of it.
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

#include "alscriptworkspace.h"
#include "llstring.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ALScriptStudioDoc;
class LLUICtrl;

// What a unit split out of the Script Studio's window asks of the window,
// whatever the unit is: to say something, to find a tab, and to go
// somewhere. What else a unit needs is an interface of its own, beside
// this one. The window implements it; a unit's test fakes it.
class ALScriptStudioServices
{
public:
    // --- saying ------------------------------------------------------------------

    // What the studio did: said in the status line and kept in the Output
    // tab, the tab's name a link to it, and after it a link for each thing
    // to be done about it -- save_anyway, retry, copy, export.
    virtual void report(const std::string& text, bool failure = false, const ALScriptStudioDoc* doc = nullptr,
                        const std::vector<std::string>& actions = {}) = 0;
    // The status line alone.
    virtual void setStatus(const std::string& text, bool failure = false) = 0;
    // One of the window's words, with its blanks filled in; and one in the
    // form its count takes in the viewer's language, [COUNT] filled in too.
    virtual std::string words(const std::string& name, const LLStringUtil::format_map_t& args = LLStringUtil::format_map_t()) const = 0;
    virtual std::string counted(const char* name, S32 count, LLStringUtil::format_map_t args = LLStringUtil::format_map_t()) const = 0;

    // --- the tabs ----------------------------------------------------------------

    // The tab in front, and a tab by its id or by what it holds; null where
    // there is none.
    virtual ALScriptStudioDoc* frontDoc()                        = 0;
    virtual ALScriptStudioDoc* findDoc(std::string_view id)      = 0;
    virtual ALScriptStudioDoc* findDoc(const ALScriptRef& ref) = 0;

    // --- going places ------------------------------------------------------------

    // A script opened in a tab, or brought forward where one has it; with
    // text carried from elsewhere, put in place of the server's once that
    // has loaded; the caret at a line; the keyboard given to it or not.
    virtual void openScript(const ALScriptRef& ref, const std::string& name, std::optional<std::string> carried = std::nullopt, S32 line = -1,
                            bool focus = true) = 0;
    // A place in a script, gone to once the script is open, or now.
    virtual void goToPlace(const ALScriptRef& ref, const std::string& name, S32 line, S32 column, S32 length) = 0;
    // A list's row chosen and its place shown: the keyboard left in the
    // list to walk on, or, `to_editor`, taken to the script to type there.
    virtual void revealed(LLUICtrl* list, bool to_editor) = 0;

protected:
    ~ALScriptStudioServices() = default;
};
