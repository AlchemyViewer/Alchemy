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

#include "linden_common.h"

#include "alscriptsnippets.h"

#include "alfilewrite.h"

#include "lldir.h"
#include "llfile.h"
#include "llsdserialize.h"

#include <algorithm>
#include <sstream>

namespace ALScriptSnippets
{
    namespace
    {
        std::vector<Snippet> sAll[2];
        bool                 sLoaded[2] = { false, false };
        std::vector<Snippet> sFollowed[2];

        std::string fileName(bool lua)
        {
            return std::string("snippets") + gDirUtilp->getDirDelimiter() + (lua ? "slua.xml" : "lsl.xml");
        }

        // A backup's name beside a file: the first of `.unreadable`,
        // `.unreadable.1` and on that is not taken.
        std::string asideOf(const std::string& file)
        {
            std::string aside = file + ".unreadable";
            for (int n = 1; LLFile::isfile(aside); ++n)
            {
                aside = file + ".unreadable." + std::to_string(n);
            }
            return aside;
        }

        // The snippets an LLSD array holds, added to `out`: each map with a
        // name and a body, into `out` or, where it says a language, the
        // list for that one.
        void snippetsIn(const LLSD& list, bool builtin, std::vector<Snippet>& out, std::vector<Snippet>* lsl = nullptr,
                        std::vector<Snippet>* slua = nullptr)
        {
            for (LLSD::array_const_iterator it = list.beginArray(); it != list.endArray(); ++it)
            {
                Snippet one;
                one.name    = (*it)["name"].asString();
                one.prefix  = (*it)["prefix"].asString();
                one.detail  = (*it)["detail"].asString();
                one.body    = (*it)["body"].asString();
                one.builtin = builtin;
                if (one.name.empty() || one.body.empty())
                {
                    continue;
                }
                const std::string language = (*it)["language"].asString();
                std::vector<Snippet>& into = language == "lsl" && lsl ? *lsl : language == "slua" && slua ? *slua : out;
                into.push_back(std::move(one));
            }
        }

        // The array a file or a notecard holds: each snippet with a name and
        // a body, and the language where one is given.
        void appendTo(LLSD& list, const std::vector<Snippet>& snippets, const char* language)
        {
            for (const Snippet& one : snippets)
            {
                // One without a name or a body is one being written, and is
                // kept in the tab rather than in the file.
                if (one.builtin || one.followed || one.name.empty() || one.body.empty())
                {
                    continue;
                }
                LLSD entry;
                entry["name"]   = one.name;
                entry["prefix"] = one.prefix;
                entry["detail"] = one.detail;
                entry["body"]   = one.body;
                if (language)
                {
                    entry["language"] = language;
                }
                list.append(entry);
            }
        }

        // The array as XML, with a note after the declaration for whoever
        // opens it.
        std::string xmlOf(const LLSD& list, const std::string& note)
        {
            std::ostringstream xml;
            LLSDSerialize::toPrettyXML(list, xml);
            std::string  text     = xml.str();
            const size_t declared = text.find("?>");
            text.insert(declared == std::string::npos ? 0 : declared + 2, note);
            return text;
        }
    }

    bool readFrom(const std::string& file, bool builtin, std::vector<Snippet>& out)
    {
        llifstream in(file.c_str());
        if (!in.is_open())
        {
            // No file is no snippets, which is nothing wrong.
            return !LLFile::isfile(file);
        }
        LLSD list;
        if (LLSDSerialize::fromXML(list, in) == LLSDParser::PARSE_FAILURE || !list.isArray())
        {
            LL_WARNS("ScriptStudio") << "The snippets at " << file << " could not be read" << LL_ENDL;
            return false;
        }
        snippetsIn(list, builtin, out);
        return true;
    }

    bool writeTo(const std::string& file, const std::vector<Snippet>& snippets)
    {
        // A file there already that does not read as snippets is somebody's
        // work all the same -- written by hand, and wrong by a comma -- and
        // the tab could make nothing of it: kept beside, rather than
        // written over with that nothing.
        std::vector<Snippet> ignored;
        if (LLFile::isfile(file) && !readFrom(file, false, ignored))
        {
            const std::string aside = asideOf(file);
            if (LLFile::rename(file, aside) != 0)
            {
                LL_WARNS("ScriptStudio") << "The snippets at " << file << " could not be read, nor kept aside; not written over" << LL_ENDL;
                return false;
            }
            LL_INFOS("ScriptStudio") << "The snippets at " << file << " could not be read, and are kept as " << aside << LL_ENDL;
        }
        LLSD list = LLSD::emptyArray();
        appendTo(list, snippets, nullptr);
        // How they are written, for whoever opens the file.
        const std::string note = "\n<!-- Your own snippets, offered beside the viewer's: each a map with a name,\n"
                                 "     the prefix completion offers it under, a line of detail, and the body,\n"
                                 "     where ${1:text}, ${2} and $1 are the places Tab goes through in order\n"
                                 "     and $0 is where the caret ends. The preferences' Snippets tab edits these. -->";
        // Whole or not at all: beside it, then in its place.
        return ALFileWrite::whole(file, xmlOf(list, note));
    }

    std::string notecardText(const std::vector<Snippet>& lsl, const std::vector<Snippet>& slua)
    {
        LLSD list = LLSD::emptyArray();
        appendTo(list, lsl, "lsl");
        appendTo(list, slua, "slua");
        const std::string note = "\n<!-- Script Studio snippets. Drop this notecard on the Snippets tab of Script\n"
                                 "     Studio's preferences to add them to your own. -->";
        return xmlOf(list, note);
    }

    bool readNotecard(const std::string& text, bool lua, std::vector<Snippet>& lsl, std::vector<Snippet>& slua)
    {
        std::istringstream in(text);
        LLSD               list;
        if (LLSDSerialize::fromXML(list, in) == LLSDParser::PARSE_FAILURE || !list.isArray())
        {
            return false;
        }
        std::vector<Snippet> read_lsl;
        std::vector<Snippet> read_slua;
        snippetsIn(list, false, lua ? read_slua : read_lsl, &read_lsl, &read_slua);
        if (read_lsl.empty() && read_slua.empty())
        {
            return false;
        }
        lsl.insert(lsl.end(), std::make_move_iterator(read_lsl.begin()), std::make_move_iterator(read_lsl.end()));
        slua.insert(slua.end(), std::make_move_iterator(read_slua.begin()), std::make_move_iterator(read_slua.end()));
        return true;
    }

    Merged merge(std::vector<Snippet>& own, const std::vector<Snippet>& incoming)
    {
        Merged     merged;
        const auto named = [&own](const std::string& name) {
            return std::find_if(own.begin(), own.end(), [&name](const Snippet& one) { return one.name == name; });
        };
        for (const Snippet& one : incoming)
        {
            // Its name, then the numbered ones, until one is free -- or
            // holds this body already, as the same notecard dropped twice
            // would find.
            for (int n = 1;; ++n)
            {
                const std::string name  = n == 1 ? one.name : one.name + " (" + std::to_string(n) + ")";
                const auto        there = named(name);
                if (there != own.end() && there->body == one.body)
                {
                    ++merged.skipped;
                    break;
                }
                if (there == own.end())
                {
                    own.push_back(one);
                    own.back().name     = name;
                    own.back().builtin  = false;
                    own.back().followed = false;
                    ++(n == 1 ? merged.added : merged.renamed);
                    break;
                }
            }
        }
        return merged;
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
            readFrom(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, fileName(lua)), true, out);
            const std::vector<Snippet>& notecard = sFollowed[lua ? 1 : 0];
            out.insert(out.end(), notecard.begin(), notecard.end());
            readFrom(path(lua), false, out);
        }
        return out;
    }

    bool follow(const std::string& notecard_text)
    {
        std::vector<Snippet> lsl, slua, unused;
        if (!notecard_text.empty() &&
            (!readNotecard(notecard_text, false, lsl, unused) || !readNotecard(notecard_text, true, unused, slua)))
        {
            return false;
        }
        for (std::vector<Snippet>* one : { &lsl, &slua })
        {
            for (Snippet& snippet : *one)
            {
                snippet.followed = true;
            }
        }
        sFollowed[0] = std::move(lsl);
        sFollowed[1] = std::move(slua);
        forget(false);
        forget(true);
        return true;
    }

    const std::vector<Snippet>& followed(bool lua)
    {
        return sFollowed[lua ? 1 : 0];
    }

    std::vector<Snippet> own(bool lua)
    {
        std::vector<Snippet> out;
        readFrom(path(lua), false, out);
        return out;
    }

    bool saveOwn(bool lua, const std::vector<Snippet>& snippets)
    {
        LLFile::mkdir(gDirUtilp->getDirName(path(lua)));
        const bool written = writeTo(path(lua), snippets);
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
            ALFileWrite::whole(path(lua), text);
        }
        forget(lua);
    }

    void forget(bool lua)
    {
        sLoaded[lua ? 1 : 0] = false;
    }
}
