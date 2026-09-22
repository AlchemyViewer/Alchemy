/**
 * @file alscriptsnippets.cpp
 * @brief The snippets Script Studio offers: the viewer's, and the scripter's own.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptsnippets.h"

#include "lldir.h"
#include "llfile.h"
#include "llsdserialize.h"

#include <sstream>

namespace ALScriptSnippets
{
    namespace
    {
        std::vector<Snippet> sAll[2];
        bool                 sLoaded[2] = { false, false };

        std::string fileName(bool lua)
        {
            return std::string("snippets") + gDirUtilp->getDirDelimiter() + (lua ? "slua.xml" : "lsl.xml");
        }

        void readInto(const std::string& file, bool builtin, std::vector<Snippet>& out)
        {
            llifstream in(file.c_str());
            if (!in.is_open())
            {
                return;
            }
            LLSD list;
            if (LLSDSerialize::fromXML(list, in) == LLSDParser::PARSE_FAILURE || !list.isArray())
            {
                LL_WARNS("ScriptStudio") << "The snippets at " << file << " could not be read" << LL_ENDL;
                return;
            }
            for (LLSD::array_const_iterator it = list.beginArray(); it != list.endArray(); ++it)
            {
                Snippet one;
                one.name    = (*it)["name"].asString();
                one.prefix  = (*it)["prefix"].asString();
                one.detail  = (*it)["detail"].asString();
                one.body    = (*it)["body"].asString();
                one.builtin = builtin;
                if (!one.name.empty() && !one.body.empty())
                {
                    out.push_back(std::move(one));
                }
            }
        }
    }

    std::string path(bool lua)
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, fileName(lua));
    }

    const std::vector<Snippet>& all(bool lua)
    {
        std::vector<Snippet>& out = sAll[lua ? 1 : 0];
        if (!sLoaded[lua ? 1 : 0])
        {
            sLoaded[lua ? 1 : 0] = true;
            out.clear();
            readInto(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, fileName(lua)), true, out);
            readInto(path(lua), false, out);
        }
        return out;
    }

    std::vector<Snippet> own(bool lua)
    {
        std::vector<Snippet> out;
        readInto(path(lua), false, out);
        return out;
    }

    bool saveOwn(bool lua, const std::vector<Snippet>& snippets)
    {
        LLSD list = LLSD::emptyArray();
        for (const Snippet& one : snippets)
        {
            // One without a name or a body is one being written, and is
            // kept in the tab rather than in the file.
            if (one.builtin || one.name.empty() || one.body.empty())
            {
                continue;
            }
            LLSD entry;
            entry["name"]   = one.name;
            entry["prefix"] = one.prefix;
            entry["detail"] = one.detail;
            entry["body"]   = one.body;
            list.append(entry);
        }
        std::ostringstream xml;
        LLSDSerialize::toPrettyXML(list, xml);
        // How they are written, for whoever opens the file.
        std::string text = xml.str();
        const size_t declared = text.find("?>");
        const std::string note = "\n<!-- Your own snippets, offered beside the viewer's: each a map with a name,\n"
                                 "     the prefix completion offers it under, a line of detail, and the body,\n"
                                 "     where ${1:text}, ${2} and $1 are the places Tab goes through in order\n"
                                 "     and $0 is where the caret ends. The preferences' Snippets tab edits these. -->";
        text.insert(declared == std::string::npos ? 0 : declared + 2, note);
        LLFile::mkdir(gDirUtilp->getDirName(path(lua)));
        llofstream out(path(lua).c_str(), std::ios::binary);
        out << text;
        const bool written = out.good();
        out.close();
        forget(lua);
        return written;
    }

    std::string fileText(bool lua, bool& exists)
    {
        exists = LLFile::isfile(path(lua));
        if (!exists)
        {
            return std::string();
        }
        llifstream        in(path(lua).c_str(), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    void restoreFileText(bool lua, const std::string& text, bool existed)
    {
        if (!existed)
        {
            LLFile::remove(path(lua));
        }
        else
        {
            llofstream out(path(lua).c_str(), std::ios::binary);
            out << text;
        }
        forget(lua);
    }

    void forget(bool lua)
    {
        sLoaded[lua ? 1 : 0] = false;
    }
}
